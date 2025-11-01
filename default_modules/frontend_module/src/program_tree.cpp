#include "webapp/ui/helper/program_tree.h"
#include "webapp/ui/helper/program_command.h"

#include <ranges>
#include <sstream>

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;
using namespace aergo::module;
using namespace aergo::module::helpers::usecase_tree;

ProgramTree::ProgramTree(aergo::module::BaseModule* base_module, ProgramTreeState& program_state_unsafe, std::function<void(std::function<void()>)> with_frontend_state_lock)
: base_module_(base_module),
  program_state_unsafe_(program_state_unsafe),
  with_frontend_state_lock_(with_frontend_state_lock)
{
    setStyleClass("program-tree");

    auto program_container = addWidget(std::make_unique<Wt::WContainerWidget>());
    program_container->setStyleClass("program-tree-container");

    existing_usecases_list_ = program_container->addWidget(std::make_unique<ProgramList>("PROGRAM", "program-tree-list", true));
    available_usecases_list_ = program_container->addWidget(std::make_unique<ProgramList>("AVAILABLE", "program-tree-available-commands", false));

    parameters_container_ = addWidget(std::make_unique<Wt::WStackedWidget>());
    parameters_container_->setStyleClass("program-tree-parameters-container");

    parameters_container_->addWidget(std::make_unique<Wt::WContainerWidget>()); // empty page for New Program

    reloadAvailableUsecases();
    setupCallbacks();

    if (program_state_unsafe.reading_custom_value_)
    {
        showReadCustomValueDialog();
    }

    if (program_state_unsafe.generating_command_data_json_)
    {
        showGenerateCommandDataPopup();
    }

    // setup refresh timer to update parameter widgets based on program_state_unsafe_ flags
    refresh_timer_ = addChild(std::make_unique<Wt::WTimer>());
    refresh_timer_->setInterval(std::chrono::milliseconds(100)); // 10Hz
    refresh_timer_->timeout().connect(this, &ProgramTree::onTimerRefresh);
    refresh_timer_->start();
} 


ProgramTree::~ProgramTree()
{
    if (refresh_timer_)
    {
        refresh_timer_->stop();
    }
}


void ProgramTree::reloadAvailableUsecases() // called with UI and frontend_state_ locks held
{
    showReloadUsecasePopup();
    base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::reloadAvailableUsecases: Reloading available usecases...");

    bool success = program_state_unsafe_.usecase_tree_->updateAvailableUsecases([this](bool success, const std::map<std::string, structs::AvailableUsecase>& available_usecases) {
        closeReloadUsecasePopup();

        // keep existing_usecases_list_, parameters_container_ and existing_usecase_parameter_widgets_ in sync
        available_usecases_list_->clearCommands();
        available_usecase_ids_.clear();
        existing_usecases_list_->clearCommands();
        existing_usecase_parameter_widgets_.clear();

        parameters_container_->clear();
        parameters_container_->addWidget(std::make_unique<Wt::WContainerWidget>()); // empty page for New Program
        parameters_container_->setCurrentIndex(0); // show empty page after reload

        if (!success)
        {
            displayErrorPopup("Failed to retrieve available usecases from connected modules.");
            base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::reloadAvailableUsecases: updateAvailableUsecases callback reported failure.");
            return;
        }

        for (const auto& [usecase_id, available_usecase] : available_usecases)
        {
            available_usecases_list_->addCommand(available_usecase.getUsecaseName(), ProgramCommand::Status::Normal);
            available_usecase_ids_.push_back(usecase_id);
        }        

        for (size_t i = 0; i < program_state_unsafe_.usecase_tree_->size(); ++i)
        {
            const auto& existing_usecase = (*program_state_unsafe_.usecase_tree_)[i];
            addExistingUsecase(existing_usecase);
        }

        base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::reloadAvailableUsecases: Reloaded usecases: " + std::to_string(available_usecases.size()) + " available, " + std::to_string(program_state_unsafe_.usecase_tree_->size()) + " existing.");
    });

    if (!success)
    {
        displayErrorPopup("Failed to reload available usecases from connected modules.");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::reloadAvailableUsecases: Failed to update available usecases from connected modules.");
    }
}


