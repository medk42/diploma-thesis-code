#include "detection/stereo_marker_matcher.h"

using namespace aergo::default_modules::scene_detection_stereocam_module;


StereoMarkerMatcher::MatchResult StereoMarkerMatcher::MatchResult::preallocate(size_t max_pairs)
{
    MatchResult result;
    result.matchedMarkers.reserve(max_pairs);
    return result;
}

double reprojError(
    const SE3& T_ref_marker,
    const SceneMarkerDetector::CameraData& camera,
    const std::array<cv::Point2f, 4>& detected_corners,
    const std::array<cv::Vec3f, 4>& marker_points_3d
)
{
    cv::Vec3d rvec, tvec;
    SE3 T_cam_marker = camera.T_cam_ref * T_ref_marker;
    T_cam_marker.toRvecTvec(rvec, tvec);
    
    std::array<cv::Point2f, 4> projected_corners_2;
    cv::projectPoints(
        marker_points_3d,
        rvec,
        tvec,
        camera.K,
        camera.D,
        projected_corners_2
    );
    double total_error = 0.0;
    for (size_t i = 0; i < 4; ++i)
    {
        cv::Point2f diff = projected_corners_2[i] - detected_corners[i];
        total_error += std::sqrt(diff.x * diff.x + diff.y * diff.y);
    }

    return total_error / 4.0; // average error
}


void StereoMarkerMatcher::matchMarkers(
    const SceneMarkerDetector::CameraData& leftCamera_,
    const SceneMarkerDetector::CameraData& rightCamera_,
    const SceneMarkerDetector::DetectionResult& leftDetections,
    const SceneMarkerDetector::DetectionResult& rightDetections,
    MatchResult& outResult
) const
{
    size_t left_count = leftDetections.markers.size();
    size_t right_count = rightDetections.markers.size();
    size_t pairs_count = left_count * right_count;

    outResult.matchedMarkers.clear();
    reprojection_errors_buffer_.resize(pairs_count);
    T_ref_marker_buffer_.resize(pairs_count);

    for (size_t left_id = 0; left_id < left_count; ++left_id)
    {
        for (size_t right_id = 0; right_id < right_count; ++right_id)
        {
            const auto& it = markersData_.find(leftDetections.markers[left_id].id);

            size_t reprojection_id = left_id * right_count + right_id;
            if (leftDetections.markers[left_id].id == rightDetections.markers[right_id].id && it != markersData_.end())
            {
                const std::array<cv::Vec3f, 4>& marker_points_3d = it->second.objectPoints();
                double left_error = reprojError(
                    leftDetections.markers[left_id].T_ref_marker_,
                    rightCamera_,
                    rightDetections.markers[right_id].corners,
                    marker_points_3d
                );
                double right_error = reprojError(
                    rightDetections.markers[right_id].T_ref_marker_,
                    leftCamera_,
                    leftDetections.markers[left_id].corners,
                    marker_points_3d
                );
                if (left_error < right_error)
                {
                    reprojection_errors_buffer_[reprojection_id] = left_error;
                    T_ref_marker_buffer_[reprojection_id] = leftDetections.markers[left_id].T_ref_marker_;
                }
                else
                {
                    reprojection_errors_buffer_[reprojection_id] = right_error;
                    T_ref_marker_buffer_[reprojection_id] = rightDetections.markers[right_id].T_ref_marker_;
                }
            }
            else
            {
                reprojection_errors_buffer_[reprojection_id] = -1.0;     // invalid
                T_ref_marker_buffer_[reprojection_id] = SE3::unit();
            }
        }
    }

    // Simple matching by marker ID
    for (size_t i = 0; i < pairs_count; ++i)
    {
        size_t found_index = pairs_count; // invalid
        for (size_t j = 0; j < pairs_count; ++j)
        {
            if (reprojection_errors_buffer_[j] >= 0.0) // valid
            {
                if (found_index == pairs_count || reprojection_errors_buffer_[j] < reprojection_errors_buffer_[found_index]) {
                    found_index = j;
                }
            }
        }

        if (found_index == pairs_count) {
            break; // no more valid matches
        }

        double reproj_error = reprojection_errors_buffer_[found_index];

        if (reproj_error > max_allowed_reprojection_error_)
        {
            break; // stop if the best match is above the threshold
        }

        size_t left_id = found_index / right_count;
        size_t right_id = found_index % right_count;

        MatchResult::MatchedMarkerPair pair;
        pair.corners_left = leftDetections.markers[left_id].corners;
        pair.corners_right = rightDetections.markers[right_id].corners;
        pair.marker_id = leftDetections.markers[left_id].id;
        pair.marker_points_3d = markersData_.at(pair.marker_id).objectPoints(); // safe to use 'at' since we checked existence before
        pair.T_ref_marker_estimate = T_ref_marker_buffer_[found_index];
        pair.reprojection_error = reproj_error;
        outResult.matchedMarkers.push_back(std::move(pair));

        // Invalidate used left and right markers
        for (size_t j = 0; j < right_count; ++j) {
            reprojection_errors_buffer_[left_id * right_count + j] = -1.0;
        }
        for (size_t j = 0; j < left_count; ++j) {
            reprojection_errors_buffer_[j * right_count + right_id] = -1.0;
        }
    }
}