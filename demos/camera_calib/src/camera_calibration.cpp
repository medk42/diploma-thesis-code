#include <opencv2/opencv.hpp>

#include "camera_calibration.h"
#include "camera_calibration_exception.h"
#include "logging.h"

using namespace aergo::camera_calibration;

CharucoCalibration::CharucoCalibration(cv::aruco::CharucoBoard charuco_board, int corner_refine_method, int min_required_corners)
: min_required_corners_(min_required_corners)
{
    cv::aruco::DetectorParameters detector_parameters;
    detector_parameters.cornerRefinementMethod = corner_refine_method;

    charuco_detector_ = std::make_unique<cv::aruco::CharucoDetector>(
        charuco_board,
        cv::aruco::CharucoParameters(),
        detector_parameters,
        cv::aruco::RefineParameters()
    );
}

bool CharucoCalibration::addImage(cv::Mat image, cv::Mat* return_visualization)
{
    if (image.empty())
    {
        return false;
    }

    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

    std::vector<cv::Point2f> charuco_corners;
    std::vector<int> charuco_ids;
    charuco_detector_->detectBoard(gray, charuco_corners, charuco_ids);

    if (charuco_corners.size() >= min_required_corners_)
    {
        charuco_corners_all_.push_back(charuco_corners);
        charuco_ids_all_.push_back(charuco_ids);
    }
    else
    {
        return false;
    }

    if (return_visualization)
    {
        *return_visualization = image.clone();
        cv::aruco::drawDetectedCornersCharuco(*return_visualization, charuco_corners, charuco_ids);
    }

    return true;
}

void CharucoCalibration::calibrateCamera()
{
    // cv::calibrateCamera()
}