#include "pen_calibration_multicam_module.h"

using namespace aergo::default_modules::pen_calibration_multicam_module;
using namespace aergo::module;

PenCalibrationMulticamModule::PenCalibrationMulticamModule(
    const char* data_path,
    aergo::module::ICore* core,
    aergo::module::InputChannelMapInfo channel_map_info,
    const aergo::module::logging::ILogger* logger,
    uint64_t module_id,
    const aergo::module::ModuleInfo* module_info)
: aergo::module::BaseModule(data_path, core, channel_map_info, logger, module_id, module_info)
{
    if (!getSubscribeChannelByName(ccrm::calibrated_camera_publish_producer.channel_type_identifier_, calibrated_camera_subscribe_channel_))
    {
        log(logging::LogType::ERROR, "PenCalibrationMulticam: failed to resolve calibrated camera subscribe channel.");
        return;
    }

    valid_ = true;
}


void* PenCalibrationMulticamModule::query_capability(const std::type_info& id) noexcept
{
    if (id == typeid(BaseModule)) return static_cast<BaseModule*>(this);
    if (id == typeid(helpers::activation_wrapper::IActivableModule)) return static_cast<helpers::activation_wrapper::IActivableModule*>(this);
    return nullptr;
}


IModule::IngressDecision PenCalibrationMulticamModule::onIngress(
    ProcessingType kind,
    uint32_t local_channel_id,
    aergo::module::ChannelIdentifier /*src*/,
    const aergo::module::message::MessageHeader& /*msg*/,
    QueueStatus queue_status) noexcept
{
    if (kind == ProcessingType::MESSAGE && local_channel_id == calibrated_camera_subscribe_channel_)
    {
        if (queue_status == QueueStatus::QUEUE_FULL)
        {
            return IngressDecision::DROP;
        }
        return IngressDecision::ACCEPT_REPLACE_QUEUE;
    }

    return IngressDecision::DROP;
}


void PenCalibrationMulticamModule::processMessage(
    uint32_t /*subscribe_consumer_id*/,
    aergo::module::ChannelIdentifier /*source_channel*/,
    aergo::module::message::MessageHeader /*message*/) noexcept
{
    // Pen calibration ingestion to be implemented in a later iteration.
}


bool PenCalibrationMulticamModule::activate(
    std::vector<std::vector<std::vector<uint8_t>>>& /*parameter_values*/,
    const std::atomic<bool>& /*cancel_flag*/,
    std::atomic<bool>& /*cancelled*/)
{
    log(logging::LogType::WARNING, "PenCalibrationMulticam: activation not implemented.");
    return false;
}


bool PenCalibrationMulticamModule::deactivate(const std::atomic<bool>& /*cancel_flag*/, std::atomic<bool>& /*cancelled*/)
{
    // Deactivation is a no-op for now.
    return false;
}


aergo::module::helpers::activation_wrapper::message_types::ProgressData PenCalibrationMulticamModule::getActivationProgress()
{
    using ProgressData = aergo::module::helpers::activation_wrapper::message_types::ProgressData;
    using ProgressType = aergo::module::helpers::activation_wrapper::message_types::ProgressType;
    return { .progress_type_ = ProgressType::NONE, .progress_max_int_ = 0, .progress_current_value_double_ = 0.0, .progress_current_value_int_ = 0 };
}


aergo::module::ISerializableModule::SaveData PenCalibrationMulticamModule::save() noexcept
{
    aergo::module::ISerializableModule::SaveData data;
    data.supports_saving_ = false;
    data.success_ = false;
    return data;
}


bool PenCalibrationMulticamModule::load(aergo::module::ISerializableModule::SaveData /*data*/) noexcept
{
    return false;
}
