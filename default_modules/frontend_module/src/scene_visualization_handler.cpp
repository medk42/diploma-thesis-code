#include "webapp/ui/helper/scene_visualization_handler.h"

#include "module_helpers/visualization_3d_interface/message_types.h"
#include "module_helpers/visualization_3d_interface/serialization_helper.h"

#undef ERROR // Gotta love Windows


using namespace aergo::module;
using namespace aergo::default_modules::frontend_module::webapp::ui::helper;
namespace vis3d = aergo::module::helpers::visualization_3d_interface;


SceneVisualizationHandler::SceneVisualizationHandler(BaseModule* base_module)
: base_module_(base_module)
{
    if (!base_module->getSubscribeChannelByName(vis3d::visualization_3d_interface_publish_producer.channel_type_identifier_, scene_subscribe_channel_id_))
    {
        base_module->log(logging::LogType::ERROR, "Frontend module requires one subscribe channel for 3D visualization data");
        return;
    }

    if (!base_module->getRequestChannelByName(vis3d::visualization_3d_interface_request_consumer.channel_type_identifier_, scene_request_channel_id_))
    {
        base_module->log(logging::LogType::ERROR, "Frontend module requires one request channel for 3D visualization requests");
        return;
    }

    reload(); // request data from all existing modules

    valid_ = true;

}



