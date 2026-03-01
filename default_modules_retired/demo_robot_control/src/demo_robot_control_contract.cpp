#include "module_common/module_contract.h"
#include "demo_robot_control.h"
#include "module_helpers/usecase_wrapper/usecase_wrapper.h"
#include "module_common/dll_module_wrapper.h"
#include "module_helpers/usecase_wrapper/message_types.h"
#include "module_helpers/robot_interface/message_types_definitions.h"

#define LOCAL_MODULE_API_VERSION 2

static_assert(LOCAL_MODULE_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

using namespace aergo::module;
using namespace aergo::default_modules::demo_robot_control;
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
    .module_type_identifier_ = "demo_robot_control",
    .display_name_ = "Demo Robot Control",
    .display_description_ = "Demo robot control module that demonstrates usecase wrapper functionality.",
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
bool pause_support = true;
bool stop_support = true;

std::string usecase_name = "DEMO_R";
std::string usecase_desc = "Robot demo usecase for testing robot movement types.";

p_desc::ParameterList auto_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Robot Poses",
        .param_desc_ = "One or more robot poses (CUSTOM type) to move to - poses contain both world and joint coordinates.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0,
        .as_list_ = true,
        .list_size_min_ = 1,
        .list_size_max_ = 5
    }
}));

p_desc::ParameterList required_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::BOOL,
        .param_name_ = "Blocking Move",
        .param_desc_ = "If true, robot will perform blocking move to each target pose. If false, robot will perform asynchronous move.",
        .default_value_ = "1"
    },
    {
        .type_ = p_desc::ParameterType::ENUM,
        .param_name_ = "Movement Type",
        .param_desc_ = "Type of movement to perform for each target pose.",
        .enum_values_ = { "JOINT", "LINEAR", "ARC", "TRAJECTORY" },
        .default_value_ = "0"
    }
}));

p_desc::ParameterList advanced_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Joint Speed [deg]",
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
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Movement Speed [mm/s]",
        .param_desc_ = "Speed at which the robot should move in linear, arc and trajectory modes.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 0,
        .max_value_double_ = 2000,
        .as_slider_ = true,
        .default_value_ = "50"
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Movement Acceleration [mm/s²]",
        .param_desc_ = "Acceleration at which the robot should move in linear, arc and trajectory modes.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 0,
        .max_value_double_ = 8000,
        .as_slider_ = true,
        .default_value_ = "500"
    },
    {
        .type_ = p_desc::ParameterType::ENUM,
        .param_name_ = "Orientation type",
        .param_desc_ = "Type of orientation for arc and trajectory movements.",
        .enum_values_ = { "FIXED", "TANGENTIAL" },
        .default_value_ = "1"
    },
    {
        .type_ = p_desc::ParameterType::BOOL,
        .param_name_ = "Arc as circle",
        .param_desc_ = "If true, arc movements will be performed as a percentage of a full circle. If false, arc movements will be performed as a standard arc between two points starting at current position.",
        .default_value_ = "0"
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Circle percentage [%]",
        .param_desc_ = "Percentage of full circle to perform for arc movements when 'Arc as circle' is true.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 1,
        .max_value_double_ = 200,
        .as_slider_ = true,
        .default_value_ = "100"
    }
}));


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::demo_robot_control::DemoRobotControl>(data_path, core, channel_map_info, logger, module_id, &module_info, multi_program_support, pause_support, stop_support);
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