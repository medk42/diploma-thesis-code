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
        json joint_positions = json::array();
        for (const auto& pos : status_msg.joint_positions)
        {
            joint_positions.push_back(pos);
        }

        command_json["target_positions"].push_back(joint_positions);
    }

    double deg_to_rad = std::numbers::pi / 180.0;
    command_json["joint_speed_rad"] = std::get<double>(advanced_parameter_values[0][0].value_) * deg_to_rad;
    command_json["joint_acceleration_rad"] = std::get<double>(advanced_parameter_values[1][0].value_) * deg_to_rad;

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
        if (!pose_json.is_array() || pose_json.size() != 7)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "DemoRobotControl: each 'target_positions' entry must be an array of 7 elements."));
        }
    }

    if (!command_json.contains("joint_speed_rad") || !command_json["joint_speed_rad"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "DemoRobotControl: command JSON missing 'joint_speed_rad' number."));
    }

    if (!command_json.contains("joint_acceleration_rad") || !command_json["joint_acceleration_rad"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "DemoRobotControl: command JSON missing 'joint_acceleration_rad' number."));
    }

    // all checks passed
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> DemoRobotControl::runProgram(
    const nlohmann::json& command_json, 
    bool simulated
)
{
    std::vector<std::vector<double>> target_positions;
    target_positions.reserve(command_json["target_positions"].size());
    for (const auto& pose_json : command_json["target_positions"])
    {
        std::vector<double> joint_positions;
        joint_positions.reserve(pose_json.size());
        for (const auto& pos_json : pose_json)
        {
            joint_positions.push_back(pos_json.get<double>());
        }
        if (joint_positions.size() != 7)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "DemoRobotControl: each 'target_positions' entry must have exactly 7 joint positions."));
        }
        target_positions.push_back(std::move(joint_positions));
    }

    const double joint_speed_rad = command_json["joint_speed_rad"].get<double>();
    const double joint_acceleration_rad = command_json["joint_acceleration_rad"].get<double>();

    double deg_to_rad = std::numbers::pi / 180.0;
    if (joint_speed_rad <= 1 * deg_to_rad || joint_speed_rad > 225.0 * deg_to_rad)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "DemoRobotControl: 'joint_speed_rad' must be in range [1, 225 degrees], got " + std::to_string(joint_speed_rad / deg_to_rad) + " degrees."));
    }
    if (joint_acceleration_rad <= 1 * deg_to_rad || joint_acceleration_rad > 360.0 * deg_to_rad)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "DemoRobotControl: 'joint_acceleration_rad' must be in range [1, 360 degrees], got " + std::to_string(joint_acceleration_rad / deg_to_rad) + " degrees."));
    }


    for (const auto& joint_positions : target_positions)
    {
        std::string pos_str = "DemoRobotControl: Moving to target positions:\n";
        for (size_t i = 0; i < joint_positions.size(); ++i)
        {
            pos_str += "\tJ" + std::to_string(i + 1) + ": " + std::to_string(joint_positions[i] * (180.0 / std::numbers::pi)) + " deg;\n";
        }
        log(logging::LogType::INFO, pos_str);
        
        rc::MoveRequestResult res = robot_wrapper_.moveJoint(
            joint_positions,
            joint_speed_rad,
            joint_acceleration_rad,
            false // false == async
        );

        if (!res.success_)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "DemoRobotControl: Failed to send move joint command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)));
        }
        
        bool cancel_requested = false;
        while (robot_wrapper_.isActionActive(res.action_id_))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            if (cancel_requested)
            {
                continue; // already requested cancel, just wait for action to end
            }

            auto [pause_requested, stop_requested] = checkControlRequests();
            if (stop_requested)
            {
                log(logging::LogType::INFO, "DemoRobotControl: Stop requested, cancelling robot action " + std::to_string(res.action_id_) + ".");

                rc::MoveRequestResult cancel_res = robot_wrapper_.cancelAction(res.action_id_);
                if (!cancel_res.success_)
                {
                    return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "DemoRobotControl: Failed to send cancel command to robot for action " + std::to_string(res.action_id_) + ": " + (cancel_res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : cancel_res.err_message_)));
                }
                cancel_requested = true;
            }
        }

        handleControlRequests(true, true);
    }
    

    return std::expected<void, uw::helper::ErrorInfo>{};
}