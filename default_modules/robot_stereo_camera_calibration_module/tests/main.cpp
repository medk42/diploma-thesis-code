#include "calib/pose_utils.h"
#include "calib/types.h"
#include "calib/charuco_defaults.h"

#include "calib/charuco_board_model.h"
#include "calib/charuco_detector.h"
#include "calib/intrinsics_calibrator.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>

using namespace aergo::default_modules::robot_stereo_camera_calibration_module;
using namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib;

namespace
{
    CharucoBoardModel make_demo_board()
    {
        CharucoBoardModel::Params bp{
            defaults::charucoboard::ROW_COUNT,
            defaults::charucoboard::COL_COUNT,
            defaults::charucoboard::SQUARE_LENGTH,
            defaults::charucoboard::MARKER_LENGTH,
            cv::aruco::DICT_4X4_100};

        return CharucoBoardModel::Create(bp);
    }

    cv::Mat make_demo_image(const CharucoBoardModel& board,
                             const cv::Size& size,
                             const std::filesystem::path& path)
    {
        cv::Mat img;
        board.board()->generateImage(size, img, 20, 1);

        if (!path.empty())
        {
            const auto absPath = std::filesystem::absolute(path);
            if (cv::imwrite(absPath.string(), img))
            {
                std::cout << "[demo_detect] Saved demo image to " << absPath.string()
                          << " (" << img.cols << "x" << img.rows << ")" << std::endl;
            }
            else
            {
                std::cout << "[demo_detect] Failed to save demo image to " << absPath.string() << std::endl;
            }
        }

        return img;
    }

    CameraIntrinsics make_demo_intrinsics(const cv::Size& imageSize)
    {
        CameraIntrinsics K;

        const double fx = 900.0;
        const double fy = 900.0;
        const double cx = static_cast<double>(imageSize.width) / 2.0;
        const double cy = static_cast<double>(imageSize.height) / 2.0;

        K.K = (cv::Mat_<double>(3, 3) << fx, 0.0, cx,
                                         0.0, fy, cy,
                                         0.0, 0.0, 1.0);
        K.D = cv::Mat::zeros(1, 5, CV_64F);
        K.imageSize = imageSize;

        return K;
    }

    CharucoDetection make_synthetic_detection(const CharucoBoardModel& board,
                                              const cv::Mat& K,
                                              const cv::Mat& D,
                                              const cv::Vec3d& rvec,
                                              const cv::Vec3d& tvec,
                                              const cv::Size& imageSize)
    {
        CharucoDetection det;
        const auto corners3d = board.board()->getChessboardCorners();

        det.ids.reserve(corners3d.size());
        for (int i = 0; i < static_cast<int>(corners3d.size()); ++i)
        {
            det.ids.push_back(i);
        }

        cv::projectPoints(corners3d, rvec, tvec, K, D, det.corners2d);
        det.imageSize = imageSize;
        det.ok = true;
        return det;
    }
}

void demo_detect(const CharucoBoardModel& board, const cv::Mat& img, const CameraIntrinsics& KL)
{
    std::cout << "[demo_detect] Starting detection on image " << img.cols << "x" << img.rows << std::endl;

    CharucoDetector::Params dp;
    CharucoDetector det(board, dp);

    CharucoDetection d = det.detect(img);

    std::cout << "[demo_detect] ok=" << d.ok
              << ", aruco markers=" << d.markerIds.size()
              << ", charuco corners=" << d.ids.size()
              << ", rejected=" << d.rejectedCandidates.size()
              << std::endl;

    if (!d.markerIds.empty())
    {
        const size_t n = std::min<size_t>(d.markerIds.size(), 4);
        std::cout << "[demo_detect] First marker ids: ";
        for (size_t i = 0; i < n; ++i)
        {
            std::cout << d.markerIds[i] << (i + 1 == n ? "" : ", ");
        }
        std::cout << std::endl;
    }

    cv::Vec3d rvec, tvec;
    const bool pose_ok = det.estimateBoardPose(d, KL, rvec, tvec);
    if (pose_ok)
    {
        std::cout << "[demo_detect] Pose estimated (T_cam<-board): rvec=["
                  << rvec[0] << ", " << rvec[1] << ", " << rvec[2] << "]"
                  << ", tvec=[" << tvec[0] << ", " << tvec[1] << ", " << tvec[2] << "]"
                  << std::endl;
    }
    else
    {
        std::cout << "[demo_detect] Pose estimation skipped/failed (need enough corners + intrinsics)." << std::endl;
    }
}

void demo_intrinsics_calibration(const CharucoBoardModel& board, const CameraIntrinsics& K_true)
{
    std::cout << "[demo_intrinsics] Generating synthetic detections for calibration demo..." << std::endl;

    std::vector<CharucoDetection> detections;
    detections.reserve(12);

    // Create several synthetic views with varying poses
    for (int i = 0; i < 12; ++i)
    {
        const double ang = 0.05 * i;
        cv::Vec3d rvec(ang, ang * 0.5, ang * 0.2);
        cv::Vec3d tvec(0.05 * i, 0.01 * i, 0.8 + 0.02 * i);
        detections.push_back(make_synthetic_detection(board, K_true.K, K_true.D, rvec, tvec, K_true.imageSize));
    }

    IntrinsicsCalibrator calib;
    auto result = calib.calibrate(detections, board, K_true.imageSize);

    std::cout << "[demo_intrinsics] ok=" << (result.ok ? "true" : "false") << "\n";
    if (!result.ok)
    {
        std::cout << "[demo_intrinsics] reason: " << result.message << "\n";
        return;
    }

    std::cout << "[demo_intrinsics] RMS: " << result.rms << "\n";
    std::cout << "[demo_intrinsics] Used views: " << result.usedViewIndices.size() << "\n";
    std::cout << "[demo_intrinsics] K:\n" << result.intr.K << "\n";
    std::cout << "[demo_intrinsics] D:\n" << result.intr.D << "\n";

    if (!result.perViewRms.empty())
    {
        std::cout << "[demo_intrinsics] Per-view RMS:\n";
        for (size_t i = 0; i < result.perViewRms.size(); ++i)
        {
            std::cout << "  view " << i << ": " << result.perViewRms[i] << "\n";
        }
    }
}


int main()
{
    Pose base_from_flange{
        {0.1, 0.2, 0.3},
        {0.0, 0.0, 0.0, 1.0}
    };

    const SE3 T = pose_utils::toSE3(base_from_flange);
    const SE3 T_inv = pose_utils::invert(T);
    const SE3 identity = pose_utils::compose(T, T_inv);
    const Pose round_trip = pose_utils::toPose(T);

    std::cout << "Round-trip position: (" << round_trip.position.x << ", "
              << round_trip.position.y << ", " << round_trip.position.z << ")\n";
    std::cout << "Identity R(0,0): " << identity.R(0, 0) << "\n";

    std::cout << "Charuco defaults: rows=" << defaults::charucoboard::ROW_COUNT
              << ", cols=" << defaults::charucoboard::COL_COUNT
              << ", square=" << defaults::charucoboard::SQUARE_LENGTH
              << ", marker=" << defaults::charucoboard::MARKER_LENGTH << "\n";

    const auto board = make_demo_board();
    const cv::Size demoSize{1280, 720};
    const auto imagePath = std::filesystem::current_path() / "demo_charuco.png";
    const cv::Mat demoImg = make_demo_image(board, demoSize, imagePath);
    const CameraIntrinsics K = make_demo_intrinsics(demoImg.size());

    demo_detect(board, demoImg, K);
    demo_intrinsics_calibration(board, K);

    return 0;
}