void ProgramTree::setupCallbacks()
{
    // callbacks hold UI lock, but NOT frontend_state_ lock; use access_program_state_ to access program state
    available_usecases_list_->onCommandClicked().connect([this](size_t index) {
        if (index >= available_usecase_ids_.size())
        {
            displayErrorPopup("Failed to add usecase: internal error.");
            base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupCallbacks: available_usecases_list_ index out of range.");
            return;
        }
        
        const std::string& usecase_id = available_usecase_ids_[index];
        with_frontend_state_lock_([this, usecase_id]() { // here we hold both UI and frontend_state_ locks
            if (!program_state_unsafe_.usecase_tree_->appendCommand(usecase_id))
            {
                displayErrorPopup("Failed to add usecase to program tree. Please reload available use-cases and try again.");
                base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupCallbacks: Failed to append usecase '" + usecase_id + "' to program tree.");
                return;
            }
            const auto& new_command = (*program_state_unsafe_.usecase_tree_)[program_state_unsafe_.usecase_tree_->size() - 1];
            addExistingUsecase(new_command);
            existing_usecases_list_->setCommandSelected(existing_usecases_list_->commandCount() - 1); // show newly added usecase
            parameters_container_->setCurrentIndex(parameters_container_->count() - 1); // show parameters of newly added usecase
        });
    });

    existing_usecases_list_->onCommandClicked().connect([this](size_t index) {
        if (index + 1 >= parameters_container_->count()) // +1 because child 0 is empty
        {
            displayErrorPopup("Failed to show usecase parameters: internal error.");
            base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupCallbacks: existing_usecases_list_ index out of range.");
            return;
        }
        parameters_container_->setCurrentIndex(index + 1); // +1 because child 0 is empty
    });
}


void ProgramTree::addExistingUsecase(const aergo::module::helpers::usecase_tree::structs::ExistingCommand& existing_usecase)
{
    // TODO this all does NOT work for insert!

    auto usecase_ref = existing_usecase.getUsecaseReference();
    if (!usecase_ref)
    {
        // keep existing_usecases_list_, parameters_container_ and existing_usecase_parameter_widgets_ in sync
        existing_usecases_list_->addCommand(existing_usecase.getUsecaseName(), ProgramCommand::Status::Invalid);
        parameters_container_->addWidget(std::make_unique<Wt::WContainerWidget>()); // empty page for invalid usecase
        existing_usecase_parameter_widgets_.push_back(nullptr);
    }
    else
    {
        // keep existing_usecases_list_, parameters_container_ and existing_usecase_parameter_widgets_ in sync
        existing_usecases_list_->addCommand(existing_usecase.getUsecaseName(), ProgramCommand::Status::Warning);
        auto params = parameters_container_->addWidget(std::make_unique<ProgramTreeParameters>(
            usecase_ref->getAutoParameters().getParameters(),
            usecase_ref->getRequiredParameters().getParameters(),
            usecase_ref->getAdvancedParameters().getParameters()
        ));
        existing_usecase_parameter_widgets_.push_back(params);

        setupParameterContainer(params, existing_usecase, existing_usecases_list_->commandCount() - 1);
    }
}


void ProgramTree::setupParameterContainer(
    ProgramTreeParameters* parameter_container, 
    const aergo::module::helpers::usecase_tree::structs::ExistingCommand& existing_usecase, 
    size_t existing_usecase_index
)
{
    parameter_container->onShowDescription().connect([this](const std::string& title, const std::string& description) {
        showParameterDescriptionPopup(title, description);
    });

    reloadCommandValues(existing_usecase_index);

    setupOnValueAddedCallback(parameter_container);
    setupOnValueRemovedCallback(parameter_container);
    setupOnValueChangedCallback(parameter_container);

    setupConfirmCallback(parameter_container);
}


void ProgramTree::setupOnValueAddedCallback(ProgramTreeParameters* parameter_container)
{
    parameter_container->onValueAdded().connect([this, parameter_container](
        ProgramTreeParameters::ParameterIndex param_index,
        value_opt_t value_opt,
        bool is_custom
    ) {
        with_frontend_state_lock_([this, parameter_container, param_index, &value_opt, is_custom]() { // here we hold both UI and frontend_state_ locks
            auto existing_usecase_index_opt = existingUsecaseIndexFromParametersWidget(parameter_container);
            auto existing_usecase = existingUsecaseFromIndex(existing_usecase_index_opt);
            if (existing_usecase == nullptr) // if not nullptr, existing_usecase_index_opt is valid too
            {
                displayErrorPopup("Failed to add parameter value: internal error.");
                base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: existingUsecaseFromParametersWidget returned nullptr.");
                return;
            }

            p_desc::ParameterValueOpt converted_value_opt;
            if (is_custom)
            {
                parameter_container->setValue(param_index.param_type, param_index.param_index, param_index.list_index, std::nullopt); // we expect new CUSTOM value to be always empty
                converted_value_opt = std::nullopt;
            }
            else
            {
                converted_value_opt = convertToParameterValueOpt(value_opt);   
            }
            if (!existing_usecase->addValue(param_index.param_type, param_index.param_index, converted_value_opt))
            {
                displayErrorPopup("Failed to add parameter value. Please try again.");
                base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: Failed to add parameter value to ExistingCommand.");
                reloadCommandValues(*existing_usecase_index_opt);
                return;
            }
            else
            {
                updateCommandStatus(*existing_usecase_index_opt);
            }
        });
    });
}


