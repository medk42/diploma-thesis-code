#include "robot_stereo_camera_calibration_module.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"

#include "module_helpers/activation_wrapper/message_types.h"
#include "module_helpers/activation_wrapper/activation_wrapper.h"
#include "module_helpers/camera_pose_helper/message_structure.h"
#include "module_helpers/parameter_description/parameter_description.h"
#include "module_helpers/calibrated_stereo_robot_messages/calibrated_stereo_messages.h"

#include <limits>

#define MODULE_A_API_VERSION 2

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

using namespace aergo::module;
using namespace aergo::default_modules::robot_stereo_camera_calibration_module;
namespace cph = aergo::module::helpers::camera_pose_helper;
namespace p_desc = aergo::module::helpers::parameter_description;
namespace csr = aergo::module::helpers::calibrated_stereo_robot_messages;

static constexpr communication_channel::Consumer subscribe_consumers[] = {
    {
        .count_ = communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = cph::camera_with_pose_publish_producer.channel_type_identifier_,
        .display_name_ = "Stereo camera with flange pose",
        .display_description_ = "Input from camera_pose_injector: camera header + robot flange pose + stereo image blob.",
        .prioritized_ = false,
        .message_queue_capacity_ = 2
    }
};

static constexpr communication_channel::Producer response_producers[] = {
    aergo::module::helpers::activation_wrapper::message_types::activation_response_producer
};

static constexpr communication_channel::Producer publish_producers[] = {
    csr::calibrated_stereo_publish_producer
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "robot_stereo_camera_calibration_module",
    .display_name_ = "Stereo Camera Calibration (Robot)",
    .display_description_ = "Calibrates a stereo camera relative to the robot flange using Charuco captures. Provide at least 10 synchronized stereo images with flange pose from camera_pose_injector. Charuco: 8x12 grid, 24 mm squares, 18 mm markers, ArUco DICT_4X4_100, legacy pattern.",
    .publish_producers_ = publish_producers,
    .publish_producer_count_ = std::size(publish_producers),
    .response_producers_ = response_producers,
    .response_producer_count_ = std::size(response_producers),
    .subscribe_consumers_ = subscribe_consumers,
    .subscribe_consumer_count_ = std::size(subscribe_consumers),
    .request_consumers_ = nullptr,
    .request_consumer_count_ = 0,
    .auto_create_ = false,
    .prioritized_workers_count_ = 1,
    .regular_workers_count_ = 1
};

static p_desc::ParameterList parameters(std::vector<p_desc::ParameterDescription>{
    {
        .type_ = p_desc::ParameterType::CUSTOM,
        .param_name_ = "Stereo images with flange pose",
        .param_desc_ = "List of synchronized stereo captures with robot flange pose from camera_pose_injector. Provide at least 10 pairs capturing the Charuco board (8 rows x 12 columns, 24 mm squares, 18 mm markers, ArUco DICT_4X4_100, legacy pattern).",
        .custom_channel_type_ = p_desc::CustomChannelType::SUBSCRIBE,
        .custom_channel_id_ = 0,
        .as_list_ = true,
        .list_size_min_ = 10,
        .list_size_max_ = 0
    }
});


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<RobotStereoCameraCalibrationModule>(data_path, core, channel_map_info, logger, module_id, &module_info);
    if (!module->valid())
    {
        return nullptr;
    }

    auto wrapped_module = std::make_unique<aergo::module::helpers::activation_wrapper::ActivationWrapper>(std::move(module), &parameters);
    if (!wrapped_module->valid())
    {
        return nullptr;
    }

    return new aergo::module::dll::DllModuleWrapper(std::move(wrapped_module), core, module_id, logger);
}

void destroyModule(aergo::module::dll::IDllModule* module)
{
    delete module;
}
