#pragma once

#include "image_structure.h"

#include "module_common/base_module.h"
#include "module_helpers/activation_wrapper/activable_module.h"

#include <memory>
#include <thread>
#include <atomic>

#include <opencv2/opencv.hpp>

namespace aergo::default_modules::camera_module
{
    class CameraModule : public aergo::module::BaseModule, public aergo::module::helpers::activation_wrapper::IActivableModule
    {
    public:
        CameraModule(const char* data_path, aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::logging::ILogger* logger, uint64_t module_id);
        
        /// @brief Ignore all messages, camera module only produces.
        virtual void processMessage(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override {}

        /// @brief Ignore all requests, camera module only produces.
        virtual aergo::module::ResponseData processRequest(uint32_t response_producer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override { return { .success_ = false }; }

        /// @brief Ignore all responses, camera module only produces.
        virtual void processResponse(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override {}

        virtual bool valid() noexcept override { return true; }
        
        virtual void* query_capability(const std::type_info& id) noexcept override;

        /// @brief Drop all incoming messages, camera module only produces.
        virtual IngressDecision onIngress(ProcessingType kind, uint32_t local_channel_id, aergo::module::ChannelIdentifier src, const aergo::module::message::MessageHeader& msg, QueueStatus queue_status) noexcept override { return IngressDecision::DROP; }

        /// @brief Starting thread is handled in activate. 
        virtual bool threadStart(uint32_t timeout_ms) noexcept override { return true; }

        /// @brief Stopping thread is handled in deactivate.
        virtual bool threadStop(uint32_t timeout_ms) noexcept override { return true; }

        virtual bool activate(std::vector<std::vector<std::vector<uint8_t>>>& parameter_values, std::atomic<bool>& cancel_flag) override;

        virtual bool deactivate(std::atomic<bool>& cancel_flag) override;

        /// @brief No requests needed for activation.
        virtual void sendRequestFromActivation(uint32_t request_consumer_id) override {}

        /// @brief Activation is immediate. No progress to report. 
        virtual aergo::module::helpers::activation_wrapper::message_types::ProgressData getActivationProgress() override { return { .progress_type_ = aergo::module::helpers::activation_wrapper::message_types::ProgressType::NONE }; }
    
    private:
        void log(aergo::module::logging::LogType type, const std::string& message) { aergo::module::BaseModule::log(type, message.c_str()); }
        int64_t nowMs() noexcept;
        void captureLoop();

        std::unique_ptr<cv::VideoCapture> cap_;

        ImageHeader frame_header_;
        AllocatorPtr frame_allocator_;

        std::thread capture_thread_;
        std::atomic<bool> stop_thread_{false};

        bool activated_{false};
        std::mutex activation_mutex_;
    };
}