#pragma once

#include "module_common/module_interface_.h"
#include "module_common/base_module.h"
#include "scene_container.h"

#include <set>
#include <map>
#include <optional>

namespace aergo::default_modules::frontend_module::webapp::ui::helper
{
    class SceneVisualizationHandler
    {
    public:
        SceneVisualizationHandler(aergo::module::BaseModule* base_module);

        void processVisualizationResponse(uint32_t request_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept;
        void processMessage(uint32_t subscribe_consumer_id, aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept;

        /// @brief Set the scene container to use for visualization. Messages from modules will be forwarded to this container
        /// after translating resource and object IDs. 
        /// If fill_existing is true, all existing resources, objects and trajectories will be added to the scene container
        /// (useful when setting up a new scene container, e.g. after a web client re-connects).
        void setSceneContainer(SceneContainer* scene_container, bool fill_existing);

        /// @brief Clear the visualization handler, request registration and scene data from all existing modules again. 
        void reload();

        uint32_t getSceneSubscribeChannelId() const { return scene_subscribe_channel_id_; }
        uint32_t getSceneRequestChannelId() const { return scene_request_channel_id_; }
        bool valid() const { return valid_; }

    private:
        struct RegisteredResource
        {
            vis3d::ComplexShape shape;
            std::optional<vis3d::ResourceId> local_id; // ID in the scene container
        };

        struct ExistingObject
        {
            vis3d::ResourceId resource_id;
            vis3d::Pose pose;
            std::optional<vis3d::ObjectId> local_id; // ID in the scene container
        };

        struct ExistingTrajectory
        {
            std::vector<vis3d::Vec3> points;
            vis3d::Color color;
            bool dashed;
            std::optional<vis3d::ObjectId> local_id; // ID in the scene container
        };

        void sendReadFullRequest(uint64_t module_id);
        void sendReadFullRequest(aergo::module::ChannelIdentifier target_channel);
        void processUpdate(
            uint64_t module_id,
            std::map<vis3d::ObjectId, vis3d::CommandBuffer::ObjectParameters>& object_commands, 
            std::map<vis3d::ObjectId, vis3d::CommandBuffer::TrajectoryParameters>& trajectory_commands
        );
        std::vector<aergo::module::ChannelIdentifier> getAllSceneVisualizationRequestChannels();

        aergo::module::BaseModule* base_module_{nullptr};
        uint32_t scene_subscribe_channel_id_ { 0 }; // ID of subscribe consumer channel for 3D visualization data
        uint32_t scene_request_channel_id_ { 0 };   // ID of request consumer channel for 3D visualization requests
        bool valid_ { false }; // was initialization successfully (found the required channels)

        SceneContainer* scene_container_{nullptr};

        std::set<uint64_t> awaiting_full_read_; // module IDs from which we requested full data, waiting for response
        std::map<uint64_t, std::map<vis3d::ResourceId, RegisteredResource>> module_registered_resources_; // per-module registered resources
        std::map<uint64_t, std::map<vis3d::ObjectId, ExistingObject>> module_existing_objects_; // per-module existing objects
        std::map<uint64_t, std::map<vis3d::ObjectId, ExistingTrajectory>> module_existing_trajectories_; // per-module existing trajectories
    };
}