#include "pen_tracking_multicam_module.h"
#include "module_helpers/calibrated_camera_world_messages/message_types.h"
#include "module_helpers/pen_messages/message_types.h"

#include "pen_defaults.h"

using namespace aergo::default_modules::pen_tracking_multicam_module;
using namespace aergo::module;

namespace ccrm = aergo::module::helpers::calibrated_camera_world_messages;
namespace pm = aergo::module::helpers::pen_messages;
namespace cm = aergo::module::helpers::camera_messages;


PenTrackingMulticamModule::PenTrackingMulticamModule(
    const char* data_path,
    aergo::module::ICore* core,
    aergo::module::InputChannelMapInfo channel_map_info,
    const aergo::module::logging::ILogger* logger,
    uint64_t module_id,
    const aergo::module::ModuleInfo* module_info)
: BaseModule(data_path, core, channel_map_info, logger, module_id, module_info),
    poseEstimator_(defaults::markersData()),
    poseOptimizer_(defaults::markersData()),
    ble_reader_(
        defaults::SERVICE_UUID, 
        defaults::CHARACTERISTIC_UUID, 
        [this](PenDataPacket packet) { onBlePacket(packet); }
    ),
    T_pen_tip_(defaults::T_pen_tip()),
    poseFilter_(PoseOneEuroFilter::Params{}),
    pen_intent_detector_(this)
{
    // should be 3, but preallocate more (10) for safety (reflections etc)
    poseEstimationResult_ = MulticamPoseEstimator::DetectionResult::preallocate(2, 10);

    visualization_helper_ = std::make_unique<vis3d::VisualizationHelper>(this);
    if (!visualization_helper_->valid())
    {
        log(logging::LogType::ERROR, "PenTrackingMulticamModule: failed to initialize VisualizationHelper.");
        return;
    }
    response_producer_id_visualization_ = visualization_helper_->getResponseProducerChannel();
    registerResources();

    if (!getSubscribeChannelByName(ccrm::calibrated_camera_publish_producer.channel_type_identifier_, subscribe_consumer_id_calibrated_camera_))
    {
        log(logging::LogType::ERROR, "PenTrackingMulticamModule: failed to resolve calibrated camera subscribe channel.");
        return;
    }

    if (!getPublishChannelByName(pm::pen_message_raw_publish_producer.channel_type_identifier_, publish_producer_id_pen_raw_))
    {
        log(logging::LogType::ERROR, "PenTrackingMulticamModule: failed to resolve pen raw publish channel.");
        return;
    }

    if (!getPublishChannelByName(pm::pen_message_intent_publish_producer.channel_type_identifier_, publish_producer_id_pen_intent_))
    {
        log(logging::LogType::ERROR, "PenTrackingMulticamModule: failed to resolve pen intent publish channel.");
        return;
    }

    // Check pen intent detector validity (constructed in initializer list)
    if (!pen_intent_detector_.valid())
    {
        log(logging::LogType::ERROR, "PenTrackingMulticamModule: failed to initialize PenIntentDetector.");
        return;
    }

    if (!ble_reader_.start())
    {
        log(logging::LogType::ERROR, "PenTrackingMulticamModule: failed to start BLE reader.");
        return;
    }

    valid_ = true;
}


bool PenTrackingMulticamModule::threadStop(uint32_t timeout_ms) noexcept
{
    if (!valid_)
    {
        return true;
    }
    if (!ble_reader_.stop())
    {
        log(logging::LogType::ERROR, "PenTrackingMulticamModule: failed to stop BLE reader.");
    }
    return true;
}


vis3d::Quat alignZToDir(const vis3d::Vec3& dir_in) {
    float norm = std::sqrt(dir_in.x * dir_in.x + dir_in.y * dir_in.y + dir_in.z * dir_in.z);
    if (norm < 1e-6f) {
        return vis3d::Quat::Identity();
    }
    const float dx = dir_in.x / norm;
    const float dy = dir_in.y / norm;
    const float dz = dir_in.z / norm;
    // rotate +Z to dir
    vis3d::Vec3 z{0.f, 0.f, 1.f};
    vis3d::Vec3 v{z.y * dz - z.z * dy, z.z * dx - z.x * dz, z.x * dy - z.y * dx};  // cross
    float dot = z.x * dx + z.y * dy + z.z * dz;
    float s = std::sqrt((1.f + dot) * 2.f);
    if (s < 1e-6f) {
        // Opposite direction: rotate 180 deg about X
        return vis3d::Quat::FromAxisDeg(1.f, 0.f, 0.f, 180.f);
    }
    float invs = 1.f / s;
    vis3d::Quat q{v.x * invs, v.y * invs, v.z * invs, s * 0.5f};
    return q.normalized();
}


