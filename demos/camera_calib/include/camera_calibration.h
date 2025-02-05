#ifndef AERGO_CAMERA_CALIBRATION_H
#define AERGO_CAMERA_CALIBRATION_H

#include <memory>
#include <vector>
#include <optional>

#include <opencv2/objdetect/charuco_detector.hpp>

namespace aergo::camera_calibration
{
    struct CalibrationResult
    {
        bool success;

        cv::Mat camera_matrix;
        cv::Mat distortion_coefficients;

        std::vector<cv::Mat> rvecs;
        std::vector<cv::Mat> tvecs;

        double rms_error;
    };

    class CharucoCalibration
    {
    public:
        /// @param charuco_board set up charuco board that will be detected
        /// @param corner_refine_method one of cv::aruco::CornerRefineMethod
        /// @param min_required_corners minimum required charuco corners found on image to add it for camera calibration
        CharucoCalibration(cv::aruco::CharucoBoard charuco_board, int corner_refine_method, int min_required_corners=20);
        virtual ~CharucoCalibration() = default;
    
        /// @param image image to add to calibration
        /// @param return_visualization draw the visualization of the result onto an image, only drawn if calibration succeeded
        /// @return true on success, false on failure (image empty, not enough charuco corners found or size does not match)
        bool addImage(cv::Mat image, cv::Mat* return_visualization = nullptr);

        /// @brief Calibrated camera using the images provided so far.
        CalibrationResult calibrateCamera();

    private:
        std::unique_ptr<cv::aruco::CharucoDetector> charuco_detector_;
        std::vector<std::vector<cv::Point3f>> object_points_all_;
        std::vector<std::vector<cv::Point2f>> image_points_all_;
        std::optional<cv::Size> image_size_;

        int min_required_corners_;
    };
}

#endif // AERGO_CAMERA_CALIBRATION_H