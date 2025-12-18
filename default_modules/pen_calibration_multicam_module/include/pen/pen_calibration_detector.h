#pragma once

#include "pen/pen_board_model.h"
#include "calib/pose_utils.h"

#include <opencv2/aruco.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

#include <string>
#include <unordered_set>
#include <vector>

namespace aergo::default_modules::pen_calibration_multicam_module::pen
{
    class PenCalibrationDetector
    {
    public:
        struct Detection
        {
            bool ok{false};
            bool validPose{false};
            std::string msg;
            std::vector<std::vector<cv::Point2f>> markerCorners;
            std::vector<int> markerIds;
            calib::SE3 T_camera_pen;
        };

        PenCalibrationDetector(const PenBoardModel& board_model, int dictionary_id);

        Detection detect(const cv::Mat& gray, const calib::CameraIntrinsics& K) const;

    private:
        const PenBoardModel& board_model_;
        std::unordered_set<int> allowed_ids_;
        cv::Ptr<cv::aruco::Board> board_;
        cv::aruco::ArucoDetector detector_;
    };
}
