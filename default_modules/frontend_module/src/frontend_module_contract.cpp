#include "frontend_module.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"

#include "module_helpers/activation_wrapper/message_types.h"
#include "module_helpers/visualization_3d_interface/message_types.h"
#include "module_helpers/usecase_wrapper/message_types.h"
#include "module_helpers/camera_messages/messages.h"
#include "module_helpers/scene_detection_helper/message_types.h"
#include "module_helpers/robot_interface/message_types_definitions.h"

#define MODULE_API_VERSION 2

static_assert(MODULE_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

    
using namespace aergo::module;
namespace sdh = aergo::module::helpers::scene_detection_helper;

static constexpr communication_channel::Consumer web_visualization_module_request_consumers[] = {
    aergo::module::helpers::activation_wrapper::message_types::activation_request_consumer,
    aergo::module::helpers::visualization_3d_interface::visualization_3d_interface_request_consumer,
    aergo::module::helpers::usecase_wrapper::message_types::usecase_request_consumer,
    {
        .count_ = communication_channel::Consumer::Count::AUTO_ALL,
        .channel_type_identifier_ = sdh::scene_detection_request_consumer.channel_type_identifier_,
        .display_name_ = sdh::scene_detection_request_consumer.display_name_,
        .display_description_ = sdh::scene_detection_request_consumer.display_description_,
        .prioritized_ = sdh::scene_detection_request_consumer.prioritized_,
        .message_queue_capacity_ = sdh::scene_detection_request_consumer.message_queue_capacity_
    },
    {
        .count_ = communication_channel::Consumer::Count::AUTO_ALL,
        .channel_type_identifier_ = aergo::module::helpers::robot_interface::robot_interface_request_consumer.channel_type_identifier_,
        .display_name_ = aergo::module::helpers::robot_interface::robot_interface_request_consumer.display_name_,
        .display_description_ = aergo::module::helpers::robot_interface::robot_interface_request_consumer.display_description_,
        .prioritized_ = false,
        .message_queue_capacity_ = 4
    }
};

static constexpr communication_channel::Consumer web_visualization_module_subscribe_consumers[] = {
    communication_channel::Consumer {
        .count_ = communication_channel::Consumer::Count::AUTO_ALL,
        .channel_type_identifier_ = aergo::module::helpers::camera_messages::camera_image_consumer.channel_type_identifier_,
        .display_name_ = aergo::module::helpers::camera_messages::camera_image_consumer.display_name_,
        .display_description_ = aergo::module::helpers::camera_messages::camera_image_consumer.display_description_,
        .prioritized_ = false,
        .message_queue_capacity_ = 4
    },
    communication_channel::Consumer {
        .count_ = communication_channel::Consumer::Count::AUTO_ALL,
        .channel_type_identifier_ = aergo::module::helpers::visualization_3d_interface::visualization_3d_interface_publish_producer.channel_type_identifier_,
        .display_name_ = "3D Visualization Data",
        .display_description_ = "Subscribe to 3D announcements and scene updates from modules supporting 3D visualization.",
        .prioritized_ = true,
        .message_queue_capacity_ = 10
    },
    communication_channel::Consumer {
        .count_ = communication_channel::Consumer::Count::AUTO_ALL,
        .channel_type_identifier_ = aergo::module::helpers::robot_interface::robot_interface_status_consumer.channel_type_identifier_,
        .display_name_ = aergo::module::helpers::robot_interface::robot_interface_status_consumer.display_name_,
        .display_description_ = aergo::module::helpers::robot_interface::robot_interface_status_consumer.display_description_,
        .prioritized_ = false,
        .message_queue_capacity_ = 4
    }
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "web_frontend_module",
    .display_name_ = "Web Visualization Module",
    .display_description_ = "Provides web-based module creation, activation and visualization interface. Contains 3D visualization of modules and their data and program tree for creation of robot programs.",
    .publish_producers_ = nullptr,
    .publish_producer_count_ = 0,
    .response_producers_ = nullptr,
    .response_producer_count_ = 0,
    .subscribe_consumers_ = web_visualization_module_subscribe_consumers,
    .subscribe_consumer_count_ = std::size(web_visualization_module_subscribe_consumers),
    .request_consumers_ = web_visualization_module_request_consumers,
    .request_consumer_count_ = std::size(web_visualization_module_request_consumers),
    .auto_create_ = true,
    .prioritized_workers_count_ = 2,
    .regular_workers_count_ = 3
};


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::frontend_module::FrontendModule>(data_path, core, channel_map_info, logger, module_id, &module_info);
    if (module->valid())
    {
        return new aergo::module::dll::DllModuleWrapper(std::move(module), core, module_id, logger);
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