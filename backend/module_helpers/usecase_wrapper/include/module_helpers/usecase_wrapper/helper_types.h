#pragma once

#include <variant>
#include <string>
#include <vector>

#include "module_helpers/parameter_description/parameter_description.h"

namespace aergo::module::helpers::usecase_wrapper::helper
{
    using value_t = std::variant<
        bool,                // boolean
        int64_t,             // long
        double,              // double
        std::string,         // string
        int32_t,             // enum index (or -1 for no selection)
        std::vector<uint8_t> // custom channel data
    >;

    struct ParameterValue
    {
        aergo::module::helpers::parameter_description::ParameterType type_;
        value_t value_;
    };
}