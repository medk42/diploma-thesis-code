#pragma once

#include "parameter_description.h"


// READ_ACTIVATION_PARAMETERS -> SUCCESS
// ACTIVATE -> SUCCESS, FAIL, RUNNING, RUNNING_WITH_PROGRESS
// DEACTIVATE -> SUCCESS, FAIL, RUNNING, RUNNING_WITH_PROGRESS
// GET_STATUS -> SUCCESS
// SET_VALUE -> SUCCESS, FAIL (out of bounds), RUNNING (custom value)
// READ_VALUE -> SUCCESS, FAIL (out of bounds)
// LIST_REMOVE -> SUCCESS, FAIL (out of bounds)
// CHECK_TASK_STATUS -> TASK_FINISHED (call GET_STATUS or READ_VALUE to see if it succeeded), RUNNING (task is still running), RUNNING_WITH_PROGRESS (same, progress value)
// CANCEL_TASK -> SUCCESS (cancelled), FAIL (failed to cancel), TASK_FINISHED (task finished before being able to cancel it)


namespace aergo::module::helpers::activation_wrapper::message_types
{
    enum class ReqType
    {
        READ_ACTIVATION_PARAMETERS,  // get activation parameters from the module as a string
        ACTIVATE,                    // attempt to activate the module
        DEACTIVATE,                  // attempt to deactivate the module
        GET_STATUS,                  // read activation status of the module

        /// @brief set/change value of parameter, existing list value or add new list value (if list_id_ is larger than list size)
        SET_VALUE,
        
        /// @brief Read value from a parameter. Reads entire list if it's list. Custom values are not read.
        READ_VALUE,

        /// @brief Remove a specific value from the list.
        LIST_REMOVE,

        /// @brief Check status of a running task (identified by running_action_id_).
        CHECK_TASK_STATUS,

        /// @brief Cancel a running task (identified by running_action_id_).
        CANCEL_TASK
    };

    enum class Result
    {
        SUCCESS,                        // successfully performed request
        FAIL,                           // failed to perform request
        RUNNING,                        // action was started and is running, in progress (check status / cancel with ID running_action_id_)
        RUNNING_WITH_PROGRESS,          // same as RUNNING, with progress bar (int 0 to progress_max_int_, inclusive OR double 0 to 1),
        TASK_FINISHED                   // task finished before CANCEL_TASK request was processed
    };

    enum class ProgressType
    {
        INT, DOUBLE
    };

    struct Request
    {
        ReqType request_type_;  // type of request

        aergo::module::helpers::activation_wrapper::params::ParameterType parameter_type_; // type of parameter that is being send (to double check)

        size_t param_id_;       // ID of the parameter being changed
        size_t list_id_;        // ID into the list inside the parameter

        uint64_t running_action_id_;  // ID of the running task to check (for CHECK_TASK_STATUS and CANCEL_TASK)
    };

    struct Resp
    {
        ReqType request_type_;  // type of request we are responding to
        Result result_;         // result of the requested action

        ProgressType progress_type_;  // specifies if the progress is int or double
        uint64_t running_action_id_;  // unique ID referencing a running task if task didn't immediately succeed or fail
        uint32_t progress_max_int_;   // maximum value for the progress bar (if progress_type_ is int)
        double progress_current_value_double_;  // current progress value double
        uint32_t progress_current_value_int_;   // current progress value int

        bool activated_;              // true if module is currently activated, false otherwise

        size_t list_size_;            // for READ_VALUE, when we are reading a list
    };

    constexpr const char* req_type_identifier = "helpers__activation_wrapper__req/v1:struct{enum,enum,size_t,size_t,uint64_t}";
    constexpr const char* resp_type_identifier = "helpers__activation_wrapper__resp/v1:struct{enum,enum,enum,uint64_t,uint32_t,double,uint32_t,bool,size_t}";
}