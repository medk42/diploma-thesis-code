#include "camera_pose_injector_module.h"
#include "message_structure.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"
#include "module_helpers/camera_messages/messages.h"
#include "module_helpers/robot_interface/message_types_definitions.h"

#define MODULE_A_API_VERSION 2

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

using namespace aergo::module;
using namespace aergo::default_modules::camera_pose_injector_module;

static constexpr communication_channel::Producer camera_pose_injector_publish_producers[] = {
    camera_with_pose_publish_producer
};

static constexpr communication_channel::Consumer camera_pose_injector_subscribe_consumers[] = {
    { // local channel 0: camera image input
        .count_ = communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = aergo::module::helpers::camera_messages::camera_image_consumer.channel_type_identifier_,
        .display_name_ = "Camera Image Input",
        .display_description_ = "Camera image input; messages are augmented with the latest robot flange pose.",
        .prioritized_ = false,
        .message_queue_capacity_ = 2
    },
    { // local channel 1: robot status input
        .count_ = communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = aergo::module::helpers::robot_interface::robot_interface_status_consumer.channel_type_identifier_,
        .display_name_ = "Robot Status Input",
        .display_description_ = "Robot interface status messages; flange pose is extracted from robot control feature.",
        .prioritized_ = false,
        .message_queue_capacity_ = 4
    }
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "camera_pose_injector_module",
    .display_name_ = "Camera Pose Injector",
    .display_description_ = "Attaches the latest robot flange pose to incoming camera images and publishes fused messages.",
    .publish_producers_ = camera_pose_injector_publish_producers,
    .publish_producer_count_ = std::size(camera_pose_injector_publish_producers),
    .response_producers_ = nullptr,
    .response_producer_count_ = 0,
    .subscribe_consumers_ = camera_pose_injector_subscribe_consumers,
    .subscribe_consumer_count_ = std::size(camera_pose_injector_subscribe_consumers),
    .request_consumers_ = nullptr,
    .request_consumer_count_ = 0,
    .auto_create_ = false,
    .prioritized_workers_count_ = 1,
    .regular_workers_count_ = 2
};

const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<CameraPoseInjectorModule>(data_path, core, channel_map_info, logger, module_id, &module_info);
    if (module->valid())
    {
        return new aergo::module::dll::DllModuleWrapper(std::move(module), core, module_id, logger);
    }
    return nullptr;
}

void destroyModule(aergo::module::dll::IDllModule* module)
{
    delete module;
}
