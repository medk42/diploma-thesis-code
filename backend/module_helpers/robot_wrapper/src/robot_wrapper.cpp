#include "module_helpers/robot_wrapper/robot_wrapper.h"

#include "module_helpers/robot_interface/message_types.h"
#include "module_helpers/robot_interface/features/robot_control/messages.h"
#include "module_helpers/robot_interface/message_types_definitions.h"

using namespace aergo::module::helpers::robot_interface::robot_control;
using namespace aergo::module;


RobotWrapper::RobotWrapper(BaseModule &base_module, uint32_t sync_request_timeout_ms)
    : base_module_(base_module), sync_request_timeout_ms_(sync_request_timeout_ms)
{
    if (!base_module_.getRequestChannelByName(ri::robot_interface_request_consumer.channel_type_identifier_, robot_request_channel_))
    {
        base_module_.log(logging::LogType::ERROR, "RobotWrapper: Failed to get robot request channel.");
        return;
    }

    if (!base_module_.getSubscribeChannelByName(ri::robot_interface_status_consumer.channel_type_identifier_, robot_status_channel_))
    {
        base_module_.log(logging::LogType::ERROR, "RobotWrapper: Failed to get robot status subscribe channel.");
        return;
    }

    if (!base_module_.getSubscribeChannelByName(ri::robot_interface_finished_consumer.channel_type_identifier_, robot_finished_channel_))
    {
        base_module_.log(logging::LogType::ERROR, "RobotWrapper: Failed to get robot finished subscribe channel.");
        return;
    }

    mixed_allocator_ = std::move(
        aergo::module::helpers::mixed_buffer_allocator::MixedBufferedAllocator::create(
            &base_module_,
            128,  // slot size bytes, should be enough for most control messages
            16    // number of slots
        )
    );

    if (!mixed_allocator_)
    {
        base_module_.log(logging::LogType::ERROR, "RobotWrapper: Failed to create mixed buffered allocator.");
        return;
    }

    auto channel_info = base_module_.getRequestChannelInfo(robot_request_channel_);
    if (channel_info.channel_identifier_count_ != 1)
    {
        base_module_.log(logging::LogType::ERROR, "RobotWrapper: Robot request channel must be mapped to exactly one module.");
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
        request_type_to_channel, base_module_
    );

    valid_ = true;
}



bool RobotWrapper::handlesIngress(IModule::ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src) const noexcept
{
    if (kind == IModule::ProcessingType::MESSAGE && 
        (local_channel_id == robot_status_channel_ || local_channel_id == robot_finished_channel_))
    {
        return true;
    }
    return sync_request_helper_->handlesIngress(kind, local_channel_id, src);
}


IModule::IngressDecision RobotWrapper::onIngress(IModule::ProcessingType kind, const message::MessageHeader& msg, IModule::QueueStatus queue_status) noexcept
{
    return sync_request_helper_->onIngress(kind, msg, queue_status);
}


bool RobotWrapper::handlesResponse(uint32_t request_consumer_id, ChannelIdentifier src) const noexcept
{
    return sync_request_helper_->handlesResponse(request_consumer_id, src);
}


void RobotWrapper::processResponse(message::MessageHeader message) noexcept
{
    sync_request_helper_->processResponse(message);
}


