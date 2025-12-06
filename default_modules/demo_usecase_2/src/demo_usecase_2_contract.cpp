#include "module_common/module_contract.h"
#include "demo_usecase_2.h"
#include "module_helpers/usecase_wrapper/usecase_wrapper.h"
#include "module_common/dll_module_wrapper.h"
#include "module_helpers/usecase_wrapper/message_types.h"
#include "module_helpers/camera_messages/messages.h"

#define LOCAL_MODULE_API_VERSION 2

static_assert(LOCAL_MODULE_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

using namespace aergo::module;
using namespace aergo::default_modules::demo_usecase_2;
using namespace aergo::module::helpers::usecase_wrapper;

static constexpr communication_channel::Consumer subscribe_consumers[] = {
    aergo::module::helpers::camera_messages::camera_image_consumer
};

static constexpr communication_channel::Producer response_producers[] = {
    aergo::module::helpers::usecase_wrapper::message_types::usecase_response_producer
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "demo_usecase_2",
    .display_name_ = "Demo Usecase 2",
    .display_description_ = "Demo usecase module 2 that demonstrates usecase wrapper functionality - reading camera data.",
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

std::string param_name = "DEMO 2";
std::string param_desc = "Demo usecase 2 that demonstrates usecase wrapper functionality - reading camera data.";

p_desc::ParameterList auto_parameters(std::move(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Camera Image",
        .param_desc_ = "An example automatic parameter (CUSTOM type) for camera image.",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0,
        .as_list_ = true,
        .list_size_min_ = 1,
        .list_size_max_ = 0 // no max limit
    }
}));

p_desc::ParameterList required_parameters; // no required parameters
p_desc::ParameterList advanced_parameters; // no advanced parameters


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::demo_usecase_2::DemoUsecase2>(data_path, core, channel_map_info, logger, module_id, &module_info, multi_program_support, pause_support, stop_support);
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