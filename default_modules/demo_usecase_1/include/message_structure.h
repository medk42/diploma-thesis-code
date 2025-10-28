#pragma once

namespace aergo::default_modules::demo_usecase_1
{
    struct Pen3DPose
    {
        double tvec_[3]; // x, y, z in meters
        double rvec_[3]; // rotation vector (Rodrigues) representing orientation
    };

    inline constexpr char pen_3d_pose_publish_producer_channel_type[] = "pen_3d_pose/v1:struct{tvec:double[3],rvec:double[3]}";
}