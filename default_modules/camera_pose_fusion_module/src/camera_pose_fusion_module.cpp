#include "camera_pose_fusion_module.h"

#include "module_helpers/robot_interface/message_types_definitions.h"
#include "module_helpers/serialization_helper/serialization_helper.h"

using namespace aergo::default_modules::camera_pose_fusion_module;
using namespace aergo::module;
namespace rc = aergo::module::helpers::robot_interface::robot_control;

CameraPoseFusionModule::CameraPoseFusionModule(
    const char* data_path,
    aergo::module::ICore* core,
    aergo::module::InputChannelMapInfo channel_map_info,
    const aergo::module::logging::ILogger* logger,
    uint64_t module_id,
    const aergo::module::ModuleInfo* module_info
)
    : BaseModule(data_path, core, channel_map_info, logger, module_id, module_info)
    , camera_consumer_id_(0)
    , robot_status_consumer_id_(0)
    , output_producer_id_(0)
{
    // Get camera consumer channel
    if (!getSubscribeChannelByName(cm::camera_image_consumer.channel_type_identifier_, camera_consumer_id_))
    {
        log(logging::LogType::ERROR, "CameraPoseFusionModule: Failed to get camera image subscribe channel.");
        return;
    }

    // Get robot status consumer channel
    if (!getSubscribeChannelByName(ri::robot_interface_status_consumer.channel_type_identifier_, robot_status_consumer_id_))
    {
        log(logging::LogType::ERROR, "CameraPoseFusionModule: Failed to get robot status subscribe channel.");
        return;
    }

    // Get output producer channel
    if (!getPublishChannelByName(camera_image_with_pose_producer.channel_type_identifier_, output_producer_id_))
    {
        log(logging::LogType::ERROR, "CameraPoseFusionModule: Failed to get output publish channel.");
        return;
    }

    valid_ = true;
}


void CameraPoseFusionModule::processMessage(
    uint32_t subscribe_consumer_id,
    aergo::module::ChannelIdentifier source_channel,
    aergo::module::message::MessageHeader message
) noexcept
{
    if (subscribe_consumer_id == camera_consumer_id_)
    {
        handleCameraMessage(message);
    }
    else if (subscribe_consumer_id == robot_status_consumer_id_)
    {
        handleRobotStatusMessage(message);
    }
}


void* CameraPoseFusionModule::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    return nullptr;
}


aergo::module::IModule::IngressDecision CameraPoseFusionModule::onIngress(
    ProcessingType kind,
    uint32_t local_channel_id,
    aergo::module::ChannelIdentifier src,
    const aergo::module::message::MessageHeader& msg,
    QueueStatus queue_status
) noexcept
{
    // Only accept messages on our two subscribe channels
    if (kind == ProcessingType::MESSAGE &&
        (local_channel_id == camera_consumer_id_ || local_channel_id == robot_status_consumer_id_))
    {
        return IngressDecision::ACCEPT;
    }
    return IngressDecision::DROP;
}




void CameraPoseFusionModule::handleRobotStatusMessage(aergo::module::message::MessageHeader message)
{
    // Deserialize status message header outside critical section
    ri::StatusMessage status_msg;
    if (!message.readAs(status_msg))
    {
        log(logging::LogType::WARNING, "CameraPoseFusionModule: Failed to deserialize robot status message header.");
        return;
    }

    // Only process ROBOT_CONTROL feature messages
    if (status_msg.feature != ri::RobotFeature::ROBOT_CONTROL)
    {
        return; // Silently ignore other features
    }

    // Validate blob
    if (message.blob_count_ != 1 || message.blobs_ == nullptr || !message.blobs_[0].valid())
    {
        log(logging::LogType::WARNING, "CameraPoseFusionModule: Robot status message missing data blob.");
        return;
    }

    // Deserialize status message blob outside critical section
    message::SharedDataBlob blob = message.blobs_[0];
    rc::BufferReader reader(blob.data(), blob.size());

    rc::status_messages::deserialization::StatusMessage status;
    if (!rc::status_messages::deserialization::deserializeStatusMessage(reader, status))
    {
        log(logging::LogType::WARNING, "CameraPoseFusionModule: Failed to deserialize robot status message.");
        return;
    }

    // Update pose in critical section (keep it short)
    {
        std::lock_guard<std::mutex> lock(pose_mutex_);
        current_flange_pose_ = status.flange_pose;
        pose_available_.store(true, std::memory_order_release);
    }
}


void CameraPoseFusionModule::handleCameraMessage(aergo::module::message::MessageHeader message)
{
    // Check if pose is available
    if (!pose_available_.load(std::memory_order_acquire))
    {
        return; // Skip until pose is available
    }

    // Read camera message header
    cm::CameraMessage camera_msg;
    if (!message.readAs(camera_msg))
    {
        log(logging::LogType::WARNING, "CameraPoseFusionModule: Failed to deserialize camera message header.");
        return;
    }

    // Get current pose (short critical section)
    rc::Pose flange_pose;
    {
        std::lock_guard<std::mutex> lock(pose_mutex_);
        flange_pose = current_flange_pose_;
    }

    // Create output message
    CameraImageWithPose output_msg;
    output_msg.camera_msg = camera_msg;
    output_msg.flange_pose = flange_pose;

    // Publish message with blobs (blobs remain valid for duration of send)
    if (message.blob_count_ > 0 && message.blobs_ != nullptr)
    {
        sendMessage(
            output_producer_id_,
            message::MessageHeader::Message(&output_msg, message.blobs_, message.blob_count_)
        );
    }
    else
    {
        sendMessage(
            output_producer_id_,
            message::MessageHeader::Message(&output_msg)
        );
    }
}

