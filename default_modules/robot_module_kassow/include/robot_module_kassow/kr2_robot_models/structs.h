#pragma once

namespace aergo::default_modules::robot_module_kassow::robot_vis::robot_model {

    struct JointDesc
    {
        const char* name;
        const char* parent;
        const char* child;
        float origin_xyz[3];
        float origin_rpy[3];
        float axis[3];
        bool movable;
    };

    enum class RobotModelType : uint8_t
    {
        A810  = 0,
        A1018 = 1,
        A1205 = 2,
        A1410 = 3,
        A1805 = 4
        // If new models are added, remember to update the functions below AND activate in robot_module_kassow.cpp
    };
    
}