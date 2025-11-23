#include "robot_module_kassow/robot_module_kassow.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"
#include "module_helpers/robot_interface/message_types_definitions.h"
#include "module_helpers/activation_wrapper/message_types.h"
#include "module_helpers/activation_wrapper/activation_wrapper.h"
#include "module_helpers/parameter_description/parameter_description.h"

#define MODULE_A_API_VERSION 2

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in Kassow robot module.");

using namespace aergo::module;

static constexpr communication_channel::Producer kassow_publish_producers[] = {
    aergo::module::helpers::robot_interface::robot_interface_status_producer,
    aergo::module::helpers::robot_interface::robot_interface_finished_producer
};

static constexpr communication_channel::Producer kassow_response_producers[] = {
    aergo::module::helpers::robot_interface::robot_interface_response_producer,
    aergo::module::helpers::activation_wrapper::message_types::activation_response_producer
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

static aergo::module::helpers::parameter_description::ParameterList parameters(std::vector<aergo::module::helpers::parameter_description::ParameterDescription>{
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::STRING,
        .param_name_ = "Host",
        .param_desc_ = "Kassow CBun host",
        .default_value_ = "127.0.0.1"
    },
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::LONG,
        .param_name_ = "Port",
        .param_desc_ = "TCP port of Kassow CBun server",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 1,
        .max_value_long_ = 65535,
        .default_value_ = "15050"
    },
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::LONG,
        .param_name_ = "Request Timeout (ms)",
        .param_desc_ = "Timeout for RPC requests",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 5,
        .max_value_long_ = 1000,
        .default_value_ = "200"
    },
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::LONG,
        .param_name_ = "Poll Interval (ms)",
        .param_desc_ = "Interval for async message polling",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 5,
        .max_value_long_ = 500,
        .default_value_ = "10"
    },
    {
        .type_ = aergo::module::helpers::parameter_description::ParameterType::LONG,
        .param_name_ = "Reconnect Wait (ms)",
        .param_desc_ = "Delay before reconnect attempts after disconnect",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 50,
        .max_value_long_ = 10000,
        .default_value_ = "500"
    }
});

const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::robot_module_kassow::RobotModuleKassow>(data_path, core, channel_map_info, logger, module_id, &module_info);
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
