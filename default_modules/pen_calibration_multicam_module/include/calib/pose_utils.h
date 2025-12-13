#pragma once

#include "calib/types.h"

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib::pose_utils
{
    cv::Matx33d quatToR(const Quaternion& q);
    Quaternion rToQuat(const cv::Matx33d& R);

    SE3 toSE3(const Pose& p);
    Pose toPose(const SE3& T);

    SE3 invert(const SE3& T);
    SE3 compose(const SE3& A, const SE3& B); // A * B

    SE3 rtToSE3(const cv::Vec3d& rvec, const cv::Vec3d& tvec);
    void se3ToRt(const SE3& T, cv::Vec3d& rvec, cv::Vec3d& tvec);

    Quaternion normalize(const Quaternion& q);
}
