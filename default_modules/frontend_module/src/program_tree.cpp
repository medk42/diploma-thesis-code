#include "webapp/ui/helper/program_tree.h"
#include "webapp/ui/helper/program_command.h"

#include <ranges>
#include <sstream>
#include <fstream>

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

    if (program_state_unsafe.save_program_task_) // saving task is running
    {
        showStateSaving();
    }

    if (program_state_unsafe.load_program_task_) // loading task is running
    {
        showStateLoading();
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

        clearExistingUsecases();

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

        loadExistingUsecases();

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
            insertExistingUsecase(new_command, existing_usecases_list_->commandCount());
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


void ProgramTree::insertExistingUsecase(const aergo::module::helpers::usecase_tree::structs::ExistingCommand& existing_usecase, size_t index)
{
    // TODO this all does NOT work for insert!

    auto usecase_ref = existing_usecase.getUsecaseReference();
    if (!usecase_ref)
    {
        // keep existing_usecases_list_, parameters_container_ and existing_usecase_parameter_widgets_ in sync
        existing_usecases_list_->insertCommand(index, existing_usecase.getUsecaseName(), ProgramCommand::Status::Invalid);
        parameters_container_->insertWidget(static_cast<int>(index + 1), std::make_unique<Wt::WContainerWidget>()); // +1 because child 0 is empty; empty page for invalid usecase
        existing_usecase_parameter_widgets_.insert(existing_usecase_parameter_widgets_.begin() + index, nullptr);
    }
    else
    {
        // keep existing_usecases_list_, parameters_container_ and existing_usecase_parameter_widgets_ in sync
        existing_usecases_list_->insertCommand(index, existing_usecase.getUsecaseName(), ProgramCommand::Status::Normal);
        auto w = std::make_unique<ProgramTreeParameters>(
            usecase_ref->getAutoParameters().getParameters(),
            usecase_ref->getRequiredParameters().getParameters(),
            usecase_ref->getAdvancedParameters().getParameters()
        );
        ProgramTreeParameters* params = w.get();
        parameters_container_->insertWidget(static_cast<int>(index + 1), std::move(w)); // +1 because child 0 is empty
        existing_usecase_parameter_widgets_.insert(existing_usecase_parameter_widgets_.begin() + index, params);

        setupParameterContainer(params, existing_usecase, index);
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
    switch (button)
    {
    case ProgramTreeButtons::NewProgram:
        onNewProgram();
        break;
    case ProgramTreeButtons::SaveProgram:
        onSaveProgram();
        break;
    case ProgramTreeButtons::LoadProgram:
        onLoadProgram();
        break;
    case ProgramTreeButtons::CutCommand:
        onCutCommand();
        break;
    case ProgramTreeButtons::CopyCommand:
        onCopyCommand();
        break;
    case ProgramTreeButtons::PasteCommand:
        onPasteCommand();
        break;
    case ProgramTreeButtons::StartProgram:
        startProgram(false);
        break;
    case ProgramTreeButtons::SimulateProgram:
        startProgram(true);
        break;
    case ProgramTreeButtons::StopProgram:
        stopProgram();
        break;
    case ProgramTreeButtons::PauseProgram:
        pauseProgram();
        break;
    case ProgramTreeButtons::ResumeProgram:
        resumeProgram();
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

            bool was_auto_loading = program_state_unsafe_.auto_loading_in_progress_;
            size_t auto_load_usecase_index = program_state_unsafe_.auto_load_usecase_index_;

            if (program_state_unsafe_.read_successful_)
            {
                // reload parameter values for the affected usecase
                reloadCommandValues(program_state_unsafe_.read_usecase_index);
                
                // if auto-loading, continue to next parameter
                if (was_auto_loading)
                {
                    // advance to next position for auto-loading
                    // if it was a list parameter, try next list index first, then next parameter
                    const ut::structs::ExistingCommand& existing_usecase = (*program_state_unsafe_.usecase_tree_)[auto_load_usecase_index];
                    const auto* parameters = existing_usecase.getParameters(ut::structs::ExistingCommand::ParamType::AUTO);
                    if (parameters && program_state_unsafe_.auto_load_current_param_index_ < parameters->getParameters().size())
                    {
                        const auto& param = parameters->getParameters()[program_state_unsafe_.auto_load_current_param_index_];
                        if (param.as_list_)
                        {
                            // for list parameters, try next list index
                            program_state_unsafe_.auto_load_current_list_index_++;
                        }
                        else
                        {
                            // for non-list, move to next parameter
                            program_state_unsafe_.auto_load_current_param_index_++;
                            program_state_unsafe_.auto_load_current_list_index_ = 0;
                        }
                    }
                    else
                    {
                        // move to next parameter
                        program_state_unsafe_.auto_load_current_param_index_++;
                        program_state_unsafe_.auto_load_current_list_index_ = 0;
                    }
                    
                    // continue auto-loading sequence
                    handleAutoLoading();
                }
            }
            else if (!program_state_unsafe_.cancel_reading_custom_value_)
            {
                // read failed - stop auto-loading if it was active
                if (was_auto_loading)
                {
                    program_state_unsafe_.auto_loading_in_progress_ = false;
                    displayErrorPopup("Failed to read custom parameter value. Auto-loading stopped.");
                    base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::onTimerRefresh: Custom parameter value read failed during auto-loading.");
                }
                else
                {
                    displayErrorPopup("Failed to read custom parameter value from connected module.");
                    base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::onTimerRefresh: Custom parameter value read failed.");
                }
            }
            else
            {
                // cancelled - if auto-loading, move to next parameter
                if (was_auto_loading)
                {
                    // advance to next position for auto-loading
                    const ut::structs::ExistingCommand& existing_usecase = (*program_state_unsafe_.usecase_tree_)[auto_load_usecase_index];
                    const auto* parameters = existing_usecase.getParameters(ut::structs::ExistingCommand::ParamType::AUTO);
                    if (parameters && program_state_unsafe_.auto_load_current_param_index_ < parameters->getParameters().size())
                    {
                        const auto& param = parameters->getParameters()[program_state_unsafe_.auto_load_current_param_index_];
                        if (param.as_list_)
                        {
                            // for list parameters, try next list index
                            program_state_unsafe_.auto_load_current_list_index_++;
                        }
                        else
                        {
                            // for non-list, move to next parameter
                            program_state_unsafe_.auto_load_current_param_index_++;
                            program_state_unsafe_.auto_load_current_list_index_ = 0;
                        }
                    }
                    else
                    {
                        // move to next parameter
                        program_state_unsafe_.auto_load_current_param_index_++;
                        program_state_unsafe_.auto_load_current_list_index_ = 0;
                    }
                    
                    // continue to next parameter
                    handleAutoLoading();
                }
                else
                {
                    base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::onTimerRefresh: Custom parameter read cancelled.");
                }
            }
        }

        // check for auto-load trigger
        if (program_state_unsafe_.auto_load_triggered_)
        {
            program_state_unsafe_.auto_load_triggered_ = false;
            handleAutoLoading();
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

        handleSaveTask();
        handleLoadTask();
        handleButtonStates();
    });
}


void ProgramTree::clearExistingUsecases()
{
    existing_usecases_list_->clearCommands();

    existing_usecase_parameter_widgets_.clear();

    parameters_container_->clear();
    parameters_container_->addWidget(std::make_unique<Wt::WContainerWidget>()); // empty page for New Program
    parameters_container_->setCurrentIndex(0); // show empty page after reload
}


void ProgramTree::loadExistingUsecases()
{
    for (size_t i = 0; i < program_state_unsafe_.usecase_tree_->size(); ++i)
    {
        const auto& existing_usecase = (*program_state_unsafe_.usecase_tree_)[i];
        insertExistingUsecase(existing_usecase, existing_usecases_list_->commandCount());
    }
}


bool ProgramTree::getProgramDirectory(std::filesystem::path& out_directory)
{
    std::string base_path = base_module_->getDataPath();
    std::filesystem::path base_dir(base_path);
    std::filesystem::path program_dir = base_dir / "programs";

    if (!std::filesystem::exists(program_dir))
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "Programs directory does not exist, creating: " + program_dir.string());
        if (!std::filesystem::create_directories(program_dir))
        {
            base_module_->log(aergo::module::logging::LogType::ERROR, "Failed to create programs directory: " + program_dir.string());
            return false;
        }
    }

    out_directory = program_dir;
    return true;
}


