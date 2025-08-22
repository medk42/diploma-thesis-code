#pragma once

#include "module_common/module_interface_.h"
#include "module_common/base_module.h"

#include <tuple>
#include <set>

namespace aergo::module::helpers::helper_base
{
    struct RegisterInfo
    {
        std::set<uint32_t> process_message_ids_;
        std::set<uint32_t> process_request_ids_;
        std::set<uint32_t> process_response_ids_;
    };

    class IHelperBase : virtual public aergo::module::IModuleBase
    {
    public:
        virtual RegisterInfo initializeAndRegister(aergo::module::BaseModule* base_module, aergo::module::ModuleInfo module_info) noexcept = 0;
    };
}