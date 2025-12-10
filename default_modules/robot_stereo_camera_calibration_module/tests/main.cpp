#include "calib/pose_utils.h"
#include "calib/types.h"
#include "calib/charuco_defaults.h"

#include "calib/charuco_board_model.h"
#include "calib/charuco_detector.h"
#include "calib/intrinsics_calibrator.h"
#include "calib/stereo_calibrator.h"
#include "calib/handeye_calibrator.h"

#include <filesystem>
#include <iostream>
#include <opencv2/aruco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>

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
        Pose robot_pose{};
        Pose camera_pose{};
        std::string path_left;
        std::string path_right;
    };

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
            if (line.empty())
            {
                continue;
            }

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
            s.path_left = (base_path / left_rel).string();
            s.path_right = (base_path / right_rel).string();

            s.robot_pose = parse_pose(j["robot_pose_world"]);
            s.camera_pose = parse_pose(j["camera_pose_world_est"]);

            s.img_left = cv::imread(s.path_left, cv::IMREAD_COLOR);
            s.img_right = cv::imread(s.path_right, cv::IMREAD_COLOR);

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

    const bool show_images = false;

    for (const auto& s : samples)
    {
        const SE3 Twr = pose_utils::toSE3(s.robot_pose);  // world <- robot
        const SE3 Twc = pose_utils::toSE3(s.camera_pose); // world <- camera
        const SE3 Tcw = pose_utils::invert(Twc);          // camera <- world
        const SE3 Tcr = pose_utils::compose(Tcw, Twr);    // camera <- robot
        const Quaternion qcr = pose_utils::rToQuat(Tcr.R);

        std::cout << "Frame " << s.frame_index
                  << " | left: " << s.img_left.cols << "x" << s.img_left.rows
                  << " | right: " << s.img_right.cols << "x" << s.img_right.rows
                  << "\n  Robot pose pos=(" << s.robot_pose.position.x << ", "
                  << s.robot_pose.position.y << ", " << s.robot_pose.position.z << ")"
                  << " ori=(" << s.robot_pose.orientation.x << ", "
                  << s.robot_pose.orientation.y << ", "
                  << s.robot_pose.orientation.z << ", "
                  << s.robot_pose.orientation.w << ")"
                  << "\n  Camera pose pos=(" << s.camera_pose.position.x << ", "
                  << s.camera_pose.position.y << ", " << s.camera_pose.position.z << ")"
                  << " ori=(" << s.camera_pose.orientation.x << ", "
                  << s.camera_pose.orientation.y << ", "
                  << s.camera_pose.orientation.z << ", "
                  << s.camera_pose.orientation.w << ")"
                  << "\n  Camera<-Robot t=(" << Tcr.t[0] << ", " << Tcr.t[1] << ", " << Tcr.t[2] << ")"
                  << " q=(" << qcr.x << ", " << qcr.y << ", " << qcr.z << ", " << qcr.w << ")"
                  << "\n";

        if (show_images)
        {
            cv::imshow("left", s.img_left);
            cv::imshow("right", s.img_right);
            cv::waitKey(0);
        }
    }

    if (samples.empty())
    {
        return 0;
    }

    std::cout << "Charuco defaults: rows=" << defaults::charucoboard::ROW_COUNT
              << ", cols=" << defaults::charucoboard::COL_COUNT
              << ", square=" << defaults::charucoboard::SQUARE_LENGTH
              << ", marker=" << defaults::charucoboard::MARKER_LENGTH << "\n";

    const auto board = make_demo_board();

    CharucoDetector detector(board);
    std::vector<CharucoDetection> viewsL;
    std::vector<CharucoDetection> viewsR;
    viewsL.reserve(samples.size());
    viewsR.reserve(samples.size());

    const bool show_overlays = false;

    for (const auto& s : samples)
    {
        CharucoDetection dl = detector.detect(s.img_left);
        CharucoDetection dr = detector.detect(s.img_right);

        viewsL.push_back(dl);
        viewsR.push_back(dr);

        if (show_overlays)
        {
            cv::Mat grayL, grayR;
            cv::cvtColor(s.img_left, grayL, cv::COLOR_BGR2GRAY);
            cv::cvtColor(s.img_right, grayR, cv::COLOR_BGR2GRAY);

            cv::Mat visL, visR;
            cv::cvtColor(grayL, visL, cv::COLOR_GRAY2BGR);
            cv::cvtColor(grayR, visR, cv::COLOR_GRAY2BGR);

            cv::aruco::drawDetectedMarkers(visL, dl.markerCorners, dl.markerIds);
            cv::aruco::drawDetectedMarkers(visR, dr.markerCorners, dr.markerIds);

            cv::aruco::drawDetectedCornersCharuco(visL, dl.corners2d, dl.ids, cv::Scalar(0, 255, 0));
            cv::aruco::drawDetectedCornersCharuco(visR, dr.corners2d, dr.ids, cv::Scalar(0, 255, 0));

            cv::imshow("left_overlay", visL);
            cv::imshow("right_overlay", visR);
            cv::waitKey(0);
        }
    }

    IntrinsicsCalibrator intrCalib;
    const cv::Size imgSize = samples.front().img_left.size();
    auto resL = intrCalib.calibrate(viewsL, board, imgSize);
    auto resR = intrCalib.calibrate(viewsR, board, imgSize);

    std::cout << "[intrinsics left] ok=" << resL.ok << ", rms=" << resL.rms << ", views=" << resL.usedViewIndices.size() << "\n";
    if (resL.ok)
    {
        std::cout << "[intrinsics left] K:\n" << resL.intr.K << "\n";
        std::cout << "[intrinsics left] D:\n" << resL.intr.D << "\n";
    }
    else
    {
        std::cout << "[intrinsics left] reason: " << resL.message << "\n";
    }

    std::cout << "[intrinsics right] ok=" << resR.ok << ", rms=" << resR.rms << ", views=" << resR.usedViewIndices.size() << "\n";
    if (resR.ok)
    {
        std::cout << "[intrinsics right] K:\n" << resR.intr.K << "\n";
        std::cout << "[intrinsics right] D:\n" << resR.intr.D << "\n";
    }
    else
    {
        std::cout << "[intrinsics right] reason: " << resR.message << "\n";
    }

    if (resL.ok && resR.ok)
    {
        StereoCalibrator::Params sp;
        sp.computeRectification = true;
        StereoCalibrator stereo(sp);
        auto sres = stereo.calibrate(viewsL, viewsR, board, resL.intr, resR.intr);

        std::cout << "[stereo] ok=" << sres.ok << ", rms=" << sres.rms << "\n";
        if (sres.ok)
        {
            std::cout << "[stereo] R:\n" << cv::Mat(sres.extr.R_RL) << "\n";
            std::cout << "[stereo] t:\n" << cv::Mat(sres.extr.t_RL) << "\n";
            std::cout << "[stereo] mean Sampson=" << sres.meanSampson << ", median=" << sres.medianSampson << "\n";

            // Hand-eye calibration using left camera
            std::vector<Pose> base_from_flange;
            std::vector<cv::Vec3d> rvec_tc;
            std::vector<cv::Vec3d> tvec_tc;
            base_from_flange.reserve(samples.size());
            rvec_tc.reserve(samples.size());
            tvec_tc.reserve(samples.size());

            for (size_t i = 0; i < samples.size(); ++i)
            {
                cv::Vec3d rvec, tvec;
                if (detector.estimateBoardPose(viewsL[i], resL.intr, rvec, tvec))
                {
                    base_from_flange.push_back(samples[i].robot_pose);
                    rvec_tc.push_back(rvec);
                    tvec_tc.push_back(tvec);
                }
            }

            std::cout << "[handeye] usable pairs: " << base_from_flange.size() << "\n";
            if (base_from_flange.size() >= 2)
            {
                HandEyeCalibrator he;
                auto heres = he.run(base_from_flange, rvec_tc, tvec_tc);
                std::cout << "[handeye] ok=" << heres.ok << "\n";
                if (heres.ok)
                {
                    std::cout << "[handeye] camL<-flange R:\n" << cv::Mat(heres.cam_from_flange.R) << "\n";
                    std::cout << "[handeye] camL<-flange t:\n" << cv::Mat(heres.cam_from_flange.t) << "\n";

                    auto camR_from_flange = HandEyeCalibrator::composeRightFromLeft(sres.extr, heres.cam_from_flange);
                    std::cout << "[handeye] camR<-flange R:\n" << cv::Mat(camR_from_flange.R) << "\n";
                    std::cout << "[handeye] camR<-flange t:\n" << cv::Mat(camR_from_flange.t) << "\n";
                }
                else
                {
                    std::cout << "[handeye] reason: " << heres.message << "\n";
                }
            }
        }
        else
        {
            std::cout << "[stereo] reason: " << sres.message << "\n";
        }
    }

    return 0;
}
