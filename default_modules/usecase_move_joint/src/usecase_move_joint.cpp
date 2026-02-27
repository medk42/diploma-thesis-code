#include "usecase_move_joint.h"
#include "module_helpers/robot_interface/message_types_definitions.h"
#include "module_helpers/robot_interface/message_types.h"
#include "module_helpers/robot_interface/features/robot_control/messages.h"
#include "module_helpers/robot_wrapper/robot_wrapper.h"

#include <numbers>


using namespace aergo::default_modules::usecase_move_joint;
using namespace aergo::module;
using namespace aergo::module::helpers::usecase_wrapper;
namespace ri = aergo::module::helpers::robot_interface;

using json = nlohmann::json;



UsecaseMoveJoint::UsecaseMoveJoint(
    const char* data_path, 
    ICore* core, 
    InputChannelMapInfo channel_map_info, 
    const logging::ILogger* logger, 
    uint64_t module_id, 
    const ModuleInfo* module_info,
    bool supports_multi_program,
    bool supports_pause,
    bool supports_stop
) : BaseUsecase(data_path, core, channel_map_info, logger, module_id, module_info, supports_multi_program, supports_pause, supports_stop),
    robot_wrapper_(*this)
{
    if (!robot_wrapper_.valid())
    {
        log(logging::LogType::ERROR, "UsecaseMoveJoint: Failed to initialize robot wrapper.");
        return;
    }

    if (!getSubscribeChannelByName(ri::robot_interface_status_consumer.channel_type_identifier_, robot_interface_status_channel_id_))
    {
        log(logging::LogType::ERROR, "UsecaseMoveJoint: Failed to get robot interface status subscribe channel.");
        return;
    }

    valid_ = true;
}


aergo::module::IModule::IngressDecision UsecaseMoveJoint::onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept
{
    if (robot_wrapper_.handlesIngress(kind, local_channel_id, src))
    {
        return robot_wrapper_.onIngress(kind, msg, queue_status);
    }

    // push all other messages to BaseUsecase for decision
    return BaseUsecase::onIngress(kind, local_channel_id, src, msg, queue_status);
}


void UsecaseMoveJoint::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (robot_wrapper_.handlesResponse(request_consumer_id, source_channel))
    {
        robot_wrapper_.processResponse(message);
        return;
    }

    // pass all other responses to BaseUsecase
    BaseUsecase::processResponse(request_consumer_id, source_channel, message);
}


void UsecaseMoveJoint::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    robot_wrapper_.processMessage(subscribe_consumer_id, message);

    // pass all messages also to BaseUsecase
    BaseUsecase::processMessage(subscribe_consumer_id, source_channel, message);
}


