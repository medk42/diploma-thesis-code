#pragma once

#include "module_common/module_interface_.h"
#include "module_common/base_module.h"
#include "frontend_state.h"

#include "ui/add_module_ui.h"
#include "ui/activation_ui.h"
#include "ui/main_visualization_ui.h"

#include "ui/helper/reusable_dialog.h"
#include "ui/helper/file_dialog.h"

#include <Wt/WApplication.h>
#include <Wt/WStackedWidget.h>
#include <Wt/WServer.h>
#include <Wt/WTimer.h>

#include <atomic>
#include <memory>
#include <chrono>
#include <filesystem>

namespace aergo::default_modules::frontend_module::webapp
{

    class FrontendApp : public Wt::WApplication
    {
    public:
        FrontendApp(const Wt::WEnvironment& env, Wt::WServer* server, FrontendState* frontend_state, aergo::module::BaseModule* base_module, uint32_t activation_request_channel_id);
        ~FrontendApp() override;

        void updateFrame(std::vector<uint8_t>&& jpeg_data); // update camera frame

    private:
        void disconnect(); // called when another instance connects and takes over
        void setupState();

        void setupUi();
        void setupCallbacks();
        void loadUiFromState();

        void handleModuleCreation(size_t available_module_id, ui::AddModuleUi::AddModuleData creation_data);
        void handleModuleDestruction(uint64_t running_module_index);
        void handleModuleDestructionDependencyCheck(uint64_t running_module_index);

        void createModuleCreatedDialog();
        void createModuleDestroyedDialog();
        void createLoadCustomValueDialog();
        void createActivationDialog();
        void updateActivationDialogProgress();

        void refreshRunningModules(); // refreshes the list of running modules in the setup modules screen, does NOT lock the frontend_state_ mutex
        void processPendingActivationResponses(); // process pending activation/deactivation responses, must be called with the frontend_state_ mutex locked

        void dismissDialog();
        void dismissFileDialog();
        void timerUpdate();

        void requestReadCurrentParameters(uint64_t running_module_index); // does not lock, does not check running_module_index validity, requests current parameters from the module if it is activable
        bool parseParameterValues(const std::vector<uint8_t>& data_blob, ActivationData &activation_data); // parse parameter values from data_blob and set to activation_data, does not lock
        void requestAddRemoveListEntry(ui::ActivationUi::ModuleSingleParameter param, bool add); // lock, send request adding or removing a list entry from the module
        void requestParameterChange(ui::ActivationUi::ModuleSingleParameter param, const ui::helper::value_t& value); // lock, send request changing a parameter value in the module
        void requestActivate(uint64_t running_module_index, bool activate); // lock, send request to activate or deactivate the module

        void loadPressedHandler(); // called when load button is pressed in the activation UI
        void savePressedHandler(); // called when save button is pressed in the activation UI
        bool getSaveDirectory(std::filesystem::path& out_directory); // get directory where state is saved, returns false on failure
        std::vector<std::string> getExistingFilesInDirectory(const std::filesystem::path& directory); // get list of existing files in the directory, returns empty list on failure
        void loadStateFromFile(const std::filesystem::path& file); // load state from file
        void saveStateToFile(const std::filesystem::path& file, bool overwrite_confirmed); // save state to file
        bool readZip(const std::string& file, std::string& project_json_out, std::vector<std::tuple<std::string, std::vector<aergo::module::ISerializableModule::SavedBlob>>>& module_states_out); // read zip file, returns false on failure
        bool writeZip(const std::string& file, const std::string& project_json, const std::vector<std::tuple<std::string, std::vector<aergo::module::ISerializableModule::SavedBlob>>>& module_states); // write zip file, returns false on failure
        void createLoadingStateDialog();
        void createSavingStateDialog();
        void handleLoadingState();
        void handleSavingState();

        std::atomic_bool connected_;

        FrontendState* frontend_state_;
        Wt::WServer* server_;
        aergo::module::BaseModule* base_module_;
        aergo::module::ICoreControl* core_; // cached pointer

        std::string session_id_;
        std::shared_ptr<int> life_guard_; // used to detect destruction

        Wt::WStackedWidget* main_container_ = nullptr;

        ui::AddModuleUi* add_module_ui_ = nullptr;
        ui::ActivationUi* activation_ui_ = nullptr;
        ui::MainVisualizationUi* main_visualization_ui_ = nullptr;

        ui::helper::ReusableDialog* reusable_dialog_ = nullptr;
        ui::helper::FileDialog* file_dialog_ = nullptr;
        Wt::WTimer* update_timer_ = nullptr;

        uint32_t activation_request_channel_id_{ 0 }; // request channel for activation wrapper requests
    };

}