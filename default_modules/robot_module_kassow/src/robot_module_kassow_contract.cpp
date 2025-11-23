#include "robot_module_kassow/robot_module_kassow.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"
#include "module_helpers/robot_interface/message_types_definitions.h"

#define MODULE_A_API_VERSION 2

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in Kassow robot module.");

using namespace aergo::module;

static constexpr communication_channel::Producer kassow_publish_producers[] = {
    aergo::module::helpers::robot_interface::robot_interface_status_producer,
    aergo::module::helpers::robot_interface::robot_interface_finished_producer
};

static constexpr communication_channel::Producer kassow_response_producers[] = {
    aergo::module::helpers::robot_interface::robot_interface_response_producer
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "robot_module_kassow",
    .display_name_ = "Kassow Robot Module",
    .display_description_ = "Adapter that forwards robot_interface requests to a Kassow CBun over TCP.",
    .publish_producers_ = kassow_publish_producers,
    .publish_producer_count_ = std::size(kassow_publish_producers),
    .response_producers_ = kassow_response_producers,
    .response_producer_count_ = std::size(kassow_response_producers),
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
    auto module = std::make_unique<aergo::default_modules::robot_module_kassow::RobotModuleKassow>(data_path, core, channel_map_info, logger, module_id, &module_info);
    if (!module->valid())
        return nullptr;

    return new aergo::module::dll::DllModuleWrapper(std::move(module), core, module_id, logger);
}

void destroyModule(aergo::module::dll::IDllModule* module)
{
    delete module;
}
