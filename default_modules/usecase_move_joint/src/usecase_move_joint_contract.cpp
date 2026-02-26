#include "module_common/module_contract.h"
#include "usecase_move_joint.h"
#include "module_helpers/usecase_wrapper/usecase_wrapper.h"
#include "module_common/dll_module_wrapper.h"
#include "module_helpers/usecase_wrapper/message_types.h"
#include "module_helpers/robot_interface/message_types_definitions.h"

#define MODULE_API_VERSION 2

static_assert(MODULE_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

using namespace aergo::module;
using namespace aergo::default_modules::usecase_move_joint;
using namespace aergo::module::helpers::usecase_wrapper;

static constexpr communication_channel::Consumer subscribe_consumers[] = {
    aergo::module::helpers::robot_interface::robot_interface_status_consumer,
    aergo::module::helpers::robot_interface::robot_interface_finished_consumer
};

static constexpr communication_channel::Consumer request_consumers[] = {
    aergo::module::helpers::robot_interface::robot_interface_request_consumer
};

static constexpr communication_channel::Producer response_producers[] = {
    aergo::module::helpers::usecase_wrapper::message_types::usecase_response_producer
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "usecase_move_joint",
    .display_name_ = "Usecase Move Joint",
    .display_description_ = "Usecase module for moving the robot to a target joint configuration. Target joint positions are read from the current robot joint configuration.",
    .publish_producers_ = nullptr,
    .publish_producer_count_ = 0,
    .response_producers_ = response_producers,
    .response_producer_count_ = std::size(response_producers),
    .subscribe_consumers_ = subscribe_consumers,
    .subscribe_consumer_count_ = std::size(subscribe_consumers),
    .request_consumers_ = request_consumers,
    .request_consumer_count_ = std::size(request_consumers),
    .auto_create_ = false,
    .prioritized_workers_count_ = 2,
    .regular_workers_count_ = 1
};

bool multi_program_support = false;
bool pause_support = false;
bool stop_support = true;

std::string usecase_name = "MOVE_J";
std::string usecase_desc = "Usecase for moving the robot to a target joint configuration. Target joint positions are read from the current robot joint configuration.";

p_desc::ParameterList auto_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Target Joints",
        .param_desc_ = "Target joint positions read from the current robot joint configuration.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0
    }
}));

p_desc::ParameterList required_parameters;

p_desc::ParameterList advanced_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Joint Speed [deg/s]",
        .param_desc_ = "Speed at which the robot joints should move.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 1,
        .max_value_double_ = 225,
        .as_slider_ = true,
        .default_value_ = "45"
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Joint Acceleration [deg/s²]",
        .param_desc_ = "Acceleration at which the robot joints should move.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 1,
        .max_value_double_ = 360,
        .as_slider_ = true,
        .default_value_ = "360"
    }
}));

const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::usecase_move_joint::UsecaseMoveJoint>(data_path, core, channel_map_info, logger, module_id, &module_info, multi_program_support, pause_support, stop_support);
    if (!module->valid())
    {
        return nullptr;
    }

    auto wrapper = std::make_unique<UsecaseWrapper>(
        std::move(module), usecase_name, usecase_desc,
        auto_parameters, required_parameters, advanced_parameters
    );

    if (!wrapper->valid())
    {
        return nullptr;
    }

    return new dll::DllModuleWrapper(std::move(wrapper), core, module_id, logger);
}

void destroyModule(aergo::module::dll::IDllModule* module)
{
    delete module;
}