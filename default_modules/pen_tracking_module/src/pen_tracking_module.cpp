#include "pen_tracking_module.h"

#include "message_structure.h"
#include "defaults.h"

#include <filesystem>



using namespace aergo::default_modules::pen_tracking_module;
using namespace aergo::module;
using namespace aergo::pen_calibration::helper;
using namespace aergo::module::helpers;



PenTrackingModule::PenTrackingModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, const logging::ILogger* logger, uint64_t module_id, const ModuleInfo* module_info)
: BaseModule(data_path, core, channel_map_info, logger, module_id, module_info), valid_(false)
{
    if (!getSubscribeChannelByName(image_bgr_calib_data_channel_type, subscribe_camera_channel_id_))
    {
        log(logging::LogType::ERROR, "Pen tracking module requires one subscribe channel for image data");
        return;
    }
    InputChannelMapInfo::IndividualChannelInfo image_input_info = getSubscribeChannelInfo(subscribe_camera_channel_id_); // image data input
    if (image_input_info.channel_identifier_count_ != 1)
    {
        log(logging::LogType::ERROR, "Pen tracking module requires exactly one input channel for image data");
        return;
    }

    if (!getPublishChannelByName(pen_3d_pose_publish_producer.channel_type_identifier_, publish_pen_channel_id_))
    {
        log(logging::LogType::ERROR, "Pen tracking module requires one publish channel for pen 3D pose data");
        return;
    }

    if (!loadPenCalibration())
    {
        log(logging::LogType::ERROR, "Failed to load pen calibration data");
        return;
    }

    vis3d_helper_ = std::make_unique<vis3d::VisualizationHelper>(this);
    if (!vis3d_helper_->valid())
    {
        log(logging::LogType::ERROR, "Failed to initialize 3D visualization helper");
        return;
    }

    registerPenVisualization();

    valid_ = true;
}



void* PenTrackingModule::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    if (id == typeid(helpers::activation_wrapper::IActivableModule)) return static_cast<helpers::activation_wrapper::IActivableModule*>(this);
    return nullptr;
}



IModule::IngressDecision PenTrackingModule::onIngress(ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, QueueStatus queue_status) noexcept
{
    // accept only messages on subscribe channel 0 (image data), drop all others
    if (kind == ProcessingType::MESSAGE && local_channel_id == 0)
    {
        return IngressDecision::ACCEPT_REPLACE_QUEUE; // only keep the latest image
    }
    return IngressDecision::DROP;
}



