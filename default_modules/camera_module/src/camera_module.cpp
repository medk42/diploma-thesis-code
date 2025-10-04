#include "camera_module.h"

#include <nlohmann/json.hpp>

using namespace aergo::default_modules::camera_module;
using namespace aergo::module;
using json = nlohmann::json;



CameraModule::CameraModule(const char* data_path, aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::logging::ILogger* logger, uint64_t module_id)
: aergo::module::BaseModule(data_path, core, channel_map_info, logger, module_id)
{}



void* CameraModule::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    if (id == typeid(helpers::activation_wrapper::IActivableModule)) return static_cast<helpers::activation_wrapper::IActivableModule*>(this);
    return nullptr;
}



bool CameraModule::activate(std::vector<std::vector<std::vector<uint8_t>>>& parameter_values, const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled)
{
    std::lock_guard<std::mutex> lock(activation_mutex_);

    if (activated_)
    {
        log(aergo::module::logging::LogType::WARNING, "Camera module is already activated");
        return true; // already activated
    }



    if (parameter_values.size() != 1 || parameter_values[0].size() != 1 || parameter_values[0][0].size() != sizeof(int64_t))
    {
        log(aergo::module::logging::LogType::ERROR, "Invalid parameters for camera module activation");
        return false; // invalid parameters
    }

    int64_t camera_index = 0;
    memcpy(&camera_index, parameter_values[0][0].data(), sizeof(int64_t));

    if (camera_index < 0 || camera_index > 10)
    {
        log(aergo::module::logging::LogType::ERROR, "Camera index out of range (0-10)");
        return false; // invalid camera index
    }



    used_camera_id_ = camera_index;
    bool success = openCamera(camera_index);

    if (!success)
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to open camera with index " + std::to_string(camera_index));
        return false; // failed to open camera
    }
    
    activated_ = true;
    return true;
}



bool CameraModule::deactivate(const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled)
{
    std::lock_guard<std::mutex> lock(activation_mutex_);

    if (!activated_)
    {
        log(aergo::module::logging::LogType::WARNING, "Camera module is already deactivated.");
        return true; // already stopped
    }

    deactivation_confirmed_.store(false, std::memory_order_release);
    activated_.store(false, std::memory_order_release);
    while (!deactivation_confirmed_.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    closeCamera();

    return true;
}



bool CameraModule::threadStart(uint32_t timeout_ms) noexcept
{
    std::lock_guard<std::mutex> lock(activation_mutex_);

    log(aergo::module::logging::LogType::INFO, "Starting camera capture thread");

    stop_thread_ = false;
    capture_thread_ = std::thread(std::bind(&CameraModule::captureLoop, this));
    thread_running_ = true;

    return true;
}



bool CameraModule::threadStop(uint32_t timeout_ms) noexcept
{
    std::lock_guard<std::mutex> lock(activation_mutex_);

    if (!thread_running_)
    {
        log(aergo::module::logging::LogType::WARNING, "Camera capture thread is not running");
        return true; // already stopped
    }

    log(aergo::module::logging::LogType::INFO, "Stopping camera capture thread");
    if (stop_thread_)
    {
        log(aergo::module::logging::LogType::WARNING, "Camera capture thread is already stopped");
        return true; // already stopped
    }

    stop_thread_ = true;
    if (capture_thread_.joinable())
    {
        capture_thread_.join();
    }
    thread_running_ = false;

    return true;
}



int64_t CameraModule::nowMs() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}



