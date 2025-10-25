#include "module_helpers/usecase_tree/program_instance.h"

#include "module_helpers/usecase_wrapper/serialization_helper.h"

using namespace aergo::module::helpers::usecase_tree;
using namespace aergo::module;



ProgramInstance::ProgramInstance(bool simulate, std::vector<structs::ExistingCommand> commands, req_func_t send_request_function)
: simulate_run_(simulate), running_program_command_list_(std::move(commands)), send_request_function_(std::move(send_request_function))
{
    for (const auto& command : running_program_command_list_)
    {
        auto usecase_ref = command.getUsecaseReference();
        if (usecase_ref)
        {
            running_program_usecase_channels_.push_back(usecase_ref->getCommunicationChannel());
        }
        else
        {
            return; // invalid usecase reference
        }

        if (!command.hasCommandDataJson() || !command.isCommandDataJsonInSync())
        {
            return; // command data JSON does not exist or is not in sync
        }
    }

    program_state_ = ProgramState::RUNNING;
    program_thread_ = std::thread(&ProgramInstance::programExecutionLoop, this);

    valid_ = true;
}


ProgramInstance::~ProgramInstance() noexcept
{
    stop(); // stop the execution loop

    if (program_thread_.joinable())
    {
        program_thread_.join();
    }
}


void ProgramInstance::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (program_state_ == ProgramState::RUNNING || program_state_ == ProgramState::PAUSED || program_state_ == ProgramState::RESUMING || program_state_ == ProgramState::PAUSING)
    {
        current_request_ = CurrentRequest::STOP;
        program_state_ = ProgramState::STOPPING;
    }
    // ignore if already stopping or stopped
}


void ProgramInstance::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (program_state_ == ProgramState::RUNNING)
    {
        current_request_ = CurrentRequest::PAUSE;
        program_state_ = ProgramState::PAUSING;
    }
    // ignore if not running (this also ignores repeated pause requests or fast pause/resume/pause/resume cycling)
}


void ProgramInstance::resume()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (program_state_ == ProgramState::PAUSED)
    {
        current_request_ = CurrentRequest::RESUME;
        program_state_ = ProgramState::RESUMING;
    }
    // ignore if not paused (this also ignores repeated resume requests or fast pause/resume/pause/resume cycling)
}



ProgramInstance::ProgramState ProgramInstance::state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return program_state_;
}


std::optional<ProgramInstance::ProgramResult> ProgramInstance::getResult() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (program_state_ == ProgramState::STOPPED)
    {
        return result_;
    }
    return std::nullopt;
}



