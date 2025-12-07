#include "stereo_camera_module.h"

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
    .module_type_identifier_ = "stereo_camera_module_windows",
    .display_name_ = "Stereo Camera Input Module (Windows)",
    .display_description_ = "Module configures and captures a side-by-side stereo MJPEG stream from a Windows camera. Set your desired stereo resolution (total width and height), target framerate, framerate tolerance and camera index, then activate the module. On activation, the module enumerates MJPEG-capable cameras, filters modes by resolution and framerate (within the given tolerance), and uses the selected index to pick a device from the remaining devices (if 3 cameras remain from original 7, use indexes 0,1 and 2 to select which one). A typical workflow is to start with your target resolution, framerate, reasonably high framerate tolerance and camera index 0 (only change camera index if you have multiple cameras with the same resolution, to select the correct camera), ensure the module activates (if it does not, resolution is likely not supported), then iteratively lower the tolerance or adjust framerate if activation fails (persistent failure usually means the framerate is unsupported). With auto exposure enabled the module should always activate (only resolution, framerate/tolerance and camera index can prevent activation). Once a working mode is found, you can disable auto exposure and experiment with manual exposure values; the supported exposure range and chosen values are reported in logs, not in the GUI. Manual exposure values outside the supported range will prevent activation.",
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
        .param_name_ = "Resolution Width [px]",
        .param_desc_ = "Width of the captured stereo image (side-by-side, so each eye is width/2), for example 1280 for 640x480 per eye",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 640,
        .max_value_long_ = 8192,
        .default_value_ = "2560"
    },
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::LONG,
        .param_name_ = "Resolution Height [px]",
        .param_desc_ = "Height of the captured stereo image, for example 480 for 640x480 per eye",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 240,
        .max_value_long_ = 2160,
        .default_value_ = "800"
    },
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::LONG,
        .param_name_ = "Framerate [FPS]",
        .param_desc_ = "Target framerate for capturing from the stereo camera",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 1,
        .max_value_long_ = 240,
        .default_value_ = "60"
    },
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::LONG,
        .param_name_ = "Framerate Tolerance [FPS]",
        .param_desc_ = "Allowed framerate tolerance for capturing from the stereo camera. For example, with target framerate 60 FPS and tolerance 5FPS, actual framerate between 55 and 65 FPS is acceptable. This is because the camera may not be able to provide exact framerate. This also allows you to specify very high tolerance (e.g. 120FPS) for testing if you don't know the resolution/framerate the camera supports.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 1,
        .max_value_long_ = 120,
        .default_value_ = "3"
    },
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::LONG,
        .param_name_ = "Camera Index",
        .param_desc_ = "Index of the camera to use. The parameters above (resolution, framerate) will filter the available cameras, use this index to select the desired camera from the filtered list. Index 0 corresponds to the first camera in the filtered list. This parameter will always stay 0 if there is only one camera matching the criteria.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 0,
        .max_value_long_ = 10,
        .default_value_ = "0"
    },
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::BOOL,
        .param_name_ = "Use Auto Exposure",
        .param_desc_ = "If true, camera auto exposure is enabled. If false, manual exposure is used. If camera does not support auto exposure, module will still activate successfully. Disabling auto exposure is recommended to reduce motion blur in fast movements.",
        .default_value_ = "1" // start with auto exposure enabled, disabling recommended to lower blur
    },
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::LONG,
        .param_name_ = "Manual Exposure Value",
        .param_desc_ = "Exposure value to use when auto exposure is disabled. Specifies the exposure setting, in log base 2 seconds. In other words, for values less than zero, the exposure time is 1/2^n seconds, and for values zero or above, the exposure time is 2^n seconds. Module will fail to activate if this value is out of camera supported range when auto exposure is disabled. Usual range is small negative values (e.g. -7 to -1) for fast exposure times up to small positive values (e.g. 0 to 3) for longer exposure times (for example -13 to 0 inclusive).",
        .default_value_ = "-4" 
    }
});


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::stereo_camera_module::StereoCameraModule>(data_path, core, channel_map_info, logger, module_id, &module_info);
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