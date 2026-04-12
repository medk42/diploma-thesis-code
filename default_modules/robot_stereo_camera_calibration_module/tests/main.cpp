#include "calib/stereo_rig_calibrator.h"
#include "calib/charuco_defaults.h"
#include "calib/pose_utils.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>
#include <string>
#include <thread>

using namespace aergo::default_modules::robot_stereo_camera_calibration_module;
using namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib;

namespace
{
    using nlohmann::json;

    struct DemoSample
    {
        int frame_index{-1};
        cv::Mat img_left;
        cv::Mat img_right;
        Pose robot_pose{}; // world <- flange
    };

    Pose parse_pose(const json& j)
    {
        Pose p{};
        p.position.x = j["position"]["x"].get<double>();
        p.position.y = j["position"]["y"].get<double>();
        p.position.z = j["position"]["z"].get<double>();
        p.orientation.x = j["orientation"]["x"].get<double>();
        p.orientation.y = j["orientation"]["y"].get<double>();
        p.orientation.z = j["orientation"]["z"].get<double>();
        p.orientation.w = j["orientation"]["w"].get<double>();
        return p;
    }

    std::vector<DemoSample> load_demo_samples(const std::filesystem::path& base_path)
    {
        std::vector<DemoSample> samples;
        const auto poses_path = base_path / "poses.jsonl";
        std::ifstream ifs(poses_path);
        if (!ifs)
        {
            std::cout << "[load_demo_samples] Failed to open " << poses_path << std::endl;
            return samples;
        }

        std::string line;
        while (std::getline(ifs, line))
        {
            if (line.empty()) continue;
            json j;
            try
            {
                j = json::parse(line);
            }
            catch (const std::exception& e)
            {
                std::cout << "[load_demo_samples] JSON parse error: " << e.what() << std::endl;
                continue;
            }

            DemoSample s;
            s.frame_index = j.value("frame_index", -1);
            const auto left_rel = j["files"]["left"].get<std::string>();
            const auto right_rel = j["files"]["right"].get<std::string>();
            const auto left_path = (base_path / left_rel).string();
            const auto right_path = (base_path / right_rel).string();

            s.robot_pose = parse_pose(j["robot_pose_world"]);
            s.img_left = cv::imread(left_path, cv::IMREAD_COLOR);
            s.img_right = cv::imread(right_path, cv::IMREAD_COLOR);
            if (s.img_left.empty() || s.img_right.empty())
            {
                std::cout << "[load_demo_samples] Failed to read images for frame " << s.frame_index << std::endl;
                continue;
            }
            samples.push_back(std::move(s));
        }
        return samples;
    }
}

int main()
{
    const auto demo_base = std::filesystem::current_path() / "demo_data";
    auto samples = load_demo_samples(demo_base);
    std::cout << "[main] Loaded " << samples.size() << " samples from " << demo_base << std::endl;
    if (samples.empty())
    {
        return 0;
    }

    // Prepare inputs
    std::vector<cv::Mat> left_images;
    std::vector<cv::Mat> right_images;
    std::vector<Pose> robot_poses;
    left_images.reserve(samples.size());
    right_images.reserve(samples.size());
    robot_poses.reserve(samples.size());
    for (const auto& s : samples)
    {
        left_images.push_back(s.img_left);
        right_images.push_back(s.img_right);
        robot_poses.push_back(s.robot_pose);
    }

    // Parameter defaults (explicit)
    CharucoBoardModel::Params board_params{
        defaults::charucoboard::ROW_COUNT,
        defaults::charucoboard::COL_COUNT,
        defaults::charucoboard::SQUARE_LENGTH,
        defaults::charucoboard::MARKER_LENGTH,
        cv::aruco::DICT_4X4_100,
        true
    };

    CharucoDetector::Params det_params;
    det_params.adaptiveWinMin = 3;
    det_params.adaptiveWinMax = 23;
    det_params.adaptiveWinStep = 10;
    det_params.minMarkerPerimeterRate = 0.02;
    det_params.refineSubpix = true;
    det_params.subpixWin = cv::Size(5, 5);
    det_params.subpixMaxIters = 50;
    det_params.subpixEps = 0.01;
    det_params.minCharucoCorners = 12;
    det_params.minArucoMarkers = 4;

    IntrinsicsCalibrator::Params intr_params;
    intr_params.minCharucoCornersPerView = 12;
    intr_params.minViews = 8;
    intr_params.criteria = cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 50, 1e-9);

    StereoCalibrator::Params stereo_params;
    stereo_params.minSharedCharucoCorners = 10;
    stereo_params.minPairs = 8;
    stereo_params.fixIntrinsics = true;

    HandEyeCalibrator::Params handeye_params;
    handeye_params.method = cv::CALIB_HAND_EYE_TSAI;
    handeye_params.minPairs = 8;

    RigRefinerCeres::Options refine_opts;
    refine_opts.refineStereo = true;
    refine_opts.refineHandEye = true;
    refine_opts.estimateBoardInWorld = true;
    refine_opts.maxIters = 50;
    refine_opts.huberDelta = 1.0;

    StereoRigCalibrator calibrator;
    std::atomic<bool> stop_thread{false};
    std::atomic<uint32_t> last_progress{0};

    std::thread progress_thread([&]()
    {
        while (!stop_thread.load(std::memory_order_relaxed))
        {
            auto prog = calibrator.progress();
            if (prog.current_progress != last_progress.load(std::memory_order_relaxed))
            {
                last_progress.store(prog.current_progress, std::memory_order_relaxed);
                std::cout << "\rProgress: " << prog.current_progress << " / " << prog.max_progress << std::flush;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    auto res = calibrator.runStereoRobotCalibration(
        board_params,
        det_params,
        intr_params,
        stereo_params,
        handeye_params,
        refine_opts,
        left_images,
        right_images,
        robot_poses,
        false
    );

    stop_thread.store(true, std::memory_order_relaxed);
    progress_thread.join();
    std::cout << std::endl;

    if (!res.has_value())
    {
        std::cout << "Calibration failed: " << res.error() << std::endl;
        return 0;
    }

    const auto& m = calibrator.report();
    std::cout << "Calibration succeeded\n";
    std::cout << "Intrinsics L RMS: " << m.intrinsics_left_rms << " (views " << m.intrinsics_left_used_views << ")\n";
    std::cout << "Intrinsics R RMS: " << m.intrinsics_right_rms << " (views " << m.intrinsics_right_used_views << ")\n";
    std::cout << "Stereo RMS: " << m.stereo_rms << " mean Sampson=" << m.stereo_mean_sampson
              << " median Sampson=" << m.stereo_median_sampson << " pairs=" << m.stereo_used_pairs << "\n";
    std::cout << "Hand-eye usable pairs: " << m.hand_eye_usable_pairs << "\n";
    std::cout << "Refine RMSE L: " << m.refine_initial_reproj_rmse_l << " -> " << m.refine_final_reproj_rmse_l
              << " | R: " << m.refine_initial_reproj_rmse_r << " -> " << m.refine_final_reproj_rmse_r << "\n";
    std::cout << "camL<-flange t: " << cv::Mat(m.camL_from_flange.t).t();
    std::cout << " camR<-flange t: " << cv::Mat(m.camR_from_flange.t).t() << "\n";
    std::cout << "Stereo t (R<-L): " << cv::Mat(m.stereo_extrinsics.t_RL).t() << "\n";
    std::cout << "Done.\n";
    return 0;
}
