#include "calib/intrinsics_calibrator.h"

#include <opencv2/aruco/charuco.hpp>
#include <opencv2/calib3d.hpp>
#include <numeric>

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    IntrinsicsCalibrator::IntrinsicsCalibrator(const Params& p)
        : prm_(p)
    {
    }

    void IntrinsicsCalibrator::buildCharucoArrays(const std::vector<CharucoDetection>& views,
                                                  const CharucoBoardModel& /*board*/,
                                                  std::vector<std::vector<cv::Point2f>>& allCorners,
                                                  std::vector<std::vector<int>>& allIds,
                                                  std::vector<int>& usedIndices) const
    {
        for (size_t i = 0; i < views.size(); ++i)
        {
            const auto& v = views[i];
            if (!v.ok || static_cast<int>(v.ids.size()) < prm_.minCharucoCornersPerView)
            {
                continue;
            }

            allCorners.push_back(v.corners2d);
            allIds.push_back(v.ids);
            usedIndices.push_back(static_cast<int>(i));
        }
    }

    std::vector<double> IntrinsicsCalibrator::computePerViewRmse(
        const std::vector<std::vector<cv::Point2f>>& allCorners,
        const std::vector<std::vector<int>>& allIds,
        const CharucoBoardModel& board,
        const CameraIntrinsics& intr,
        const std::vector<cv::Vec3d>& rvecs,
        const std::vector<cv::Vec3d>& tvecs) const
    {
        std::vector<double> rms;
        rms.reserve(allCorners.size());

        for (size_t i = 0; i < allCorners.size(); ++i)
        {
            const auto objPoints = board.boardPointsFromIds(allIds[i]);
            if (objPoints.size() != allCorners[i].size())
            {
                rms.push_back(-1.0);
                continue;
            }

            std::vector<cv::Point2f> projected;
            cv::projectPoints(objPoints, rvecs[i], tvecs[i], intr.K, intr.D, projected);

            double errSum = 0.0;
            for (size_t k = 0; k < projected.size(); ++k)
            {
                const cv::Point2f diff = projected[k] - allCorners[i][k];
                errSum += diff.x * diff.x + diff.y * diff.y;
            }

            const double mse = projected.empty() ? -1.0 : errSum / static_cast<double>(projected.size());
            rms.push_back(mse >= 0.0 ? std::sqrt(mse) : -1.0);
        }

        return rms;
    }

    IntrinsicsCalibrator::Result IntrinsicsCalibrator::calibrate(const std::vector<CharucoDetection>& views,
                                                                 const CharucoBoardModel& board,
                                                                 const cv::Size& imageSize) const
    {
        Result res;

        if (imageSize.width <= 0 || imageSize.height <= 0)
        {
            res.message = "IntrinsicsCalibrator: invalid image size.";
            return res;
        }

        std::vector<std::vector<cv::Point2f>> allCorners;
        std::vector<std::vector<int>> allIds;
        std::vector<int> usedIndices;
        buildCharucoArrays(views, board, allCorners, allIds, usedIndices);

        if (static_cast<int>(allCorners.size()) < prm_.minViews)
        {
            res.message = "IntrinsicsCalibrator: not enough usable views.";
            return res;
        }

        cv::Mat cameraMatrix, distCoeffs;
        std::vector<cv::Vec3d> rvecs, tvecs;

        const double rms = cv::aruco::calibrateCameraCharuco(
            allCorners,
            allIds,
            board.board(),
            imageSize,
            cameraMatrix,
            distCoeffs,
            rvecs,
            tvecs,
            prm_.flags,
            prm_.criteria);

        res.intr.K = cameraMatrix;
        res.intr.D = distCoeffs;
        res.intr.imageSize = imageSize;
        res.rms = rms;
        res.usedViewIndices = std::move(usedIndices);
        res.rvecs = std::move(rvecs);
        res.tvecs = std::move(tvecs);
        res.perViewRms = computePerViewRmse(allCorners, allIds, board, res.intr, res.rvecs, res.tvecs);
        res.ok = true;
        return res;
    }
}