void PenTrackingModule::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (!announced_)
    {
        std::lock_guard<std::mutex> lock(vis3d_mutex_);
        vis3d_helper_->announce();
        announced_ = true;
    }

    if (subscribe_consumer_id != subscribe_camera_channel_id_)
    {
        log(logging::LogType::WARNING, "Pen tracking module received message on invalid subscribe channel, dropping");
        return;
    }

    if (message.data_ == nullptr || message.data_len_ != sizeof(CalibratedImageHeader))
    {
        log(logging::LogType::WARNING, "Pen tracking module received invalid calibrated image message, dropping");
        return;
    }

    CalibratedImageHeader* calibrated_img_header = reinterpret_cast<CalibratedImageHeader*>(message.data_);
    if (message.blob_count_ != 1 || message.blobs_[0].size() != calibrated_img_header->image_header_.width_ * calibrated_img_header->image_header_.height_ * 3)
    {
        log(logging::LogType::WARNING, "Pen tracking module received invalid image data, dropping");
        return;
    }

    auto blob = message.blobs_[0];

    
    if (!marker_tracker_)
    {
        cv::Mat camera_matrix_raw(3, 3, CV_64F, calibrated_img_header->camera_matrix_);
        cv::Mat distortion_coefficients_raw(1, 5, CV_64F, calibrated_img_header->distortion_coefficients_);

        cv::Mat camera_matrix = camera_matrix_raw.clone();
        cv::Mat distortion_coefficients = distortion_coefficients_raw.clone();

        double search_window_percent = 9; // the area in which to search for the pen, relative to the rectangle bounding the last detected markers
        marker_tracker_ = std::make_unique<aergo::pen_tracking::MarkerTracker>(
            camera_matrix, distortion_coefficients, defaults::pen::getArucoDetector(),
            defaults::pen::USED_MARKER_IDS, defaults::pen::getMarkerPoints3d(),
            tip_to_other_transformations_, search_window_percent
        );
    }

    cv::Mat image(calibrated_img_header->image_header_.height_, calibrated_img_header->image_header_.width_, CV_8UC3, blob.data());

    int64_t start_ns = nowNs();
    aergo::pen_tracking::MarkerTracker::Result result = marker_tracker_->processImage(image, nullptr);
    int64_t end_ns = nowNs();
    double processing_time_ms = (end_ns - start_ns) / 1e6;

    if (last_detect_success_ && result.lost_tracking)
    {
        log(logging::LogType::INFO, "Pen tracking module lost tracking of the pen");
    }

    last_detect_success_ = result.success;

    if (!result.success) // image not detected
    {
        if (pen_object_added_)
        {
            std::lock_guard<std::mutex> lock(vis3d_mutex_);
            vis3d_helper_->removeObject(pen_object_id_);
            vis3d_helper_->sendUpdate();
            pen_object_added_ = false;
        }
        log(aergo::module::logging::LogType::INFO, "MODULE,PEN,INTERNAL,INFO=\"tracking fail, processing: " + std::to_string(processing_time_ms) + " ms\"\n\n\n\n\n");
        return;
    }

    // Prepare and send pen pose message
    Pen3DPose pose_msg;
    auto [rvec, tvec] = result.camera_to_origin.asRvecTvec();
    memcpy(pose_msg.tvec_, tvec.ptr<double>(), 3 * sizeof(double));
    memcpy(pose_msg.rvec_, rvec.ptr<double>(), 3 * sizeof(double));

    message::MessageHeader pen_pose_message {
        .data_ = reinterpret_cast<uint8_t*>(&pose_msg),
        .data_len_ = sizeof(Pen3DPose),
        .blobs_ = nullptr,
        .blob_count_ = 0
    };

    std::string msg = "Detected at " + 
        std::to_string(tvec.at<double>(0) * 1000) + ", " + std::to_string(tvec.at<double>(1) * 1000) + ", " + std::to_string(tvec.at<double>(2) * 1000) + 
        ") mm, processing: " + std::to_string(processing_time_ms) + " ms";

    log(aergo::module::logging::LogType::INFO, "MODULE,PEN,PUBLISH,INFO=\"" + msg + "\"\n\n\n\n\n");

    sendMessage(publish_pen_channel_id_, pen_pose_message); // publish on pen pose channel


    // Update 3d visualization
    vis3d::Pose pen_pose {
        .t = { float(tvec.at<double>(0)), float(tvec.at<double>(1)), float(tvec.at<double>(2)) },
        .q = vis3d::Quat::QuatFromRvec(rvec.at<double>(0), rvec.at<double>(1), rvec.at<double>(2))
    };

    if (pen_object_added_)
    {
        std::lock_guard<std::mutex> lock(vis3d_mutex_);
        vis3d_helper_->updateObject(pen_object_id_, pen_pose);
        vis3d_helper_->sendUpdate();
    }
    else
    {
        std::lock_guard<std::mutex> lock(vis3d_mutex_);
        if (vis3d_helper_->addObject(pen_resource_id_, pen_pose, pen_object_id_))
        {
            pen_object_added_ = true;
        }
        vis3d_helper_->sendUpdate();
    }
}