void SceneVisualizationHandler::processVisualizationResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (!valid_)
        return;

    if (request_consumer_id != scene_request_channel_id_)
    {
        base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: received response on unknown request_consumer_id: " + std::to_string(request_consumer_id) + ", expected: " + std::to_string(scene_request_channel_id_));
        return; // not our channel
    }

    if (!message.success_)
    {
        base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: received failed response from module " + std::to_string(source_channel.producer_module_id_) + ", removing from awaiting list");
        awaiting_full_read_.erase(source_channel.producer_module_id_);
        return;
    }

    if (message.data_ == nullptr || message.data_len_ != sizeof(vis3d::ReqType))
    {
        base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: received response with invalid data");
        return;
    }


    vis3d::ReqType req_type = *reinterpret_cast<vis3d::ReqType*>(message.data_);

    if (message.blob_count_ != 1 || message.blobs_ == nullptr || !message.blobs_[0].valid())
    {
        base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: received response with invalid blobs");
        return;
    }

    message::SharedDataBlob blob = message.blobs_[0];
    vis3d::deserialization::BufferReader reader(reinterpret_cast<const char*>(blob.data()), blob.size());


    if (req_type == vis3d::ReqType::READ_FULL)
    {
        if (awaiting_full_read_.find(source_channel.producer_module_id_) == awaiting_full_read_.end())
        {
            base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: received unexpected READ_FULL response from module " + std::to_string(source_channel.producer_module_id_));
        }
        else
        {
            awaiting_full_read_.erase(source_channel.producer_module_id_);
        }

        if (module_registered_resources_.find(source_channel.producer_module_id_) != module_registered_resources_.end())
        {
            base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: received READ_FULL response from module " + std::to_string(source_channel.producer_module_id_) + " that already sent registrations, ignoring registrations");
        }
        else
        {
            // Read registrations only once per module, map them internally and forward to scene container
            std::map<vis3d::ResourceId, vis3d::ComplexShape> registered_resources_;
            if (!vis3d::deserialization::readPendingRegistration(reader, registered_resources_))
            {
                base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: failed to read registrations from response");
                return;
            }

            // Ensure we have an entry for this module, even if no resources were registered (this avoids re-requesting registrations)
            module_registered_resources_[source_channel.producer_module_id_];
            module_existing_objects_[source_channel.producer_module_id_];
            module_existing_trajectories_[source_channel.producer_module_id_];

            for (const auto& [res_id, shape] : registered_resources_)
            {
                module_registered_resources_[source_channel.producer_module_id_][res_id] = RegisteredResource{shape, std::nullopt};
                if (scene_container_)
                {
                    auto local_id = scene_container_->createObjectDescription(shape);
                    module_registered_resources_[source_channel.producer_module_id_][res_id].local_id = local_id;   
                }
            }
        }
    }

    if (req_type == vis3d::ReqType::READ_FULL || req_type == vis3d::ReqType::READ_SCENE)
    {
        // Read scene objects and trajectories
        std::map<vis3d::ObjectId, vis3d::ObjectData> scene_objects_;
        std::map<vis3d::ObjectId, vis3d::TrajectoryData> scene_trajectories_;

        if (!vis3d::deserialization::readSceneObjects(reader, scene_objects_))
        {
            base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: failed to read scene objects from response");
            return;
        }
        if (!vis3d::deserialization::readSceneTrajectories(reader, scene_trajectories_))
        {
            base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: failed to read scene trajectories from response");
            return;
        }


        // Find registered resources for this module, send READ_FULL if we don't have any
        auto res_it = module_registered_resources_.find(source_channel.producer_module_id_);
        if (res_it == module_registered_resources_.end())
        {
            base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: received response from unknown module " + std::to_string(source_channel.producer_module_id_) + ", sending READ_FULL request");
            sendReadFullRequest(source_channel.producer_module_id_);
            return;
        }
        auto& module_resource_map = res_it->second;


        // Clear existing objects and trajectories for this module
        auto& existing_objects = module_existing_objects_[source_channel.producer_module_id_];
        auto& existing_trajectories = module_existing_trajectories_[source_channel.producer_module_id_];

        if (scene_container_)
        {
            // Remove old objects and trajectories from scene container
            for (const auto& [obj_id, obj] : existing_objects)
            {
                if (obj.local_id.has_value())
                {
                    scene_container_->removeObject(obj.local_id.value());
                }
            }
            for (const auto& [traj_id, traj] : existing_trajectories)
            {
                if (traj.local_id.has_value())
                {
                    scene_container_->removeTrajectory(traj.local_id.value());
                }
            }
        }

        existing_objects.clear();
        existing_trajectories.clear();


        // Add new objects and trajectories
        for (const auto& [obj_id, obj_data] : scene_objects_)
        {
            // Check if resource is registered
            auto res_it2 = module_resource_map.find(obj_data.resource_id);
            if (res_it2 == module_resource_map.end())
            {
                base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: received object with unregistered resource from module " + std::to_string(source_channel.producer_module_id_) + ", resource ID: " + std::to_string(obj_data.resource_id.id));
                continue;
            }
            auto& registered_resource = res_it2->second;

            existing_objects[obj_id] = ExistingObject{obj_data.resource_id, obj_data.pose, std::nullopt};
            if (scene_container_)
            {
                if (!registered_resource.local_id.has_value())
                {
                    base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: resource was not registered in scene container yet, registering now, resource ID: " + std::to_string(obj_data.resource_id.id));
                    // Resource was not registered in scene container yet, do it now
                    registered_resource.local_id = scene_container_->createObjectDescription(registered_resource.shape);
                }

                vis3d::ObjectId local_obj_id;
                if (!scene_container_->addObject(registered_resource.local_id.value(), obj_data.pose, local_obj_id))
                {
                    base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: failed to add object to scene container, resource ID: " + std::to_string(obj_data.resource_id.id));
                    continue;
                }
                else
                {
                    existing_objects[obj_id].local_id = local_obj_id;
                }
            }
        }
        
        for (const auto& [traj_id, traj_data] : scene_trajectories_)
        {
            existing_trajectories[traj_id] = ExistingTrajectory{traj_data.points, traj_data.color, traj_data.dashed, std::nullopt};
            if (scene_container_)
            {
                vis3d::ObjectId local_traj_id;
                if (!scene_container_->addTrajectory(traj_data.points, traj_data.color, traj_data.dashed, local_traj_id))
                {
                    base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: failed to add trajectory to scene container");
                    continue;
                }
                else
                {
                    existing_trajectories[traj_id].local_id = local_traj_id;
                }
            }
        }
    }
    else
    {
        base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processVisualizationResponse: received response with unknown ReqType");
        return;
    }
}



