#pragma once

#include "module_common/base_module.h"
#include "module_helpers/activation_wrapper/activable_module.h"
#include "module_helpers/activation_wrapper/message_types.h"
#include "module_helpers/camera_messages/messages.h"
#include "module_helpers/camera_pose_helper/message_structure.h"

#include <atomic>
#include <mutex>
#include <opencv2/core.hpp>

namespace aergo::default_modules::robot_stereo_camera_calibration_module
{
    namespace cm = aergo::module::helpers::camera_messages;
    namespace cph = aergo::module::helpers::camera_pose_helper;

    class RobotStereoCameraCalibrationModule : public aergo::module::BaseModule, public aergo::module::helpers::activation_wrapper::IActivableModule
    {
    public:
        RobotStereoCameraCalibrationModule(
            const char* data_path,
            aergo::module::ICore* core,
            aergo::module::InputChannelMapInfo channel_map_info,
            const aergo::module::logging::ILogger* logger,
            uint64_t module_id,
            const aergo::module::ModuleInfo* module_info);

        virtual ~RobotStereoCameraCalibrationModule() noexcept = default;

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

        bool activate(std::vector<std::vector<std::vector<uint8_t>>>& parameter_values, const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) override;

        bool deactivate(const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) override;

        void sendRequestFromActivation(uint32_t request_consumer_id) override {}

        aergo::module::helpers::activation_wrapper::message_types::ProgressData getActivationProgress() override;

        bool isActivated() override;

        aergo::module::ISerializableModule::SaveData save() noexcept override;
        bool load(aergo::module::ISerializableModule::SaveData data) noexcept override;

    private:
        struct ParsedSampleView
        {
            cph::CameraWithFlangePose camera_pose{};
            const uint8_t* blob_data{nullptr};
            size_t blob_size{0};
        };

        bool parseSample(const std::vector<uint8_t>& raw, ParsedSampleView& out_view, size_t sample_index) noexcept;
        bool validateAndLogSample(const ParsedSampleView& view, size_t sample_index) noexcept;
        bool buildImageView(const cm::BlobHeader& blob_header, const cm::ImageHeader& img_header, int mat_type, const uint8_t* blob_data, cv::Mat& out_mat) noexcept;

        bool valid_{false};
        uint32_t camera_pose_input_channel_{0};

        std::mutex mutex_;
        bool activated_{false};
        bool activation_running_{false};
        size_t activation_total_{0};
        size_t activation_processed_{0};
    };
}
