#pragma once

#include "a810.h"
#include "a1018.h"
#include "a1205.h"
#include "a1410.h"
#include "a1805.h"

#include <stdexcept>

namespace aergo::default_modules::robot_module_kassow::robot_vis::robot_model
{
    namespace vis3d = aergo::module::helpers::visualization_3d_interface;

    inline const std::string_view getRootLink(RobotModelType type)
    {
        switch (type)
        {
            case RobotModelType::A810:
                return a810::kRootLink;
            case RobotModelType::A1018:
                return a1018::kRootLink;
            case RobotModelType::A1205:
                return a1205::kRootLink;
            case RobotModelType::A1410:
                return a1410::kRootLink;
            case RobotModelType::A1805:
                return a1805::kRootLink;
            default:
                throw std::invalid_argument("Invalid RobotModelType in getRootLink()");
        }
    }

    inline const std::array<JointDesc, 11>& getJoints(RobotModelType type)
    {
        switch (type)
        {
            case RobotModelType::A810:
                return a810::kJoints;
            case RobotModelType::A1018:
                return a1018::kJoints;
            case RobotModelType::A1205:
                return a1205::kJoints;
            case RobotModelType::A1410:
                return a1410::kJoints;
            case RobotModelType::A1805:
                return a1805::kJoints;
            default:
                throw std::invalid_argument("Invalid RobotModelType in getJoints()");
        }
    }

    inline const std::array<vis3d::CylinderDesc, 10>& getCylinders(RobotModelType type)
    {
        switch (type)
        {
            case RobotModelType::A810:
                return a810::kCylinders;
            case RobotModelType::A1018:
                return a1018::kCylinders;
            case RobotModelType::A1205:
                return a1205::kCylinders;
            case RobotModelType::A1410:
                return a1410::kCylinders;
            case RobotModelType::A1805:
                return a1805::kCylinders;
            default:
                throw std::invalid_argument("Invalid RobotModelType in getCylinders()");
        }
    }
}