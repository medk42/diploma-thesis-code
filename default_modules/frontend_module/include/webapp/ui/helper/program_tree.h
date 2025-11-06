#pragma once

#include "program_list.h"
#include "reusable_dialog.h"
#include "file_dialog.h"
#include "program_tree_parameters.h"

#include "module_common/base_module.h"
#include "module_helpers/usecase_tree/usecase_tree.h"
#include "module_helpers/usecase_tree/structs.h"

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <filesystem>

#include <Wt/WContainerWidget.h>
#include <Wt/WStackedWidget.h>
#include <Wt/WTimer.h>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    namespace ut = aergo::module::helpers::usecase_tree;

    struct ProgramTreeState
    {
        std::unique_ptr<aergo::module::helpers::usecase_tree::UsecaseTree> usecase_tree_;

        bool reading_custom_value_ = false; // whether a custom value read is in progress
        std::atomic<bool> cancel_reading_custom_value_{ false }; // request to cancel reading custom value
        bool read_finished_ = false; // whether reading of existing usecases has finished
        bool read_successful_ = false; // whether reading of existing usecases was successful
        size_t read_usecase_index = 0; // index of existing usecase being read initially
        
        bool generating_command_data_json_ = false; // whether command data JSON generation is in progress
        bool generate_finished_ = false; // whether generation of command data JSON was successful
        ut::uw::helper::ErrorInfo generate_error_info_; // error info from generation of command data JSON
        bool generate_successful_ = false; // whether generation of command data JSON was successful
        size_t generate_usecase_index = 0; // index of existing usecase being generated
    };

    enum class ProgramTreeButtons {
        NewProgram, SaveProgram, LoadProgram,
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
        ~ProgramTree() override;

        // call after available usecases may have changed; call WHILE HOLDING both the frontend_state and UI locks
        void reloadAvailableUsecases();

        // handle button clicks, call WHILE HOLDING both the frontend_state and UI locks
        void onButtonClicked(ProgramTreeButtons button);

    private:
        void setupCallbacks();
        ReusableDialog* showParameterDescriptionPopup(const std::string& title, const std::string& description);
        void insertExistingUsecase(const aergo::module::helpers::usecase_tree::structs::ExistingCommand& existing_usecase, size_t index);

        ReusableDialog* showPopupDialog(std::unique_ptr<ReusableDialog> dialog, bool dismiss_on_button_click = true, bool dismiss_on_background_click = true);
        void closePopupDialog();
        ReusableDialog* displayErrorPopup(const std::string& message, const std::string& title = "Error");

        void showReadCustomValueDialog();
        void closeReadCustomValueDialog();

        void showGenerateCommandDataPopup();
        void closeGenerateCommandDataPopup();

        void showReloadUsecasePopup();
        void closeReloadUsecasePopup();

        void closeProgramFileDialog();

        void setupParameterContainer(ProgramTreeParameters* parameter_container, const aergo::module::helpers::usecase_tree::structs::ExistingCommand& existing_usecase, size_t existing_usecase_index);
        std::optional<size_t> existingUsecaseIndexFromParametersWidget(ProgramTreeParameters* parameter_widget) const; // find index from parameter_widget and return it, or std::nullopt if not found
        ut::structs::ExistingCommand* existingUsecaseFromIndex(std::optional<size_t> index_opt) const; // get existing usecase from index, or nullptr if index_opt is std::nullopt or invalid
        void reloadCommandValues(size_t existing_usecase_index); // reload all parameter values from existing_usecase into parameter_widget, set confirm and existing usecase status accordingly
        p_desc::ParameterValueOpt convertToParameterValueOpt(const value_opt_t& value_opt) const; // does NOT handle CUSTOM type, bool is only for boolean parameters
        void updateCommandStatus(size_t existing_usecase_index); // update confirm button and existing usecase status in parameter_widget

        void setupOnValueAddedCallback(ProgramTreeParameters* parameter_container);
        void setupOnValueRemovedCallback(ProgramTreeParameters* parameter_container);
        void setupOnValueChangedCallback(ProgramTreeParameters* parameter_container);
        void setupConfirmCallback(ProgramTreeParameters* parameter_container);

        void onTimerRefresh();

        void clearExistingUsecases();
        void loadExistingUsecases();

        bool getProgramDirectory(std::filesystem::path& out_directory);
        std::vector<std::string> getExistingProgramsInDirectory(const std::filesystem::path& directory);

        void onCutCommand();
        void onCopyCommand();
        void onPasteCommand();

        void onNewProgram();
        void onSaveProgram();
        void onLoadProgram();

        const char* AERGO_PROGRAM_EXTENSION = ".paergo";

        aergo::module::BaseModule* base_module_{ nullptr };
        std::function<void(std::function<void()>)> with_frontend_state_lock_; // function to access program tree state with frontend_state lock
        ProgramTreeState& program_state_unsafe_; // unsafe reference to program tree state, must be accessed only via access_program_state_

        // child 0 is empty, child 1-N corresponds to parameters of existing usecases (0, N-1). 
        // If a usecase is invalid (not found in available usecases), an empty container is shown.
        Wt::WStackedWidget* parameters_container_{ nullptr };
        Wt::WTimer* refresh_timer_{ nullptr };

        ProgramList* existing_usecases_list_{ nullptr };
        ProgramList* available_usecases_list_{ nullptr };

        ReusableDialog* popup_dialog_{ nullptr };
        ReusableDialog* read_custom_value_dialog_{ nullptr };
        ReusableDialog* generate_command_data_json_dialog_{ nullptr };
        ReusableDialog* reload_usecase_dialog_{ nullptr };
        ReusableDialog* new_program_dialog_{ nullptr };
        FileDialog* program_file_dialog_{ nullptr };

        std::vector<std::string> available_usecase_ids_;
        std::vector<ProgramTreeParameters*> existing_usecase_parameter_widgets_;

        std::optional<ut::structs::ExistingCommand> clipboard_command_{ std::nullopt };
    };
}