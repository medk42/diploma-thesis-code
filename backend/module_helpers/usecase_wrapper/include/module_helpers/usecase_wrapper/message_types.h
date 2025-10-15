#pragma once

#include "module_common/module_interface_.h"

// READ_COMMAND_PARAMETERS -> SUCCESS (+ blob with parameters), FAIL
// READ_CUSTOM_PARAMETER (+ parameter_id) -> SUCCESS (+ blob with serialized response & blobs), FAIL (failed to allocate memory or invalid parameter id))
// CREATE_COMMAND (+ blob with filled parameters) -> SUCCESS (+ blob representing command data), FAIL (failed to allocate memory or invalid parameters)
// PROGRAM_READ_VISUALIZATION (+ blob with command data) -> SUCCESS (+ blob with visualization data), FAIL (failed to allocate memory or invalid command data)
// PROGRAM_START_REAL (+ blob with command data) -> SUCCESS (+ command_id), FAIL (failed to allocate memory or invalid command data)
// PROGRAM_START_SIMULATED (+ blob with command data) -> SUCCESS (+ command_id), FAIL (failed to allocate memory or invalid command data)
// PROGRAM_PAUSE (+ command_id) -> SUCCESS (paused), FAIL (pausing not supported), IN_PROGRESS (pause request was accepted, check status), ID_INVALID (command_id does not exist)
// PROGRAM_RESUME (+ command_id) -> SUCCESS (resumed), FAIL (resuming not supported), IN_PROGRESS (resume request was accepted, check status), ID_INVALID (command_id does not exist)
// PROGRAM_STATUS (+ command_id) -> SUCCESS (+ status), FAIL (failed to get status), ID_INVALID (command_id does not exist)
// PROGRAM_STOP (+ command_id) -> SUCCESS (stopped), FAIL (stopping not supported), IN_PROGRESS (stop request was accepted, check status), ID_INVALID (command_id does not exist)
// PROGRAM_REMOVE (+ command_id) -> SUCCESS (removed, + status), FAIL (command not in a removable state), ID_INVALID (command_id does not exist)

// Expected usecase module behavior:
// 1) READ_COMMAND_PARAMETERS: read parameters for command from module and fill them out in your module
//    (optionally) READ_CUSTOM_PARAMETER: read custom parameter value from module input if you have CUSTOM parameters
// 2) CREATE_COMMAND: create command with filled parameters, get command data blob
// 3) PROGRAM_START_REAL or PROGRAM_START_SIMULATED: start command execution, get command ID
// 4) PROGRAM_STATUS: check status of command execution (RUNNING, PAUSED, COMPLETED, FAILED, STOPPED) periodically
//    Optionally PROGRAM_PAUSE, PROGRAM_RESUME or PROGRAM_STOP the command execution if supported by the module.
//    Note: PAUSE, RESUME and STOP may return IN_PROGRESS if the request was accepted but not yet completed. Check status periodically again.
// 5) PROGRAM_REMOVE: remove command from history if in a final state (COMPLETED, FAILED, STOPPED)


namespace aergo::module::helpers::usecase_wrapper::message_types
{
    enum class ReqType : uint8_t
    {
        READ_COMMAND_PARAMETERS,      // get parameters required to create a command from the module
        READ_CUSTOM_PARAMETER,        // read a custom parameter value from the modules input (for CUSTOM parameter types)
        CREATE_COMMAND,               // create a command with filled parameters, get command data blob representing the created command
        PROGRAM_READ_VISUALIZATION,   // create a visualization of the command in its current state, get visualization data blob
        PROGRAM_START_REAL,           // start executing the command in real mode, get command ID
        PROGRAM_START_SIMULATED,      // start executing the command in simulated mode, get command ID
        PROGRAM_PAUSE,                // pause executing the command (if supported)
        PROGRAM_STATUS,               // get status of the command execution
        PROGRAM_RESUME,               // resume executing the command (if supported)
        PROGRAM_STOP,                 // stop executing the command (if supported)
        PROGRAM_REMOVE                // remove command from history, only if completed/failed/stopped (has to be called to not leak memory)
    };

    enum class Result : uint8_t
    {
        SUCCESS,
        FAIL,
        IN_PROGRESS,
        ID_INVALID
    };

    enum class ProgramStatus : uint8_t
    {
        RUNNING,
        PAUSED,
        COMPLETED,
        FAILED,
        STOPPED
    };

    struct Request
    {
        ReqType req_type_;
        uint64_t command_id_; // for PROGRAM_STATUS, PROGRAM_PAUSE, PROGRAM_RESUME, PROGRAM_STOP
        uint32_t param_id_;   // for READ_CUSTOM_PARAMETER, id in the CUSTOM parameters list
    };

    struct Response
    {
        Result result_;
        uint64_t command_id_; // for PROGRAM_START_REAL, PROGRAM_START_SIMULATED
        ProgramStatus program_status_; // for PROGRAM_STATUS, PROGRAM_STOP
    };


    constexpr aergo::module::communication_channel::Consumer usecase_request_consumer = {
        .count_ = aergo::module::communication_channel::Consumer::Count::RANGE,
        .min_ = 1,
        .max_ = 1000,
        .channel_type_identifier_ = "helpers__usecase_wrapper__req/v1:struct{enum:uint8_t,uint64_t,uint32_t}[+blob{filled_parameters|command_data}]",
        .display_name_ = "Registered Usecases",
        .display_description_ = "Request channel for usecase wrapper messages to control usecase commands.",
        .prioritized_ = true,
        .message_queue_capacity_ = 16
    };

    constexpr aergo::module::communication_channel::Producer usecase_response_producer = {
        .channel_type_identifier_ = "helpers__usecase_wrapper__resp/v1:struct{enum:uint8_t,uint64_t,enum:uint8_t}[+blob{command_parameters|visualization_data|command_data}]",
        .display_name_ = "Registered Usecase Response",
        .display_description_ = "Response channel for usecase wrapper messages to control usecase commands.",
        .prioritized_ = true,
        .message_queue_capacity_ = 16
    };

}