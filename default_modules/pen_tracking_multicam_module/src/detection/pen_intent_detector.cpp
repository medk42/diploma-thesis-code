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

    PenIntentDetector::PenIntentDetector(
        aergo::module::BaseModule* base_module,
        vis3d::VisualizationHelper* visualization_helper,
        vis3d::ResourceId arrow_resource_id,
        double intent_visualization_timeout_s,
        int32_t trajectory_time_threshold_ms)
        : base_module_(base_module)
        , visualization_helper_(visualization_helper)
        , arrow_resource_id_(arrow_resource_id)
        , intent_visualization_timeout_s_(intent_visualization_timeout_s)
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
            // Hide any existing trajectory from previous interaction
            if (trajectory_displayed_)
            {
                hideTrajectory();
            }
            primary_press_time_us_ = micros();
            trajectory_poses_.clear();
            trajectory_should_display_ = false;
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
                
                // Start timer for trajectory when button is released (trajectory stays visible)
                if (trajectory_displayed_)
                {
                    trajectory_display_time_ = std::chrono::steady_clock::now();
                }
            }
            else if (!trajectory_poses_.empty())
            {
                // Short press = POSE (use the first pose)
                pm::PenMessageIntent intent = pm::PenMessageIntent::PoseIntent(trajectory_poses_[0]);
                base_module_->sendMessage(publish_channel_id_, aergo::module::message::MessageHeader::Message(&intent));
                
                // Hide trajectory when pose is detected
                if (trajectory_displayed_)
                {
                    hideTrajectory();
                }
                
                // Show pose arrow visualization
                showPoseArrow(trajectory_poses_[0]);
            }
            
            trajectory_should_display_ = false;
            trajectory_poses_.clear();
        }
        
        // Check if trajectory should start displaying (call addTrajectory once when conditions are met)
        if (primary_down_)
        {
            int64_t press_duration_us = micros() - primary_press_time_us_;
            int64_t press_duration_ms = press_duration_us / 1000;
            
            if (press_duration_ms >= trajectory_time_threshold_ms_ && 
                trajectory_poses_.size() >= 2 && 
                !trajectory_should_display_)
            {
                trajectory_should_display_ = true;
                showTrajectory();  // Calls addTrajectory once
            }
        }
        
        checkVisualizationTimeouts();
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
            
            // Update trajectory visualization if it's being displayed
            if (trajectory_displayed_)
            {
                updateTrajectory();
            }
        }
        
        checkVisualizationTimeouts();
    }
    
    void PenIntentDetector::checkVisualizationTimeouts()
    {
        if (!visualization_helper_)
        {
            return;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto timeout_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double>(intent_visualization_timeout_s_));
        
        // Check pose arrow timeout
        if (pose_arrow_displayed_)
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - pose_arrow_display_time_);
            if (elapsed >= timeout_duration)
            {
                hidePoseArrow();
            }
        }
        
        // Check trajectory timeout (only if timer has been started, i.e., button was released)
        if (trajectory_displayed_ && trajectory_display_time_.time_since_epoch().count() > 0)
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - trajectory_display_time_);
            if (elapsed >= timeout_duration)
            {
                hideTrajectory();
            }
        }
    }
    
    void PenIntentDetector::showPoseArrow(const pm::Pose& pose)
    {
        if (!visualization_helper_ || !visualization_helper_->valid())
        {
            return;
        }
        
        // Hide existing pose arrow if any
        if (pose_arrow_displayed_)
        {
            hidePoseArrow();
        }
        
        vis3d::Pose arrow_pose{
            .t = { static_cast<float>(pose.x), static_cast<float>(pose.y), static_cast<float>(pose.z) },
            .q = { static_cast<float>(pose.qx), static_cast<float>(pose.qy), static_cast<float>(pose.qz), static_cast<float>(pose.qw) }
        };
        
        if (visualization_helper_->addObject(arrow_resource_id_, arrow_pose, pose_arrow_object_id_))
        {
            pose_arrow_displayed_ = true;
            pose_arrow_display_time_ = std::chrono::steady_clock::now();
            visualization_helper_->sendUpdate();
        }
    }
    
    void PenIntentDetector::hidePoseArrow()
    {
        if (!visualization_helper_ || !pose_arrow_displayed_)
        {
            return;
        }
        
        visualization_helper_->removeObject(pose_arrow_object_id_);
        pose_arrow_displayed_ = false;
        visualization_helper_->sendUpdate();
    }
    
    void PenIntentDetector::showTrajectory()
    {
        if (!visualization_helper_ || !visualization_helper_->valid() || trajectory_poses_.size() < 2)
        {
            return;
        }
        
        // Hide existing trajectory and pose arrow if any
        if (trajectory_displayed_)
        {
            hideTrajectory();
        }
        if (pose_arrow_displayed_)
        {
            hidePoseArrow();
        }
        
        // Convert poses to Vec3 points using reusable buffer
        trajectory_points_buffer_.clear();
        trajectory_points_buffer_.reserve(trajectory_poses_.size());
        for (const auto& pose : trajectory_poses_)
        {
            trajectory_points_buffer_.push_back({
                static_cast<float>(pose.x),
                static_cast<float>(pose.y),
                static_cast<float>(pose.z)
            });
        }
        
        vis3d::Color trajectory_color{ 0xff, 0x6b, 0xf0, 0xff }; // pink, matching module default
        if (visualization_helper_->addTrajectory(trajectory_points_buffer_, trajectory_color, false, trajectory_object_id_))
        {
            trajectory_displayed_ = true;
            // Timer starts when button is released, not when trajectory is first shown
            trajectory_display_time_ = std::chrono::steady_clock::time_point{}; // Will be set when button is released
            trajectory_last_displayed_count_ = trajectory_poses_.size();
            visualization_helper_->sendUpdate();
        }
    }
    
    void PenIntentDetector::updateTrajectory()
    {
        if (!visualization_helper_ || !trajectory_displayed_ || trajectory_poses_.size() < 2)
        {
            return;
        }
        
        // Add only new points since last update using reusable buffer
        if (trajectory_poses_.size() > trajectory_last_displayed_count_)
        {
            trajectory_points_buffer_.clear();
            trajectory_points_buffer_.reserve(trajectory_poses_.size() - trajectory_last_displayed_count_);
            
            for (size_t i = trajectory_last_displayed_count_; i < trajectory_poses_.size(); ++i)
            {
                const auto& pose = trajectory_poses_[i];
                trajectory_points_buffer_.push_back({
                    static_cast<float>(pose.x),
                    static_cast<float>(pose.y),
                    static_cast<float>(pose.z)
                });
            }
            
            visualization_helper_->updateTrajectory(trajectory_object_id_, trajectory_points_buffer_, 0);
            trajectory_last_displayed_count_ = trajectory_poses_.size();
            visualization_helper_->sendUpdate();
        }
    }
    
    void PenIntentDetector::hideTrajectory()
    {
        if (!visualization_helper_ || !trajectory_displayed_)
        {
            return;
        }
        
        visualization_helper_->removeTrajectory(trajectory_object_id_);
        trajectory_displayed_ = false;
        trajectory_should_display_ = false;
        trajectory_last_displayed_count_ = 0;
        visualization_helper_->sendUpdate();
    }
}

