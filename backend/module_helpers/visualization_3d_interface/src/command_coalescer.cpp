#include "module_helpers/visualization_3d_interface/command_coalescer.h"


using namespace aergo::module::helpers::visualization_3d_interface;



void CommandCoalescer::enableGrid(bool on)
{
    cmd_buf_.grid_commanded_ = true;
    cmd_buf_.grid_enabled_ = on;
}



void CommandCoalescer::createObjectDescription(ResourceId resource_id, const ComplexShape& s)
{
    cmd_buf_.pending_registrations_[resource_id] = s;
}



void CommandCoalescer::addObject(ResourceId resource_id, const Pose& pose, ObjectId object_id)
{
    cmd_buf_.objects_[object_id] = CommandBuffer::ObjectParameters {
        .action = CommandBuffer::Action::ADD,
        .resource_id = resource_id,
        .pose = pose
    };
}



void CommandCoalescer::updateObject(ObjectId object_id, const Pose& pose)
{
    if (cmd_buf_.objects_.find(object_id) != cmd_buf_.objects_.end())
    {
        // already in command buffer (as ADD, UPDATE or REMOVE), just update the pose (REMOVE ignores pose)
        cmd_buf_.objects_[object_id].pose = pose;
    }
    else
    {
        // not yet in command buffer, add an update command
        cmd_buf_.objects_[object_id] = CommandBuffer::ObjectParameters {
            .action = CommandBuffer::Action::UPDATE,
            .pose = pose
        };
    }
}



void CommandCoalescer::removeObject(ObjectId object_id)
{
    if (cmd_buf_.objects_.find(object_id) != cmd_buf_.objects_.end())
    {
        // already in command buffer (as ADD, UPDATE or REMOVE)
        if (cmd_buf_.objects_[object_id].action == CommandBuffer::Action::ADD)
        {
            // was an ADD, just remove the command
            cmd_buf_.objects_.erase(object_id);
        }
        else
        {
            // was an UPDATE or REMOVE, change to REMOVE
            cmd_buf_.objects_[object_id].action = CommandBuffer::Action::REMOVE;
        }
    }
    else
    {
        // not yet in command buffer, add a remove command
        cmd_buf_.objects_[object_id] = CommandBuffer::ObjectParameters {
            .action = CommandBuffer::Action::REMOVE
        };
    }
}



void CommandCoalescer::addTrajectory(const std::vector<Vec3>& pts, Color color, bool dashed, ObjectId trajectory_id)
{
    cmd_buf_.trajectories_[trajectory_id] = CommandBuffer::TrajectoryParameters {
        .action = CommandBuffer::Action::ADD,
        .color = color,
        .dashed = dashed,
        .points = pts,
        .remove_from_head = 0
    };
}



void CommandCoalescer::updateTrajectory(ObjectId trajectory_id, const std::vector<Vec3>& add_pts, uint32_t remove_from_head)
{
    if (cmd_buf_.trajectories_.find(trajectory_id) != cmd_buf_.trajectories_.end())
    {
        // already in command buffer (as ADD, UPDATE or REMOVE), just update the points (REMOVE ignores points)
        if (cmd_buf_.trajectories_[trajectory_id].action == CommandBuffer::Action::ADD)
        {
            // was an ADD, just update the points
            auto& buf_pts = cmd_buf_.trajectories_[trajectory_id].points;
            if (remove_from_head > buf_pts.size())
                remove_from_head = buf_pts.size();
            buf_pts.erase(buf_pts.begin(), buf_pts.begin() + remove_from_head);
            buf_pts.insert(buf_pts.end(), add_pts.begin(), add_pts.end());
        }
        else if (cmd_buf_.trajectories_[trajectory_id].action == CommandBuffer::Action::UPDATE)
        {
            // was an UPDATE, add new points and add up the remove_from_head
            cmd_buf_.trajectories_[trajectory_id].remove_from_head += remove_from_head;
            auto& buf_pts = cmd_buf_.trajectories_[trajectory_id].points;
            buf_pts.insert(buf_pts.end(), add_pts.begin(), add_pts.end());
        }
    }
    else
    {
        // not yet in command buffer, add an update command
        cmd_buf_.trajectories_[trajectory_id] = CommandBuffer::TrajectoryParameters {
            .action = CommandBuffer::Action::UPDATE,
            .points = add_pts,
            .remove_from_head = remove_from_head
        };
    }
}



void CommandCoalescer::removeTrajectory(ObjectId trajectory_id)
{
    if (cmd_buf_.trajectories_.find(trajectory_id) != cmd_buf_.trajectories_.end())
    {
        // already in command buffer (as ADD, UPDATE or REMOVE)
        if (cmd_buf_.trajectories_[trajectory_id].action == CommandBuffer::Action::ADD)
        {
            // was an ADD, just remove the command
            cmd_buf_.trajectories_.erase(trajectory_id);
        }
        else
        {
            // was an UPDATE or REMOVE, change to REMOVE
            cmd_buf_.trajectories_[trajectory_id].action = CommandBuffer::Action::REMOVE;
        }
    }
    else
    {
        // not yet in command buffer, add a remove command
        cmd_buf_.trajectories_[trajectory_id] = CommandBuffer::TrajectoryParameters {
            .action = CommandBuffer::Action::REMOVE
        };
    }
}



CommandBuffer& CommandCoalescer::getBuffer()
{
    return cmd_buf_;
}



void CommandCoalescer::clearBuffer()
{
    cmd_buf_.clear();
}