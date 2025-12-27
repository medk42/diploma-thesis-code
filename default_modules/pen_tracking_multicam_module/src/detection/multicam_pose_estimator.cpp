#include "detection/multicam_pose_estimator.h"

#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <opencv2/imgproc.hpp>


using namespace aergo::default_modules::pen_tracking_multicam_module;


HuberLoss::HuberLoss(double delta)
: delta_(delta)
{
    if (delta_ <= 0)
    {
        throw std::invalid_argument("Huber loss delta must be positive");
    }
}


double HuberLoss::operator()(double error) const
{
    double absError = std::abs(error);
    if (absError <= delta_)
    {
        return 0.5 * error * error;
    }
    else
    {
        return delta_ * (absError - 0.5 * delta_);
    }
}


MulticamPoseEstimator::MulticamPoseEstimator(
    std::map<int, MarkerData> markersData,
    cv::SolvePnPMethod pnpMethod,
    double huberDelta,
    double normalizedReprojectionErrorThreshold
)
: markersData_(std::move(markersData)),
  pnpMethod_(pnpMethod),
  huberLoss_(huberDelta),
  normalizedReprojectionErrorThreshold_(normalizedReprojectionErrorThreshold)
{
    if (markersData_.empty())
    {
        throw std::invalid_argument("markersData cannot be empty");
    }
    if (normalizedReprojectionErrorThreshold <= 0)
    {
        throw std::invalid_argument("reprojectionErrorThreshold must be positive");
    }
}


double markerQualityScore(const std::array<cv::Point2f, 4>& corners)
{
    if (corners.size() != 4) return 0.0;

    double area = std::abs(cv::contourArea(corners));
    if (area <= 0.0) return 0.0;

    // ---- side lengths ----
    double s[4];
    for (int i = 0; i < 4; ++i)
        s[i] = cv::norm(corners[i] - corners[(i + 1) % 4]);

    double mean_s = (s[0] + s[1] + s[2] + s[3]) * 0.25;
    if (mean_s <= 1e-6) return 0.0;

    double var = 0.0;
    for (int i = 0; i < 4; ++i)
        var += (s[i] - mean_s) * (s[i] - mean_s);
    var *= 0.25;

    double cv_sides = std::sqrt(var) / mean_s;

    // ---- diagonals ----
    double d0 = cv::norm(corners[0] - corners[2]);
    double d1 = cv::norm(corners[1] - corners[3]);
    double diag_error = std::abs(d0 - d1) / ((d0 + d1) * 0.5 + 1e-6);

    // ---- squareness ----
    double squareness = std::exp(-(cv_sides + diag_error)); // in (0, 1], 1 is perfect

    // ---- final score ----
    return std::sqrt(area) * squareness;
}


std::array<cv::Point3d, 4> triangulateCenterNormalized(
    const std::array<cv::Point2f,4>& u1_px, const std::array<cv::Point2f,4>& u2_px,
    const cv::Mat& K1, const cv::Mat& D1,
    const cv::Mat& K2, const cv::Mat& D2,
    const pu::SE3& T_2_1
)
{
    // 1) undistort -> normalized coords
    std::array<cv::Point2f, 4> n1, n2;
    cv::undistortPoints(u1_px, n1, K1, D1); // -> normalized
    cv::undistortPoints(u2_px, n2, K2, D2);

    // 2) projection matrices in normalized camera coords
    cv::Matx34d P1;
    pu::SE3::unit().toProjection(P1);

    cv::Matx34d P2;
    T_2_1.toProjection(P2);

    cv::Matx34f P1f(P1);
    cv::Matx34f P2f(P2);

    // 3) triangulate
    cv::Mat X4;
    cv::triangulatePoints(P1f, P2f, n1, n2, X4); // 4xN

    std::array<cv::Point3d, 4> Xs;
    for (int i = 0; i < 4; ++i)
    {
         double w = X4.at<float>(3,i);
         Xs[i] = cv::Point3d(
            X4.at<float>(0,i)/w,
            X4.at<float>(1,i)/w,
            X4.at<float>(2,i)/w
         );
    }

    return Xs; // in camera 1 coords
}


