#ifndef AERGO_MARKER_TRACKER_H
#define AERGO_MARKER_TRACKER_H

#include "pen_calibration_helper.h"

using namespace aergo::pen_calibration::helper;

namespace aergo::pen_tracking
{
    
    class MarkerTracker
    {
    public:

        struct Result
        {
            bool success;
            bool lost_tracking;
            Transformation camera_to_origin;
            std::map<int, Transformation> camera_to_visible_marker;
        };

        /// @param ignore_markers_above_angle use non-positive value to disable (-1 for example)
        MarkerTracker(
            cv::Mat camera_matrix, cv::Mat distortion_coefficients, 
            cv::aruco::ArucoDetector aruco_detector, const std::set<int> used_marker_ids,
            std::vector<cv::Point3f> marker_points,
            std::map<int, Transformation> origin_to_other_transformations, double search_window_perc
        );
        
        Result processImage(cv::Mat image, cv::Mat* return_visualization);

    private:

        std::optional<cv::Rect> getRoi(Transformation last_position, cv::Size2i image_dimensions, double search_window_perc, cv::Mat* return_visualization);

        cv::Mat camera_matrix_;
        cv::Mat distortion_coefficients_;
        cv::aruco::ArucoDetector aruco_detector_;
        const std::set<int> used_marker_ids_;
        std::vector<cv::Point3f> marker_points_;
        std::map<int, Transformation> origin_to_other_transformations_;
        std::optional<Transformation> last_camera_to_origin_;
        double search_window_perc_;
    };

} // namespace aergo::pen_tracking

#endif // AERGO_MARKER_TRACKER_H