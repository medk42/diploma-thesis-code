#pragma once

#include "scene_desc_api.h"
#include "command_buffer.h"

namespace aergo::module::helpers::visualization_3d_interface
{
    class CommandCoalescer
    {
    public:

        void enableGrid(bool on);

        // Static resource registry
        void createObjectDescription(ResourceId resource_id, const ComplexShape& s);

        // Instances (object_id)
        void addObject(ResourceId resource_id, const Pose& pose, ObjectId object_id);
        void updateObject(ObjectId object_id, const Pose& pose);
        void removeObject(ObjectId object_id);

        // Trajectories
        void addTrajectory(const std::vector<Vec3>& pts, Color color, bool dashed, ObjectId trajectory_id);
        void updateTrajectory(ObjectId trajectory_id, const std::vector<Vec3>& add_pts, uint32_t remove_from_head);
        void removeTrajectory(ObjectId trajectory_id);

        CommandBuffer& getBuffer();
        void clearBuffer();

    private:
        CommandBuffer cmd_buf_;
    };
}