#include "usecase_move_arc.h"

#include "module_helpers/pen_messages/message_types.h"

#include <numbers>

using namespace aergo::default_modules::usecase_move_arc;
using namespace aergo::module;
using namespace aergo::module::helpers::usecase_wrapper;
namespace pm = aergo::module::helpers::pen_messages;

using json = nlohmann::json;



UsecaseMoveArc::UsecaseMoveArc(
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
    if (!getSubscribeChannelByName(pm::pen_message_intent_subscribe_consumer.channel_type_identifier_, pen_message_intent_subscribe_channel_id_))
    {
        log(logging::LogType::ERROR, "UsecaseMoveArc: Failed to get pen message intent subscribe channel.");
        return;
    }

    valid_ = true;
}


aergo::module::IModule::IngressDecision UsecaseMoveArc::onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept
{
    if (robot_wrapper_.handlesIngress(kind, local_channel_id, src))
    {
        return robot_wrapper_.onIngress(kind, msg, queue_status);
    }

    // push all other messages to BaseUsecase for decision
    return BaseUsecase::onIngress(kind, local_channel_id, src, msg, queue_status);
}


void UsecaseMoveArc::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (robot_wrapper_.handlesResponse(request_consumer_id, source_channel))
    {
        robot_wrapper_.processResponse(message);
        return;
    }

    // pass all other responses to BaseUsecase
    BaseUsecase::processResponse(request_consumer_id, source_channel, message);
}


void UsecaseMoveArc::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    robot_wrapper_.processMessage(subscribe_consumer_id, message);

    // pass all messages also to BaseUsecase
    BaseUsecase::processMessage(subscribe_consumer_id, source_channel, message);
}


UsecaseMoveArc::ProcessingResult UsecaseMoveArc::processCustomMessageOrResponse(
    ProcessingChannelType channel_type, 
    uint32_t consumer_id, 
    ChannelIdentifier source_channel, 
    message::MessageHeader message, 
    std::vector<uint8_t>& out_data_replace
)
{
    if (consumer_id != pen_message_intent_subscribe_channel_id_)
    {
        log(logging::LogType::WARNING, "UsecaseMoveArc: Received message from unexpected channel.");
        return ProcessingResult::DROP;
    }

    pm::PenMessageIntent pen_message_intent;
    if (!message.readAs(pen_message_intent))
    {
        log(logging::LogType::WARNING, "UsecaseMoveArc: Received message is not a pen message intent.");
        return ProcessingResult::DROP;
    }

    if (pen_message_intent.intent != pm::PenIntent::POSE)
    {
        return ProcessingResult::DROP;
    }

    out_data_replace.clear();
    out_data_replace.reserve(sizeof(double) * 7);

    using aergo::module::helpers::serialization_helper::serialization::push;
    push<double>(out_data_replace, pen_message_intent.pose.x);
    push<double>(out_data_replace, pen_message_intent.pose.y);
    push<double>(out_data_replace, pen_message_intent.pose.z);
    push<double>(out_data_replace, pen_message_intent.pose.qx);
    push<double>(out_data_replace, pen_message_intent.pose.qy);
    push<double>(out_data_replace, pen_message_intent.pose.qz);
    push<double>(out_data_replace, pen_message_intent.pose.qw);

    return ProcessingResult::ACCEPT_REPLACE;
}


