#include "pen/pen_calibration_detector.h"

#include <opencv2/aruco.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

using namespace aergo::default_modules::pen_calibration_multicam_module;
using namespace aergo::default_modules::pen_calibration_multicam_module::pen;
namespace pose_utils = aergo::default_modules::pen_calibration_multicam_module::calib::pose_utils;

PenCalibrationDetector::PenCalibrationDetector(const PenBoardModel& board_model, int dictionary_id)
: board_model_(board_model)
{
    cv::aruco::DetectorParameters params;
    params.adaptiveThreshWinSizeMin = 3;
    params.adaptiveThreshWinSizeMax = 23;
    params.adaptiveThreshWinSizeStep = 10;
    params.minMarkerPerimeterRate = 0.02;
    params.maxMarkerPerimeterRate = 4.0;
    params.cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
    params.cornerRefinementWinSize = 5;
    params.cornerRefinementMaxIterations = 50;
    params.cornerRefinementMinAccuracy = 0.01;

    auto dict = cv::aruco::getPredefinedDictionary(dictionary_id);
    detector_ = cv::aruco::ArucoDetector(dict, params);

    board_ = board_model_.buildCvBoard();

    allowed_ids_.reserve(board_model_.cadMarkers().size());
    for (const auto& m : board_model_.cadMarkers())
    {
        allowed_ids_.insert(m.id);
    }
}

PenCalibrationDetector::Detection PenCalibrationDetector::detect(const cv::Mat& gray, const calib::CameraIntrinsics& K) const
{
    Detection d;

    if (gray.empty())
    {
        d.msg = "Input image is empty.";
        return d;
    }

    if (K.K.empty() || K.D.empty())
    {
        d.msg = "Camera intrinsics missing.";
        return d;
    }

    if (!board_)
    {
        d.msg = "Board model not initialized.";
        return d;
    }

    std::vector<std::vector<cv::Point2f>> markerCorners;
    std::vector<std::vector<cv::Point2f>> rejected;
    std::vector<int> markerIds;

    detector_.detectMarkers(gray, markerCorners, markerIds, rejected);

    std::vector<std::vector<cv::Point2f>> filteredCorners;
    std::vector<int> filteredIds;
    filteredCorners.reserve(markerCorners.size());
    filteredIds.reserve(markerIds.size());

    for (size_t i = 0; i < markerIds.size(); ++i)
    {
        if (allowed_ids_.find(markerIds[i]) == allowed_ids_.end())
        {
            continue;
        }
        filteredIds.push_back(markerIds[i]);
        filteredCorners.push_back(markerCorners[i]);
    }

    if (filteredIds.empty())
    {
        d.msg = "No pen markers detected.";
        return d;
    }

    d.ok = true;

    cv::Vec3d rvec, tvec;
    const double markersUsed = cv::aruco::estimatePoseBoard(filteredCorners, filteredIds, board_, K.K, K.D, rvec, tvec);
    if (markersUsed <= 0.0)
    {
        d.msg = "Failed to estimate pen pose from detected markers.";
        d.markerCorners = std::move(filteredCorners);
        d.markerIds = std::move(filteredIds);
        return d;
    }

    d.T_camera_pen = pose_utils::rtToSE3(rvec, tvec);
    d.markerCorners = std::move(filteredCorners);
    d.markerIds = std::move(filteredIds);
    d.validPose = true;
    return d;
}
