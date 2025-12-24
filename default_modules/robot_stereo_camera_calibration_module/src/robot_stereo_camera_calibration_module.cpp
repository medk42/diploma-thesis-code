#include "robot_stereo_camera_calibration_module.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include "module_helpers/serialization_helper/serialization_helper.h"

#include "calib/charuco_board_model.h"
#include "calib/charuco_defaults.h"
#include "calib/charuco_detector.h"
#include "calib/intrinsics_calibrator.h"
#include "calib/stereo_calibrator.h"
#include "calib/handeye_calibrator.h"
#include "calib/rig_refiner_ceres.h"
#include "calib/pose_utils.h"
#include "module_helpers/calibrated_camera_robot_messages/message_types.h"

#include <cstring>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>

using namespace aergo::default_modules::robot_stereo_camera_calibration_module;
using namespace aergo::module;
using json = nlohmann::json;
namespace messages = aergo::module::helpers::calibrated_camera_robot_messages;

namespace
{
    using namespace aergo::default_modules::robot_stereo_camera_calibration_module::calib;

    // Default parameter bundle mirrored from tests/main.cpp for consistency.
    const CharucoBoardModel::Params kBoardParams{
        .rows = defaults::charucoboard::ROW_COUNT,
        .cols = defaults::charucoboard::COL_COUNT,
        .squareLength = defaults::charucoboard::SQUARE_LENGTH,
        .markerLength = defaults::charucoboard::MARKER_LENGTH,
        .dictionary = cv::aruco::DICT_4X4_100,
        .useLegacyPattern = defaults::charucoboard::LEGACY_PATTERN
    };

    const CharucoDetector::Params kDetParams{
        .adaptiveWinMin = 3,
        .adaptiveWinMax = 23,
        .adaptiveWinStep = 10,
        .minMarkerPerimeterRate = 0.02,
        .refineSubpix = true,
        .subpixWin = cv::Size(5, 5),
        .subpixMaxIters = 50,
        .subpixEps = 0.01,
        .minCharucoCorners = 12,
        .minArucoMarkers = 4
    };

    const IntrinsicsCalibrator::Params kIntrParams{
        .minCharucoCornersPerView = 12,
        .minViews = 8,
        .criteria = cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 50, 1e-9)
    };

    const StereoCalibrator::Params kStereoParams{
        .minSharedCharucoCorners = 10,
        .minPairs = 8,
        .fixIntrinsics = true
    };

    const HandEyeCalibrator::Params kHandEyeParams{
        .method = cv::CALIB_HAND_EYE_TSAI,
        .minPairs = 8
    };

    const RigRefinerCeres::Options kRefineOpts{
        .refineStereo = true,
        .refineHandEye = true,
        .estimateBoardInWorld = true,
        .maxIters = 50,
        .huberDelta = 1.0
    };
}


RobotStereoCameraCalibrationModule::RobotStereoCameraCalibrationModule(
    const char* data_path,
    aergo::module::ICore* core,
    aergo::module::InputChannelMapInfo channel_map_info,
    const aergo::module::logging::ILogger* logger,
    uint64_t module_id,
    const aergo::module::ModuleInfo* module_info)
: aergo::module::BaseModule(data_path, core, channel_map_info, logger, module_id, module_info)
{
    if (!getSubscribeChannelByName(cph::camera_with_pose_publish_producer.channel_type_identifier_, camera_pose_input_channel_))
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: failed to resolve camera+pose subscribe channel.");
        return;
    }

    if (!getPublishChannelByName(messages::calibrated_camera_publish_producer.channel_type_identifier_, calibrated_publish_channel_))
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: failed to resolve calibrated stereo publish channel.");
        return;
    }

    valid_ = true;
}


void* RobotStereoCameraCalibrationModule::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    if (id == typeid(helpers::activation_wrapper::IActivableModule)) return static_cast<helpers::activation_wrapper::IActivableModule*>(this);
    return nullptr;
}