bool UsecaseMoveArc::readPose(const std::vector<std::vector<uw::helper::ParameterTypeValue>>& auto_parameter_values, size_t index, nlohmann::json& out_pose_json)
{
    if (index >= auto_parameter_values.size())
    {
        return false;
    }

    if (auto_parameter_values[index].size() != 1)
    {
        return false;
    }

    if (!std::holds_alternative<std::vector<uint8_t>>(auto_parameter_values[index][0].value_))
    {
        return false;
    }

    const auto& data = std::get<std::vector<uint8_t>>(auto_parameter_values[index][0].value_);

    pm::Pose pose;

    using aergo::module::helpers::serialization_helper::deserialization::BufferReader;
    BufferReader reader(data.data(), data.size());
    if (!reader.read<double>(pose.x) ||
        !reader.read<double>(pose.y) ||
        !reader.read<double>(pose.z) ||
        !reader.read<double>(pose.qx) ||
        !reader.read<double>(pose.qy) ||
        !reader.read<double>(pose.qz) ||
        !reader.read<double>(pose.qw))
    {
        return false;
    }

    out_pose_json = json::object();
    out_pose_json["x"] = pose.x;
    out_pose_json["y"] = pose.y;
    out_pose_json["z"] = pose.z;
    out_pose_json["qx"] = pose.qx;
    out_pose_json["qy"] = pose.qy;
    out_pose_json["qz"] = pose.qz;
    out_pose_json["qw"] = pose.qw;

    return true;
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveArc::createCommandFromParameters(
    const uw::p_desc::ParameterList& auto_parameters,
    const uw::p_desc::ParameterList& required_parameters,
    const uw::p_desc::ParameterList& advanced_parameters,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& auto_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& required_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
    nlohmann::json& out_command_json
)
{
    if (auto_parameter_values.size() != 3)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseMoveArc: Expected 3 auto parameter value groups, got " + std::to_string(auto_parameter_values.size())));
    }

    if (auto_parameter_values[0].size() != 1 || auto_parameter_values[1].size() != 1 || auto_parameter_values[2].size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "UsecaseMoveArc: Expected 1 value per auto parameter group."));
    }

    // Read arc start pose
    json arc_start_pose;
    if (!readPose(auto_parameter_values, 0, arc_start_pose))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "UsecaseMoveArc: Failed to read arc start pose from auto parameter value."));
    }

    // Read arc through point pose
    json arc_through_pose;
    if (!readPose(auto_parameter_values, 1, arc_through_pose))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "UsecaseMoveArc: Failed to read arc through point pose from auto parameter value."));
    }

    // Read arc end pose
    json arc_end_pose;
    if (!readPose(auto_parameter_values, 2, arc_end_pose))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecaseMoveArc: Failed to read arc end pose from auto parameter value."));
    }

    // Read advanced parameters: speed, acceleration, as_circle, circle_degrees, orientation_type
    if (advanced_parameter_values.size() != 5)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "UsecaseMoveArc: Expected 5 advanced parameter value groups, got " + std::to_string(advanced_parameter_values.size())));
    }

    if (advanced_parameter_values[0].size() != 1 || advanced_parameter_values[1].size() != 1 || advanced_parameter_values[2].size() != 1 || advanced_parameter_values[3].size() != 1 || advanced_parameter_values[4].size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "UsecaseMoveArc: Expected 1 value per advanced parameter group."));
    }

    // Read speed
    if (!std::holds_alternative<double>(advanced_parameter_values[0][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "UsecaseMoveArc: Expected double for speed, got " + std::to_string(advanced_parameter_values[0][0].value_.index())));
    }
    double speed = std::get<double>(advanced_parameter_values[0][0].value_);

    // Read acceleration
    if (!std::holds_alternative<double>(advanced_parameter_values[1][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(9, "UsecaseMoveArc: Expected double for acceleration, got " + std::to_string(advanced_parameter_values[1][0].value_.index())));
    }
    double acceleration = std::get<double>(advanced_parameter_values[1][0].value_);

    // Read as_circle
    if (!std::holds_alternative<bool>(advanced_parameter_values[2][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(10, "UsecaseMoveArc: Expected bool for as_circle, got " + std::to_string(advanced_parameter_values[2][0].value_.index())));
    }
    bool as_circle = std::get<bool>(advanced_parameter_values[2][0].value_);

    // Read circle_degrees
    if (!std::holds_alternative<double>(advanced_parameter_values[3][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(11, "UsecaseMoveArc: Expected double for circle_degrees, got " + std::to_string(advanced_parameter_values[3][0].value_.index())));
    }
    double circle_degrees = std::get<double>(advanced_parameter_values[3][0].value_);

    // Read orientation_type
    if (!std::holds_alternative<int32_t>(advanced_parameter_values[4][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(12, "UsecaseMoveArc: Expected int32_t for orientation_type, got " + std::to_string(advanced_parameter_values[4][0].value_.index())));
    }
    int32_t orientation_type_index = std::get<int32_t>(advanced_parameter_values[4][0].value_);
    if (orientation_type_index < 0 || orientation_type_index > 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(13, "UsecaseMoveArc: Orientation type index must be 0 (Fixed) or 1 (Tangent), got " + std::to_string(orientation_type_index)));
    }

    // Validate speed and acceleration
    if (speed <= 0 || acceleration <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(14, "UsecaseMoveArc: Speed and acceleration must be greater than 0."));
    }

    // Validate circle_degrees
    if (circle_degrees <= 0 || circle_degrees > 720)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(15, "UsecaseMoveArc: Circle degrees must be greater than 0 and less than or equal to 720."));
    }

    // Build command JSON
    json command_json;
    command_json["arc_start"] = arc_start_pose;
    command_json["arc_through"] = arc_through_pose;
    command_json["arc_end"] = arc_end_pose;
    command_json["speed_mm_s"] = speed;
    command_json["acceleration_mm_s2"] = acceleration;
    command_json["as_circle"] = as_circle;
    command_json["circle_degrees_deg"] = circle_degrees;
    command_json["orientation_type"] = orientation_type_index;

    out_command_json = command_json;
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveArc::validatePose(const nlohmann::json& command_json, const std::string& pose_key, int error_code_base)
{
    if (!command_json.contains(pose_key) || !command_json[pose_key].is_object())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base, "UsecaseMoveArc: command JSON missing '" + pose_key + "' object."));
    }
    if (!command_json[pose_key].contains("x") || !command_json[pose_key]["x"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 1, "UsecaseMoveArc: command JSON missing '" + pose_key + ".x' number."));
    }
    if (!command_json[pose_key].contains("y") || !command_json[pose_key]["y"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 2, "UsecaseMoveArc: command JSON missing '" + pose_key + ".y' number."));
    }
    if (!command_json[pose_key].contains("z") || !command_json[pose_key]["z"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 3, "UsecaseMoveArc: command JSON missing '" + pose_key + ".z' number."));
    }
    if (!command_json[pose_key].contains("qx") || !command_json[pose_key]["qx"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 4, "UsecaseMoveArc: command JSON missing '" + pose_key + ".qx' number."));
    }
    if (!command_json[pose_key].contains("qy") || !command_json[pose_key]["qy"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 5, "UsecaseMoveArc: command JSON missing '" + pose_key + ".qy' number."));
    }
    if (!command_json[pose_key].contains("qz") || !command_json[pose_key]["qz"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 6, "UsecaseMoveArc: command JSON missing '" + pose_key + ".qz' number."));
    }
    if (!command_json[pose_key].contains("qw") || !command_json[pose_key]["qw"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 7, "UsecaseMoveArc: command JSON missing '" + pose_key + ".qw' number."));
    }

    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveArc::validateParameters(const nlohmann::json& command_json)
{
    // Validate arc_start pose
    auto result = validatePose(command_json, "arc_start", 1);
    if (!result)
    {
        return result;
    }

    // Validate arc_through pose
    result = validatePose(command_json, "arc_through", 9);
    if (!result)
    {
        return result;
    }

    // Validate arc_end pose
    result = validatePose(command_json, "arc_end", 17);
    if (!result)
    {
        return result;
    }

    // Validate speed_mm_s
    if (!command_json.contains("speed_mm_s") || !command_json["speed_mm_s"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(25, "UsecaseMoveArc: command JSON missing 'speed_mm_s' number."));
    }
    double speed = command_json["speed_mm_s"].get<double>();
    if (speed <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(26, "UsecaseMoveArc: Speed must be greater than 0."));
    }

    // Validate acceleration_mm_s2
    if (!command_json.contains("acceleration_mm_s2") || !command_json["acceleration_mm_s2"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(27, "UsecaseMoveArc: command JSON missing 'acceleration_mm_s2' number."));
    }
    double acceleration = command_json["acceleration_mm_s2"].get<double>();
    if (acceleration <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(28, "UsecaseMoveArc: Acceleration must be greater than 0."));
    }

    // Validate as_circle
    if (!command_json.contains("as_circle") || !command_json["as_circle"].is_boolean())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(29, "UsecaseMoveArc: command JSON missing 'as_circle' boolean."));
    }

    // Validate circle_degrees_deg
    if (!command_json.contains("circle_degrees_deg") || !command_json["circle_degrees_deg"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(30, "UsecaseMoveArc: command JSON missing 'circle_degrees_deg' number."));
    }
    double circle_degrees = command_json["circle_degrees_deg"].get<double>();
    if (circle_degrees <= 0 || circle_degrees > 720)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(31, "UsecaseMoveArc: Circle degrees must be greater than 0 and less than or equal to 720."));
    }

    // Validate orientation_type
    if (!command_json.contains("orientation_type") || !command_json["orientation_type"].is_number_integer())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(32, "UsecaseMoveArc: command JSON missing 'orientation_type' integer."));
    }
    int32_t orientation_type = command_json["orientation_type"].get<int32_t>();
    if (orientation_type < 0 || orientation_type > 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(33, "UsecaseMoveArc: Orientation type must be 0 (Fixed) or 1 (Tangent), got " + std::to_string(orientation_type)));
    }

    // all checks passed, return success
    return std::expected<void, uw::helper::ErrorInfo>{};
}