std::optional<std::pair<size_t, size_t>> findMarkerInOtherCameras(
    std::span<MarkerDetector::DetectionResult> detectedMarkersPerCamera,
    size_t excludeCamIdx,
    int markerId
)
{
    for (size_t camIdx = 0; camIdx < detectedMarkersPerCamera.size(); ++camIdx)
    {
        if (camIdx == excludeCamIdx) continue;

        const auto& detection = detectedMarkersPerCamera[camIdx];
        for (size_t markerIdx = 0; markerIdx < detection.markers.size(); ++markerIdx)
        {
            const auto& marker = detection.markers[markerIdx];
            if (marker.id == markerId)
            {
                return std::make_pair(camIdx, markerIdx);
            }
        }
    }
    return std::nullopt;
}


bool MulticamPoseEstimator::multicamCorrectZ(
    const std::vector<CameraData>& camerasData,
    const std::span<MarkerDetector::DetectionResult> detectedMarkersPerCamera, 
    const size_t camIdx, const size_t markerIdx, const pu::SE3& T_cam1_marker,
    pu::SE3& out_T_cam1_marker_corrected, CorrectionInfo& out_correction_info
)
{
    pu::SE3 T_1_ref = camerasData[camIdx].T_cam_ref;
    auto marker_1 = detectedMarkersPerCamera[camIdx].markers[markerIdx];

    auto match = findMarkerInOtherCameras(detectedMarkersPerCamera, camIdx, marker_1.id);
    if (!match.has_value())
    {
        return false; // no match found
    }

    auto marker_2 = detectedMarkersPerCamera[match->first].markers[match->second];
    pu::SE3 T_2_ref = camerasData[match->first].T_cam_ref;

    pu::SE3 T_2_1 = T_2_ref * T_1_ref.inverse();

    std::array<cv::Point3d, 4> marker_corners_cam1 = triangulateCenterNormalized(
        marker_1.corners,
        marker_2.corners,
        camerasData[camIdx].K,
        camerasData[camIdx].D,
        camerasData[match->first].K,
        camerasData[match->first].D,
        T_2_1
    );

    auto marker_it = markersData_.find(marker_1.id);
    if (marker_it == markersData_.end())
    {
        return false; // marker data not found
    }

    const auto& objectPts = marker_it->second.objectPoints();
    std::array<double, 4> scales;
    for (size_t i = 0; i < 4; ++i)
    {
        auto pnp_marker_corner_cam1 = T_cam1_marker * objectPts[i];
        scales[i] = cv::norm(marker_corners_cam1[i]) / cv::norm(pnp_marker_corner_cam1);
    }

    std::sort(scales.begin(), scales.end());
    out_correction_info.scale = (scales[1] + scales[2]) * 0.5;
    out_correction_info.spread = scales[3] - scales[0];

    pu::SE3 T_cam1_marker_corrected = T_cam1_marker;
    T_cam1_marker_corrected.t *= out_correction_info.scale;
    out_T_cam1_marker_corrected = T_cam1_marker_corrected;

    return true;
}


void updateBestCandidate(const MulticamPoseEstimator::CandidatePose& a, size_t cam_idx, size_t mark_idx,
                  size_t& bestInliers, double& bestQual, double& bestErr,
                  MulticamPoseEstimator::DetectionResult& outResult, bool refinedFlag)
{
    if (!a.valid) return;
    if (a.inlierCount > bestInliers ||
        (a.inlierCount == bestInliers && a.markerQuality > bestQual) ||
        (a.inlierCount == bestInliers && a.markerQuality == bestQual && a.errorSum < bestErr))
    {
        bestInliers = a.inlierCount;
        bestQual    = a.markerQuality;
        bestErr     = a.errorSum;
        outResult.best_camera_index = cam_idx;
        outResult.best_marker_index = mark_idx;
        outResult.refinedPoseUsed   = refinedFlag;
    }
};