IModule::IngressDecision RobotStereoCameraCalibrationModule::onIngress(
    ProcessingType kind,
    uint32_t local_channel_id,
    aergo::module::ChannelIdentifier /*src*/,
    const aergo::module::message::MessageHeader& /*msg*/,
    QueueStatus queue_status) noexcept
{
    if (kind == ProcessingType::MESSAGE && local_channel_id == camera_pose_input_channel_)
    {
        if (queue_status == QueueStatus::QUEUE_FULL)
        {
            return IngressDecision::DROP;
        }
        return IngressDecision::ACCEPT_REPLACE_QUEUE;
    }

    return IngressDecision::DROP;
}


void RobotStereoCameraCalibrationModule::processMessage(
    uint32_t subscribe_consumer_id,
    aergo::module::ChannelIdentifier /*source_channel*/,
    aergo::module::message::MessageHeader message) noexcept
{
    if (subscribe_consumer_id != camera_pose_input_channel_)
    {
        log(logging::LogType::WARNING, "RobotStereoCameraCalibration: received message on unexpected subscribe consumer.");
        return;
    }

    cph::CameraWithFlangePose payload{};
    if (!message.readAs(payload))
    {
        log(logging::LogType::WARNING, "RobotStereoCameraCalibration: received message without CameraWithFlangePose payload.");
        return;
    }

    calib::StereoRigWorldCameraPoses world_poses;
    {
        std::lock_guard<std::mutex> c_lock(calibrator_mutex_);
        if (!calibrator_)
        {
            return;
        }
        world_poses = calibrator_->computeWorld(payload.flange_pose);
    }

    messages::CalibratedCameraSet out_msg{};
    out_msg.camera_header = payload.camera_header;
    out_msg.calibrated_count = 2;
    if (!messages::addCalibData(out_msg, 0, world_poses.pose_left, world_poses.intrinsics_left.K, world_poses.intrinsics_left.D) ||
        !messages::addCalibData(out_msg, 1, world_poses.pose_right, world_poses.intrinsics_right.K, world_poses.intrinsics_right.D))
    {
        log(logging::LogType::WARNING, "RobotStereoCameraCalibration: failed to pack calibrated message (invalid intrinsics).");
        return;
    }

    auto out_header = aergo::module::message::MessageHeader::Message(&out_msg, message.blobs_, message.blob_count_);
    sendMessage(calibrated_publish_channel_, out_header);
}


