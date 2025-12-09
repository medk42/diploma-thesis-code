#include "robot_stereo_camera_calibration_module.h"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include "module_helpers/serialization_helper/serialization_helper.h"

#include <cstring>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>

using namespace aergo::default_modules::robot_stereo_camera_calibration_module;
using namespace aergo::module;
using json = nlohmann::json;

namespace
{
    constexpr int CHARUCO_ROW_COUNT = 8;
    constexpr int CHARUCO_COL_COUNT = 12;
    constexpr float CHARUCO_SQUARE_LENGTH = 0.024f;
    constexpr float CHARUCO_MARKER_LENGTH = 0.018f;
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

    activation_progress_.store(ActivationProgress::RUNNING(0, static_cast<uint16_t>(samples.size())), std::memory_order_relaxed);

    std::ostringstream board_info;
    board_info << "RobotStereoCameraCalibration: Charuco board requirements - "
               << CHARUCO_ROW_COUNT << " rows x " << CHARUCO_COL_COUNT << " columns, "
               << "square length " << CHARUCO_SQUARE_LENGTH << " m, "
               << "marker length " << CHARUCO_MARKER_LENGTH << " m, "
               << "dictionary DICT_4X4_100, legacy pattern enabled.";
    log(logging::LogType::INFO, board_info.str());

    for (size_t i = 0; i < samples.size(); ++i)
    {
        if (cancel_flag.load(std::memory_order_relaxed))
        {
            cancelled.store(true, std::memory_order_relaxed);
            activation_progress_.store(ActivationProgress::NOT_RUNNING(), std::memory_order_relaxed);
            log(logging::LogType::INFO, "RobotStereoCameraCalibration: activation cancelled.");
            return false;
        }

        ParsedSampleView view;
        if (!parseSample(samples[i], view, i))
        {
            activation_progress_.store(ActivationProgress::NOT_RUNNING(), std::memory_order_relaxed);
            return false;
        }

        if (!validateAndLogSample(view, i))
        {
            activation_progress_.store(ActivationProgress::NOT_RUNNING(), std::memory_order_relaxed);
            return false;
        }

        activation_progress_.store(ActivationProgress::RUNNING(static_cast<uint16_t>(i + 1), static_cast<uint16_t>(samples.size())), std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    activation_progress_.store(ActivationProgress::NOT_RUNNING(), std::memory_order_relaxed);
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
    activation_progress_.store(ActivationProgress::NOT_RUNNING(), std::memory_order_relaxed);
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

    ActivationProgress progress = activation_progress_.load(std::memory_order_relaxed);

    if (!progress.running || progress.total_samples == 0)
    {
        return { .progress_type_ = ProgressType::NONE, .progress_max_int_ = 0, .progress_current_value_double_ = 0.0, .progress_current_value_int_ = 0 };
    }

    return {
        .progress_type_ = ProgressType::INT,
        .progress_max_int_ = progress.total_samples,
        .progress_current_value_double_ = 0.0,
        .progress_current_value_int_ = std::min(progress.total_samples, progress.processed_samples)
    };
}


bool RobotStereoCameraCalibrationModule::parseSample(const std::vector<uint8_t>& raw, ParsedSampleView& out_view, size_t sample_index) noexcept
{
    using aergo::module::helpers::serialization_helper::deserialization::BufferReader;

    BufferReader reader(raw.data(), raw.size());

    uint64_t data_len = 0;
    if (!reader.read<uint64_t>(data_len))
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " malformed (data length).");
        return false;
    }

    if (data_len != sizeof(cph::CameraWithFlangePose))
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " data payload size mismatch for CameraWithFlangePose.");
        return false;
    }

    if (!reader.read<cph::CameraWithFlangePose>(out_view.camera_pose))
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " failed to read CameraWithFlangePose payload.");
        return false;
    }

    uint64_t blob_count = 0;
    if (!reader.read<uint64_t>(blob_count))
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " missing blob count.");
        return false;
    }

    if (blob_count != 1)
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " expected 1 blob, got " + std::to_string(blob_count) + ".");
        return false;
    }

    uint64_t blob_size = 0;
    if (!reader.read<uint64_t>(blob_size) || blob_size == 0)
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " invalid blob size.");
        return false;
    }

    void* blob_ptr = reader.advance(static_cast<size_t>(blob_size));
    if (blob_ptr == nullptr)
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " blob data incomplete.");
        return false;
    }

    out_view.blob_data = static_cast<uint8_t*>(blob_ptr);
    out_view.blob_size = static_cast<size_t>(blob_size);

    return true;
}


