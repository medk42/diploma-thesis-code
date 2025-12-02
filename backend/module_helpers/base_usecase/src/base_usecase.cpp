#include "module_helpers/base_usecase/base_usecase.h"

#include <algorithm>

using namespace aergo::module::helpers::base_usecase;
using namespace aergo::module;
using namespace aergo::module::helpers;


BaseUsecase::BaseUsecase(
    const char* data_path, 
    ICore* core, 
    InputChannelMapInfo channel_map_info, 
    const logging::ILogger* logger, 
    uint64_t module_id, 
    const ModuleInfo* module_info,
    bool supports_multi_program,
    bool supports_pause,
    bool supports_stop
) 
: aergo::module::BaseModule(data_path, core, channel_map_info, logger, module_id, module_info),
  supports_multi_program_(supports_multi_program),
  supports_pause_(supports_pause),
  supports_stop_(supports_stop) {}



BaseUsecase::~BaseUsecase() noexcept
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::for_each(program_statuses_.begin(), program_statuses_.end(), [](auto& pair) { 
            pair.second.stop_requested_ = true; 
        });

        control_cv_.notify_all();
    }

    std::for_each(program_statuses_.begin(), program_statuses_.end(), [](auto& pair){
        if (pair.second.thread_.joinable())
        {
            pair.second.thread_.join();
        }
    });
}



void* BaseUsecase::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(aergo::module::BaseModule)) return static_cast<aergo::module::BaseModule*>(this);
    if (id == typeid(usecase_wrapper::IUsecaseModule)) return static_cast<usecase_wrapper::IUsecaseModule*>(this);
    return nullptr;
}



ISerializableModule::SaveData BaseUsecase::save() noexcept
{
    // usecases are stateless, no need to save anything
    ISerializableModule::SaveData data;
    data.success_ = true;
    data.supports_saving_ = false;
    
    return data;
}



bool BaseUsecase::threadStop(uint32_t timeout_ms) noexcept
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::for_each(program_statuses_.begin(), program_statuses_.end(), [](auto& pair) { 
            pair.second.stop_requested_ = true; 
        });
        control_cv_.notify_all();
    }

    while (true)
    {
        std::thread stolen_thread;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = program_statuses_.begin();
            if (it == program_statuses_.end())
            {
                return true; // no running programs
            }
            stolen_thread = std::move(it->second.thread_);
            program_statuses_.erase(it);
        }
        if (stolen_thread.joinable())
        {
            stolen_thread.join();
        }
    }
}



