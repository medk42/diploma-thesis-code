#include "module_common/dll_module_wrapper.h"

using namespace aergo::module;
using namespace aergo::module::dll;



DllModuleWrapper::DllModuleWrapper(std::unique_ptr<aergo::module::IModule> module, uint32_t thread_sleep_ms)
: PeriodicThread(thread_sleep_ms), module_(std::move(module))
{}



bool DllModuleWrapper::threadStart(uint32_t timeout_ms) noexcept
{
    return _threadStart(timeout_ms);
}



bool DllModuleWrapper::threadStop(uint32_t timeout_ms) noexcept
{
    return _threadStop(timeout_ms);
}



void DllModuleWrapper::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    pushProcessingData(ProcessingData::Type::MESSAGE, subscribe_consumer_id, source_channel, message);
}



void DllModuleWrapper::processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    pushProcessingData(ProcessingData::Type::REQUEST, response_producer_id, source_channel, message);
}



void DllModuleWrapper::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    pushProcessingData(ProcessingData::Type::RESPONSE, request_consumer_id, source_channel, message);
}



void DllModuleWrapper::pushProcessingData(ProcessingData::Type type, uint32_t local_channel_id, ChannelIdentifier source_channel, message::MessageHeader message)
{
    std::vector<uint8_t> data(message.data_, message.data_ + message.data_len_);
    message.data_ = data.data();

    std::vector<message::SharedDataBlob> blobs(message.blobs_, message.blobs_ + message.blob_count_);
    message.blobs_ = blobs.data();

    ProcessingData processing_data {
        .type_ = type,
        .local_channel_id_ = local_channel_id,
        .source_channel_ = source_channel,
        .message_ = message,
        .data_ = std::move(data),
        .blobs_ = std::move(blobs)
    };

    {
        std::lock_guard<std::mutex> lock(processing_data_queue_mutex_);
        processing_data_queue_.push(std::move(processing_data));
    }
}



void DllModuleWrapper::_threadInit() {}

void DllModuleWrapper::_threadDeinit() {}



void DllModuleWrapper::_threadCycle()
{
    // Process messages/requests/responses
    while (true)
    {
        bool request_available = false;
        ProcessingData processing_data;
        {
            std::lock_guard<std::mutex> lock(processing_data_queue_mutex_);
            if (!processing_data_queue_.empty())
            {
                processing_data = std::move(processing_data_queue_.front());
                processing_data_queue_.pop();
                request_available = true;
            }
        }

        if (request_available)
        {
            switch (processing_data.type_)
            {
                case ProcessingData::Type::MESSAGE:
                    module_->processMessage(processing_data.local_channel_id_, processing_data.source_channel_, processing_data.message_);
                    break;
                case ProcessingData::Type::REQUEST:
                    module_->processRequest(processing_data.local_channel_id_, processing_data.source_channel_, processing_data.message_);
                    break;
                case ProcessingData::Type::RESPONSE:
                    module_->processResponse(processing_data.local_channel_id_, processing_data.source_channel_, processing_data.message_);
                    break;
            }
        }
        else
        {
            break;
        }
    }
    
    // Call module's cycle function
    module_->cycleImpl();
}



aergo::module::IModule* DllModuleWrapper::getModule()
{
    return module_.get();
}