#include "pen/jig_pose_estimator.h"
#include "pen/pen_board_model.h"
#include "pen/pen_calibration_detector.h"
#include "pen/defaults.h"
#include "calib/pose_utils.h"
#include "calib/types.h"

#include <nlohmann/json.hpp>
#include <opencv2/aruco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace aergo::default_modules::pen_calibration_multicam_module;
using namespace aergo::default_modules::pen_calibration_multicam_module::pen;
namespace pose_utils = aergo::default_modules::pen_calibration_multicam_module::calib::pose_utils;

namespace
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    calib::SE3 identitySE3()
    {
        return { cv::Matx33d::eye(), cv::Vec3d(0.0, 0.0, 0.0) };
    }

    cv::Mat loadK(const json& j, const std::string& key)
    {
        const auto& arr = j.at(key);
        if (!arr.is_array() || arr.size() != 9)
        {
            throw std::runtime_error("Invalid K array for key " + key);
        }
        cv::Mat K(3, 3, CV_64F);
        for (int i = 0; i < 9; ++i) K.at<double>(i / 3, i % 3) = arr[i].get<double>();
        return K;
    }

    cv::Mat loadD(const json& j, const std::string& key)
    {
        const auto& arr = j.at(key);
        if (!arr.is_array() || arr.empty())
        {
            throw std::runtime_error("Invalid D array for key " + key);
        }
        cv::Mat D(1, static_cast<int>(arr.size()), CV_64F);
        for (int i = 0; i < static_cast<int>(arr.size()); ++i) D.at<double>(0, i) = arr[i].get<double>();
        return D;
    }

    calib::SE3 loadSE3(const json& j)
    {
        const auto& Rj = j.at("R");
        const auto& tj = j.at("t");
        if (!Rj.is_array() || Rj.size() != 9 || !tj.is_array() || tj.size() != 3)
        {
            throw std::runtime_error("Invalid SE3 json");
        }

        calib::SE3 T;
        T.R = cv::Matx33d(
            Rj[0].get<double>(), Rj[1].get<double>(), Rj[2].get<double>(),
            Rj[3].get<double>(), Rj[4].get<double>(), Rj[5].get<double>(),
            Rj[6].get<double>(), Rj[7].get<double>(), Rj[8].get<double>());
        T.t = cv::Vec3d(tj[0].get<double>(), tj[1].get<double>(), tj[2].get<double>());
        return T;
    }

    bool loadCamera(const fs::path& json_path,
                    calib::CameraIntrinsics& KL,
                    calib::CameraIntrinsics& KR,
                    calib::SE3& T_R_L)
    {
        std::ifstream ifs(json_path);
        if (!ifs)
        {
            std::cout << "Failed to open " << json_path << std::endl;
            return false;
        }

        json j;
        try
        {
            ifs >> j;
            KL.K = loadK(j, "KL_K");
            KL.D = loadD(j, "KL_D");
            KR.K = loadK(j, "KR_K");
            KR.D = loadD(j, "KR_D");
            const auto sizeL = j.at("KL_size");
            const auto sizeR = j.at("KR_size");
            KL.imageSize = cv::Size(sizeL[0].get<int>(), sizeL[1].get<int>());
            KR.imageSize = cv::Size(sizeR[0].get<int>(), sizeR[1].get<int>());
            T_R_L = loadSE3(j.at("RL"));
        }
        catch (const std::exception& e)
        {
            std::cout << "Failed to parse camera json: " << e.what() << std::endl;
            return false;
        }

        return true;
    }

    struct StereoSample
    {
        cv::Mat left;
        cv::Mat right;
        calib::SE3 T_left_world{identitySE3()};
        calib::SE3 T_right_world{identitySE3()};
    };

    std::vector<StereoSample> loadStereoSamples(const fs::path& images_dir)
    {
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(images_dir))
        {
            if (!entry.is_regular_file()) continue;
            files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        std::vector<StereoSample> samples;
        samples.reserve(files.size());

        for (const auto& p : files)
        {
            cv::Mat img = cv::imread(p.string(), cv::IMREAD_COLOR);
            if (img.empty())
            {
                std::cout << "Skip unreadable image: " << p << std::endl;
                continue;
            }

            if (img.cols != 2560 || img.rows != 800)
            {
                std::cout << "Skip image with unexpected size: " << p << " (" << img.cols << "x" << img.rows << ")\n";
                continue;
            }

            StereoSample s;
            cv::Rect left_roi(0, 0, 1280, 800);
            cv::Rect right_roi(1280, 0, 1280, 800);
            s.left = img(left_roi).clone();
            s.right = img(right_roi).clone();
            samples.push_back(std::move(s));
        }

        return samples;
    }
}

