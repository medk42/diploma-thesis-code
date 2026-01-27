#include "module_common/module_contract.h"
#include "usecase_pick_and_place.h"
#include "module_helpers/usecase_wrapper/usecase_wrapper.h"
#include "module_common/dll_module_wrapper.h"
#include "module_helpers/usecase_wrapper/message_types.h"
#include "module_helpers/robot_interface/message_types_definitions.h"
#include "module_helpers/pen_messages/message_types.h"
#include "module_helpers/scene_detection_helper/message_types.h"

#define MODULE_API_VERSION 2

static_assert(MODULE_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

using namespace aergo::module;
using namespace aergo::default_modules::usecase_pick_and_place;
using namespace aergo::module::helpers::usecase_wrapper;

static constexpr communication_channel::Consumer subscribe_consumers[] = {
    aergo::module::helpers::pen_messages::pen_message_intent_subscribe_consumer,
    aergo::module::helpers::robot_interface::robot_interface_status_consumer,
    aergo::module::helpers::robot_interface::robot_interface_finished_consumer
};

static constexpr communication_channel::Consumer request_consumers[] = {
    aergo::module::helpers::robot_interface::robot_interface_request_consumer,
    aergo::module::helpers::scene_detection_helper::scene_detection_request_consumer
};

static constexpr communication_channel::Producer response_producers[] = {
    aergo::module::helpers::usecase_wrapper::message_types::usecase_response_producer
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "usecase_pick_and_place",
    .display_name_ = "Usecase Pick and Place",
    .display_description_ = "Pick and place usecase module for picking objects from a pick position and placing them at a target position.",
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

std::string usecase_name = "PICK";
std::string usecase_desc = "Pick and place usecase for picking objects from a pick position and placing them at a target position.";

p_desc::ParameterList auto_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Scene Request",
        .param_desc_ = "Request to scan the scene and detect objects.",
        .custom_channel_type_ = p_desc::CustomChannelType::REQUEST,
        .custom_channel_id_ = 1
    },
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Pick Position",
        .param_desc_ = "Pick position in the world coordinate system using the pen's current pose.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0
    },
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Place Position",
        .param_desc_ = "Place position in the world coordinate system using the pen's current pose.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0
    }
}));

p_desc::ParameterList required_parameters;

p_desc::ParameterList advanced_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Movement Speed [mm/s]",
        .param_desc_ = "Speed at which the robot should move in linear path.",
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
        .param_desc_ = "Acceleration at which the robot should move in linear path.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 10,
        .max_value_double_ = 8000,
        .as_slider_ = true,
        .default_value_ = "500"
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Pick Detection Distance [mm]",
        .param_desc_ = "Maximum distance in Z direction from pick position to detect an object for picking.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 1,
        .max_value_double_ = 100,
        .as_slider_ = true,
        .default_value_ = "20"
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Lift Distance [mm]",
        .param_desc_ = "Distance to move up after picking an object before moving to place position.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 10,
        .max_value_double_ = 500,
        .as_slider_ = true,
        .default_value_ = "200"
    }
}));

const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::usecase_pick_and_place::UsecasePickAndPlace>(data_path, core, channel_map_info, logger, module_id, &module_info, multi_program_support, pause_support, stop_support);
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