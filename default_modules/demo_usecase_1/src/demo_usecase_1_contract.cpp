#include "module_common/module_contract.h"
#include "demo_usecase_1.h"
#include "message_structure.h"
#include "module_helpers/usecase_wrapper/usecase_wrapper.h"
#include "module_common/dll_module_wrapper.h"
#include "module_helpers/usecase_wrapper/message_types.h"

#define LOCAL_MODULE_API_VERSION 2

static_assert(LOCAL_MODULE_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

using namespace aergo::module;
using namespace aergo::default_modules::demo_usecase_1;
using namespace aergo::module::helpers::usecase_wrapper;

static constexpr communication_channel::Consumer subscribe_consumers[] = {
    { 
        .count_ = communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = pen_3d_pose_publish_producer_channel_type,
        .display_name_ = "3D Pen Pose", 
        .display_description_ = "3D position and orientation of the pen in meters and Rodrigues rotation vector format",
        .prioritized_ = false,
        .message_queue_capacity_ = 1 // no need for queueing, only latest pose is used
    }
};

static constexpr communication_channel::Producer response_producers[] = {
    aergo::module::helpers::usecase_wrapper::message_types::usecase_response_producer
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "demo_usecase_1",
    .display_name_ = "Demo Usecase 1",
    .display_description_ = "Demo usecase module 1 that demonstrates usecase wrapper functionality.",
    .publish_producers_ = nullptr,
    .publish_producer_count_ = 0,
    .response_producers_ = response_producers,
    .response_producer_count_ = std::size(response_producers),
    .subscribe_consumers_ = subscribe_consumers,
    .subscribe_consumer_count_ = std::size(subscribe_consumers),
    .request_consumers_ = nullptr,
    .request_consumer_count_ = 0,
    .auto_create_ = false
};

bool multi_program_support = false;
bool pause_support = true;
bool stop_support = true;

std::string param_name = "DEMO 1";
std::string param_desc = "Demo usecase 1 that demonstrates usecase wrapper functionality.";

p_desc::ParameterList auto_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Pen Poses #1",
        .param_desc_ = "An example automatic parameter (CUSTOM type), list 1-3.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0,
        .as_list_ = true,
        .list_size_min_ = 1,
        .list_size_max_ = 3
    },
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Pen Pose #2",
        .param_desc_ = "An example automatic parameter (CUSTOM type), single value.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0
    }
}));

p_desc::ParameterList required_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::LONG,
        .param_name_ = "Sleep between writes (ms)",
        .param_desc_ = "Time to sleep between writing pen poses to log file, in milliseconds.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 0,
        .max_value_long_ = 2000,
        .default_value_ = "500"
    },
    {
        .type_ = p_desc::ParameterType::LONG,
        .param_name_ = "Repetitions count",
        .param_desc_ = "Number of times to write pen poses to log file.",
        .limit_min_ = true,
        .limit_max_ = true,
        .min_value_long_ = 1,
        .max_value_long_ = 10
    }
}));

p_desc::ParameterList advanced_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::STRING,
        .param_name_ = "Message Prefix",
        .param_desc_ = "Prefix to add to each line written to log file.",
        .default_value_ = "PenPose"
    },
    {
        .type_ = p_desc::ParameterType::BOOL,
        .param_name_ = "Allow Pause",
        .param_desc_ = "If true, program execution can be paused and resumed during this command.",
        .default_value_ = "1"
    },
    {
        .type_ = p_desc::ParameterType::BOOL,
        .param_name_ = "Allow Stop",
        .param_desc_ = "If true, program execution can be stopped during this command.",
        .default_value_ = "1"
    }
}));


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::demo_usecase_1::DemoUsecase1>(data_path, core, channel_map_info, logger, module_id, &module_info, multi_program_support, pause_support, stop_support);
    if (!module->valid())
    {
        return nullptr;
    }

    auto wrapper = std::make_unique<UsecaseWrapper>(
        std::move(module), param_name, param_desc,
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