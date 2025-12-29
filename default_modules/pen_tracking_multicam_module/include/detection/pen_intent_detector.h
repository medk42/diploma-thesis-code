#pragma once

#include "module_common/base_module.h"
#include "module_helpers/pen_messages/message_types.h"

#include <cstdint>
#include <vector>

namespace aergo::default_modules::pen_tracking_multicam_module
{
    namespace pm = aergo::module::helpers::pen_messages;

    class PenIntentDetector
    {
    public:
        PenIntentDetector(aergo::module::BaseModule* base_module, int32_t trajectory_time_threshold_ms = 250);

        void updateButtonState(bool primary_down, bool secondary_down);
        void updatePenPose(pm::Pose pen_tip_world);

        bool valid() const { return valid_; }

    private:
        aergo::module::BaseModule* base_module_;
        int32_t trajectory_time_threshold_ms_;
        
        bool valid_;                          // True if output channel is valid
        uint32_t publish_channel_id_;         // Channel ID for publishing pen_message_intent
        
        // Button state tracking
        bool primary_down_;
        bool secondary_down_;
        bool prev_primary_down_;
        bool prev_secondary_down_;
        
        // Trajectory tracking
        int64_t primary_press_time_us_;       // Timestamp when primary button was pressed
        std::vector<pm::Pose> trajectory_poses_;  // Buffer for trajectory poses (grows dynamically)
        std::vector<std::byte> trajectory_blob_data_;  // Buffer for serialized trajectory data (cleared but not reserved, SharedDataBlob not allocated yet)
    };
};