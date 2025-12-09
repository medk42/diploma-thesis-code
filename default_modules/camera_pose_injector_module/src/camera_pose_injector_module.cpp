#include "camera_pose_injector_module.h"

#include "module_helpers/robot_interface/message_types.h"
#include "module_helpers/robot_interface/message_types_definitions.h"
#include "module_helpers/robot_interface/features/robot_control/messages.h"

using namespace aergo::default_modules::camera_pose_injector_module;
using namespace aergo::module;
namespace cph = aergo::module::helpers::camera_pose_helper;

CameraPoseInjectorModule::CameraPoseInjectorModule(
    const char* data_path,
    aergo::module::ICore* core,
    aergo::module::InputChannelMapInfo channel_map_info,
    const aergo::module::logging::ILogger* logger,
    uint64_t module_id,
    const aergo::module::ModuleInfo* module_info)
: aergo::module::BaseModule(data_path, core, channel_map_info, logger, module_id, module_info)
{
    if (!getSubscribeChannelByName(cm::camera_image_consumer.channel_type_identifier_, camera_input_channel_))
    {
        log(logging::LogType::ERROR, "CameraPoseInjector: failed to resolve camera subscribe channel.");
        return;
    }

    if (!getSubscribeChannelByName(ri::robot_interface_status_consumer.channel_type_identifier_, robot_status_channel_))
    {
        log(logging::LogType::ERROR, "CameraPoseInjector: failed to resolve robot status subscribe channel.");
        return;
    }

    if (!getPublishChannelByName(cph::camera_with_pose_publish_producer.channel_type_identifier_, publish_channel_))
    {
        log(logging::LogType::ERROR, "CameraPoseInjector: failed to resolve publish channel.");
        return;
    }

    valid_ = true;
}


void* CameraPoseInjectorModule::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    return nullptr;
}


IModule::IngressDecision CameraPoseInjectorModule::onIngress(
    ProcessingType kind,
    uint32_t local_channel_id,
    aergo::module::ChannelIdentifier /*src*/,
    const aergo::module::message::MessageHeader& /*msg*/,
    QueueStatus queue_status) noexcept
{
    if (kind != ProcessingType::MESSAGE)
    {
        return IngressDecision::DROP;
    }

    if (local_channel_id == camera_input_channel_)
    {
        // keep only the latest camera frame in the queue
        if (queue_status == QueueStatus::QUEUE_FULL)
        {
            return IngressDecision::DROP;
        }
        return IngressDecision::ACCEPT_REPLACE_QUEUE;
    }

    if (local_channel_id == robot_status_channel_)
    {
        if (queue_status == QueueStatus::QUEUE_FULL)
        {
            return IngressDecision::DROP;
        }
        if (queue_status == QueueStatus::QUEUE_FULL_CAN_DROP)
        {
            return IngressDecision::ACCEPT_DROP_QUEUE_FIRST;
        }
        return IngressDecision::ACCEPT;
    }

    return IngressDecision::DROP;
}


void CameraPoseInjectorModule::processMessage(
    uint32_t subscribe_consumer_id,
    aergo::module::ChannelIdentifier /*source_channel*/,
    aergo::module::message::MessageHeader message) noexcept
{
    if (subscribe_consumer_id == camera_input_channel_)
    {
        handleCameraMessage(message);
    }
    else if (subscribe_consumer_id == robot_status_channel_)
    {
        handleRobotStatus(message);
    }
    else
    {
        log(logging::LogType::WARNING, "CameraPoseInjector: received message on unknown subscribe channel, dropping.");
    }
}


void CameraPoseInjectorModule::handleRobotStatus(const aergo::module::message::MessageHeader& message) noexcept
{
    ri::StatusMessage status_msg;
    if (!message.readAs(status_msg))
    {
        log(logging::LogType::WARNING, "CameraPoseInjector: failed to deserialize robot status header.");
        return;
    }

    if (status_msg.feature != ri::RobotFeature::ROBOT_CONTROL)
    {
        // accept but ignore status messages for other features
        return;
    }

    if (message.blob_count_ != 1 || message.blobs_ == nullptr || !message.blobs_[0].valid())
    {
        log(logging::LogType::WARNING, "CameraPoseInjector: robot status message missing data blob.");
        return;
    }

    message::SharedDataBlob blob = message.blobs_[0];
    rc::BufferReader reader(blob.data(), blob.size());

    rc::status_messages::deserialization::StatusMessage status{};
    if (!rc::status_messages::deserialization::deserializeStatusMessage(reader, status))
    {
        log(logging::LogType::WARNING, "CameraPoseInjector: failed to deserialize robot control status message.");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(pose_mutex_);
        latest_flange_pose_ = status.flange_pose;
    }
}


void CameraPoseInjectorModule::handleCameraMessage(const aergo::module::message::MessageHeader& message) noexcept
{
    std::optional<rc::Pose> pose_opt;
    {
        std::lock_guard<std::mutex> lock(pose_mutex_);
        pose_opt = latest_flange_pose_;
    }

    if (!pose_opt.has_value())
    {
        // No robot pose yet; drop the frame.
        return;
    }

    cm::CameraMessage camera_header{};
    if (!message.readAs(camera_header))
    {
        log(logging::LogType::WARNING, "CameraPoseInjector: failed to read camera message header.");
        return;
    }

    cph::CameraWithFlangePose fused_message{
        .camera_header = camera_header,
        .flange_pose = pose_opt.value()
    };

    message::MessageHeader out_header = message::MessageHeader::Message(&fused_message, message.blobs_, message.blob_count_);

    sendMessage(publish_channel_, out_header);
}


ISerializableModule::SaveData CameraPoseInjectorModule::save() noexcept
{
    ISerializableModule::SaveData data;
    data.supports_saving_ = false;
    return data;
}


bool CameraPoseInjectorModule::load(ISerializableModule::SaveData /*data*/) noexcept
{
    // stateless; nothing to load
    return true;
}
