#pragma once

#include "module_common/module_interface_.h"
#include "module_helpers/activation_wrapper/parameter_description.h"
#include "ui/helper/parameter_value.h"
#include "module_helpers/activation_wrapper/async_task.h"
#include "ui/add_module_ui.h"
#include "module_helpers/activation_wrapper/message_types.h"

#include <mutex>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>
#include <memory>
#include <deque>

namespace aergo::default_modules::frontend_module::webapp
{
    class FrontendApp;

    enum class FrontendScreen : int
    {
        ADD_MODULE = 0,
        SETUP_MODULES = 1
    };

    

    struct AddModuleParameterDescription
    {
        std::string type_;
        std::vector<ui::helper::value_t> values_;
    };

    struct CreateStateData
    {
        std::vector<std::vector<aergo::module::ChannelIdentifier>> subscribe_channels_; // for each subscribe consumer, one or more mapped channels
        std::vector<std::vector<aergo::module::ChannelIdentifier>> request_channels_;   // for each request consumer, one or more mapped channels

        std::vector<aergo::module::InputChannelMapInfo::IndividualChannelInfo> subscribe_channels_ptrs_; // pointers into creation_data_ for InputChannelMapInfo
        std::vector<aergo::module::InputChannelMapInfo::IndividualChannelInfo> request_channels_ptrs_;   // pointers into creation_data_ for InputChannelMapInfo
    };

    enum class RunningTask
    {
        NONE,
        CREATE_MODULE,
        DESTROY_MODULE
    };

    struct ActivationResponse
    {
        uint64_t running_module_index_;
        bool success_;
        aergo::module::helpers::activation_wrapper::message_types::Response response_;
        std::vector<uint8_t> data_blob_; // extra data from response, e.g. activation parameters
    };

    struct FrontendState
    {
        std::mutex mutex_;
        bool setup_done_ = false; // starts as false, set to true once setupState is done; used to load state from the core

        FrontendApp* active_app_ = nullptr;
        
        FrontendScreen current_screen_ = FrontendScreen::SETUP_MODULES;
        
        std::vector<const aergo::module::ModuleInfo*> available_modules_;
        uint64_t last_modules_mapping_state_id_ = 0; // last known state id of modules mapping, used to detect changes

        std::unique_ptr<CreateStateData> creation_data_; // if set, data for creating a module for the currently running core_->addModule call
        
        std::unique_ptr<aergo::module::helpers::activation_wrapper::AsyncTask<bool>> async_task_;
        RunningTask running_task_ = RunningTask::NONE;

        std::vector<bool> known_running_modules_; // size should correspond to core_->getRunningModulesCount(), true if module exists, false if it was destroyed
        std::vector<const aergo::module::ModuleInfo*> known_running_modules_info_; // size should correspond to core_->getRunningModulesCount(), info of running module
        std::unordered_map<std::string, std::vector<ui::AddModuleUi::ChannelInfo>> running_modules_publish_channel_lookup_; // map of channel type to list of existing publish channels, used when creating modules
        std::unordered_map<std::string, std::vector<ui::AddModuleUi::ChannelInfo>> running_modules_response_channel_lookup_; // map of channel type to list of existing response channels, used when creating modules

        std::deque<ActivationResponse> pending_activation_responses_; // list of pending activation/deactivation responses to be processed by the frontend app

        struct {
            /// @brief Map of required channel type to existing channels. Keys are subscribe channel types that are required by one or more modules to be created.
            /// Values are lists of existing channels that can be used to satisfy the requirement.
            /// Updated when modules are added/removed.
            std::unordered_map<std::string, std::vector<aergo::module::ChannelIdentifier>> required_existing_subscribe_channels_;

            /// @brief Map of required channel type to existing channels. Keys are request channel types that are required by one or more modules to be created.
            /// Values are lists of existing channels that can be used to satisfy the requirement.
            /// Updated when modules are added/removed.
            std::unordered_map<std::string, std::vector<aergo::module::ChannelIdentifier>> required_existing_request_channels_;

            std::vector<AddModuleParameterDescription> subscribe_parameters_;
            std::vector<AddModuleParameterDescription> request_parameters_;
            
        } add_module_data_;

        struct {
            std::vector<aergo::module::RunningModuleInfo> running_modules_;
            uint32_t selected_module_index_ = 0;
        } setup_modules_data_;
    };
}