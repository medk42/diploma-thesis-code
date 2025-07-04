#include "module_a/module_a.h"

using namespace aergo::demo_modules_1::module_a;
using namespace aergo::module;



ModuleA::ModuleA(ICore* core, InputChannelMapInfo channel_map_info, const logging::ILogger* logger, uint64_t module_id)
: ModuleWrapper(core, channel_map_info, logger, module_id, 10) {}



