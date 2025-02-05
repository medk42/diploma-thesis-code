#ifndef AERGO_CAMERA_CALIBRATION_H
#define AERGO_CAMERA_CALIBRATION_H

#include <memory>
#include <vector>

#include <opencv2/objdetect/charuco_detector.hpp>

namespace aergo::camera_calibration
{
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
        /// @return true on success, false on failure (image empty or not enough charuco corners found)
        bool addImage(cv::Mat image, cv::Mat* return_visualization = nullptr);

        /// @brief Calibrated camera using the images provided so far.
        void calibrateCamera();

    private:
        std::unique_ptr<cv::aruco::CharucoDetector> charuco_detector_;
        std::vector<std::vector<cv::Point2f>> charuco_corners_all_;
        std::vector<std::vector<int>> charuco_ids_all_;

        int min_required_corners_;
    };
}

#endif // AERGO_CAMERA_CALIBRATION_H