#include "demo_robot_control.h"
#include "module_helpers/usecase_wrapper/serialization_helper.h"
#include "module_helpers/robot_interface/features/robot_control/messages.h"
#include "module_helpers/robot_interface/message_types.h"
#include "module_helpers/robot_interface/message_types_definitions.h"

#include <cmath>
#include <vector>
#include <numbers>

using namespace aergo::default_modules::demo_robot_control;
using namespace aergo::module;
using namespace aergo::module::helpers::usecase_wrapper;
namespace ri = aergo::module::helpers::robot_interface;
namespace rc = ri::robot_control;

using json = nlohmann::json;



aergo::module::IModule::IngressDecision DemoRobotControl::onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept
{
    if (robot_wrapper_.handlesIngress(kind, local_channel_id, src))
    {
        return robot_wrapper_.onIngress(kind, msg, queue_status);
    }

    // push all other messages to BaseUsecase for decision
    return BaseUsecase::onIngress(kind, local_channel_id, src, msg, queue_status);
}


void DemoRobotControl::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (robot_wrapper_.handlesResponse(request_consumer_id, source_channel))
    {
        robot_wrapper_.processResponse(message);
        return;
    }

    // pass all other responses to BaseUsecase
    BaseUsecase::processResponse(request_consumer_id, source_channel, message);
}

void DemoRobotControl::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    robot_wrapper_.processMessage(subscribe_consumer_id, message);

    // pass all messages also to BaseUsecase
    BaseUsecase::processMessage(subscribe_consumer_id, source_channel, message);
}


std::expected<void, uw::helper::ErrorInfo> DemoRobotControl::createCommandFromParameters(
    const uw::p_desc::ParameterList& auto_parameters,
    const uw::p_desc::ParameterList& required_parameters,
    const uw::p_desc::ParameterList& advanced_parameters,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& auto_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& required_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
    nlohmann::json& out_command_json
)
{
    std::vector<rc::status_messages::deserialization::StatusMessage> status_msgs;

    for (const auto& value : auto_parameter_values[0])
    {
        ri::StatusMessage msg;
        std::vector<std::vector<uint8_t>> blobs;
        if (!readMessageDataAs<ri::StatusMessage>(value.value_, msg, &blobs))
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(0, "DemoRobotControl: Failed to read first status message from auto parameters."));
        }
        if (msg.feature != ri::RobotFeature::ROBOT_CONTROL)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(0, "DemoRobotControl: First status message from auto parameters does not have the expected feature."));
        }
        if (blobs.size() != 1)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(0, "DemoRobotControl: First status message from auto parameters does not have the expected blob data."));
        }

        rc::status_messages::deserialization::BufferReader reader(blobs[0].data(), blobs[0].size());

        rc::status_messages::deserialization::StatusMessage status_msg;
        if (!rc::status_messages::deserialization::deserializeStatusMessage(reader, status_msg))
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(0, "DemoRobotControl: Failed to deserialize first status message blob from auto parameters."));
        }

        status_msgs.push_back(status_msg);
    }

    if (status_msgs.empty())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(0, "DemoRobotControl: No status messages provided in auto parameters."));
    }

    json command_json;
    command_json["target_positions"] = json::array();
    for (const auto& status_msg : status_msgs)
    {        
        json world_position = json::object();
        world_position["x"] = status_msg.end_effector_pose.position.x;
        world_position["y"] = status_msg.end_effector_pose.position.y;
        world_position["z"] = status_msg.end_effector_pose.position.z;

        json world_orientation = json::object();
        world_orientation["w"] = status_msg.end_effector_pose.orientation.w;
        world_orientation["x"] = status_msg.end_effector_pose.orientation.x;
        world_orientation["y"] = status_msg.end_effector_pose.orientation.y;
        world_orientation["z"] = status_msg.end_effector_pose.orientation.z;

        json joint_positions = json::array();
        for (const auto& pos : status_msg.joint_positions)
        {
            joint_positions.push_back(pos);
        }

        json pose = json::object();
        pose["world_position"] = world_position;
        pose["world_orientation"] = world_orientation;
        pose["joint_positions"] = joint_positions;

        command_json["target_positions"].push_back(pose);
    }

    command_json["is_blocking"] = std::get<bool>(required_parameter_values[0][0].value_);
    int32_t movement_type_index = std::get<int32_t>(required_parameter_values[1][0].value_);
    command_json["movement_type"] = movement_type_index;

    if (movement_type_index == 2 || movement_type_index == 3) // ARC or TRAJECTORY
    {
        if (status_msgs.size() < 3)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(0, "DemoRobotControl: ARC and TRAJECTORY movement types require at least 3 target positions."));
        }
    }

    double deg_to_rad = std::numbers::pi / 180.0;
    command_json["joint_speed_rad"] = std::get<double>(advanced_parameter_values[0][0].value_) * deg_to_rad;
    command_json["joint_acceleration_rad"] = std::get<double>(advanced_parameter_values[1][0].value_) * deg_to_rad;

    command_json["speed_mps"] = std::get<double>(advanced_parameter_values[2][0].value_) / 1000.0;
    command_json["acceleration_mps2"] = std::get<double>(advanced_parameter_values[3][0].value_) / 1000.0;

    command_json["orientation_type"] = std::get<int32_t>(advanced_parameter_values[4][0].value_); // enum index
    command_json["as_circle"] = std::get<bool>(advanced_parameter_values[5][0].value_);
    command_json["circle_percent"] = std::get<double>(advanced_parameter_values[6][0].value_) / 100.0;


    out_command_json = command_json;
    
    return std::expected<void, uw::helper::ErrorInfo>{};
}



