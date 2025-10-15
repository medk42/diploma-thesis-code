#pragma once

#include "module_common/module_interface_.h"

namespace aergo::module::helpers::usecase_wrapper
{
    // Wrapper for usecase modules (modules that implement a specific usecase, e.g. pen tracking, object recognition, etc.)
    // Currently no additional functionality, just a marker interface.
    class IUsecaseModule
    {
    public:
        virtual ~IUsecaseModule() = default;
    };
}