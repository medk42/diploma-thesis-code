#pragma once

#include "module_common/module_interface_.h"

namespace aergo::module::helpers::activation_wrapper
{
    class IActivableModule
    {
    public:
        virtual void activate() = 0;
        virtual void deactivate() = 0;
    }
}