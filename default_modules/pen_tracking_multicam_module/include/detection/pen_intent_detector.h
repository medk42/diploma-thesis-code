#pragma once

#include "module_common/base_module.h"
#include "module_helpers/pen_messages/message_types.h"
#include "module_helpers/mixed_buffer_allocator/mixed_buffered_allocator.h"
#include "module_helpers/visualization_3d_interface/visualization_helper.h"

#include <cstdint>
#include <memory>
#include <chrono>

namespace aergo::default_modules::pen_tracking_multicam_module
{
    namespace pm = aergo::module::helpers::pen_messages;

    namespace vis3d = aergo::module::helpers::visualization_3d_interface;

    class PenIntentDetector
    {
    public:
        PenIntentDetector(
            aergo::module::BaseModule* base_module,
            vis3d::VisualizationHelper* visualization_helper,
            vis3d::ResourceId arrow_resource_id,
            double intent_visualization_timeout_s = 5.0,
            int32_t trajectory_time_threshold_ms = 250);

        void updateButtonState(bool primary_down, bool secondary_down);
        void updatePenPose(pm::Pose pen_tip_world);

        bool valid() const { return valid_; }

    private:
        void checkVisualizationTimeouts();
        void showPoseArrow(const pm::Pose& pose);
        void hidePoseArrow();
        void showTrajectory();
        void updateTrajectory();
        void hideTrajectory();
        
        aergo::module::BaseModule* base_module_;
        vis3d::VisualizationHelper* visualization_helper_;
        vis3d::ResourceId arrow_resource_id_;
        double intent_visualization_timeout_s_;
        int32_t trajectory_time_threshold_ms_;
        
        bool valid_;                          // True if output channel is valid
        uint32_t publish_channel_id_;         // Channel ID for publishing pen_message_intent
        
        // Mixed buffer allocator for trajectory blob data
        std::unique_ptr<aergo::module::helpers::mixed_buffer_allocator::MixedBufferedAllocator> trajectory_allocator_;
        
        // Button state tracking
        bool primary_down_;
        bool secondary_down_;
        bool prev_primary_down_;
        bool prev_secondary_down_;
        
        // Trajectory tracking
        int64_t primary_press_time_us_;       // Timestamp when primary button was pressed
        std::vector<pm::Pose> trajectory_poses_;  // Buffer for trajectory poses (grows dynamically)
        std::vector<std::byte> trajectory_blob_data_;  // Buffer for serialized trajectory data
        
        // Visualization state
        bool pose_arrow_displayed_{false};
        vis3d::ObjectId pose_arrow_object_id_{0};
        std::chrono::steady_clock::time_point pose_arrow_display_time_{};
        
        bool trajectory_displayed_{false};
        vis3d::ObjectId trajectory_object_id_{0};
        std::chrono::steady_clock::time_point trajectory_display_time_{};
        bool trajectory_should_display_{false};  // True when conditions are met but not yet displayed
        size_t trajectory_last_displayed_count_{0};  // Number of points already displayed in trajectory
        std::vector<vis3d::Vec3> trajectory_points_buffer_;  // Reusable buffer for trajectory points to avoid allocations
    };
};