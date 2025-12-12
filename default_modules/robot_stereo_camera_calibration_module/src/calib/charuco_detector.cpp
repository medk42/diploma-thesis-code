#include "calib/charuco_detector.h"

#include <opencv2/imgproc.hpp>

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    CharucoDetector::CharucoDetector(const CharucoBoardModel& model, const Params& p)
        : model_(model),
          prm_(p),
          arucoParams_(cv::aruco::DetectorParameters())
    {
        arucoParams_.adaptiveThreshWinSizeMin = prm_.adaptiveWinMin;
        arucoParams_.adaptiveThreshWinSizeMax = prm_.adaptiveWinMax;
        arucoParams_.adaptiveThreshWinSizeStep = prm_.adaptiveWinStep;
        arucoParams_.minMarkerPerimeterRate = prm_.minMarkerPerimeterRate;
        arucoDet_ = cv::aruco::ArucoDetector(model_.dictionary(), arucoParams_);
    }

    CharucoDetector::Result CharucoDetector::detect(const cv::Mat& img) const
    {
        CharucoDetector::Result det;

        if (img.empty())
        {
            return det;
        }

        cv::Mat gray;
        if (img.channels() == 1)
        {
            gray = img;
        }
        else
        {
            cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        }

        det.imageSize = gray.size();

        arucoDet_.detectMarkers(gray, det.markerCorners, det.markerIds, det.rejectedCandidates);

        if (static_cast<int>(det.markerIds.size()) < prm_.minArucoMarkers)
        {
            det.ok = false;
            return det;
        }

        std::vector<cv::Point2f> charucoCorners;
        std::vector<int> charucoIds;
        cv::aruco::interpolateCornersCharuco(
            det.markerCorners,
            det.markerIds,
            gray,
            model_.board(),
            charucoCorners,
            charucoIds);

        if (prm_.refineSubpix && !charucoCorners.empty())
        {
            cv::TermCriteria crit(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, prm_.subpixMaxIters, prm_.subpixEps);
            cv::cornerSubPix(gray, charucoCorners, prm_.subpixWin, cv::Size(-1, -1), crit);
        }

        det.ids = std::move(charucoIds);
        det.corners2d = std::move(charucoCorners);
        det.ok = static_cast<int>(det.ids.size()) >= prm_.minCharucoCorners;
        return det;
    }

    bool CharucoDetector::estimateBoardPose(const CharucoDetector::Result& det,
                                            const CameraIntrinsics& K,
                                            cv::Vec3d& rvec, cv::Vec3d& tvec) const
    {
        if (!det.ok || det.ids.empty() || det.corners2d.empty())
        {
            return false;
        }

        if (K.K.empty())
        {
            return false;
        }

        cv::Mat distCoeffs = K.D.empty() ? cv::Mat() : K.D;

        const bool success = cv::aruco::estimatePoseCharucoBoard(
            det.corners2d,
            det.ids,
            model_.board(),
            K.K,
            distCoeffs,
            rvec,
            tvec);

        return success;
    }
}
