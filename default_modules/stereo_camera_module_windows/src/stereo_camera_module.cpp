#include "stereo_camera_module.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <sstream>

using namespace aergo::default_modules::stereo_camera_module;
using namespace aergo::module;
using json = nlohmann::json;


void* StereoCameraModule::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    if (id == typeid(helpers::activation_wrapper::IActivableModule)) return static_cast<helpers::activation_wrapper::IActivableModule*>(this);
    return nullptr;
}


bool StereoCameraModule::activate(std::vector<std::vector<std::vector<uint8_t>>>& parameter_values, const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (activated_)
    {
        log(aergo::module::logging::LogType::WARNING, "Stereo camera module is already activated");
        return true; // already activated
    }

    if (parameter_values.size() != 7 || parameter_values[0].size() != 1 || parameter_values[1].size() != 1 ||
        parameter_values[2].size() != 1 || parameter_values[3].size() != 1 || parameter_values[4].size() != 1 || 
        parameter_values[5].size() != 1 || parameter_values[6].size() != 1 ||
        parameter_values[0][0].size() != sizeof(int64_t) || parameter_values[1][0].size() != sizeof(int64_t) ||
        parameter_values[2][0].size() != sizeof(int64_t) || parameter_values[3][0].size() != sizeof(int64_t) ||
        parameter_values[4][0].size() != sizeof(int64_t) || parameter_values[5][0].size() != 1 || // bool
        parameter_values[6][0].size() != sizeof(int64_t))
    {
        log(aergo::module::logging::LogType::ERROR, "Invalid parameters for stereo camera module activation");
        return false; // invalid parameters
    }

    memcpy(&res_width_, parameter_values[0][0].data(), sizeof(int64_t));
    memcpy(&res_height_, parameter_values[1][0].data(), sizeof(int64_t));
    memcpy(&fps_, parameter_values[2][0].data(), sizeof(int64_t));
    memcpy(&fps_tolerance_, parameter_values[3][0].data(), sizeof(int64_t));
    memcpy(&camera_index_, parameter_values[4][0].data(), sizeof(int64_t));
    memcpy(&manual_exposure_, parameter_values[6][0].data(), sizeof(int64_t));
    auto_exposure_ = parameter_values[5][0][0] != 0;

    if (!openCamera())
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to open stereo camera");
        return false; // failed to open camera
    }

    activated_.store(true, std::memory_order_release);

    return true;
}


bool StereoCameraModule::deactivate(const std::atomic<bool>& cancel_flag, std::atomic<bool>& cancelled)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!activated_)
    {
        log(aergo::module::logging::LogType::WARNING, "Stereo camera module is already deactivated");
        return true; // already deactivated
    }

    deactivation_confirmed_.store(false, std::memory_order_release);
    activated_.store(false, std::memory_order_release);
    while (!deactivation_confirmed_.load(std::memory_order_acquire)) // wait for capture thread to confirm deactivation before closing camera
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    closeCamera();

    log(aergo::module::logging::LogType::INFO, "Stereo camera module deactivated");


    return true;
}


bool StereoCameraModule::threadStart(uint32_t timeout_ms) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_running_)
    {
        log(aergo::module::logging::LogType::WARNING, "Capture thread is already running");
        return true; // already running
    }

    stop_thread_.store(false, std::memory_order_relaxed);
    capture_thread_ = std::thread(&StereoCameraModule::captureLoop, this);
    thread_running_ = true;
    
    return true;
}


bool StereoCameraModule::threadStop(uint32_t timeout_ms) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!thread_running_)
    {
        log(aergo::module::logging::LogType::WARNING, "Capture thread is not running");
        return true; // already stopped
    }

    stop_thread_.store(true, std::memory_order_relaxed);

    if (capture_thread_.joinable())
    {
        capture_thread_.join();
    }
    thread_running_ = false;

    return true;
}


