#include "frontend_module.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"

#include "module_helpers/activation_wrapper/message_types.h"


#define MODULE_API_VERSION 2

static_assert(MODULE_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

    
using namespace aergo::module;


static constexpr communication_channel::Consumer web_visualization_module_request_consumers[] = {
    aergo::module::helpers::activation_wrapper::message_types::activation_request_consumer
};

static constexpr ModuleInfo module_info = {
    .display_name_ = "Web Visualization Module",
    .display_description_ = "Provides web-based module creation, activation and visualization interface. Contains 3D visualization of modules and their data and program tree for creation of robot programs.",
    .publish_producers_ = nullptr,
    .publish_producer_count_ = 0,
    .response_producers_ = nullptr,
    .response_producer_count_ = 0,
    .subscribe_consumers_ = nullptr,
    .subscribe_consumer_count_ = 0,
    .request_consumers_ = web_visualization_module_request_consumers,
    .request_consumer_count_ = std::size(web_visualization_module_request_consumers),
    .auto_create_ = true,
    .prioritized_workers_count_ = 1,
    .regular_workers_count_ = 1
};


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::frontend_module::FrontendModule>(data_path, core, channel_map_info, logger, module_id);
    if (module->valid())
    {
        return new aergo::module::dll::DllModuleWrapper(std::move(module), &module_info, core, module_id, logger);
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