void ProgramInstance::programExecutionLoop()
{
    for (size_t i = 0; i < running_program_command_list_.size(); ++i)
    {
        auto& command = running_program_command_list_[i];
        ChannelIdentifier usecase_channel = running_program_usecase_channels_[i];

        uint64_t task_id;
        if (!startCommand(command.getCommandDataJson(), usecase_channel, task_id))
        {
            return; // startCommand already handled stopping with reason
        }

        while (true)
        {
            if (!handleRequest(task_id, usecase_channel))
            {
                removeCommand(task_id, usecase_channel);
                return; // handleRequest already handled stopping with reason
            }

            uw::message_types::ProgramStatus status;
            uw::helper::ErrorInfo error_info;
            if (!updateStatus(task_id, usecase_channel, status, error_info))
            {
                removeCommand(task_id, usecase_channel);
                return; // updateStatus already handled stopping with reason
            }

            if (status == uw::message_types::ProgramStatus::COMPLETED)
            {
                removeCommand(task_id, usecase_channel);
                break; // command finished successfully, start next command
            }
            else if (status == uw::message_types::ProgramStatus::FAILED)
            {
                removeCommand(task_id, usecase_channel);
                stopWithReason(ProgramStopReason::ERROR, error_info);
                return;
            }
            else if (status == uw::message_types::ProgramStatus::EXCEPTION)
            {
                removeCommand(task_id, usecase_channel);
                stopWithReason(ProgramStopReason::EXCEPTION, error_info);
                return;
            }
            else if (status == uw::message_types::ProgramStatus::STOPPED)
            {
                removeCommand(task_id, usecase_channel);
                stopWithReason(ProgramStopReason::STOP_REQUESTED);
                return;
            }

            updateCurrentState(status);

            // else still RUNNING or PAUSED, continue looping
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        bool stop_requested = false;
        handleStateAfterFinishingCommand(stop_requested);
        if (stop_requested)
        {
            return; 
        }
    }

    stopWithReason(ProgramStopReason::COMPLETED);
}



bool ProgramInstance::startCommand(const std::string& command_data_json, const ChannelIdentifier usecase_channel, uint64_t& out_task_id)
{
    auto command_data_span = std::span{
        reinterpret_cast<const std::byte*>(command_data_json.data()),
        command_data_json.size()
    };

    uw::message_types::Response response;
    aergo::module::message::SharedDataBlob error_blob;
    if (!send_request_function_(
        usecase_channel,
        uw::message_types::Request { .req_type_ = simulate_run_ ? uw::message_types::ReqType::PROGRAM_START_SIMULATED : uw::message_types::ReqType::PROGRAM_START_REAL },
        command_data_span,
        response,
        &error_blob
    ))
    {
        stopWithReason(ProgramStopReason::INTERNAL_ERROR, uw::helper::ErrorInfo::WithDetails(0, "Internal error: Failed to send PROGRAM_START request."));
        return false;
    }

    if (response.result_ != uw::message_types::Result::SUCCESS)
    {
        uw::helper::ErrorInfo error_info;
        uw::deserialize::des::BufferReader reader(error_blob.data(), error_blob.size());
        if (error_blob.valid() && uw::deserialize::readErrorInfo(reader, error_info) && error_info.has_details_)
        {
            stopWithReason(ProgramStopReason::INTERNAL_ERROR, error_info);
        }
        else
        {
            stopWithReason(ProgramStopReason::INTERNAL_ERROR, uw::helper::ErrorInfo::WithDetails(0, "Module reported failure in PROGRAM_START response, no specific error info available."));
        }
        return false;
    }

    out_task_id = response.task_id_;
    return true;
}



bool ProgramInstance::updateStatus(const uint64_t task_id, const ChannelIdentifier usecase_channel, uw::message_types::ProgramStatus& out_status, uw::helper::ErrorInfo& out_error_info)
{
    uw::message_types::Request request { .req_type_ = uw::message_types::ReqType::PROGRAM_STATUS, .task_id_ = task_id };

    uw::message_types::Response response;
    aergo::module::message::SharedDataBlob error_blob;
    if (!send_request_function_(
        usecase_channel,
        request,
        std::nullopt,
        response,
        &error_blob
    ))
    {
        stopWithReason(ProgramStopReason::INTERNAL_ERROR, uw::helper::ErrorInfo::WithDetails(0, "Internal error: Failed to send PROGRAM_STATUS request."));
        return false;
    }

    if (response.result_ == uw::message_types::Result::ID_INVALID)
    {
        stopWithReason(ProgramStopReason::INTERNAL_ERROR, uw::helper::ErrorInfo::WithDetails(0, "Module reported ID_INVALID result in PROGRAM_STATUS response."));
        return false;
    }

    if (response.result_ != uw::message_types::Result::SUCCESS) // checks FAIL or if we ever add more
    {
        stopWithReason(ProgramStopReason::INTERNAL_ERROR, uw::helper::ErrorInfo::WithDetails(0, "Module reported FAILURE result in PROGRAM_STATUS response."));
        return false;
    }
    
    // Now it must be SUCCESS
    
    auto status = response.program_status_;

    uw::helper::ErrorInfo error_info;
    if ((status == uw::message_types::ProgramStatus::FAILED || status == uw::message_types::ProgramStatus::EXCEPTION) && error_blob.valid())
    {
        uw::deserialize::des::BufferReader reader(error_blob.data(), error_blob.size());
        if (!uw::deserialize::readErrorInfo(reader, error_info))
        {
            error_info = uw::helper::ErrorInfo::WithoutDetails();
        }
    }

    out_status = status;
    out_error_info = error_info;

    return true;
}


bool ProgramInstance::removeCommand(const uint64_t task_id, const ChannelIdentifier usecase_channel)
{
    uw::message_types::Request request { .req_type_ = uw::message_types::ReqType::PROGRAM_REMOVE, .task_id_ = task_id };

    uw::message_types::Response response;
    aergo::module::message::SharedDataBlob error_blob;
    if (!send_request_function_(
        usecase_channel,
        request,
        std::nullopt,
        response,
        &error_blob
    ))
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result_.failed_to_remove_programs_ = true;
        return false;
    }

    if (response.result_ == uw::message_types::Result::FAIL) // ID_INVALID is also considered success for removal
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result_.failed_to_remove_programs_ = true;
        return false;
    }

    return true;
}


