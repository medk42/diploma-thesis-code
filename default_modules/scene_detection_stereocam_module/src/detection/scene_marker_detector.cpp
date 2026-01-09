#include "detection/scene_marker_detector.h"

using namespace aergo::default_modules::scene_detection_stereocam_module;


SceneMarkerDetector::DetectionResult SceneMarkerDetector::DetectionResult::preallocate(size_t max_markers)
{
    DetectionResult result;
    result.markers.reserve(max_markers);
    return result;
}


SceneMarkerDetector::SceneMarkerDetector(
    std::map<int, MarkerData> markersData,
    cv::aruco::Dictionary dictionary,
    RefineMode refineMode,
    cv::SolvePnPMethod pnpMethod
) : 
  markersData_(std::move(markersData)),
  arucoDetector_(dictionary, buildParams(refineMode)),
  pnpMethod_(pnpMethod)
{}


cv::aruco::DetectorParameters SceneMarkerDetector::buildParams(RefineMode refineMode)
{
    cv::aruco::DetectorParameters params;

    switch (refineMode)
    {
        case RefineMode::SUBPIXEL:
            params.cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
            break;
        case RefineMode::CONTOUR:
            params.cornerRefinementMethod = cv::aruco::CORNER_REFINE_CONTOUR;
            break;
        default:
            params.cornerRefinementMethod = cv::aruco::CORNER_REFINE_NONE;
            break;
    }

    return params;
}


void SceneMarkerDetector::detectMarkers(const CameraData& camera, const cv::Mat& gray, DetectionResult& result) const
{
    found_corners_buffer_.clear();
    found_ids_buffer_.clear();

    result.markers.clear();

    try
    {
        arucoDetector_.detectMarkers(gray, found_corners_buffer_, found_ids_buffer_);
    }
    catch (const cv::Exception& e)
    {
        return;
    }

    SE3 T_ref_cam = camera.T_cam_ref.inverse();

    for (size_t i = 0; i < found_ids_buffer_.size(); ++i)
    {
        int id = found_ids_buffer_[i];
        const auto& it = markersData_.find(id);
        if (it != markersData_.end())
        {
            const auto& corners_vec = found_corners_buffer_[i];
            DetectionResult::DetectedMarker marker;
            marker.id = id;
            for (size_t c = 0; c < 4; ++c)
            {
                marker.corners[c] = corners_vec[c];
            }

            cv::Vec3d rvec, tvec;
            if (!cv::solvePnP(it->second.objectPoints(), marker.corners, camera.K, camera.D, rvec, tvec, false, pnpMethod_))
            {
                continue;
            }
            SE3 T_cam_marker = SE3::fromRvecTvec(rvec, tvec);
            marker.T_ref_marker_ = T_ref_cam * T_cam_marker;

            result.markers.push_back(std::move(marker));
        }
    }
}