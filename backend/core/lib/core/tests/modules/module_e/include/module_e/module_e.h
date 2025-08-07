#pragma once

#include "module_common/module_wrapper.h"
#include "module_common/module_common.h"

namespace aergo::tests::core_1
{
    class ModuleE : public ModuleCommon
    {
    public:
        ModuleE(aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::logging::ILogger* logger, uint64_t module_id) : ModuleCommon(core, channel_map_info, logger, module_id) {}
    };
}