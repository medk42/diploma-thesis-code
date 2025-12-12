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

#include <cstring>
#include <sstream>
#include <algorithm>

using namespace aergo::default_modules::robot_stereo_camera_calibration_module;
using namespace aergo::module;
using json = nlohmann::json;

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
    uint32_t /*subscribe_consumer_id*/,
    aergo::module::ChannelIdentifier /*source_channel*/,
    aergo::module::message::MessageHeader /*message*/) noexcept
{
    // Message passthrough will be added after calibration results are produced.
}


bool RobotStereoCameraCalibrationModule::activate(
    std::vector<std::vector<std::vector<uint8_t>>>& parameter_values,
    const std::atomic<bool>& cancel_flag,
    std::atomic<bool>& cancelled)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (activated_)
    {
        log(logging::LogType::WARNING, "RobotStereoCameraCalibration: already activated.");
        return true;
    }

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

    std::ostringstream board_info;
    board_info << "RobotStereoCameraCalibration: ChArUco board requirements - "
               << kBoardParams.rows << " rows x " << kBoardParams.cols << " columns, "
               << "square length " << kBoardParams.squareLength << " m, "
               << "marker length " << kBoardParams.markerLength << " m, "
               << "dictionary id " << kBoardParams.dictionary << ", legacy pattern "
               << (kBoardParams.useLegacyPattern ? "enabled" : "disabled") << ".";
    log(logging::LogType::INFO, board_info.str());

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

    log(logging::LogType::INFO, "RobotStereoCameraCalibration: parsed " + std::to_string(left_images.size()) + " stereo samples.");
    activated_ = true;
    return true;
}


bool RobotStereoCameraCalibrationModule::deactivate(const std::atomic<bool>& /*cancel_flag*/, std::atomic<bool>& /*cancelled*/)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!activated_)
    {
        log(logging::LogType::WARNING, "RobotStereoCameraCalibration: already deactivated.");
        return true;
    }

    activated_ = false;
    return true;
}


bool RobotStereoCameraCalibrationModule::isActivated()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return activated_;
}


aergo::module::ISerializableModule::SaveData RobotStereoCameraCalibrationModule::save() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    ISerializableModule::SaveData data;
    data.success_ = true;
    data.supports_saving_ = true;
    data.schema_version_ = 1;

    json j;
    j["activated"] = activated_;
    data.json_header_ = j.dump();

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

    try
    {
        json j = json::parse(data.json_header_);
        if (!j.contains("activated") || !j["activated"].is_boolean())
        {
            log(logging::LogType::ERROR, "RobotStereoCameraCalibration: invalid save data payload.");
            return false;
        }

        activated_ = j["activated"].get<bool>();
    }
    catch (const std::exception& e)
    {
        log(logging::LogType::ERROR, std::string("RobotStereoCameraCalibration: failed to parse save data: ") + e.what());
        return false;
    }

    return true;
}


aergo::module::helpers::activation_wrapper::message_types::ProgressData RobotStereoCameraCalibrationModule::getActivationProgress()
{
    using ProgressData = aergo::module::helpers::activation_wrapper::message_types::ProgressData;
    using ProgressType = aergo::module::helpers::activation_wrapper::message_types::ProgressType;

    return { .progress_type_ = ProgressType::NONE, .progress_max_int_ = 0, .progress_current_value_double_ = 0.0, .progress_current_value_int_ = 0 };
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