void SceneVisualizationHandler::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (!valid_)
        return;

    if (subscribe_consumer_id != scene_subscribe_channel_id_)
    {
        base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processMessage: received message on unknown subscribe_consumer_id: " + std::to_string(subscribe_consumer_id) + ", expected: " + std::to_string(scene_subscribe_channel_id_));
        return; // not our channel
    }

    if (message.data_ == nullptr || message.data_len_ != sizeof(vis3d::PubType))
    {
        base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processMessage: received message with invalid data");
        return;
    }

    vis3d::PubType pub_type = *reinterpret_cast<vis3d::PubType*>(message.data_);

    if (pub_type == vis3d::PubType::ANNOUNCE)
    {
        // New module, request full scene (with registrations)
        if (module_registered_resources_.find(source_channel.producer_module_id_) == module_registered_resources_.end())
        {
            sendReadFullRequest(source_channel.producer_module_id_);
        }
        // Otherwise ignore, we already know this module
    }
    else if (pub_type == vis3d::PubType::UPDATE)
    {
        if (module_registered_resources_.find(source_channel.producer_module_id_) == module_registered_resources_.end())
        {
            base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processMessage: received UPDATE message from unknown module " + std::to_string(source_channel.producer_module_id_) + ", sending READ_FULL request");
            sendReadFullRequest(source_channel.producer_module_id_);
            return;
        }

        if (message.blob_count_ != 1 || message.blobs_ == nullptr || !message.blobs_[0].valid())
        {
            base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processMessage: received message with invalid blobs");
            return;
        }

        message::SharedDataBlob blob = message.blobs_[0];
        vis3d::deserialization::BufferReader reader(reinterpret_cast<const char*>(blob.data()), blob.size());

        std::map<vis3d::ObjectId, vis3d::CommandBuffer::ObjectParameters> object_commands;
        std::map<vis3d::ObjectId, vis3d::CommandBuffer::TrajectoryParameters> trajectory_commands;

        if (!vis3d::deserialization::readObjectCommands(reader, object_commands))
        {
            base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processMessage: failed to read object commands from message");
            return;
        }
        if (!vis3d::deserialization::readTrajectoryCommands(reader, trajectory_commands))
        {
            base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processMessage: failed to read trajectory commands from message");
            return;
        }

        processUpdate(source_channel.producer_module_id_, object_commands, trajectory_commands);
    }
    else
    {
        base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processMessage: received message with unknown PubType");
        return;
    }
}



void SceneVisualizationHandler::sendReadFullRequest(uint64_t module_id)
{
    if (awaiting_full_read_.find(module_id) != awaiting_full_read_.end())
    {
        // Already requested full read from this module, wait for response
        return;
    }

    auto available_scene_visualization_channels = getAllSceneVisualizationRequestChannels();
    auto it = std::find_if(
        available_scene_visualization_channels.begin(), 
        available_scene_visualization_channels.end(),
        [module_id](const ChannelIdentifier& ch_id) {
            return ch_id.producer_module_id_ == module_id;
        }
    );

    if (it == available_scene_visualization_channels.end())
    {
        base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::sendReadFullRequest: no response channel found for module " + std::to_string(module_id));
        return;
    }

    sendReadFullRequest(*it);
}



void SceneVisualizationHandler::sendReadFullRequest(ChannelIdentifier target_channel)
{
    vis3d::ReqType request = vis3d::ReqType::READ_FULL;
    message::MessageHeader msg {
        .data_ = reinterpret_cast<uint8_t*>(&request),
        .data_len_ = sizeof(request),
        .blobs_ = nullptr,
        .blob_count_ = 0
    };

    awaiting_full_read_.insert(target_channel.producer_module_id_);

    base_module_->sendRequest(scene_request_channel_id_, target_channel, msg);
}



