#include "camera_module.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"

#include "module_helpers/activation_wrapper/message_types.h"
#include "module_helpers/activation_wrapper/activation_wrapper.h"
#include "module_helpers/camera_messages/messages.h"


#define MODULE_A_API_VERSION 2

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

    
using namespace aergo::module;

static constexpr communication_channel::Producer camera_module_publish_producers[] = {
    aergo::module::helpers::camera_messages::camera_image_producer
};

static constexpr communication_channel::Producer camera_module_response_producers[] = {
    aergo::module::helpers::activation_wrapper::message_types::activation_response_producer
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "camera_module",
    .display_name_ = "Camera Input Module",
    .display_description_ = "Module captures raw camera data from a connected camera using OpenCV and publishes it as image frames.",
    .publish_producers_ = camera_module_publish_producers,
    .publish_producer_count_ = std::size(camera_module_publish_producers),
    .response_producers_ = camera_module_response_producers,
    .response_producer_count_ = std::size(camera_module_response_producers),
    .subscribe_consumers_ = nullptr,
    .subscribe_consumer_count_ = 0,
    .request_consumers_ = nullptr,
    .request_consumer_count_ = 0,
    .auto_create_ = false
};

static aergo::module::helpers::parameter_description::ParameterList parameters(std::vector<aergo::module::helpers::parameter_description::ParameterDescription>{
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::LONG,
        .param_name_ = "Camera Index",
        .param_desc_ = "Index of the camera to use (for use with OpenCV VideoCapture)",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 0,
        .max_value_long_ = 10,
        .default_value_ = "0"
    }
});


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::camera_module::CameraModule>(data_path, core, channel_map_info, logger, module_id, &module_info);
    if (!module->valid())
        return nullptr;

    auto wrapped_module = std::make_unique<aergo::module::helpers::activation_wrapper::ActivationWrapper>(std::move(module), &parameters);
    if (!wrapped_module->valid())
        return nullptr;

    return new aergo::module::dll::DllModuleWrapper(std::move(wrapped_module), core, module_id, logger);
}

void destroyModule(aergo::module::dll::IDllModule* module)
{
    delete module;
}