void PenTrackingMulticamModule::registerFixedResources(
    ArrowConfig arrow_cfg,
    vis3d::Color trajectory_color
)
{
    // Register arrow resource (TCP axes)
    auto axis_shape = [&](const vis3d::Vec3& axis_dir, const vis3d::Color& color, vis3d::ComplexShape& out_shape) -> void {
        float line_offset = arrow_cfg.line_length_m * 0.5f;

        vis3d::PrimitiveShape line {
            .type = vis3d::PrimitiveShapeType::CYLINDER,
            .desc = vis3d::CylinderDesc {
                .rBot = arrow_cfg.line_radius_m,
                .rTop = arrow_cfg.line_radius_m,
                .h = arrow_cfg.line_length_m
            },
            .origin = vis3d::Pose {
                .t = vis3d::Vec3 {
                    .x = axis_dir.x * line_offset,
                    .y = axis_dir.y * line_offset,
                    .z = axis_dir.z * line_offset
                },
                .q = alignZToDir(axis_dir)
            },
            .color = color
        };

        float tip_offset = arrow_cfg.line_length_m + (arrow_cfg.tip_length_m * 0.5f);

        vis3d::PrimitiveShape tip {
            .type = vis3d::PrimitiveShapeType::CYLINDER,
            .desc = vis3d::CylinderDesc {
                .rBot = arrow_cfg.tip_radius_m,
                .rTop = 0.0f,
                .h = arrow_cfg.tip_length_m
            },
            .origin = vis3d::Pose {
                .t = vis3d::Vec3{ 
                    .x = axis_dir.x * tip_offset, 
                    .y = axis_dir.y * tip_offset, 
                    .z = axis_dir.z * tip_offset
                },
                .q = alignZToDir(axis_dir)
            },
            .color = color
        };

        out_shape.parts.push_back(line);
        out_shape.parts.push_back(tip);
    };

    vis3d::ComplexShape arrow_shape{};
    axis_shape(vis3d::Vec3{1.f, 0.f, 0.f}, vis3d::Color{255, 0, 0, 255}, arrow_shape); // X axis - red
    axis_shape(vis3d::Vec3{0.f, 1.f, 0.f}, vis3d::Color{0, 255, 0, 255}, arrow_shape); // Y axis - green
    axis_shape(vis3d::Vec3{0.f, 0.f, 1.f}, vis3d::Color{0, 0, 255, 255}, arrow_shape); // Z axis - blue

    arrow_resource_id_ = visualization_helper_->registerResource(arrow_shape);

    trajectory_color_ = trajectory_color;
}


void PenTrackingMulticamModule::registerResources()
{
    vis3d::Color color = {.r = 0x80, .g = 0x80, .b = 0xC0, .a = 0xFF};
    pen_resource_id_ = visualization_helper_->registerResource(vis3d::ComplexShape {
        .parts = {
            vis3d::PrimitiveShape {
                .type = vis3d::PrimitiveShapeType::BOX,
                .desc = vis3d::BoxDesc {
                    .sx = 0.048f, .sy = 0.048f, .sz = 0.048f
                },
                .origin = vis3d::Pose(),
                .color = color
            },
            vis3d::PrimitiveShape {
                .type = vis3d::PrimitiveShapeType::CYLINDER,
                .desc = vis3d::CylinderDesc {
                    .rBot = 0.0075f, .rTop = 0.0075f, .h = 0.110f
                },
                .origin = vis3d::Pose {
                    .t = {0, 0, 0.024f + 0.110f / 2.0f}
                },
                .color = color
            },
            vis3d::PrimitiveShape {
                .type = vis3d::PrimitiveShapeType::CYLINDER,
                .desc = vis3d::CylinderDesc {
                    .rBot = 0.0075f, .rTop = 0.0f, .h = 0.020f
                },
                .origin = vis3d::Pose {
                    .t = {0, 0, 0.024f + 0.110f + 0.020f / 2.0f}
                },
                .color = color
            }
        }
    });

    registerFixedResources(
        ArrowConfig{},
        vis3d::Color{ 0xff, 0x6b, 0xf0, 0xff } // pink
    );
}


