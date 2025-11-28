#pragma once
#include <array>
#include <cstddef>
#include <string_view>

namespace robot_model {
struct JointDesc {
    const char* name;
    const char* parent;
    const char* child;
    float origin_xyz[3];
    float origin_rpy[3];
    float axis[3];
    bool movable;
};
struct CylinderDesc { float rBot; float rTop; float h; };
inline constexpr std::string_view kRootLink = "world";
inline constexpr std::size_t kJointCount = 11;
inline constexpr std::size_t kMovableCount = 7;

inline constexpr std::array<JointDesc, 11> kJoints = {
    JointDesc{"joint0", "base", "link1", {0.000000f, 0.000000f, 0.035000f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, true},
    JointDesc{"joint1", "link1", "linkJ2", {0.050000f, 0.000000f, 0.273700f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 1.000000f, 0.000000f}, true},
    JointDesc{"jointPL2", "linkJ2", "link2", {-0.050000f, -0.085700f, 0.000000f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, false},
    JointDesc{"joint2", "link2", "link3", {0.000000f, 0.000000f, 0.274000f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, true},
    JointDesc{"joint3", "link3", "linkJ4", {0.040000f, 0.000000f, 0.142900f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 1.000000f, 0.000000f}, true},
    JointDesc{"jointPL4", "linkJ4", "link4", {-0.040000f, 0.084050f, 0.000000f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, false},
    JointDesc{"joint4", "link4", "link5", {0.000000f, 0.000000f, 0.271000f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 0.000000f, 1.000000f}, true},
    JointDesc{"joint5", "link5", "linkJ6", {0.000000f, 0.000000f, 0.103500f}, {0.000000f, 0.000000f, 0.000000f}, {0.000000f, 1.000000f, 0.000000f}, true},
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

inline constexpr std::array<CylinderDesc, 10> kCylinders = {
    CylinderDesc{ 0.050000f, 0.025000f, 0.035000f },
    CylinderDesc{ 0.050000f, 0.025000f, 0.278230f },
    CylinderDesc{ 0.050000f, 0.025000f, 0.099219f },
    CylinderDesc{ 0.050000f, 0.025000f, 0.274000f },
    CylinderDesc{ 0.050000f, 0.025000f, 0.148393f },
    CylinderDesc{ 0.050000f, 0.025000f, 0.093083f },
    CylinderDesc{ 0.050000f, 0.025000f, 0.271000f },
    CylinderDesc{ 0.050000f, 0.025000f, 0.103500f },
    CylinderDesc{ 0.050000f, 0.025000f, 0.111000f },
    CylinderDesc{ 0.050000f, 0.025000f, 0.108000f },
};
} // namespace robot_model