std::vector<std::string> ProgramTree::getExistingProgramsInDirectory(const std::filesystem::path& directory)
{
    std::vector<std::string> files;

    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "Directory does not exist or is not a directory: " + directory.string());
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.is_regular_file())
        {
            files.push_back(entry.path().filename().string());
        }
    }

    std::vector<std::string> filtered_files;
    for (const auto& file : files) {
        if (file.ends_with(AERGO_PROGRAM_EXTENSION)) { // aergo program file extension
            filtered_files.push_back(file.substr(0, file.find_last_of('.')));
        }
    }

    return filtered_files;
}


void ProgramTree::closeProgramFileDialog()
{
    if (program_file_dialog_ != nullptr)
    {
        removeWidget(program_file_dialog_);
        program_file_dialog_ = nullptr;
    }
}


void ProgramTree::closeSaveProgramDialog()
{
    if (save_program_dialog_ != nullptr)
    {
        removeWidget(save_program_dialog_);
        save_program_dialog_ = nullptr;
    }
}


void ProgramTree::closeLoadProgramDialog()
{
    if (load_program_dialog_ != nullptr)
    {
        removeWidget(load_program_dialog_);
        load_program_dialog_ = nullptr;
    }
}


void ProgramTree::closeSaveProgramOverwriteDialog()
{
    if (save_program_overwrite_dialog_ != nullptr)
    {
        removeWidget(save_program_overwrite_dialog_);
        save_program_overwrite_dialog_ = nullptr;
    }
}


void ProgramTree::showStateSaving()
{
    closeSaveProgramDialog();

    save_program_dialog_ = addWidget(std::make_unique<ReusableDialog>(
        "Saving Program", 
        "Please wait while the program state is being saved to file...", 
        std::vector<ButtonDescription>{}
    ));
}


