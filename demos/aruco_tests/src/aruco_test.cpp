#include <opencv2/opencv.hpp>
#include <opencv2/aruco.hpp>
#include <iostream>
#include <algorithm>

// --- CONFIG PARAMETERS ---
// Camera index (change if needed).
static constexpr int   kCameraIndex            = 2;
// Fraction of full frame height used by the zoomed marker view (0.0–1.0).
static constexpr float kOverlayHeightFraction  = 0.5f;
// Which dictionary to use.
static constexpr cv::aruco::PredefinedDictionaryType kDictionaryId = cv::aruco::DICT_4X4_100;
// Where to place the zoomed marker: "TR" = top-right, "TL" = top-left,
// "BR" = bottom-right, "BL" = bottom-left.
static const std::string kOverlayCorner = "TR";
// Base square size (before resizing to overlay height).
static constexpr int kCanonicalMarkerSize = 256;

void drawMarkerOutline(cv::Mat &img,
                       const std::vector<cv::Point2f> &corners,
                       const cv::Scalar &color,
                       int thickness = 2)
{
    for (int j = 0; j < 4; ++j) {
        cv::Point2f p1 = corners[j];
        cv::Point2f p2 = corners[(j + 1) % 4];
        cv::line(img, p1, p2, color, thickness, cv::LINE_AA);
    }
}

