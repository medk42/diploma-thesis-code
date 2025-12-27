#pragma once

#include "module_helpers/pose_utils/pose_utils.h"
#include "marker_detection.h"
#include "multicam_pose_estimator.h"

#include <span>
#include <map>

#include <ceres/solver.h>

namespace aergo::default_modules::pen_tracking_multicam_module
{
    class MulticamPoseOptimizer
    {
    public:
        struct Result {
            pu::SE3 T_ref_pose_optimized; // reference cam <- pose
            bool success;
            ceres::Solver::Summary summary;
        };

        MulticamPoseOptimizer(
            std::map<int, MulticamPoseEstimator::MarkerData> markersData,
            double cauchyLoss = 1.0
        );

        void optimizePoses(
            const std::vector<MulticamPoseEstimator::CameraData>& camerasData,
            pu::SE3 T_ref_pose_initial,
            std::span<MarkerDetector::DetectionResult> detectedMarkersPerCamera,
            Result& outResult,
            bool use_wz_from_first_camera = true
        );

    private:
        std::map<int, MulticamPoseEstimator::MarkerData> markersData_;
        double cauchyLoss_;
    };
}