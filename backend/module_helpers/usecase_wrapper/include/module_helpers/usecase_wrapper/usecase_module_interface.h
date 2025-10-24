#pragma once

#include "module_helpers/parameter_description/parameter_description.h"
#include "helper_types.h"
#include "message_types.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <expected>

namespace aergo::module::helpers::usecase_wrapper
{
    namespace p_desc = aergo::module::helpers::parameter_description;

    class IUsecaseModule
    {
    public:
        virtual ~IUsecaseModule() noexcept = default;

        /// @brief Send request from usecase wrapper to the module. Used to request CUSTOM parameter values.
        /// @param request_consumer_id ID of the request consumer channel to send the request to
        /// @return ID of the request (to match with response ID)
        virtual uint64_t sendRequestFromUsecase(uint32_t request_consumer_id) = 0;

        /// @brief Create a command from provided parameter values. Command is represented as a string in JSON format.
        /// @param auto_parameters parameters that are set from the input Consumer channels (subscribe/request) - only CUSTOM type allowed (value or list of values)
        /// @param required_parameters parameters that are required to be set by the user before activation - any non-CUSTOM type allowed (value or list of values)
        /// @param advanced_parameters parameters that are optional to set by the user before activation - any non-CUSTOM type allowed, need to have default value (value or list of values)
        /// @param auto_parameter_values parameter values for auto_parameters
        /// @param required_parameter_values parameter values for required_parameters
        /// @param advanced_parameter_values parameter values for advanced_parameters
        /// @param out_command_json output parameter to store created command in JSON format
        /// @return {} if command creation was successful, std::unexpected(ErrorInfo) otherwise
        virtual std::expected<void, helper::ErrorInfo> createCommandFromParameters(
            const p_desc::ParameterList& auto_parameters,
            const p_desc::ParameterList& required_parameters,
            const p_desc::ParameterList& advanced_parameters,
            std::vector<std::vector<helper::ParameterTypeValue>>& auto_parameter_values,
            std::vector<std::vector<helper::ParameterTypeValue>>& required_parameter_values,
            std::vector<std::vector<helper::ParameterTypeValue>>& advanced_parameter_values,
            nlohmann::json& out_command_json
        ) = 0;

        /// @brief Start execution of a command. Command is represented as a string in JSON format.
        /// @param command_json command in JSON format
        /// @param simulated true to start command in simulated mode, false to start in real mode
        /// @param out_task_id output parameter to store started task ID to allow control / reading status later, only on SUCCESS
        /// @param out_error_info output parameter to optionally store error details on FAIL
        /// @return SUCCESS + filled out out_task_id (command started successfully), FAIL (failed to allocate memory, invalid command data or unable to start another command, can add error detail to error_info)
        virtual message_types::Result programStart(const nlohmann::json& command_json, bool simulated, uint64_t& out_task_id, helper::ErrorInfo& out_error_info) = 0;

        /// @brief Pause execution of a command. Only if supported by the command - if supported, RESUME must also be supported.
        /// @param task_id ID of the task to pause
        /// @return SUCCESS (paused with this call or already in pause state), FAIL (pausing not supported), IN_PROGRESS (pause request was accepted, check status), ID_INVALID (task_id does not exist)
        virtual message_types::Result programPause(uint64_t task_id) = 0;

        /// @brief Resume execution of a command. Only if supported by the command - if supported, PAUSE must also be supported.
        /// @param task_id ID of the task to resume
        /// @return SUCCESS (resumed with this call or already in running state, or command already finished), FAIL (resuming not supported), IN_PROGRESS (resume request was accepted, check status), ID_INVALID (task_id does not exist)
        virtual message_types::Result programResume(uint64_t task_id) = 0;

        /// @brief Get current status of a command execution.
        /// @param task_id ID of the task to get status for
        /// @param out_status output parameter to store current status of the command execution
        /// @param out_error_info output parameter to store error details if response is SUCCESS (for final statuses - COMPLETED, FAILED, EXCEPTION, STOPPED)
        /// @return SUCCESS + filled out out_status (status retrieved successfully), FAIL (failed to get status), ID_INVALID (task_id does not exist)
        virtual message_types::Result programStatus(uint64_t task_id, message_types::ProgramStatus& out_status, helper::ErrorInfo& out_error_info) = 0;

        /// @brief Stop execution of a command. Only if supported by the command.
        /// @param task_id ID of the task to stop
        /// @return SUCCESS (stopped with this call or already in a final state), FAIL (stopping not supported), IN_PROGRESS (stop request was accepted, check status), ID_INVALID (task_id does not exist)
        virtual message_types::Result programStop(uint64_t task_id) = 0;

        /// @brief Remove a command from history. Only allowed if command is in a final state (COMPLETED, FAILED, STOPPED).
        /// @param task_id ID of the task to remove
        /// @return SUCCESS (removed), FAIL (command not in a removable state), ID_INVALID (task_id does not exist)
        virtual message_types::Result programRemove(uint64_t task_id) = 0;
    };
}