bool RobotStereoCameraCalibrationModule::validateAndLogSample(const ParsedSampleView& view, size_t sample_index) noexcept
{
    if (view.blob_data == nullptr || view.blob_size == 0)
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " missing data.");
        return false;
    }

    auto* blob_ptr = const_cast<uint8_t*>(view.blob_data);
    if (!cm::isBlobValid(reinterpret_cast<std::byte*>(blob_ptr), view.blob_size))
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " blob failed validation.");
        return false;
    }

    cm::BlobHeader blob_header;
    cm::ImageHeader left_header;
    cm::ImageHeader right_header;
    if (!cm::readBlobHeader(reinterpret_cast<std::byte*>(blob_ptr), view.blob_size, blob_header))
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " failed to read blob header.");
        return false;
    }

    if (blob_header.image_count_ != 2)
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " is not stereo (image_count=" + std::to_string(blob_header.image_count_) + ").");
        return false;
    }

    if (!cm::readImageHeader(reinterpret_cast<std::byte*>(blob_ptr), view.blob_size, 0, left_header) ||
        !cm::readImageHeader(reinterpret_cast<std::byte*>(blob_ptr), view.blob_size, 1, right_header))
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " failed to read image headers.");
        return false;
    }

    int mat_type = -1;
    switch (blob_header.format_)
    {
        case cm::ImageFormat::BGR8:
            mat_type = CV_8UC3;
            break;
        case cm::ImageFormat::BGRA8:
            mat_type = CV_8UC4;
            break;
        default:
            mat_type = -1;
            break;
    }

    if (mat_type == -1)
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " uses unsupported image format.");
        return false;
    }

    cv::Mat left_img;
    cv::Mat right_img;
    if (!buildImageView(blob_header, left_header, mat_type, view.blob_data, left_img) ||
        !buildImageView(blob_header, right_header, mat_type, view.blob_data, right_img))
    {
        log(logging::LogType::ERROR, "RobotStereoCameraCalibration: sample " + std::to_string(sample_index) + " failed to create image view.");
        return false;
    }

    cv::Scalar left_mean = cv::mean(left_img);
    cv::Scalar right_mean = cv::mean(right_img);

    auto format_to_string = [](cm::ImageFormat fmt) -> std::string
    {
        switch (fmt)
        {
            case cm::ImageFormat::BGR8: return "BGR8";
            case cm::ImageFormat::BGRA8: return "BGRA8";
            default: return "UNKNOWN";
        }
    };

    const auto& pose = view.camera_pose.flange_pose;

    std::ostringstream oss;
    oss << "RobotStereoCameraCalibration: sample #" << sample_index << "\n"
        << "  Timestamp [us]: " << view.camera_pose.camera_header.timestamp_us_ << "\n"
        << "  Pose position: (" << pose.position.x << ", " << pose.position.y << ", " << pose.position.z << ")\n"
        << "  Pose orientation (xyzw): (" << pose.orientation.x << ", " << pose.orientation.y << ", " << pose.orientation.z << ", " << pose.orientation.w << ")\n"
        << "  Blob: size=" << view.blob_size << " bytes, stride=" << blob_header.stride_ << ", format=" << format_to_string(blob_header.format_) << ", image_count=" << blob_header.image_count_ << "\n"
        << "  Left  image: " << left_header.width_ << "x" << left_header.height_ << ", offset=" << left_header.data_offset_ << "\n"
        << "  Right image: " << right_header.width_ << "x" << right_header.height_ << ", offset=" << right_header.data_offset_ << "\n"
        << "  Mean (left): B=" << left_mean[0] << ", G=" << left_mean[1] << ", R=" << left_mean[2];

    if (blob_header.format_ == cm::ImageFormat::BGRA8)
    {
        oss << ", A=" << left_mean[3];
    }

    oss << "\n  Mean (right): B=" << right_mean[0] << ", G=" << right_mean[1] << ", R=" << right_mean[2];

    if (blob_header.format_ == cm::ImageFormat::BGRA8)
    {
        oss << ", A=" << right_mean[3];
    }

    log(logging::LogType::INFO, oss.str());
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