int main()
{
    namespace fs = std::filesystem;

    const fs::path data_dir = fs::current_path() / "data";
    const fs::path images_dir = data_dir / "images";
    const fs::path camera_json_path = data_dir / "camera.json";

    calib::CameraIntrinsics KL, KR;
    calib::SE3 T_R_L = identitySE3();
    if (!loadCamera(camera_json_path, KL, KR, T_R_L))
    {
        return 1;
    }

    auto samples = loadStereoSamples(images_dir);
    std::cout << "Loaded " << samples.size() << " stereo pairs from " << images_dir << std::endl;
    if (samples.empty())
    {
        return 0;
    }

    JigPoseEstimator estimator(defaults::DICTIONARY_ID, defaults::JIG_MARKER_SIZE, defaults::JIG_MARKER_ID);

    // Build pen board model from defaults.
    std::vector<PenBoardModel::MarkerSpec> marker_specs;
    marker_specs.reserve(defaults::getDefaultMarkers().size());
    for (const auto& m : defaults::getDefaultMarkers())
    {
        marker_specs.push_back(PenBoardModel::MarkerSpec{ m.first, m.second });
    }
    PenBoardModel pen_board(marker_specs, defaults::MARKER_SIZE, defaults::PEN_REFERENCE_MARKER_ID, defaults::PEN_TIP_INIT_P, defaults::DICTIONARY_ID);
    PenCalibrationDetector pen_detector(pen_board, defaults::DICTIONARY_ID);

    int jig_success_count = 0;
    int marker_success_count = 0;
    int pen_pose_success_count = 0;

    struct SampleDet
    {
        bool jig_ok{false};
        calib::SE3 T_left_world{identitySE3()};
        calib::SE3 T_right_world{identitySE3()};
        PenCalibrationDetector::Detection det_left;
        PenCalibrationDetector::Detection det_right;
        bool tip_left_valid{false};
        bool tip_right_valid{false};
        cv::Vec3d tip_left_world{0, 0, 0};
        cv::Vec3d tip_right_world{0, 0, 0};
    };

    std::vector<SampleDet> detections(samples.size());
    std::vector<cv::Vec3d> tip_world_points;

    const cv::Vec3d tip_P(defaults::PEN_TIP_INIT_P.x, defaults::PEN_TIP_INIT_P.y, defaults::PEN_TIP_INIT_P.z);

    // Pass 1: detection + pose collection.
    for (size_t i = 0; i < samples.size(); ++i)
    {
        cv::Mat gray_left;
        cv::Mat gray_right;
        cv::cvtColor(samples[i].left, gray_left, cv::COLOR_BGR2GRAY);
        cv::cvtColor(samples[i].right, gray_right, cv::COLOR_BGR2GRAY);

        calib::SE3 T_left_world = identitySE3();
        SampleDet entry;
        entry.jig_ok = estimator.estimate(gray_left, KL, T_left_world);
        if (entry.jig_ok)
        {
            samples[i].T_left_world = T_left_world;
            samples[i].T_right_world = pose_utils::compose(T_R_L, T_left_world);
            entry.T_left_world = samples[i].T_left_world;
            entry.T_right_world = samples[i].T_right_world;
            ++jig_success_count;
        }
        else
        {
            std::cout << "Pose estimation failed for sample " << i << std::endl;
        }

        entry.det_left = pen_detector.detect(gray_left, KL);
        entry.det_right = pen_detector.detect(gray_right, KR);

        if (!entry.det_left.ok)
        {
            std::cout << "Left pen detection failed for sample " << i << ": " << entry.det_left.msg << std::endl;
        }
        if (!entry.det_right.ok)
        {
            std::cout << "Right pen detection failed for sample " << i << ": " << entry.det_right.msg << std::endl;
        }

        bool any_markers_left = !entry.det_left.markerIds.empty();
        bool any_markers_right = !entry.det_right.markerIds.empty();
        if (any_markers_left) ++marker_success_count;
        if (any_markers_right) ++marker_success_count;

        if (entry.jig_ok && entry.det_left.ok)
        {
            auto T_world_pen = pose_utils::compose(entry.T_left_world, entry.det_left.T_camera_pen);
            entry.tip_left_world = T_world_pen.R * tip_P + T_world_pen.t;
            entry.tip_left_valid = true;
            tip_world_points.push_back(entry.tip_left_world);
        }
        if (entry.jig_ok && entry.det_right.ok)
        {
            auto T_world_pen = pose_utils::compose(entry.T_right_world, entry.det_right.T_camera_pen);
            entry.tip_right_world = T_world_pen.R * tip_P + T_world_pen.t;
            entry.tip_right_valid = true;
            tip_world_points.push_back(entry.tip_right_world);
        }

        detections[i] = entry;
    }

    // Median tip position.
    cv::Vec3d median_tip{0, 0, 0};
    if (!tip_world_points.empty())
    {
        std::vector<double> xs, ys, zs;
        xs.reserve(tip_world_points.size());
        ys.reserve(tip_world_points.size());
        zs.reserve(tip_world_points.size());
        for (const auto& p : tip_world_points)
        {
            xs.push_back(p[0]);
            ys.push_back(p[1]);
            zs.push_back(p[2]);
        }
        auto median_of = [](std::vector<double>& v) -> double
        {
            std::sort(v.begin(), v.end());
            size_t mid = v.size() / 2;
            if (v.size() % 2 == 0)
            {
                return 0.5 * (v[mid - 1] + v[mid]);
            }
            return v[mid];
        };
        median_tip[0] = median_of(xs);
        median_tip[1] = median_of(ys);
        median_tip[2] = median_of(zs);
    }

    const double max_dist = 0.03; // 3cm

    auto drawMarkers = [](cv::Mat& img, const PenCalibrationDetector::Detection& det)
    {
        for (size_t k = 0; k < det.markerIds.size(); ++k)
        {
            const auto& corners = det.markerCorners[k];
            if (corners.size() == 4)
            {
                for (size_t c = 0; c < 4; ++c)
                {
                    cv::line(img, corners[c], corners[(c + 1) % 4], cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
                }
                cv::Point2f label_pt = corners[0];
                cv::putText(img, std::to_string(det.markerIds[k]), label_pt, cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
            }
        }
    };

    auto drawPoseAt = [&](const calib::SE3& T_cam_pen, cv::Mat& img, const calib::CameraIntrinsics& K_cam, const cv::Vec3d& tip_cam_P)
    {
        cv::Vec3d rvec, tvec;
        pose_utils::se3ToRt(T_cam_pen, rvec, tvec);
        cv::drawFrameAxes(img, K_cam.K, K_cam.D, rvec, tvec, 0.05);

        cv::Vec3d tip_cam = T_cam_pen.R * tip_cam_P + T_cam_pen.t;
        cv::drawFrameAxes(img, K_cam.K, K_cam.D, rvec, tip_cam, 0.03);
    };

    // Pass 2: visualization with filtering/selection.
    for (size_t i = 0; i < samples.size(); ++i)
    {
        auto& entry = detections[i];
        cv::Mat left_vis = samples[i].left.clone();
        cv::Mat right_vis = samples[i].right.clone();

        drawMarkers(left_vis, entry.det_left);
        drawMarkers(right_vis, entry.det_right);

        // Jig overlay if available.
        if (entry.jig_ok)
        {
            cv::Vec3d jig_rvecL, jig_tvecL, jig_rvecR, jig_tvecR;
            pose_utils::se3ToRt(entry.T_left_world, jig_rvecL, jig_tvecL);
            pose_utils::se3ToRt(entry.T_right_world, jig_rvecR, jig_tvecR);
            cv::drawFrameAxes(left_vis, KL.K, KL.D, jig_rvecL, jig_tvecL, 0.05);
            cv::drawFrameAxes(right_vis, KR.K, KR.D, jig_rvecR, jig_tvecR, 0.05);

            const float half = static_cast<float>(defaults::JIG_MARKER_SIZE * 0.5);
            std::vector<cv::Point3f> markerObj = {
                {-half,  half, 0.0f},
                { half,  half, 0.0f},
                { half, -half, 0.0f},
                {-half, -half, 0.0f}
            };

            std::vector<cv::Point2f> projL, projR;
            cv::projectPoints(markerObj, jig_rvecL, jig_tvecL, KL.K, KL.D, projL);
            cv::projectPoints(markerObj, jig_rvecR, jig_tvecR, KR.K, KR.D, projR);
            if (projL.size() == 4)
            {
                for (size_t k = 0; k < 4; ++k)
                {
                    cv::line(left_vis, projL[k], projL[(k + 1) % 4], cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
                }
            }
            if (projR.size() == 4)
            {
                for (size_t k = 0; k < 4; ++k)
                {
                    cv::line(right_vis, projR[k], projR[(k + 1) % 4], cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
                }
            }
        }

        struct Candidate
        {
            bool is_left{false};
            PenCalibrationDetector::Detection det;
            calib::SE3 T_world_pen;
            cv::Vec3d tip_world;
            double dist_to_median{0.0};
        };
        std::vector<Candidate> candidates;

        auto add_candidate = [&](bool is_left)
        {
            const auto& det = is_left ? entry.det_left : entry.det_right;
            const bool tip_valid = is_left ? entry.tip_left_valid : entry.tip_right_valid;
            if (!entry.jig_ok || !det.ok || !tip_valid)
            {
                return;
            }

            const auto& T_world_cam = is_left ? entry.T_left_world : entry.T_right_world;
            auto T_world_pen = pose_utils::compose(T_world_cam, det.T_camera_pen);
            const cv::Vec3d tip_world = is_left ? entry.tip_left_world : entry.tip_right_world;

            const double dx = tip_world[0] - median_tip[0];
            const double dy = tip_world[1] - median_tip[1];
            const double dz = tip_world[2] - median_tip[2];
            const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist > max_dist)
            {
                return;
            }

            candidates.push_back(Candidate{is_left, det, T_world_pen, tip_world, dist});
        };

        add_candidate(true);
        add_candidate(false);

        Candidate* chosen = nullptr;
        if (candidates.size() == 1)
        {
            chosen = &candidates[0];
        }
        else if (candidates.size() == 2)
        {
            const size_t c0 = candidates[0].det.markerIds.size();
            const size_t c1 = candidates[1].det.markerIds.size();
            if (c0 > c1)
            {
                chosen = &candidates[0];
            }
            else if (c1 > c0)
            {
                chosen = &candidates[1];
            }
            else
            {
                chosen = (candidates[0].dist_to_median <= candidates[1].dist_to_median) ? &candidates[0] : &candidates[1];
            }
        }

        if (chosen)
        {
            calib::SE3 T_world_pen = chosen->T_world_pen;

            auto T_left_pen = pose_utils::compose(pose_utils::invert(entry.T_left_world), T_world_pen);
            auto T_right_pen = pose_utils::compose(pose_utils::invert(entry.T_right_world), T_world_pen);

            drawPoseAt(T_left_pen, left_vis, KL, tip_P);
            drawPoseAt(T_right_pen, right_vis, KR, tip_P);

            ++pen_pose_success_count;
        }

        cv::imshow("Left", left_vis);
        cv::imshow("Right", right_vis);
        cv::waitKey(0);
    }

    std::cout << "Jig successes: " << jig_success_count << " / " << samples.size() << std::endl;
    std::cout << "Marker detections (left+right combined): " << marker_success_count << " (max " << samples.size() * 2 << ")" << std::endl;
    std::cout << "Pen pose successes (filtered, combined): " << pen_pose_success_count << " (max " << samples.size() << ")" << std::endl;
    return 0;
}