message_types::Result BaseUsecase::programStart(const nlohmann::json& command_json, bool simulated, uint64_t& out_task_id, usecase_wrapper_helper::ErrorInfo& out_error_info)
{
    // validate command
    auto validate_result = validateParameters(command_json);
    if (!validate_result)
    {
        out_error_info = validate_result.error();
        return message_types::Result::FAIL;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    // check if multiple programs are supported
    if (!supports_multi_program_ && !program_statuses_.empty())
    {
        out_error_info = usecase_wrapper_helper::ErrorInfo::WithDetails(0, "Multiple simultaneous programs are not supported by this usecase.");
        return message_types::Result::FAIL;
    }

    UsecaseStatus program_status;
    program_status.status_ = message_types::ProgramStatus::RUNNING;

    // start program in a separate thread
    program_status.thread_ = std::thread([this, command_json, simulated]()
    {
        std::expected<void, usecase_wrapper_helper::ErrorInfo> run_result;
        try
        {
            run_result = runProgram(command_json, simulated);
        }
        catch (const StopException& e)
        {
            setStatus(message_types::ProgramStatus::STOPPED);
            return;
        }
        catch (const std::exception& e)
        {
            setStatus(message_types::ProgramStatus::EXCEPTION, usecase_wrapper_helper::ErrorInfo::AsException(0, e.what()));
            return;
        }
        if (!run_result)
        {
            setStatus(message_types::ProgramStatus::FAILED, run_result.error());
            return;
        }
        setStatus(message_types::ProgramStatus::COMPLETED);
    });

    uint64_t task_id = next_task_id_++;
    std::thread::id thread_id = program_status.thread_.get_id();
    program_statuses_[thread_id] = std::move(program_status);
    task_id_to_thread_id_[task_id] = thread_id;
    out_task_id = task_id;

    return message_types::Result::SUCCESS;
}



message_types::Result BaseUsecase::programPause(uint64_t task_id)
{
    if (!supports_pause_)
    {
        return message_types::Result::FAIL;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto program_status = getStatusForIdUnsafe(task_id);
    if (!program_status)
    {
        return message_types::Result::ID_INVALID;
    }

    program_status->pause_requested_ = true;

    if (program_status->status_ == message_types::ProgramStatus::RUNNING)
    {
        return message_types::Result::IN_PROGRESS;
    }
    else
    {
        return message_types::Result::SUCCESS;
    }
}



message_types::Result BaseUsecase::programResume(uint64_t task_id)
{
    if (!supports_pause_)
    {
        return message_types::Result::FAIL;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto program_status = getStatusForIdUnsafe(task_id);
    if (!program_status)
    {
        return message_types::Result::ID_INVALID;
    }

    program_status->pause_requested_ = false;
    control_cv_.notify_all();

    if (program_status->status_ == message_types::ProgramStatus::PAUSED)
    {
        return message_types::Result::IN_PROGRESS;
    }
    else
    {
        return message_types::Result::SUCCESS;
    }
}



message_types::Result BaseUsecase::programStatus(uint64_t task_id, message_types::ProgramStatus& out_status, usecase_wrapper_helper::ErrorInfo& out_error_info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto program_status = getStatusForIdUnsafe(task_id);
    if (!program_status)
    {
        return message_types::Result::ID_INVALID;
    }

    out_status = program_status->status_;
    out_error_info = program_status->error_info_;

    return message_types::Result::SUCCESS;
}



message_types::Result BaseUsecase::programStop(uint64_t task_id)
{
    if (!supports_stop_)
    {
        return message_types::Result::FAIL;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto program_status = getStatusForIdUnsafe(task_id);
    if (!program_status)
    {
        return message_types::Result::ID_INVALID;
    }

    program_status->stop_requested_ = true;
    control_cv_.notify_all();

    if (program_status->status_ == message_types::ProgramStatus::RUNNING || program_status->status_ == message_types::ProgramStatus::PAUSED)
    {
        return message_types::Result::IN_PROGRESS;
    }
    else
    {
        return message_types::Result::SUCCESS;
    }
}



message_types::Result BaseUsecase::programRemove(uint64_t task_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto program_status = getStatusForIdUnsafe(task_id);
    if (!program_status)
    {
        return message_types::Result::ID_INVALID;
    }

    if (program_status->status_ == message_types::ProgramStatus::RUNNING || program_status->status_ == message_types::ProgramStatus::PAUSED)
    {
        return message_types::Result::FAIL; // cannot remove running program
    }

    if (program_status->thread_.joinable())
    {
        program_status->thread_.join();
    }

    auto thread_id = task_id_to_thread_id_[task_id];
    task_id_to_thread_id_.erase(task_id);
    program_statuses_.erase(thread_id);

    return message_types::Result::SUCCESS;
}



void BaseUsecase::handleControlRequests(bool allow_pause, bool allow_stop)
{
    std::unique_lock<std::mutex> lock(mutex_);
    
    auto thread_id = std::this_thread::get_id();
    auto it = program_statuses_.find(thread_id);
    if (it == program_statuses_.end())
    {
        log(logging::LogType::ERROR, "handleControlRequests called from unknown thread: " + std::to_string(std::hash<std::thread::id>{}(thread_id)));
        return; // no status found for this thread
    }
    auto& program_status = it->second;

    // handle stop request
    if (allow_stop && program_status.stop_requested_)
    {
        throw StopException();
    }

    // handle pause request
    if (allow_pause && program_status.pause_requested_)
    {
        program_status.status_ = message_types::ProgramStatus::PAUSED;
        control_cv_.wait(lock, [this, thread_id]()
        {
            auto it = program_statuses_.find(thread_id);
            if (it == program_statuses_.end())
            {
                log(logging::LogType::ERROR, "handleControlRequests wait woke up for unknown thread: " + std::to_string(std::hash<std::thread::id>{}(thread_id)));
                return true; // no status found for this thread, continue
            }
            return !it->second.pause_requested_ || it->second.stop_requested_;
        });

        // after waking up, check if stop was requested
        auto it_after_wait = program_statuses_.find(thread_id);
        if (it_after_wait == program_statuses_.end())
        {
            log(logging::LogType::ERROR, "handleControlRequests wait woke up for unknown thread: " + std::to_string(std::hash<std::thread::id>{}(thread_id)));
            return; // no status found for this thread
        }

        auto& program_status_after_wait = it_after_wait->second;
        program_status_after_wait.status_ = message_types::ProgramStatus::RUNNING;
        if (allow_stop && program_status_after_wait.stop_requested_)
        {
            throw StopException();
        }
    }
}



std::tuple<bool, bool> BaseUsecase::checkControlRequests()
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto thread_id = std::this_thread::get_id();
    auto it = program_statuses_.find(thread_id);
    if (it == program_statuses_.end())
    {
        log(logging::LogType::ERROR, "checkControlRequests called from unknown thread: " + std::to_string(std::hash<std::thread::id>{}(thread_id)));
        return {false, false}; // no status found for this thread
    }
    auto& program_status = it->second;

    return {program_status.pause_requested_, program_status.stop_requested_};
}



BaseUsecase::UsecaseStatus* BaseUsecase::getStatusForIdUnsafe(uint64_t task_id)
{
    auto it = task_id_to_thread_id_.find(task_id);
    if (it == task_id_to_thread_id_.end())
    {
        return nullptr;
    }

    auto thread_id = it->second;
    auto status_it = program_statuses_.find(thread_id);
    if (status_it == program_statuses_.end())
    {
        return nullptr;
    }

    return &status_it->second;
}



void BaseUsecase::setStatus(message_types::ProgramStatus status, std::optional<usecase_wrapper_helper::ErrorInfo> error_info)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto& usecase_status = program_statuses_[std::this_thread::get_id()];
    usecase_status.status_ = status;
    if (error_info.has_value())
    {
        usecase_status.error_info_ = error_info.value();
    }
}