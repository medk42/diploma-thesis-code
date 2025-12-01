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


DemoRobotControl::DemoRobotControl(
    const char* data_path, 
    aergo::module::ICore* core, 
    aergo::module::InputChannelMapInfo channel_map_info, 
    const aergo::module::logging::ILogger* logger, 
    uint64_t module_id, 
    const aergo::module::ModuleInfo* module_info,
    bool supports_multi_program,
    bool supports_pause,
    bool supports_stop
)  : BaseUsecase(
        data_path, core, channel_map_info, 
        logger, module_id, module_info,
        supports_multi_program, supports_pause, supports_stop
    )
{

    if (!getRequestChannelByName(ri::robot_interface_request_consumer.channel_type_identifier_, robot_request_channel_))
    {
        log(logging::LogType::ERROR, "DemoRobotControl: Failed to get robot request channel.");
        return;
    }

    if (!getSubscribeChannelByName(ri::robot_interface_status_consumer.channel_type_identifier_, robot_status_channel_))
    {
        log(logging::LogType::ERROR, "DemoRobotControl: Failed to get robot status subscribe channel.");
        return;
    }

    if (!getSubscribeChannelByName(ri::robot_interface_finished_consumer.channel_type_identifier_, robot_finished_channel_))
    {
        log(logging::LogType::ERROR, "DemoRobotControl: Failed to get robot finished subscribe channel.");
        return;
    }

    mixed_allocator_ = std::move(
        aergo::module::helpers::mixed_buffer_allocator::MixedBufferedAllocator::create(
            this,
            128,  // slot size bytes
            16    // number of slots
        )
    );

    if (!mixed_allocator_)
    {
        log(logging::LogType::ERROR, "DemoRobotControl: Failed to create mixed buffered allocator.");
        return;
    }

    auto channel_info = getRequestChannelInfo(robot_request_channel_);
    if (channel_info.channel_identifier_count_ != 1)
    {
        log(logging::LogType::ERROR, "DemoRobotControl: Robot request channel must be mapped to exactly one module.");
        return;
    }

    std::map<RequestType, sync_req::RequestChannelInfo> request_type_to_channel = {
        { 
            RequestType::ROBOT_REQUEST, 
            {
                .local_channel_id_ = robot_request_channel_,
                .target_channel_ = channel_info.channel_identifier_[0]
            }
        }
    };

    sync_request_helper_ = std::make_unique<sync_req::SynchronousRequestHelper<RequestType>>(
        request_type_to_channel, *(static_cast<BaseModule*>(this))
    );


    valid_ = true;
}


aergo::module::IModule::IngressDecision DemoRobotControl::onIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept
{
    if (kind == IModule::ProcessingType::RESPONSE)
    {
        log(logging::LogType::INFO, "DemoRobotControl: onIngress, response received on channel " + std::to_string(local_channel_id) + " from module " + std::to_string(src.producer_module_id_) + ", channel " + std::to_string(src.producer_channel_id_) + ".");
    }

    if (sync_request_helper_->handlesIngress(kind, local_channel_id, src))
    {
        return sync_request_helper_->onIngress(kind, msg, queue_status);
    }
    if (kind == IModule::ProcessingType::MESSAGE && local_channel_id == robot_finished_channel_)
    {
        // always accept robot finished messages
        return IModule::IngressDecision::ACCEPT;
    }

    // push all other messages to BaseUsecase for decision
    return BaseUsecase::onIngress(kind, local_channel_id, src, msg, queue_status);
}


void DemoRobotControl::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    log(logging::LogType::INFO, "DemoRobotControl: processResponse called on channel " + std::to_string(request_consumer_id) + " from module " + std::to_string(source_channel.producer_module_id_) + ", channel " + std::to_string(source_channel.producer_channel_id_) + ".");

    if (sync_request_helper_->handlesResponse(request_consumer_id, source_channel))
    {
        sync_request_helper_->processResponse(message);
        return;
    }

    // pass all other responses to BaseUsecase
    BaseUsecase::processResponse(request_consumer_id, source_channel, message);
}

void DemoRobotControl::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (subscribe_consumer_id == robot_finished_channel_)
    {
        // process robot finished message
        ri::FinishedMessage finished_msg;
        if (!message.readAs(finished_msg))
        {
            log(logging::LogType::ERROR, "DemoRobotControl: Failed to read robot finished message.");
            return;
        }
        log(logging::LogType::INFO, "DemoRobotControl: Received robot finished message for action ID " + std::to_string(finished_msg.action_id) + ", success: " + std::to_string(finished_msg.success) + ", blobs: " + std::to_string(message.blob_count_) + ".");
        std::string err_msg;
        if (message.blob_count_ != 0)
        {
            rc::BufferReader reader(message.blobs_[0].data(), message.blobs_[0].size());
            rc::common::deserialization::deserializeErrorMessage(reader, err_msg);
        }

        log(
            finished_msg.success ? logging::LogType::INFO : logging::LogType::ERROR, 
            finished_msg.success ? "DemoRobotControl: Robot action " + std::to_string(finished_msg.action_id) + " finished successfully." : ("DemoRobotControl: Robot action " + std::to_string(finished_msg.action_id) + " finished with error: " + (err_msg.empty() ? std::string("UNKNOWN_ERROR") : err_msg))
        );
        
        if (running_action_id_.has_value() && *running_action_id_ == finished_msg.action_id)
        {
            // clear running action ID
            running_action_id_.reset();
        }
        else
        {
            log(logging::LogType::WARNING, "DemoRobotControl: Received finished message for unknown action ID, received: " + std::to_string(finished_msg.action_id) + " expected: " + (running_action_id_.has_value() ? std::to_string(*running_action_id_) : "NONE"));
        }

        return;
    }

    // pass all other messages to BaseUsecase
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
        
        ri::Request joint_move_request
        {
            .req_type = ri::ReqType::START_ACTION,
            .feature = ri::RobotFeature::ROBOT_CONTROL,
            .action_id = 0 // not used for START_ACTION
        };

        std::vector<std::byte> request_data;
        rc::start::requests::serialization::moveJoint(
            request_data,
            Span<const double>(joint_positions.data(), joint_positions.size()),
            joint_speed_rad,
            joint_acceleration_rad
        );

        ri::Response response;
        std::vector<std::vector<std::byte>> response_blobs;
        sync_req::RequestResult sync_result = sync_request_helper_->sendSynchronousRequest(
            RequestType::ROBOT_REQUEST,
            joint_move_request,
            std::span<std::byte>(request_data),
            response,
            &response_blobs,
            mixed_allocator_.get(),
            100 // timeout ms
        );

        if (sync_result != sync_req::RequestResult::SUCCESS)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(4, "DemoRobotControl: Synchronous request to move joints failed, error code: " + std::to_string(static_cast<uint8_t>(sync_result))));
        }

        if (response.resp_type != ri::RespType::SUCCESS_IN_PROGRESS)
        {
            return std::unexpected(uw::helper::ErrorInfo::WithDetails(5, "DemoRobotControl: Robot responded with failure to move joints, response type: " + std::to_string(static_cast<uint8_t>(response.resp_type))));
        }

        log(logging::LogType::INFO, "DemoRobotControl: Robot action " + std::to_string(response.action_id) + " finished successfully.");

        running_action_id_ = response.action_id;
        while (running_action_id_.has_value())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    

    return std::expected<void, uw::helper::ErrorInfo>{};
}