void* PenTrackingMulticamModule::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    return nullptr;
}


IModule::IngressDecision PenTrackingMulticamModule::onIngress(
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
    if (kind == ProcessingType::REQUEST && local_channel_id == response_producer_id_visualization_)
    {
        return IngressDecision::ACCEPT; // accept all visualization requests for visualization_helper_
    }
    return IngressDecision::DROP; // drop all other messages
}


void PenTrackingMulticamModule::onBlePacket(PenDataPacket packet)
{
    if (!packet.isValid())
    {
        log(logging::LogType::WARNING, "PenTrackingMulticamModule: received invalid BLE pen data packet.");
        return;
    }

    bool primary_down = packet.isPrimaryButtonPressed();
    bool secondary_down = packet.isSecondaryButtonPressed();
    
    pen_button_state_.update(primary_down, secondary_down);
    
    // Update intent detector with button state
    if (pen_intent_detector_.valid())
    {
        pen_intent_detector_.updateButtonState(primary_down, secondary_down);
    }
}


bool PenTrackingMulticamModule::parseCalibrationHeader(message::MessageHeader message, uint32_t& out_camera_count)
{
    ccrm::CalibratedCameraSet cameraSet;
    if (!message.readAs(cameraSet))
    {
        log(logging::LogType::ERROR, "PenTrackingMulticamModule: failed to read CalibratedCameraSet message.");
        return false;
    }

    if (cameraSet.calibrated_count > 4)
    {
        log(logging::LogType::ERROR, "PenTrackingMulticamModule: invalid calibrated camera count in message.");
        return false;
    }

    for (uint32_t i = 0; i < cameraSet.calibrated_count; ++i)
    {
        if (camerasDataBuffer_.size() <= i)
        {
            camerasDataBuffer_.emplace_back();
        }

        auto& camData = camerasDataBuffer_[i];
        
        ccrm::Pose camera_pose;

        if (!ccrm::parseCalibData(cameraSet, i, camera_pose, camData.K, camData.D))
        {
            log(logging::LogType::ERROR, "PenTrackingMulticamModule: failed to get camera calibration from message.");
            return false;
        }

        pu::SE3 T_world_cam = pu::SE3::fromQuatTvec(
            cv::Vec4d(
                camera_pose.qw,
                camera_pose.qx,
                camera_pose.qy,
                camera_pose.qz
            ),
            cv::Vec3d(
                camera_pose.x,
                camera_pose.y,
                camera_pose.z
            ),
            false
        );

        camData.T_cam_ref = T_world_cam.inverse();
    }
    out_camera_count = cameraSet.calibrated_count;

    return true;
}


