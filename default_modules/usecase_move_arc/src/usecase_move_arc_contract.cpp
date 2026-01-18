#include "module_common/module_contract.h"
#include "usecase_move_arc.h"
#include "module_helpers/usecase_wrapper/usecase_wrapper.h"
#include "module_common/dll_module_wrapper.h"
#include "module_helpers/usecase_wrapper/message_types.h"
#include "module_helpers/robot_interface/message_types_definitions.h"
#include "module_helpers/pen_messages/message_types.h"

#define MODULE_API_VERSION 2

static_assert(MODULE_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

using namespace aergo::module;
using namespace aergo::default_modules::usecase_move_arc;
using namespace aergo::module::helpers::usecase_wrapper;

static constexpr communication_channel::Consumer subscribe_consumers[] = {
    aergo::module::helpers::pen_messages::pen_message_intent_subscribe_consumer,
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
    .module_type_identifier_ = "usecase_move_arc",
    .display_name_ = "Usecase Move Arc",
    .display_description_ = "Usecase module for moving the robot from the current position to a target position in an arc path. Target position is specified as a pose in the world coordinate system using the pen's current pose.",
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

std::string usecase_name = "MOVE_A";
std::string usecase_desc = "Usecase for moving the robot from the current position to a target position in an arc path. Target position is specified as a pose in the world coordinate system using the pen's current pose.";

p_desc::ParameterList auto_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Arc Start",
        .param_desc_ = "Start pose of the arc in the world coordinate system using the pen's current pose.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0
    },
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Arc Through Point",
        .param_desc_ = "Through point pose of the arc in the world coordinate system using the pen's current pose.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0
    },
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Arc End",
        .param_desc_ = "End pose of the arc in the world coordinate system using the pen's current pose.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0
    }
}));

p_desc::ParameterList required_parameters;

p_desc::ParameterList advanced_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Movement Speed [mm/s]",
        .param_desc_ = "Speed at which the robot should move in arc path.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 10,
        .max_value_double_ = 2000,
        .as_slider_ = true,
        .default_value_ = "100"
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Movement Acceleration [mm/s²]",
        .param_desc_ = "Acceleration at which the robot should move in arc path.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 10,
        .max_value_double_ = 8000,
        .as_slider_ = true,
        .default_value_ = "500"
    },
    {
        .type_ = p_desc::ParameterType::BOOL,
        .param_name_ = "As Circle",
        .param_desc_ = "If enabled, treat the path as a circle instead of an arc segment defined by the three points.",
        .default_value_ = "0"
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Circle Degrees",
        .param_desc_ = "Percentage of the full circle to traverse when 'As Circle' is enabled (0 to 720 degrees, where 360 = full circle).",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 0,
        .max_value_double_ = 720,
        .as_slider_ = true,
        .default_value_ = "360"
    },
    {
        .type_ = p_desc::ParameterType::ENUM,
        .param_name_ = "Arc Orientation Type",
        .param_desc_ = "Orientation behavior during arc movement: Fixed keeps orientation constant, Tangent aligns orientation with the path tangent.",
        .enum_values_ = { "Fixed", "Tangent" },
        .default_value_ = "1"
    }
}));

const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::usecase_move_arc::UsecaseMoveArc>(data_path, core, channel_map_info, logger, module_id, &module_info, multi_program_support, pause_support, stop_support);
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