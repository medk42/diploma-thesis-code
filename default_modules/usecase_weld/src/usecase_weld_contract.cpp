#include "module_common/module_contract.h"
#include "usecase_weld.h"
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
using namespace aergo::default_modules::usecase_weld;
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
    .module_type_identifier_ = "usecase_weld",
    .display_name_ = "Usecase Weld",
    .display_description_ = "Welding usecase module that detects seams between objects and follows a user-defined welding trajectory along detected edges.",
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

std::string usecase_name = "WELD";
std::string usecase_desc = "Welding usecase that detects seams between objects and follows a user-defined welding trajectory along detected edges.";

p_desc::ParameterList auto_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Scene Scan",
        .param_desc_ = "Request to scan the scene and detect objects for seam detection.",
        .custom_channel_type_ = p_desc::CustomChannelType::REQUEST,
        .custom_channel_id_ = 1
    },
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Welding Trajectory",
        .param_desc_ = "Welding trajectory in the world coordinate system using the pen's pose intent. The trajectory will be aligned to detected seams between objects.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0
    }
}));

p_desc::ParameterList required_parameters;

p_desc::ParameterList advanced_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Movement Speed [mm/s]",
        .param_desc_ = "Speed of movement outside of welding (approach, depart, repositioning).",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 10,
        .max_value_double_ = 2000,
        .as_slider_ = true,
        .default_value_ = "100"
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Welding Speed [mm/s]",
        .param_desc_ = "Speed during welding along the seam.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 1,
        .max_value_double_ = 50,
        .as_slider_ = true,
        .default_value_ = "10"
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Acceleration [mm/s²]",
        .param_desc_ = "Acceleration for all movements.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 10,
        .max_value_double_ = 8000,
        .as_slider_ = true,
        .default_value_ = "500"
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Weld Offset [mm]",
        .param_desc_ = "Distance of the torch TCP from the seam while welding (offset from seam line).",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 0,
        .max_value_double_ = 30,
        .as_slider_ = true,
        .default_value_ = "10"
    },
    {
        .type_ = p_desc::ParameterType::DOUBLE,
        .param_name_ = "Approach Distance [mm]",
        .param_desc_ = "Distance of approach and depart points from the seam.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_double_ = 20,
        .max_value_double_ = 200,
        .as_slider_ = true,
        .default_value_ = "70"
    }
}));

const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::usecase_weld::UsecaseWeld>(data_path, core, channel_map_info, logger, module_id, &module_info, multi_program_support, pause_support, stop_support);
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