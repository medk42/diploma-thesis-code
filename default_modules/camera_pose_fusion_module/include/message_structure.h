#pragma once

#include "module_helpers/camera_messages/messages.h"
#include "module_helpers/robot_interface/features/robot_control/structs.h"

namespace aergo::default_modules::camera_pose_fusion_module
{
    namespace cm = aergo::module::helpers::camera_messages;
    namespace rc = aergo::module::helpers::robot_interface::robot_control;

    /// @brief Message structure combining camera image data with robot flange pose.
    /// This message contains the camera message header (version and timestamp) and the robot flange pose,
    /// along with the camera image blob(s). This allows downstream modules to know the camera position
    /// in world coordinates by combining this pose with the camera offset from the tool flange.
    struct CameraImageWithPose
    {
        cm::CameraMessage camera_msg;  // Camera message header (version, timestamp_us)
        rc::Pose flange_pose;           // Robot flange pose in world coordinates
    };

    static constexpr aergo::module::communication_channel::Producer camera_image_with_pose_producer
    {
        .channel_type_identifier_ = "camera_image_with_pose/v1:struct{camera_msg:struct{version:uint64,timestamp_us:int64},flange_pose:struct{position:struct{x:double,y:double,z:double},orientation:struct{x:double,y:double,z:double,w:double}}}+blob{BlobHeader+image_count*ImageHeader+image_data}",
        .display_name_ = "Camera Image with Robot Pose",
        .display_description_ = "Camera image output with robot flange pose attached. Contains camera message header, robot flange pose in world coordinates, and camera image blob(s)."
    };
}