void ProgramTree::showStateLoading()
{
    closeLoadProgramDialog();

    load_program_dialog_ = addWidget(std::make_unique<ReusableDialog>(
        "Loading Program", 
        "Please wait while the program state is being loaded from file...", 
        std::vector<ButtonDescription>{}
    ));
}


void ProgramTree::saveStateToFile(const std::filesystem::path& file_path, bool overwrite_confirmed)
{
    if (!overwrite_confirmed && std::filesystem::exists(file_path))
    {
        // ask for confirmation
        save_program_overwrite_dialog_ = addWidget(std::make_unique<ReusableDialog>(
            "Confirm Overwrite", 
            "The selected file already exists. Do you want to overwrite it?",
            std::vector<ButtonDescription>{
                ButtonDescription{"Cancel", ButtonStyle::Secondary, true},
                ButtonDescription{"Overwrite", ButtonStyle::Danger, true}
            }
        ));

        save_program_overwrite_dialog_->onButtonClicked().connect([this, file_path](size_t button_id) {
            closeSaveProgramOverwriteDialog();
            if (button_id == 1) // Overwrite
            {
                saveStateToFile(file_path, true);
            }
        });

        return;
    }

    with_frontend_state_lock_([this, &file_path]() { // here we hold both UI and frontend_state_ locks
        auto usecase_tree_ptr = program_state_unsafe_.usecase_tree_.get();

        if (program_state_unsafe_.save_program_task_ != nullptr)
        {
            displayErrorPopup("A save operation is already in progress. Please wait.");
            base_module_->log(aergo::module::logging::LogType::WARNING, "ProgramTree::saveStateToFile: Save operation already in progress.");
            return;
        }

        program_state_unsafe_.save_program_task_ = std::make_unique<helpers::async_helpers::AsyncTask<std::optional<std::string>>>(
            [usecase_tree_ptr, file_path](const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled_flag) -> std::optional<std::string> {
                return usecase_tree_ptr->toJson();
            }
        );
        program_state_unsafe_.save_program_task_->start();
        program_state_unsafe_.save_file_path_ = file_path.string();
        showStateSaving();
    });
}


void ProgramTree::handleSaveTask()
{
    const auto& task = program_state_unsafe_.save_program_task_;
    if (task == nullptr)
    {
        closeSaveProgramDialog();
        return; // no task
    }

    auto state = task->getState();
    if (state == helpers::async_helpers::AsyncTaskState::NOT_STARTED)
    {
        closeSaveProgramDialog();
        displayErrorPopup("Internal error.", "Failed to save program");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleSaveTask: Save task in NOT_STARTED state.");
        program_state_unsafe_.save_program_task_ = nullptr;
        return;
    }

    if (state == helpers::async_helpers::AsyncTaskState::RUNNING)
    {
        return; // still running
    }

    auto result = task->getResult(); // otherwise task finished
    program_state_unsafe_.save_program_task_ = nullptr; // clear task
    closeSaveProgramDialog();

    if (!result.has_value() || !result.value().has_value())
    {
        displayErrorPopup("Could not serialize program state to JSON.", "Failed to save program");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleSaveTask: Failed to serialize program state to JSON.");
        return;
    }

    if (!writeFile(program_state_unsafe_.save_file_path_, result.value().value()))
    {
        displayErrorPopup("Could not write to file.", "Failed to save program");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleSaveTask: Failed to write program state to file: " + program_state_unsafe_.save_file_path_);
        return;
    }
}


void ProgramTree::handleLoadTask()
{
    const auto& task = program_state_unsafe_.load_program_task_;
    if (task == nullptr)
    {
        closeLoadProgramDialog();
        return; // no task
    }

    auto state = task->getState();
    if (state == helpers::async_helpers::AsyncTaskState::NOT_STARTED)
    {
        closeLoadProgramDialog();
        displayErrorPopup("Internal error.", "Failed to load program");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleLoadTask: Load task in NOT_STARTED state.");
        program_state_unsafe_.load_program_task_ = nullptr;
        return;
    }

    if (state == helpers::async_helpers::AsyncTaskState::RUNNING)
    {
        return; // still running
    }

    auto result_opt = task->getResult(); // otherwise task finished
    program_state_unsafe_.load_program_task_ = nullptr; // clear task
    closeLoadProgramDialog();

    // reload existing usecases in UI
    clearExistingUsecases();
    loadExistingUsecases();

    if (!result_opt.has_value())
    {
        displayErrorPopup("Internal error.", "Failed to load program");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleLoadTask: Load task returned no result.");
        return;
    }

    const auto& result = *result_opt;
    if (!result)
    {
        auto missing_identifier_opt = result.error();
        if (missing_identifier_opt.has_value())
        {
            displayErrorPopup("The program file references a usecase with identifier '" + *missing_identifier_opt + "' which is not available in the connected modules. Please add the missing identifier and try loading again.", "Failed to load program");
            base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleLoadTask: Missing usecase identifier: " + *missing_identifier_opt);
        }
        else
        {
            displayErrorPopup("The program file is not valid.", "Failed to load program");
            base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleLoadTask: Program file is not valid.");
        }

        // clear usecases after failed load (no need to reload, there are none)
        program_state_unsafe_.usecase_tree_->clearCommands(); // clear program tree
        clearExistingUsecases(); // clear UI
        return;
    }

    base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::handleLoadTask: Program loaded successfully from file with " + std::to_string(program_state_unsafe_.usecase_tree_->size()) + " usecases.");
}