bool ProgramInstance::handleRequest(const uint64_t task_id, const ChannelIdentifier usecase_channel)
{
    CurrentRequest request_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request_copy = current_request_;
        current_request_ = CurrentRequest::NONE; // reset after copying
    }
    
    if (request_copy == CurrentRequest::NONE)
    {
        return true; // nothing to do
    }

    uw::message_types::Request request;
    if (request_copy == CurrentRequest::STOP)
    {
        request = uw::message_types::Request { .req_type_ = uw::message_types::ReqType::PROGRAM_STOP, .task_id_ = task_id };
    }
    else if (request_copy == CurrentRequest::PAUSE)
    {
        request = uw::message_types::Request { .req_type_ = uw::message_types::ReqType::PROGRAM_PAUSE, .task_id_ = task_id };
    }
    else if (request_copy == CurrentRequest::RESUME)
    {
        request = uw::message_types::Request { .req_type_ = uw::message_types::ReqType::PROGRAM_RESUME, .task_id_ = task_id };
    }
    else
    {
        return true; // unknown request, ignore
    }

    uw::message_types::Response response;
    if (!send_request_function_(
        usecase_channel,
        request,
        std::nullopt,
        response,
        nullptr
    ))
    {
        stopWithReason(ProgramStopReason::INTERNAL_ERROR, uw::helper::ErrorInfo::WithDetails(0, "Internal error: Failed to send PROGRAM_STOP/PAUSE/RESUME request."));
        return false;
    }

    if (response.result_ == uw::message_types::Result::ID_INVALID)
    {
        stopWithReason(ProgramStopReason::INTERNAL_ERROR, uw::helper::ErrorInfo::WithDetails(0, "Module reported ID_INVALID result in PROGRAM_STOP/PAUSE/RESUME response."));
        return false;
    }

    // we sent the request successfully, usecase may not support it but that's not our concern here
    // it will either pause/stop or it won't and we will after it finishes
    return true; 
}


void ProgramInstance::stopWithReason(ProgramStopReason reason, std::optional<uw::helper::ErrorInfo> error_info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    program_state_ = ProgramState::STOPPED;
    result_.program_stop_reason_ = reason;
    result_.program_last_error_info_ = error_info;
}


void ProgramInstance::updateCurrentState(uw::message_types::ProgramStatus status)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (program_state_ == ProgramState::PAUSING && status == uw::message_types::ProgramStatus::PAUSED)
    {
        program_state_ = ProgramState::PAUSED;
        return;
    }
    if (program_state_ == ProgramState::RESUMING && status == uw::message_types::ProgramStatus::RUNNING)
    {
        program_state_ = ProgramState::RUNNING;
        return;
    }
}


void ProgramInstance::handleStateAfterFinishingCommand(bool& out_stop_requested)
{
    out_stop_requested = false;
    auto program_state = state();

    if (program_state == ProgramState::STOPPING)
    {
        stopWithReason(ProgramStopReason::STOP_REQUESTED);
        out_stop_requested = true;
        return;
    }

    if (program_state == ProgramState::RESUMING)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        program_state_ = ProgramState::RUNNING;
        return;
    }

    if (program_state == ProgramState::PAUSING || program_state == ProgramState::PAUSED)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            program_state_ = ProgramState::PAUSED;
        }

        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (current_request_ == CurrentRequest::RESUME)
                {
                    program_state_ = ProgramState::RUNNING;
                    current_request_ = CurrentRequest::NONE;
                    return;
                }
                if (current_request_ == CurrentRequest::STOP)
                {
                    program_state_ = ProgramState::STOPPED;
                    result_.program_stop_reason_ = ProgramStopReason::STOP_REQUESTED;
                    result_.program_last_error_info_ = std::nullopt;
                    current_request_ = CurrentRequest::NONE;
                    out_stop_requested = true;
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        return;
    }

    
}