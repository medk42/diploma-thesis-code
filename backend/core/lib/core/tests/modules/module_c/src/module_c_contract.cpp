#include "module_common/module_contract.h"
#include "module_c/module_c.h"


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

static constexpr communication_channel::Consumer module_request_consumers[] = {
    {
        .count_ = communication_channel::Consumer::Count::RANGE,
        .min_ = 0,
        .max_ = 3,
        .channel_type_identifier_ = "message_4/v1:int",
        .display_name_ = "Message 4",
        .display_description_ = ""
    }
};

static constexpr ModuleInfo module_info = {
    .display_name_ = "Module C",
    .display_description_ = "",
    .publish_producers_ = module_publish_producers,
    .publish_producer_count_ = std::size(module_publish_producers),
    .response_producers_ = module_response_producers,
    .response_producer_count_ = std::size(module_response_producers),
    .subscribe_consumers_ = module_subscribe_consumers,
    .subscribe_consumer_count_ = std::size(module_subscribe_consumers),
    .request_consumers_ = module_request_consumers,
    .request_consumer_count_ = std::size(module_request_consumers),
    .auto_create_ = false
};


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

IModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    return new aergo::tests::core_1::ModuleC(core, channel_map_info, logger, module_id);
}

void destroyModule(IModule* module)
{
    delete module;
}