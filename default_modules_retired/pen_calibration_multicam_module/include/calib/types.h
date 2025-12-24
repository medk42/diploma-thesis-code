#pragma once

#include <opencv2/core.hpp>
#include "module_helpers/robot_interface/features/robot_control/structs.h"

namespace aergo::default_modules::pen_calibration_multicam_module::calib
{
    using Vector3 = aergo::module::helpers::robot_interface::robot_control::Vector3;
    using Quaternion = aergo::module::helpers::robot_interface::robot_control::Quaternion;
    using Pose = aergo::module::helpers::robot_interface::robot_control::Pose;

    struct SE3
    {
        cv::Matx33d R{cv::Matx33d::eye()};
        cv::Vec3d t{0.0, 0.0, 0.0};
    };

    struct CameraIntrinsics
    {
        cv::Mat K; // 3x3 CV_64F
        cv::Mat D; // 1xN CV_64F (N in {5..14})
        cv::Size imageSize;
    };
}