void ProgramTree::setupOnValueRemovedCallback(ProgramTreeParameters* parameter_container)
{
    parameter_container->onValueRemoved().connect([this, parameter_container](ProgramTreeParameters::ParameterIndex param_index) {
        with_frontend_state_lock_([this, parameter_container, param_index]() { // here we hold both UI and frontend_state_ locks
            auto existing_usecase_index_opt = existingUsecaseIndexFromParametersWidget(parameter_container);
            auto existing_usecase = existingUsecaseFromIndex(existing_usecase_index_opt);
            if (existing_usecase == nullptr) // if not nullptr, existing_usecase_index_opt is valid too
            {
                displayErrorPopup("Failed to remove parameter value: internal error.");
                base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: existingUsecaseFromParametersWidget returned nullptr.");
                return;
            }

            if (!existing_usecase->removeValue(param_index.param_type, param_index.param_index, param_index.list_index))
            {
                displayErrorPopup("Failed to remove parameter value. Please try again.");
                base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: Failed to remove parameter value from ExistingCommand.");
                reloadCommandValues(*existing_usecase_index_opt);
                return;
            }
            else
            {
                updateCommandStatus(*existing_usecase_index_opt);
            }
        });
    });
}


void ProgramTree::setupOnValueChangedCallback(ProgramTreeParameters* parameter_container)
{
    parameter_container->onValueChanged().connect([this, parameter_container](
        ProgramTreeParameters::ParameterIndex param_index,
        value_t value,
        bool is_custom
    ) {
        with_frontend_state_lock_([this, parameter_container, param_index, &value, is_custom]() { // here we hold both UI and frontend_state_ locks
            auto existing_usecase_index_opt = existingUsecaseIndexFromParametersWidget(parameter_container);
            auto existing_usecase = existingUsecaseFromIndex(existing_usecase_index_opt);
            if (existing_usecase == nullptr) // if not nullptr, existing_usecase_index_opt is valid too
            {
                displayErrorPopup("Failed to change parameter value: internal error.");
                base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: existingUsecaseFromParametersWidget returned nullptr.");
                return;
            }

            if (is_custom)
            {
                bool load_requested = std::get<bool>(value);
                base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::setupParameterContainer: CUSTOM parameter value change requested, load_requested = " + std::string(load_requested ? "true" : "false") + ".");
                if (load_requested) // load CUSTOM value requested
                {
                    if (program_state_unsafe_.reading_custom_value_)
                    {
                        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: Another custom value read is already in progress.");
                        return;
                    }

                    program_state_unsafe_.cancel_reading_custom_value_ = false;
                    auto program_state_unsafe_ptr = &program_state_unsafe_;
                    if (program_state_unsafe_.usecase_tree_->readCustomValue(
                        *existing_usecase_index_opt, 
                        param_index.param_index, 
                        param_index.list_index, 
                        program_state_unsafe_.cancel_reading_custom_value_, 
                        [program_state_unsafe_ptr](bool read_success, size_t existing_usecase_index) { // we already hold both UI and frontend_state_unsafe_ lock here (readCustomValue callback is called from UsecaseTree::handleResponse that is called on UI thread from FrontendModule::processResponse with frontend_state_ lock held)
                            program_state_unsafe_ptr->read_finished_ = true;
                            program_state_unsafe_ptr->read_successful_ = read_success;
                            program_state_unsafe_ptr->read_usecase_index = existing_usecase_index;
                        }
                    ))
                    {
                        program_state_unsafe_.reading_custom_value_ = true;
                        program_state_unsafe_.cancel_reading_custom_value_ = false;
                        program_state_unsafe_.read_finished_ = false;
                        showReadCustomValueDialog();
                    }
                    else
                    {
                        displayErrorPopup("Failed to initiate CUSTOM parameter value read. Please try again.");
                        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: Failed to initiate readCustomValue.");
                    }
                }
                else // remove CUSTOM value requested
                {
                    if (!existing_usecase->resetValue(param_index.param_type, param_index.param_index, param_index.list_index))
                    {
                        displayErrorPopup("Failed to reset parameter value. Please try again.");
                        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: Failed to reset parameter value in ExistingCommand.");
                        reloadCommandValues(*existing_usecase_index_opt);
                        return;
                    }
                    else
                    {
                        parameter_container->setValue(param_index.param_type, param_index.param_index, param_index.list_index, std::nullopt); // show that we removed the CUSTOM value
                        updateCommandStatus(*existing_usecase_index_opt);
                    }
                }
            }
            else
            {
                if (!existing_usecase->setValue(param_index.param_type, param_index.param_index, param_index.list_index, *convertToParameterValueOpt(value)))
                {
                    displayErrorPopup("Failed to change parameter value. Please try again.");
                    base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: Failed to set parameter value in ExistingCommand.");
                    reloadCommandValues(*existing_usecase_index_opt);
                    return;
                }
                else
                {
                    updateCommandStatus(*existing_usecase_index_opt);
                }   
            }
            
        });
    });
}


