#include "usecase_move_trajectory.h"

#include "module_helpers/pen_messages/message_types.h"

using namespace aergo::default_modules::usecase_move_trajectory;
using namespace aergo::module;
using namespace aergo::module::helpers::usecase_wrapper;
namespace pm = aergo::module::helpers::pen_messages;

using json = nlohmann::json;



UsecaseMoveTrajectory::UsecaseMoveTrajectory(
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
        log(logging::LogType::ERROR, "UsecaseMoveTrajectory: Failed to get pen message intent subscribe channel.");
        return;
    }

    valid_ = true;
}


aergo::module::IModule::IngressDecision UsecaseMoveTrajectory::onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept
{
    if (robot_wrapper_.handlesIngress(kind, local_channel_id, src))
    {
        return robot_wrapper_.onIngress(kind, msg, queue_status);
    }

    // push all other messages to BaseUsecase for decision
    return BaseUsecase::onIngress(kind, local_channel_id, src, msg, queue_status);
}


void UsecaseMoveTrajectory::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (robot_wrapper_.handlesResponse(request_consumer_id, source_channel))
    {
        robot_wrapper_.processResponse(message);
        return;
    }

    // pass all other responses to BaseUsecase
    BaseUsecase::processResponse(request_consumer_id, source_channel, message);
}


void UsecaseMoveTrajectory::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    robot_wrapper_.processMessage(subscribe_consumer_id, message);

    // pass all messages also to BaseUsecase
    BaseUsecase::processMessage(subscribe_consumer_id, source_channel, message);
}


