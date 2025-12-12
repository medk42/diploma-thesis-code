#pragma once

#include <string>
#include <vector>
#include <optional>

#include "calib/types.h"
#include "calib/charuco_board_model.h"
#include "calib/charuco_detector.h"
#include "calib/charuco_defaults.h"

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    class RigRefinerCeres
    {
    public:
        struct Options
        {
            bool refineStereo{true};
            bool refineHandEye{true};
            bool estimateBoardInWorld{true};
            int maxIters{50};
            double huberDelta{1.0};
        };

        struct Input
        {
            std::vector<CharucoDetection> viewsL;
            std::vector<CharucoDetection> viewsR;
            std::vector<Pose> base_from_flange;
            CharucoBoardModel board;
            CameraIntrinsics KL;
            CameraIntrinsics KR;
            StereoExtrinsics RL;
            SE3 camL_from_flange;
        };

        struct Result
        {
            CameraIntrinsics KL;
            CameraIntrinsics KR;
            StereoExtrinsics RL;
            SE3 camL_from_flange;
            SE3 camR_from_flange;
            SE3 world_from_board;
            double initialReprojRmseL{-1.0};
            double initialReprojRmseR{-1.0};
            double finalReprojRmseL{-1.0};
            double finalReprojRmseR{-1.0};
            double medianSampson{-1.0};
            bool ok{false};
            std::string message;
        };

        explicit RigRefinerCeres(const Options& o = Options());
        Result refine(const Input& in) const;

    private:
        Options opt_;
    };
}