void RobotWrapper::processMessage(uint32_t subscribe_consumer_id, message::MessageHeader message) noexcept
{
    if (subscribe_consumer_id == robot_status_channel_ && rc_status_callback_)
    {
        ri::StatusMessage status_msg;
        if (!message.readAs(status_msg))
        {
            base_module_.log(logging::LogType::WARNING, "RobotWrapper: Failed to deserialize robot status message header.");
        }

        if (status_msg.feature != ri::RobotFeature::ROBOT_CONTROL)
        {
            base_module_.log(logging::LogType::WARNING, "RobotWrapper: Received robot status message for unsupported feature.");
            return;
        }

        if (message.blob_count_ != 1 || message.blobs_ == nullptr || !message.blobs_[0].valid())
        {
            base_module_.log(logging::LogType::WARNING, "RobotWrapper: Robot status message missing data blob.");
            return;
        }

        message::SharedDataBlob blob = message.blobs_[0];
        rc::BufferReader reader(blob.data(), blob.size());

        StatusMessage status;
        if (!rc::status_messages::deserialization::deserializeStatusMessage(reader, status))
        {
            base_module_.log(logging::LogType::WARNING, "RobotWrapper: Failed to deserialize robot status message.");
            return;
        }

        rc_status_callback_(status);
    }
    else if (subscribe_consumer_id == robot_finished_channel_)
    {
        ri::FinishedMessage finished_msg;
        if (!message.readAs(finished_msg))
        {
            base_module_.log(logging::LogType::WARNING, "RobotWrapper: Failed to deserialize robot finished message header.");
            return;
        }

        std::string err_msg;
        if (message.blob_count_ != 0 && message.blobs_ != nullptr && message.blobs_[0].valid())
        {
            message::SharedDataBlob blob = message.blobs_[0];
            rc::BufferReader reader(blob.data(), blob.size());
            if (!rc::common::deserialization::deserializeErrorMessage(reader, err_msg))
            {
                base_module_.log(logging::LogType::WARNING, "RobotWrapper: Failed to deserialize robot finished message error blob.");
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it_sync = sync_action_ids_.find(finished_msg.action_id);
            if (it_sync != sync_action_ids_.end())
            {
                it_sync->second = MoveRequestResult{ finished_msg.success, err_msg, 0 }; // copy result to map, blocking moveCommon() will pick it up and handle erase
                cv_.notify_all();
            }
            else
            {
                auto it_async = async_action_ids_.find(finished_msg.action_id);
                if (it_async != async_action_ids_.end())
                {
                    async_action_ids_.erase(it_async); // remove from active async actions
                }
                else
                {
                    base_module_.log(logging::LogType::WARNING, "RobotWrapper: Received finished message for unknown action ID: " + std::to_string(finished_msg.action_id));
                }
            }
        }

        if (rc_finished_callback_)
        {
            rc_finished_callback_(finished_msg.action_id, finished_msg.success, err_msg);
        }
    }
}


const char* parseRequestResult(sync_req::RequestResult result)
{
    switch (result)
    {
        case sync_req::RequestResult::SUCCESS:
            return "RobotWrapper: Request sent and response received successfully.";
        case sync_req::RequestResult::FAILURE:
            return "RobotWrapper: Request sent, but response indicates transport layer failure.";
        case sync_req::RequestResult::ALLOC_FAILED:
            return "RobotWrapper: Failed to allocate memory for request blobs or request blob is provided, but allocator is nullptr.";
        case sync_req::RequestResult::UNKNOWN_REQ_ENUM:
            return "RobotWrapper: Request type does not match any configured request channel.";
        case sync_req::RequestResult::MISMATCHED_RESPONSE_TYPE:
            return "RobotWrapper: Response received, but data is either nullptr or of incorrect size.";
        case sync_req::RequestResult::TIMEOUT:
            return "RobotWrapper: Request sent, no response received within timeout.";
        case sync_req::RequestResult::CANCELLED:
            return "RobotWrapper: Request sent, but waiting for response was cancelled before it was received.";
        case sync_req::RequestResult::QUEUE_FULL:
            return "RobotWrapper: Request sent, but failed to receive response because local queue was full.";
        case sync_req::RequestResult::ALREADY_PENDING:
            return "RobotWrapper: Request is already pending, cannot send another.";
        default:
            return "RobotWrapper: Unknown request result.";
    }
}


const char* parseRespType(ri::RespType resp_type)
{
    switch (resp_type)
    {
        case ri::RespType::FEATURE_NOT_SUPPORTED:
            return "RobotWrapper: Requested feature is not supported by the robot interface.";
        case ri::RespType::NOT_IN_PROGRESS:
            return "RobotWrapper: No such action in progress (unknown action_id).";
        case ri::RespType::DATA_INVALID:
            return "RobotWrapper: Data blob is invalid or malformed for the requested feature/action.";
        case ri::RespType::SUCCESS_IN_PROGRESS:
            return "RobotWrapper: Action has been started successfully and is ongoing.";
        case ri::RespType::SUCCESS:
            return "RobotWrapper: Action request was successful (finished or updated successfully).";
        case ri::RespType::FAILURE:
            return "RobotWrapper: Action request failed (could not be started or updated or failed).";
        default:
            return "RobotWrapper: Unknown response type.";
    }
}


MoveRequestResult RobotWrapper::moveCommon(std::span<const std::byte> request_data, bool blocking, std::unique_lock<std::mutex>& lock)
{
    ri::Request robot_control_request =
    {
        .req_type = ri::ReqType::START_ACTION,
        .feature = ri::RobotFeature::ROBOT_CONTROL,
        .action_id = 0 // not used for START_ACTION
    };

    ri::Response response;
    std::vector<std::vector<std::byte>> response_blobs;

    sync_req::RequestResult sync_result = sync_request_helper_->sendSynchronousRequest(
        RequestType::ROBOT_REQUEST,
        robot_control_request,
        request_data,
        response,
        &response_blobs,
        mixed_allocator_.get(),
        sync_request_timeout_ms_
    );

    if (sync_result != sync_req::RequestResult::SUCCESS)
    {
        auto err_msg = parseRequestResult(sync_result);
        base_module_.log(logging::LogType::ERROR, err_msg);
        return MoveRequestResult{ false, err_msg, 0 };
    }

    std::string err_msg;
    if (response_blobs.size() > 0 && !response_blobs[0].empty())
    {
        rc::BufferReader reader(response_blobs[0].data(), response_blobs[0].size());
        if (!rc::common::deserialization::deserializeErrorMessage(reader, err_msg))
        {
            base_module_.log(logging::LogType::WARNING, "RobotWrapper: Failed to deserialize robot move response error blob.");
        }
    }

    if (response.resp_type == ri::RespType::FAILURE)
    {
        err_msg = err_msg.empty() ? "RobotWrapper: Robot responded with failure." : ("RobotWrapper: Robot responded with failure: " + err_msg);
        base_module_.log(logging::LogType::ERROR, err_msg);
        return MoveRequestResult{ false, err_msg, 0 };
    }

    if (response.resp_type == ri::RespType::SUCCESS)
    {
        base_module_.log(logging::LogType::ERROR, "RobotWrapper: Move action finished immediately, not supported.");
        return MoveRequestResult{ false, "RobotWrapper: Move action finished immediately, not supported.", 0 };
    }

    if (response.resp_type != ri::RespType::SUCCESS_IN_PROGRESS)
    {
        auto resp_msg = parseRespType(response.resp_type);
        base_module_.log(logging::LogType::ERROR, resp_msg);
        return MoveRequestResult{ false, resp_msg, 0 };
    }

    uint64_t action_id = response.action_id;
    if (blocking)
    {
        if (sync_action_ids_.find(action_id) != sync_action_ids_.end())
        {
            base_module_.log(logging::LogType::WARNING, "RobotWrapper: Duplicate action ID received for blocking move: " +  std::to_string(action_id));
        }

        sync_action_ids_[action_id] = std::nullopt; // track as blocking action
        cv_.wait(lock, [this, action_id]() {
            auto it = sync_action_ids_.find(action_id);
            return it == sync_action_ids_.end() || it->second.has_value(); // movement considered finished if action ID is removed or has a result
        });

        auto it = sync_action_ids_.find(action_id);
        if (it != sync_action_ids_.end())
        {
            auto res_opt = it->second;
            sync_action_ids_.erase(it);
            return res_opt.value_or(MoveRequestResult{ false, "RobotWrapper: Action ID found but no result available after blocking wait.", 0 });
        }
        else
        {
            return MoveRequestResult{ false, "RobotWrapper: Action ID not found after blocking wait.", 0 };
        }
    }
    else // non-blocking, return immediately with action ID
    {
        if (async_action_ids_.find(action_id) != async_action_ids_.end())
        {
            base_module_.log(logging::LogType::WARNING, "RobotWrapper: Duplicate action ID received for async move: " +  std::to_string(action_id));
        }

        async_action_ids_.insert(action_id); // track as async action
        return MoveRequestResult{ true, {}, action_id };
    }
}


MoveRequestResult RobotWrapper::moveJoint(Span<const double> joint_targets, double speed, double acceleration, bool blocking)
{
    std::unique_lock<std::mutex> lock(mutex_);

    return moveCommon(
        std::span<std::byte>(
            rc::start::requests::serialization::moveJoint(request_data_buffer_, joint_targets, speed, acceleration)
        ), 
        blocking,
        lock
    );
}


MoveRequestResult RobotWrapper::moveLinear(rc::Pose pose_target, double speed, double acceleration, bool blocking)
{
    std::unique_lock<std::mutex> lock(mutex_);

    return moveCommon(
        std::span<std::byte>(
            rc::start::requests::serialization::moveLinear(request_data_buffer_, pose_target, speed, acceleration)
        ), 
        blocking,
        lock
    );
}


MoveRequestResult RobotWrapper::moveArc(rc::Pose pose_through,  rc::Pose pose_target, double speed, double acceleration, rc::OrientationType orientation_type, bool as_circle, double circle_percentage, bool blocking)
{
    std::unique_lock<std::mutex> lock(mutex_);

    return moveCommon(
        std::span<std::byte>(
            rc::start::requests::serialization::moveArc(request_data_buffer_, pose_through, pose_target, speed, acceleration, orientation_type, as_circle, circle_percentage)
        ), 
        blocking,
        lock
    );
}


MoveRequestResult RobotWrapper::moveTrajectory(Span<const rc::Pose> pose_targets, double speed, double acceleration, OrientationType orientation_type, bool blocking)
{
    std::unique_lock<std::mutex> lock(mutex_);
    
    return moveCommon(
        std::span<std::byte>(
            rc::start::requests::serialization::moveTrajectory(request_data_buffer_, pose_targets, speed, acceleration, orientation_type)
        ), 
        blocking,
        lock
    );
}


MoveRequestResult RobotWrapper::cancelAction(uint64_t action_id)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (async_action_ids_.find(action_id) == async_action_ids_.end())
    {
        // not an active async action, nothing to cancel
        return MoveRequestResult{ true, {}, 0 };
    }

    ri::Request robot_control_request =
    {
        .req_type = ri::ReqType::UPDATE_ACTION,
        .feature = ri::RobotFeature::ROBOT_CONTROL,
        .action_id = action_id
    };

    ri::Response response;
    std::vector<std::vector<std::byte>> response_blobs;

    sync_req::RequestResult sync_result = sync_request_helper_->sendSynchronousRequest(
        RequestType::ROBOT_REQUEST,
        robot_control_request,
        std::span<const std::byte> {
            rc::update::requests::serialization::moveRequest(
                request_data_buffer_, 
                rc::update::requests::MoveRequest::CancelMovement
            )
        },
        response,
        &response_blobs,
        mixed_allocator_.get(),
        sync_request_timeout_ms_
    );

    if (sync_result != sync_req::RequestResult::SUCCESS)
    {
        auto err_msg = parseRequestResult(sync_result);
        base_module_.log(logging::LogType::ERROR, err_msg);
        return MoveRequestResult{ false, err_msg, 0 };
    }

    std::string err_msg;
    if (response_blobs.size() > 0 && !response_blobs[0].empty())
    {
        rc::BufferReader reader(response_blobs[0].data(), response_blobs[0].size());
        if (!rc::common::deserialization::deserializeErrorMessage(reader, err_msg))
        {
            base_module_.log(logging::LogType::WARNING, "RobotWrapper: Failed to deserialize robot cancel action response error blob.");
        }
    }

    if (response.resp_type == ri::RespType::FAILURE)
    {
        err_msg = err_msg.empty() ? "RobotWrapper: Robot responded with failure." : ("RobotWrapper: Robot responded with failure: " + err_msg);
        base_module_.log(logging::LogType::ERROR, err_msg);
        return MoveRequestResult{ false, err_msg, 0 };
    }

    if (response.resp_type == ri::RespType::SUCCESS_IN_PROGRESS)
    {
        base_module_.log(logging::LogType::ERROR, "RobotWrapper: Cancel action reports in progress, not supported.");
        return MoveRequestResult{ false, "RobotWrapper: Cancel action reports in progress, not supported.", 0 };
    }

    if (response.resp_type != ri::RespType::SUCCESS)
    {
        auto resp_msg = parseRespType(response.resp_type);
        base_module_.log(logging::LogType::ERROR, resp_msg);
        return MoveRequestResult{ false, resp_msg, 0 };
    }

    return MoveRequestResult{ true, {}, 0 };
}


