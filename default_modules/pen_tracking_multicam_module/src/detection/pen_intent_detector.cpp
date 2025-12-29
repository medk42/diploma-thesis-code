#include "detection/pen_intent_detector.h"

#include <chrono>
#include <cstring>
#include <span>
#include <vector>

namespace aergo::default_modules::pen_tracking_multicam_module
{
    namespace pm = aergo::module::helpers::pen_messages;

    namespace
    {
        inline int64_t micros()
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }
    }

    PenIntentDetector::PenIntentDetector(aergo::module::BaseModule* base_module, int32_t trajectory_time_threshold_ms)
        : base_module_(base_module)
        , trajectory_time_threshold_ms_(trajectory_time_threshold_ms)
        , valid_(false)
        , publish_channel_id_(0)
        , trajectory_allocator_(nullptr)
        , primary_down_(false)
        , secondary_down_(false)
        , prev_primary_down_(false)
        , prev_secondary_down_(false)
        , primary_press_time_us_(0)
        , trajectory_poses_()
        , trajectory_blob_data_()
    {
        if (!base_module_)
        {
            return;
        }

        if (!base_module_->getPublishChannelByName(
                pm::pen_message_intent_publish_producer.channel_type_identifier_,
                publish_channel_id_))
        {
            base_module_->log(aergo::module::logging::LogType::ERROR,
                "PenIntentDetector: Failed to find pen_message_intent publish channel");
            return;
        }

        // Create mixed buffer allocator: 5 slots, each with space for ~500 poses
        // Size calculation: 8 bytes (uint64_t count) + 500 * 7 * 8 bytes (7 doubles per pose) = 28008 bytes
        constexpr uint64_t slot_size_bytes = 8 * (1 + 500 * 7); // 28008 bytes
        constexpr uint32_t number_of_slots = 5;
        trajectory_allocator_ = aergo::module::helpers::mixed_buffer_allocator::MixedBufferedAllocator::create(
            base_module_, slot_size_bytes, number_of_slots);
        
        if (!trajectory_allocator_)
        {
            base_module_->log(aergo::module::logging::LogType::ERROR,
                "PenIntentDetector: Failed to create trajectory allocator");
            return;
        }

        // Reserve initial capacity for trajectory buffer (will grow as needed)
        trajectory_poses_.reserve(64);
        valid_ = true;
    }

    void PenIntentDetector::updateButtonState(bool primary_down, bool secondary_down)
    {
        if (!valid_)
        {
            return;
        }

        prev_primary_down_ = primary_down_;
        prev_secondary_down_ = secondary_down_;
        primary_down_ = primary_down;
        secondary_down_ = secondary_down;

        // Handle secondary button press (SPECIAL_ACTION)
        if (!prev_secondary_down_ && secondary_down_)
        {
            // Secondary button just pressed - emit SPECIAL_ACTION
            pm::PenMessageIntent intent = pm::PenMessageIntent::SpecialActionIntent();
            base_module_->sendMessage(publish_channel_id_, aergo::module::message::MessageHeader::Message(&intent));
        }

        // Handle primary button state changes
        if (!prev_primary_down_ && primary_down_)
        {
            // Primary button just pressed - start tracking trajectory
            primary_press_time_us_ = micros();
            trajectory_poses_.clear();
        }
        else if (prev_primary_down_ && !primary_down_)
        {
            // Primary button just released - determine intent and emit
            int64_t press_duration_us = micros() - primary_press_time_us_;
            int64_t press_duration_ms = press_duration_us / 1000;

            if (press_duration_ms >= trajectory_time_threshold_ms_ && !trajectory_poses_.empty())
            {
                // Long press = TRAJECTORY
                trajectory_blob_data_.clear();
                std::span<const pm::Pose> poses_span(trajectory_poses_);
                pm::PenMessageIntent intent = pm::PenMessageIntent::TrajectoryIntent(poses_span, trajectory_blob_data_);
                
                // Allocate blob from mixed allocator with size of serialized data
                const uint64_t blob_size = trajectory_blob_data_.size();
                aergo::module::message::SharedDataBlob trajectory_blob = trajectory_allocator_->allocate(blob_size);
                
                if (!trajectory_blob.valid())
                {
                    base_module_->log(aergo::module::logging::LogType::WARNING,
                        "PenIntentDetector: Failed to allocate trajectory blob");
                }
                else
                {
                    // Copy serialized data into blob
                    std::memcpy(trajectory_blob.data(), trajectory_blob_data_.data(), blob_size);
                    
                    // Send message with blob attached
                    base_module_->sendMessage(publish_channel_id_, 
                        aergo::module::message::MessageHeader::Message(&intent, &trajectory_blob));
                }
            }
            else if (!trajectory_poses_.empty())
            {
                // Short press = POSE (use the first pose)
                pm::PenMessageIntent intent = pm::PenMessageIntent::PoseIntent(trajectory_poses_[0]);
                base_module_->sendMessage(publish_channel_id_, aergo::module::message::MessageHeader::Message(&intent));
            }
            trajectory_poses_.clear();
        }
    }

    void PenIntentDetector::updatePenPose(pm::Pose pen_tip_world)
    {
        if (!valid_)
        {
            return;
        }

        // If primary button is down, accumulate poses for trajectory
        if (primary_down_)
        {
            trajectory_poses_.push_back(pen_tip_world);
        }
    }
}

