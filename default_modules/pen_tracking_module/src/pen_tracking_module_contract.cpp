#include "pen_tracking_module.h"
#include "message_structure.h"

#include "module_common/module_contract.h"
#include "module_common/dll_module_wrapper.h"

#include "module_helpers/visualization_3d_interface/message_types.h"


#define MODULE_A_API_VERSION 2

static_assert(MODULE_A_API_VERSION == PLUGIN_API_VERSION,
    "Incompatible plugin API version in module.");

    
using namespace aergo::module;
using namespace aergo::module::helpers;

static constexpr communication_channel::Producer pen_tracking_module_publish_producers[] = {
    aergo::default_modules::pen_tracking_module::pen_3d_pose_publish_producer,
    visualization_3d_interface::visualization_3d_interface_publish_producer
};

static constexpr communication_channel::Consumer pen_tracking_module_message_consumers[] = {
    { 
        .count_ = communication_channel::Consumer::Count::SINGLE,
        .channel_type_identifier_ = aergo::default_modules::pen_tracking_module::image_bgr_calib_data_channel_type,
        .display_name_ = "Calibrated Camera Frame Input",
        .display_description_ = "Input channel for calibrated camera frames in 8-bit BGR format with 3 channels, extended with camera calibration data (camera matrix and distortion coefficients). While activated, this data is used to track the pen position in 3D space based on visible markers via OpenCV. While deactivated, input images are used for calibrating the pen.",
        .prioritized_ = false, // activation communication is priority
        .message_queue_capacity_ = 1 // no need for queueing, only latest image is used
    }
};

static constexpr communication_channel::Producer pen_tracking_module_response_producers[] = {
    visualization_3d_interface::visualization_3d_interface_response_producer
};

static constexpr ModuleInfo module_info = {
    .module_type_identifier_ = "pen_tracking_module",
    .display_name_ = "Pen Tracking Module",
    .display_description_ = "Module receives calibrated camera data from a connected camera, tracks the pen position in 3D space based on visible markers via OpenCV and publishes the 3D pose of the pen tip. While deactivated, input images are used for calibrating the pen.",
    .publish_producers_ = pen_tracking_module_publish_producers,
    .publish_producer_count_ = std::size(pen_tracking_module_publish_producers),
    .response_producers_ = pen_tracking_module_response_producers,
    .response_producer_count_ = std::size(pen_tracking_module_response_producers),
    .subscribe_consumers_ = pen_tracking_module_message_consumers,
    .subscribe_consumer_count_ = std::size(pen_tracking_module_message_consumers),
    .request_consumers_ = nullptr,
    .request_consumer_count_ = 0,
    .auto_create_ = false,
    .prioritized_workers_count_ = 1,
    .regular_workers_count_ = 1
};


const ModuleInfo* readModuleInfo()
{
    return &module_info;
}

aergo::module::dll::IDllModule* createModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
{
    auto module = std::make_unique<aergo::default_modules::pen_tracking_module::PenTrackingModule>(data_path, core, channel_map_info, logger, module_id, &module_info);
    if (module->valid())
    {
        return new aergo::module::dll::DllModuleWrapper(std::move(module), core, module_id, logger);
    }
    else
    {
        return nullptr;
    }
}

void destroyModule(aergo::module::dll::IDllModule* module)
{
    delete module;
}