UsecaseMoveTrajectory::ProcessingResult UsecaseMoveTrajectory::processCustomMessageOrResponse(
    ProcessingChannelType channel_type, 
    uint32_t consumer_id, 
    ChannelIdentifier source_channel, 
    message::MessageHeader message, 
    std::vector<uint8_t>& out_data_replace
)
{
    if (consumer_id != pen_message_intent_subscribe_channel_id_)
    {
        log(logging::LogType::WARNING, "UsecaseMoveTrajectory: Received message from unexpected channel.");
        return ProcessingResult::DROP;
    }

    pm::PenMessageIntent pen_message_intent;
    if (!message.readAs(pen_message_intent))
    {
        log(logging::LogType::WARNING, "UsecaseMoveTrajectory: Received message is not a pen message intent.");
        return ProcessingResult::DROP;
    }

    if (pen_message_intent.intent != pm::PenIntent::TRAJECTORY)
    {
        return ProcessingResult::DROP;
    }

    if (message.blob_count_ != 1 || !message.blobs_[0].valid())
    {
        log(logging::LogType::WARNING, "UsecaseMoveTrajectory: Received message with unexpected blob count or invalid blob.");
        return ProcessingResult::DROP;
    }

    std::vector<pm::Pose> poses;
    if (!pm::parseTrajectoryBlob(message.blobs_[0].data(), message.blobs_[0].size(), poses))
    {
        log(logging::LogType::WARNING, "UsecaseMoveTrajectory: Failed to parse trajectory blob.");
        return ProcessingResult::DROP;
    }

    if (poses.size() < 2)
    {
        log(logging::LogType::WARNING, "UsecaseMoveTrajectory: Received trajectory with less than 2 poses.");
        return ProcessingResult::DROP;
    }

    out_data_replace.clear();
    out_data_replace.reserve(sizeof(size_t) + poses.size() * sizeof(double) * 7);
    using aergo::module::helpers::serialization_helper::serialization::push;
    push<size_t>(out_data_replace, static_cast<size_t>(poses.size()));
    for (const auto& pose : poses)
    {
        push<double>(out_data_replace, pose.x);
        push<double>(out_data_replace, pose.y);
        push<double>(out_data_replace, pose.z);
        push<double>(out_data_replace, pose.qx);
        push<double>(out_data_replace, pose.qy);
        push<double>(out_data_replace, pose.qz);
        push<double>(out_data_replace, pose.qw);
    }

    return ProcessingResult::ACCEPT_REPLACE;
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveTrajectory::createCommandFromParameters(
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
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseMoveTrajectory: Expected 1 auto parameter value, got " + std::to_string(auto_parameter_values.size())));
    }

    if (auto_parameter_values[0].size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "UsecaseMoveTrajectory: Expected 1 auto parameter value, got " + std::to_string(auto_parameter_values[0].size())));
    }

    if (!std::holds_alternative<std::vector<uint8_t>>(auto_parameter_values[0][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "UsecaseMoveTrajectory: Expected vector<uint8_t> auto parameter value, got " + std::to_string(auto_parameter_values[0][0].value_.index())));
    }
    const auto& data = std::get<std::vector<uint8_t>>(auto_parameter_values[0][0].value_);

    using aergo::module::helpers::serialization_helper::deserialization::BufferReader;
    BufferReader reader(data.data(), data.size());

    // Read number of poses
    size_t pose_count = 0;
    if (!reader.read<size_t>(pose_count))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "UsecaseMoveTrajectory: Failed to read pose count from auto parameter value."));
    }

    if (pose_count < 2)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecaseMoveTrajectory: Trajectory must contain at least 2 poses, got " + std::to_string(pose_count)));
    }

    // Read all poses into JSON array
    json trajectory_array = json::array();
    for (size_t i = 0; i < pose_count; ++i)
    {
        pm::Pose pose;
        if (!reader.read<double>(pose.x) ||
            !reader.read<double>(pose.y) ||
            !reader.read<double>(pose.z) ||
            !reader.read<double>(pose.qx) ||
            !reader.read<double>(pose.qy) ||
            !reader.read<double>(pose.qz) ||
            !reader.read<double>(pose.qw))
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "UsecaseMoveTrajectory: Failed to read pose " + std::to_string(i) + " from auto parameter value."));
        }

        json pose_json = json::object();
        pose_json["x"] = pose.x;
        pose_json["y"] = pose.y;
        pose_json["z"] = pose.z;
        pose_json["qx"] = pose.qx;
        pose_json["qy"] = pose.qy;
        pose_json["qz"] = pose.qz;
        pose_json["qw"] = pose.qw;
        trajectory_array.push_back(pose_json);
    }

    if (advanced_parameter_values.size() != 3)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "UsecaseMoveTrajectory: Expected 3 advanced parameter value groups, got " + std::to_string(advanced_parameter_values.size())));
    }

    if (advanced_parameter_values[0].size() != 1 || advanced_parameter_values[1].size() != 1 || advanced_parameter_values[2].size() != 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "UsecaseMoveTrajectory: Expected 1 value per advanced parameter group."));
    }

    // Read speed
    if (!std::holds_alternative<double>(advanced_parameter_values[0][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(9, "UsecaseMoveTrajectory: Expected double for speed, got " + std::to_string(advanced_parameter_values[0][0].value_.index())));
    }
    double speed = std::get<double>(advanced_parameter_values[0][0].value_);

    // Read acceleration
    if (!std::holds_alternative<double>(advanced_parameter_values[1][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(10, "UsecaseMoveTrajectory: Expected double for acceleration, got " + std::to_string(advanced_parameter_values[1][0].value_.index())));
    }
    double acceleration = std::get<double>(advanced_parameter_values[1][0].value_);

    // Read orientation_type
    if (!std::holds_alternative<int32_t>(advanced_parameter_values[2][0].value_))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(11, "UsecaseMoveTrajectory: Expected int32_t for orientation_type, got " + std::to_string(advanced_parameter_values[2][0].value_.index())));
    }
    int32_t orientation_type_index = std::get<int32_t>(advanced_parameter_values[2][0].value_);
    if (orientation_type_index < 0 || orientation_type_index > 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(12, "UsecaseMoveTrajectory: Orientation type index must be 0 (Fixed) or 1 (Tangent), got " + std::to_string(orientation_type_index)));
    }

    // Validate speed and acceleration
    if (speed <= 0 || acceleration <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(13, "UsecaseMoveTrajectory: Speed and acceleration must be greater than 0."));
    }

    // Build command JSON
    json command_json;
    command_json["trajectory"] = trajectory_array;
    command_json["speed_mm_s"] = speed;
    command_json["acceleration_mm_s2"] = acceleration;
    command_json["orientation_type"] = orientation_type_index;

    out_command_json = command_json;
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveTrajectory::validatePose(const nlohmann::json& pose_json, int error_code_base)
{
    if (!pose_json.is_object())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base, "UsecaseMoveTrajectory: Pose must be an object."));
    }
    if (!pose_json.contains("x") || !pose_json["x"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 1, "UsecaseMoveTrajectory: Pose missing 'x' number."));
    }
    if (!pose_json.contains("y") || !pose_json["y"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 2, "UsecaseMoveTrajectory: Pose missing 'y' number."));
    }
    if (!pose_json.contains("z") || !pose_json["z"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 3, "UsecaseMoveTrajectory: Pose missing 'z' number."));
    }
    if (!pose_json.contains("qx") || !pose_json["qx"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 4, "UsecaseMoveTrajectory: Pose missing 'qx' number."));
    }
    if (!pose_json.contains("qy") || !pose_json["qy"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 5, "UsecaseMoveTrajectory: Pose missing 'qy' number."));
    }
    if (!pose_json.contains("qz") || !pose_json["qz"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 6, "UsecaseMoveTrajectory: Pose missing 'qz' number."));
    }
    if (!pose_json.contains("qw") || !pose_json["qw"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(error_code_base + 7, "UsecaseMoveTrajectory: Pose missing 'qw' number."));
    }

    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveTrajectory::validateParameters(const nlohmann::json& command_json)
{
    // Validate trajectory array
    if (!command_json.contains("trajectory") || !command_json["trajectory"].is_array())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseMoveTrajectory: command JSON missing 'trajectory' array."));
    }

    const auto& trajectory = command_json["trajectory"];
    if (trajectory.size() < 2)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "UsecaseMoveTrajectory: Trajectory must contain at least 2 poses, got " + std::to_string(trajectory.size())));
    }

    // Validate each pose in the trajectory
    for (size_t i = 0; i < trajectory.size(); ++i)
    {
        auto result = validatePose(trajectory[i], static_cast<int>(3 + i * 8));
        if (!result)
        {
            return result;
        }
    }

    // Validate speed_mm_s
    if (!command_json.contains("speed_mm_s") || !command_json["speed_mm_s"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(100, "UsecaseMoveTrajectory: command JSON missing 'speed_mm_s' number."));
    }
    double speed = command_json["speed_mm_s"].get<double>();
    if (speed <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(101, "UsecaseMoveTrajectory: Speed must be greater than 0."));
    }

    // Validate acceleration_mm_s2
    if (!command_json.contains("acceleration_mm_s2") || !command_json["acceleration_mm_s2"].is_number())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(102, "UsecaseMoveTrajectory: command JSON missing 'acceleration_mm_s2' number."));
    }
    double acceleration = command_json["acceleration_mm_s2"].get<double>();
    if (acceleration <= 0)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(103, "UsecaseMoveTrajectory: Acceleration must be greater than 0."));
    }

    // Validate orientation_type
    if (!command_json.contains("orientation_type") || !command_json["orientation_type"].is_number_integer())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(104, "UsecaseMoveTrajectory: command JSON missing 'orientation_type' integer."));
    }
    int32_t orientation_type = command_json["orientation_type"].get<int32_t>();
    if (orientation_type < 0 || orientation_type > 1)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(105, "UsecaseMoveTrajectory: Orientation type must be 0 (Fixed) or 1 (Tangent), got " + std::to_string(orientation_type)));
    }

    // all checks passed, return success
    return std::expected<void, uw::helper::ErrorInfo>{};
}