bool PenTrackingModule::loadPenCalibration()
{
    auto& data_path_str = getDataPath();
    if (data_path_str.empty())
    {
        log(logging::LogType::ERROR, "Pen tracking module data path is empty");
        return false;
    }

    std::filesystem::path base(data_path_str);
    auto path_calibration_data = base / "pen_calibration.xml";

    if (!std::filesystem::exists(path_calibration_data))
    {
        log(logging::LogType::ERROR, "Camera calibration data file does not exist: " + path_calibration_data.string());
        return false;
    }

    cv::FileStorage fs(path_calibration_data.string(), cv::FileStorage::READ);
    if (!fs.isOpened())
    {
        log(logging::LogType::ERROR, "Failed to open camera calibration data file with OpenCV");
        return false;
    }

    try
    {
        cv::FileNode transformations_node = fs["origin_to_other"];
        for (cv::FileNodeIterator it = transformations_node.begin(); it != transformations_node.end(); ++it)
        {
            cv::FileNode it_node = (*it);
            int key = std::stoi(it_node.name().substr(4));
            Transformation t;
            it_node["translation"] >> t.translation;
            it_node["rotation"] >> t.rotation;
            tip_to_other_transformations_[key] = t;
        }

        fs.release();

    } 
    catch (const cv::Exception& e)
    {
        fs.release();

        log(logging::LogType::ERROR, std::string("Camera Matrix or Distortion Coefficients failed to load, error: ") + e.what());
        return false;
    }

    if (tip_to_other_transformations_.size() != defaults::pen::USED_MARKER_IDS.size())
    {
        log(logging::LogType::ERROR, "Loaded pen calibration data contains wrong number of markers.");
        return false;
    }

    for (const auto id : defaults::pen::USED_MARKER_IDS)
    {
        auto it = tip_to_other_transformations_.find(id);
        if (it == tip_to_other_transformations_.end())
        {
            log(logging::LogType::ERROR, "Loaded pen calibration data is missing marker ID " + std::to_string(id));
            return false;
        }

        auto& t = it->second;
        if (t.rotation.empty() || t.rotation.rows != 3 || t.rotation.cols != 3 || t.rotation.type() != CV_64F ||
            t.translation.empty() || t.translation.rows != 3 || t.translation.cols != 1 || t.translation.type() != CV_64F)
        {
            log(logging::LogType::ERROR, "Loaded pen calibration data has invalid format for marker ID " + std::to_string(id));
            return false;
        }
    }

    return true;
}



void PenTrackingModule::registerPenVisualization()
{
    std::lock_guard<std::mutex> lock(vis3d_mutex_);

    auto mm_to_m = [](float mm) { return mm / 1000.0f; };

    pen_resource_id_ = vis3d_helper_->registerResource(vis3d::ComplexShape{
        .parts = {
            vis3d::PrimitiveShape{
                .type = vis3d::PrimitiveShapeType::BOX,
                .desc = vis3d::BoxDesc{ .sx = mm_to_m(20), .sy = mm_to_m(20), .sz = mm_to_m(20) },
                .origin = {
                    .t = { 0.0f, 0.0f, 0.0f },
                    .q = { 0.0f, 0.0f, 0.0f, 1.0f }
                },
                .color = vis3d::Color{ 50, 50, 50 }
            },
            vis3d::PrimitiveShape{
                .type = vis3d::PrimitiveShapeType::BOX,
                .desc = vis3d::BoxDesc{ .sx = mm_to_m(20), .sy = mm_to_m(20), .sz = mm_to_m(22) },
                .origin = {
                    .t = { 0.0f, 0.0f, -mm_to_m(20.0/2+8+22.0/2) },
                    .q = vis3d::Quat::Identity().RotateDegZ(45)
                },
                .color = vis3d::Color{ 50, 50, 50 }
            },
            vis3d::PrimitiveShape{
                .type = vis3d::PrimitiveShapeType::CYLINDER,
                .desc = vis3d::CylinderDesc{ .rBot = mm_to_m(15.2 / 2), .rTop = mm_to_m(15.2 / 2), .h = mm_to_m(91) },
                .origin = {
                    .t = { 0.0f, 0.0f, mm_to_m(20.0/2 + 10 + 91.0/2) },
                    .q = vis3d::Quat::Identity()
                },
                .color = vis3d::Color{ 50, 50, 50 }
            },
            vis3d::PrimitiveShape{
                .type = vis3d::PrimitiveShapeType::CYLINDER,
                .desc = vis3d::CylinderDesc{ .rBot = mm_to_m(20.0 / 2), .rTop = mm_to_m(15.2 / 2), .h = mm_to_m(10) },
                .origin = {
                    .t = { 0.0f, 0.0f, mm_to_m(20.0/2 + 10.0/2) },
                    .q = vis3d::Quat::Identity()
                },
                .color = vis3d::Color{ 50, 50, 50 }
            },
            vis3d::PrimitiveShape{
                .type = vis3d::PrimitiveShapeType::CYLINDER,
                .desc = vis3d::CylinderDesc{ .rBot = mm_to_m(15.2 / 2), .rTop = mm_to_m(0), .h = mm_to_m(19.7) },
                .origin = {
                    .t = { 0.0f, 0.0f, mm_to_m(20.0/2 + 10 + 91 + 19.7/2) },
                    .q = vis3d::Quat::Identity()
                },
                .color = vis3d::Color{ 50, 50, 50 }
            },
        }
    });
}