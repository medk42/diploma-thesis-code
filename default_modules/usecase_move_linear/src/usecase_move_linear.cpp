#include "usecase_move_linear.h"

#include "module_helpers/pen_messages/message_types.h"

using namespace aergo::default_modules::usecase_move_linear;
using namespace aergo::module;
using namespace aergo::module::helpers::usecase_wrapper;
namespace ri = aergo::module::helpers::robot_interface;
namespace pm = aergo::module::helpers::pen_messages;

using json = nlohmann::json;



UsecaseMoveLinear::UsecaseMoveLinear(
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
        log(logging::LogType::ERROR, "UsecaseMoveLinear: Failed to get pen message intent subscribe channel.");
        return;
    }

    valid_ = true;
}


aergo::module::IModule::IngressDecision UsecaseMoveLinear::onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept
{
    if (robot_wrapper_.handlesIngress(kind, local_channel_id, src))
    {
        return robot_wrapper_.onIngress(kind, msg, queue_status);
    }

    // push all other messages to BaseUsecase for decision
    return BaseUsecase::onIngress(kind, local_channel_id, src, msg, queue_status);
}


void UsecaseMoveLinear::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (robot_wrapper_.handlesResponse(request_consumer_id, source_channel))
    {
        robot_wrapper_.processResponse(message);
        return;
    }

    // pass all other responses to BaseUsecase
    BaseUsecase::processResponse(request_consumer_id, source_channel, message);
}


void UsecaseMoveLinear::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    robot_wrapper_.processMessage(subscribe_consumer_id, message);

    // pass all messages also to BaseUsecase
    BaseUsecase::processMessage(subscribe_consumer_id, source_channel, message);
}


