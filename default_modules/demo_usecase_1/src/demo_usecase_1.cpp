#include "demo_usecase_1.h"
#include "message_structure.h"
#include "module_helpers/usecase_wrapper/serialization_helper.h"

#include <vector>

using namespace aergo::default_modules::demo_usecase_1;
using namespace aergo::module;
using namespace aergo::module::helpers::usecase_wrapper;

using json = nlohmann::json;


std::expected<void, uw::helper::ErrorInfo> DemoUsecase1::createCommandFromParameters(
    const uw::p_desc::ParameterList& auto_parameters,
    const uw::p_desc::ParameterList& required_parameters,
    const uw::p_desc::ParameterList& advanced_parameters,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& auto_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& required_parameter_values,
    std::vector<std::vector<uw::helper::ParameterTypeValue>>& advanced_parameter_values,
    nlohmann::json& out_command_json
)
{

    std::vector<Pen3DPose> pen_poses_1;
    Pen3DPose pen_pose_2;

    for (const auto& value : auto_parameter_values[0])
    {
        Pen3DPose pose;
        if (!readMessageDataAs<Pen3DPose>(value.value_, pose))
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "DemoUsecase1: Failed to read first pen 3d pose from auto parameters."));
        }        
        pen_poses_1.push_back(pose);
    }

    if (!readMessageDataAs<Pen3DPose>(auto_parameter_values[1][0].value_, pen_pose_2))
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "DemoUsecase1: Failed to read second pen 3d pose from required parameters."));
    }

    auto serialize_pose = [](const Pen3DPose& pose) -> std::string {
        return "{[" + std::to_string(pose.tvec_[0]) + "," + std::to_string(pose.tvec_[1]) + "," + std::to_string(pose.tvec_[2]) + "], [" +
            std::to_string(pose.rvec_[0]) + "," + std::to_string(pose.rvec_[1]) + "," + std::to_string(pose.rvec_[2]) + "]}";
    };

    json command_json;
    command_json["pen_poses_1"] = json::array();
    for (const auto& pose : pen_poses_1)
    {
        command_json["pen_poses_1"].push_back(serialize_pose(pose));
    }
    command_json["pen_pose_2"] = serialize_pose(pen_pose_2);

    command_json["sleep"] = std::get<int64_t>(required_parameter_values[0][0].value_);
    command_json["repetitions"] = std::get<int64_t>(required_parameter_values[1][0].value_);

    command_json["message_prefix"] = std::get<std::string>(advanced_parameter_values[0][0].value_);
    command_json["allow_pause"] = std::get<bool>(advanced_parameter_values[1][0].value_);
    command_json["allow_stop"] = std::get<bool>(advanced_parameter_values[2][0].value_);

    out_command_json = command_json;
    
    return std::expected<void, uw::helper::ErrorInfo>{}; // no parameters to set up in this usecase
}



std::expected<void, uw::helper::ErrorInfo> DemoUsecase1::validateParameters(const nlohmann::json& command_json)
{
    if (!command_json.contains("pen_poses_1") || !command_json["pen_poses_1"].is_array())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(1, "DemoUsecase1: command JSON missing 'pen_poses_1' array."));
    }

    for (const auto& pose_json : command_json["pen_poses_1"])
    {
        if (!pose_json.is_string())
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(2, "DemoUsecase1: each 'pen_poses_1' entry must be a string."));
        }
    }

    if (!command_json.contains("pen_pose_2") || !command_json["pen_pose_2"].is_string())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(3, "DemoUsecase1: command JSON missing 'pen_pose_2' string."));
    }

    if (!command_json.contains("sleep") || !command_json["sleep"].is_number_integer())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "DemoUsecase1: command JSON missing 'sleep' integer."));
    }

    if (!command_json.contains("repetitions") || !command_json["repetitions"].is_number_integer())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "DemoUsecase1: command JSON missing 'repetitions' integer."));
    }

    if (!command_json.contains("message_prefix") || !command_json["message_prefix"].is_string())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(6, "DemoUsecase1: command JSON missing 'message_prefix' string."));
    }

    if (!command_json.contains("allow_pause") || !command_json["allow_pause"].is_boolean())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(7, "DemoUsecase1: command JSON missing 'allow_pause' boolean."));
    }

    if (!command_json.contains("allow_stop") || !command_json["allow_stop"].is_boolean())
    {
        return std::unexpected(uw::helper::ErrorInfo::WithDetails(8, "DemoUsecase1: command JSON missing 'allow_stop' boolean."));
    }

    // all checks passed
    return std::expected<void, uw::helper::ErrorInfo>{};
}


std::expected<void, uw::helper::ErrorInfo> DemoUsecase1::runProgram(
    const nlohmann::json& command_json, 
    bool simulated
)
{
    const std::string message_prefix = command_json["message_prefix"].get<std::string>();
    const bool allow_pause = command_json["allow_pause"].get<bool>();
    const bool allow_stop = command_json["allow_stop"].get<bool>();
    const int64_t sleep_duration = command_json["sleep"].get<int64_t>();
    const int64_t repetitions = command_json["repetitions"].get<int64_t>();

    std::vector<std::string> pen_poses_1;
    for (const auto& pose_json : command_json["pen_poses_1"])
    {
        pen_poses_1.push_back(pose_json.get<std::string>());
    }
    const std::string pen_pose_2 = command_json["pen_pose_2"].get<std::string>();

    for (int64_t i = 0; i < repetitions; ++i)
    {
        log(logging::LogType::INFO, message_prefix + "Running iteration " + std::to_string(i + 1) + "/" + std::to_string(repetitions) + ":");
        for (size_t j = 0; j < pen_poses_1.size(); ++j)
        {
            log(logging::LogType::INFO, message_prefix + "   Pen Pose 1 - " + std::to_string(j + 1) + ": " + pen_poses_1[j]);
        }
        log(logging::LogType::INFO, message_prefix + "   Pen Pose 2: " + pen_pose_2);
        
        handleControlRequests(allow_pause, allow_stop);

        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_duration));
    }

    log(logging::LogType::INFO, message_prefix + "Program completed.");

    return std::expected<void, uw::helper::ErrorInfo>{};
}