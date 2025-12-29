#pragma once

#include "module_common/base_module.h"
#include "module_helpers/visualization_3d_interface/visualization_helper.h"

#include "detection/marker_detection.h"
#include "detection/multicam_pose_estimator.h"
#include "detection/multicam_pose_optimizer.h"
#include "detection/pose_one_euro_filter.h"
#include "detection/ble_reader.h"
#include "detection/pen_intent_detector.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace aergo::default_modules::pen_tracking_multicam_module
{
    namespace vis3d = aergo::module::helpers::visualization_3d_interface;

    class PenButtonState
    {
    public:
        struct State
        {
            bool primary_down;
            bool secondary_down;
        };

        void update(bool primary_down, bool secondary_down)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            button_primary_ = primary_down;
            button_secondary_ = secondary_down;
        }

        State readAndReset()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return {
                .primary_down = button_primary_,
                .secondary_down = button_secondary_
            };
        }

    private:
        mutable std::mutex mutex_;
        bool button_primary_;
        bool button_secondary_;
    };

    class PenTrackingMulticamModule : public aergo::module::BaseModule
    {
    public:
        PenTrackingMulticamModule(
            const char* data_path,
            aergo::module::ICore* core,
            aergo::module::InputChannelMapInfo channel_map_info,
            const aergo::module::logging::ILogger* logger,
            uint64_t module_id,
            const aergo::module::ModuleInfo* module_info);

        virtual ~PenTrackingMulticamModule() noexcept = default;

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
            aergo::module::message::MessageHeader message) noexcept override;

        void processResponse(
            uint32_t request_consumer_id,
            aergo::module::ChannelIdentifier source_channel,
            aergo::module::message::MessageHeader message) noexcept override
        {
            // no outgoing requests
        }

        // all processing happens in processMessage
        bool threadStart(uint32_t timeout_ms) noexcept override { return true; }
        bool threadStop(uint32_t timeout_ms) noexcept override;

        // State-less module, no saving/loading needed
        aergo::module::ISerializableModule::SaveData save() noexcept override
        { 
            return aergo::module::ISerializableModule::SaveData{ .success_ = true, .supports_saving_ = false };
        }

        bool load(aergo::module::ISerializableModule::SaveData data) noexcept override { return true; }

    private:
        struct ArrowConfig
        {
            float line_length_m = 0.06f;
            float line_radius_m = 0.002f;
            float tip_radius_m = 0.005f;
            float tip_length_m = 0.01f;
        };

        void onBlePacket(PenDataPacket packet);
        bool parseCalibrationHeader(aergo::module::message::MessageHeader message, uint32_t& out_camera_count); // never makes camerasDataBuffer_ smaller, ensure that you do not read past out_camera_count
        bool parseImageData(aergo::module::message::MessageHeader message, uint32_t expected_camera_count); // never makes cameraImagesBuffer_ smaller, ensure that you do not read past expected_camera_count
        void registerResources();
        void registerFixedResources(ArrowConfig arrow_cfg, vis3d::Color trajectory_color = vis3d::Color{ 0xff, 0x6b, 0xf0, 0xFF });
        void penVisualizationRemove();
        void penVisualizationUpdate(
            const cv::Vec3d& t_world_pen, const cv::Vec4d& q_world_pen, 
            const cv::Vec3d& t_world_arrow, const cv::Vec4d& q_world_arrow
        );

        bool valid_{false};

        uint32_t subscribe_consumer_id_calibrated_camera_{0};
        uint32_t publish_producer_id_pen_raw_{0};
        uint32_t publish_producer_id_pen_intent_{0};
        uint32_t response_producer_id_visualization_{0};


        std::mutex vis3d_mutex_;
        bool announced_visualization_{ false };

        vis3d::ResourceId pen_resource_id_{ 0 };
        vis3d::ResourceId arrow_resource_id_{ 0 };
        vis3d::Color trajectory_color_{};

        bool pen_displayed_{ false };
        vis3d::ObjectId pen_object_id_{ 0 };
        bool arrow_displayed_{ false };
        vis3d::ObjectId arrow_object_id_{ 0 };
        
        std::unique_ptr<vis3d::VisualizationHelper> visualization_helper_;


        std::vector<MarkerDetector> markerDetectors_;
        std::vector<MarkerDetector::DetectionResult> detectionsBuffer_;

        MulticamPoseEstimator poseEstimator_;
        MulticamPoseEstimator::DetectionResult poseEstimationResult_;

        MulticamPoseOptimizer poseOptimizer_;
        MulticamPoseOptimizer::Result poseOptimizationResult_;

        PoseOneEuroFilter poseFilter_;

        BleReader ble_reader_;

        pu::SE3 T_pen_tip_; // pen <- tip


        std::vector<MulticamPoseEstimator::CameraData> camerasDataBuffer_;
        std::vector<cv::Mat> grayCameraImagesBuffer_;


        PenButtonState pen_button_state_; // protected by its own mutex
        std::unique_ptr<PenIntentDetector> pen_intent_detector_;
    };
}