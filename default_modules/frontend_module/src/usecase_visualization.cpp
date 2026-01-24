#include "webapp/ui/helper/usecase_visualization.h"

#include "webapp/ui/helper/scene_container.h"

#include <cmath>

using namespace aergo::default_modules::frontend_module::webapp::ui::helper;
namespace vis3d = aergo::module::helpers::visualization_3d_interface;
namespace uw = aergo::module::helpers::usecase_wrapper;


namespace
{
    vis3d::Quat alignZToDir(const vis3d::Vec3& dir_in) {
        float norm = std::sqrt(dir_in.x * dir_in.x + dir_in.y * dir_in.y + dir_in.z * dir_in.z);
        if (norm < 1e-6f) {
            return vis3d::Quat::Identity();
        }
        const float dx = dir_in.x / norm;
        const float dy = dir_in.y / norm;
        const float dz = dir_in.z / norm;
        // rotate +Z to dir
        vis3d::Vec3 z{0.f, 0.f, 1.f};
        vis3d::Vec3 v{z.y * dz - z.z * dy, z.z * dx - z.x * dz, z.x * dy - z.y * dx};  // cross
        float dot = z.x * dx + z.y * dy + z.z * dz;
        float s = std::sqrt((1.f + dot) * 2.f);
        if (s < 1e-6f) {
            // Opposite direction: rotate 180 deg about X
            return vis3d::Quat::FromAxisDeg(1.f, 0.f, 0.f, 180.f);
        }
        float invs = 1.f / s;
        vis3d::Quat q{v.x * invs, v.y * invs, v.z * invs, s * 0.5f};
        return q.normalized();
    }
}


UsecaseVisualization::UsecaseVisualization(
    SceneContainer* scene_container,
    const vis3d::Color& point_color,
    const vis3d::Color& trajectory_color)
    : scene_container_(scene_container),
      point_color_(point_color),
      trajectory_color_(trajectory_color)
{
    if (!scene_container_)
    {
        return;
    }

    registerResources();
}


void UsecaseVisualization::registerResources()
{
    if (!scene_container_)
    {
        return;
    }

    // Default arrow config (same as pen_tracking_multicam_module)
    struct ArrowConfig
    {
        float line_length_m = 0.06f;
        float line_radius_m = 0.002f;
        float tip_radius_m = 0.005f;
        float tip_length_m = 0.01f;
    };
    ArrowConfig arrow_cfg{};

    // Pose axis colors
    vis3d::Color pose_color_x{255, 0, 0, 255};      // Red for X axis
    vis3d::Color pose_color_y{0, 255, 0, 255};      // Green for Y axis
    vis3d::Color pose_color_z{0, 0, 255, 255};      // Blue for Z axis

    // Register pose resource (axes without tips, with sphere in middle) - different from pen pose
    auto axis_shape = [&](const vis3d::Vec3& axis_dir, const vis3d::Color& color, vis3d::ComplexShape& out_shape) -> void {
        float line_offset = arrow_cfg.line_length_m * 0.5f;

        // Just a line, no tip
        vis3d::PrimitiveShape line {
            .type = vis3d::PrimitiveShapeType::CYLINDER,
            .desc = vis3d::CylinderDesc {
                .rBot = arrow_cfg.line_radius_m,
                .rTop = arrow_cfg.line_radius_m,
                .h = arrow_cfg.line_length_m
            },
            .origin = vis3d::Pose {
                .t = vis3d::Vec3 {
                    .x = axis_dir.x * line_offset,
                    .y = axis_dir.y * line_offset,
                    .z = axis_dir.z * line_offset
                },
                .q = alignZToDir(axis_dir)
            },
            .color = color
        };

        out_shape.parts.push_back(line);
    };

    vis3d::ComplexShape pose_shape{};
    
    // Add sphere in the middle (at origin) - same size and color as point resource
    pose_shape.parts.push_back(vis3d::PrimitiveShape {
        .type = vis3d::PrimitiveShapeType::SPHERE,
        .desc = vis3d::SphereDesc {
            .r = 0.0075f
        },
        .origin = vis3d::Pose(),
        .color = point_color_  // Same color as point resource
    });
    
    // Add axes (lines without tips)
    axis_shape(vis3d::Vec3{1.f, 0.f, 0.f}, pose_color_x, pose_shape); // X axis - red
    axis_shape(vis3d::Vec3{0.f, 1.f, 0.f}, pose_color_y, pose_shape); // Y axis - green
    axis_shape(vis3d::Vec3{0.f, 0.f, 1.f}, pose_color_z, pose_shape); // Z axis - blue

    pose_resource_id_ = scene_container_->createObjectDescription(pose_shape);

    // Register sphere resource (for points) - gray-ish ball, 2x of 0.0075f radius = 0.015f
    vis3d::ComplexShape sphere_shape{};
    sphere_shape.parts.push_back(vis3d::PrimitiveShape {
        .type = vis3d::PrimitiveShapeType::SPHERE,
        .desc = vis3d::SphereDesc {
            .r = 0.0075f
        },
        .origin = vis3d::Pose(),
        .color = point_color_
    });

    point_resource_id_ = scene_container_->createObjectDescription(sphere_shape);
}


