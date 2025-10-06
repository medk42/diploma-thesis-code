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
        /// @param publish_channel Channel ID to publish scene messages on (must be a valid publish producer channel in base_module).
        VisualizationHelper(aergo::module::BaseModule* base_module, uint32_t publish_channel);

        /// @brief Register a resource description. Call once for each resource your module will use.
        /// Calling after announce() is NOT allowed.
        ResourceId registerResource(const ComplexShape& resource_description);

        // Call once when your module can start processing requests (e.g. at the end of your module constructor, or after activation if Activable).
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

        aergo::module::ResponseData processVisualizationRequest(aergo::module::message::MessageHeader message);

    private:
    
        aergo::module::BaseModule* base_module_{ nullptr };
        uint32_t publish_channel_{ 0 };

        aergo::module::BaseModule::AllocatorPtr dynamic_allocator_;

        uint32_t next_resource_id_{ 1 };
        uint32_t next_object_id_{ 1 };

        bool announced_{ false };
        CommandCoalescer cmd_coalescer_;

        std::map<ResourceId, ComplexShape> registered_resources_;
        std::map<ObjectId, ObjectData> existing_objects_;
        std::map<ObjectId, TrajectoryData> existing_trajectories_;
    };
}