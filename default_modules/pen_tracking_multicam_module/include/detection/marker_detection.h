#pragma once

#include <vector>
#include <array>
#include <optional>
#include <span>

#include <opencv2/objdetect/aruco_detector.hpp>

namespace aergo::default_modules::pen_tracking_multicam_module
{
    class MarkerDetector
    {
    public:
        enum class RefineMode { SUBPIXEL, CONTOUR };
        struct DetectionResult;

        /// @param windowSizeMultiple Marker detection on the full image is slow, therefore we may use the last detected position
        /// to initialize a search window center. This parameter indicates the size of that window as a multiple of the bounding
        /// box of all last detected markers. The multiplier is on area, not side. Value of 0 means this feature is disabled.
        /// This is less predictable (for performance) than using a percentage of the image size, but allows scaling with different
        /// marker sizes and distances.
        MarkerDetector(std::span<const int> markerIdsToDetect, cv::aruco::Dictionary dictionary, RefineMode refineMode, int windowSizeMultiple);

        /// Detect markers in the given grayscale image.
        /// @param gray Grayscale input image (CV_8UC1).
        void detectMarkers(const cv::Mat& gray, DetectionResult& result) const;

    private:
        cv::aruco::DetectorParameters buildParams(RefineMode refineMode);
        void updateLastDetectedMarkerCenter(const DetectionResult& result) const;
        bool detectMarkersInteral(const cv::Mat& gray, DetectionResult& result, int offsetX, int offsetY) const;

        cv::aruco::ArucoDetector arucoDetector_;
        std::vector<int> markerIdsToDetect_;
        int windowSizeMultiple_ = 0;

        mutable std::optional<cv::Point2f> lastDetectedMarkerCenter_{std::nullopt};
        mutable std::optional<cv::Rect2f> lastBoundingBox_{std::nullopt};
        mutable std::vector<std::vector<cv::Point2f>> found_corners_buffer_;
        mutable std::vector<int> found_ids_buffer_;
    };
    

    struct MarkerDetector::DetectionResult
    {
        struct DetectedMarker;

        static DetectionResult preallocate(size_t max_markers);

        std::vector<DetectedMarker> markers;                   // Detected markers in the image.
        bool full_image_searched;                              // Whether the full image was searched (true) or a search window was used (false).
        std::optional<cv::Point2f> detected_markers_center;    // New center of all detected markers, if any for this frame.
        std::optional<cv::Rect2i> used_search_roi;             // The ROI used for the detection attempt, if any.
    };


    struct MarkerDetector::DetectionResult::DetectedMarker
    {
        int id;
        std::array<cv::Point2f, 4> corners; // in clockwise order
    };
}