int main()
{
    cv::VideoCapture cap(kCameraIndex);
    if (!cap.isOpened()) {
        std::cerr << "ERROR: Could not open camera index " << kCameraIndex << "\n";
        return 1;
    }

    cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M','J','P','G'));
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 2560);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 800);
    cap.set(cv::CAP_PROP_FPS, 60);
    cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 1); // enable manual exposure
    double exposure_us = 500; // 500 microseconds = 1/2000 sec
    cap.set(cv::CAP_PROP_EXPOSURE, exposure_us);

    int fourcc = cap.get(cv::CAP_PROP_FOURCC);
    char fcc[5] = {
        (char)(fourcc & 0xFF),
        (char)((fourcc >> 8) & 0xFF),
        (char)((fourcc >> 16) & 0xFF),
        (char)((fourcc >> 24) & 0xFF),
        0
    };
    std::cout << "FOURCC: " << fcc << "\n";
    std::cout << cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
            << cap.get(cv::CAP_PROP_FRAME_HEIGHT) << " @ "
            << cap.get(cv::CAP_PROP_FPS) << " FPS\n";
    std::cout << "Exposure: " << cap.get(cv::CAP_PROP_EXPOSURE) << " us, auto ="
            << cap.get(cv::CAP_PROP_AUTO_EXPOSURE) << "\n";

    auto dictionary = cv::aruco::getPredefinedDictionary(kDictionaryId);
    auto detectorParams = cv::aruco::DetectorParameters();
    detectorParams.minMarkerPerimeterRate = 0.005; // smaller markers
    detectorParams.minSideLengthCanonicalImg = 8; // smaller markers
    detectorParams.minCornerDistanceRate = 0.02; // allow close markers
    detectorParams.minDistanceToBorder = 1; // allow markers at border
    detectorParams.perspectiveRemovePixelPerCell = 6; // higher res for small markers
    cv::aruco::ArucoDetector detector(dictionary, detectorParams);

    cv::Mat frame;
    std::cout << "Press 'q' or ESC to quit.\n";

    while (true) {
        if (!cap.read(frame) || frame.empty()) {
            std::cerr << "WARNING: Empty frame from camera.\n";
            break;
        }

        // Detect markers.
        std::vector<std::vector<cv::Point2f>> corners, rejected;
        std::vector<int> ids;

        detector.detectMarkers(frame, corners, ids, rejected);

        int minId = -1;
        int minIdIndex = -1;

        if (!ids.empty()) {
            // Find smallest ID and its index.
            auto it = std::min_element(ids.begin(), ids.end());
            minId = *it;
            minIdIndex = static_cast<int>(std::distance(ids.begin(), it));
        }

        // Draw all markers. Smallest ID in green, others in red.
        for (size_t i = 0; i < ids.size(); ++i) {
            cv::Scalar color = (static_cast<int>(i) == minIdIndex)
                                   ? cv::Scalar(0, 255, 0)      // green
                                   : cv::Scalar(0, 0, 255);     // red

            drawMarkerOutline(frame, corners[i], color, 2);

            // Draw ID text near the first corner.
            cv::Point2f textPos = corners[i][0];
            textPos.y -= 5; // shift up a bit
            cv::putText(frame,
                        std::to_string(ids[i]),
                        textPos,
                        cv::FONT_HERSHEY_SIMPLEX,
                        0.6,
                        color,
                        2,
                        cv::LINE_AA);
        }

        // If we have at least one marker, visualize the smallest ID marker in a corner.
        if (minIdIndex >= 0) {
            const auto &markerCorners = corners[minIdIndex];

            // Perspective warp to canonical square view (this makes it "right way up"
            // according to how OpenCV defines the corner order).
            std::vector<cv::Point2f> src(4), dst(4);
            for (int i = 0; i < 4; ++i) src[i] = markerCorners[i];

            dst[0] = cv::Point2f(0.0f, 0.0f);
            dst[1] = cv::Point2f(static_cast<float>(kCanonicalMarkerSize - 1), 0.0f);
            dst[2] = cv::Point2f(static_cast<float>(kCanonicalMarkerSize - 1),
                                 static_cast<float>(kCanonicalMarkerSize - 1));
            dst[3] = cv::Point2f(0.0f, static_cast<float>(kCanonicalMarkerSize - 1));

            cv::Mat H = cv::getPerspectiveTransform(src, dst);
            cv::Mat markerCanon;
            cv::warpPerspective(frame, markerCanon, H,
                                cv::Size(kCanonicalMarkerSize, kCanonicalMarkerSize),
                                cv::INTER_LINEAR,
                                cv::BORDER_REPLICATE);

            // Optionally convert to grayscale, then back to BGR for a "how OpenCV sees bits" look.
            cv::Mat markerGray;
            cv::cvtColor(markerCanon, markerGray, cv::COLOR_BGR2GRAY);
            cv::cvtColor(markerGray, markerCanon, cv::COLOR_GRAY2BGR);

            // Compute overlay size based on current frame height.
            int overlayH = static_cast<int>(frame.rows * kOverlayHeightFraction);
            overlayH = std::max(16, std::min(overlayH, frame.rows)); // clamp reasonably
            int overlayW = overlayH; // keep it square

            cv::Mat markerOverlay;
            cv::resize(markerCanon, markerOverlay, cv::Size(overlayW, overlayH),
                       0, 0, cv::INTER_NEAREST); // NEAREST keeps the pixel structure sharp

            // Decide where to place it.
            int x = 0, y = 0; // default TL
            if (kOverlayCorner == "TR") {
                x = frame.cols - overlayW;
                y = 0;
            } else if (kOverlayCorner == "BR") {
                x = frame.cols - overlayW;
                y = frame.rows - overlayH;
            } else if (kOverlayCorner == "BL") {
                x = 0;
                y = frame.rows - overlayH;
            } // "TL" already covered by defaults

            // Clamp ROI to frame size just in case.
            x = std::max(0, std::min(x, frame.cols - overlayW));
            y = std::max(0, std::min(y, frame.rows - overlayH));

            cv::Rect roi(x, y, overlayW, overlayH);
            if (0 <= roi.x && 0 <= roi.y &&
                roi.x + roi.width  <= frame.cols &&
                roi.y + roi.height <= frame.rows)
            {
                // Draw a border under it so it stands out.
                cv::rectangle(frame, roi, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
                markerOverlay.copyTo(frame(roi));
            }
        }

        cv::imshow("ArUco Demo", frame);
        char key = static_cast<char>(cv::waitKey(1));
        if (key == 27 || key == 'q' || key == 'Q') {
            break;
        }
    }

    return 0;
}
