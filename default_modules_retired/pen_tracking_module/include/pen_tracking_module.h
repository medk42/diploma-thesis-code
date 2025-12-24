#pragma once

#include "module_common/base_module.h"
#include "module_helpers/activation_wrapper/activable_module.h"
#include "module_helpers/visualization_3d_interface/visualization_helper.h"
#include "pen_calibration_helper.h"
#include "marker_tracker.h"

#include <opencv2/opencv.hpp>

#include <map>
#include <mutex>

namespace aergo::default_modules::pen_tracking_module
{
    namespace vis3d = aergo::module::helpers::visualization_3d_interface;

    class PenTrackingModule : public aergo::module::BaseModule, public aergo::module::helpers::activation_wrapper::IActivableModule
    {
    public:
        PenTrackingModule(const char* data_path, aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::logging::ILogger* logger, uint64_t module_id, const aergo::module::ModuleInfo* module_info);
        
        /// @brief Process incoming calibrated image data messages on subscribe channel 0 and send pen data on publish channel 0.
        virtual void processMessage(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;

        /// @brief Ignore all requests, pen tracking module only produces.
        virtual aergo::module::ResponseData processRequest(uint32_t response_producer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;

        /// @brief Ignore all responses, pen tracking module only produces.
        virtual void processResponse(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override;

        virtual bool valid() noexcept override { return valid_; }
         
        virtual void* query_capability(const std::type_info& id) noexcept override;

        /// @brief Accept only messages on subscribe channel 0 (calibrated image data), drop all others. Accepts via ACCEPT_REPLACE_QUEUE to only keep the latest image.
        virtual IngressDecision onIngress(ProcessingType kind, uint32_t local_channel_id, aergo::module::ChannelIdentifier src, const aergo::module::message::MessageHeader& msg, QueueStatus queue_status) noexcept override;

        /// @brief Module does not use threads.
        virtual bool threadStart(uint32_t timeout_ms) noexcept override { return true; }

        /// @brief Module does not use threads.
        virtual bool threadStop(uint32_t timeout_ms) noexcept override { return true; }

        virtual bool activate(std::vector<std::vector<std::vector<uint8_t>>>& parameter_values, const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) override { return true; }

        virtual bool deactivate(const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled) override { return true; }

        /// @brief No requests needed for activation.
        virtual void sendRequestFromActivation(uint32_t request_consumer_id) override {}

        /// @brief Activation is immediate. No progress to report. 
        virtual aergo::module::helpers::activation_wrapper::message_types::ProgressData getActivationProgress() override { return { .progress_type_ = aergo::module::helpers::activation_wrapper::message_types::ProgressType::NONE }; }

        virtual bool isActivated() override { return false; }

        virtual ISerializableModule::SaveData save() noexcept override
        {
            return ISerializableModule::SaveData { .supports_saving_ = false };
        }

        virtual bool load(ISerializableModule::SaveData data) noexcept override
        {
            return true;
        }

    private:
        bool loadPenCalibration();
        void registerPenVisualization();

        bool valid_;

        bool last_detect_success_ { true };

        uint32_t subscribe_camera_channel_id_{ 0 };
        uint32_t publish_pen_channel_id_{ 0 };

        std::map<int, aergo::pen_calibration::helper::Transformation> tip_to_other_transformations_; // transformations from pen tip to markers

        std::unique_ptr<aergo::pen_tracking::MarkerTracker> marker_tracker_;

        std::unique_ptr<vis3d::VisualizationHelper> vis3d_helper_;
        bool announced_{ false };
        std::mutex vis3d_mutex_;
        vis3d::ResourceId pen_resource_id_{ 0 };
        vis3d::ObjectId pen_object_id_{ 0 };
        bool pen_object_added_{ false };
    };
}