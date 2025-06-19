#include "module_wrapper.h"

using namespace aergo::module;



ModuleWrapper::ModuleWrapper(const logging::ILogger* logger, uint32_t thread_sleep_ms)
: PeriodicThread(thread_sleep_ms), logger_(logger) {}

ModuleWrapper::~ModuleWrapper() {}



bool ModuleWrapper::threadStart(uint32_t timeout_ms)
{
    return _threadStart(timeout_ms);
}



bool ModuleWrapper::threadStop(uint32_t timeout_ms)
{
    return _threadStop(timeout_ms);
}



void ModuleWrapper::processMessage(uint64_t subscribe_consumer_id, uint64_t module_id, message::MessageHeader message)
{
    pushProcessingData(ProcessingData::Type::MESSAGE, subscribe_consumer_id, module_id, message);
}



void ModuleWrapper::processRequest(uint64_t response_producer_id, message::MessageHeader message)
{
    pushProcessingData(ProcessingData::Type::REQUEST, response_producer_id, 0, message);
}



void ModuleWrapper::processResponse(uint64_t request_consumer_id, uint64_t module_id, message::MessageHeader message)
{
    pushProcessingData(ProcessingData::Type::RESPONSE, request_consumer_id, module_id, message);
}



void ModuleWrapper::pushProcessingData(ProcessingData::Type type, uint64_t source_id, uint64_t module_id, message::MessageHeader message)
{
    std::vector<uint8_t> data(message.data_, message.data_ + message.data_len_);
    message.data_ = data.data();

    std::vector<message::SharedDataBlob> blobs(message.blobs_, message.blobs_ + message.blob_count_);
    message.blobs_ = blobs.data();

    ProcessingData processing_data {
        .type_ = type,
        .source_id_ = source_id,
        .module_id_ = module_id,
        .message_ = message,
        .data_ = std::move(data),
        .blobs_ = std::move(blobs)
    };

    {
        std::lock_guard<std::mutex> lock(processing_data_queue_mutex_);
        processing_data_queue_.push(std::move(processing_data));
    }
}



void ModuleWrapper::_threadInit() {}

void ModuleWrapper::_threadDeinit() {}



void ModuleWrapper::_threadCycle()
{

}



void ModuleWrapper::log(logging::LogType type, const char* message)
{
    logger_->log(type, message);
}