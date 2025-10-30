#include "webapp/ui/helper/program_tree.h"
#include "webapp/ui/helper/program_tree_parameters.h"
#include "webapp/ui/helper/program_command.h"

#include <ranges>

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
} 


void ProgramTree::reloadAvailableUsecases() // called with UI and frontend_state_ locks held
{
    auto updating_note_dialog = showPopupDialog(
            std::move(std::make_unique<ReusableDialog>(
            "Updating Usecase List", 
            "Please wait while the list of available and existing usecases is being updated from connected modules...", 
            std::vector<ButtonDescription>{}
        )),
        false, // this pop-up is not dismissed on button click
        false  // this pop-up is not dismissed on background click
    );

    base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::reloadAvailableUsecases: Reloading available usecases...");
    bool success = program_state_unsafe_.usecase_tree_->updateAvailableUsecases([this, updating_note_dialog](bool success, const std::map<std::string, structs::AvailableUsecase>& available_usecases) {
        if (popup_dialog_ == updating_note_dialog)
        {
            closePopupDialog();
        }

        available_usecases_list_->clearCommands();
        available_usecase_ids_.clear();
        existing_usecases_list_->clearCommands();

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
    if (usecase_ref == nullptr)
    {
        existing_usecases_list_->addCommand(existing_usecase.getUsecaseName(), ProgramCommand::Status::Invalid);
        parameters_container_->addWidget(std::make_unique<Wt::WContainerWidget>()); // empty page for invalid usecase
    }
    else if (!existing_usecase.hasCommandDataJson() || !existing_usecase.isCommandDataJsonInSync())
    {
        existing_usecases_list_->addCommand(existing_usecase.getUsecaseName(), ProgramCommand::Status::Warning);

        auto params = parameters_container_->addWidget(std::make_unique<ProgramTreeParameters>(
            usecase_ref->getAutoParameters().getParameters(),
            usecase_ref->getRequiredParameters().getParameters(),
            usecase_ref->getAdvancedParameters().getParameters()
        ));
        params->onShowDescription().connect([this](const std::string& title, const std::string& description) {
            showParameterDescriptionPopup(title, description);
        });

        // TODO show parameters + enable CREATE button if all filled
        // TODO re-build parameters UI
    }
    else
    {
        existing_usecases_list_->addCommand(existing_usecase.getUsecaseName(), ProgramCommand::Status::Normal);

        auto params = parameters_container_->addWidget(std::make_unique<ProgramTreeParameters>(
            usecase_ref->getAutoParameters().getParameters(),
            usecase_ref->getRequiredParameters().getParameters(),
            usecase_ref->getAdvancedParameters().getParameters()
        )); 
        params->onShowDescription().connect([this](const std::string& title, const std::string& description) {
            showParameterDescriptionPopup(title, description);
        });

        // TODO set all values too!
        // TODO show parameters + disable CREATE button (already created and in sync)
        // TODO re-build parameters UI
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


ReusableDialog* ProgramTree::displayErrorPopup(const std::string& message)
{
    return showPopupDialog(std::move(std::make_unique<ReusableDialog>("Error", message, std::vector<ButtonDescription>{
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