#include "robot_module_kassow/robot_visualization.h"

using namespace aergo::default_modules::robot_module_kassow::robot_vis;



Mat4 Mat4::identity() {
    Mat4 T{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            T.m[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
    return T;
}


Mat4 Mat4::multiply(const Mat4& A, const Mat4& B) {
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


Mat4 Mat4::fromRpyXyz(const float xyz[3], const float rpy[3]) {
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


Mat4 Mat4::rotationAboutAxis(const float axis_in[3], float angle_rad) {
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


vis3d::Vec3 Mat4::extractTranslation() const {
    return vis3d::Vec3{m[0][3], m[1][3], m[2][3]};
}


vis3d::Quat Mat4::quatFromMatrix() const {
    const float tr = m[0][0] + m[1][1] + m[2][2];
    vis3d::Quat q{};
    if (tr > 0.f) {
        float s = std::sqrt(tr + 1.f) * 2.f;
        q.w = 0.25f * s;
        q.x = (m[2][1] - m[1][2]) / s;
        q.y = (m[0][2] - m[2][0]) / s;
        q.z = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        float s = std::sqrt(1.f + m[0][0] - m[1][1] - m[2][2]) * 2.f;
        q.w = (m[2][1] - m[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (m[0][1] + m[1][0]) / s;
        q.z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        float s = std::sqrt(1.f + m[1][1] - m[0][0] - m[2][2]) * 2.f;
        q.w = (m[0][2] - m[2][0]) / s;
        q.x = (m[0][1] + m[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (m[1][2] + m[2][1]) / s;
    } else {
        float s = std::sqrt(1.f + m[2][2] - m[0][0] - m[1][1]) * 2.f;
        q.w = (m[1][0] - m[0][1]) / s;
        q.x = (m[0][2] + m[2][0]) / s;
        q.y = (m[1][2] + m[2][1]) / s;
        q.z = 0.25f * s;
    }
    return q.normalized();
}


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


bool RobotVisualization::registerResources(
    vis3d::Color robot_color,
    std::string_view root_link,
    std::span<const robot_model::JointDesc> joints,
    std::span<const vis3d::CylinderDesc> cylinders,
    ArrowConfig arrow_cfg
) {
    if (resources_registered_ || helper_ == nullptr || !helper_->valid())
    {
        return false;
    }

    // Set local data
    root_link_ = std::string(root_link);

    joints_.clear();
    joints_.reserve(joints.size());
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
        joints_.push_back(JointRuntime{joints[i], -1});
    }

    cylinders_.assign(cylinders.begin(), cylinders.end());

    movable_order_.clear();
    for (std::size_t i = 0; i < joints_.size(); ++i)
    {
        if (joints_[i].desc.movable)
        {
            joints_[i].angle_index = static_cast<int>(movable_order_.size());
            movable_order_.push_back(i);
        }
    }

    resources_.resize(joints_.size());
    objects_.assign(joints_.size(), vis3d::ObjectId{0});

    // Register one cylinder resource per joint segment.
    for (std::size_t i = 0; i < joints_.size(); ++i)
    {
        if (i >= cylinders_.size() || cylinders_[i].h <= 1e-6f)
        {
            resources_[i] = vis3d::ResourceId{0};
            continue;
        }

        vis3d::ComplexShape shape{};
        shape.parts.push_back(vis3d::PrimitiveShape {
            .type = vis3d::PrimitiveShapeType::CYLINDER,
            .desc = cylinders_[i],
            .origin = vis3d::Pose{}, // center at origin; orient along +Z
            .color = robot_color
        });

        resources_[i] = helper_->registerResource(shape);
    }

    // Compute adjacency
    adj_.clear();
    for (std::size_t i = 0; i < joints_.size(); ++i)
    {
        adj_[joints_[i].desc.parent].push_back(i);
    }

    // Register arrow resource (TCP axes)
    auto axis_shape = [&](const vis3d::Vec3& axis_dir, const vis3d::Color& color, vis3d::ComplexShape& out_shape) -> void {
        float line_offset = arrow_cfg.line_length_m * 0.5f;

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

        float tip_offset = arrow_cfg.line_length_m + (arrow_cfg.tip_length_m * 0.5f);

        vis3d::PrimitiveShape tip {
            .type = vis3d::PrimitiveShapeType::CYLINDER,
            .desc = vis3d::CylinderDesc {
                .rBot = arrow_cfg.tip_radius_m,
                .rTop = 0.0f,
                .h = arrow_cfg.tip_length_m
            },
            .origin = vis3d::Pose {
                .t = vis3d::Vec3{ 
                    .x = axis_dir.x * tip_offset, 
                    .y = axis_dir.y * tip_offset, 
                    .z = axis_dir.z * tip_offset
                },
                .q = alignZToDir(axis_dir)
            },
            .color = color
        };

        out_shape.parts.push_back(line);
        out_shape.parts.push_back(tip);
    };

    vis3d::ComplexShape arrow_shape{};
    axis_shape(vis3d::Vec3{1.f, 0.f, 0.f}, vis3d::Color{255, 0, 0, 255}, arrow_shape); // X axis - red
    axis_shape(vis3d::Vec3{0.f, 1.f, 0.f}, vis3d::Color{0, 255, 0, 255}, arrow_shape); // Y axis - green
    axis_shape(vis3d::Vec3{0.f, 0.f, 1.f}, vis3d::Color{0, 0, 255, 255}, arrow_shape); // Z axis - blue

    arrow_resource_ = helper_->registerResource(arrow_shape);


    resources_registered_ = true;

    return true;
}


bool RobotVisualization::createVisualization() {
    if (!resources_registered_ || objects_created_)
    {
        return false;
    }

    bool ok = true;

    for (std::size_t i = 0; i < joints_.size(); ++i)
    {
        if (resources_[i].id == 0)
        {
            continue;
        }

        vis3d::Pose pose{};
        vis3d::ObjectId id{};
        bool added = helper_->addObject(resources_[i], pose, id);

        ok = ok && added;
        objects_[i] = id;
    }

    if (arrow_resource_.id != 0)
    {
        vis3d::Pose pose{};
        
        bool added = helper_->addObject(arrow_resource_, pose, tcp_arrow_object_);
        ok = ok && added;

        added = helper_->addObject(arrow_resource_, pose, tfc_arrow_object_);
        ok = ok && added;

        added = helper_->addObject(arrow_resource_, pose, base_arrow_object_);
        ok = ok && added;
    }

    helper_->sendUpdate();
    objects_created_ = ok;

    return ok;
}

void RobotVisualization::computeLinkPoses(
    std::unordered_map<std::string, Mat4>& out_link_pose,
    std::span<const double> joint_angles_rad
) const {
    out_link_pose.clear();
    out_link_pose[root_link_] = Mat4::identity();

    std::function<void(const std::string&)> dfs = [&](const std::string& link) -> void {
        auto it = adj_.find(link);
        if (it == adj_.end())
        {
            return;
        }

        for (std::size_t joint_idx : it->second)
        {
            const auto& j = joints_[joint_idx];
            const Mat4 parent_pose = out_link_pose[link];

            Mat4 T_origin = Mat4::fromRpyXyz(j.desc.origin_xyz, j.desc.origin_rpy);

            float angle = 0.f;
            if (j.angle_index >= 0 && j.angle_index < static_cast<int>(joint_angles_rad.size()))
            {
                angle = static_cast<float>(joint_angles_rad[static_cast<std::size_t>(j.angle_index)]);
            }
            Mat4 T_rot = Mat4::rotationAboutAxis(j.desc.axis, angle);

            Mat4 child_pose = Mat4::multiply(parent_pose, Mat4::multiply(T_origin, T_rot));
            out_link_pose[j.desc.child] = child_pose;
            dfs(j.desc.child);
        }
    };

    dfs(root_link_);
}


bool RobotVisualization::updateRobotVisualization(std::span<const double> joint_angles_rad) {
    if (!objects_created_ || joint_angles_rad.size() != movable_order_.size())
    {
        return false;
    }

    std::unordered_map<std::string, Mat4> link_pose;
    computeLinkPoses(link_pose, joint_angles_rad);

    for (std::size_t i = 0; i < joints_.size(); ++i)
    {
        const auto& j = joints_[i];
        
        auto parent_it = link_pose.find(j.desc.parent);
        auto child_it = link_pose.find(j.desc.child);

        if (parent_it == link_pose.end() || child_it == link_pose.end())
        {
            continue;
        }

        if (resources_[i].id == 0)
        {
            continue;  // not visualized
        }

        vis3d::Vec3 p0 = parent_it->second.extractTranslation();
        vis3d::Vec3 p1 = child_it->second.extractTranslation();

        vis3d::Vec3 center{0.5f * (p0.x + p1.x), 0.5f * (p0.y + p1.y), 0.5f * (p0.z + p1.z)};
        vis3d::Vec3 dir{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};

        vis3d::Quat q = alignZToDir(dir);

        helper_->updateObject(objects_[i], vis3d::Pose{
            .t = center,
            .q = q
        });
    }

    helper_->sendUpdate();

    return true;
}


vis3d::Pose convertPoseToVis3D(const ri::robot_control::Pose& pose_in) {
    return vis3d::Pose{
        .t = vis3d::Vec3{
            .x = static_cast<float>(pose_in.position.x),
            .y = static_cast<float>(pose_in.position.y),
            .z = static_cast<float>(pose_in.position.z)
        },
        .q = vis3d::Quat{
            .x = static_cast<float>(pose_in.orientation.x),
            .y = static_cast<float>(pose_in.orientation.y),
            .z = static_cast<float>(pose_in.orientation.z),
            .w = static_cast<float>(pose_in.orientation.w)
        }.normalized()
    };
}


bool RobotVisualization::updateTcpPose(
    const ri::robot_control::Pose& base_pose,
    const ri::robot_control::Pose& flange_pose,
    const ri::robot_control::Pose& end_effector_pose
) {
    if (!objects_created_ || tcp_arrow_object_.id == 0 || tfc_arrow_object_.id == 0 || base_arrow_object_.id == 0)
    {
        return false;
    }

    helper_->updateObject(base_arrow_object_, convertPoseToVis3D(base_pose));
    helper_->updateObject(tfc_arrow_object_, convertPoseToVis3D(flange_pose));
    helper_->updateObject(tcp_arrow_object_, convertPoseToVis3D(end_effector_pose));

    helper_->sendUpdate();

    return true;
}


void RobotVisualization::removeVisualization() {
    if (!objects_created_)
    {
        return;
    }

    for (const auto& id : objects_)
    {
        if (id.id == 0) continue;

        helper_->removeObject(id);
    }

    if (tcp_arrow_object_.id != 0)
    {
        helper_->removeObject(tcp_arrow_object_);
        tcp_arrow_object_ = vis3d::ObjectId{0};
    }

    if (tfc_arrow_object_.id != 0)
    {
        helper_->removeObject(tfc_arrow_object_);
        tfc_arrow_object_ = vis3d::ObjectId{0};
    }

    if (base_arrow_object_.id != 0)
    {
        helper_->removeObject(base_arrow_object_);
        base_arrow_object_ = vis3d::ObjectId{0};
    }

    helper_->sendUpdate();
    objects_created_ = false;
}