bool RobotStereoCameraCalibrationModule::activate(
    std::vector<std::vector<std::vector<uint8_t>>>& parameter_values,
    const std::atomic<bool>& cancel_flag,
    std::atomic<bool>& cancelled)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (parameter_values.size() != 1)
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: expected exactly one activation parameter (stereo camera+pose samples).");
        return false;
    }

    const auto& samples = parameter_values[0];
    if (samples.size() < 10)
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: need at least 10 stereo camera+pose samples for activation.");
        return false;
    }

    std::vector<cv::Mat> left_images;
    std::vector<cv::Mat> right_images;
    std::vector<Pose> poses;
    if (!parseStereoSamples(samples, cancel_flag, cancelled, left_images, right_images, poses))
    {
        return false;
    }

    if (cancel_flag.load(std::memory_order_relaxed))
    {
        cancelled.store(true, std::memory_order_relaxed);
        log(logging::LogType::INFO, "RobotStereoCameraCalibration: activation cancelled.");
        return false;
    }

    // Instantiate calibrator under its own mutex to avoid concurrent activation.
    {
        std::lock_guard<std::mutex> c_lock(calibrator_mutex_);
        if (calibrator_)
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: calibration already running or completed; create a new module instance.");
            return false;
        }
        calibrator_ = std::make_unique<calib::StereoRigCalibrator>();
    }

    std::atomic<bool> stop_thread{false};
    std::thread cancel_thread([&]()
    {
        while (!stop_thread.load(std::memory_order_relaxed))
        {
            if (cancel_flag.load(std::memory_order_relaxed))
            {
                if (calibrator_)
                {
                    calibrator_->cancel();
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    auto res = calibrator_->runStereoRobotCalibration(
        kBoardParams, kDetParams, kIntrParams, kStereoParams, kHandEyeParams, kRefineOpts,
        left_images, right_images, poses);

    stop_thread.store(true, std::memory_order_relaxed);
    if (cancel_thread.joinable())
    {
        cancel_thread.join();
    }

    if (!res.has_value())
    {
        {
            std::lock_guard<std::mutex> c_lock(calibrator_mutex_);
            calibrator_.reset();
        }

        if (cancel_flag.load(std::memory_order_relaxed))
        {
            cancelled.store(true, std::memory_order_relaxed);
        }

        std::ostringstream oss_fail;
        oss_fail << "RobotStereoCameraCalibration: calibration failed: " << res.error()
                 << "\n      RobotStereoCameraCalibration: ChArUco board requirements - "
                << kBoardParams.rows << " rows x " << kBoardParams.cols << " columns, "
                << "square length " << kBoardParams.squareLength << " m, "
                << "marker length " << kBoardParams.markerLength << " m, "
                << "dictionary id " << kBoardParams.dictionary << ", legacy pattern "
                << (kBoardParams.useLegacyPattern ? "enabled" : "disabled") << ".";
                
        log(logging::LogType::ERROR, oss_fail.str());
        return false;
    }

    std::ostringstream oss;
    {
        std::lock_guard<std::mutex> c_lock(calibrator_mutex_);
        if (!calibrator_)
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: internal error - calibrator missing after successful run.");
            return false;
        }
        const auto& m = calibrator_->report();
        oss << "Calibration succeeded\n";
        oss << "      Intrinsics L RMS: " << m.intrinsics_left_rms << " (views " << m.intrinsics_left_used_views << ")\n";
        oss << "      Intrinsics R RMS: " << m.intrinsics_right_rms << " (views " << m.intrinsics_right_used_views << ")\n";
        oss << "      Stereo RMS: " << m.stereo_rms << " (pairs " << m.stereo_used_pairs << ")\n";
        oss << "      Stereo Sampson mean/median: " << m.stereo_mean_sampson << " / " << m.stereo_median_sampson << "\n";
        oss << "      Hand-eye usable pairs: " << m.hand_eye_usable_pairs << "\n";
        oss << "      Refine RMSE L (init/final): " << m.refine_initial_reproj_rmse_l << " / " << m.refine_final_reproj_rmse_l << "\n";
        oss << "      Refine RMSE R (init/final): " << m.refine_initial_reproj_rmse_r << " / " << m.refine_final_reproj_rmse_r << "\n";

        const auto& t_RL = m.stereo_extrinsics.t_RL;
        auto q_RL = calib::pose_utils::rToQuat(m.stereo_extrinsics.R_RL);
        oss << "      Stereo t_RL (m): [" << t_RL(0) << ", " << t_RL(1) << ", " << t_RL(2) << "]\n";
        oss << "      Stereo q_RL (xyzw): [" << q_RL.x << ", " << q_RL.y << ", " << q_RL.z << ", " << q_RL.w << "]\n";

        const auto dumpMat = [](const cv::Mat& M) -> std::string {
            std::ostringstream s;
            s << "[";
            for (int r = 0; r < M.rows; ++r)
            {
                if (r > 0) s << "; ";
                const double* row = M.ptr<double>(r);
                for (int c = 0; c < M.cols; ++c)
                {
                    if (c > 0) s << ", ";
                    s << row[c];
                }
            }
            s << "]";
            return s.str();
        };

        oss << "      K_left: " << dumpMat(m.camera_intrinsics_left.K) << "\n";
        oss << "      D_left: " << dumpMat(m.camera_intrinsics_left.D) << "\n";
        oss << "      K_right: " << dumpMat(m.camera_intrinsics_right.K) << "\n";
        oss << "      D_right: " << dumpMat(m.camera_intrinsics_right.D) << "\n";

        const auto& t_FL = m.camL_from_flange.t;
        auto q_FL = calib::pose_utils::rToQuat(m.camL_from_flange.R);
        oss << "      camL_from_flange t (m): [" << t_FL(0) << ", " << t_FL(1) << ", " << t_FL(2) << "]\n";
        oss << "      camL_from_flange q (xyzw): [" << q_FL.x << ", " << q_FL.y << ", " << q_FL.z << ", " << q_FL.w << "]\n";
        oss << "      Refinement message: " << m.refine_message << "\n";
    }
    log(logging::LogType::INFO, oss.str());
    
    return true;
}


bool RobotStereoCameraCalibrationModule::deactivate(const std::atomic<bool>& /*cancel_flag*/, std::atomic<bool>& /*cancelled*/)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::lock_guard<std::mutex> c_lock(calibrator_mutex_);
    if (calibrator_ && !calibrator_->valid())
    {
        log(logging::LogType::WARNING, "RobotStereoCameraCalibration: unable to deactivate - calibration still running.");
        return false;
    }
    
    calibrator_.reset();
    return true;
}


bool RobotStereoCameraCalibrationModule::isActivated()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::lock_guard<std::mutex> c_lock(calibrator_mutex_);
    return calibrator_ != nullptr;
}


aergo::module::ISerializableModule::SaveData RobotStereoCameraCalibrationModule::save() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    ISerializableModule::SaveData data;
    data.supports_saving_ = true;
    data.schema_version_ = 1;

    std::lock_guard<std::mutex> c_lock(calibrator_mutex_);
    if (!calibrator_)
    {
        data.success_ = true;
        json j;
        j["calibrator_present"] = false;
        data.json_header_ = j.dump();
        return data;
    }

    if (!calibrator_->valid())
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: save failed because calibration is not valid.");
        data.success_ = false;
        return data;
    }

    json j;
    j["calibrator_present"] = true;
    j["calibration"] = calibrator_->saveJson();
    data.json_header_ = j.dump();
    data.success_ = true;
    return data;
}


