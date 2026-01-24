#pragma once

#include "module_helpers/parameter_description/parameter_description.h"
#include "helper_types.h"
#include "message_types.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <expected>
#include <vector>

namespace aergo::module::helpers::usecase_wrapper
{
    namespace p_desc = aergo::module::helpers::parameter_description;

    class IUsecaseModule
    {
    public:
        /// @brief Processing result for custom parameter loading. Together with the processCustomMessageOrResponse function,
        /// allows the usecase module to control how incoming messages / responses are handled for CUSTOM parameter loading.
        /// ACCEPT: accept the incoming message / response as is, use the standard serialize::pushMessage serialization.
        /// ACCEPT_REPLACE: accept the incoming message / response, but replace the data with custom data provided by the module.
        /// DROP: drop the incoming message / response, it does not contain the data needed for CUSTOM parameter loading.
        /// For subscribe, wrapper will wait for next message. For request, wrapper fail reading. 
        enum class ProcessingResult : uint8_t
        {
            ACCEPT         = 0,  // accept message as is
            ACCEPT_REPLACE = 1,  // accept message, but replace data with custom data
            DROP           = 2,  // drop message, wait for next
        };

        enum class ProcessingChannelType : uint8_t
        {
            MESSAGE = 0,
            RESPONSE = 1,
        };

        struct Vector3
        {
            double x, y, z;
        };

        struct Quaternion
        {
            double qw, qx, qy, qz;
        };

        /// @brief Pose with position and orientation.
        /// Represents the transformation world <- object (points in object coordinates transformed to world coordinates).
        struct Pose
        {
            Vector3 position;
            Quaternion orientation;
        };

        /// @brief Visualization data produced by the usecase.
        /// This data can be used by the frontend to visualize the results of a confirmed usecase.
        /// You can visualize poses (dot in 3d space with axes), points (dots in 3d space) and trajectories (lines in 3d space).
        /// Set supports_visualization_ to true if any data is present or false if your usecase does not support visualization.
        struct UsecaseVisualization
        {
            bool supports_visualization = false; // whether visualization data is supported
            std::vector<Pose> poses;
            std::vector<Vector3> points;
            std::vector<std::vector<Vector3>> trajectories;
        };

        virtual ~IUsecaseModule() noexcept = default;

        /// @brief Send request from usecase wrapper to the module. Used to request CUSTOM parameter values.
        /// @param auto_parameters parameters that are set from the input Consumer channels (subscribe/request) - only CUSTOM type allowed (value or list of values)
        /// @param param_id ID of the parameter to request (index in auto_parameters)
        /// @param out_request_id output parameter to store generated request ID 
        /// @return true if request was sent successfully, false otherwise
        virtual bool sendRequestFromUsecase(const std::vector<p_desc::ParameterDescription>& auto_parameters, const uint32_t param_id, uint64_t& out_request_id) = 0;

        /// @brief Process incoming message or response for CUSTOM parameter loading.
        /// This function is called when a message or response is received that may contain data for a CUSTOM parameter.
        /// The usecase module can decide whether to accept the message/response as is, accept it but replace the data with custom data,
        /// or drop it and wait for the next message/response.
        /// @param channel_type type of the channel (MESSAGE or RESPONSE)
        /// @param consumer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source request channel (module and channel ID)
        /// @param message the incoming message or response
        /// @param out_data_replace output parameter to store custom data to replace the message data with, if ACCEPT_REPLACE is returned
        /// @return ProcessingResult indicating how to handle the incoming message/response
        virtual ProcessingResult processCustomMessageOrResponse(ProcessingChannelType channel_type, uint32_t consumer_id, ChannelIdentifier source_channel, message::MessageHeader message, std::vector<uint8_t>& out_data_replace) = 0;

        /// @brief Create a command from provided parameter values. Command is represented as a string in JSON format.
        /// This method's caller (UsecaseWrapper) is responsible for validating that parameter values match the parameter descriptions (no need to validate again here).
        /// Every parameter value contains the expected type, value and list sized limits and enum index are also validated.
        /// @param auto_parameters parameters that are set from the input Consumer channels (subscribe/request) - only CUSTOM type allowed (value or list of values)
        /// @param required_parameters parameters that are required to be set by the user before activation - any non-CUSTOM type allowed (value or list of values)
        /// @param advanced_parameters parameters that are optional to set by the user before activation - any non-CUSTOM type allowed, need to have default value (value or list of values)
        /// @param auto_parameter_values parameter values for auto_parameters
        /// @param required_parameter_values parameter values for required_parameters
        /// @param advanced_parameter_values parameter values for advanced_parameters
        /// @param out_command_json output parameter to store created command in JSON format
        /// @param out_visualization output parameter to store visualization data produced by the usecase (if no visualization is supported, you do not need to use this parameter)
        /// @return {} if command creation was successful, std::unexpected(ErrorInfo) otherwise
        virtual std::expected<void, helper::ErrorInfo> createCommandFromParameters(
            const p_desc::ParameterList& auto_parameters,
            const p_desc::ParameterList& required_parameters,
            const p_desc::ParameterList& advanced_parameters,
            std::vector<std::vector<helper::ParameterTypeValue>>& auto_parameter_values,
            std::vector<std::vector<helper::ParameterTypeValue>>& required_parameter_values,
            std::vector<std::vector<helper::ParameterTypeValue>>& advanced_parameter_values,
            nlohmann::json& out_command_json,
            UsecaseVisualization& out_visualization
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