bool PenTrackingMulticamModule::parseImageData(message::MessageHeader message, uint32_t expected_image_count)
{
    uint32_t image_count = 0;

    for (uint64_t blob_id = 0; blob_id < message.blob_count_; ++blob_id)
    {
        auto& blob = message.blobs_[blob_id];
        
        if (!blob.valid())
        {
            log(logging::LogType::ERROR, "PenTrackingMulticamModule: invalid blob in calibrated camera message.");
            return false;
        }

        std::byte* blob_data = reinterpret_cast<std::byte*>(blob.data());
        uint64_t blob_size = blob.size();

        if (!cm::isBlobValid(blob_data, blob_size))
        {
            log(logging::LogType::ERROR, "PenTrackingMulticamModule: invalid camera image blob data.");
            return false;
        }

        cm::BlobHeader blobHeader;
        if (!cm::readBlobHeader(blob_data, blob_size, blobHeader))
        {
            log(logging::LogType::ERROR, "PenTrackingMulticamModule: failed to read camera image blob header.");
            return false;
        }

        int mat_type, conv_type;
        if (blobHeader.format_ == cm::ImageFormat::BGR8)
        {
            mat_type = CV_8UC3;
            conv_type = cv::COLOR_BGR2GRAY;
        }
        else if (blobHeader.format_ == cm::ImageFormat::BGRA8)
        {
            mat_type = CV_8UC4;
            conv_type = cv::COLOR_BGRA2GRAY;
        }
        else
        {
            log(logging::LogType::ERROR, "PenTrackingMulticamModule: unsupported camera image format in blob.");
            return false;
        }

        

        for (uint32_t img_idx = 0; img_idx < blobHeader.image_count_; ++img_idx)
        {
            ++image_count;
            if (image_count > expected_image_count)
            {
                log(logging::LogType::ERROR, "PenTrackingMulticamModule: received more images than expected.");
                return false;
            }
            while (grayCameraImagesBuffer_.size() < image_count)
            {
                grayCameraImagesBuffer_.emplace_back();
            }

            cm::ImageHeader imageHeader;
            if (!cm::readImageHeader(blob_data, blob_size, img_idx, imageHeader))
            {
                log(logging::LogType::ERROR, "PenTrackingMulticamModule: failed to read camera image header.");
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
                grayCameraImagesBuffer_[image_count - 1],
                conv_type
            );
        }
    }
    
    return true;
}


pm::Pose toPenPose(const cv::Vec3d& t, const cv::Vec4d& q)
{
    return {
        .x = t[0],
        .y = t[1],
        .z = t[2],
        .qx = q[1],
        .qy = q[2],
        .qz = q[3],
        .qw = q[0]
    };
}


void PenTrackingMulticamModule::processMessage(
    uint32_t subscribe_consumer_id,
    ChannelIdentifier src,
    message::MessageHeader message) noexcept
{
    if (subscribe_consumer_id != subscribe_consumer_id_calibrated_camera_)
    {
        penVisualizationRemove();
        log(logging::LogType::ERROR, "PenTrackingMulticamModule: received message on unknown subscribe channel.");
        return;
    }

    uint32_t camera_count = 0;
    if (!parseCalibrationHeader(message, camera_count))
    {
        penVisualizationRemove();
        return;
    }

    if (!parseImageData(message, camera_count))
    {
        penVisualizationRemove();
        return;
    }

    while (markerDetectors_.size() < camera_count)
    {
        markerDetectors_.emplace_back(
            defaults::markerIdsToDetect,
            defaults::dictionary(),
            defaults::refineMode,
            defaults::windowSizeMultiple
        );
    }

    while (detectionsBuffer_.size() < camera_count)
    {
        detectionsBuffer_.emplace_back(MarkerDetector::DetectionResult::preallocate(10));
    }

    for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx)
    {
        markerDetectors_[cam_idx].detectMarkers(
            grayCameraImagesBuffer_[cam_idx],
            detectionsBuffer_[cam_idx]
        );
    }

    bool success = false;

    poseEstimator_.estimatePose(camerasDataBuffer_, detectionsBuffer_, poseEstimationResult_);
    if (poseEstimationResult_.success)
    {
        pu::SE3 T_ref_pose_initial = poseEstimationResult_.refinedPoseUsed ?
            poseEstimationResult_.refinedCandidatePosesPerCameraPerMarker[poseEstimationResult_.best_camera_index][poseEstimationResult_.best_marker_index].T_ref_pose :
            poseEstimationResult_.candidatePosesPerCameraPerMarker[poseEstimationResult_.best_camera_index][poseEstimationResult_.best_marker_index].T_ref_pose;

        poseOptimizer_.optimizePoses(
            camerasDataBuffer_,
            T_ref_pose_initial,
            detectionsBuffer_,
            poseOptimizationResult_
        );

        if (poseOptimizationResult_.success)
        {
            success = true;
        }
    }

    if (!success)
    {
        penVisualizationRemove();
        poseFilter_.updateLost();
        return;
    }

    pu::SE3 T_world_pen_filtered = poseFilter_.update(poseOptimizationResult_.T_ref_pose_optimized);
    pu::SE3 T_world_pen_tip = T_world_pen_filtered * T_pen_tip_;

    cv::Vec3d t_world_pen_tip;
    cv::Vec4d q_world_pen_tip; // qw, qx, qy, qz
    T_world_pen_tip.toQuatTvec(q_world_pen_tip, t_world_pen_tip, false);

    cv::Vec3d t_world_pen;
    cv::Vec4d q_world_pen; // qw, qx, qy, qz
    T_world_pen_filtered.toQuatTvec(q_world_pen, t_world_pen, false);
    

    PenButtonState::State button_state = pen_button_state_.readAndReset();


    pm::PenMessageRaw pen_raw_message(toPenPose(t_world_pen_tip, q_world_pen_tip), button_state.primary_down, button_state.secondary_down);
    sendMessage(publish_producer_id_pen_raw_, message::MessageHeader::Message(&pen_raw_message));

    // Update intent detector with pen pose
    if (pen_intent_detector_.valid())
    {
        pm::Pose pen_tip_pose = toPenPose(t_world_pen_tip, q_world_pen_tip);
        pen_intent_detector_.updatePenPose(pen_tip_pose);
    }

    if (!announced_visualization_)
    {
        std::lock_guard<std::mutex> lock(vis3d_mutex_);
        visualization_helper_->announce();
        announced_visualization_ = true;
    }
    penVisualizationUpdate(t_world_pen, q_world_pen, t_world_pen_tip, q_world_pen_tip);
}


