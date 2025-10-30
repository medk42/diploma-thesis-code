#include "module_helpers/visualization_3d_interface/visualization_helper.h"

#include "module_helpers/visualization_3d_interface/message_types.h"
#include "module_helpers/visualization_3d_interface/serialization_helper.h"


using namespace aergo::module::helpers::visualization_3d_interface;



VisualizationHelper::VisualizationHelper(aergo::module::BaseModule* base_module)
: base_module_(base_module)
{
    dynamic_allocator_ = base_module_->createDynamicAllocator();
    if (!dynamic_allocator_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper: Failed to create dynamic allocator.");
        return;
    }
    
    if (!base_module_->getPublishChannelByName(visualization_3d_interface_publish_producer.channel_type_identifier_, publish_channel_))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper: Failed to find publish channel '" + std::string(visualization_3d_interface_publish_producer.channel_type_identifier_) + "'.");
        return;
    }

    if (!base_module_->getResponseChannelByName(visualization_3d_interface_response_producer.channel_type_identifier_, response_producer_channel_))
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper: Failed to find response producer channel '" + std::string(visualization_3d_interface_response_producer.channel_type_identifier_) + "'.");
        return;
    }

    valid_ = true;
}



ResourceId VisualizationHelper::registerResource(const ComplexShape& resource_description)
{
    if (announced_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper: Cannot register resource after announce() has been called.");
        return ResourceId{ 0 };
    }

    // Register resource
    ResourceId res_id{ next_resource_id_++ };
    registered_resources_[res_id] = resource_description;
    return res_id;
}



void VisualizationHelper::announce()
{
    if (announced_)
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "VisualizationHelper: announce() has already been called.");
        return;
    }

    PubType pub_type = PubType::ANNOUNCE;

    aergo::module::message::MessageHeader announce_msg{
        .data_ = reinterpret_cast<uint8_t*>(&pub_type),
        .data_len_ = sizeof(pub_type),
        .blobs_ = nullptr,
        .blob_count_ = 0
    };

    // Publish announce message
    base_module_->sendMessage(publish_channel_, announce_msg);

    announced_ = true;
}



bool VisualizationHelper::addObject(ResourceId resource_id, const Pose& pose, ObjectId& out_id)
{
    if (!announced_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper::addObject(): announce() has not been called yet.");
        return false;
    }

    if (registered_resources_.find(resource_id) == registered_resources_.end())
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "VisualizationHelper::addObject(): resource_id " + std::to_string(resource_id.id) + " not registered");
        return false;
    }

    ObjectId oid{ next_object_id_++ };
    cmd_coalescer_.addObject(resource_id, pose, oid);
    existing_objects_[oid] = { resource_id, pose };
    out_id = oid;
    return true;
}



bool VisualizationHelper::updateObject(ObjectId object_id, const Pose& pose)
{
    if (!announced_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper::updateObject(): announce() has not been called yet.");
        return false;
    }

    auto it = existing_objects_.find(object_id);
    if (it == existing_objects_.end())
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "VisualizationHelper::updateObject(): object_id " + std::to_string(object_id.id) + " not found");
        return false;
    }
    it->second = { it->second.resource_id, pose };

    cmd_coalescer_.updateObject(object_id, pose);
    return true;
}



bool VisualizationHelper::removeObject(ObjectId object_id)
{
    if (!announced_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper::removeObject(): announce() has not been called yet.");
        return false;
    }

    auto it = existing_objects_.find(object_id);
    if (it == existing_objects_.end())
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "VisualizationHelper::removeObject(): object_id " + std::to_string(object_id.id) + " not found");
        return false;
    }
    existing_objects_.erase(it);

    cmd_coalescer_.removeObject(object_id);
    return true;
}



bool VisualizationHelper::addTrajectory(const std::vector<Vec3>& pts, Color color, bool dashed, ObjectId& out_id)
{
    if ( !announced_ )
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper::addTrajectory(): announce() has not been called yet.");
        return false;
    }

    if (pts.size() < 2)
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "VisualizationHelper::addTrajectory(): need at least 2 points");
        return false;
    }

    ObjectId oid{ next_object_id_++ };
    cmd_coalescer_.addTrajectory(pts, color, dashed, oid);
    existing_trajectories_[oid] = { pts, color, dashed };
    out_id = oid;
    return true;
}



