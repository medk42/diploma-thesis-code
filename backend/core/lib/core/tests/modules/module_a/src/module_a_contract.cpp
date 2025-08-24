#include "module_common/module_contract.h"
#include "module_a/module_a.h"
#include "module_common/dll_module_wrapper.h"


#define MODULE_A_API_VERSION 2

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

    
using namespace aergo::module;

static constexpr communication_channel::Producer module_a_publish_producers[] = {
    { 
        .channel_type_identifier_ = "message_6/v1:int",
        .display_name_ = "Message 6", 
        .display_description_ = "" 
    },
    { 
        .channel_type_identifier_ = "message_1/v1:int",
        .display_name_ = "Message 1", 
        .display_description_ = "" 
    }
};

static constexpr ModuleInfo module_info = {
    .display_name_ = "Module A",
    .display_description_ = "",
    .publish_producers_ = module_a_publish_producers,
    .publish_producer_count_ = std::size(module_a_publish_producers),
    .response_producers_ = nullptr,
    .response_producer_count_ = 0,
    .subscribe_consumers_ = nullptr,
    .subscribe_consumer_count_ = 0,
    .request_consumers_ = nullptr,
    .request_consumer_count_ = 0,
    .auto_create_ = false
};


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::tests::core_1::ModuleA>(data_path, core, channel_map_info, logger, module_id);
    if (module->valid())
    {
        return new aergo::module::dll::DllModuleWrapper(std::move(module), &module_info, logger);
    }
    else
    {
        return nullptr;
    }
}

void destroyModule(aergo::module::dll::IDllModule* module)
{
    delete module;
}