void StereoCameraModule::captureLoop()
{
    log(aergo::module::logging::LogType::INFO, "Capture thread started");

    while (!stop_thread_.load(std::memory_order_relaxed))
    {
        if (!activated_)
        {
            deactivation_confirmed_.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue; // not activated
        }

        message::SharedDataBlob frame_blob = frame_allocator_->allocate(0); // size is ignored for buffer allocator
        if (!frame_blob.valid())
        {
            log(aergo::module::logging::LogType::ERROR, "Failed to allocate frame blob");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue; // allocation failed
        }

        size_t required_header_size = cm::headerSizeForImages(2);
        size_t frame_buffer_size = camera_->frameBufferSize();
        if (frame_blob.size() < required_header_size + frame_buffer_size)
        {
            log(aergo::module::logging::LogType::ERROR, "Allocated frame blob is too small");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue; // allocation too small
        }

        if (!cm::writeBlobHeader(reinterpret_cast<std::byte*>(frame_blob.data()), frame_blob.size(), frame_blob_header_))
        {
            log(aergo::module::logging::LogType::ERROR, "Failed to write frame blob header");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue; // failed to write header
        }

        if (!cm::writeImageHeader(reinterpret_cast<std::byte*>(frame_blob.data()), frame_blob.size(), 0, image_1_header_))
        {
            log(aergo::module::logging::LogType::ERROR, "Failed to write image 1 header");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue; // failed to write header
        }

        if (!cm::writeImageHeader(reinterpret_cast<std::byte*>(frame_blob.data()), frame_blob.size(), 1, image_2_header_))
        {
            log(aergo::module::logging::LogType::ERROR, "Failed to write image 2 header");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue; // failed to write header
        }

        if (!camera_->grabFrame(frame_blob.data() + image_1_header_.data_offset_, frame_blob.size() - image_1_header_.data_offset_))
        {
            log(aergo::module::logging::LogType::ERROR, "Failed to grab frame from camera");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue; // failed to grab frame
        }

        cm::CameraMessage camera_msg;
        sendMessage(
            0, 
            message::MessageHeader {
                .data_ = reinterpret_cast<uint8_t*>(&camera_msg),
                .data_len_ = sizeof(camera_msg),
                .blobs_ = &frame_blob,
                .blob_count_ = 1
            }
        );
        
    }
}


ISerializableModule::SaveData StereoCameraModule::save() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    ISerializableModule::SaveData data;
    data.supports_saving_ = true;
    data.schema_version_ = 1;

    json j;
    j["activated"] = activated_.load(std::memory_order_acquire);
    j["res_width"] = res_width_;
    j["res_height"] = res_height_;
    j["fps"] = fps_;
    j["fps_tolerance"] = fps_tolerance_;
    j["camera_index"] = camera_index_;
    j["auto_exposure"] = auto_exposure_;
    j["manual_exposure"] = manual_exposure_;

    data.json_header_ = j.dump();

    return data;
}


bool StereoCameraModule::load(ISerializableModule::SaveData data) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!data.supports_saving_ || data.schema_version_ != 1)
    {
        log(aergo::module::logging::LogType::ERROR, "Unsupported save data for stereo camera module load");
        return false; // unsupported save data
    }

    try 
    {
        json j = json::parse(data.json_header_);

        if (!j.contains("activated") || !j.contains("res_width") || !j.contains("res_height") ||
            !j.contains("fps") || !j.contains("fps_tolerance") || !j.contains("camera_index") ||
            !j.contains("auto_exposure") || !j.contains("manual_exposure") || !j["activated"].is_boolean() ||
            !j["res_width"].is_number_integer() || !j["res_height"].is_number_integer() ||
            !j["fps"].is_number_integer() || !j["fps_tolerance"].is_number_integer() ||
            !j["camera_index"].is_number_integer() || !j["auto_exposure"].is_boolean() || !j["manual_exposure"].is_number_integer())
        {
            log(aergo::module::logging::LogType::ERROR, "Incomplete save data for stereo camera module load");
            return false; // incomplete data
        }

        bool was_activated = j["activated"].get<bool>();
        res_width_ = j["res_width"].get<int64_t>();
        res_height_ = j["res_height"].get<int64_t>();
        fps_ = j["fps"].get<int64_t>();
        fps_tolerance_ = j["fps_tolerance"].get<int64_t>();
        camera_index_ = j["camera_index"].get<int64_t>();
        auto_exposure_ = j["auto_exposure"].get<bool>();
        manual_exposure_ = j["manual_exposure"].get<int64_t>();

        if (was_activated)
        {
            if (openCamera())
            {
                activated_.store(true, std::memory_order_release);
            }
            else
            {
                log(aergo::module::logging::LogType::WARNING, "Failed to open stereo camera module during load");
                activated_.store(false, std::memory_order_release);
            }
        }
        else
        {
            activated_.store(false, std::memory_order_release);
        }
    }
    catch (const std::exception& e)
    {
        log(aergo::module::logging::LogType::ERROR, std::string("Failed to parse saved data for stereo camera module: ") + e.what());
        return false; // failed to parse JSON
    }

    return true;
}