aergo::module::ResponseData PenTrackingMulticamModule::processRequest(
    uint32_t response_producer_id,
    ChannelIdentifier source_channel,
    message::MessageHeader message) noexcept
{
    if (response_producer_id == response_producer_id_visualization_)
    {
        std::lock_guard<std::mutex> lock(vis3d_mutex_);
        return visualization_helper_->processVisualizationRequest(message);
    }

    return { .success_ = false };
}


void PenTrackingMulticamModule::penVisualizationRemove()
{
    std::lock_guard<std::mutex> lock(vis3d_mutex_);
    if (pen_displayed_)
    {
        visualization_helper_->removeObject(pen_object_id_);
        pen_displayed_ = false;
    }
    if (arrow_displayed_)
    {
        visualization_helper_->removeObject(arrow_object_id_);
        arrow_displayed_ = false;
    }
    if (pen_displayed_ || arrow_displayed_)
    {
        visualization_helper_->sendUpdate();
    }
}


void PenTrackingMulticamModule::penVisualizationUpdate(
    const cv::Vec3d& t_world_pen, const cv::Vec4d& q_world_pen, 
    const cv::Vec3d& t_world_arrow, const cv::Vec4d& q_world_arrow
)
{
    std::lock_guard<std::mutex> lock(vis3d_mutex_);

    vis3d::Pose pen_pose {
        .t = { static_cast<float>(t_world_pen[0]), static_cast<float>(t_world_pen[1]), static_cast<float>(t_world_pen[2]) },
        .q = { static_cast<float>(q_world_pen[1]), static_cast<float>(q_world_pen[2]), static_cast<float>(q_world_pen[3]), static_cast<float>(q_world_pen[0]) }
    };

    vis3d::Pose arrow_pose {
        .t = { static_cast<float>(t_world_arrow[0]), static_cast<float>(t_world_arrow[1]), static_cast<float>(t_world_arrow[2]) },
        .q = { static_cast<float>(q_world_arrow[1]), static_cast<float>(q_world_arrow[2]), static_cast<float>(q_world_arrow[3]), static_cast<float>(q_world_arrow[0]) }
    };

    if (!pen_displayed_)
    {
        if (visualization_helper_->addObject(pen_resource_id_, pen_pose, pen_object_id_))
        {
            pen_displayed_ = true;
        }
    }
    else
    {
        visualization_helper_->updateObject(pen_object_id_, pen_pose);
    }

    if (!arrow_displayed_)
    {
        if (visualization_helper_->addObject(arrow_resource_id_, arrow_pose, arrow_object_id_))
        {
            arrow_displayed_ = true;
        }
    }
    else
    {
        visualization_helper_->updateObject(arrow_object_id_, arrow_pose);
    }

    visualization_helper_->sendUpdate();
}