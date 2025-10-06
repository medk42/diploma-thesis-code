#pragma once

#include "scene_desc_api.h"

#include <map>
#include <vector>
#include <cstdint>

namespace aergo::module::helpers::visualization_3d_interface
{
    struct CommandBuffer
    {
        enum class Action : uint8_t { ADD=0, UPDATE=1, REMOVE=2 };
        struct ObjectParameters { Action action; ResourceId resource_id; Pose pose; };
        struct TrajectoryParameters { Action action; Color color; bool dashed; std::vector<Vec3> points; uint32_t remove_from_head; };

        void clear()
        {
            pending_registrations_.clear();
            objects_.clear();
            trajectories_.clear();
            grid_commanded_ = false;
            grid_enabled_ = false;
        }

        bool isEmpty() const
        {
            return pending_registrations_.empty() && objects_.empty() && trajectories_.empty() && !grid_commanded_;
        }

        bool grid_commanded_{false};
        bool grid_enabled_{false};

        std::map<ResourceId, ComplexShape> pending_registrations_;

        std::map<ObjectId, ObjectParameters> objects_;
        std::map<ObjectId, TrajectoryParameters> trajectories_;
    };
}