void SceneVisualizationHandler::processUpdate(
    uint64_t module_id,
    std::map<vis3d::ObjectId, vis3d::CommandBuffer::ObjectParameters>& object_commands, 
    std::map<vis3d::ObjectId, vis3d::CommandBuffer::TrajectoryParameters>& trajectory_commands
)
{
    auto& registered_resources = module_registered_resources_[module_id];
    auto& existing_objects = module_existing_objects_[module_id];
    auto& existing_trajectories = module_existing_trajectories_[module_id];

    for (const auto& [obj_id, obj_cmd] : object_commands)
    {
        if (obj_cmd.action == vis3d::CommandBuffer::Action::ADD)
        {
            if (registered_resources.find(obj_cmd.resource_id) == registered_resources.end())
            {
                base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: received ADD command for object with unregistered resource from module " + std::to_string(module_id) + ", resource ID: " + std::to_string(obj_cmd.resource_id.id));
                continue;
            }
            auto& registered_resource = registered_resources[obj_cmd.resource_id];

            if (existing_objects.find(obj_id) != existing_objects.end())
            {
                base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: received ADD command for already existing object ID " + std::to_string(obj_id.id) + " from module " + std::to_string(module_id));
                continue;
            }

            existing_objects[obj_id] = ExistingObject{obj_cmd.resource_id, obj_cmd.pose, std::nullopt};
            if (scene_container_)
            {
                if (!registered_resource.local_id.has_value())
                {
                    base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: resource was not registered in scene container yet, registering now, resource ID: " + std::to_string(obj_cmd.resource_id.id));
                    // Resource was not registered in scene container yet, do it now
                    registered_resource.local_id = scene_container_->createObjectDescription(registered_resource.shape);
                }

                vis3d::ObjectId local_obj_id;
                if (!scene_container_->addObject(registered_resource.local_id.value(), obj_cmd.pose, local_obj_id))
                {
                    base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: failed to add object to scene container, resource ID: " + std::to_string(obj_cmd.resource_id.id));
                    continue;
                }
                else
                {
                    existing_objects[obj_id].local_id = local_obj_id;
                }
            }
        }
        else if (obj_cmd.action == vis3d::CommandBuffer::Action::UPDATE)
        {
            auto it = existing_objects.find(obj_id);
            if (it == existing_objects.end())
            {
                base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: received UPDATE command for unknown object ID " + std::to_string(obj_id.id) + " from module " + std::to_string(module_id));
                continue;
            }

            it->second.pose = obj_cmd.pose;

            if (scene_container_ && it->second.local_id.has_value())
            {
                scene_container_->updateObject(it->second.local_id.value(), obj_cmd.pose);
            }
        }
        else if (obj_cmd.action == vis3d::CommandBuffer::Action::REMOVE)
        {
            auto it = existing_objects.find(obj_id);
            if (it == existing_objects.end())
            {
                base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: received REMOVE command for unknown object ID " + std::to_string(obj_id.id) + " from module " + std::to_string(module_id));
                continue;
            }

            if (scene_container_ && it->second.local_id.has_value())
            {
                scene_container_->removeObject(it->second.local_id.value());
            }

            existing_objects.erase(it);
        }
        else 
        {
            base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: received command with unknown action " + std::to_string((uint8_t)obj_cmd.action) + " for object ID " + std::to_string(obj_id.id) + " from module " + std::to_string(module_id));
            continue;
        }
    }

    for (const auto& [traj_id, traj_cmd] : trajectory_commands)
    {
        if (traj_cmd.action == vis3d::CommandBuffer::Action::ADD)
        {
            if (existing_trajectories.find(traj_id) != existing_trajectories.end())
            {
                base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: received ADD command for already existing trajectory ID " + std::to_string(traj_id.id) + " from module " + std::to_string(module_id));
                continue;
            }

            existing_trajectories[traj_id] = ExistingTrajectory{traj_cmd.points, traj_cmd.color, traj_cmd.dashed, std::nullopt};
            if (scene_container_)
            {
                vis3d::ObjectId local_traj_id;
                if (!scene_container_->addTrajectory(traj_cmd.points, traj_cmd.color, traj_cmd.dashed, local_traj_id))
                {
                    base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: failed to add trajectory to scene container");
                    continue;
                }
                else
                {
                    existing_trajectories[traj_id].local_id = local_traj_id;
                }
            }
        }
        else if (traj_cmd.action == vis3d::CommandBuffer::Action::UPDATE)
        {
            auto it = existing_trajectories.find(traj_id);
            if (it == existing_trajectories.end())
            {
                base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: received UPDATE command for unknown trajectory ID " + std::to_string(traj_id.id) + " from module " + std::to_string(module_id));
                continue;
            }

            it->second.points.erase(it->second.points.begin(), it->second.points.begin() + std::min((size_t)traj_cmd.remove_from_head, it->second.points.size()));
            it->second.points.insert(it->second.points.end(), traj_cmd.points.begin(), traj_cmd.points.end());

            if (scene_container_ && it->second.local_id.has_value())
            {
                scene_container_->updateTrajectory(it->second.local_id.value(), traj_cmd.points, traj_cmd.remove_from_head);
            }
        }
        else if (traj_cmd.action == vis3d::CommandBuffer::Action::REMOVE)
        {
            auto it = existing_trajectories.find(traj_id);
            if (it == existing_trajectories.end())
            {
                base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: received REMOVE command for unknown trajectory ID " + std::to_string(traj_id.id) + " from module " + std::to_string(module_id));
                continue;
            }

            if (scene_container_ && it->second.local_id.has_value())
            {
                scene_container_->removeTrajectory(it->second.local_id.value());
            }

            existing_trajectories.erase(it);
        }
        else 
        {
            base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::processUpdate: received command with unknown action " + std::to_string((uint8_t)traj_cmd.action) + " for trajectory ID " + std::to_string(traj_id.id) + " from module " + std::to_string(module_id));
            continue;
        }
    }
}



