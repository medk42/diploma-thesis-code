#pragma once

#include "module_common/module_interface_.h"
#include "module_helpers/camera_messages/messages.h"
#include "module_helpers/robot_interface/features/robot_control/structs.h"

namespace aergo::default_modules::camera_pose_injector_module
{
    namespace cm = aergo::module::helpers::camera_messages;
    namespace rc = aergo::module::helpers::robot_interface::robot_control;

    /// @brief Camera image header extended with current robot flange pose.
    struct CameraWithFlangePose
    {
        cm::CameraMessage camera_header; // original camera header (version + timestamp)
        rc::Pose flange_pose;            // latest known robot flange pose in world coordinates
    };

    // Publish: camera image with flange pose
    static constexpr aergo::module::communication_channel::Producer camera_with_pose_publish_producer {
        .channel_type_identifier_ = "camera_image_with_pose/v1:struct{camera_header:struct{version:uint64,timestamp_us:int64},flange_pose:struct{position:struct{x:double,y:double,z:double},orientation:struct{x:double,y:double,z:double,w:double}}}+blob{BlobHeader+image_count*ImageHeader+image_data}",
        .display_name_ = "Camera Image With Flange Pose",
        .display_description_ = "Camera image data extended with the current robot flange pose (world coordinates)."
    };
}
