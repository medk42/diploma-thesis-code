#pragma once

#include <atomic>
#include <expected>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "calib/types.h"
#include "calib/charuco_board_model.h"
#include "calib/charuco_detector.h"
#include "calib/intrinsics_calibrator.h"
#include "calib/stereo_calibrator.h"
#include "calib/handeye_calibrator.h"
#include "calib/rig_refiner_ceres.h"

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    struct StereoRigMetrics
    {
        double intrinsics_left_rms{-1.0};              ///< Reprojection RMS from left intrinsics calibration.
        double intrinsics_right_rms{-1.0};             ///< Reprojection RMS from right intrinsics calibration.
        size_t intrinsics_left_used_views{0};          ///< Number of left views used for intrinsics calibration.
        size_t intrinsics_right_used_views{0};         ///< Number of right views used for intrinsics calibration.
        double stereo_rms{-1.0};                       ///< RMS from stereo calibration.
        double stereo_mean_sampson{-1.0};              ///< Mean Sampson distance after stereo calibration.
        double stereo_median_sampson{-1.0};            ///< Median Sampson distance after stereo calibration.
        size_t stereo_used_pairs{0};                   ///< Count of stereo pairs used in stereo calibration.
        size_t hand_eye_usable_pairs{0};               ///< Count of pairs usable for hand-eye calibration.
        double hand_eye_rms{-1.0};                     ///< RMS from hand-eye calibration.
        CameraIntrinsics camera_intrinsics_left;       ///< Final left camera intrinsics.
        CameraIntrinsics camera_intrinsics_right;      ///< Final right camera intrinsics.
        StereoExtrinsics stereo_extrinsics;            ///< Final stereo extrinsics (Right <- Left).
        SE3 camL_from_flange;                          ///< Final camL <- flange transform.
        SE3 camR_from_flange;                          ///< Final camR <- flange transform.
        SE3 world_from_board;                          ///< Estimated world <- board (if refined).
        double refine_initial_reproj_rmse_l{-1.0};     ///< Initial left reprojection RMSE before refinement.
        double refine_initial_reproj_rmse_r{-1.0};     ///< Initial right reprojection RMSE before refinement.
        double refine_final_reproj_rmse_l{-1.0};       ///< Final left reprojection RMSE after refinement.
        double refine_final_reproj_rmse_r{-1.0};       ///< Final right reprojection RMSE after refinement.
    };

    struct StereoRigProgress
    {
        uint32_t current_progress{0};                  ///< Current progress tick.
        uint32_t max_progress{0};                      ///< Total ticks for the calibration run.
    };

    struct StereoRigWorldCameraPoses
    {
        Pose pose_left;                                ///< World <- left camera pose.
        Pose pose_right;                               ///< World <- right camera pose.
        CameraIntrinsics intrinsics_left;              ///< Left camera intrinsics/distortion.
        CameraIntrinsics intrinsics_right;             ///< Right camera intrinsics/distortion.
    };

    /**
     * @brief Runs a full stereo rig calibration (intrinsics, stereo extrinsics, hand-eye, optional refinement) once.
     *        Subsequent calls to runStereoRobotCalibration will fail; create a new instance for another run.
     *        report() may be called at any time (even before/after calibration), but only makes sense after a run.
     */
    class StereoRigCalibrator
    {
    public:
        StereoRigCalibrator() = default;

        /**
         * @brief Execute the stereo + robot calibration pipeline. May be called only once.
         * @param board_params Charuco board configuration.
         * @param det_params Detector parameters.
         * @param intr_params Intrinsics calibrator parameters.
         * @param stereo_params Stereo calibrator parameters.
         * @param handeye_params Hand-eye calibrator parameters.
         * @param refine_opts Ceres refinement options.
         * @param left_images Left camera images (aligned with right_images and robot_poses).
         * @param right_images Right camera images.
         * @param robot_poses World<-flange poses.
         * @return expected<void, std::string> Error string on failure.
         */
        std::expected<void, std::string> runStereoRobotCalibration(
            const CharucoBoardModel::Params& board_params,
            const CharucoDetector::Params& det_params,
            const IntrinsicsCalibrator::Params& intr_params,
            const StereoCalibrator::Params& stereo_params,
            const HandEyeCalibrator::Params& handeye_params,
            const RigRefinerCeres::Options& refine_opts,
            const std::vector<cv::Mat>& left_images,
            const std::vector<cv::Mat>& right_images,
            const std::vector<Pose>& robot_poses);

        /**
         * @brief Report calibration metrics (intrinsics/stereo/hand-eye/refine). Metrics only valid after a successful run.
         */
        const StereoRigMetrics& report() const { return metrics_; }

        /**
         * @brief True after a successful calibration (or successful loadJson).
         * False if calibration has not been run yet or if it failed.
         */
        bool valid() const { return valid_; }

        /**
         * @brief Return current progress snapshot (thread-safe).
         */
        StereoRigProgress progress() const;

        /**
         * @brief Request cancellation. If calibration is running, it will terminate early (thread-safe).
         */
        void cancel() { cancel_requested_.store(true, std::memory_order_relaxed); }

        /**
         * @brief Serialize calibration state to JSON.
         */
        nlohmann::json saveJson() const;

        /**
         * @brief Load calibration state from JSON.
         */
        std::expected<void, std::string> loadJson(const nlohmann::json& s);

        /**
         * @brief Given world<-flange, compute world poses for left and right cameras and include intrinsics.
         */
        StereoRigWorldCameraPoses computeWorld(const Pose& world_from_flange) const;

    private:
        void initializeProgress(uint32_t max_progress);
        void updateProgress(uint32_t current_progress);

        std::atomic<uint64_t> packed_progress_{0}; // high 32 bits: max, low 32 bits: current
        std::atomic<bool> cancel_requested_{false};
        std::atomic<bool> already_ran_{false};
        std::atomic<bool> valid_{false};
        StereoRigMetrics metrics_{};
    };
}
