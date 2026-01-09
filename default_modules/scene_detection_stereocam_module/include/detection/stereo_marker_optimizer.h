#pragma once

#include "pose_utils.h"
#include "scene_marker_detector.h"
#include "stereo_marker_matcher.h"

#include <ceres/solver.h>

namespace aergo::default_modules::scene_detection_stereocam_module
{
    class StereoMarkerOptimizer
    {
    public:
        struct Result {
            SE3 T_ref_marker_optimized; // reference <- marker
            bool success;
            ceres::Solver::Summary summary;
        };

        StereoMarkerOptimizer(double cauchyLoss = 1.0)
        : cauchyLoss_(cauchyLoss)
        {}

        void optimizeMarker(
            const SceneMarkerDetector::CameraData& leftCamera,
            const SceneMarkerDetector::CameraData& rightCamera,
            const StereoMarkerMatcher::MatchResult::MatchedMarkerPair& matchedPair,
            Result& outResult
        );

    private:
        double cauchyLoss_;
    };
}