#pragma once

#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "calib/types.h"
#include "calib/charuco_board_model.h"
#include "calib/charuco_detector.h"

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    class StereoCalibrator
    {
    public:
        struct Params
        {
            int minSharedCharucoCorners{10};
            int minPairs{8};
            bool fixIntrinsics{true};
        };

        struct Pair
        {
            int index{-1};
            std::vector<cv::Point3f> objPts;
            std::vector<cv::Point2f> imgL;
            std::vector<cv::Point2f> imgR;
            std::vector<int> ids;
        };

        struct Result
        {
            StereoExtrinsics extr;
            cv::Mat E;
            cv::Mat F;
            double rms{-1.0};

            double meanSampson{-1.0};
            double medianSampson{-1.0};

            std::vector<int> usedPairIndices;
            bool ok{false};
            std::string message;
        };

        explicit StereoCalibrator(const Params& p = Params());

        Result calibrate(const std::vector<CharucoDetection>& viewsL,
                         const std::vector<CharucoDetection>& viewsR,
                         const CharucoBoardModel& board,
                         const CameraIntrinsics& KL,
                         const CameraIntrinsics& KR) const;

    private:
        std::vector<Pair> buildPairs(const std::vector<CharucoDetection>& viewsL,
                                     const std::vector<CharucoDetection>& viewsR,
                                     const CharucoBoardModel& board) const;

        std::pair<double, double> epipolarStats(const std::vector<Pair>& pairs,
                                                const cv::Mat& F) const;

        Params prm_;
    };
}
