#pragma once

#include <variant>
#include <string>
#include <vector>

#include "module_helpers/parameter_description/parameter_description.h"

namespace aergo::module::helpers::usecase_wrapper::helper
{
    struct ParameterTypeValue
    {
        aergo::module::helpers::parameter_description::ParameterType type_;
        aergo::module::helpers::parameter_description::ParameterValue value_;
    };

    struct ErrorInfo
    {
        static ErrorInfo WithoutDetails()
        {
            return ErrorInfo{ false, false, 0, "" };
        }

        static ErrorInfo WithDetails(uint32_t error_code, const std::string& error_message)
        {
            return ErrorInfo{ true, false, error_code, error_message };
        }

        static ErrorInfo AsException(uint32_t error_code, const std::string& error_message)
        {
            return ErrorInfo{ true, true, error_code, error_message };
        }
        

        bool has_details_{false};
        bool is_exception_{false};
        uint32_t error_code_{0};
        std::string error_message_;
    };
}