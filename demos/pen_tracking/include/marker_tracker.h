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
            Transformation camera_to_origin;
            std::map<int, Transformation> camera_to_visible_marker;
        };

        MarkerTracker(
            cv::Mat camera_matrix, cv::Mat distortion_coefficients, 
            cv::aruco::ArucoDetector aruco_detector, const std::set<int> used_marker_ids,
            std::vector<cv::Point3f> marker_points, double ignore_markers_above_angle,
            std::map<int, Transformation> origin_to_other_transformations
        );
        
        Result processImage(cv::Mat image, cv::Mat* return_visualization);

    private:

        cv::Mat camera_matrix_;
        cv::Mat distortion_coefficients_;
        cv::aruco::ArucoDetector aruco_detector_;
        const std::set<int> used_marker_ids_;
        double ignore_markers_above_angle_;
        std::vector<cv::Point3f> marker_points_;
        std::map<int, Transformation> origin_to_other_transformations_;
    };

} // namespace aergo::pen_tracking

#endif // AERGO_MARKER_TRACKER_H