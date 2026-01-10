#include "scene_detection_module.h"
#include "module_helpers/calibrated_camera_world_messages/message_types.h"
#include "module_helpers/scene_detection_helper/message_types.h"
#include "detection/pose_utils.h"
#include "detection/scene_marker_detector.h"

#include <atomic>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

using namespace aergo::default_modules::scene_detection_stereocam_module;
using namespace aergo::module;

namespace ccrm = aergo::module::helpers::calibrated_camera_world_messages;
namespace sdh = aergo::module::helpers::scene_detection_helper;
using json = nlohmann::json;


SceneDetectionStereocamModule::SceneDetectionStereocamModule(
    const char* data_path,
    aergo::module::ICore* core,
    aergo::module::InputChannelMapInfo channel_map_info,
    const aergo::module::logging::ILogger* logger,
    uint64_t module_id,
    const aergo::module::ModuleInfo* module_info)
: BaseModule(data_path, core, channel_map_info, logger, module_id, module_info)
{
    if (!loadRegisteredObjects())
    {
        return;
    }

    auto dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_100);
    auto refineMode = SceneMarkerDetector::RefineMode::CONTOUR;

    marker_detector_ = std::make_unique<SceneMarkerDetector>(markers_data_, dict, refineMode);
    stereo_marker_matcher_ = std::make_unique<StereoMarkerMatcher>(markers_data_);
    stereo_marker_optimizer_ = std::make_unique<StereoMarkerOptimizer>();
    optimizer_results_.reserve(10);

    mixed_buffered_allocator_ = aergo::module::helpers::mixed_buffer_allocator::MixedBufferedAllocator::create(this, 1024, 4);
    if (!mixed_buffered_allocator_)
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: failed to create mixed buffered allocator.");
        return;
    }

    if (!getSubscribeChannelByName(ccrm::calibrated_camera_publish_producer.channel_type_identifier_, subscribe_consumer_id_calibrated_camera_))
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: failed to resolve calibrated camera subscribe channel.");
        return;
    }

    if (!getResponseChannelByName(sdh::scene_detection_response_producer.channel_type_identifier_, response_producer_id_scene_detection_))
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: failed to resolve scene detection response channel.");
        return;
    }

    valid_ = true;
}


void* SceneDetectionStereocamModule::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    return nullptr;
}


IModule::IngressDecision SceneDetectionStereocamModule::onIngress(
    ProcessingType kind,
    uint32_t local_channel_id,
    aergo::module::ChannelIdentifier src,
    const aergo::module::message::MessageHeader& msg,
    QueueStatus queue_status) noexcept
{
    if (kind == ProcessingType::MESSAGE && local_channel_id == subscribe_consumer_id_calibrated_camera_)
    {
        return IngressDecision::ACCEPT_REPLACE_QUEUE; // accept all calibrated camera messages (keep only latest for camera stream)
    }
    if (kind == ProcessingType::REQUEST && local_channel_id == response_producer_id_scene_detection_)
    {
        return IngressDecision::ACCEPT; // accept all scene detection requests
    }
    return IngressDecision::DROP; // drop all other messages
}


bool SceneDetectionStereocamModule::parseCalibrationHeader(aergo::module::message::MessageHeader message)
{
    ccrm::CalibratedCameraSet cameraSet;
    if (!message.readAs(cameraSet))
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: failed to read CalibratedCameraSet message.");
        return false;
    }

    if (cameraSet.calibrated_count != 2)
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: expected 2 calibrated cameras, got " + std::to_string(cameraSet.calibrated_count));
        return false;
    }

    ccrm::Pose left_cam_pose, right_cam_pose;
    if (!ccrm::parseCalibData(cameraSet, 0, left_cam_pose, left_cam_data_.K, left_cam_data_.D) ||
        !ccrm::parseCalibData(cameraSet, 1, right_cam_pose, right_cam_data_.K, right_cam_data_.D))
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: failed to get camera calibration from message.");
        return false;
    }

    SE3 T_world_left_cam = SE3::fromQuatTvec(
        cv::Vec4d(left_cam_pose.qw, left_cam_pose.qx, left_cam_pose.qy, left_cam_pose.qz),
        cv::Vec3d(left_cam_pose.x, left_cam_pose.y, left_cam_pose.z),
        false
    );
    SE3 T_world_right_cam = SE3::fromQuatTvec(
        cv::Vec4d(right_cam_pose.qw, right_cam_pose.qx, right_cam_pose.qy, right_cam_pose.qz),
        cv::Vec3d(right_cam_pose.x, right_cam_pose.y, right_cam_pose.z),
        false
    );

    left_cam_data_.T_cam_ref = T_world_left_cam.inverse();
    right_cam_data_.T_cam_ref = T_world_right_cam.inverse();

    return true;
}