UsecaseMoveLinear::ProcessingResult UsecaseMoveLinear::processCustomMessageOrResponse(
    ProcessingChannelType channel_type, 
    uint32_t consumer_id, 
    ChannelIdentifier source_channel, 
    message::MessageHeader message, 
    std::vector<uint8_t>& out_data_replace
)
{
    if (consumer_id != pen_message_intent_subscribe_channel_id_)
    {
        log(logging::LogType::WARNING, "UsecaseMoveLinear: Received message from unexpected channel.");
        return ProcessingResult::DROP;
    }

    pm::PenMessageIntent pen_message_intent;
    if (!message.readAs(pen_message_intent))
    {
        log(logging::LogType::WARNING, "UsecaseMoveLinear: Received message is not a pen message intent.");
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


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveLinear::createCommandFromParameters(
    const uw::p_desc::ParameterList& auto_parameters,
    const uw::p_desc::ParameterList& required_parameters,
    const uw::p_desc::ParameterList& advanced_parameters,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& auto_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& required_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
    nlohmann::json& out_command_json
)
{
    if (auto_parameter_values.size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseMoveLinear: Expected 1 auto parameter value, got " + std::to_string(auto_parameter_values.size())));
    }

    if (auto_parameter_values[0].size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "UsecaseMoveLinear: Expected 1 auto parameter value, got " + std::to_string(auto_parameter_values[0].size())));
    }

    if (!std::holds_alternative<std::vector<uint8_t>>(auto_parameter_values[0][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "UsecaseMoveLinear: Expected vector<uint8_t> auto parameter value, got " + std::to_string(auto_parameter_values[0][0].value_.index())));
    }
    const auto& data = std::get<std::vector<uint8_t>>(auto_parameter_values[0][0].value_);

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
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "UsecaseMoveLinear: Failed to read target pose from auto parameter value."));
    }

    if (advanced_parameter_values.size() != 2 || advanced_parameter_values[0].size() != 1 || advanced_parameter_values[1].size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecaseMoveLinear: Expected 2 advanced parameter values, got " + std::to_string(advanced_parameter_values.size())));
    }
    if (!std::holds_alternative<double>(advanced_parameter_values[0][0].value_) || !std::holds_alternative<double>(advanced_parameter_values[1][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "UsecaseMoveLinear: Expected double advanced parameter values, got " + std::to_string(advanced_parameter_values[0][0].value_.index()) + " and " + std::to_string(advanced_parameter_values[1][0].value_.index())));
    }
    double speed = std::get<double>(advanced_parameter_values[0][0].value_);
    double acceleration = std::get<double>(advanced_parameter_values[1][0].value_);
    if (speed <= 0 || acceleration <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "UsecaseMoveLinear: Speed and acceleration must be non-negative."));
    }

    json command_json;
    command_json["target_pose"] = json::object();
    command_json["target_pose"]["x"] = pose.x;
    command_json["target_pose"]["y"] = pose.y;
    command_json["target_pose"]["z"] = pose.z;
    command_json["target_pose"]["qx"] = pose.qx;
    command_json["target_pose"]["qy"] = pose.qy;
    command_json["target_pose"]["qz"] = pose.qz;
    command_json["target_pose"]["qw"] = pose.qw;
    command_json["speed_mm_s"] = speed;
    command_json["acceleration_mm_s2"] = acceleration;

    out_command_json = command_json;
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveLinear::validateParameters(const nlohmann::json& command_json)
{
    if (!command_json.contains("target_pose") || !command_json["target_pose"].is_object())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseMoveLinear: command JSON missing 'target_pose' object."));
    }
    if (!command_json["target_pose"].contains("x") || !command_json["target_pose"]["x"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "UsecaseMoveLinear: command JSON missing 'target_pose.x' number."));
    }
    if (!command_json["target_pose"].contains("y") || !command_json["target_pose"]["y"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "UsecaseMoveLinear: command JSON missing 'target_pose.y' number."));
    }
    if (!command_json["target_pose"].contains("z") || !command_json["target_pose"]["z"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "UsecaseMoveLinear: command JSON missing 'target_pose.z' number."));
    }
    if (!command_json["target_pose"].contains("qx") || !command_json["target_pose"]["qx"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecaseMoveLinear: command JSON missing 'target_pose.qx' number."));
    }
    if (!command_json["target_pose"].contains("qy") || !command_json["target_pose"]["qy"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "UsecaseMoveLinear: command JSON missing 'target_pose.qy' number."));
    }
    if (!command_json["target_pose"].contains("qz") || !command_json["target_pose"]["qz"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "UsecaseMoveLinear: command JSON missing 'target_pose.qz' number."));
    }
    if (!command_json["target_pose"].contains("qw") || !command_json["target_pose"]["qw"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "UsecaseMoveLinear: command JSON missing 'target_pose.qw' number."));
    }
    if (!command_json.contains("speed_mm_s") || !command_json["speed_mm_s"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(9, "UsecaseMoveLinear: command JSON missing 'speed_mm_s' number."));
    }
    if (!command_json.contains("acceleration_mm_s2") || !command_json["acceleration_mm_s2"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(10, "UsecaseMoveLinear: command JSON missing 'acceleration_mm_s2' number."));
    }
    if (command_json["speed_mm_s"].get<double>() <= 0 || command_json["acceleration_mm_s2"].get<double>() <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(11, "UsecaseMoveLinear: Speed and acceleration must be non-negative."));
    }

    // all checks passed, return success
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveLinear::runProgram(const nlohmann::json& command_json, bool simulated)
{
    const rc::Pose target_pose = {
        .position = {
            .x = command_json["target_pose"]["x"].get<double>(),
            .y = command_json["target_pose"]["y"].get<double>(),
            .z = command_json["target_pose"]["z"].get<double>()
        },
        .orientation = {
            .x = command_json["target_pose"]["qx"].get<double>(),
            .y = command_json["target_pose"]["qy"].get<double>(),
            .z = command_json["target_pose"]["qz"].get<double>(),
            .w = command_json["target_pose"]["qw"].get<double>()
        }
    };
    const double speed_m_s = command_json["speed_mm_s"].get<double>() / 1000.0;
    const double acceleration_m_s2 = command_json["acceleration_mm_s2"].get<double>() / 1000.0;


    rc::MoveRequestResult res = robot_wrapper_.moveLinear(target_pose, speed_m_s, acceleration_m_s2, false);
    if (!res.success_)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseMoveLinear: Failed to send move linear command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)));
    }

    auto async_res = asyncWaitForFinish(res.action_id_);
    if (!async_res) return async_res;

    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveLinear::asyncWaitForFinish(uint64_t action_id)
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
            log(logging::LogType::INFO, "UsecaseMoveLinear: Stop requested, cancelling robot action " + std::to_string(action_id) + ".");

            rc::MoveRequestResult cancel_res = robot_wrapper_.cancelAction(action_id);
            if (!cancel_res.success_)
            {
                return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecaseMoveLinear: Failed to send cancel command to robot for action " + std::to_string(action_id) + ": " + (cancel_res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : cancel_res.err_message_)));
            }
            cancel_requested = true;
        }
    }

    return std::expected<void, uw::helper::ErrorInfo>{};
}