bool ProgramTree::writeFile(const std::filesystem::path& p, std::string_view data) noexcept {
    try {
        if (!p.has_parent_path() || !std::filesystem::exists(p.parent_path())) {
            std::filesystem::create_directories(p.parent_path());
        }

        // Write exact bytes (no locale conversions)
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out) return false;

        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out) return false;

        out.flush();
        return static_cast<bool>(out);
    } catch (...) {
        return false;
    }
}


bool ProgramTree::readFile(const std::filesystem::path& p, std::string& out_data) noexcept {
    out_data.clear();

    try {
        // Quick existence and file check
        if (!std::filesystem::exists(p) || !std::filesystem::is_regular_file(p))
            return false;

        std::ifstream in(p, std::ios::binary);
        if (!in)
            return false;

        // Determine size
        in.seekg(0, std::ios::end);
        std::streamsize size = in.tellg();
        if (size < 0)
            return false; // tellg() failed

        out_data.resize(static_cast<size_t>(size));
        in.seekg(0, std::ios::beg);

        // Read whole file
        if (!in.read(out_data.data(), size))
            return false;

        return true;
    }
    catch (...) {
        return false;
    }
}


void ProgramTree::onCutCommand()
{
    auto selected_index_opt = existing_usecases_list_->selectedCommandIndex();
    if (!selected_index_opt) 
    {
        displayErrorPopup("Failed to cut command: no command selected.");
        base_module_->log(aergo::module::logging::LogType::WARNING, "ProgramTree::onCutCommand: No command selected to cut.");
        return;
    }
    
    size_t selected_index = *selected_index_opt;
    if (selected_index >= program_state_unsafe_.usecase_tree_->size())
    {
        displayErrorPopup("Failed to cut command: internal error.");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::onCutCommand: selected_index out of range.");
        return;
    }

    const auto& existing_usecase = (*program_state_unsafe_.usecase_tree_)[selected_index];
    clipboard_command_ = existing_usecase; // copy the command

    if (!program_state_unsafe_.usecase_tree_->removeCommand(selected_index))
    {
        displayErrorPopup("Failed to remove command after copy. Please try again.");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::onCopyCommand: Failed to remove command after copy.");
        return;
    }

    // remove from UI
    if (selected_index >= existing_usecases_list_->commandCount() || selected_index >= existing_usecase_parameter_widgets_.size())
    {
        displayErrorPopup("Failed to remove command from UI after copy. Please try again.");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::onCopyCommand: selected_index out of range for UI removal.");
        return;
    }

    // keep existing_usecases_list_, parameters_container_ and existing_usecase_parameter_widgets_ in sync
    existing_usecases_list_->removeCommand(selected_index);
    parameters_container_->setCurrentIndex(static_cast<int>(selected_index + 1)); // switch to the page of the removed usecase to remove it
    parameters_container_->removeWidget(parameters_container_->currentWidget());
    existing_usecase_parameter_widgets_.erase(existing_usecase_parameter_widgets_.begin() + selected_index);

    parameters_container_->setCurrentIndex(0); // show empty page after removal
}


void ProgramTree::onCopyCommand()
{
    auto selected_index_opt = existing_usecases_list_->selectedCommandIndex();
    if (!selected_index_opt) 
    {
        displayErrorPopup("Failed to copy command: no command selected.");
        base_module_->log(aergo::module::logging::LogType::WARNING, "ProgramTree::onCopyCommand: No command selected to copy.");
        return;
    }
    
    size_t selected_index = *selected_index_opt;
    if (selected_index >= program_state_unsafe_.usecase_tree_->size())
    {
        displayErrorPopup("Failed to copy command: internal error.");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::onCopyCommand: selected_index out of range.");
        return;
    }

    const auto& existing_usecase = (*program_state_unsafe_.usecase_tree_)[selected_index];
    clipboard_command_ = existing_usecase; // copy the command
}


void ProgramTree::onPasteCommand()
{
    if (!clipboard_command_)
    {
        displayErrorPopup("Failed to paste command: clipboard is empty.");
        base_module_->log(aergo::module::logging::LogType::WARNING, "ProgramTree::onPasteCommand: Clipboard is empty.");
        return;
    }

    auto selected_index_opt = existing_usecases_list_->selectedCommandIndex();
    if (!selected_index_opt) 
    {
        displayErrorPopup("Failed to paste command: no command selected to paste after.");
        base_module_->log(aergo::module::logging::LogType::WARNING, "ProgramTree::onPasteCommand: No command selected to paste after.");
        return;
    }
    size_t copy_target_index = *selected_index_opt + 1; // paste after selected

    if (!program_state_unsafe_.usecase_tree_->insertCommand(copy_target_index, *clipboard_command_))
    {
        displayErrorPopup("Failed to append command from clipboard. Please try again.");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::onPasteCommand: Failed to append command from clipboard.");
        return;
    }

    const auto& new_command = (*program_state_unsafe_.usecase_tree_)[copy_target_index];
    insertExistingUsecase(new_command, copy_target_index);
    existing_usecases_list_->setCommandSelected(copy_target_index); // show newly pasted usecase
    parameters_container_->setCurrentIndex(static_cast<int>(copy_target_index + 1)); // show parameters of newly pasted usecase
}


