#pragma once

#include <opencv2/core.hpp>
#include <opencv2/aruco/charuco.hpp>
#include <opencv2/calib3d.hpp>
#include <string>
#include <vector>

#include "calib/types.h"
#include "calib/charuco_board_model.h"
#include "calib/charuco_detector.h"

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    class IntrinsicsCalibrator
    {
    public:
        struct Params
        {
            int minCharucoCornersPerView{12};
            int minViews{8};

            cv::TermCriteria criteria{ cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 50, 1e-9) };
        };

        struct Result
        {
            CameraIntrinsics intr;
            double rms{-1.0};
            std::vector<double> perViewRms;
            std::vector<int> usedViewIndices;
            std::vector<cv::Vec3d> rvecs;
            std::vector<cv::Vec3d> tvecs;
            bool ok{false};
            std::string message;
        };

        explicit IntrinsicsCalibrator(const Params& p = Params());

        Result calibrate(const std::vector<CharucoDetector::Result>& views,
                         const CharucoBoardModel& board,
                         const cv::Size& imageSize) const;

    private:
        Params prm_;

        void buildCharucoArrays(const std::vector<CharucoDetector::Result>& views,
                                const CharucoBoardModel& board,
                                std::vector<std::vector<cv::Point2f>>& allCorners,
                                std::vector<std::vector<int>>& allIds,
                                std::vector<int>& usedIndices) const;

        std::vector<double> computePerViewRmse(const std::vector<std::vector<cv::Point2f>>& allCorners,
                                               const std::vector<std::vector<int>>& allIds,
                                               const CharucoBoardModel& board,
                                               const CameraIntrinsics& intr,
                                               const std::vector<cv::Vec3d>& rvecs,
                                               const std::vector<cv::Vec3d>& tvecs) const;
    };
}
