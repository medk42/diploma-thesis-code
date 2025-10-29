#pragma once

#include "program_list.h"

#include "module_common/base_module.h"
#include "module_helpers/usecase_tree/usecase_tree.h"

#include <memory>
#include <vector>
#include <string>
#include <functional>

#include <Wt/WContainerWidget.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    struct ProgramTreeState
    {
        std::unique_ptr<aergo::module::helpers::usecase_tree::UsecaseTree> usecase_tree_;
    };

    enum class ProgramTreeButtons {
        NewProgram, OpenProgram, SaveProgram,
        CutCommand, CopyCommand, PasteCommand,
        StartProgram, SimulateProgram, StopProgram, PauseProgram, ResumeProgram
    };

    /// @brief UI widget representing the program tree and related controls
    /// UI and frontend_state_ locks shall be held when calling its methods + all callbacks 
    /// from UsecaseTree (so UsecaseTree::handleResponse must be called with UI and frontend_state_ locks held).
    /// However, UI callbacks from buttons etc. do NOT have frontend_state_ lock held, just the UI lock. Use the access_program_state_ function
    /// to access program tree state from those callbacks.
    class ProgramTree : public Wt::WContainerWidget
    {
    public:
        /// @param base_module Pointer to frontend module's BaseModule for logging and communication
        /// @param program_state_unsafe Unsafe reference to program tree state, must be accessed only via access_program_state_
        /// @param with_frontend_state_lock Function to access frontend state data (program_state_unsafe in our case) with frontend_state lock
        ProgramTree(aergo::module::BaseModule* base_module, ProgramTreeState& program_state_unsafe, std::function<void(std::function<void()>)> with_frontend_state_lock);

        // call after available usecases may have changed; call WHILE HOLDING both the frontend_state and UI locks
        void reloadAvailableUsecases();

        // handle button clicks, call WHILE HOLDING both the frontend_state and UI locks
        void onButtonClicked(ProgramTreeButtons button) { /* TODO */ }

    private:
        void setupCallbacks();
        void displayErrorPopup(const std::string& message) { /* TODO */ }
        void addExistingUsecase(const aergo::module::helpers::usecase_tree::structs::ExistingCommand& existing_usecase);

        aergo::module::BaseModule* base_module_{ nullptr };
        std::function<void(std::function<void()>)> with_frontend_state_lock_; // function to access program tree state with frontend_state lock
        ProgramTreeState& program_state_unsafe_; // unsafe reference to program tree state, must be accessed only via access_program_state_

        ProgramList* existing_usecases_list_{ nullptr };
        ProgramList* available_usecases_list_{ nullptr };

        std::vector<std::string> available_usecase_ids_;
    };
}