void validateCameraData(const std::vector<MulticamPoseEstimator::CameraData>& camerasData)
{
    if (camerasData.empty())
    {
        throw std::invalid_argument("camerasData cannot be empty");
    }
    for (const auto& camData : camerasData)
    {
        if (camData.K.empty() || camData.D.empty())
        {
            throw std::invalid_argument("Camera intrinsics (K, D) cannot be empty");
        }
        if (camData.K.rows != 3 || camData.K.cols != 3 || camData.K.type() != CV_64F)
        {
            throw std::invalid_argument("Camera intrinsic matrix K must be 3x3 of type CV_64F");
        }
        if (camData.D.rows != 1 || (camData.D.cols < 4 || camData.D.cols > 14) || camData.D.type() != CV_64F)
        {
            throw std::invalid_argument("Camera distortion coefficients D must be 1xN (4 <= N <= 14) of type CV_64F");
        }
    }
}



void MulticamPoseEstimator::estimatePose(
    const std::vector<CameraData>& camerasData,
    std::span<MarkerDetector::DetectionResult> detectedMarkersPerCamera,
    DetectionResult& outResult
)
{
    outResult.success = false;

    validateCameraData(camerasData);

    if (detectedMarkersPerCamera.size() != camerasData.size())
    {
        throw std::invalid_argument("Size of detectedMarkersPerCamera must match number of cameras");
    }

    if (outResult.candidatePosesPerCameraPerMarker.size() != camerasData.size())
    {
        throw std::invalid_argument("outResult candidatePosesPerCameraPerMarker size must match number of cameras");
    }

    size_t bestInlierCount = 0;
    double bestMarkerQuality = 0.0;
    double bestErrorSum = std::numeric_limits<double>::max();

    outResult.best_camera_index = 0;
    outResult.best_marker_index = 0;
    outResult.normalizedReprojectionErrorThreshold = normalizedReprojectionErrorThreshold_;

    for (size_t camIdx = 0; camIdx < camerasData.size(); ++camIdx)
    {
        if (outResult.candidatePosesPerCameraPerMarker[camIdx].size() < detectedMarkersPerCamera[camIdx].markers.size())
        {
            throw std::invalid_argument("outResult candidatePosesPerCameraPerMarker inner vector size must be at least number of detected markers for each camera");
        }
        outResult.markersPerCameraCount[camIdx] = detectedMarkersPerCamera[camIdx].markers.size();

        const auto& camData = camerasData[camIdx];
        const auto& detection = detectedMarkersPerCamera[camIdx];

        for (size_t markerIdx = 0; markerIdx < detection.markers.size(); ++markerIdx)
        {
            auto& candidatePose = outResult.candidatePosesPerCameraPerMarker[camIdx][markerIdx];
            auto& candidatePoseRefined = outResult.refinedCandidatePosesPerCameraPerMarker[camIdx][markerIdx];
            candidatePose.valid = false; // Initialize as invalid
            candidatePoseRefined.valid = false; // Initialize as invalid

            const auto& detectedMarker = detection.markers[markerIdx];

            auto markerIt = markersData_.find(detectedMarker.id);
            if (markerIt == markersData_.end())
            {
                continue; // Marker not in our known set
            }
            const auto& markerData = markerIt->second;

            cv::Vec3d rvec, tvec;
            if (!cv::solvePnP(
                markerData.objectPoints(),
                detectedMarker.corners,
                camData.K,
                camData.D,
                rvec,
                tvec,
                false,
                pnpMethod_
            ))
            {
                continue; // PnP failed for this marker
            }

            pu::SE3 T_cam_marker = pu::SE3::fromRvecTvec(rvec, tvec);
            candidatePose.T_ref_pose = camData.T_cam_ref.inverse() * T_cam_marker * markerData.T_marker_pose();
            candidatePose.valid = true;

            evaluateError(camerasData, detectedMarkersPerCamera, candidatePose);

            candidatePose.markerQuality = markerQualityScore(detectedMarker.corners);

            // Update best pose if needed
            updateBestCandidate(
                candidatePose, camIdx, markerIdx,
                bestInlierCount, bestMarkerQuality, bestErrorSum,
                outResult, false
            );

            // ---- refined pose with Z correction ----
            pu::SE3 T_cam_marker_corrected;
            CorrectionInfo correctionInfo;
            if (multicamCorrectZ(camerasData, detectedMarkersPerCamera, camIdx, markerIdx, T_cam_marker, T_cam_marker_corrected, correctionInfo))
            {
                candidatePoseRefined.T_ref_pose = camData.T_cam_ref.inverse() * T_cam_marker_corrected * markerData.T_marker_pose();
                candidatePoseRefined.correctionInfo = correctionInfo;
                candidatePoseRefined.valid = true;
                
                evaluateError(camerasData, detectedMarkersPerCamera, candidatePoseRefined);

                candidatePoseRefined.markerQuality = candidatePose.markerQuality;

                updateBestCandidate(
                    candidatePoseRefined, camIdx, markerIdx,
                    bestInlierCount, bestMarkerQuality, bestErrorSum,
                    outResult, true
                );
            }
        }
    }

    outResult.success = bestInlierCount > 0;
}


