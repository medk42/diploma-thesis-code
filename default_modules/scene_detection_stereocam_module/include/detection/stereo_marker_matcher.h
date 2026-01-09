#pragma once

#include "scene_marker_detector.h"
#include "pose_utils.h"

#include <opencv2/core.hpp>

#include <utility>
#include <map>

namespace aergo::default_modules::scene_detection_stereocam_module
{
    class StereoMarkerMatcher
    {
    public:
        struct MatchResult;

        StereoMarkerMatcher(std::map<int, SceneMarkerDetector::MarkerData> markersData, double max_allowed_reprojection_error = 10.0)
            : markersData_(std::move(markersData)),
              max_allowed_reprojection_error_(max_allowed_reprojection_error)
        {}

        /// Match detected markers between left and right images.
        /// @param leftDetections Detections from the left image.
        /// @param rightDetections Detections from the right image.
        /// @return Vector of matched marker pairs.
        void matchMarkers(
            const SceneMarkerDetector::CameraData& leftCamera_,
            const SceneMarkerDetector::CameraData& rightCamera_,
            const SceneMarkerDetector::DetectionResult& leftDetections,
            const SceneMarkerDetector::DetectionResult& rightDetections,
            MatchResult& outResult
        ) const;
            
    private:
        /// @brief Calculate pose from left and right marker corners and compute reprojection error.
        /// Return the lower reprojection error and corresponding pose. 
        /// @return true if pose was successfully estimated, out parameters only valid if true is returned.
        bool calculatePoseReprojectionError(
            const std::array<cv::Vec3f, 4>& marker_points_3d,
            const std::array<cv::Point2f, 4>& corners_left,
            const std::array<cv::Point2f, 4>& corners_right,
            double& out_reprojection_error,
            SE3& out_T_ref_marker
        ) const;

        std::map<int, SceneMarkerDetector::MarkerData> markersData_;
        double max_allowed_reprojection_error_;

        mutable std::vector<double> reprojection_errors_buffer_;
        mutable std::vector<SE3> T_ref_marker_buffer_;
    };
    
    struct StereoMarkerMatcher::MatchResult
    {
        struct MatchedMarkerPair;

        static MatchResult preallocate(size_t max_pairs);

        std::vector<MatchedMarkerPair> matchedMarkers; // Matched marker pairs between left and right images.
    };

    struct StereoMarkerMatcher::MatchResult::MatchedMarkerPair
    {
        int marker_id;                             // ID of the matched marker
        std::array<cv::Vec3f, 4> marker_points_3d; // 3D points of the marker in marker coordinate system
        std::array<cv::Point2f, 4> corners_left;   // corners in left image
        std::array<cv::Point2f, 4> corners_right;  // corners in right image
        SE3 T_ref_marker_estimate;                 // reference <- marker pose estimate (marker in reference coordinate system)
        double reprojection_error;                 // average reprojection error in pixels
    };
}