#include "camera_pose_fusion_module.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"

#include "module_helpers/camera_messages/messages.h"
#include "module_helpers/robot_interface/message_types_definitions.h"
#include "message_structure.h"

#define MODULE_API_VERSION 2

static_assert(MODULE_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

using namespace aergo::module;
namespace cm = aergo::module::helpers::camera_messages;
namespace ri = aergo::module::helpers::robot_interface;

static constexpr communication_channel::Consumer camera_pose_fusion_subscribe_consumers[] = {
    cm::camera_image_consumer,
    ri::robot_interface_status_consumer
};

static constexpr communication_channel::Producer camera_pose_fusion_publish_producers[] = {
    aergo::default_modules::camera_pose_fusion_module::camera_image_with_pose_producer
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "camera_pose_fusion_module",
    .display_name_ = "Camera Pose Fusion Module",
    .display_description_ = "Fuses camera image data with robot flange pose. Subscribes to camera images and robot status messages, adds the current robot flange pose to each camera frame for world-coordinate processing downstream.",
    .publish_producers_ = camera_pose_fusion_publish_producers,
    .publish_producer_count_ = std::size(camera_pose_fusion_publish_producers),
    .response_producers_ = nullptr,
    .response_producer_count_ = 0,
    .subscribe_consumers_ = camera_pose_fusion_subscribe_consumers,
    .subscribe_consumer_count_ = std::size(camera_pose_fusion_subscribe_consumers),
    .request_consumers_ = nullptr,
    .request_consumer_count_ = 0,
    .auto_create_ = false,
    .prioritized_workers_count_ = 1,
    .regular_workers_count_ = 2  // 2 threads for message processing
};

const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::camera_pose_fusion_module::CameraPoseFusionModule>(
        data_path, core, channel_map_info, logger, module_id, &module_info
    );
    if (!module->valid())
        return nullptr;

    return new aergo::module::dll::DllModuleWrapper(std::move(module), core, module_id, logger);
}

void destroyModule(aergo::module::dll::IDllModule* module)
{
    delete module;
}

