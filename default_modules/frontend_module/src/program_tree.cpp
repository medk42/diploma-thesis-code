#include "webapp/ui/helper/program_tree.h"
#include "webapp/ui/helper/program_tree_parameters.h"
#include "webapp/ui/helper/program_command.h"

#include "webapp/ui/helper/program_tree_dummy_params.h"

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

    reloadAvailableUsecases();
    setupCallbacks();

    auto parameters_container = addWidget(std::make_unique<Wt::WContainerWidget>());
    parameters_container->setStyleClass("program-tree-parameters-container");



    // Dummy parameters for demonstration
    auto [dummy_auto, dummy_required, dummy_advanced] = generateParams();

    auto parameters = parameters_container->addWidget(
        std::make_unique<ProgramTreeParameters>(
            dummy_auto, dummy_required, dummy_advanced
        )
    );
} 


void ProgramTree::reloadAvailableUsecases() // called with UI and frontend_state_ locks held
{
    base_module_->log(aergo::module::logging::LogType::INFO, "ProgramTree::reloadAvailableUsecases: Reloading available usecases...");
    bool success = program_state_unsafe_.usecase_tree_->updateAvailableUsecases([this](bool success, const std::map<std::string, structs::AvailableUsecase>& available_usecases) {
        available_usecases_list_->clearCommands();
        available_usecase_ids_.clear();
        existing_usecases_list_->clearCommands();

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
        });
    });
}


void ProgramTree::addExistingUsecase(const aergo::module::helpers::usecase_tree::structs::ExistingCommand& existing_usecase)
{
    if (existing_usecase.getUsecaseReference() == nullptr)
    {
        existing_usecases_list_->addCommand(existing_usecase.getUsecaseName(), ProgramCommand::Status::Invalid);
        // TODO hide parameters
    }
    else if (!existing_usecase.hasCommandDataJson() || !existing_usecase.isCommandDataJsonInSync())
    {
        existing_usecases_list_->addCommand(existing_usecase.getUsecaseName(), ProgramCommand::Status::Warning);
        // TODO show parameters + enable CREATE button if all filled
        // TODO re-build parameters UI
    }
    else
    {
        existing_usecases_list_->addCommand(existing_usecase.getUsecaseName(), ProgramCommand::Status::Normal);
        // TODO show parameters + disable CREATE button (already created and in sync)
        // TODO re-build parameters UI
    }
}