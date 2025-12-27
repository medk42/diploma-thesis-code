#pragma once

#include <map>
#include <vector>
#include <span>
#include <array>

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>

#include "module_helpers/pose_utils/pose_utils.h"
#include "marker_detection.h"

namespace aergo::default_modules::pen_tracking_multicam_module
{
    namespace pu = aergo::module::helpers::pose_utils;

    class HuberLoss
    {
    public:
        HuberLoss(double delta);
        double operator()(double error) const;

    private:
        double delta_;
    };


    class MulticamPoseEstimator
    {
    public:
        struct MarkerData;
        struct CameraData;
        struct DetectionResult;
        struct CandidatePose;
        struct SingleMarkerReprojection;

        /// @param normalizedReprojectionErrorThreshold reprojection error in pixels divided by the average side length of the marker in pixels
        /// used to determine inliers
        MulticamPoseEstimator(
            std::map<int, MarkerData> markersData, 
            cv::SolvePnPMethod pnpMethod = cv::SolvePnPMethod::SOLVEPNP_IPPE_SQUARE,
            double huberDelta = 0.03,
            double normalizedReprojectionErrorThreshold = 0.05
        );
        
        void estimatePose(
            const std::vector<CameraData>& camerasData,
            std::span<MarkerDetector::DetectionResult> detectedMarkersPerCamera,
            DetectionResult& outResult
        );

    private:
        struct CorrectionInfo { double scale = 1.0; double spread = 0.0; };

        void evaluateError(
            const std::vector<CameraData>& camerasData,
            std::span<MarkerDetector::DetectionResult> detectedMarkersPerCamera, 
            CandidatePose& candidatePose
        );

        bool multicamCorrectZ(
            const std::vector<CameraData>& camerasData,
            const std::span<MarkerDetector::DetectionResult> detectedMarkersPerCamera, 
            const size_t camIdx, const size_t markerIdx, const pu::SE3& T_cam1_marker,
            pu::SE3& out_T_cam1_marker_corrected, CorrectionInfo& out_correction_info
        );

        std::map<int, MarkerData> markersData_;
        cv::SolvePnPMethod pnpMethod_;
        HuberLoss huberLoss_;
        double normalizedReprojectionErrorThreshold_;
    };

    
    class MulticamPoseEstimator::MarkerData
    {
    public:
        MarkerData(float size_m, const pu::SE3& T_marker_pose)
            : size_m_(size_m), T_marker_pose_(T_marker_pose)
        {
            objectPointsCache_[0] = cv::Vec3f(-size_m_/2.0f,  size_m_/2.0f, 0);
            objectPointsCache_[1] = cv::Vec3f( size_m_/2.0f,  size_m_/2.0f, 0);
            objectPointsCache_[2] = cv::Vec3f( size_m_/2.0f, -size_m_/2.0f, 0);
            objectPointsCache_[3] = cv::Vec3f(-size_m_/2.0f, -size_m_/2.0f, 0);
        }

        float size_m() const { return size_m_; }
        const pu::SE3& T_marker_pose() const { return T_marker_pose_; } // marker <- pose frame
        const std::array<cv::Vec3f, 4>& objectPoints() const { return objectPointsCache_; }

    private:
        float size_m_;
        pu::SE3 T_marker_pose_; // marker <- pose frame
        std::array<cv::Vec3f, 4> objectPointsCache_;
    };


    struct MulticamPoseEstimator::CameraData
    {
        cv::Mat K; // Intrinsics
        cv::Mat D; // Distortion
        pu::SE3 T_cam_ref; // cam <- reference cam (eg. left)
    };


    struct MulticamPoseEstimator::SingleMarkerReprojection
    {
        bool valid; // whether the reprojection is valid

        std::array<cv::Point2f, 4> projectedPoints; // 2D projected points
        std::array<double, 4> reprojectionErrorsPxNormalized; // reprojection errors (normalized by average marker side length in pixels)
        std::array<bool, 4> inliers; // whether each point is an inlier based on reprojection error threshold
        double averageSideLengthPx; // average side length of the marker in pixels
    };


    struct MulticamPoseEstimator::CandidatePose
    {
        static CandidatePose preallocate(size_t num_cameras, size_t max_markers_per_camera)
        {
            CandidatePose res;
            res.reprojectionsPerCameraPerMarker.resize(num_cameras);
            for (auto& vec : res.reprojectionsPerCameraPerMarker)
            {
                vec.resize(max_markers_per_camera);
            }
            return res;
        }

        bool valid; // whether the pose estimation was successful
        pu::SE3 T_ref_pose; // reference cam <- pose

        double errorSum; // sum of huber losses over all reprojection errors
        double markerQuality; // quality score of the marker used for this pose
        size_t inlierCount; // number of inliers based on reprojection error threshold
        size_t totalCount; // total number points considered
        CorrectionInfo correctionInfo; // info about any Z correction applied, only for refined poses

        std::vector<std::vector<MulticamPoseEstimator::SingleMarkerReprojection>> reprojectionsPerCameraPerMarker;
    };


    struct MulticamPoseEstimator::DetectionResult
    {
        static DetectionResult preallocate(size_t num_cameras, size_t max_markers_per_camera)
        {
            DetectionResult res;
            res.candidatePosesPerCameraPerMarker.resize(num_cameras);
            res.refinedCandidatePosesPerCameraPerMarker.resize(num_cameras);
            for (size_t cam_idx = 0; cam_idx < num_cameras; ++cam_idx)
            {
                auto& vec = res.candidatePosesPerCameraPerMarker[cam_idx];
                auto& refined_vec = res.refinedCandidatePosesPerCameraPerMarker[cam_idx];
                vec.reserve(max_markers_per_camera);
                refined_vec.reserve(max_markers_per_camera);
                for (size_t i = 0; i < max_markers_per_camera; ++i)
                {
                    vec.emplace_back(std::move(CandidatePose::preallocate(num_cameras, max_markers_per_camera)));
                    refined_vec.emplace_back(std::move(CandidatePose::preallocate(num_cameras, max_markers_per_camera)));
                }
            }
            res.markersPerCameraCount.resize(num_cameras, 0);
            return res;
        }

        // Candidate pose for a single marker detected in a single camera
        // Usage: candidatePosesPerCameraPerMarker[camera_index][marker_index]
        std::vector<std::vector<MulticamPoseEstimator::CandidatePose>> candidatePosesPerCameraPerMarker;
        std::vector<std::vector<MulticamPoseEstimator::CandidatePose>> refinedCandidatePosesPerCameraPerMarker;
        std::vector<size_t> markersPerCameraCount; // number of markers detected per camera

        bool success;                // whether pose estimation was successful overall
        size_t best_camera_index{0}; // index into the outer vector
        size_t best_marker_index{0}; // index into the inner vector
        bool refinedPoseUsed{false}; // whether the best pose is from refined poses

        double normalizedReprojectionErrorThreshold; // reprojection error threshold used for inlier determination
    };
}