void CameraModule::captureLoop()
{    
    while (!stop_thread_)
    {
        if (!activated_.load(std::memory_order_relaxed))
        {
            deactivation_confirmed_.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        message::SharedDataBlob frame_blob = frame_allocator_->allocate(frame_header_.width_ * frame_header_.height_ * 3);
        if (!frame_blob.valid())
        {
            log(aergo::module::logging::LogType::WARNING, "Failed to allocate memory for frame");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        cv::Mat frame(frame_header_.height_, frame_header_.width_, CV_8UC3, frame_blob.data());
        if (!cap_->read(frame))
        {
            log(aergo::module::logging::LogType::WARNING, "Failed to read frame from camera");
            continue;
        }
        if (frame.data != frame_blob.data() || frame.empty() || frame.type() != CV_8UC3 || frame.step != frame.cols * 3 || frame.cols != frame_header_.width_ || frame.rows != frame_header_.height_)
        {
            log(aergo::module::logging::LogType::WARNING, "Capture mismatch, skipping frame");
            continue;
        }

        message::MessageHeader msg {
            .data_ = reinterpret_cast<uint8_t*>(&frame_header_),
            .data_len_ = sizeof(frame_header_),
            .blobs_ = &frame_blob,
            .blob_count_ = 1
        };

        log(aergo::module::logging::LogType::INFO, "MODULE,CAMERA,PUBLISH,INFO=\"" + std::to_string(frame_header_.width_) + "x" + std::to_string(frame_header_.height_) + "\"");

        sendMessage(0, msg); // publish on channel 0

        // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}



bool CameraModule::openCamera(int64_t camera_index)
{
    log(aergo::module::logging::LogType::INFO, "Initializing camera module " + std::to_string(camera_index));
    
    cap_ = std::make_unique<cv::VideoCapture>((int)camera_index);//, cv::CAP_DSHOW);
    // cap_->set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));

    if (!cap_->isOpened())
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to open camera");
        return false; // failed to open camera, return with valid_ = false
    }

    cv::Mat probe;
    if (!cap_->read(probe))
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to read from camera");
        return false; // failed to read from camera, return with valid_ = false
    }

    if (probe.empty() || probe.type() != CV_8UC3 || probe.step != probe.cols * 3)
    {
        log(aergo::module::logging::LogType::ERROR, "Frame from camera is not a valid 3-channel BGR image");
        return false; // empty frame, return with valid_ = false
    }

    frame_header_ = {
        .width_ = (uint16_t)probe.cols,
        .height_ = (uint16_t)probe.rows
    };

    log(aergo::module::logging::LogType::INFO, "Camera frame size: " + std::to_string(frame_header_.width_) + "x" + std::to_string(frame_header_.height_));

    uint64_t expected_size = frame_header_.width_ * frame_header_.height_ * 3;
    if (expected_size != probe.total() * probe.elemSize())
    {
        std::string size_comparison = std::to_string(frame_header_.width_) + " * " + std::to_string(frame_header_.height_) + " * 3   !=  " + std::to_string(probe.total()) + " * " + std::to_string(probe.elemSize());
        log(aergo::module::logging::LogType::ERROR, "Frame size from camera does not match expected size: " + size_comparison);
        return false; // frame size does not match expected size, return with valid_ = false
    }

    if (probe.cols > 65535 || probe.rows > 65535 || probe.cols < 0 || probe.rows < 0)
    {
        log(aergo::module::logging::LogType::ERROR, "Frame dimensions exceed maximum supported size");
        return false; // frame dimensions exceed maximum supported size, return with valid_ = false
    }

    frame_allocator_ = createBufferAllocator(expected_size, 30); // buffer for 30 frames
    if (!frame_allocator_)
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to create frame allocator");
        return false; // failed to create allocator, return with valid_ = false
    }

    log(aergo::module::logging::LogType::INFO, "Camera module initialized successfully");

    return true;
}



void CameraModule::closeCamera()
{
    cap_.reset();
}



ISerializableModule::SaveData CameraModule::save() noexcept
{
    std::lock_guard<std::mutex> lock(activation_mutex_);

    ISerializableModule::SaveData data;
    data.supports_saving_ = true;
    data.schema_version_ = 1;
    
    json header;
    header["activated"] = activated_.load();
    header["used_camera_id"] = used_camera_id_;

    data.json_header_ = header.dump();

    return data;
}



bool CameraModule::load(ISerializableModule::SaveData data) noexcept
{
    std::lock_guard<std::mutex> lock(activation_mutex_);

    if (!data.supports_saving_ || data.schema_version_ != 1)
    {
        log(aergo::module::logging::LogType::ERROR, "Unsupported save data for camera module");
        return false; // unsupported save data
    }

    try
    {
        auto header = json::parse(data.json_header_);
        
        if (!header.contains("activated")  || !header["activated"].is_boolean()
        || !header.contains("used_camera_id") || !header["used_camera_id"].is_number_integer())
        {
            log(aergo::module::logging::LogType::ERROR, "Save data missing required fields");
            return false; // missing required fields
        }

        bool was_activated = header["activated"].get<bool>();
        int64_t camera_id = header["used_camera_id"].get<int64_t>();

        if (was_activated)
        {
            used_camera_id_ = camera_id;
            bool success = openCamera(camera_id);
            if (success)
            {
                activated_ = true;
            }
            else
            {
                log(aergo::module::logging::LogType::ERROR, "Failed to open camera during load");
                activated_ = false;
            }
        }
        else
        {
            activated_ = false;
        }
    }
    catch (const std::exception& e)
    {
        log(aergo::module::logging::LogType::ERROR, std::string("Failed to parse save data: ") + e.what());
        return false; // failed to parse save data
    }

    return true;
}