#include "module_common/module_contract.h"
#include "module_a/module_a.h"


#define MODULE_A_API_VERSION 1

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

    
using namespace aergo::demo_modules_1::module_a;
using namespace aergo::module;

static constexpr communication_channel::PublishProducer module_a_publish_producers[] = {
    { 
        .channel_type_identifier_ = "message_1_small", 
        .display_name_ = "Small message", 
        .display_description_ = "Small message containing just an int32_t and uint8_t." 
    },
    { 
        .channel_type_identifier_ = "message_2_large_fixed", 
        .display_name_ = "Large fixed messages", 
        .display_description_ = "Large message, header contains a random number uint8_t, message is 1000B and counts up from header with 255->0 wrapping." 
    }
};

static constexpr communication_channel::ResponseProducer module_a_response_producers[] = {
    {
        .channel_type_identifier_ = "response_1_large_variable",
        .display_name_ = "Large variable response",
        .display_description_ = "Request will contain int32_t describing the requested size and uint8_t requesting the starting number"
    }
};

static constexpr ModuleInfo module_a_info = {
    .display_name_ = "Module A",
    .display_description_ = "Example module for publishing small messages and large fixed messages and providing large dynamic messages on request.",
    .publish_producers_ = module_a_publish_producers,
    .publish_producer_count_ = std::size(module_a_publish_producers),
    .response_producers_ = module_a_response_producers,
    .response_producer_count_ = std::size(module_a_response_producers),
    .subscribe_consumers_ = nullptr,
    .subscribe_consumer_count_ = 0,
    .request_consumers_ = nullptr,
    .request_consumer_count_ = 0,
    .auto_create_ = false
};


const ModuleInfo* readModuleInfo()
{
    return &module_a_info;
}

IModule* createModule(ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    return new ModuleA(core, channel_map_info, logger, module_id);
}

void destroyModule(IModule* module)
{
    delete module;
}