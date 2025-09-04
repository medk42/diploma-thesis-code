#include "camera_module.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"


#define MODULE_A_API_VERSION 2

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

    
using namespace aergo::module;

static constexpr communication_channel::Producer camera_module_publish_producers[] = {
    { 
        .channel_type_identifier_ = "image_bgr/v1:struct{width:uint16,height:uint16}+blob{width*height*3}",
        .display_name_ = "Camera Frame", 
        .display_description_ = "Frame captured from the camera in 8-bit BGR format with 3 channels"
    }
};

static constexpr ModuleInfo module_info = {
    .display_name_ = "Camera Input Module",
    .display_description_ = "Module captures raw camera data from a connected camera using OpenCV and publishes it as BGR image frames.",
    .publish_producers_ = camera_module_publish_producers,
    .publish_producer_count_ = std::size(camera_module_publish_producers),
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
    auto module = std::make_unique<aergo::default_modules::camera_module::CameraModule>(data_path, core, channel_map_info, logger, module_id);
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