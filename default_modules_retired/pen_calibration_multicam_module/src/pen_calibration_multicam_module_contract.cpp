#include "pen_calibration_multicam_module.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"

#include "module_helpers/activation_wrapper/message_types.h"
#include "module_helpers/activation_wrapper/activation_wrapper.h"
#include "module_helpers/parameter_description/parameter_description.h"
#include "module_helpers/calibrated_camera_world_messages/message_types.h"

#define MODULE_A_API_VERSION 2

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

using namespace aergo::module;
using namespace aergo::default_modules::pen_calibration_multicam_module;
namespace p_desc = aergo::module::helpers::parameter_description;
namespace ccrm = aergo::module::helpers::calibrated_camera_world_messages;

static constexpr communication_channel::Consumer subscribe_consumers[] = {
    {
        .count_ = communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = ccrm::calibrated_camera_publish_producer.channel_type_identifier_,
        .display_name_ = "Calibrated multi-camera frames of pen",
        .display_description_ = "Input calibrated camera sets (e.g., stereo or multi-cam) that include per-camera poses and intrinsics. Used to ingest pen images for multi-camera pen calibration.",
        .prioritized_ = false,
        .message_queue_capacity_ = 2
    }
};

static constexpr communication_channel::Producer response_producers[] = {
    aergo::module::helpers::activation_wrapper::message_types::activation_response_producer
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "pen_calibration_multicam_module",
    .display_name_ = "Pen Calibration (Multi-Cam)",
    .display_description_ = "Collects calibrated multi-camera captures of the coded 3D pen for later calibration. Feeds on calibrated camera sets and stores activation parameters; publishing is not implemented yet.",
    .publish_producers_ = nullptr,
    .publish_producer_count_ = 0,
    .response_producers_ = response_producers,
    .response_producer_count_ = std::size(response_producers),
    .subscribe_consumers_ = subscribe_consumers,
    .subscribe_consumer_count_ = std::size(subscribe_consumers),
    .request_consumers_ = nullptr,
    .request_consumer_count_ = 0,
    .auto_create_ = false,
    .prioritized_workers_count_ = 1,
    .regular_workers_count_ = 1
};

static p_desc::ParameterList parameters(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Multi-camera pen captures",
        .param_desc_ = "At least 15 calibrated image sets of the 3D pen from multiple angles. Keep codes close to the cameras for resolution, rotate the pen to expose all 8 codes across the set, and ensure 2-3 codes are visible per camera per image. Capture while the pen rests in a fixed jig (tip dimple) and vary rotation plus small side-to-side and vertical shifts.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0,
        .as_list_ = true,
        .list_size_min_ = 15,
        .list_size_max_ = 0
    }
});


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<PenCalibrationMulticamModule>(data_path, core, channel_map_info, logger, module_id, &module_info);
    if (!module->valid())
    {
        return nullptr;
    }

    auto wrapped_module = std::make_unique<aergo::module::helpers::activation_wrapper::ActivationWrapper>(std::move(module), &parameters);
    if (!wrapped_module->valid())
    {
        return nullptr;
    }

    return new aergo::module::dll::DllModuleWrapper(std::move(wrapped_module), core, module_id, logger);
}

void destroyModule(aergo::module::dll::IDllModule* module)
{
    delete module;
}