void ProgramTree::setupConfirmCallback(ProgramTreeParameters* parameter_container)
{
    parameter_container->confirmClicked().connect([this, parameter_container]() {
        with_frontend_state_lock_([this, parameter_container]() { // here we hold both UI and frontend_state_ locks
            auto existing_usecase_index_opt = existingUsecaseIndexFromParametersWidget(parameter_container);
            if (!existing_usecase_index_opt)
            {
                displayErrorPopup("Failed to confirm parameter values: internal error.");
                base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: existingUsecaseIndexFromParametersWidget returned std::nullopt.");
                return;
            }

            auto program_state_unsafe_ptr = &program_state_unsafe_;
            if (program_state_unsafe_.usecase_tree_->generateCommandDataJson(*existing_usecase_index_opt, [program_state_unsafe_ptr](bool success, uw::helper::ErrorInfo error_info, size_t existing_usecase_index) {
                program_state_unsafe_ptr->generate_finished_ = true;
                program_state_unsafe_ptr->generate_successful_ = success;
                program_state_unsafe_ptr->generate_error_info_ = error_info;
                program_state_unsafe_ptr->generate_usecase_index = existing_usecase_index;
            }))
            {
                program_state_unsafe_.generating_command_data_json_ = true;
                program_state_unsafe_.generate_finished_ = false;
                showGenerateCommandDataPopup();
            }
            else // failed to start generation
            {
                displayErrorPopup("Failed to initiate command data JSON generation. Please try again.");
                base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: Failed to initiate generateCommandDataJson.");
                return;
            }
        });
    });
}


void ProgramTree::reloadCommandValues(size_t existing_usecase_index)
{
    const ut::structs::ExistingCommand& existing_usecase = (*program_state_unsafe_.usecase_tree_)[existing_usecase_index];
    ProgramTreeParameters* parameter_widget = existing_usecase_parameter_widgets_[existing_usecase_index];

    parameter_widget->setAllValues(
        existing_usecase.getParameterValues(structs::ExistingCommand::ParamType::AUTO),
        existing_usecase.getParameterValues(structs::ExistingCommand::ParamType::REQUIRED),
        existing_usecase.getParameterValues(structs::ExistingCommand::ParamType::ADVANCED)
    );

    updateCommandStatus(existing_usecase_index);
}


void ProgramTree::updateCommandStatus(size_t existing_usecase_index)
{
    const ut::structs::ExistingCommand& existing_usecase = (*program_state_unsafe_.usecase_tree_)[existing_usecase_index];
    ProgramTreeParameters* parameter_widget = existing_usecase_parameter_widgets_[existing_usecase_index];
    bool command_ready = existing_usecase.hasCommandDataJson() && existing_usecase.isCommandDataJsonInSync();

    if (command_ready)
    {
        parameter_widget->setConfirmEnable(false); // command data already exists and is in sync
        existing_usecases_list_->setCommandStatus(existing_usecase_index, ProgramCommand::Status::Normal);
    }
    else
    {
        parameter_widget->setConfirmEnable(parameter_widget->areAllValuesSet());
        existing_usecases_list_->setCommandStatus(existing_usecase_index, ProgramCommand::Status::Warning);
    }
}


