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

#include "kr2_robot_models/structs.h"

#include "module_helpers/visualization_3d_interface/visualization_helper.h"
#include "module_helpers/robot_interface/features/robot_control/structs.h"

namespace aergo::default_modules::robot_module_kassow::robot_vis {

    namespace vis3d = aergo::module::helpers::visualization_3d_interface;
    namespace ri = aergo::module::helpers::robot_interface;

    struct ArrowConfig
    {
        float line_length_m = 0.20f;   // 20 cm
        float line_radius_m = 0.005f;  // 1 cm width -> 5 mm radius
        float tip_radius_m = 0.01f;    // 2 cm width -> 1 cm radius
        float tip_length_m = 0.02f;    // 2 cm
    };

    struct Mat4
    {
        static Mat4 identity();
        static Mat4 multiply(const Mat4& A, const Mat4& B);
        static Mat4 fromRpyXyz(const float xyz[3], const float rpy[3]);
        static Mat4 rotationAboutAxis(const float axis_in[3], float angle_rad);

        vis3d::Vec3 extractTranslation() const;
        vis3d::Quat quatFromMatrix() const;

        float m[4][4];
    };

    struct JointRuntime
    {
        robot_model::JointDesc desc;
        int angle_index;  // -1 for fixed
    };

    class RobotVisualization {
    public:
        explicit RobotVisualization(vis3d::VisualizationHelper* helper) : helper_(helper) {}

        // Register with external descriptors.
        bool registerResources(
            vis3d::Color robot_color,
            std::string_view root_link,
            std::span<const robot_model::JointDesc> joints,
            std::span<const vis3d::CylinderDesc> cylinders,
            ArrowConfig arrow_cfg = ArrowConfig{}
        );

        // Create visualization objects (call after registerResources).
        bool createVisualization();

        // Update poses from 7 joint angles (radians). Order must match kMovableJointNames.
        bool updateRobotVisualization(std::span<const double> joint_angles_rad);

        // Update TCP arrow pose.
        bool updateTcpPose(
            const ri::robot_control::Pose& base_pose,
            const ri::robot_control::Pose& flange_pose,
            const ri::robot_control::Pose& end_effector_pose
        );

        // Remove all objects from scene.
        void removeVisualization();

        // Check if visualization has been created.
        bool isVisualizationCreated() const { return objects_created_; }

    private:
        void computeLinkPoses(
            std::unordered_map<std::string, Mat4>& out_link_pose,
            std::span<const double> joint_angles_rad
        ) const;

        vis3d::VisualizationHelper* helper_{nullptr};

        std::string root_link_;
        std::vector<JointRuntime> joints_;
        std::vector<vis3d::CylinderDesc> cylinders_;

        std::vector<std::size_t> movable_order_;  // indices into joints_
        std::unordered_map<std::string, std::vector<std::size_t>> adj_;

        std::vector<vis3d::ResourceId> resources_;
        std::vector<vis3d::ObjectId> objects_;
        
        vis3d::ResourceId arrow_resource_{0};
        vis3d::ObjectId tcp_arrow_object_{0};
        vis3d::ObjectId tfc_arrow_object_{0};
        vis3d::ObjectId base_arrow_object_{0};

        bool resources_registered_{false};
        bool objects_created_{false};
    };

}  // namespace robot_vis