bool SceneDetectionStereocamModule::parseImageData(message::MessageHeader message)
{
    uint32_t image_count = 0;

    for (uint64_t blob_id = 0; blob_id < message.blob_count_; ++blob_id)
    {
        auto& blob = message.blobs_[blob_id];
        
        if (!blob.valid())
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: invalid blob in calibrated camera message.");
            return false;
        }

        std::byte* blob_data = reinterpret_cast<std::byte*>(blob.data());
        uint64_t blob_size = blob.size();

        if (!helpers::camera_messages::isBlobValid(blob_data, blob_size))
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: invalid camera image blob data.");
            return false;
        }

        helpers::camera_messages::BlobHeader blobHeader;
        if (!helpers::camera_messages::readBlobHeader(blob_data, blob_size, blobHeader))
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: failed to read camera image blob header.");
            return false;
        }

        int mat_type, conv_type;
        if (blobHeader.format_ == helpers::camera_messages::ImageFormat::BGR8)
        {
            mat_type = CV_8UC3;
            conv_type = cv::COLOR_BGR2GRAY;
        }
        else if (blobHeader.format_ == helpers::camera_messages::ImageFormat::BGRA8)
        {
            mat_type = CV_8UC4;
            conv_type = cv::COLOR_BGRA2GRAY;
        }
        else
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: unsupported camera image format in blob.");
            return false;
        }

        

        for (uint32_t img_idx = 0; img_idx < blobHeader.image_count_; ++img_idx)
        {
            ++image_count;
            if (image_count > 2)
            {
                log(logging::LogType::ERROR, "SceneDetectionStereocamModule: received more images than expected.");
                return false;
            }

            helpers::camera_messages::ImageHeader imageHeader;
            if (!helpers::camera_messages::readImageHeader(blob_data, blob_size, img_idx, imageHeader))
            {
                log(logging::LogType::ERROR, "SceneDetectionStereocamModule: failed to read camera image header.");
                return false;
            }

            cv::Mat camera_image(
                imageHeader.height_,
                imageHeader.width_,
                mat_type,
                blob_data + imageHeader.data_offset_,
                blobHeader.stride_
            ); // lightweight wrapper, does not copy data

            cv::cvtColor(
                camera_image,
                image_count == 1 ? left_cam_image_ : right_cam_image_,
                conv_type
            );
        }
    }

    if (image_count != 2)
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: expected 2 images, got " + std::to_string(image_count));
        return false;
    }
    
    return true;
}


void SceneDetectionStereocamModule::processMessage(
    uint32_t subscribe_consumer_id,
    aergo::module::ChannelIdentifier source_channel,
    aergo::module::message::MessageHeader message) noexcept
{
    if (subscribe_consumer_id != subscribe_consumer_id_calibrated_camera_)
    {
        log(logging::LogType::WARNING, "SceneDetectionStereocamModule: received message on unexpected subscribe consumer.");
        return;
    }

    if (!image_requested_.load(std::memory_order_acquire))
    {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(image_access_mutex_);
        if (!parseCalibrationHeader(message))
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: failed to parse calibration header.");
            return;
        }

        if (!parseImageData(message))
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: failed to parse image data.");
            return;
        }
        
        image_requested_.store(false, std::memory_order_release);
    }

    image_ready_condition_.notify_all();
}


