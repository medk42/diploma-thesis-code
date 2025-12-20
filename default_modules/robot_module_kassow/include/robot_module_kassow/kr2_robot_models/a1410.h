#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "module_helpers/visualization_3d_interface/scene_desc_api.h"
#include "structs.h"

namespace aergo::default_modules::robot_module_kassow::robot_vis::robot_model::a1410 {

    namespace vis3d = aergo::module::helpers::visualization_3d_interface;

    inline constexpr std::string_view kRootLink = "world";
    inline constexpr std::size_t kJointCount = 11;
    inline constexpr std::size_t kMovableCount = 7;

    inline constexpr std::array<JointDesc, 11> kJoints = {
        JointDesc{"joint0", "base", "link1", {0.000000f, 0.000000f, 0.035000f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, true},
        JointDesc{"joint1", "link1", "linkJ2", {0.062000f, 0.000000f, 0.322200f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 1.000000f, 0.000000f}, true},
        JointDesc{"jointPL2", "linkJ2", "link2", {-0.062000f, 0.105200f, 0.000000f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, false},
        JointDesc{"joint2", "link2", "link3", {0.000000f, 0.000000f, 0.539600f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, true},
        JointDesc{"joint3", "link3", "linkJ4", {0.050000f, 0.000000f, 0.182900f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 1.000000f, 0.000000f}, true},
        JointDesc{"jointPL4", "linkJ4", "link4", {-0.050000f, -0.085700f, 0.000000f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, false},
        JointDesc{"joint4", "link4", "link5", {0.000000f, 0.000000f, 0.576400f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, true},
        JointDesc{"joint5", "link5", "linkJ6", {0.000000f, 0.000000f, 0.101100f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 1.000000f, 0.000000f}, true},
        JointDesc{"jointPL6", "linkJ6", "link6", {0.000000f, 0.111000f, 0.000000f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, false},
        JointDesc{"joint6", "link6", "end_effector", {0.000000f, 0.000000f, 0.108000f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, true},
        JointDesc{"joint_world", "world", "base", {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, false},
    };

    inline constexpr std::array<int, 7> kMovableJointIndices = {
        0, 1, 3, 4, 6, 7, 9
    };

    inline constexpr std::array<const char*, 7> kMovableJointNames = {
        "joint0", "joint1", "joint2", "joint3", "joint4", "joint5", "joint6"
    };

    inline constexpr std::array<vis3d::CylinderDesc, 10> kCylinders = {
        vis3d::CylinderDesc{ 0.050000f, 0.025000f, 0.035000f },
        vis3d::CylinderDesc{ 0.050000f, 0.025000f, 0.328111f },
        vis3d::CylinderDesc{ 0.050000f, 0.025000f, 0.122111f },
        vis3d::CylinderDesc{ 0.050000f, 0.025000f, 0.539600f },
        vis3d::CylinderDesc{ 0.050000f, 0.025000f, 0.189611f },
        vis3d::CylinderDesc{ 0.050000f, 0.025000f, 0.099219f },
        vis3d::CylinderDesc{ 0.050000f, 0.025000f, 0.576400f },
        vis3d::CylinderDesc{ 0.050000f, 0.025000f, 0.101100f },
        vis3d::CylinderDesc{ 0.050000f, 0.025000f, 0.111000f },
        vis3d::CylinderDesc{ 0.050000f, 0.025000f, 0.108000f },
    };
} // namespace a1410