bool StereoCameraModule::openCamera()
{
    if (res_width_ < 640 || res_width_ > 8192 || res_height_ < 240 || res_height_ > 2160 ||
        fps_ < 1 || fps_ > 240 || fps_tolerance_ < 1 || fps_tolerance_ > 120 || 
        camera_index_ < 0 || camera_index_ > 10)
    {
        log(aergo::module::logging::LogType::ERROR, "Activation parameters out of range");
        return false; // invalid parameters
    }

    std::stringstream log_msg;
    log_msg << "Activating stereo camera module\n" 
        << "\tRequested parameters: \n"
        << "\t\tResolution: " << res_width_ << "x" << res_height_ << "\n"
        << "\t\tFramerate: " << fps_ << " FPS\n"
        << "\t\tFramerate Tolerance: " << fps_tolerance_ << " FPS\n"
        << "\t\tCamera Index: " << camera_index_ << "\n"
        << "\t\tAuto Exposure: " << (auto_exposure_ ? "Enabled" : "Disabled") << "\n"
        << "\t\tManual Exposure Value: " << manual_exposure_ << "\n"
        << "\t\tFormat: MJPEG (=" << static_cast<int>(CompressionFormat::MJPEG) << ")\n"
        << "\tEnumerating available cameras...\n";

    std::vector<CameraInfo> available_cameras = enumerateCameras();
    std::vector<CameraInfo> filtered_cameras;
    for (const auto& cam : available_cameras)
    {
        log_msg << "\t\tFound camera: " << cam.name << " (ID: " << cam.id << "), available modes:\n";
        std::vector<VideoMode> filtered_modes;
        for (const auto& mode : cam.modes)
        {
            log_msg << "\t\t\t" << mode.width << "x" << mode.height << " @ " << mode.fps << " FPS, format: " << static_cast<int>(mode.format);

            if (mode.width == res_width_ && mode.height == res_height_ && 
                std::abs(mode.fps - fps_) < fps_tolerance_ &&
                mode.format == CompressionFormat::MJPEG)
            {
                filtered_modes.push_back(mode);
                log_msg << " => ACCEPTED";
            }
            log_msg << "\n";
        }
        if (!filtered_modes.empty())
        {
            filtered_cameras.push_back({
                .id = cam.id,
                .name = cam.name,
                .modes = filtered_modes
            });
        }
    }
    log_msg << "\tTotal cameras after filtering: " << filtered_cameras.size() << "\n";
    log(aergo::module::logging::LogType::INFO, log_msg.str());

    if (filtered_cameras.empty())
    {
        log(aergo::module::logging::LogType::ERROR, "No cameras found matching the requested parameters");
        return false; // no cameras found
    }

    if (camera_index_ >= static_cast<int64_t>(filtered_cameras.size()))
    {
        log(aergo::module::logging::LogType::ERROR, "Camera index out of range after filtering");
        return false; // invalid camera index
    }

    Camera::Config cam_cfg;
    cam_cfg.cameraId = filtered_cameras[camera_index_].id;
    cam_cfg.width = filtered_cameras[camera_index_].modes[0].width;
    cam_cfg.height = filtered_cameras[camera_index_].modes[0].height;
    cam_cfg.fps = filtered_cameras[camera_index_].modes[0].fps;
    cam_cfg.format = filtered_cameras[camera_index_].modes[0].format;

    log(aergo::module::logging::LogType::INFO, "Opening camera: " + filtered_cameras[camera_index_].name + " (ID: " + filtered_cameras[camera_index_].id + ")");

    try 
    {
        camera_ = std::make_unique<Camera>(cam_cfg);
    }
    catch (const std::runtime_error& e)
    {
        camera_.reset();
        log(aergo::module::logging::LogType::ERROR, std::string("Failed to open camera: ") + e.what());
        return false; // failed to open camera
    }

    // Set exposure
    if (auto_exposure_)
    {
        if (camera_->setAutoExposure())
        {
            log(aergo::module::logging::LogType::INFO, "Auto exposure enabled");
        }
        else
        {
            log(aergo::module::logging::LogType::WARNING, "Failed to enable auto exposure"); 
        }
    }
    else
    {
        if (camera_->setExposure(manual_exposure_))
        {
            log(aergo::module::logging::LogType::INFO, "Manual exposure set to " + std::to_string(manual_exposure_));
        }
        else
        {
            log(aergo::module::logging::LogType::ERROR, "Failed to set manual exposure to " + std::to_string(manual_exposure_));
            camera_.reset();
            return false; // failed to set manual exposure
        }
    }

    size_t required_header_size = cm::headerSizeForImages(2);
    size_t frame_buffer_size = camera_->frameBufferSize();
    frame_allocator_ = createBufferAllocator(required_header_size + frame_buffer_size, 30); // buffer for 30 frames
    if (!frame_allocator_)
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to create frame buffer allocator");
        camera_.reset();
        return false; // failed to create allocator
    }

    frame_blob_header_ = cm::BlobHeader {
        .stride_ = static_cast<uint32_t>(camera_->stride()),
        .format_ = cm::ImageFormat::BGRA8,
        .image_count_ = 2
    };

    image_1_header_ = cm::ImageHeader {
        .width_ = static_cast<uint32_t>(camera_->width() / 2),
        .height_ = static_cast<uint32_t>(camera_->height()),
        .data_offset_ = required_header_size
    };

    image_2_header_ = cm::ImageHeader {
        .width_ = static_cast<uint32_t>(camera_->width() / 2),
        .height_ = static_cast<uint32_t>(camera_->height()),
        // right image is on the right half (hence /2) of the frame, 4 bytes per pixel (BGRA8, hence *4)
        .data_offset_ = required_header_size + ((camera_->width() / 2) * 4)
    };

    return true;
}


void StereoCameraModule::closeCamera()
{
    camera_.reset();
    frame_allocator_.reset();
}