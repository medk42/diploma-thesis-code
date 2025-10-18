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
}