bool RobotStereoCameraCalibrationModule::load(aergo::module::ISerializableModule::SaveData data) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!data.supports_saving_ || data.schema_version_ != 1)
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: unsupported save data.");
        return false;
    }

    json j;
    try
    {
        j = json::parse(data.json_header_);
    }
    catch (const std::exception& e)
    {
        log(logging::LogType::ERROR, std::string("RobotStereoCameraCalibration: failed to parse save data: ") + e.what());
        std::lock_guard<std::mutex> c_lock(calibrator_mutex_);
        calibrator_.reset();
        return false;
    }

    std::lock_guard<std::mutex> c_lock(calibrator_mutex_);
    calibrator_.reset();

    const bool present = j.value("calibrator_present", false);
    if (!present)
    {
        return true;
    }

    if (!j.contains("calibration"))
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: save data missing calibration payload.");
        return false;
    }

    auto calib_ptr = std::make_unique<calib::StereoRigCalibrator>();
    auto res = calib_ptr->loadJson(j["calibration"]);
    if (!res.has_value())
    {
        log(logging::LogType::ERROR, std::string("RobotStereoCameraCalibration: failed to load calibration: ") + res.error());
        return false;
    }

    calibrator_ = std::move(calib_ptr);
    return true;
}


aergo::module::helpers::activation_wrapper::message_types::ProgressData RobotStereoCameraCalibrationModule::getActivationProgress()
{
    using ProgressData = aergo::module::helpers::activation_wrapper::message_types::ProgressData;
    using ProgressType = aergo::module::helpers::activation_wrapper::message_types::ProgressType;

    std::lock_guard<std::mutex> c_lock(calibrator_mutex_);
    if (!calibrator_)
    {
        return { .progress_type_ = ProgressType::NONE, .progress_max_int_ = 0, .progress_current_value_double_ = 0.0, .progress_current_value_int_ = 0 };
    }

    auto prog = calibrator_->progress();
    if (prog.max_progress == 0)
    {
        return { .progress_type_ = ProgressType::NONE, .progress_max_int_ = 0, .progress_current_value_double_ = 0.0, .progress_current_value_int_ = 0 };
    }

    return {
        .progress_type_ = ProgressType::INT,
        .progress_max_int_ = prog.max_progress,
        .progress_current_value_double_ = 0.0,
        .progress_current_value_int_ = prog.current_progress
    };
}


