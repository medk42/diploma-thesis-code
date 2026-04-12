#include "calib/stereo_rig_calibrator.h"
#include "calib/pose_utils.h"

#include <limits>
#include <thread>

namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib
{
    namespace
    {
        inline uint64_t packProgress(uint32_t current, uint32_t max)
        {
            return (static_cast<uint64_t>(max) << 32) | static_cast<uint64_t>(current);
        }

        inline StereoRigProgress unpackProgress(uint64_t packed)
        {
            StereoRigProgress out{};
            out.current_progress = static_cast<uint32_t>(packed & 0xFFFFFFFFu);
            out.max_progress = static_cast<uint32_t>(packed >> 32);
            return out;
        }
    }

    void StereoRigCalibrator::initializeProgress(uint32_t max_progress)
    {
        packed_progress_.store(packProgress(0, max_progress), std::memory_order_relaxed);
    }

    void StereoRigCalibrator::updateProgress(uint32_t current_progress)
    {
        StereoRigProgress prog = unpackProgress(packed_progress_.load(std::memory_order_relaxed));
        packed_progress_.store(packProgress(current_progress, prog.max_progress), std::memory_order_relaxed);
    }

    StereoRigProgress StereoRigCalibrator::progress() const
    {
        return unpackProgress(packed_progress_.load(std::memory_order_relaxed));
    }

    std::expected<void, std::string> StereoRigCalibrator::runStereoRobotCalibration(
        const CharucoBoardModel::Params& board_params,
        const CharucoDetector::Params& det_params,
        const IntrinsicsCalibrator::Params& intr_params,
        const StereoCalibrator::Params& stereo_params,
        const HandEyeCalibrator::Params& handeye_params,
        const RigRefinerCeres::Options& refine_opts,
        const std::vector<cv::Mat>& left_images,
        const std::vector<cv::Mat>& right_images,
        const std::vector<Pose>& robot_poses,
        bool skip_handeye)
    {
        if (already_ran_.load(std::memory_order_relaxed))
        {
            return std::unexpected("Calibration already executed; create a new StereoRigCalibrator instance.");
        }
        already_ran_.store(true, std::memory_order_relaxed);

        if (left_images.size() != right_images.size() || left_images.size() != robot_poses.size() || left_images.empty())
        {
            return std::unexpected("Input sizes mismatch or empty images.");
        }

        cancel_requested_.store(false, std::memory_order_relaxed);

        const size_t n = left_images.size();
        // Per frame: 2 (detect L/R) + 1 (board pose tick). Then +3 intr L/R + stereo; +1 hand-eye +1 Ceres refine, or neither when skip_handeye (virtual static flange / moving board).
        const uint32_t max_ticks = static_cast<uint32_t>(3 * n + (skip_handeye ? 3 : 5));
        initializeProgress(max_ticks);
        uint32_t tick = 0;

        CharucoBoardModel board = CharucoBoardModel::Create(board_params);
        CharucoDetector detector(board, det_params);

        std::vector<CharucoDetector::Result> viewsL;
        std::vector<CharucoDetector::Result> viewsR;
        viewsL.reserve(n);
        viewsR.reserve(n);

        for (size_t i = 0; i < n; ++i)
        {
            if (cancel_requested_.load(std::memory_order_relaxed))
            {
                return std::unexpected("Calibration cancelled.");
            }
            viewsL.push_back(detector.detect(left_images[i]));
            ++tick; updateProgress(tick);
            viewsR.push_back(detector.detect(right_images[i]));
            ++tick; updateProgress(tick);
        }

        IntrinsicsCalibrator intrCalib(intr_params);
        auto intrL_res = intrCalib.calibrate(viewsL, board, left_images.front().size());
        ++tick; updateProgress(tick);
        if (!intrL_res.ok)
        {
            return std::unexpected("Left intrinsics calibration failed: " + intrL_res.message);
        }
        auto intrR_res = intrCalib.calibrate(viewsR, board, right_images.front().size());
        ++tick; updateProgress(tick);
        if (!intrR_res.ok)
        {
            return std::unexpected("Right intrinsics calibration failed: " + intrR_res.message);
        }

        StereoCalibrator stereo(stereo_params);
        auto stereo_res = stereo.calibrate(viewsL, viewsR, board, intrL_res.intr, intrR_res.intr);
        ++tick; updateProgress(tick);
        if (!stereo_res.ok)
        {
            return std::unexpected("Stereo calibration failed: " + stereo_res.message);
        }

        // Estimate board pose per usable frame (left camera)
        std::vector<Pose> base_from_flange; // T_base_flange, flange pose in base
        std::vector<cv::Vec3d> rvec_tc;
        std::vector<cv::Vec3d> tvec_tc;
        base_from_flange.reserve(n);
        rvec_tc.reserve(n);
        tvec_tc.reserve(n);

        for (size_t i = 0; i < n; ++i)
        {
            if (cancel_requested_.load(std::memory_order_relaxed))
            {
                return std::unexpected("Calibration cancelled.");
            }
            cv::Vec3d rvec, tvec;
            if (detector.estimateBoardPose(viewsL[i], intrL_res.intr, rvec, tvec))
            {
                base_from_flange.push_back(robot_poses[i]);
                rvec_tc.push_back(rvec);
                tvec_tc.push_back(tvec);
            }
            ++tick; updateProgress(tick);
        }

        if (base_from_flange.empty())
        {
            return std::unexpected(skip_handeye
                ? "Calibration failed: no left views with a detected Charuco board (needed for board pose and refinement)."
                : "Hand-eye calibration failed: no usable pose pairs.");
        }

        SE3 T_flange_cam;
        if (skip_handeye)
        {
            T_flange_cam = SE3{}; // identity: left camera frame == flange (T_FC = flange <- cam)
        }
        else
        {
            HandEyeCalibrator he(handeye_params);
            auto he_res = he.run(base_from_flange, rvec_tc, tvec_tc);
            ++tick; updateProgress(tick);
            if (!he_res.ok)
            {
                return std::unexpected("Hand-eye calibration failed: " + he_res.message);
            }
            T_flange_cam = he_res.T_FC;
        }

        SE3 T_base_flange = pose_utils::toSE3(base_from_flange.front());
        SE3 T_cam_board = pose_utils::rtToSE3(rvec_tc.front(), tvec_tc.front());

        SE3 T_base_cam = pose_utils::compose(T_base_flange, T_flange_cam);
        SE3 T_base_board = pose_utils::compose(T_base_cam, T_cam_board);

        const SE3 camL_from_flange = pose_utils::invert(T_flange_cam);

        // Populate metrics (intrinsics + stereo always; Ceres only when hand-eye runs — skip_handeye implies static virtual flange / moving board, incompatible with single world<-board in Ceres)
        metrics_.intrinsics_left_rms = intrL_res.rms;
        metrics_.intrinsics_right_rms = intrR_res.rms;
        metrics_.intrinsics_left_used_views = intrL_res.usedViewIndices.size();
        metrics_.intrinsics_right_used_views = intrR_res.usedViewIndices.size();
        metrics_.stereo_rms = stereo_res.rms;
        metrics_.stereo_mean_sampson = stereo_res.meanSampson;
        metrics_.stereo_median_sampson = stereo_res.medianSampson;
        metrics_.stereo_used_pairs = stereo_res.usedPairIndices.size();
        metrics_.hand_eye_usable_pairs = base_from_flange.size();

        if (skip_handeye)
        {
            metrics_.camera_intrinsics_left = intrL_res.intr;
            metrics_.camera_intrinsics_right = intrR_res.intr;
            metrics_.stereo_extrinsics = stereo_res.extr;
            metrics_.camL_from_flange = camL_from_flange;
            metrics_.camR_from_flange = pose_utils::compose(SE3{stereo_res.extr.R_RL, stereo_res.extr.t_RL}, camL_from_flange);
            metrics_.world_from_board = T_base_board;
            metrics_.refine_initial_reproj_rmse_l = -1.0;
            metrics_.refine_initial_reproj_rmse_r = -1.0;
            metrics_.refine_final_reproj_rmse_l = -1.0;
            metrics_.refine_final_reproj_rmse_r = -1.0;
            metrics_.refine_message = "Ceres refinement skipped (skip_handeye: nominal board pose from first Charuco frame only; use intrinsics + stereo).";
        }
        else
        {
            RigRefinerCeres refiner(refine_opts);
            RigRefinerCeres::Input rin;
            rin.viewsL = viewsL;
            rin.viewsR = viewsR;
            rin.base_from_flange = robot_poses;
            rin.board = board;
            rin.KL = intrL_res.intr;
            rin.KR = intrR_res.intr;
            rin.RL = stereo_res.extr;
            rin.camL_from_flange = camL_from_flange;
            rin.world_from_board = T_base_board;

            auto refine_res = refiner.refine(rin);
            ++tick; updateProgress(tick);
            if (!refine_res.ok)
            {
                return std::unexpected("Ceres refinement failed: " + refine_res.message);
            }

            metrics_.camera_intrinsics_left = refine_res.KL;
            metrics_.camera_intrinsics_right = refine_res.KR;
            metrics_.stereo_extrinsics = refine_res.RL;
            metrics_.camL_from_flange = refine_res.camL_from_flange;
            metrics_.camR_from_flange = refine_res.camR_from_flange;
            metrics_.world_from_board = refine_res.world_from_board;
            metrics_.refine_initial_reproj_rmse_l = refine_res.initialReprojRmseL;
            metrics_.refine_initial_reproj_rmse_r = refine_res.initialReprojRmseR;
            metrics_.refine_final_reproj_rmse_l = refine_res.finalReprojRmseL;
            metrics_.refine_final_reproj_rmse_r = refine_res.finalReprojRmseR;
            metrics_.refine_message = refine_res.message;
        }

        valid_.store(true, std::memory_order_release); // Calibration successful, can read metrics
        updateProgress(max_ticks);
        return {};
    }

    nlohmann::json StereoRigCalibrator::saveJson() const
    {
        nlohmann::json j;
        const auto matToVec = [](const cv::Mat& M)
        {
            std::vector<double> v;
            if (!M.empty())
            {
                cv::Mat d64;
                M.convertTo(d64, CV_64F);
                d64 = d64.reshape(1, 1);
                v.assign(reinterpret_cast<double*>(d64.data), reinterpret_cast<double*>(d64.data) + d64.total());
            }
            return v;
        };
        j["valid"] = valid_.load(std::memory_order_acquire);
        j["intr_L_rms"] = metrics_.intrinsics_left_rms;
        j["intr_R_rms"] = metrics_.intrinsics_right_rms;
        j["intr_L_used"] = metrics_.intrinsics_left_used_views;
        j["intr_R_used"] = metrics_.intrinsics_right_used_views;
        j["stereo_rms"] = metrics_.stereo_rms;
        j["stereo_mean_sampson"] = metrics_.stereo_mean_sampson;
        j["stereo_median_sampson"] = metrics_.stereo_median_sampson;
        j["stereo_used"] = metrics_.stereo_used_pairs;
        j["hand_eye_pairs"] = metrics_.hand_eye_usable_pairs;
        j["KL_K"] = matToVec(metrics_.camera_intrinsics_left.K);
        j["KL_D"] = matToVec(metrics_.camera_intrinsics_left.D);
        j["KL_size"] = {metrics_.camera_intrinsics_left.imageSize.width, metrics_.camera_intrinsics_left.imageSize.height};
        j["KR_K"] = matToVec(metrics_.camera_intrinsics_right.K);
        j["KR_D"] = matToVec(metrics_.camera_intrinsics_right.D);
        j["KR_size"] = {metrics_.camera_intrinsics_right.imageSize.width, metrics_.camera_intrinsics_right.imageSize.height};

        auto se3ToJson = [](const SE3& T)
        {
            nlohmann::json jj;
            jj["R"] = { T.R(0,0), T.R(0,1), T.R(0,2),
                        T.R(1,0), T.R(1,1), T.R(1,2),
                        T.R(2,0), T.R(2,1), T.R(2,2) };
            jj["t"] = { T.t[0], T.t[1], T.t[2] };
            return jj;
        };

        j["RL"] = se3ToJson(SE3{metrics_.stereo_extrinsics.R_RL, metrics_.stereo_extrinsics.t_RL});
        j["camL_from_flange"] = se3ToJson(metrics_.camL_from_flange);
        j["camR_from_flange"] = se3ToJson(metrics_.camR_from_flange);
        j["world_from_board"] = se3ToJson(metrics_.world_from_board);
        j["refine_initial_rmse_L"] = metrics_.refine_initial_reproj_rmse_l;
        j["refine_initial_rmse_R"] = metrics_.refine_initial_reproj_rmse_r;
        j["refine_final_rmse_L"] = metrics_.refine_final_reproj_rmse_l;
        j["refine_final_rmse_R"] = metrics_.refine_final_reproj_rmse_r;

        return j;
    }

    std::expected<void, std::string> StereoRigCalibrator::loadJson(const nlohmann::json& s)
    {
        auto se3FromJson = [](const nlohmann::json& jj, SE3& out) -> bool
        {
            if (!jj.contains("R") || !jj.contains("t")) return false;
            const auto& r = jj["R"];
            const auto& t = jj["t"];
            if (!r.is_array() || r.size() != 9 || !t.is_array() || t.size() != 3) return false;
            out.R = cv::Matx33d(
                r[0].get<double>(), r[1].get<double>(), r[2].get<double>(),
                r[3].get<double>(), r[4].get<double>(), r[5].get<double>(),
                r[6].get<double>(), r[7].get<double>(), r[8].get<double>());
            out.t = cv::Vec3d(t[0].get<double>(), t[1].get<double>(), t[2].get<double>());
            return true;
        };

        const auto vecToMat = [](const std::vector<double>& v, int rows, int cols) -> cv::Mat
        {
            if (static_cast<int>(v.size()) != rows * cols) return {};
            cv::Mat M(rows, cols, CV_64F);
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                    M.at<double>(r, c) = v[r * cols + c];
            return M;
        };

        try
        {
            metrics_.intrinsics_left_rms = s.value("intr_L_rms", -1.0);
            metrics_.intrinsics_right_rms = s.value("intr_R_rms", -1.0);
            metrics_.intrinsics_left_used_views = s.value("intr_L_used", 0);
            metrics_.intrinsics_right_used_views = s.value("intr_R_used", 0);
            metrics_.stereo_rms = s.value("stereo_rms", -1.0);
            metrics_.stereo_mean_sampson = s.value("stereo_mean_sampson", -1.0);
            metrics_.stereo_median_sampson = s.value("stereo_median_sampson", -1.0);
            metrics_.stereo_used_pairs = s.value("stereo_used", 0);
            metrics_.hand_eye_usable_pairs = s.value("hand_eye_pairs", 0);

            metrics_.camera_intrinsics_left.K = vecToMat(s.value<std::vector<double>>("KL_K", {}), 3, 3);
            metrics_.camera_intrinsics_left.D = vecToMat(s.value<std::vector<double>>("KL_D", {}), 1, static_cast<int>(s.value<std::vector<double>>("KL_D", {}).size()));
            auto szL = s.value<std::vector<int>>("KL_size", {});
            if (szL.size() == 2) metrics_.camera_intrinsics_left.imageSize = cv::Size(szL[0], szL[1]);

            metrics_.camera_intrinsics_right.K = vecToMat(s.value<std::vector<double>>("KR_K", {}), 3, 3);
            metrics_.camera_intrinsics_right.D = vecToMat(s.value<std::vector<double>>("KR_D", {}), 1, static_cast<int>(s.value<std::vector<double>>("KR_D", {}).size()));
            auto szR = s.value<std::vector<int>>("KR_size", {});
            if (szR.size() == 2) metrics_.camera_intrinsics_right.imageSize = cv::Size(szR[0], szR[1]);

            if (!se3FromJson(s.at("RL"), *reinterpret_cast<SE3*>(&metrics_.stereo_extrinsics))) return std::unexpected("Bad RL in JSON.");
            if (!se3FromJson(s.at("camL_from_flange"), metrics_.camL_from_flange)) return std::unexpected("Bad camL_from_flange in JSON.");
            if (!se3FromJson(s.at("camR_from_flange"), metrics_.camR_from_flange)) return std::unexpected("Bad camR_from_flange in JSON.");
            if (!se3FromJson(s.at("world_from_board"), metrics_.world_from_board)) return std::unexpected("Bad world_from_board in JSON.");

            metrics_.refine_initial_reproj_rmse_l = s.value("refine_initial_rmse_L", -1.0);
            metrics_.refine_initial_reproj_rmse_r = s.value("refine_initial_rmse_R", -1.0);
            metrics_.refine_final_reproj_rmse_l = s.value("refine_final_rmse_L", -1.0);
            metrics_.refine_final_reproj_rmse_r = s.value("refine_final_rmse_R", -1.0);

            valid_.store(s.value("valid", false), std::memory_order_release);
        }
        catch (const std::exception& e)
        {
            return std::unexpected(std::string("Failed to load JSON: ") + e.what());
        }

        return {};
    }

    StereoRigWorldCameraPoses StereoRigCalibrator::computeWorld(const Pose& world_from_flange) const
    {
        StereoRigWorldCameraPoses out{};

        SE3 T_WF = pose_utils::toSE3(world_from_flange);
        SE3 T_FL = pose_utils::invert(metrics_.camL_from_flange);
        SE3 T_FR = pose_utils::invert(metrics_.camR_from_flange);
        SE3 T_WL = pose_utils::compose(T_WF, T_FL);
        SE3 T_WR = pose_utils::compose(T_WF, T_FR);

        out.pose_left = pose_utils::toPose(T_WL);
        out.pose_right = pose_utils::toPose(T_WR);
        out.intrinsics_left = metrics_.camera_intrinsics_left;
        out.intrinsics_right = metrics_.camera_intrinsics_right;

        return out;
    }
}
