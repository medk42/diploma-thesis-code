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
            SE3 cam_from_flange;
            double rms{-1.0};
            bool ok{false};
            std::string message;
        };

        struct Params
        {
            cv::HandEyeCalibrationMethod method {cv::CALIB_HAND_EYE_TSAI};
            int minPairs{8};
        };

        explicit HandEyeCalibrator(const Params& p = Params());

        Result run(const std::vector<Pose>& robotBase_from_flange,
                   const std::vector<cv::Vec3d>& rvec_target_from_cam,
                   const std::vector<cv::Vec3d>& tvec_target_from_cam) const;

        static SE3 composeRightFromLeft(const StereoExtrinsics& right_from_left,
                                        const SE3& camL_from_flange);

    private:
        Params prm_;
    };
}
