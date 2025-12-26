#pragma once

#include "module_common/module_interface_.h"
#include "module_helpers/serialization_helper/serialization_helper.h"

#include <chrono>
#include <span>
#include <vector>
#include <cstddef>

namespace aergo::module::helpers::pen_messages
{
    int64_t micros() 
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }


    struct Pose
    {
        double x{0.0};              // X position in world coordinate system, in meters
        double y{0.0};              // Y position in world coordinate system, in meters
        double z{0.0};              // Z position in world coordinate system, in meters

        double qx{0.0};             // X component of orientation as a quaternion
        double qy{0.0};             // Y component of orientation as a quaternion
        double qz{0.0};             // Z component of orientation as a quaternion
        double qw{1.0};             // W component of orientation as a quaternion
    };


    /// @brief Raw pen message containing pose and button states.
    /// Allows access to low-level pen data.
    struct PenMessageRaw
    {
        PenMessageRaw() : timestamp_us(micros()) {}
        PenMessageRaw(Pose p, bool primary, bool secondary)
        : timestamp_us(micros()), pose(p), primary_down(primary), secondary_down(secondary) {}

        int64_t timestamp_us{0};    // Timestamp in microseconds
        Pose pose{};                  // Pen position and orientation in world coordinate system
        bool primary_down{false};   // True if the primary pen button is pressed
        bool secondary_down{false}; // True if the secondary pen button is pressed
    };


    enum class PenIntent : uint8_t
    {
        POSE = 0,              // pen recorded pose data (position + orientation), e.g. pen on pen primary buton click
        TRAJECTORY = 1,        // pen recorded trajectory data (sequence of poses over time), e.g. on pen primary button click, drag and release
        SPECIAL_ACTION = 2     // special action (use depends on context), e.g. on pen secondary button click
    };


    /// @brief Encodes input for a high level user intent with the pen. 
    /// POSE: single pose message (position + orientation), pose in pose field
    /// TRAJECTORY: sequence of poses over time, poses encoded to the SharedDataBlob
    /// SPECIAL_ACTION: special action, no additional data
    struct PenMessageIntent
    {
        PenMessageIntent(PenIntent i = PenIntent::POSE, Pose p = {})
        : timestamp_us(micros()), intent(i), pose(p) {}

        static PenMessageIntent PoseIntent(const Pose& p) { return PenMessageIntent(PenIntent::POSE, p); }
        static PenMessageIntent SpecialActionIntent() { return PenMessageIntent(PenIntent::SPECIAL_ACTION, {}); }
        static PenMessageIntent TrajectoryIntent(std::span<const Pose> poses_span, std::vector<std::byte> out_blob)
        {
            out_blob.clear();
            out_blob.reserve(poses_span.size() * sizeof(Pose) + sizeof(uint64_t));
            using aergo::module::helpers::serialization_helper::serialization::push;
            push<uint64_t>(out_blob, static_cast<uint64_t>(poses_span.size()));
            for (const auto& p : poses_span)
            {
                push<double>(out_blob, p.x);
                push<double>(out_blob, p.y);
                push<double>(out_blob, p.z);
                push<double>(out_blob, p.qx);
                push<double>(out_blob, p.qy);
                push<double>(out_blob, p.qz);
                push<double>(out_blob, p.qw);
            }

            return PenMessageIntent(PenIntent::TRAJECTORY, {});
        }

        int64_t timestamp_us{0};           // Timestamp in microseconds
        PenIntent intent{PenIntent::POSE}; // Intent type
        Pose pose;                         // Pen position and orientation in world coordinate system (for POSE intent)
    };


    bool parseTrajectoryBlob(void* data, size_t size, std::vector<Pose>& out_poses)
    {
        using aergo::module::helpers::serialization_helper::deserialization::BufferReader;

        BufferReader reader(data, size);

        uint64_t pose_count = 0;
        if (!reader.read<uint64_t>(pose_count))
        {
            return false;
        }

        out_poses.clear();
        out_poses.reserve(pose_count);

        for (uint64_t i = 0; i < pose_count; ++i)
        {
            Pose p{};
            if (!reader.read<double>(p.x) ||
                !reader.read<double>(p.y) ||
                !reader.read<double>(p.z) ||
                !reader.read<double>(p.qx) ||
                !reader.read<double>(p.qy) ||
                !reader.read<double>(p.qz) ||
                !reader.read<double>(p.qw))
            {
                return false;
            }
            out_poses.push_back(p);
        }

        return true;
    }

    inline constexpr aergo::module::communication_channel::Producer pen_message_raw_publish_producer{
        .channel_type_identifier_ =
            "pen_message_raw/v1:struct{timestamp_us:int64,pose:struct{position:struct{x:double,y:double,z:double},orientation:struct{x:double,y:double,z:double,w:double}},primary_down:bool,secondary_down:bool}",
        .display_name_ = "Raw pen message",
        .display_description_ = "Low-level pen message containing pose and button states.",
        .prioritized_ = false
    };

    inline constexpr aergo::module::communication_channel::Consumer pen_message_raw_subscribe_consumer{
        .count_ = aergo::module::communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = pen_message_raw_publish_producer.channel_type_identifier_,
        .display_name_ = "Raw pen message",
        .display_description_ = "Low-level pen message containing pose and button states.",
        .prioritized_ = false,
        .message_queue_capacity_ = 4 // we should only need the last message, but they are small
    };

    inline constexpr aergo::module::communication_channel::Producer pen_message_intent_publish_producer{
        .channel_type_identifier_ =
            "pen_message_intent/v1:struct{timestamp_us:int64,intent:uint8,pose:struct{position:struct{x:double,y:double,z:double},orientation:struct{x:double,y:double,z:double,w:double}}}[+blob{pose_count:uint64,poses:struct{position:struct{x:double,y:double,z:double},orientation:struct{x:double,y:double,z:double,w:double}}[pose_count]}]",
        .display_name_ = "Pen message intent",
        .display_description_ = "High-level pen message encoding user intent (user entered pose, trajectory or special action).",
        .prioritized_ = false
    };

    inline constexpr aergo::module::communication_channel::Consumer pen_message_intent_subscribe_consumer{
        .count_ = aergo::module::communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = pen_message_intent_publish_producer.channel_type_identifier_,
        .display_name_ = "Pen message intent",
        .display_description_ = "High-level pen message encoding user intent (user entered pose, trajectory or special action).",
        .prioritized_ = false,
        .message_queue_capacity_ = 4
    };
} 