void ProgramTree::onNewProgram()
{
    if (new_program_dialog_ != nullptr)
    {
        return; // already shown
    }

    new_program_dialog_ = addWidget(std::make_unique<ReusableDialog>(
        "New Program", 
        "Are you sure you want to create a new program? All unsaved changes will be lost.",
        std::vector<ButtonDescription>{
            ButtonDescription{"Cancel", ButtonStyle::Secondary, true},
            ButtonDescription{"Create New Program", ButtonStyle::Danger, true}
        }
    ));

    new_program_dialog_->onButtonClicked().connect([this](size_t button_id) {
        if (button_id == 1) // Create New Program
        {
            with_frontend_state_lock_([this]() { // here we hold both UI and frontend_state_ locks
                // clear program tree
                program_state_unsafe_.usecase_tree_->clearCommands();

                // clear UI
                clearExistingUsecases();
            });
        }

        // close dialog in any case
        if (new_program_dialog_ != nullptr)
        {
            removeWidget(new_program_dialog_);
            new_program_dialog_ = nullptr;
        }
    });
}


void ProgramTree::onSaveProgram()
{
    if (program_file_dialog_ != nullptr)
    {
        return; // already shown
    }

    std::filesystem::path program_dir;
    if (!getProgramDirectory(program_dir))
    {
        displayErrorPopup("Failed to get or create program directory for saving.");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::onSaveProgram: getProgramDirectory failed.");
        return;
    }

    std::vector<std::string> filtered_files = getExistingProgramsInDirectory(program_dir);

    std::string selected_file;
    for (int i = 0; i < 1000; ++i)
    {
        std::string filename = "program_" + std::to_string(i);
        if (std::find(filtered_files.begin(), filtered_files.end(), filename) == filtered_files.end())
        {
            selected_file = filename;
            break;
        }
    }

    program_file_dialog_ = addWidget(std::make_unique<FileDialog>(
        "Save Program As", 
        std::move(filtered_files),
        selected_file,
        "Save"
    ));

    program_file_dialog_->onCancelClicked().connect([this]() { closeProgramFileDialog(); });
    program_file_dialog_->onAcceptClicked().connect([this, program_dir](std::string selected_file) {
        closeProgramFileDialog();
        saveStateToFile(program_dir / (selected_file + AERGO_PROGRAM_EXTENSION), false);
    });
}


void ProgramTree::onLoadProgram()
{
    if (program_file_dialog_ != nullptr)
    {
        return; // already shown
    }

    std::filesystem::path program_dir;
    if (!getProgramDirectory(program_dir))
    {
        displayErrorPopup("Failed to get or create program directory for loading.");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::onLoadProgram: getProgramDirectory failed.");
        return;
    }

    std::vector<std::string> filtered_files = getExistingProgramsInDirectory(program_dir);
    std::string selected_file;
    if (!filtered_files.empty())
    {
        selected_file = filtered_files[filtered_files.size() - 1];
    }

    program_file_dialog_ = addWidget(std::make_unique<FileDialog>(
        "Load Program", 
        std::move(filtered_files),
        selected_file,
        "Load"
    ));

    program_file_dialog_->onCancelClicked().connect([this]() { closeProgramFileDialog(); });
    program_file_dialog_->onAcceptClicked().connect([this, program_dir](std::string selected_file) {
        closeProgramFileDialog();

        std::filesystem::path file_path = program_dir / (selected_file + AERGO_PROGRAM_EXTENSION);
        std::string file_content;
        if (!readFile(file_path, file_content))
        {
            displayErrorPopup("Failed to read program file.");
            base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::onLoadProgram: Failed to read program file: " + file_path.string());
            return;
        }

        with_frontend_state_lock_([this, &file_content]() {
            auto usecase_tree_ptr = program_state_unsafe_.usecase_tree_.get();

            if (program_state_unsafe_.load_program_task_ != nullptr)
            {
                displayErrorPopup("A load operation is already in progress. Please wait.");
                base_module_->log(aergo::module::logging::LogType::WARNING, "ProgramTree::onLoadProgram: Load operation already in progress.");
                return;
            }
            
            program_state_unsafe_.usecase_tree_->clearCommands(); // clear program tree
            clearExistingUsecases(); // clear UI

            program_state_unsafe_.load_program_task_ = std::make_unique<helpers::async_helpers::AsyncTask<std::expected<void, std::optional<std::string>>>>(
                [usecase_tree_ptr, file_content = std::move(file_content)](const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled_flag) -> std::expected<void, std::optional<std::string>> {
                    std::string missing_identifier;
                    bool result = usecase_tree_ptr->fromJson(file_content, missing_identifier);
                    if (result)
                    {
                        return std::expected<void, std::optional<std::string>>{};
                    }
                    else
                    {
                        return std::unexpected{ missing_identifier.empty() ? std::nullopt : std::optional<std::string>{missing_identifier} };
                    }
                }
            );
            program_state_unsafe_.load_program_task_->start();
            showStateLoading();
        });
    });
}


