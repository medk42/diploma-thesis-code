#include "camera_calibration_module.h"

#include "message_structure.h"

#include <filesystem>



using namespace aergo::default_modules::camera_calibration_module;
using namespace aergo::module;



CameraCalibrationModule::CameraCalibrationModule(const char* data_path, aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::logging::ILogger* logger, uint64_t module_id)
: aergo::module::BaseModule(data_path, core, channel_map_info, logger, module_id), valid_(false)
{
    aergo::module::InputChannelMapInfo::IndividualChannelInfo image_input_info = getSubscribeChannelInfo(0); // image data input
    if (image_input_info.channel_identifier_count_ != 1)
    {
        log(aergo::module::logging::LogType::ERROR, "Camera calibration module requires exactly one input channel for image data");
        return;
    }

    if (!loadCameraCalibration())
    {
        return;
    }

    valid_ = true;
}



void* CameraCalibrationModule::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    if (id == typeid(helpers::activation_wrapper::IActivableModule)) return static_cast<helpers::activation_wrapper::IActivableModule*>(this);
    return nullptr;
}



IModule::IngressDecision CameraCalibrationModule::onIngress(ProcessingType kind, uint32_t local_channel_id, aergo::module::ChannelIdentifier src, const aergo::module::message::MessageHeader& msg, QueueStatus queue_status) noexcept
{
    // accept only messages on subscribe channel 0 (image data), drop all others
    if (kind == ProcessingType::MESSAGE && local_channel_id == 0)
    {
        return IngressDecision::ACCEPT_REPLACE_QUEUE; // only keep the latest image
    }
    return IngressDecision::DROP;
}



void CameraCalibrationModule::processMessage(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept
{
    if (subscribe_consumer_id != 0)
    {
        log(aergo::module::logging::LogType::WARNING, "Camera calibration module received message on invalid subscribe channel, dropping");
        return;
    }

    if (message.data_ == nullptr || message.data_len_ != sizeof(ImageHeader))
    {
        log(aergo::module::logging::LogType::WARNING, "Camera calibration module received invalid image message, dropping");
        return;
    }

    ImageHeader* img_header = reinterpret_cast<ImageHeader*>(message.data_);
    if (message.blob_count_ != 1 || message.blobs_[0].size() != img_header->width_ * img_header->height_ * 3)
    {
        log(aergo::module::logging::LogType::WARNING, "Camera calibration module received invalid image data, dropping");
        return;
    }

    auto blob = message.blobs_[0];

    CalibratedImageHeader calibrated_img_header;
    calibrated_img_header.image_header_ = *img_header;
    memcpy(calibrated_img_header.camera_matrix_, camera_matrix_.ptr<double>(), 9 * sizeof(double));
    memcpy(calibrated_img_header.distortion_coefficients_, distortion_coefficients_.ptr<double>(), 5 * sizeof(double));

    std::string msg = "Calibrating image of size " + std::to_string(calibrated_img_header.image_header_.width_) + "x" + std::to_string(calibrated_img_header.image_header_.height_)
    + " with camera matrix: [";
    for (int i = 0; i < 9; i++)
    {
        msg += std::to_string(calibrated_img_header.camera_matrix_[i]);
        if (i < 8) msg += ", ";
    }
    msg += "] and distortion coefficients: [";
    for (int i = 0; i < 5; i++)
    {
        msg += std::to_string(calibrated_img_header.distortion_coefficients_[i]);
        if (i < 4) msg += ", ";
    }
    msg += "]";
    log(aergo::module::logging::LogType::INFO, msg);

    message::MessageHeader calibrated_image_msg {
        .data_ = reinterpret_cast<uint8_t*>(&calibrated_img_header),
        .data_len_ = sizeof(CalibratedImageHeader),
        .blobs_ = &blob,
        .blob_count_ = 1
    };
    
    sendMessage(0, calibrated_image_msg); // publish on channel 0
}



bool CameraCalibrationModule::loadCameraCalibration()
{
    auto& data_path_str = getDataPath();
    if (data_path_str.empty())
    {
        log(aergo::module::logging::LogType::ERROR, "Camera calibration module data path is empty");
        return false;
    }

    std::filesystem::path base(data_path_str);
    auto path_calibration_data = base / "camera_parameters.xml";

    if (!std::filesystem::exists(path_calibration_data))
    {
        log(aergo::module::logging::LogType::ERROR, "Camera calibration data file does not exist: " + path_calibration_data.string());
        return false;
    }

    cv::FileStorage fs(path_calibration_data.string(), cv::FileStorage::READ);
    if (!fs.isOpened())
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to open camera calibration data file with OpenCV");
        return false;
    }


    try
    {
        fs["CAMERA_MATRIX"] >> camera_matrix_;
        fs["DISTORTION_COEFFICIENTS"] >> distortion_coefficients_;
        fs.release();
    }
    catch (const cv::Exception& e)
    {
        fs.release();
        log(aergo::module::logging::LogType::ERROR, std::string("Failed to read camera calibration data from file: ") + e.what());
        return false;
    }

    if (camera_matrix_.empty() || camera_matrix_.rows != 3 || camera_matrix_.cols != 3 || camera_matrix_.type() != CV_64F)
    {
        log(aergo::module::logging::LogType::ERROR, "Invalid camera matrix in calibration data");
        return false;
    }

    if (distortion_coefficients_.empty() || distortion_coefficients_.rows != 1 || (distortion_coefficients_.cols != 5) || distortion_coefficients_.type() != CV_64F)
    {
        log(aergo::module::logging::LogType::ERROR, "Invalid distortion coefficients in calibration data");
        return false;
    }

    return true;
}