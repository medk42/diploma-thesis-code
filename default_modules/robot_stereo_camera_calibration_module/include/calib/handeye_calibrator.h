#pragma once

#include <opencv2/calib3d.hpp>
#include <string>
#include <vector>

#include "calib/types.h"

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    class HandEyeCalibrator
    {
    public:
        struct Result
        {
            SE3 T_FC; // cam to flange / flange from cam (p_f = T_FC * p_c)
            bool ok{false};
            std::string message;
        };

        struct Params
        {
            cv::HandEyeCalibrationMethod method {cv::CALIB_HAND_EYE_TSAI};
            int minPairs{8};
        };

        explicit HandEyeCalibrator(const Params& p);

        Result run(const std::vector<Pose>& robotBase_from_flange,
                   const std::vector<cv::Vec3d>& rvec_target_from_cam,
                   const std::vector<cv::Vec3d>& tvec_target_from_cam) const;

        static SE3 composeRightFromLeft(const StereoExtrinsics& right_from_left,
                                        const HandEyeCalibrator::Result& hand_eye_left);

    private:
        Params prm_;
    };
}
