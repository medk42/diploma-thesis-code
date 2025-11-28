#pragma once

#include <array>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cmath>

#include "kr2_robot_models/a810.h"

#include "module_helpers/visualization_3d_interface/visualization_helper.h"
#include "module_helpers/robot_interface/features/robot_control/structs.h"

namespace robot_vis {

namespace vis3d = aergo::module::helpers::visualization_3d_interface;
namespace ri = aergo::module::helpers::robot_interface;

struct ArrowConfig {
    float line_length_m = 0.20f;   // 20 cm
    float line_radius_m = 0.005f;  // 1 cm width -> 5 mm radius
    float tip_radius_m = 0.01f;    // 2 cm width -> 1 cm radius
    float tip_length_m = 0.02f;    // 2 cm
};

struct Mat4 {
    float m[4][4];
};

inline Mat4 identity() {
    Mat4 T{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            T.m[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
    return T;
}

inline Mat4 multiply(const Mat4& A, const Mat4& B) {
    Mat4 R{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float v = 0.0f;
            for (int k = 0; k < 4; ++k) {
                v += A.m[i][k] * B.m[k][j];
            }
            R.m[i][j] = v;
        }
    }
    return R;
}

inline Mat4 from_rpy_xyz(const float xyz[3], const float rpy[3]) {
    const float cr = std::cos(rpy[0]), sr = std::sin(rpy[0]);
    const float cp = std::cos(rpy[1]), sp = std::sin(rpy[1]);
    const float cy = std::cos(rpy[2]), sy = std::sin(rpy[2]);
    Mat4 T = identity();
    T.m[0][0] = cy * cp;
    T.m[0][1] = cy * sp * sr - sy * cr;
    T.m[0][2] = cy * sp * cr + sy * sr;
    T.m[1][0] = sy * cp;
    T.m[1][1] = sy * sp * sr + cy * cr;
    T.m[1][2] = sy * sp * cr - cy * sr;
    T.m[2][0] = -sp;
    T.m[2][1] = cp * sr;
    T.m[2][2] = cp * cr;
    T.m[0][3] = xyz[0];
    T.m[1][3] = xyz[1];
    T.m[2][3] = xyz[2];
    return T;
}

inline Mat4 rotation_about_axis(const float axis_in[3], float angle_rad) {
    float norm = std::sqrt(axis_in[0] * axis_in[0] + axis_in[1] * axis_in[1] + axis_in[2] * axis_in[2]);
    float ax = (norm > 1e-9f) ? axis_in[0] / norm : 0.f;
    float ay = (norm > 1e-9f) ? axis_in[1] / norm : 0.f;
    float az = (norm > 1e-9f) ? axis_in[2] / norm : 1.f;
    const float c = std::cos(angle_rad);
    const float s = std::sin(angle_rad);
    const float C = 1.f - c;
    Mat4 T = identity();
    T.m[0][0] = c + ax * ax * C;
    T.m[0][1] = ax * ay * C - az * s;
    T.m[0][2] = ax * az * C + ay * s;
    T.m[1][0] = ay * ax * C + az * s;
    T.m[1][1] = c + ay * ay * C;
    T.m[1][2] = ay * az * C - ax * s;
    T.m[2][0] = az * ax * C - ay * s;
    T.m[2][1] = az * ay * C + ax * s;
    T.m[2][2] = c + az * az * C;
    return T;
}

inline vis3d::Vec3 extract_translation(const Mat4& T) {
    return vis3d::Vec3{T.m[0][3], T.m[1][3], T.m[2][3]};
}

inline vis3d::Quat quat_from_matrix(const Mat4& T) {
    const float tr = T.m[0][0] + T.m[1][1] + T.m[2][2];
    vis3d::Quat q{};
    if (tr > 0.f) {
        float s = std::sqrt(tr + 1.f) * 2.f;
        q.w = 0.25f * s;
        q.x = (T.m[2][1] - T.m[1][2]) / s;
        q.y = (T.m[0][2] - T.m[2][0]) / s;
        q.z = (T.m[1][0] - T.m[0][1]) / s;
    } else if (T.m[0][0] > T.m[1][1] && T.m[0][0] > T.m[2][2]) {
        float s = std::sqrt(1.f + T.m[0][0] - T.m[1][1] - T.m[2][2]) * 2.f;
        q.w = (T.m[2][1] - T.m[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (T.m[0][1] + T.m[1][0]) / s;
        q.z = (T.m[0][2] + T.m[2][0]) / s;
    } else if (T.m[1][1] > T.m[2][2]) {
        float s = std::sqrt(1.f + T.m[1][1] - T.m[0][0] - T.m[2][2]) * 2.f;
        q.w = (T.m[0][2] - T.m[2][0]) / s;
        q.x = (T.m[0][1] + T.m[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (T.m[1][2] + T.m[2][1]) / s;
    } else {
        float s = std::sqrt(1.f + T.m[2][2] - T.m[0][0] - T.m[1][1]) * 2.f;
        q.w = (T.m[1][0] - T.m[0][1]) / s;
        q.x = (T.m[0][2] + T.m[2][0]) / s;
        q.y = (T.m[1][2] + T.m[2][1]) / s;
        q.z = 0.25f * s;
    }
    return q.normalized();
}

inline vis3d::Quat align_z_to_dir(const vis3d::Vec3& dir_in) {
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

struct JointRuntime {
    robot_model::JointDesc desc;
    int angle_index;  // -1 for fixed
};

class RobotVisualization {
public:
    explicit RobotVisualization(vis3d::VisualizationHelper* helper)
        : helper_(helper) {}

    // Registers resources using generated kr2_robot_model.hpp data.
    bool registerResources();

    // Register with external descriptors.
    bool registerResources(std::string_view root_link,
                           std::span<const robot_model::JointDesc> joints,
                           std::span<const robot_model::CylinderDesc> cylinders,
                           ArrowConfig arrow_cfg = {});

    // Create visualization objects (call after registerResources).
    bool createVisualization();

    // Update poses from 7 joint angles (radians). Order must match kMovableJointNames.
    bool updateVisualization(std::span<const double> joint_angles_rad);

    // Update TCP arrow pose.
    bool updateTcpPose(
        const ri::robot_control::Pose& base_pose,
        const ri::robot_control::Pose& flange_pose,
        const ri::robot_control::Pose& end_effector_pose
    );

    // Remove all objects from scene.
    void removeVisualization();

    bool isVisualizationCreated() const {
        return objects_created_;
    }

private:
    void computeAdjacency();
    void computeLinkPoses(std::unordered_map<std::string, Mat4>& out_link_pose,
                          std::span<const double> joint_angles_rad) const;

    vis3d::VisualizationHelper* helper_{nullptr};
    std::string root_link_;
    std::vector<JointRuntime> joints_;
    std::vector<robot_model::CylinderDesc> cylinders_;
    std::vector<vis3d::ResourceId> resources_;
    std::vector<vis3d::ObjectId> objects_;
    std::vector<std::size_t> movable_order_;  // indices into joints_
    std::unordered_map<std::string, std::vector<std::size_t>> adj_;
    ArrowConfig arrow_cfg_{};
    vis3d::ResourceId arrow_resource_{0};
    vis3d::ObjectId tcp_arrow_object_{0};
    vis3d::ObjectId tfc_arrow_object_{0};
    vis3d::ObjectId base_arrow_object_{0};
    bool resources_registered_{false};
    bool objects_created_{false};
};

// Implementation --------------------------------------------------------------

inline bool RobotVisualization::registerResources() {
    return registerResources(robot_model::kRootLink, robot_model::kJoints, robot_model::kCylinders, ArrowConfig{});
}

inline bool RobotVisualization::registerResources(std::string_view root_link,
                                                  std::span<const robot_model::JointDesc> joints,
                                                  std::span<const robot_model::CylinderDesc> cylinders,
                                                  ArrowConfig arrow_cfg) {
    if (resources_registered_ || helper_ == nullptr || !helper_->valid()) {
        return false;
    }
    root_link_ = std::string(root_link);
    joints_.clear();
    joints_.reserve(joints.size());
    for (std::size_t i = 0; i < joints.size(); ++i) {
        joints_.push_back(JointRuntime{joints[i], -1});
    }
    cylinders_.assign(cylinders.begin(), cylinders.end());
    arrow_cfg_ = arrow_cfg;
    movable_order_.clear();
    for (std::size_t i = 0; i < joints_.size(); ++i) {
        if (joints_[i].desc.movable) {
            joints_[i].angle_index = static_cast<int>(movable_order_.size());
            movable_order_.push_back(i);
        }
    }
    resources_.resize(joints_.size());
    objects_.assign(joints_.size(), vis3d::ObjectId{0});

    // Register one cylinder resource per joint segment.
    for (std::size_t i = 0; i < joints_.size(); ++i) {
        if (i >= cylinders_.size() || cylinders_[i].h <= 1e-6f) {
            resources_[i] = vis3d::ResourceId{0};
            continue;
        }
        vis3d::ComplexShape shape{};
        vis3d::PrimitiveShape part{};
        part.type = vis3d::PrimitiveShapeType::CYLINDER;
        vis3d::CylinderDesc cyl{};
        cyl.rBot = cylinders_[i].rBot;
        cyl.rTop = cylinders_[i].rTop;
        cyl.h = cylinders_[i].h;
        part.desc = cyl;
        part.origin = vis3d::Pose{};  // center at origin; orient along +Z
        part.color = vis3d::Color{};
        shape.parts.push_back(part);
        resources_[i] = helper_->registerResource(shape);
    }
    // Arrow resource (TCP axes)
    auto axis_shape = [&](const vis3d::Vec3& axis_dir, const vis3d::Color& color) {
        vis3d::ComplexShape shape{};
        vis3d::PrimitiveShape line{};
        line.type = vis3d::PrimitiveShapeType::CYLINDER;
        line.desc = vis3d::CylinderDesc{arrow_cfg_.line_radius_m, arrow_cfg_.line_radius_m, arrow_cfg_.line_length_m};
        vis3d::Pose line_pose{};
        line_pose.t = vis3d::Vec3{axis_dir.x * (arrow_cfg_.line_length_m * 0.5f),
                                  axis_dir.y * (arrow_cfg_.line_length_m * 0.5f),
                                  axis_dir.z * (arrow_cfg_.line_length_m * 0.5f)};
        line_pose.q = align_z_to_dir(axis_dir);
        line.origin = line_pose;
        line.color = color;
        shape.parts.push_back(line);

        vis3d::PrimitiveShape tip{};
        tip.type = vis3d::PrimitiveShapeType::CYLINDER;
        tip.desc = vis3d::CylinderDesc{arrow_cfg_.tip_radius_m, 0.0f, arrow_cfg_.tip_length_m};
        vis3d::Pose tip_pose{};
        float offset = arrow_cfg_.line_length_m + (arrow_cfg_.tip_length_m * 0.5f);
        tip_pose.t = vis3d::Vec3{axis_dir.x * offset, axis_dir.y * offset, axis_dir.z * offset};
        tip_pose.q = align_z_to_dir(axis_dir);
        tip.origin = tip_pose;
        tip.color = color;
        shape.parts.push_back(tip);
        return shape;
    };
    vis3d::ComplexShape arrow_shape{};
    // X axis - red
    vis3d::ComplexShape xshape = axis_shape(vis3d::Vec3{1.f, 0.f, 0.f}, vis3d::Color{255, 0, 0, 255});
    arrow_shape.parts.insert(arrow_shape.parts.end(), xshape.parts.begin(), xshape.parts.end());
    // Y axis - green
    vis3d::ComplexShape yshape = axis_shape(vis3d::Vec3{0.f, 1.f, 0.f}, vis3d::Color{0, 255, 0, 255});
    arrow_shape.parts.insert(arrow_shape.parts.end(), yshape.parts.begin(), yshape.parts.end());
    // Z axis - blue
    vis3d::ComplexShape zshape = axis_shape(vis3d::Vec3{0.f, 0.f, 1.f}, vis3d::Color{0, 0, 255, 255});
    arrow_shape.parts.insert(arrow_shape.parts.end(), zshape.parts.begin(), zshape.parts.end());
    arrow_resource_ = helper_->registerResource(arrow_shape);
    computeAdjacency();
    resources_registered_ = true;
    return true;
}

inline void RobotVisualization::computeAdjacency() {
    adj_.clear();
    for (std::size_t i = 0; i < joints_.size(); ++i) {
        adj_[joints_[i].desc.parent].push_back(i);
    }
}

inline bool RobotVisualization::createVisualization() {
    if (!resources_registered_ || objects_created_) {
        return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < joints_.size(); ++i) {
        if (resources_[i].id == 0) {
            continue;
        }
        vis3d::Pose pose{};
        vis3d::ObjectId id{};
        bool added = helper_->addObject(resources_[i], pose, id);
        ok = ok && added;
        objects_[i] = id;
    }
    if (arrow_resource_.id != 0) {
        vis3d::Pose pose{};
        vis3d::ObjectId id{};
        
        bool added = helper_->addObject(arrow_resource_, pose, id);
        ok = ok && added;
        tcp_arrow_object_ = id;
        
        added = helper_->addObject(arrow_resource_, pose, id);
        ok = ok && added;
        tfc_arrow_object_ = id;

        added = helper_->addObject(arrow_resource_, pose, id);
        ok = ok && added;
        base_arrow_object_ = id;
    }
    helper_->sendUpdate();
    objects_created_ = ok;
    return ok;
}

inline void RobotVisualization::computeLinkPoses(std::unordered_map<std::string, Mat4>& out_link_pose,
                                                 std::span<const double> joint_angles_rad) const {
    out_link_pose.clear();
    out_link_pose[root_link_] = identity();
    std::function<void(const std::string&)> dfs = [&](const std::string& link) {
        auto it = adj_.find(link);
        if (it == adj_.end()) {
            return;
        }
        for (std::size_t joint_idx : it->second) {
            const auto& j = joints_[joint_idx];
            const Mat4 parent_pose = out_link_pose[link];
            Mat4 T_origin = from_rpy_xyz(j.desc.origin_xyz, j.desc.origin_rpy);
            float angle = 0.f;
            if (j.angle_index >= 0 && j.angle_index < static_cast<int>(joint_angles_rad.size())) {
                angle = static_cast<float>(joint_angles_rad[static_cast<std::size_t>(j.angle_index)]);
            }
            Mat4 T_rot = rotation_about_axis(j.desc.axis, angle);
            Mat4 child_pose = multiply(parent_pose, multiply(T_origin, T_rot));
            out_link_pose[j.desc.child] = child_pose;
            dfs(j.desc.child);
        }
    };
    dfs(root_link_);
}

inline bool RobotVisualization::updateVisualization(std::span<const double> joint_angles_rad) {
    if (!objects_created_ || joint_angles_rad.size() != movable_order_.size()) {
        return false;
    }
    std::unordered_map<std::string, Mat4> link_pose;
    computeLinkPoses(link_pose, joint_angles_rad);

    for (std::size_t i = 0; i < joints_.size(); ++i) {
        const auto& j = joints_[i];
        auto parent_it = link_pose.find(j.desc.parent);
        auto child_it = link_pose.find(j.desc.child);
        if (parent_it == link_pose.end() || child_it == link_pose.end()) {
            continue;
        }
        if (resources_[i].id == 0) {
            continue;  // not visualized
        }
        vis3d::Vec3 p0 = extract_translation(parent_it->second);
        vis3d::Vec3 p1 = extract_translation(child_it->second);
        vis3d::Vec3 center{0.5f * (p0.x + p1.x), 0.5f * (p0.y + p1.y), 0.5f * (p0.z + p1.z)};
        vis3d::Vec3 dir{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
        vis3d::Quat q = align_z_to_dir(dir);
        vis3d::Pose pose{};
        pose.t = center;
        pose.q = q;
        helper_->updateObject(objects_[i], pose);
    }
    helper_->sendUpdate();
    return true;
}

inline vis3d::Pose convertPoseToVis3D(const ri::robot_control::Pose& pose_in) {
    vis3d::Pose pose{};
    pose.t = vis3d::Vec3{static_cast<float>(pose_in.position.x),
                         static_cast<float>(pose_in.position.y),
                         static_cast<float>(pose_in.position.z)};
    pose.q = vis3d::Quat{static_cast<float>(pose_in.orientation.x),
                         static_cast<float>(pose_in.orientation.y),
                         static_cast<float>(pose_in.orientation.z),
                         static_cast<float>(pose_in.orientation.w)}.normalized();
    return pose;
}

inline bool RobotVisualization::updateTcpPose(const ri::robot_control::Pose& base_pose,
                                             const ri::robot_control::Pose& flange_pose,
                                             const ri::robot_control::Pose& end_effector_pose) {
    if (!objects_created_ || tcp_arrow_object_.id == 0 || tfc_arrow_object_.id == 0 || base_arrow_object_.id == 0) {
        return false;
    }

    helper_->updateObject(base_arrow_object_, convertPoseToVis3D(base_pose));
    helper_->updateObject(tfc_arrow_object_, convertPoseToVis3D(flange_pose));
    helper_->updateObject(tcp_arrow_object_, convertPoseToVis3D(end_effector_pose));

    helper_->sendUpdate();
    return true;
}

inline void RobotVisualization::removeVisualization() {
    if (!objects_created_) {
        return;
    }
    for (const auto& id : objects_) {
        if (id.id == 0) {
            continue;
        }
        helper_->removeObject(id);
    }
    if (tcp_arrow_object_.id != 0) {
        helper_->removeObject(tcp_arrow_object_);
        tcp_arrow_object_ = vis3d::ObjectId{0};
    }
    if (tfc_arrow_object_.id != 0) {
        helper_->removeObject(tfc_arrow_object_);
        tfc_arrow_object_ = vis3d::ObjectId{0};
    }
    if (base_arrow_object_.id != 0) {
        helper_->removeObject(base_arrow_object_);
        base_arrow_object_ = vis3d::ObjectId{0};
    }
    helper_->sendUpdate();
    objects_created_ = false;
}

}  // namespace robot_vis