float averageSideLength(const std::array<cv::Point2f, 4>& corners)
{
    float side1 = cv::norm(corners[0] - corners[1]);
    float side2 = cv::norm(corners[1] - corners[2]);
    float side3 = cv::norm(corners[2] - corners[3]);
    float side4 = cv::norm(corners[3] - corners[0]);
    return (side1 + side2 + side3 + side4) / 4.0f;
}


void MulticamPoseEstimator::evaluateError(
    const std::vector<CameraData>& camerasData,
    std::span<MarkerDetector::DetectionResult> detectedMarkersPerCamera, 
    CandidatePose& candidatePose
)
{
    if (!candidatePose.valid)
    {
        throw std::invalid_argument("CandidatePose is not valid");
    }

    if (candidatePose.reprojectionsPerCameraPerMarker.size() != camerasData.size())
    {
        throw std::invalid_argument("CandidatePose reprojectionsPerCameraPerMarker size must match number of cameras");
    }

    candidatePose.errorSum = 0.0;
    candidatePose.markerQuality = 0.0;
    candidatePose.inlierCount = 0;
    candidatePose.totalCount = 0;

    for (size_t camIdx = 0; camIdx < camerasData.size(); ++camIdx)
    {
        if (candidatePose.reprojectionsPerCameraPerMarker[camIdx].size() < detectedMarkersPerCamera[camIdx].markers.size())
        {
            throw std::invalid_argument("CandidatePose reprojectionsPerCameraPerMarker inner vector size must be at least number of detected markers for each camera");
        }

        const auto& camData = camerasData[camIdx];
        const auto& detection = detectedMarkersPerCamera[camIdx];

        for (size_t markerIdx = 0; markerIdx < detection.markers.size(); ++markerIdx)
        {
            const auto& detectedMarker = detection.markers[markerIdx];
            auto& reproj = candidatePose.reprojectionsPerCameraPerMarker[camIdx][markerIdx];
            reproj.valid = false; // Initialize as invalid

            auto markerIt = markersData_.find(detectedMarker.id);
            if (markerIt == markersData_.end())
            {
                continue; // Marker not in our known set
            }
            const auto& markerData = markerIt->second;

            pu::SE3 T_cam_marker_estimated = camData.T_cam_ref * candidatePose.T_ref_pose * markerData.T_marker_pose().inverse();

            cv::Vec3d rvec_estimated, tvec_estimated;
            T_cam_marker_estimated.toRvecTvec(rvec_estimated, tvec_estimated);

            std::vector<cv::Point2f> projectedPoints;
            cv::projectPoints(
                markerData.objectPoints(),
                rvec_estimated,
                tvec_estimated,
                camData.K,
                camData.D,
                projectedPoints
            );

            reproj.averageSideLengthPx = std::max(averageSideLength(detectedMarker.corners), 1e-6f); // avoid division by zero
            for (size_t ptIdx = 0; ptIdx < 4; ++ptIdx)
            {
                reproj.projectedPoints[ptIdx] = projectedPoints[ptIdx];
                reproj.reprojectionErrorsPxNormalized[ptIdx] = cv::norm(projectedPoints[ptIdx] - detectedMarker.corners[ptIdx]) / reproj.averageSideLengthPx;
                reproj.inliers[ptIdx] = (reproj.reprojectionErrorsPxNormalized[ptIdx] <= normalizedReprojectionErrorThreshold_);
                candidatePose.errorSum += huberLoss_(reproj.reprojectionErrorsPxNormalized[ptIdx]);
                candidatePose.totalCount++;
                if (reproj.inliers[ptIdx])
                {
                    candidatePose.inlierCount++;
                }
            }
            reproj.valid = true;
        }
    }
}