void ProgramTree::handleButtonStates()
{
    if (program_state_unsafe_.save_program_task_ != nullptr || program_state_unsafe_.load_program_task_ != nullptr)
    {
        return; // we can not access usecase tree while saving or loading (we would be stuck on a mutex lock)
    }

    auto program_state_opt = program_state_unsafe_.usecase_tree_->getProgramState();
    bool all_existing_usecases_valid = existing_usecases_list_->allCommandsHaveStatus(ProgramCommand::Status::Normal);
    size_t command_count = existing_usecases_list_->commandCount();

    if (program_state_opt == program_state_unsafe_.current_running_program_state_ && 
        all_existing_usecases_valid == all_existing_usecases_valid_ &&
        command_count == last_existing_usecase_count_) 
    {
        return; // no change
    }

    if (program_state_opt.has_value() && program_state_opt == ut::ProgramInstance::ProgramState::STOPPED && program_state_opt != program_state_unsafe_.current_running_program_state_)
    {
        handleProgramStopped();
    }
    
    program_state_unsafe_.current_running_program_state_ = program_state_opt;
    all_existing_usecases_valid_ = all_existing_usecases_valid;
    last_existing_usecase_count_ = command_count;

    if (!program_state_opt.has_value() || *program_state_opt == ut::ProgramInstance::ProgramState::STOPPED)
    {
        if (all_existing_usecases_valid && command_count > 0)
        {
            onButtonStateChanged_.emit(ProgramTreeButtonState { true, true, false, false, false });
        }
        else
        {
            onButtonStateChanged_.emit(ProgramTreeButtonState { false, false, false, false, false });
        }
    }
    else if (*program_state_opt == ut::ProgramInstance::ProgramState::NOT_STARTED || *program_state_opt == ut::ProgramInstance::ProgramState::STOPPING)
    {
        onButtonStateChanged_.emit(ProgramTreeButtonState { false, false, false, false, false });
    }
    else if (*program_state_opt == ut::ProgramInstance::ProgramState::RUNNING)
    {
        onButtonStateChanged_.emit(ProgramTreeButtonState { false, false, true, true, false });
    }
    else if (*program_state_opt == ut::ProgramInstance::ProgramState::PAUSED)
    {
        onButtonStateChanged_.emit(ProgramTreeButtonState { false, false, true, false, true });
    }
    else // RESUMING or PAUSING
    {
        onButtonStateChanged_.emit(ProgramTreeButtonState { false, false, true, false, false });
    }
}


void ProgramTree::handleProgramStopped()
{
    auto program_result_opt = program_state_unsafe_.usecase_tree_->getProgramResult();

    if (!program_result_opt.has_value())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleButtonStates: Failed to get program result after stop.");
        displayErrorPopup("Internal error: Unable to get stop reason.", "Program Finished");
        return;
    }

    const auto& program_result = *program_result_opt;
    if (program_result.failed_to_remove_programs_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleButtonStates: Program failed to remove running programs after stopping.");
    }

    if (program_result.program_stop_reason_ == ut::ProgramInstance::ProgramStopReason::NONE)
    {
        displayErrorPopup("Internal error: Program stopped without a specified reason.", "Program Finished");
        base_module_->log(aergo::module::logging::LogType::WARNING, "ProgramTree::handleButtonStates: Program stopped without a specified reason.");
        return;
    }
    else if (program_result.program_stop_reason_ == ut::ProgramInstance::ProgramStopReason::COMPLETED)
    {
        displayErrorPopup("Program completed successfully.", "Program Finished");
        base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::handleButtonStates: Program completed successfully.");
        return;
    }
    else if (program_result.program_stop_reason_ == ut::ProgramInstance::ProgramStopReason::STOP_REQUESTED)
    {
        displayErrorPopup("Program stopped on user request.", "Program Stopped");
        base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::handleButtonStates: Program stopped as requested by the user.");
        return;
    }
    else if (program_result.program_stop_reason_ == ut::ProgramInstance::ProgramStopReason::ERROR)
    {
        std::string err_msg = parseErrorInfo(program_result.program_last_error_info_);
        displayErrorPopup("Program stopped due to an error during execution: " + err_msg, "Program Error");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleButtonStates: Program stopped due to an error during execution: " + err_msg);
        return;
    }
    else if (program_result.program_stop_reason_ == ut::ProgramInstance::ProgramStopReason::EXCEPTION)
    {
        std::string err_msg = parseErrorInfo(program_result.program_last_error_info_);
        displayErrorPopup("Program stopped due to an exception during execution: " + err_msg, "Program Exception");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleButtonStates: Program stopped due to an exception during execution: " + err_msg);
        return;
    }
    else if (program_result.program_stop_reason_ == ut::ProgramInstance::ProgramStopReason::INTERNAL_ERROR)
    {
        std::string err_msg = parseErrorInfo(program_result.program_last_error_info_);
        displayErrorPopup("Program stopped due to an internal error: " + err_msg, "Program Internal Error");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleButtonStates: Program stopped due to an internal error: " + err_msg);
        return;
    }
    else
    {
        displayErrorPopup("Program stopped due to an unknown reason.", "Program Stopped");
        base_module_->log(aergo::module::logging::LogType::WARNING, "ProgramTree::handleButtonStates: Program stopped due to an unknown reason.");
        return;
    }
}