bool VisualizationHelper::updateTrajectory(ObjectId trajectory_id, const std::vector<Vec3>& add_pts, uint32_t remove_from_head)
{
    if (!announced_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper::updateTrajectory(): announce() has not been called yet.");
        return false;
    }

    auto it = existing_trajectories_.find(trajectory_id);
    if (it == existing_trajectories_.end())
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "VisualizationHelper::updateTrajectory(): trajectory_id " + std::to_string(trajectory_id.id) + " not found");
        return false;
    }

    // update existing points
    auto& [pts, color, dashed] = it->second;
    if (remove_from_head > pts.size())
        remove_from_head = pts.size();
    if (pts.size() - remove_from_head + add_pts.size() < 2)
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "VisualizationHelper::updateTrajectory(): need at least 2 points after update");
        return false; // need at least 2 points
    }

    pts.erase(pts.begin(), pts.begin() + remove_from_head);
    pts.insert(pts.end(), add_pts.begin(), add_pts.end());

    cmd_coalescer_.updateTrajectory(trajectory_id, add_pts, remove_from_head);
    return true;
}



bool VisualizationHelper::removeTrajectory(ObjectId trajectory_id)
{
    if (!announced_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper::removeTrajectory(): announce() has not been called yet.");
        return false;
    }
    
    auto it = existing_trajectories_.find(trajectory_id);
    if (it == existing_trajectories_.end())
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "VisualizationHelper::removeTrajectory(): trajectory_id " + std::to_string(trajectory_id.id) + " not found");
        return false;
    }
    existing_trajectories_.erase(it);

    cmd_coalescer_.removeTrajectory(trajectory_id);
    return true;
}



bool VisualizationHelper::sendUpdate()
{
    if (!announced_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper::sendUpdate(): announce() has not been called yet.");
        return false;
    }

    // Get command buffer
    CommandBuffer& cmd_buf = cmd_coalescer_.getBuffer();

    if (cmd_buf.isEmpty())
    {
        return true; // nothing to send
    }

    // Build update message
    std::vector<char> blob_data;
    serialization::pushObjectCommands(blob_data, cmd_buf.objects_);
    serialization::pushTrajectoryCommands(blob_data, cmd_buf.trajectories_);

    aergo::module::message::SharedDataBlob cmd_blob = dynamic_allocator_->allocate(blob_data.size());
    if (!cmd_blob.valid() || cmd_blob.data() == nullptr || cmd_blob.size() != blob_data.size())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper::sendUpdate(): Failed to allocate memory for command buffer blob.");
        return false; // allocation failed
    }
    std::memcpy(cmd_blob.data(), blob_data.data(), blob_data.size());

    PubType pub_type = PubType::UPDATE;

    aergo::module::message::MessageHeader update_msg{
        .data_ = reinterpret_cast<uint8_t*>(&pub_type),
        .data_len_ = sizeof(pub_type),
        .blobs_ = &cmd_blob,
        .blob_count_ = 1
    };


    // Publish update message
    base_module_->sendMessage(publish_channel_, update_msg);

    // Clear command buffer
    cmd_coalescer_.clearBuffer();

    return true;
}



aergo::module::ResponseData VisualizationHelper::processVisualizationRequest(aergo::module::message::MessageHeader message)
{
    if (!announced_)
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper: Received request before announce() was called.");
        return { .success_ = false };
    }

    ReqType req_type;
    if (!message.readAs<ReqType>(req_type))
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "VisualizationHelper: Received invalid request (bad data).");
        return { .success_ = false };
    }

    // Prepare response data based on request type
    std::vector<char> blob_data;
    if (req_type == ReqType::READ_FULL) // Full scene requested (all resources, all objects, all trajectories)
    {
        // Push registered resources
        serialization::pushPendingRegistration(blob_data, registered_resources_);
        // Push scene objects
        serialization::pushSceneObjects(blob_data, existing_objects_);
        // Push scene trajectories
        serialization::pushSceneTrajectories(blob_data, existing_trajectories_);
    }
    else if (req_type == ReqType::READ_SCENE)
    {
        // Push scene objects
        serialization::pushSceneObjects(blob_data, existing_objects_);
        // Push scene trajectories
        serialization::pushSceneTrajectories(blob_data, existing_trajectories_);
    }
    else
    {
        base_module_->log(aergo::module::logging::LogType::WARNING, "VisualizationHelper: Received invalid request (bad ReqType).");
        return { .success_ = false };
    }

    aergo::module::message::SharedDataBlob response_blob = dynamic_allocator_->allocate(blob_data.size());
    if (!response_blob.valid() || response_blob.data() == nullptr || response_blob.size() != blob_data.size())
    {
        base_module_->log(aergo::module::logging::LogType::ERROR, "VisualizationHelper: Failed to allocate memory for response blob.");
        return { .success_ = false }; // allocation failed
    }

    std::memcpy(response_blob.data(), blob_data.data(), blob_data.size());

    // Return response
    aergo::module::ResponseData response{ .success_ = true };
    response.data_ = std::vector<uint8_t>(message.data_, message.data_ + message.data_len_);
    response.blobs_.push_back(response_blob);

    return response;
}



uint32_t VisualizationHelper::getResponseProducerChannel() const
{
    return response_producer_channel_;
}



bool VisualizationHelper::valid()
{
    return valid_;
}