std::optional<size_t> ProgramTree::existingUsecaseIndexFromParametersWidget(ProgramTreeParameters* parameter_widget) const
{
    auto it = std::find(existing_usecase_parameter_widgets_.begin(), existing_usecase_parameter_widgets_.end(), parameter_widget);
    if (it == existing_usecase_parameter_widgets_.end())
    {
        return std::nullopt;
    }
    return std::distance(existing_usecase_parameter_widgets_.begin(), it);
}


ut::structs::ExistingCommand* ProgramTree::existingUsecaseFromIndex(std::optional<size_t> index_opt) const
{
    if (!index_opt)
    {
        return nullptr;
    }

    size_t index = *index_opt;
    if (index >= program_state_unsafe_.usecase_tree_->size())
    {
        return nullptr;
    }
    return &(*program_state_unsafe_.usecase_tree_)[index];
}


p_desc::ParameterValueOpt ProgramTree::convertToParameterValueOpt(const value_opt_t& value_opt) const
{
    if (!value_opt.has_value())
    {
        return std::nullopt;
    }

    const auto& value = *value_opt;
    if (std::holds_alternative<bool>(value))
    {
        return p_desc::ParameterValueOpt{ p_desc::ParameterValue{ std::get<bool>(value) } };
    }
    else if (std::holds_alternative<int64_t>(value))
    {
        return p_desc::ParameterValueOpt{ p_desc::ParameterValue{ std::get<int64_t>(value) } };
    }
    else if (std::holds_alternative<double>(value))
    {
        return p_desc::ParameterValueOpt{ p_desc::ParameterValue{ std::get<double>(value) } };
    }
    else if (std::holds_alternative<std::string>(value))
    {
        return p_desc::ParameterValueOpt{ p_desc::ParameterValue{ std::get<std::string>(value) } };
    }
    else if (std::holds_alternative<int>(value))
    {
        return p_desc::ParameterValueOpt{ p_desc::ParameterValue{ std::get<int>(value) } };
    }
    else
    {
        // CUSTOM type not handled here
        return std::nullopt;
    }
}


void ProgramTree::onButtonClicked(ProgramTreeButtons button)
{
    if (button == ProgramTreeButtons::NewProgram)
    {
        parameters_container_->setCurrentIndex(0); // show dummy parameters for demonstration
    }
    if (button == ProgramTreeButtons::LoadProgram)
    {
        parameters_container_->setCurrentIndex(1); // show dummy parameters for demonstration
    }

    switch (button)
    {
    case ProgramTreeButtons::NewProgram:
        base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::onButtonClicked: NewProgram button clicked.");
        // TODO implement
        break;
    case ProgramTreeButtons::SaveProgram:
        base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::onButtonClicked: SaveProgram button clicked.");
        // TODO implement
        break;
    case ProgramTreeButtons::LoadProgram:
        base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::onButtonClicked: LoadProgram button clicked.");
        // TODO implement
        break;
    default:
        base_module_->log(aergo::module::logging::LogType::WARNING, "ProgramTree::onButtonClicked: Unhandled button clicked.");
        break;
    }
}


ReusableDialog* ProgramTree::displayErrorPopup(const std::string& message, const std::string& title)
{
    return showPopupDialog(std::move(std::make_unique<ReusableDialog>(title, message, std::vector<ButtonDescription>{
        ButtonDescription{"OK", ButtonStyle::Primary, true}
    })));
}


ReusableDialog* ProgramTree::showParameterDescriptionPopup(const std::string& title, const std::string& description)
{
    return showPopupDialog(std::move(std::make_unique<ReusableDialog>(title, description, std::vector<ButtonDescription>{
        ButtonDescription{"Close", ButtonStyle::Secondary, true}
    })));
}


ReusableDialog* ProgramTree::showPopupDialog(std::unique_ptr<ReusableDialog> dialog, bool dismiss_on_button_click, bool dismiss_on_background_click)
{
    closePopupDialog();

    popup_dialog_ = addWidget(std::move(dialog));

    if (dismiss_on_background_click)
    {
        popup_dialog_->onBackgroundClicked().connect([this]() {
            closePopupDialog();
        });
    }
    
    if (dismiss_on_button_click)
    {
        popup_dialog_->onButtonClicked().connect([this](size_t button_id) {
            closePopupDialog();
        });
    }

    return popup_dialog_;
}


