#include "detection/marker_detection.h"

#include <stdexcept>

using namespace aergo::default_modules::pen_tracking_multicam_module;


MarkerDetector::DetectionResult MarkerDetector::DetectionResult::preallocate(size_t max_markers)
{
    DetectionResult result;
    result.markers.reserve(max_markers);
    return result;
}


MarkerDetector::MarkerDetector(std::span<const int> markerIdsToDetect,
                                 cv::aruco::Dictionary dictionary,
                                 RefineMode refineMode,
                                 int windowSizeMultiple)
: arucoDetector_(dictionary, buildParams(refineMode)),
  markerIdsToDetect_(markerIdsToDetect.begin(), markerIdsToDetect.end()),
  windowSizeMultiple_(windowSizeMultiple)
{
    if (markerIdsToDetect_.empty())
    {
        throw std::invalid_argument("markerIdsToDetect cannot be empty");
    }

    if (windowSizeMultiple_ < 0)
    {
        throw std::invalid_argument("windowSizeMultiple must be non-negative");
    }
}


cv::aruco::DetectorParameters MarkerDetector::buildParams(RefineMode refineMode)
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


void calculateSearchImage(
    const cv::Mat& image,
    const cv::Point2f lastCenter,
    const cv::Rect2f lastBoundingBox,
    int windowSizeMultiple,
    cv::Mat& outSearchImage,
    cv::Rect2i& outRoi)
{
    if (windowSizeMultiple <= 0)
    {
        outSearchImage = image;
        outRoi = cv::Rect2i(0, 0, image.cols, image.rows);
        return;
    }

    const int imgW = image.cols;
    const int imgH = image.rows;

    const float targetArea = lastBoundingBox.area() * windowSizeMultiple;

    int side = static_cast<int>(std::round(std::sqrt(targetArea)));

    // Clamp side to image dimensions
    side = std::clamp(side, 1, std::min(imgW, imgH));

    // --- Centered placement ---
    int x = static_cast<int>(std::round(lastCenter.x - side * 0.5));
    int y = static_cast<int>(std::round(lastCenter.y - side * 0.5));

    // --- Keep ROI fully inside image ---
    x = std::clamp(x, 0, imgW - side);
    y = std::clamp(y, 0, imgH - side);

    cv::Rect2i roi(x, y, side, side);

    outSearchImage = image(roi);
    outRoi = roi;
}


void MarkerDetector::detectMarkers(const cv::Mat& gray, DetectionResult& result) const
{
    if (gray.channels() != 1 || gray.type() != CV_8UC1)
    {
        throw std::invalid_argument("Input image must be grayscale CV_8UC1");
    }

    if (windowSizeMultiple_ > 0 && lastDetectedMarkerCenter_.has_value() && lastBoundingBox_.has_value())
    {
        cv::Mat search_image;
        cv::Rect2i roi;
        calculateSearchImage(gray, lastDetectedMarkerCenter_.value(), lastBoundingBox_.value(), windowSizeMultiple_, search_image, roi);
        result.used_search_roi = roi;
        if (!detectMarkersInteral(search_image, result, roi.x, roi.y))
        {
            // try full image if not found in search window
            detectMarkersInteral(gray, result, 0, 0);
            result.full_image_searched = true;
        }
        else
        {
            result.full_image_searched = false;
        }
    }
    else
    {
        detectMarkersInteral(gray, result, 0, 0);
        result.full_image_searched = true;
        result.used_search_roi = std::nullopt;
    }

    updateLastDetectedMarkerCenter(result);
    result.detected_markers_center = lastDetectedMarkerCenter_;
}


bool MarkerDetector::detectMarkersInteral(const cv::Mat& gray, DetectionResult& result, int offsetX, int offsetY) const
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
        result.markers.clear();
        return false;
    }

    for (size_t i = 0; i < found_ids_buffer_.size(); ++i)
    {
        int id = found_ids_buffer_[i];
        if (std::find(markerIdsToDetect_.begin(), markerIdsToDetect_.end(), id) != markerIdsToDetect_.end())
        {
            const auto& corners_vec = found_corners_buffer_[i];
            DetectionResult::DetectedMarker marker;
            marker.id = id;
            for (size_t c = 0; c < 4; ++c)
            {
                marker.corners[c] = corners_vec[c];
                marker.corners[c].x += offsetX;
                marker.corners[c].y += offsetY;
            }
            result.markers.push_back(std::move(marker));
        }
    }

    return !result.markers.empty();
}


void MarkerDetector::updateLastDetectedMarkerCenter(const DetectionResult& result) const
{
    if (result.markers.empty())
    {
        lastDetectedMarkerCenter_ = std::nullopt;
        lastBoundingBox_ = std::nullopt;
        return;
    }

    cv::Point2f minPt(1e6, 1e6);
    cv::Point2f maxPt(-1e6, -1e6);

    for (const auto& marker : result.markers)
    {
        for (const auto& corner : marker.corners)
        {
            minPt.x = std::min(minPt.x, corner.x);
            minPt.y = std::min(minPt.y, corner.y);
            maxPt.x = std::max(maxPt.x, corner.x);
            maxPt.y = std::max(maxPt.y, corner.y);
        }
    }
    lastDetectedMarkerCenter_ = (minPt + maxPt) * 0.5f;
    lastBoundingBox_ = cv::Rect2f(minPt, maxPt);
}