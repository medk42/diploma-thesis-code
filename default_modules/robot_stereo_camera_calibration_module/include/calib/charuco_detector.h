#pragma once

#include <opencv2/aruco.hpp>
#include <opencv2/aruco/charuco.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>

#include "calib/types.h"
#include "calib/charuco_board_model.h"

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    struct CharucoDetection
    {
        std::vector<int> ids;
        std::vector<cv::Point2f> corners2d;

        std::vector<int> markerIds;
        std::vector<std::vector<cv::Point2f>> markerCorners;
        std::vector<std::vector<cv::Point2f>> rejectedCandidates;

        cv::Size imageSize{};
        bool ok{false};
    };

    class CharucoDetector
    {
    public:
        struct Params
        {
            int adaptiveWinMin{3};
            int adaptiveWinMax{23};
            int adaptiveWinStep{10};
            double minMarkerPerimeterRate{0.02};
            bool refineSubpix{true};

            cv::Size subpixWin{5, 5};
            int subpixMaxIters{50};
            double subpixEps{0.01};

            int minCharucoCorners{12};
            int minArucoMarkers{4};
        };

        explicit CharucoDetector(const CharucoBoardModel& model, const Params& p = {});

        CharucoDetection detect(const cv::Mat& img) const;

        bool estimateBoardPose(const CharucoDetection& det,
                               const CameraIntrinsics& K,
                               cv::Vec3d& rvec, cv::Vec3d& tvec) const;

    private:
        CharucoBoardModel model_;
        Params prm_;
        cv::aruco::DetectorParameters arucoParams_;
        cv::aruco::ArucoDetector arucoDet_;
    };
}