std::string ProgramTree::parseErrorInfo(const std::optional<ut::uw::helper::ErrorInfo>& error_info) const
{
    if (!error_info.has_value() || !error_info->has_details_)
    {
        return "Unspecified error.";
    }

    return (error_info->is_exception_ ? "EXCEPTION " : "ERROR ") + std::to_string(error_info->error_code_) + ": " + error_info->error_message_;
}


void ProgramTree::startProgram(bool simulate)
{
    if (!program_state_unsafe_.usecase_tree_->start(simulate))
    {
        displayErrorPopup("Failed to start program. Please try again.");
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::startProgram: Failed to start program.");
    }
}


void ProgramTree::stopProgram()
{
    program_state_unsafe_.usecase_tree_->stop();
}


void ProgramTree::pauseProgram()
{
    program_state_unsafe_.usecase_tree_->pause();
}


void ProgramTree::resumeProgram()
{
    program_state_unsafe_.usecase_tree_->resume();
}


void ProgramTree::handleAutoLoading()
{
    // if already loading, cancel current load and move to next
    if (program_state_unsafe_.auto_loading_in_progress_ && program_state_unsafe_.reading_custom_value_)
    {
        program_state_unsafe_.cancel_reading_custom_value_ = true;
        // will continue in next timer tick after cancellation
        return;
    }

    // get currently selected usecase
    auto selected_index_opt = existing_usecases_list_->selectedCommandIndex();
    if (!selected_index_opt)
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "ProgramTree::handleAutoLoading: No usecase selected.");
        program_state_unsafe_.auto_loading_in_progress_ = false;
        return;
    }

    size_t usecase_index = *selected_index_opt;
    if (usecase_index >= program_state_unsafe_.usecase_tree_->size())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleAutoLoading: Selected usecase index out of range.");
        program_state_unsafe_.auto_loading_in_progress_ = false;
        return;
    }

    const ut::structs::ExistingCommand& existing_usecase = (*program_state_unsafe_.usecase_tree_)[usecase_index];
    const auto* parameters = existing_usecase.getParameters(ut::structs::ExistingCommand::ParamType::AUTO);
    if (!parameters)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleAutoLoading: Failed to get auto parameters.");
        program_state_unsafe_.auto_loading_in_progress_ = false;
        return;
    }

    const auto& param_list = parameters->getParameters();
    const auto& param_values = existing_usecase.getParameterValues(ut::structs::ExistingCommand::ParamType::AUTO);

    // find next unloaded CUSTOM parameter
    size_t start_param_index = program_state_unsafe_.auto_loading_in_progress_ ? 
        program_state_unsafe_.auto_load_current_param_index_ : 0;
    size_t start_list_index = program_state_unsafe_.auto_loading_in_progress_ ? 
        program_state_unsafe_.auto_load_current_list_index_ : 0;

    for (size_t param_idx = start_param_index; param_idx < param_list.size(); ++param_idx)
    {
        const auto& param = param_list[param_idx];
        if (param.type_ != p_desc::ParameterType::CUSTOM)
        {
            continue;
        }

        if (param_idx >= param_values.size())
        {
            continue;
        }

        const auto& param_value_list = param_values[param_idx];
        size_t list_start = (param_idx == start_param_index) ? start_list_index : 0;

        if (param.as_list_)
        {
            // for list parameters, check if we can add a new value or if there's an unloaded one
            size_t current_list_size = param_value_list.size();
            size_t max_size = param.list_size_max_ == 0 ? SIZE_MAX : param.list_size_max_;

            // check if there's an unloaded value in the list
            bool found_unloaded = false;
            for (size_t list_idx = list_start; list_idx < current_list_size; ++list_idx)
            {
                if (!param_value_list[list_idx].has_value())
                {
                    // found unloaded value, load it
                    found_unloaded = true;
                    program_state_unsafe_.auto_loading_in_progress_ = true;
                    program_state_unsafe_.auto_load_current_param_index_ = param_idx;
                    program_state_unsafe_.auto_load_current_list_index_ = list_idx;
                    program_state_unsafe_.auto_load_usecase_index_ = usecase_index;
                    program_state_unsafe_.cancel_reading_custom_value_ = false;

                    auto program_state_unsafe_ptr = &program_state_unsafe_;
                    if (program_state_unsafe_.usecase_tree_->readCustomValue(
                        usecase_index,
                        param_idx,
                        list_idx,
                        program_state_unsafe_.cancel_reading_custom_value_,
                        [program_state_unsafe_ptr](bool read_success, size_t existing_usecase_index) {
                            program_state_unsafe_ptr->read_finished_ = true;
                            program_state_unsafe_ptr->read_successful_ = read_success;
                            program_state_unsafe_ptr->read_usecase_index = existing_usecase_index;
                        }
                    ))
                    {
                        program_state_unsafe_.reading_custom_value_ = true;
                        program_state_unsafe_.read_finished_ = false;
                        showReadCustomValueDialog();
                        return;
                    }
                    else
                    {
                        program_state_unsafe_.auto_loading_in_progress_ = false;
                        base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleAutoLoading: Failed to initiate readCustomValue.");
                        return;
                    }
                }
            }

            // if no unloaded value found and we can add more, add a new one
            if (!found_unloaded && current_list_size < max_size)
            {
                // add a new empty value to the list
                if (!const_cast<ut::structs::ExistingCommand&>(existing_usecase).addValue(
                    ut::structs::ExistingCommand::ParamType::AUTO,
                    param_idx,
                    std::nullopt))
                {
                    base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleAutoLoading: Failed to add new list value.");
                    program_state_unsafe_.auto_loading_in_progress_ = false;
                    return;
                }

                // reload UI to show the new list value
                reloadCommandValues(usecase_index);

                // get updated parameter values to find the new list index
                const auto& updated_param_values = existing_usecase.getParameterValues(ut::structs::ExistingCommand::ParamType::AUTO);
                if (param_idx >= updated_param_values.size())
                {
                    base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleAutoLoading: Parameter index out of range after adding list value.");
                    program_state_unsafe_.auto_loading_in_progress_ = false;
                    return;
                }
                const auto& updated_param_value_list = updated_param_values[param_idx];
                size_t new_list_idx = updated_param_value_list.size() - 1; // the index of the newly added value
                program_state_unsafe_.auto_loading_in_progress_ = true;
                program_state_unsafe_.auto_load_current_param_index_ = param_idx;
                program_state_unsafe_.auto_load_current_list_index_ = new_list_idx;
                program_state_unsafe_.auto_load_usecase_index_ = usecase_index;
                program_state_unsafe_.cancel_reading_custom_value_ = false;

                auto program_state_unsafe_ptr = &program_state_unsafe_;
                if (program_state_unsafe_.usecase_tree_->readCustomValue(
                    usecase_index,
                    param_idx,
                    new_list_idx,
                    program_state_unsafe_.cancel_reading_custom_value_,
                    [program_state_unsafe_ptr](bool read_success, size_t existing_usecase_index) {
                        program_state_unsafe_ptr->read_finished_ = true;
                        program_state_unsafe_ptr->read_successful_ = read_success;
                        program_state_unsafe_ptr->read_usecase_index = existing_usecase_index;
                    }
                ))
                {
                    program_state_unsafe_.reading_custom_value_ = true;
                    program_state_unsafe_.read_finished_ = false;
                    showReadCustomValueDialog();
                    return;
                }
                else
                {
                    program_state_unsafe_.auto_loading_in_progress_ = false;
                    base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleAutoLoading: Failed to initiate readCustomValue for new list value.");
                    return;
                }
            }
        }
        else
        {
            // non-list parameter - check if it's loaded
            if (param_value_list.empty() || !param_value_list[0].has_value())
            {
                // unloaded, start loading
                program_state_unsafe_.auto_loading_in_progress_ = true;
                program_state_unsafe_.auto_load_current_param_index_ = param_idx;
                program_state_unsafe_.auto_load_current_list_index_ = 0;
                program_state_unsafe_.auto_load_usecase_index_ = usecase_index;
                program_state_unsafe_.cancel_reading_custom_value_ = false;

                auto program_state_unsafe_ptr = &program_state_unsafe_;
                if (program_state_unsafe_.usecase_tree_->readCustomValue(
                    usecase_index,
                    param_idx,
                    0,
                    program_state_unsafe_.cancel_reading_custom_value_,
                    [program_state_unsafe_ptr](bool read_success, size_t existing_usecase_index) {
                        program_state_unsafe_ptr->read_finished_ = true;
                        program_state_unsafe_ptr->read_successful_ = read_success;
                        program_state_unsafe_ptr->read_usecase_index = existing_usecase_index;
                    }
                ))
                {
                    program_state_unsafe_.reading_custom_value_ = true;
                    program_state_unsafe_.read_finished_ = false;
                    showReadCustomValueDialog();
                    return;
                }
                else
                {
                    program_state_unsafe_.auto_loading_in_progress_ = false;
                    base_module_->log(aergo::module::logging::LogType::ERROR, "ProgramTree::handleAutoLoading: Failed to initiate readCustomValue.");
                    return;
                }
            }
        }
    }

    // no more unloaded parameters found
    program_state_unsafe_.auto_loading_in_progress_ = false;
    base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::handleAutoLoading: All CUSTOM parameters are loaded.");
}