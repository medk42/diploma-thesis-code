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
        if (!image_size_)
        {
            image_size_ = cv::Size(image.cols, image.rows);
        }
        else if (image_size_->width != image.cols || image_size_->height != image.rows)
        {
            return false;
        }

        std::vector<cv::Point3f> object_points;
        std::vector<cv::Point2f> image_points;
        charuco_detector_->getBoard().matchImagePoints(charuco_corners, charuco_ids, object_points, image_points);

        object_points_all_.push_back(object_points);
        image_points_all_.push_back(image_points);
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

CalibrationResult CharucoCalibration::calibrateCamera()
{
    CalibrationResult result {.success = false }; 

    if (!image_size_)
    {
        return result;
    }

    result.rms_error = cv::calibrateCamera(
        object_points_all_, 
        image_points_all_, 
        *image_size_, 
        result.camera_matrix, 
        result.distortion_coefficients, 
        result.rvecs, 
        result.tvecs
    );
    result.success = true;

    return result;
}