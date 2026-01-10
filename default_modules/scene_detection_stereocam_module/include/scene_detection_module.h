#pragma once

#include "module_common/base_module.h"
#include "module_helpers/scene_detection_helper/message_types.h"
#include "module_helpers/mixed_buffer_allocator/mixed_buffered_allocator.h"
#include "module_helpers/visualization_3d_interface/visualization_helper.h"

#include "detection/scene_marker_detector.h"
#include "detection/stereo_marker_matcher.h"
#include "detection/stereo_marker_optimizer.h"

#include <cstdint>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include <opencv2/core.hpp>

namespace aergo::default_modules::scene_detection_stereocam_module
{
    namespace vis3d = aergo::module::helpers::visualization_3d_interface;

    class SceneDetectionStereocamModule : public aergo::module::BaseModule
    {
    public:
        SceneDetectionStereocamModule(
            const char* data_path,
            aergo::module::ICore* core,
            aergo::module::InputChannelMapInfo channel_map_info,
            const aergo::module::logging::ILogger* logger,
            uint64_t module_id,
            const aergo::module::ModuleInfo* module_info);

        virtual ~SceneDetectionStereocamModule() noexcept = default;

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

        bool threadStart(uint32_t timeout_ms) noexcept override { return true; }
        bool threadStop(uint32_t timeout_ms) noexcept override { return true; }

        aergo::module::ISerializableModule::SaveData save() noexcept override
        {
            return aergo::module::ISerializableModule::SaveData{ .success_ = true, .supports_saving_ = false };
        }

        bool load(aergo::module::ISerializableModule::SaveData data) noexcept override { return true; }

    private:
        bool loadRegisteredObjects();
        bool parseCalibrationHeader(aergo::module::message::MessageHeader message);
        bool parseImageData(aergo::module::message::MessageHeader message);
        void registerBoxResources();
        void updateBoxVisualization();

        bool valid_{false};

        uint32_t subscribe_consumer_id_calibrated_camera_{0};
        uint32_t response_producer_id_scene_detection_{0};
        uint32_t response_producer_id_visualization_{0};

        std::map<int, SceneMarkerDetector::MarkerData> markers_data_;
        std::vector<aergo::module::helpers::scene_detection_helper::RegisteredBox> registered_boxes_;

        std::atomic<bool> image_requested_{false};
        std::mutex image_access_mutex_;
        std::condition_variable image_ready_condition_;

        std::vector<std::byte> blob_buffer_;
        std::unique_ptr<aergo::module::helpers::mixed_buffer_allocator::MixedBufferedAllocator> mixed_buffered_allocator_;
        SceneMarkerDetector::CameraData left_cam_data_, right_cam_data_;
        cv::Mat left_cam_image_, right_cam_image_;

        std::unique_ptr<SceneMarkerDetector> marker_detector_;
        std::array<SceneMarkerDetector::DetectionResult, 2> detection_results_ {
            SceneMarkerDetector::DetectionResult::preallocate(10),
            SceneMarkerDetector::DetectionResult::preallocate(10)
        };
        
        std::unique_ptr<StereoMarkerMatcher> stereo_marker_matcher_;
        StereoMarkerMatcher::MatchResult match_result_ = StereoMarkerMatcher::MatchResult::preallocate(10);

        std::unique_ptr<StereoMarkerOptimizer> stereo_marker_optimizer_;
        std::vector<StereoMarkerOptimizer::Result> optimizer_results_;

        std::vector<aergo::module::helpers::scene_detection_helper::DetectedBox> detected_boxes_;

        std::mutex vis3d_mutex_;
        bool announced_visualization_{false};
        std::unique_ptr<vis3d::VisualizationHelper> visualization_helper_;
        std::map<uint64_t, vis3d::ResourceId> box_resource_ids_;  // box id -> resource id
        std::vector<vis3d::ObjectId> box_object_ids_;
    };
}

