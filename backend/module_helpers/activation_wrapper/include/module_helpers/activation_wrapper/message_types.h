#pragma once

#include "parameter_description.h"


// READ_ACTIVATION_PARAMETERS -> SUCCESS, FAIL (failed to allocate memory)
// ACTIVATE -> SUCCESS, FAIL, RUNNING
// DEACTIVATE -> SUCCESS, FAIL, RUNNING
// GET_STATUS -> SUCCESS, RUNNING (running if activating/deactivating or waiting for CUSTOM message)
// SET_VALUE -> SUCCESS, FAIL (out of bounds), RUNNING (custom value)
// READ_VALUE -> SUCCESS, FAIL (out of bounds)
// LIST_REMOVE -> SUCCESS, FAIL (out of bounds)
// CANCEL_TASK -> SUCCESS (read cancelled), FAIL (failed to cancel or task finished already; or cancelling activation task, check with GET_STATUS)

// values can not change while activated or activation task is running


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
        
        /// @brief Read values of all parameters. Custom values are not read.
        READ_VALUES,

        /// @brief Remove a specific value from the list.
        LIST_REMOVE,

        /// @brief Cancel a running task (SET_VALUE, ACTIVATE or DEACTIVATE).
        CANCEL_TASK
    };

    enum class Result
    {
        SUCCESS,                        // successfully performed request
        FAIL,                           // failed to perform request
        RUNNING,                        // action was started and is running, in progress (check status / cancel with ID running_action_id_)
    };

    enum class ProgressType
    {
        NONE, INT, DOUBLE
    };

    struct ProgressData
    {
        ProgressType progress_type_;  // specifies if the progress is none, int or double
        uint32_t progress_max_int_;   // maximum value for the progress bar (if progress_type_ is int)
        double progress_current_value_double_;  // current progress value double
        uint32_t progress_current_value_int_;   // current progress value int
    };

    struct Request
    {
        ReqType request_type_;  // type of request

        aergo::module::helpers::activation_wrapper::params::ParameterType parameter_type_; // type of parameter that is being send (to double check)

        size_t param_id_;       // ID of the parameter being changed
        size_t list_id_;        // ID into the list inside the parameter
    };

    struct Response
    {
        ReqType request_type_;  // type of request we are responding to
        Result result_;         // result of the requested action

        ProgressData progress_; // progress data

        bool activated_;              // true if module is currently activated, false otherwise

        size_t list_size_;            // for READ_VALUE, when we are reading a list
    };

    constexpr const char* req_type_identifier = "helpers__activation_wrapper__req/v1:struct{enum,enum,size_t,size_t,uint64_t}";
    constexpr const char* resp_type_identifier = "helpers__activation_wrapper__resp/v1:struct{enum,enum,struct{enum,uint32_t,double,uint32_t},bool,size_t}";
}