aergo::module::ResponseData SceneDetectionStereocamModule::processRequest(
    uint32_t response_producer_id,
    aergo::module::ChannelIdentifier source_channel,
    aergo::module::message::MessageHeader message) noexcept
{
    if (response_producer_id != response_producer_id_scene_detection_)
    {
        log(logging::LogType::WARNING, "SceneDetectionStereocamModule: received request on unexpected response producer.");
        return { .success_ = false };
    }

    sdh::Request request;
    if (!message.readAs(request))
    {
        log(logging::LogType::WARNING, "SceneDetectionStereocamModule: received request with invalid data.");
        return { .success_ = false };
    }

    if (request.version != sdh::SCENE_DETECTION_MESSAGE_VERSION)
    {
        log(logging::LogType::WARNING, "SceneDetectionStereocamModule: received request with invalid version.");
        return { .success_ = false };
    }

    if (request.req_type == sdh::ReqType::READ_REGISTRY)
    {
        blob_buffer_.clear();
        sdh::Response response = sdh::Response::registryResponse(registered_boxes_, blob_buffer_);
        return ResponseData::createResponse(response, std::span<std::byte>(blob_buffer_), mixed_buffered_allocator_.get());
    }

    if (request.req_type != sdh::ReqType::READ_SCENE)
    {
        log(logging::LogType::WARNING, "SceneDetectionStereocamModule: received request with invalid request type.");
        return { .success_ = false };
    }

    // Request is READ_SCENE, perform scene detection

    std::unique_lock<std::mutex> lock(image_access_mutex_);

    if (image_requested_.load(std::memory_order_acquire))
    {
        log(logging::LogType::WARNING, "SceneDetectionStereocamModule: previous read scene request is still being processed.");
        return { .success_ = false };
    }

    image_requested_.store(true, std::memory_order_release);
    image_ready_condition_.wait(lock, [this]() { return !image_requested_.load(std::memory_order_acquire); });

    marker_detector_->detectMarkers(left_cam_data_, left_cam_image_, detection_results_[0]);
    marker_detector_->detectMarkers(right_cam_data_, right_cam_image_, detection_results_[1]);

    stereo_marker_matcher_->matchMarkers(left_cam_data_, right_cam_data_, detection_results_[0], detection_results_[1], match_result_);

    std::string log_msg;

    optimizer_results_.clear();
    detected_boxes_.clear();
    for (const auto& match : match_result_.matchedMarkers)
    {
        optimizer_results_.push_back(StereoMarkerOptimizer::Result{});
        auto& result = optimizer_results_.back();
        stereo_marker_optimizer_->optimizeMarker(left_cam_data_, right_cam_data_, match, result);

        if (result.success)
        {
            // box wants T_ref_pose

            SE3 T_ref_pose = result.T_ref_marker_optimized * markers_data_[match.marker_id].T_marker_pose();

            cv::Vec3d t_ref_pose;
            cv::Vec4d q_ref_pose; // (qw,qx,qy,qz)
            T_ref_pose.toQuatTvec(q_ref_pose, t_ref_pose, true);

            aergo::module::helpers::scene_detection_helper::DetectedBox box;
            box.id = match.marker_id;
            box.pose.x = t_ref_pose[0];
            box.pose.y = t_ref_pose[1];
            box.pose.z = t_ref_pose[2];
            box.pose.qw = q_ref_pose[0];
            box.pose.qx = q_ref_pose[1];
            box.pose.qy = q_ref_pose[2];
            box.pose.qz = q_ref_pose[3];
            detected_boxes_.push_back(box);

            log_msg += "\tBox " + std::to_string(box.id) + ": [" + std::to_string(box.pose.x * 1000) + ", " + std::to_string(box.pose.y * 1000) + ", " + std::to_string(box.pose.z * 1000) + "] mm, [" + std::to_string(box.pose.qw) + ", " + std::to_string(box.pose.qx) + ", " + std::to_string(box.pose.qy) + ", " + std::to_string(box.pose.qz) + "]\n";
        }
        else
        {
            log_msg += "\tBox " + std::to_string(match.marker_id) + ": optimization failed.\n";
        }
    }
    log_msg = "SceneDetectionStereocamModule: detected " + std::to_string(detected_boxes_.size()) + "/" + std::to_string(match_result_.matchedMarkers.size()) + " boxes:\n" + log_msg;
    log(logging::LogType::INFO, log_msg);

    blob_buffer_.clear();
    sdh::Response response = sdh::Response::sceneResponse(detected_boxes_, blob_buffer_);
    return ResponseData::createResponse(response, std::span<std::byte>(blob_buffer_), mixed_buffered_allocator_.get());
}