rc::Pose UsecaseMoveTrajectory::extractPoseFromJson(const nlohmann::json& pose_json)
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


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveTrajectory::runProgram(const nlohmann::json& command_json, bool simulated)
{
    // Extract trajectory
    const auto& trajectory_json = command_json["trajectory"];
    std::vector<rc::Pose> trajectory;
    trajectory.reserve(trajectory_json.size());
    for (const auto& pose_json : trajectory_json)
    {
        trajectory.push_back(extractPoseFromJson(pose_json));
    }

    const double speed_m_s = command_json["speed_mm_s"].get<double>() / 1000.0;
    const double acceleration_m_s2 = command_json["acceleration_mm_s2"].get<double>() / 1000.0;
    const int32_t orientation_type_index = command_json["orientation_type"].get<int32_t>();
    const rc::OrientationType orientation_type = (orientation_type_index == 0) ? rc::OrientationType::FIXED : rc::OrientationType::TANGENT;

    rc::MoveRequestResult res = robot_wrapper_.moveTrajectory(trajectory, speed_m_s, acceleration_m_s2, orientation_type, false);
    if (!res.success_)
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "UsecaseMoveTrajectory: Failed to send move linear command to robot: " + (res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : res.err_message_)));
    }

    auto async_res = asyncWaitForFinish(res.action_id_);
    if (!async_res) return async_res;

    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> UsecaseMoveTrajectory::asyncWaitForFinish(uint64_t action_id)
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
            log(logging::LogType::INFO, "UsecaseMoveTrajectory: Stop requested, cancelling robot action " + std::to_string(action_id) + ".");

            rc::MoveRequestResult cancel_res = robot_wrapper_.cancelAction(action_id);
            if (!cancel_res.success_)
            {
                return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "UsecaseMoveTrajectory: Failed to send cancel command to robot for action " + std::to_string(action_id) + ": " + (cancel_res.err_message_.empty() ? std::string("UNKNOWN_ERROR") : cancel_res.err_message_)));
            }
            cancel_requested = true;
        }
    }

    return std::expected<void, uw::helper::ErrorInfo>{};
}