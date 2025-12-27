#include "pen_tracking_multicam_module.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"

#include "module_helpers/calibrated_camera_world_messages/message_types.h"
#include "module_helpers/visualization_3d_interface/message_types.h"
#include "module_helpers/pen_messages/message_types.h"

#define MODULE_A_API_VERSION 2

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

using namespace aergo::module;
using namespace aergo::default_modules::pen_tracking_multicam_module;

static constexpr communication_channel::Consumer subscribe_consumers[] = {
    aergo::module::helpers::calibrated_camera_world_messages::calibrated_camera_subscribe_consumer
};

static constexpr communication_channel::Producer response_producers[] = {
    aergo::module::helpers::visualization_3d_interface::visualization_3d_interface_response_producer
};

static constexpr communication_channel::Producer publish_producers[] = {
    aergo::module::helpers::visualization_3d_interface::visualization_3d_interface_publish_producer,
    aergo::module::helpers::pen_messages::pen_message_raw_publish_producer,
    aergo::module::helpers::pen_messages::pen_message_intent_publish_producer  
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "pen_tracking_multicam_module",
    .display_name_ = "Pen Tracking (Multi-Cam)",
    .display_description_ = "Module that tracks a pen in 3D space using multiple calibrated cameras. Module uses set of calibrated cameras to track pen pose and publishes raw pen messages and high-level pen intent messages. It also provides 3D visualization interface for visualizing the tracked pen in a 3D scene.",
    .publish_producers_ = publish_producers,
    .publish_producer_count_ = std::size(publish_producers),
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


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<PenTrackingMulticamModule>(data_path, core, channel_map_info, logger, module_id, &module_info);
    if (!module->valid())
    {
        return nullptr;
    }

    return new aergo::module::dll::DllModuleWrapper(std::move(module), core, module_id, logger);
}

void destroyModule(aergo::module::dll::IDllModule* module)
{
    delete module;
}