bool SceneDetectionStereocamModule::loadRegisteredObjects()
{
    std::filesystem::path json_path(getDataPath());
    json_path /= "registered_objects.json";

    if (!std::filesystem::exists(json_path))
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: registered_objects.json does not exist at: " + json_path.string());
        return false;
    }

    std::ifstream file(json_path);
    if (!file.is_open())
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: failed to open registered_objects.json at: " + json_path.string());
        return false;
    }

    json json_data;
    try
    {
        file >> json_data;
    }
    catch (const json::parse_error& e)
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: failed to parse registered_objects.json: " + std::string(e.what()));
        return false;
    }

    if (!json_data.is_array())
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: registered_objects.json root must be an array.");
        return false;
    }

    if (json_data.empty())
    {
        log(logging::LogType::ERROR, "SceneDetectionStereocamModule: registered_objects.json must contain at least one object.");
        return false;
    }

    markers_data_.clear();
    registered_boxes_.clear();

    for (const auto& obj : json_data)
    {
        // Validate required fields
        if (!obj.contains("markerId") || !obj["markerId"].is_number_integer())
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: object missing or invalid markerId.");
            return false;
        }

        if (!obj.contains("marker_size") || !obj["marker_size"].is_number())
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: object missing or invalid marker_size.");
            return false;
        }

        if (!obj.contains("size_x") || !obj["size_x"].is_number())
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: object missing or invalid size_x.");
            return false;
        }

        if (!obj.contains("size_y") || !obj["size_y"].is_number())
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: object missing or invalid size_y.");
            return false;
        }

        if (!obj.contains("size_z") || !obj["size_z"].is_number())
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: object missing or invalid size_z.");
            return false;
        }

        if (!obj.contains("T_marker_pose_") || !obj["T_marker_pose_"].is_object())
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: object missing or invalid T_marker_pose_.");
            return false;
        }

        const auto& pose = obj["T_marker_pose_"];
        if (!pose.contains("position") || !pose["position"].is_object())
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: T_marker_pose_ missing or invalid position.");
            return false;
        }

        if (!pose.contains("orientation") || !pose["orientation"].is_object())
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: T_marker_pose_ missing or invalid orientation.");
            return false;
        }

        const auto& pos = pose["position"];
        const auto& orient = pose["orientation"];

        if (!pos.contains("x") || !pos.contains("y") || !pos.contains("z") ||
            !pos["x"].is_number() || !pos["y"].is_number() || !pos["z"].is_number())
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: position missing or invalid x/y/z.");
            return false;
        }

        if (!orient.contains("qx") || !orient.contains("qy") || !orient.contains("qz") || !orient.contains("qw") ||
            !orient["qx"].is_number() || !orient["qy"].is_number() || !orient["qz"].is_number() || !orient["qw"].is_number())
        {
            log(logging::LogType::ERROR, "SceneDetectionStereocamModule: orientation missing or invalid qx/qy/qz/qw.");
            return false;
        }

        int marker_id = obj["markerId"];
        float marker_size = obj["marker_size"];

        double pos_x = pos["x"];
        double pos_y = pos["y"];
        double pos_z = pos["z"];

        double qx = orient["qx"];
        double qy = orient["qy"];
        double qz = orient["qz"];
        double qw = orient["qw"];

        // Convert quaternion from [qx,qy,qz,qw] to [qw,qx,qy,qz] format for SE3::fromQuatTvec
        cv::Vec4d quat(qw, qx, qy, qz);
        cv::Vec3d tvec(pos_x, pos_y, pos_z);

        SE3 T_marker_pose = SE3::fromQuatTvec(quat, tvec, true);

        // Build MarkerData
        markers_data_[marker_id] = SceneMarkerDetector::MarkerData(marker_size, T_marker_pose);

        // Build RegisteredBox
        sdh::RegisteredBox box;
        box.id = static_cast<uint64_t>(marker_id);
        box.size_x = obj["size_x"];
        box.size_y = obj["size_y"];
        box.size_z = obj["size_z"];
        registered_boxes_.push_back(box);
    }

    return true;
}

