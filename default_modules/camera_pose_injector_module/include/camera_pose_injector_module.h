#pragma once

#include "module_common/base_module.h"
#include "message_structure.h"

#include <mutex>
#include <optional>

namespace aergo::default_modules::camera_pose_injector_module
{
    namespace cm = aergo::module::helpers::camera_messages;
    namespace ri = aergo::module::helpers::robot_interface;
    namespace rc = ri::robot_control;

    class CameraPoseInjectorModule : public aergo::module::BaseModule
    {
    public:
        CameraPoseInjectorModule(
            const char* data_path,
            aergo::module::ICore* core,
            aergo::module::InputChannelMapInfo channel_map_info,
            const aergo::module::logging::ILogger* logger,
            uint64_t module_id,
            const aergo::module::ModuleInfo* module_info);

        virtual ~CameraPoseInjectorModule() noexcept = default;

        bool valid() noexcept override { return valid_; }

        void* query_capability(const std::type_info& id) noexcept override;

        aergo::module::IModule::IngressDecision onIngress(
            ProcessingType kind,
            uint32_t local_channel_id,
            aergo::module::ChannelIdentifier src,
            const aergo::module::message::MessageHeader& msg,
            QueueStatus queue_status) noexcept override;

        void processMessage(
            uint32_t subscribe_consumer_id,
            aergo::module::ChannelIdentifier source_channel,
            aergo::module::message::MessageHeader message) noexcept override;

        aergo::module::ResponseData processRequest(
            uint32_t response_producer_id,
            aergo::module::ChannelIdentifier source_channel,
            aergo::module::message::MessageHeader message) noexcept override
        {
            return { .success_ = false };
        }

        void processResponse(
            uint32_t request_consumer_id,
            aergo::module::ChannelIdentifier source_channel,
            aergo::module::message::MessageHeader message) noexcept override
        {
            // no requests are issued by this module
        }

        bool threadStart(uint32_t timeout_ms) noexcept override { return true; }
        bool threadStop(uint32_t timeout_ms) noexcept override { return true; }

        aergo::module::ISerializableModule::SaveData save() noexcept override;
        bool load(aergo::module::ISerializableModule::SaveData data) noexcept override;

    private:
        void handleRobotStatus(const aergo::module::message::MessageHeader& message) noexcept;
        void handleCameraMessage(const aergo::module::message::MessageHeader& message) noexcept;

        bool valid_{false};
        uint32_t camera_input_channel_{0};
        uint32_t robot_status_channel_{1};
        uint32_t publish_channel_{0};

        std::mutex pose_mutex_;
        std::optional<rc::Pose> latest_flange_pose_;
    };
}