void SceneVisualizationHandler::setSceneContainer(SceneContainer* container, bool fill_existing)
{
    if (!valid_)
        return;

    scene_container_ = container;
    if (!fill_existing)
        return;

    // Fill the new scene container with existing data / or clear local IDs if no scene container is set
    for (auto& [module_id, resource_map] : module_registered_resources_)
    {
        for (auto& [res_id, reg_res] : resource_map)
        {
            reg_res.local_id = std::nullopt;
            if (scene_container_)
            {
                reg_res.local_id = scene_container_->createObjectDescription(reg_res.shape);
            }
        }
    }

    for (auto& [module_id, object_map] : module_existing_objects_)
    {
        for (auto& [obj_id, obj] : object_map)
        {
            obj.local_id = std::nullopt;
            if (scene_container_)
            {
                auto res_it = module_registered_resources_.find(module_id);
                if (res_it == module_registered_resources_.end())
                {
                    base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::setSceneContainer: existing object from unknown module " + std::to_string(module_id) + ", cannot add to scene container");
                    continue;
                }
                auto& module_resource_map = res_it->second;

                auto res_it2 = module_resource_map.find(obj.resource_id);
                if (res_it2 == module_resource_map.end())
                {
                    base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::setSceneContainer: existing object with unregistered resource from module " + std::to_string(module_id) + ", resource ID: " + std::to_string(obj.resource_id.id) + ", cannot add to scene container");
                    continue;
                }
                auto& registered_resource = res_it2->second;

                if (!registered_resource.local_id.has_value())
                {
                    base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::setSceneContainer: resource was not registered in scene container yet, registering now, resource ID: " + std::to_string(obj.resource_id.id));
                    // Resource was not registered in scene container yet, do it now
                    registered_resource.local_id = scene_container_->createObjectDescription(registered_resource.shape);
                }

                vis3d::ObjectId local_obj_id;
                if (!scene_container_->addObject(registered_resource.local_id.value(), obj.pose, local_obj_id))
                {
                    base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::setSceneContainer: failed to add existing object to scene container, resource ID: " + std::to_string(obj.resource_id.id));
                    continue;
                }
                else
                {
                    obj.local_id = local_obj_id;
                }
            }
        }
    }

    for (auto& [module_id, traj_map] : module_existing_trajectories_)
    {
        for (auto& [traj_id, traj] : traj_map)
        {
            traj.local_id = std::nullopt;
            if (scene_container_)
            {
                vis3d::ObjectId local_traj_id;
                if (!scene_container_->addTrajectory(traj.points, traj.color, traj.dashed, local_traj_id))
                {
                    base_module_->log(logging::LogType::WARNING, "SceneVisualizationHandler::setSceneContainer: failed to add existing trajectory to scene container");
                    continue;
                }
                else
                {
                    traj.local_id = local_traj_id;
                }
            }
        }
    }
}



