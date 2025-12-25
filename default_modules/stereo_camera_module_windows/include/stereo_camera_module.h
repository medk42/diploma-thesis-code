#pragma once

#include "CameraWrapper.h"

#include "module_common/base_module.h"
#include "module_helpers/activation_wrapper/activable_module.h"
#include "module_helpers/camera_messages/messages.h"

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

namespace aergo::default_modules::stereo_camera_module
{
    namespace cm = aergo::module::helpers::camera_messages;
    namespace p_desc = aergo::module::helpers::parameter_description;

    class StereoCameraModule : public aergo::module::BaseModule, public aergo::module::helpers::activation_wrapper::IActivableModule
    {
    public:
        StereoCameraModule(const char* data_path, aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::logging::ILogger* logger, uint64_t module_id, const aergo::module::ModuleInfo* module_info)
        : aergo::module::BaseModule(data_path, core, channel_map_info, logger, module_id, module_info) {}
        
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

        /// @brief Start the capture thread.
        virtual bool threadStart(uint32_t timeout_ms) noexcept override;

        /// @brief Stop the capture thread.
        virtual bool threadStop(uint32_t timeout_ms) noexcept override;

        virtual bool activate(std::vector<std::vector<std::vector<uint8_t>>>& parameter_values, const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) override;

        virtual bool deactivate(const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) override;

        virtual bool sendRequestFromActivation(const std::vector<p_desc::ParameterDescription>& auto_parameters, const uint32_t param_id, uint64_t& out_request_id) override { return false; } // No activation-time requests needed.
        virtual ProcessingResult processCustomMessageOrResponse(ProcessingChannelType channel_type, uint32_t consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message, std::vector<uint8_t>& out_data_replace) override { return ProcessingResult::ACCEPT; } // accept the default parameter serialization

        /// @brief Activation is immediate. No progress to report. 
        virtual aergo::module::helpers::activation_wrapper::message_types::ProgressData getActivationProgress() override { return { .progress_type_ = aergo::module::helpers::activation_wrapper::message_types::ProgressType::NONE }; }

        virtual bool isActivated() override { return activated_; }

        virtual ISerializableModule::SaveData save() noexcept override;

        virtual bool load(ISerializableModule::SaveData data) noexcept override;
    
    private:
        void captureLoop();
        bool openCamera();
        void closeCamera();

        std::mutex mutex_;

        std::thread capture_thread_;
        bool thread_running_{false};
        std::atomic<bool> stop_thread_{false};
        std::atomic<bool> activated_{false};
        std::atomic<bool> deactivation_confirmed_{false};

        std::unique_ptr<Camera> camera_;
        AllocatorPtr frame_allocator_;

        cm::BlobHeader frame_blob_header_;
        cm::ImageHeader image_1_header_;
        cm::ImageHeader image_2_header_;

        int64_t res_width_{0};
        int64_t res_height_{0};
        int64_t fps_{0};
        int64_t fps_tolerance_{0};
        int64_t camera_index_{0};
        int64_t manual_exposure_{0};
        bool auto_exposure_{false};
    };
}