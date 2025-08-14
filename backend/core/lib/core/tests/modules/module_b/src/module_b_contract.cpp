#include "module_common/module_contract.h"
#include "module_b/module_b.h"
#include "module_common/dll_module_wrapper.h"


#define MODULE_API_VERSION 2

static_assert(MODULE_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

    
using namespace aergo::module;

static constexpr communication_channel::Producer module_publish_producers[] = {
    { 
        .channel_type_identifier_ = "message_6/v1:int",
        .display_name_ = "Message 6", 
        .display_description_ = "" 
    }
};

static constexpr communication_channel::Producer module_response_producers[] = {
    { 
        .channel_type_identifier_ = "message_2/v1:int",
        .display_name_ = "Message 2", 
        .display_description_ = "" 
    }
};

static constexpr communication_channel::Consumer module_subscribe_consumers[] = {
    {
        .count_ = communication_channel::Consumer::Count::SINGLE,
        .min_ = 0,
        .max_ = 0,
        .channel_type_identifier_ = "message_1/v1:int",
        .display_name_ = "Message 1", 
        .display_description_ = ""
    }
};

static constexpr ModuleInfo module_info = {
    .display_name_ = "Module B",
    .display_description_ = "",
    .publish_producers_ = module_publish_producers,
    .publish_producer_count_ = std::size(module_publish_producers),
    .response_producers_ = module_response_producers,
    .response_producer_count_ = std::size(module_response_producers),
    .subscribe_consumers_ = module_subscribe_consumers,
    .subscribe_consumer_count_ = std::size(module_subscribe_consumers),
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
    return new aergo::module::dll::DllModuleWrapper(std::make_unique<aergo::tests::core_1::ModuleB>(data_path, core, channel_map_info, logger, module_id), 10);
}

void destroyModule(aergo::module::dll::IDllModule* module)
{
    delete module;
}