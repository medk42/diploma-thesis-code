#pragma once

#include "pose_utils.h"

#include <map>

#include <opencv2/objdetect/aruco_detector.hpp>


namespace aergo::default_modules::scene_detection_stereocam_module
{
    class SceneMarkerDetector
    {
    public:
        enum class RefineMode { SUBPIXEL, CONTOUR };
        struct DetectionResult;

        struct MarkerData;
        struct CameraData
        {
            cv::Mat K;      // Intrinsic matrix
            cv::Mat D;      // Distortion coefficients
            SE3 T_cam_ref;  // cam <- reference frame (pose of reference frame in camera coordinate system)
        };

        SceneMarkerDetector(std::map<int, MarkerData> markersData, cv::aruco::Dictionary dictionary, RefineMode refineMode, cv::SolvePnPMethod pnpMethod = cv::SolvePnPMethod::SOLVEPNP_IPPE_SQUARE);

        /// Detect markers in the given grayscale image.
        /// @param gray Grayscale input image (CV_8UC1).
        void detectMarkers(const CameraData& camera, const cv::Mat& gray, DetectionResult& result) const;
    private:
        cv::aruco::DetectorParameters buildParams(RefineMode refineMode);
        
        cv::aruco::ArucoDetector arucoDetector_;
        std::map<int, MarkerData> markersData_;
        cv::SolvePnPMethod pnpMethod_;

        mutable std::vector<std::vector<cv::Point2f>> found_corners_buffer_;
        mutable std::vector<int> found_ids_buffer_;
    };

    struct SceneMarkerDetector::DetectionResult
    {
        struct DetectedMarker;

        static DetectionResult preallocate(size_t max_markers);

        std::vector<DetectedMarker> markers;                   // Detected markers in the image.
    };


    struct SceneMarkerDetector::DetectionResult::DetectedMarker
    {
        int id;
        std::array<cv::Point2f, 4> corners; // in clockwise order
        SE3 T_ref_marker_; // reference <- marker frame (pose of marker in reference frame)
    };
    
    struct SceneMarkerDetector::MarkerData
    {
    public:
        MarkerData() = default;
        MarkerData(float size_m, const SE3& T_marker_pose)
            : size_m_(size_m), T_marker_pose_(T_marker_pose)
        {
            objectPointsCache_[0] = cv::Vec3f(-size_m_/2.0f,  size_m_/2.0f, 0);
            objectPointsCache_[1] = cv::Vec3f( size_m_/2.0f,  size_m_/2.0f, 0);
            objectPointsCache_[2] = cv::Vec3f( size_m_/2.0f, -size_m_/2.0f, 0);
            objectPointsCache_[3] = cv::Vec3f(-size_m_/2.0f, -size_m_/2.0f, 0);   
        }

        float size_m() const { return size_m_; }
        const SE3& T_marker_pose() const { return T_marker_pose_; } // marker <- pose frame
        const std::array<cv::Vec3f, 4>& objectPoints() const { return objectPointsCache_; }

    private:
        float size_m_;
        SE3 T_marker_pose_; // marker <- pose frame
        std::array<cv::Vec3f, 4> objectPointsCache_;
    };
}