std::expected<rc::RobotSpecs, std::string> RobotWrapper::getRobotSpecs()
{
    std::unique_lock<std::mutex> lock(mutex_);

    ri::Request robot_control_request =
    {
        .req_type = ri::ReqType::START_ACTION,
        .feature = ri::RobotFeature::ROBOT_CONTROL,
        .action_id = 0 // not used for GET_SPECS
    };

    ri::Response response;
    std::vector<std::vector<std::byte>> response_blobs;

    sync_req::RequestResult sync_result = sync_request_helper_->sendSynchronousRequest(
        RequestType::ROBOT_REQUEST,
        robot_control_request,
        std::span<const std::byte>{rc::start::requests::serialization::getRobotSpecs(request_data_buffer_)},
        response,
        &response_blobs,
        mixed_allocator_.get(),
        sync_request_timeout_ms_
    );

    if (sync_result != sync_req::RequestResult::SUCCESS)
    {
        auto err_msg = parseRequestResult(sync_result);
        base_module_.log(logging::LogType::ERROR, err_msg);
        return std::unexpected(err_msg);
    }

    if (response.resp_type == ri::RespType::FAILURE)
    {
        std::string err_msg;
        if (response_blobs.size() > 0 && !response_blobs[0].empty())
        {
            rc::BufferReader reader(response_blobs[0].data(), response_blobs[0].size());
            if (!rc::common::deserialization::deserializeErrorMessage(reader, err_msg))
            {
                base_module_.log(logging::LogType::WARNING, "RobotWrapper: Failed to deserialize robot specs response error blob.");
            }
        }

        err_msg = err_msg.empty() ? "RobotWrapper: Robot responded with failure." : ("RobotWrapper: Robot responded with failure: " + err_msg);
        base_module_.log(logging::LogType::ERROR, err_msg);
        return std::unexpected(err_msg);
    }

    if (response.resp_type == ri::RespType::SUCCESS_IN_PROGRESS)
    {
        base_module_.log(logging::LogType::ERROR, "RobotWrapper: Get robot specs action reports in progress, not supported.");
        return std::unexpected("RobotWrapper: Get robot specs action reports in progress, not supported.");
    }

    if (response.resp_type != ri::RespType::SUCCESS)
    {
        auto resp_msg = parseRespType(response.resp_type);
        base_module_.log(logging::LogType::ERROR, resp_msg);
        return std::unexpected(resp_msg);
    }

    if (response_blobs.size() == 0 || response_blobs[0].empty())
    {
        base_module_.log(logging::LogType::ERROR, "RobotWrapper: Robot specs response missing data blob.");
        return std::unexpected("RobotWrapper: Robot specs response missing data blob.");
    }

    rc::BufferReader reader(response_blobs[0].data(), response_blobs[0].size());
    rc::RobotSpecs specs;
    if (!rc::start::responses::deserialization::deserializeRobotSpecs(reader, specs))
    {
        base_module_.log(logging::LogType::ERROR, "RobotWrapper: Failed to deserialize robot specs.");
        return std::unexpected("RobotWrapper: Failed to deserialize robot specs.");
    }

    return specs;
}


bool RobotWrapper::isActionActive(uint64_t action_id) const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return async_action_ids_.find(action_id) != async_action_ids_.end();
}