bool RobotStereoCameraCalibrationModule::parseStereoSamples(
    const std::vector<std::vector<uint8_t>>& samples,
    const std::atomic<bool>& cancel_flag,
    std::atomic<bool>& cancelled,
    std::vector<cv::Mat>& left_images,
    std::vector<cv::Mat>& right_images,
    std::vector<Pose>& poses) noexcept
{
    using aergo::module::helpers::serialization_helper::deserialization::BufferReader;

    left_images.clear();
    right_images.clear();
    poses.clear();

    left_images.reserve(samples.size());
    right_images.reserve(samples.size());
    poses.reserve(samples.size());

    for (size_t i = 0; i < samples.size(); ++i)
    {
        if (cancel_flag.load(std::memory_order_relaxed))
        {
            cancelled.store(true, std::memory_order_relaxed);
            log(logging::LogType::INFO, "RobotStereoCameraCalibration: activation cancelled.");
            return false;
        }

        const auto& raw = samples[i];
        BufferReader reader(raw.data(), raw.size());

        uint64_t data_len = 0;
        if (!reader.read<uint64_t>(data_len) || data_len != sizeof(cph::CameraWithFlangePose))
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(i) + " malformed (CameraWithFlangePose length mismatch).");
            return false;
        }

        cph::CameraWithFlangePose camera_pose{};
        if (!reader.read<cph::CameraWithFlangePose>(camera_pose))
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(i) + " failed to read CameraWithFlangePose payload.");
            return false;
        }

        uint64_t blob_count = 0;
        if (!reader.read<uint64_t>(blob_count) || blob_count != 1)
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(i) + " expected 1 blob, got " + std::to_string(blob_count) + ".");
            return false;
        }

        uint64_t blob_size = 0;
        if (!reader.read<uint64_t>(blob_size) || blob_size == 0)
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(i) + " invalid blob size.");
            return false;
        }

        void* blob_ptr = reader.advance(static_cast<size_t>(blob_size));
        if (blob_ptr == nullptr)
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(i) + " blob data incomplete.");
            return false;
        }

        auto* blob_bytes = reinterpret_cast<std::byte*>(blob_ptr);
        if (!cm::isBlobValid(blob_bytes, blob_size))
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(i) + " blob failed validation.");
            return false;
        }

        cm::BlobHeader blob_header{};
        cm::ImageHeader left_header{};
        cm::ImageHeader right_header{};
        if (!cm::readBlobHeader(blob_bytes, blob_size, blob_header))
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(i) + " failed to read blob header.");
            return false;
        }

        if (blob_header.image_count_ != 2)
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(i) + " is not stereo (image_count=" + std::to_string(blob_header.image_count_) + ").");
            return false;
        }

        if (!cm::readImageHeader(blob_bytes, blob_size, 0, left_header) ||
            !cm::readImageHeader(blob_bytes, blob_size, 1, right_header))
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(i) + " failed to read image headers.");
            return false;
        }

        int mat_type = -1;
        switch (blob_header.format_)
        {
            case cm::ImageFormat::BGR8: mat_type = CV_8UC3; break;
            case cm::ImageFormat::BGRA8: mat_type = CV_8UC4; break;
            default: mat_type = -1; break;
        }

        if (mat_type == -1)
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(i) + " uses unsupported image format.");
            return false;
        }

        cv::Mat left_img;
        cv::Mat right_img;
        if (!buildImageView(blob_header, left_header, mat_type, reinterpret_cast<uint8_t*>(blob_ptr), left_img) ||
            !buildImageView(blob_header, right_header, mat_type, reinterpret_cast<uint8_t*>(blob_ptr), right_img))
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(i) + " failed to create image view.");
            return false;
        }

        left_images.push_back(left_img);
        right_images.push_back(right_img);
        poses.push_back(camera_pose.flange_pose);
    }

    return true;

    // This is fallback for testing when flange poses are not available; not used in normal operation.
    // Try to infer poses from images for offline testing (non-fatal).
    if (!computePosesFromLeftImages(left_images, poses))
    {
        log(logging::LogType::WARNING, "RobotStereoCameraCalibration: fallback to provided flange poses (image-based pose estimation failed).");
    }

    return true;
}

