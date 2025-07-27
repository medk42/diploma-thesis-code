#include "module_common/module_contract.h"
#include "module_a/module_a.h"


#define MODULE_A_API_VERSION 2

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

    
using namespace aergo::demo_modules_1::module_a;
using namespace aergo::module;

static constexpr communication_channel::Producer module_a_publish_producers[] = {
    { 
        .channel_type_identifier_ = "message_1_small/v1:struct{int32_t counter1;uint8_t counter2}", 
        .display_name_ = "Small message", 
        .display_description_ = "Small message containing just an int32_t and uint8_t." 
    },
    { 
        .channel_type_identifier_ = "message_2_large_fixed/v1:struct{uint8_t counter_start} + blob[1000;counter]", 
        .display_name_ = "Large fixed messages", 
        .display_description_ = "Large message, header contains a random number uint8_t, message is 1000B and counts up from header with 255->0 wrapping." 
    }
};

static constexpr communication_channel::Producer module_a_response_producers[] = {
    {
        // Request: "request_1_large_variable/v1:struct{int32_t requested_size;uint8_t counter_start}"
        .channel_type_identifier_ = "response_1_large_variable/v1:struct{} + blob[dynamic, counter]",
        .display_name_ = "Large variable response",
        .display_description_ = "Request will contain int32_t describing the requested size and uint8_t requesting the starting number. Response will be an empty struct and blob of requested size starting at the requested number."
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

IModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    return new ModuleA(core, channel_map_info, logger, module_id);
}

void destroyModule(IModule* module)
{
    delete module;
}