#pragma once

#include "scene_desc_api.h"

#include "command_coalescer.h"
#include "module_common/base_module.h"

#include <map>

namespace aergo::module::helpers::visualization_3d_interface
{
    class VisualizationHelper
    {
    public:
        /// @brief Initialize the helper with base module and publish channel.
        /// This class is NOT async safe. Call with mutual exclusion if used from multiple threads.
        /// @param base_module Reference to the base module, so that messages can be sent (only used for publishing).
        VisualizationHelper(aergo::module::BaseModule* base_module);

        /// @brief Is the helper valid (was initialized correctly). Call after constructor to check.
        bool valid();

        /// @brief Register a resource description. Call once for each resource your module will use.
        /// Calling after announce() is NOT allowed.
        ResourceId registerResource(const ComplexShape& resource_description);

        /// @brief Call once when your module can start processing requests (e.g. at the end of your module constructor, or after activation if Activable).
        void announce();

        bool addObject(ResourceId resource_id, const Pose& pose, ObjectId& out_id);
        bool updateObject(ObjectId object_id, const Pose& pose);
        bool removeObject(ObjectId object_id);

        // Trajectories
        bool addTrajectory(const std::vector<Vec3>& pts, Color color, bool dashed, ObjectId& out_id);
        bool updateTrajectory(ObjectId trajectory_id, const std::vector<Vec3>& add_pts, uint32_t remove_from_head);
        bool removeTrajectory(ObjectId trajectory_id);

        /// @brief  Send an update message after making changes (add/update/remove). Allows batching multiple changes into one message.
        bool sendUpdate();

        /// @brief Call to process incoming requests on the response producer channel. Call from your module's processRequest() method.
        /// Response producer channel for this helper can be obtained with getResponseProducerChannel().
        aergo::module::ResponseData processVisualizationRequest(aergo::module::message::MessageHeader message);

        /// @brief Get the response producer channel ID used for visualization requests.
        uint32_t getResponseProducerChannel() const;

    private:
    
        aergo::module::BaseModule* base_module_{ nullptr };
        uint32_t publish_channel_{ 0 };
        uint32_t response_producer_channel_{ 0 };

        aergo::module::BaseModule::AllocatorPtr dynamic_allocator_;

        uint32_t next_resource_id_{ 1 };
        uint32_t next_object_id_{ 1 };

        bool valid_{ false };
        bool announced_{ false };
        CommandCoalescer cmd_coalescer_;

        std::map<ResourceId, ComplexShape> registered_resources_;
        std::map<ObjectId, ObjectData> existing_objects_;
        std::map<ObjectId, TrajectoryData> existing_trajectories_;
    };
}