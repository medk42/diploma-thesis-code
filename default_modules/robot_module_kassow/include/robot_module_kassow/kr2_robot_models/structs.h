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
    
}