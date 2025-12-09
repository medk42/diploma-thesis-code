#pragma once

#include "module_common/base_module.h"
#include "module_helpers/camera_messages/messages.h"
#include "module_helpers/robot_interface/message_types.h"
#include "module_helpers/robot_interface/features/robot_control/messages.h"
#include "message_structure.h"

#include <mutex>
#include <atomic>

namespace aergo::default_modules::camera_pose_fusion_module
{
    namespace cm = aergo::module::helpers::camera_messages;
    namespace ri = aergo::module::helpers::robot_interface;
    namespace rc = aergo::module::helpers::robot_interface::robot_control;

    class CameraPoseFusionModule : public aergo::module::BaseModule
    {
    public:
        CameraPoseFusionModule(
            const char* data_path,
            aergo::module::ICore* core,
            aergo::module::InputChannelMapInfo channel_map_info,
            const aergo::module::logging::ILogger* logger,
            uint64_t module_id,
            const aergo::module::ModuleInfo* module_info
        );

        virtual void processMessage(
            uint32_t subscribe_consumer_id,
            aergo::module::ChannelIdentifier source_channel,
            aergo::module::message::MessageHeader message
        ) noexcept override;

        virtual aergo::module::ResponseData processRequest(
            uint32_t response_producer_id,
            aergo::module::ChannelIdentifier source_channel,
            aergo::module::message::MessageHeader message
        ) noexcept override { return { .success_ = false }; }

        virtual void processResponse(
            uint32_t request_consumer_id,
            aergo::module::ChannelIdentifier source_channel,
            aergo::module::message::MessageHeader message
        ) noexcept override { /* No requests handled */ }

        virtual bool valid() noexcept override { return valid_; }

        virtual void* query_capability(const std::type_info& id) noexcept override;

        virtual IngressDecision onIngress(ProcessingType kind, uint32_t local_channel_id, aergo::module::ChannelIdentifier src, const aergo::module::message::MessageHeader& msg, QueueStatus queue_status) noexcept override;

        virtual bool threadStart(uint32_t timeout_ms) noexcept override { return true; }

        virtual bool threadStop(uint32_t timeout_ms) noexcept override { return true; }

        virtual ISerializableModule::SaveData save() noexcept override
        {
            ISerializableModule::SaveData data;
            data.supports_saving_ = false;
            return data;
        }

        virtual bool load(ISerializableModule::SaveData data) noexcept override { return true; }

    private:
        void handleRobotStatusMessage(aergo::module::message::MessageHeader message);
        void handleCameraMessage(aergo::module::message::MessageHeader message);

        uint32_t camera_consumer_id_;
        uint32_t robot_status_consumer_id_;
        uint32_t output_producer_id_;

        std::mutex pose_mutex_;
        rc::Pose current_flange_pose_;
        std::atomic<bool> pose_available_{false};
        bool valid_{false};
    };
}