void UsecaseVisualization::clearVisualization()
{
    if (!scene_container_)
    {
        return;
    }

    // Remove all active objects
    for (const auto& object_id : active_object_ids_)
    {
        scene_container_->removeObject(object_id);
    }

    // Remove all active trajectories
    for (const auto& trajectory_id : active_trajectory_ids_)
    {
        scene_container_->removeTrajectory(trajectory_id);
    }

    active_object_ids_.clear();
    active_trajectory_ids_.clear();
}


void UsecaseVisualization::displayVisualization(const uw::IUsecaseModule::UsecaseVisualization& visualization_info)
{
    if (!scene_container_)
    {
        return;
    }

    // Clear existing visualization first
    clearVisualization();

    // If visualization is not supported, we're done (already cleared)
    if (!visualization_info.supports_visualization)
    {
        return;
    }

    // Convert and display poses
    for (const auto& pose : visualization_info.poses)
    {
        // Convert from UsecaseVisualization::Pose (double, Quaternion with qw first) 
        // to vis3d::Pose (float, Quat with w last)
        vis3d::Pose vis3d_pose {
            .t = vis3d::Vec3 {
                .x = static_cast<float>(pose.position.x),
                .y = static_cast<float>(pose.position.y),
                .z = static_cast<float>(pose.position.z)
            },
            .q = vis3d::Quat {
                .x = static_cast<float>(pose.orientation.qx),
                .y = static_cast<float>(pose.orientation.qy),
                .z = static_cast<float>(pose.orientation.qz),
                .w = static_cast<float>(pose.orientation.qw)
            }
        };

        vis3d::ObjectId object_id;
        if (scene_container_->addObject(pose_resource_id_, vis3d_pose, object_id))
        {
            active_object_ids_.push_back(object_id);
        }
    }

    // Convert and display points
    for (const auto& point : visualization_info.points)
    {
        // Points are just positions (no orientation)
        vis3d::Pose vis3d_pose {
            .t = vis3d::Vec3 {
                .x = static_cast<float>(point.x),
                .y = static_cast<float>(point.y),
                .z = static_cast<float>(point.z)
            },
            .q = vis3d::Quat::Identity()
        };

        vis3d::ObjectId object_id;
        if (scene_container_->addObject(point_resource_id_, vis3d_pose, object_id))
        {
            active_object_ids_.push_back(object_id);
        }
    }

    // Convert and display trajectories
    for (const auto& trajectory : visualization_info.trajectories)
    {
        // Convert trajectory points from double to float
        std::vector<vis3d::Vec3> vis3d_points;
        vis3d_points.reserve(trajectory.size());
        for (const auto& point : trajectory)
        {
            vis3d_points.push_back(vis3d::Vec3 {
                .x = static_cast<float>(point.x),
                .y = static_cast<float>(point.y),
                .z = static_cast<float>(point.z)
            });
        }

        vis3d::ObjectId trajectory_id;
        if (scene_container_->addTrajectory(vis3d_points, trajectory_color_, false, trajectory_id))
        {
            active_trajectory_ids_.push_back(trajectory_id);
        }
    }
}