void SceneVisualizationHandler::reload()
{
    if (!valid_)
        return;

    // Clear existing data from scene container
    if (scene_container_)
    {
        for (auto& [module_id, object_map] : module_existing_objects_)
        {
            for (auto& [obj_id, obj] : object_map)
            {
                if (obj.local_id.has_value())
                {
                    scene_container_->removeObject(obj.local_id.value());
                }
            }
        }
        for (auto& [module_id, traj_map] : module_existing_trajectories_)
        {
            for (auto& [traj_id, traj] : traj_map)
            {
                if (traj.local_id.has_value())
                {
                    scene_container_->removeTrajectory(traj.local_id.value());
                }
            }
        }
    }

    module_registered_resources_.clear();
    module_existing_objects_.clear();
    module_existing_trajectories_.clear();
    awaiting_full_read_.clear();

    // Request full data from all modules
    std::vector<ChannelIdentifier> available_visualization_request_channels = getAllSceneVisualizationRequestChannels();
    for (auto request_channel : available_visualization_request_channels)
    {
        sendReadFullRequest(request_channel);
    }
}



std::vector<ChannelIdentifier> SceneVisualizationHandler::getAllSceneVisualizationRequestChannels()
{
    auto buf = base_module_->getCoreControl()->getExistingResponseChannelsByName(
        vis3d::visualization_3d_interface_response_producer.channel_type_identifier_
    );

    if (!buf.valid() || buf.data() == nullptr) {
        base_module_->log(logging::LogType::ERROR, "SceneVisualizationHandler::getAllSceneVisualizationRequestChannels: no data");
        return {};
    }

    const std::size_t byte_count = buf.size(); // bytes
    const std::byte*  bytes      = reinterpret_cast<const std::byte*>(buf.data());

    // Need at least the leading u64
    if (byte_count < sizeof(std::uint64_t)) {
        base_module_->log(logging::LogType::ERROR, "SceneVisualizationHandler::getAllSceneVisualizationRequestChannels: buffer too small for header");
        return {};
    }

    std::uint64_t channel_count = 0;
    std::memcpy(&channel_count, bytes, sizeof(channel_count)); // avoids alignment issues

    if (byte_count != sizeof(std::uint64_t) + channel_count * sizeof(ChannelIdentifier)) {
        base_module_->log(logging::LogType::ERROR, "SceneVisualizationHandler::getAllSceneVisualizationRequestChannels: invalid buffer size");
        return {};
    }

    std::vector<ChannelIdentifier> out;
    out.resize(static_cast<std::size_t>(channel_count));

    // Copy payload
    std::memcpy(out.data(), bytes + sizeof(std::uint64_t), out.size() * sizeof(ChannelIdentifier));

    return out;
}