rc::Pose UsecaseMoveArc::extractPoseFromJson(const nlohmann::json& pose_json)
{
    return rc::Pose{
        .position = {
            .x = pose_json["x"].get<double>(),
            .y = pose_json["y"].get<double>(),
            .z = pose_json["z"].get<double>()
        },
        .orientation = {
            .x = pose_json["qx"].get<double>(),
            .y = pose_json["qy"].get<double>(),
            .z = pose_json["qz"].get<double>(),
            .w = pose_json["qw"].get<double>()
        }
    };
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveArc::runProgram(const nlohmann::json& command_json, bool simulated)
{
    const rc::Pose arc_start = extractPoseFromJson(command_json["arc_start"]);
    const rc::Pose arc_through = extractPoseFromJson(command_json["arc_through"]);
    const rc::Pose arc_end = extractPoseFromJson(command_json["arc_end"]);

    const double speed_m_s = command_json["speed_mm_s"].get<double>() / 1000.0;
    const double acceleration_m_s2 = command_json["acceleration_mm_s2"].get<double>() / 1000.0;
    const bool as_circle = command_json["as_circle"].get<bool>();
    const double circle_percentage = command_json["circle_degrees_deg"].get<double>() / 360.0;
    const int32_t orientation_type_index = command_json["orientation_type"].get<int32_t>();
    const rc::OrientationType orientation_type = (orientation_type_index == 0) ? rc::OrientationType::FIXED : rc::OrientationType::TANGENT;

    // Move linearly to arc_start first
    rc::MoveRequestResult res = robot_wrapper_.moveLinear(arc_start, speed_m_s, acceleration_m_s2, false);
    if (!res.success_)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseMoveArc: Failed to send move linear command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)));
    }

    auto async_res = asyncWaitForFinish(res.action_id_);
    if (!async_res) return async_res;

    res = robot_wrapper_.moveArc(arc_through, arc_end, speed_m_s, acceleration_m_s2, orientation_type, as_circle, circle_percentage, false);
    if (!res.success_)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "UsecaseMoveArc: Failed to send move arc command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)));
    }

    async_res = asyncWaitForFinish(res.action_id_);
    if (!async_res) return async_res;

    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveArc::asyncWaitForFinish(uint64_t action_id)
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
            log(logging::LogType::INFO, "UsecaseMoveArc: Stop requested, cancelling robot action " + std::to_string(action_id) + ".");

            rc::MoveRequestResult cancel_res = robot_wrapper_.cancelAction(action_id);
            if (!cancel_res.success_)
            {
                return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecaseMoveArc: Failed to send cancel command to robot for action " + std::to_string(action_id) + ": " + (cancel_res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : cancel_res.err_message_)));
            }
            cancel_requested = true;
        }
    }

    return std::expected<void, uw::helper::ErrorInfo>{};
}