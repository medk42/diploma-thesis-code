#pragma once

#include "module_common/base_module.h"
#include "module_helpers/activation_wrapper/activable_module.h"
#include "module_helpers/calibrated_camera_robot_messages/message_types.h"

#include <atomic>

namespace aergo::default_modules::pen_calibration_multicam_module
{
    namespace ccrm = aergo::module::helpers::calibrated_camera_robot_messages;

    class PenCalibrationMulticamModule : public aergo::module::BaseModule, public aergo::module::helpers::activation_wrapper::IActivableModule
    {
    public:
        PenCalibrationMulticamModule(
            const char* data_path,
            aergo::module::ICore* core,
            aergo::module::InputChannelMapInfo channel_map_info,
            const aergo::module::logging::ILogger* logger,
            uint64_t module_id,
            const aergo::module::ModuleInfo* module_info);

        virtual ~PenCalibrationMulticamModule() noexcept = default;

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
            // no outgoing requests
        }

        bool threadStart(uint32_t timeout_ms) noexcept override { return true; }
        bool threadStop(uint32_t timeout_ms) noexcept override { return true; }

        bool activate(std::vector<std::vector<std::vector<uint8_t>>>& parameter_values, const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) override;

        bool deactivate(const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) override;

        void sendRequestFromActivation(uint32_t request_consumer_id) override {}

        aergo::module::helpers::activation_wrapper::message_types::ProgressData getActivationProgress() override;

        bool isActivated() override { return false; }

        aergo::module::ISerializableModule::SaveData save() noexcept override;
        bool load(aergo::module::ISerializableModule::SaveData data) noexcept override;

    private:
        bool valid_{false};
        uint32_t calibrated_camera_subscribe_channel_{0};
    };
}