bool RobotStereoCameraCalibrationModule::computePosesFromLeftImages(const std::vector<cv::Mat>& left_images,
                                                                    std::vector<Pose>& poses) noexcept
{
    if (left_images.empty() || poses.size() != left_images.size())
    {
        return false;
    }

    auto board = calib::CharucoBoardModel::Create(kBoardParams);
    calib::CharucoDetector detector(board, kDetParams);

    std::vector<calib::CharucoDetector::Result> views;
    views.reserve(left_images.size());
    for (const auto& img : left_images)
    {
        views.push_back(detector.detect(img));
    }

    calib::IntrinsicsCalibrator intrCalib(kIntrParams);
    auto intrRes = intrCalib.calibrate(views, board, left_images.front().size());
    if (!intrRes.ok)
    {
        return false;
    }

    // Known transform: flange <- camL (cam expressed in flange frame): translation + 20 deg about X).
    const double deg_to_rad = 3.14159265358979323846 / 180.0;
    const double angle = 20.0 * deg_to_rad;
    cv::Matx33d R_flange_cam = {
        1, 0, 0,
        0, std::cos(angle), -std::sin(angle),
        0, std::sin(angle),  std::cos(angle)
    };
    cv::Vec3d t_flange_cam(-0.03, 0.15, 0.02);

    calib::SE3 T_flange_from_cam{R_flange_cam, t_flange_cam};

    for (size_t i = 0; i < views.size(); ++i)
    {
        cv::Vec3d rvec, tvec;
        if (!detector.estimateBoardPose(views[i], intrRes.intr, rvec, tvec))
        {
            log(logging::LogType::WARNING, "RobotStereoCameraCalibration: pose estimation failed for left image " + std::to_string(i) + "; skipping.");
            continue;
        }

        auto T_cam_from_board = calib::pose_utils::rtToSE3(rvec, tvec);
        auto T_flange_from_board = calib::pose_utils::compose(T_flange_from_cam, T_cam_from_board);
        auto T_world_from_flange = calib::pose_utils::invert(T_flange_from_board);
        poses[i] = calib::pose_utils::toPose(T_world_from_flange);
    }

    return true;
}


bool RobotStereoCameraCalibrationModule::buildImageView(const cm::BlobHeader& blob_header, const cm::ImageHeader& img_header, int mat_type, const uint8_t* blob_data, cv::Mat& out_mat) noexcept
{
    if (img_header.height_ == 0 || img_header.width_ == 0)
    {
        return false;
    }

    const uint8_t* image_ptr = blob_data + img_header.data_offset_;
    out_mat = cv::Mat(static_cast<int>(img_header.height_), static_cast<int>(img_header.width_), mat_type, const_cast<uint8_t*>(image_ptr), blob_header.stride_);
    return out_mat.data != nullptr;
}