void ProgramTree::closePopupDialog()
{
    if (popup_dialog_ != nullptr)
    {
        removeWidget(popup_dialog_);
        popup_dialog_ = nullptr;
    }
}


void ProgramTree::showReadCustomValueDialog()
{
    if (read_custom_value_dialog_ != nullptr)
    {
        return; // already shown
    }

    read_custom_value_dialog_ = addWidget(std::make_unique<ReusableDialog>(
        "Reading Custom Value", 
        "Please wait while the custom parameter value is being read from the connected module...", 
        std::vector<ButtonDescription>{
            ButtonDescription{ "Cancel", ButtonStyle::Danger, true }
        }
    ));

    read_custom_value_dialog_->onButtonClicked().connect([this](size_t button_id) {
        // Cancel button clicked
        with_frontend_state_lock_([this]() { // here we hold both UI and frontend_state_ locks
            if (program_state_unsafe_.reading_custom_value_)
            {
                program_state_unsafe_.cancel_reading_custom_value_ = true;
            }
        });
    });
}


void ProgramTree::closeReadCustomValueDialog()
{
    if (read_custom_value_dialog_ != nullptr)
    {
        removeWidget(read_custom_value_dialog_);
        read_custom_value_dialog_ = nullptr;
    }
}


void ProgramTree::showGenerateCommandDataPopup()
{
    if (generate_command_data_json_dialog_ != nullptr)
    {
        return; // already shown
    }

    generate_command_data_json_dialog_ = addWidget(std::make_unique<ReusableDialog>(
        "Generating Command Data JSON", 
        "Please wait while the command data JSON is being generated...", 
        std::vector<ButtonDescription>{}
    ));
}


void ProgramTree::closeGenerateCommandDataPopup()
{
    if (generate_command_data_json_dialog_ != nullptr)
    {
        removeWidget(generate_command_data_json_dialog_);
        generate_command_data_json_dialog_ = nullptr;
    }
}


void ProgramTree::showReloadUsecasePopup()
{
    if (reload_usecase_dialog_ != nullptr)
    {
        return; // already shown
    }

    reload_usecase_dialog_ = addWidget(std::make_unique<ReusableDialog>(
        "Reloading Usecases", 
        "Please wait while available usecases are being reloaded from connected modules...", 
        std::vector<ButtonDescription>{}
    ));
}


void ProgramTree::closeReloadUsecasePopup()
{
    if (reload_usecase_dialog_ != nullptr)
    {
        removeWidget(reload_usecase_dialog_);
        reload_usecase_dialog_ = nullptr;
    }
}


void ProgramTree::onTimerRefresh()
{
    with_frontend_state_lock_([this]() { // here we hold both UI and frontend_state_ locks
        if (program_state_unsafe_.reading_custom_value_ && program_state_unsafe_.read_finished_)
        {
            // reading custom value finished
            closeReadCustomValueDialog();
            program_state_unsafe_.reading_custom_value_ = false;

            if (program_state_unsafe_.read_successful_)
            {
                // reload parameter values for the affected usecase
                reloadCommandValues(program_state_unsafe_.read_usecase_index);
            }
            else if (!program_state_unsafe_.cancel_reading_custom_value_)
            {
                displayErrorPopup("Failed to read custom parameter value from connected module.");
                base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::onTimerRefresh: Custom parameter value read failed.");
            }
        }

        if (program_state_unsafe_.generating_command_data_json_ && program_state_unsafe_.generate_finished_)
        {
            // command data JSON generation finished
            closeGenerateCommandDataPopup();
            program_state_unsafe_.generating_command_data_json_ = false;

            if (program_state_unsafe_.generate_successful_)
            {
                // this should disable the confirm button and update the command status to Normal
                updateCommandStatus(program_state_unsafe_.generate_usecase_index);
            }
            else // error, confirm and command will stay the same, but we show error message
            {
                const auto& error_info = program_state_unsafe_.generate_error_info_;
                if (error_info.has_details_)
                {
                    std::stringstream ss;
                    ss << (error_info.is_exception_ ? "EXCEPTION" : "ERROR") << " " << error_info.error_code_ << ": " << error_info.error_message_;
                    displayErrorPopup(ss.str(), "Command Creation Failed");
                    base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: generateCommandDataJson failed: " + ss.str());
                }
                else
                {
                    displayErrorPopup("Unspecified error.", "Command Creation Failed");
                    base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::setupParameterContainer: generateCommandDataJson failed: Unknown error.");
                }
            }
        }
    });
}