std::expected<void, uw::helper::ErrorInfo> DemoRobotControl::validateParameters(const nlohmann::json& command_json)
{
    if (!command_json.contains("target_positions") || !command_json["target_positions"].is_array())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "DemoRobotControl: command JSON missing 'target_positions' array."));
    }

    for (const auto& pose_json : command_json["target_positions"])
    {
        if (!pose_json.is_object() 
            || !pose_json.contains("joint_positions") || !pose_json["joint_positions"].is_array() || pose_json["joint_positions"].size() != 7
            || !pose_json.contains("world_position") || !pose_json["world_position"].is_object()
            || !pose_json["world_position"].contains("x") || !pose_json["world_position"]["x"].is_number()
            || !pose_json["world_position"].contains("y") || !pose_json["world_position"]["y"].is_number()
            || !pose_json["world_position"].contains("z") || !pose_json["world_position"]["z"].is_number()
            || !pose_json.contains("world_orientation") || !pose_json["world_orientation"].is_object()
            || !pose_json["world_orientation"].contains("w") || !pose_json["world_orientation"]["w"].is_number()
            || !pose_json["world_orientation"].contains("x") || !pose_json["world_orientation"]["x"].is_number()
            || !pose_json["world_orientation"].contains("y") || !pose_json["world_orientation"]["y"].is_number()
            || !pose_json["world_orientation"].contains("z") || !pose_json["world_orientation"]["z"].is_number())
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "DemoRobotControl: each 'target_positions' entry must be an array of joint positions."));
        }

        for (const auto& pos_json : pose_json["joint_positions"])
        {
            if (!pos_json.is_number())
            {
                return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "DemoRobotControl: each 'target_positions' entry must be an array of joint positions."));
            }
        }
    }

    if (!command_json.contains("is_blocking") || !command_json["is_blocking"].is_boolean())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "DemoRobotControl: command JSON missing 'is_blocking' boolean."));
    }

    if (!command_json.contains("movement_type") || !command_json["movement_type"].is_number_integer())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "DemoRobotControl: command JSON missing 'movement_type' integer."));
    }

    int32_t movement_type = command_json["movement_type"].get<int32_t>();
    if (movement_type < 0 || movement_type > 3) // JOINT, LINEAR, ARC, TRAJECTORY
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "DemoRobotControl: 'movement_type' integer out of range."));
    }

    if ((movement_type == 2 || movement_type == 3) && command_json["target_positions"].size() < 3) // ARC and TRAJECTORY require at least 3 poses
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "DemoRobotControl: 'ARC' and 'TRAJECTORY' movement types require at least 3 target positions."));
    }


    if (!command_json.contains("joint_speed_rad") || !command_json["joint_speed_rad"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "DemoRobotControl: command JSON missing 'joint_speed_rad' number."));
    }

    if (!command_json.contains("joint_acceleration_rad") || !command_json["joint_acceleration_rad"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "DemoRobotControl: command JSON missing 'joint_acceleration_rad' number."));
    }

    if (!command_json.contains("speed_mps") || !command_json["speed_mps"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(9, "DemoRobotControl: command JSON missing 'speed_mps' number."));
    }

    if (!command_json.contains("acceleration_mps2") || !command_json["acceleration_mps2"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(10, "DemoRobotControl: command JSON missing 'acceleration_mps2' number."));
    }

    if (!command_json.contains("orientation_type") || !command_json["orientation_type"].is_number_integer() || 
        command_json["orientation_type"].get<int32_t>() < 0 || command_json["orientation_type"].get<int32_t>() > 1) // FIXED, TOOL
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(11, "DemoRobotControl: command JSON missing 'orientation_type' integer or invalid option."));
    }

    if (!command_json.contains("as_circle") || !command_json["as_circle"].is_boolean())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(12, "DemoRobotControl: command JSON missing 'as_circle' boolean."));
    }

    if (!command_json.contains("circle_percent") || !command_json["circle_percent"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(13, "DemoRobotControl: command JSON missing 'circle_percent' number."));
    }


    // all checks passed
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> DemoRobotControl::runProgram(
    const nlohmann::json& command_json, 
    bool simulated
)
{
    std::vector<std::vector<double>> target_positions_joint;
    std::vector<rc::Pose> target_positions_world;

    target_positions_joint.reserve(command_json["target_positions"].size());
    target_positions_world.reserve(command_json["target_positions"].size());
    
    for (const auto& pose_json : command_json["target_positions"])
    {
        auto& pose_joint_json = pose_json["joint_positions"];
        auto& pose_world_json = pose_json["world_position"];
        auto& pose_orientation_json = pose_json["world_orientation"];

        std::vector<double> joint_positions;
        joint_positions.reserve(pose_joint_json.size());
        for (const auto& pos_json : pose_joint_json)
        {
            joint_positions.push_back(pos_json.get<double>());
        }
        if (joint_positions.size() != 7)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "DemoRobotControl: each 'target_positions' entry must have exactly 7 joint positions."));
        }
        target_positions_joint.push_back(std::move(joint_positions));

        target_positions_world.emplace_back(rc::Pose {
            .position = {
                .x = pose_world_json["x"].get<double>(),
                .y = pose_world_json["y"].get<double>(),
                .z = pose_world_json["z"].get<double>()
            },
            .orientation = {
                .x = pose_orientation_json["x"].get<double>(),
                .y = pose_orientation_json["y"].get<double>(),
                .z = pose_orientation_json["z"].get<double>(),
                .w = pose_orientation_json["w"].get<double>()
            }
        });
    }

    const bool blocking = command_json["is_blocking"].get<bool>();
    const int32_t movement_type = command_json["movement_type"].get<int32_t>(); // JOINT=0, LINEAR=1, ARC=2, TRAJECTORY=3

    const double joint_speed_rad = command_json["joint_speed_rad"].get<double>();
    const double joint_acceleration_rad = command_json["joint_acceleration_rad"].get<double>();

    const double speed_mps = command_json["speed_mps"].get<double>();
    const double acceleration_mps2 = command_json["acceleration_mps2"].get<double>();

    const int32_t orientation_type = command_json["orientation_type"].get<int32_t>(); // FIXED=0, TOOL=1
    const bool as_circle = command_json["as_circle"].get<bool>();
    const double circle_percent = command_json["circle_percent"].get<double>();

    if (movement_type == 0)
    {
        double deg_to_rad = std::numbers::pi / 180.0;
        if (joint_speed_rad <= 1 * deg_to_rad || joint_speed_rad > 225.0 * deg_to_rad)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "DemoRobotControl: 'joint_speed_rad' must be in range [1, 225 degrees], got " + std::to_string(joint_speed_rad / deg_to_rad) + " degrees."));
        }
        if (joint_acceleration_rad <= 1 * deg_to_rad || joint_acceleration_rad > 360.0 * deg_to_rad)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "DemoRobotControl: 'joint_acceleration_rad' must be in range [1, 360 degrees], got " + std::to_string(joint_acceleration_rad / deg_to_rad) + " degrees."));
        }
    }
    else
    {
        if (speed_mps <= 0.01 || speed_mps > 2.0)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(9, "DemoRobotControl: 'speed_mps' must be in range [0.01, 2.0], got " + std::to_string(speed_mps) + "."));
        }
        if (acceleration_mps2 <= 0.01 || acceleration_mps2 > 8.0)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(10, "DemoRobotControl: 'acceleration_mps2' must be in range [0.01, 8.0], got " + std::to_string(acceleration_mps2) + "."));
        }
        if (movement_type == 2 && as_circle && circle_percent < 0.01) // ARC with circle
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(13, "DemoRobotControl: 'circle_percent' must be in range at least 1%, got " + std::to_string(circle_percent * 100) + "."));
        }
    }
    

    if (movement_type == 0) // JOINT
    {
        for (const auto& joint_positions : target_positions_joint)
        {
            std::string pos_str = "DemoRobotControl: Joint moving to target positions:\n";
            for (size_t i = 0; i < joint_positions.size(); ++i)
            {
                pos_str += "\tJ" + std::to_string(i + 1) + ": " + std::to_string(joint_positions[i] * (180.0 / std::numbers::pi)) + " deg;\n";
            }
            log(logging::LogType::INFO, pos_str);
            
            rc::MoveRequestResult res = robot_wrapper_.moveJoint(
                joint_positions,
                joint_speed_rad,
                joint_acceleration_rad,
                blocking
            );

            if (!res.success_)
            {
                return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "DemoRobotControl: Failed to send move joint command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)));
            }
            
            if (!blocking)
            {
                auto async_res = asyncWaitForFinish(res.action_id_);
                if (!async_res) return async_res;
            }
            handleControlRequests(true, true);
        }
    }
    else if (movement_type == 1) // LINEAR
    {
        for (size_t i = 0; i < target_positions_world.size(); ++i)
        {
            log(logging::LogType::INFO, "DemoRobotControl: Linear moving to target position " + std::to_string(i + 1) + ".");

            rc::MoveRequestResult res = robot_wrapper_.moveLinear(
                target_positions_world[i],
                speed_mps,
                acceleration_mps2,
                blocking
            );

            if (!res.success_)
            {
                return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "DemoRobotControl: Failed to send move linear command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)));
            }

            if (!blocking)
            {
                auto async_res = asyncWaitForFinish(res.action_id_);
                if (!async_res) return async_res;
            }
            handleControlRequests(true, true);
        }
    }
    else if (movement_type == 2) // ARC
    {
        log(logging::LogType::INFO, "DemoRobotControl: Arc moving to target positions.");

        rc::MoveRequestResult res = robot_wrapper_.moveLinear(
            target_positions_world[0],
            speed_mps,
            acceleration_mps2,
            blocking
        );
        if (!res.success_)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "DemoRobotControl: Failed to send move arc command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)));
        }
        if (!blocking)
        {
            auto async_res = asyncWaitForFinish(res.action_id_);
            if (!async_res) return async_res;
        }
        handleControlRequests(true, true);

        res = robot_wrapper_.moveArc(
            target_positions_world[1],
            target_positions_world[2],
            speed_mps,
            acceleration_mps2,
            static_cast<rc::OrientationType>(orientation_type),
            as_circle,
            circle_percent,
            blocking
        );
        if (!res.success_)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "DemoRobotControl: Failed to send move arc command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)));
        }
        if (!blocking)
        {
            auto async_res = asyncWaitForFinish(res.action_id_);
            if (!async_res) return async_res;
        }
        handleControlRequests(true, true);
    }
    else if (movement_type == 3) // TRAJECTORY
    {
        log(logging::LogType::INFO, "DemoRobotControl: Trajectory moving to target positions.");

        rc::MoveRequestResult res = robot_wrapper_.moveTrajectory(
            target_positions_world,
            speed_mps,
            acceleration_mps2,
            static_cast<rc::OrientationType>(orientation_type),
            blocking
        );

        if (!res.success_)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "DemoRobotControl: Failed to send move trajectory command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)));
        }

        if (!blocking)
        {
            auto async_res = asyncWaitForFinish(res.action_id_);
            if (!async_res) return async_res;
        }
        handleControlRequests(true, true);
    }
    

    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> DemoRobotControl::asyncWaitForFinish(uint64_t action_id)
{
    bool cancel_requested = false;
    while (robot_wrapper_.isActionActive(action_id))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (cancel_requested)
        {
            continue; // already requested cancel, just wait for action to end
        }

        auto [pause_requested, stop_requested] = checkControlRequests();
        if (stop_requested)
        {
            log(logging::LogType::INFO, "DemoRobotControl: Stop requested, cancelling robot action " + std::to_string(action_id) + ".");

            rc::MoveRequestResult cancel_res = robot_wrapper_.cancelAction(action_id);
            if (!cancel_res.success_)
            {
                return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "DemoRobotControl: Failed to send cancel command to robot for action " + std::to_string(action_id) + ": " + (cancel_res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : cancel_res.err_message_)));
            }
            cancel_requested = true;
        }
    }

    return std::expected<void, uw::helper::ErrorInfo>{};
}