UsecaseMoveJoint::ProcessingResult UsecaseMoveJoint::processCustomMessageOrResponse(
    ProcessingChannelType channel_type, 
    uint32_t consumer_id, 
    ChannelIdentifier source_channel, 
    message::MessageHeader message, 
    std::vector<uint8_t>& out_data_replace
)
{
    if (consumer_id != robot_interface_status_channel_id_)
    {
        log(logging::LogType::WARNING, "UsecaseMoveJoint: Received message from unexpected channel.");
        return ProcessingResult::DROP;
    }

    ri::StatusMessage status_msg;
    if (!message.readAs(status_msg))
    {
        log(logging::LogType::WARNING, "UsecaseMoveJoint: Received message is not a robot interface status message.");
        return ProcessingResult::DROP;
    }

    if (status_msg.feature != ri::RobotFeature::ROBOT_CONTROL)
    {
        // ignore messages for other features
        return ProcessingResult::DROP;
    }

    if (message.blob_count_ != 1 || message.blobs_ == nullptr || !message.blobs_[0].valid())
    {
        log(logging::LogType::WARNING, "UsecaseMoveJoint: Robot interface status message missing data blob.");
        return ProcessingResult::DROP;
    }

    message::SharedDataBlob blob = message.blobs_[0];
    rc::BufferReader reader(blob.data(), blob.size());
    rc::status_messages::deserialization::StatusMessage status;
    if (!rc::status_messages::deserialization::deserializeStatusMessage(reader, status))
    {
        log(logging::LogType::WARNING, "UsecaseMoveJoint: Failed to deserialize robot interface status message.");
        return ProcessingResult::DROP;
    }

    if (status.joint_positions.size() == 0)
    {
        log(logging::LogType::WARNING, "UsecaseMoveJoint: Robot interface status message has no joint positions.");
        return ProcessingResult::DROP;
    }

    out_data_replace.clear();
    out_data_replace.reserve(sizeof(size_t) + sizeof(double) * status.joint_positions.size());

    using aergo::module::helpers::serialization_helper::serialization::push;
    push<size_t>(out_data_replace, status.joint_positions.size());
    for (double joint_pos : status.joint_positions)
    {
        push<double>(out_data_replace, joint_pos);
    }
    return ProcessingResult::ACCEPT_REPLACE;
}
std::expected<void, uw::helper::ErrorInfo> UsecaseMoveJoint::createCommandFromParameters(
    const uw::p_desc::ParameterList& auto_parameters,
    const uw::p_desc::ParameterList& required_parameters,
    const uw::p_desc::ParameterList& advanced_parameters,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& auto_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& required_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
    nlohmann::json& out_command_json,
    uw::IUsecaseModule::UsecaseVisualization& out_visualization
)
{
    if (auto_parameter_values.size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseMoveJoint: Expected 1 auto parameter value, got " + std::to_string(auto_parameter_values.size())));
    }

    if (auto_parameter_values[0].size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "UsecaseMoveJoint: Expected 1 auto parameter value, got " + std::to_string(auto_parameter_values[0].size())));
    }

    if (!std::holds_alternative<std::vector<uint8_t>>(auto_parameter_values[0][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "UsecaseMoveJoint: Expected vector<uint8_t> auto parameter value, got " + std::to_string(auto_parameter_values[0][0].value_.index())));
    }
    const auto& data = std::get<std::vector<uint8_t>>(auto_parameter_values[0][0].value_);

    using aergo::module::helpers::serialization_helper::deserialization::BufferReader;
    BufferReader reader(data.data(), data.size());
    std::vector<double> joint_positions;
    size_t joint_count = 0;
    if (!reader.read<size_t>(joint_count))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "UsecaseMoveJoint: Failed to read target joints from auto parameter value."));
    }
    joint_positions.resize(joint_count);
    for (size_t i = 0; i < joint_count; ++i)
    {
        if (!reader.read<double>(joint_positions[i]))
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecaseMoveJoint: Failed to read target joint " + std::to_string(i) + " from auto parameter value."));
        }
    }

    if (advanced_parameter_values.size() != 2 || advanced_parameter_values[0].size() != 1 || advanced_parameter_values[1].size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "UsecaseMoveJoint: Expected 2 advanced parameter values, got " + std::to_string(advanced_parameter_values.size())));
    }
    if (!std::holds_alternative<double>(advanced_parameter_values[0][0].value_) || !std::holds_alternative<double>(advanced_parameter_values[1][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "UsecaseMoveJoint: Expected double advanced parameter values, got " + std::to_string(advanced_parameter_values[0][0].value_.index()) + " and " + std::to_string(advanced_parameter_values[1][0].value_.index())));
    }
    double speed_deg_s = std::get<double>(advanced_parameter_values[0][0].value_);
    double acceleration_deg_s2 = std::get<double>(advanced_parameter_values[1][0].value_);
    if (speed_deg_s <= 0 || acceleration_deg_s2 <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "UsecaseMoveJoint: Speed and acceleration must be greater than 0."));
    }

    // Convert from degrees to radians
    const double deg_to_rad = std::numbers::pi / 180.0;
    double speed_rad_s = speed_deg_s * deg_to_rad;
    double acceleration_rad_s2 = acceleration_deg_s2 * deg_to_rad;

    // Build command JSON
    json command_json;
    
    // Store joint positions as JSON array
    command_json["joint_positions"] = json::array();
    for (double joint_pos : joint_positions)
    {
        command_json["joint_positions"].push_back(joint_pos);
    }
    
    // Store speed and acceleration in radians
    command_json["joint_speed_rad_s"] = speed_rad_s;
    command_json["joint_acceleration_rad_s2"] = acceleration_rad_s2;

    out_command_json = command_json;
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveJoint::validateParameters(const nlohmann::json& command_json)
{
    // Validate joint_positions array
    if (!command_json.contains("joint_positions") || !command_json["joint_positions"].is_array())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseMoveJoint: command JSON missing 'joint_positions' array."));
    }

    const auto& joint_positions = command_json["joint_positions"];
    if (joint_positions.size() == 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "UsecaseMoveJoint: 'joint_positions' array must contain at least one joint position."));
    }

    if (joint_positions.size() > 16)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "UsecaseMoveJoint: 'joint_positions' array must contain at most 16 joint positions, got " + std::to_string(joint_positions.size())));
    }

    // Validate each joint position is a number
    for (size_t i = 0; i < joint_positions.size(); ++i)
    {
        if (!joint_positions[i].is_number())
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "UsecaseMoveJoint: 'joint_positions[" + std::to_string(i) + "]' must be a number."));
        }
    }

    // Validate joint_speed_rad_s
    if (!command_json.contains("joint_speed_rad_s") || !command_json["joint_speed_rad_s"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecaseMoveJoint: command JSON missing 'joint_speed_rad_s' number."));
    }
    double speed = command_json["joint_speed_rad_s"].get<double>();
    if (speed <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "UsecaseMoveJoint: 'joint_speed_rad_s' must be greater than 0."));
    }

    // Validate joint_acceleration_rad_s2
    if (!command_json.contains("joint_acceleration_rad_s2") || !command_json["joint_acceleration_rad_s2"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "UsecaseMoveJoint: command JSON missing 'joint_acceleration_rad_s2' number."));
    }
    double acceleration = command_json["joint_acceleration_rad_s2"].get<double>();
    if (acceleration <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "UsecaseMoveJoint: 'joint_acceleration_rad_s2' must be greater than 0."));
    }

    // all checks passed, return success
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveJoint::runProgram(const nlohmann::json& command_json, bool simulated)
{
    // Extract joint positions
    const auto& joint_positions_json = command_json["joint_positions"];
    std::vector<double> joint_positions;
    joint_positions.reserve(joint_positions_json.size());
    for (const auto& pos_json : joint_positions_json)
    {
        joint_positions.push_back(pos_json.get<double>());
    }

    const double speed_rad_s = command_json["joint_speed_rad_s"].get<double>();
    const double acceleration_rad_s2 = command_json["joint_acceleration_rad_s2"].get<double>();


    rc::MoveRequestResult res = robot_wrapper_.moveJoint(joint_positions, speed_rad_s, acceleration_rad_s2, false);
    if (!res.success_)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseMoveJoint: Failed to send move joint command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)));
    }

    auto async_res = asyncWaitForFinish(res.action_id_);
    if (async_res.error) return std::unexpected(async_res.error.value());
    if (async_res.stopped) handleControlRequests(false, true); // if stopped, handle the stop request (ends runProgram via StopException)
    
    return std::expected<void, uw::helper::ErrorInfo>{};
}


UsecaseMoveJoint::AsyncResult UsecaseMoveJoint::asyncWaitForFinish(uint64_t action_id)
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
            log(logging::LogType::INFO, "UsecaseMoveJoint: Stop requested, cancelling robot action " + std::to_string(action_id) + ".");

            rc::MoveRequestResult cancel_res = robot_wrapper_.cancelAction(action_id);
            if (!cancel_res.success_)
            {
                return AsyncResult
                {
                    .stopped = true,
                    .error = uw::helper::ErrorInfo::WithDetails(5, "UsecaseMoveJoint: Failed to send cancel command to robot for action " + std::to_string(action_id) + ": " + (cancel_res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : cancel_res.err_message_))
                };
            }
            cancel_requested = true;
        }
    }

    return AsyncResult{ 
        .stopped = cancel_requested, 
        .error = std::nullopt 
    };
}