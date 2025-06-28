#include "module_wrapper.h"

#include <optional>

#define TRY_CATCH_LOG(__method, __code) try {\
    __code \
} catch(const std::exception& e) { \
    logger_->log(logging::LogType::ERROR, (std::string("Module implementation of function \"") + __method + "\" raised the following exception: " + e.what()).c_str());\
}

using namespace aergo::module;



ModuleWrapper::ModuleWrapper(ICore* core, InputChannelMapInfo channel_map_info, const logging::ILogger* logger, uint64_t module_id, uint32_t thread_sleep_ms)
: PeriodicThread(thread_sleep_ms), module_id_(module_id), core_(core), logger_(logger), request_id_(0)
{
    // subscribe side
    subscribe_consumer_info_.reserve(channel_map_info.subscribe_consumer_info_count_);
    for (uint32_t i = 0; i < channel_map_info.subscribe_consumer_info_count_; ++i)
    {
        auto& info = channel_map_info.subscribe_consumer_info_[i];
        subscribe_consumer_info_.emplace_back(
            info.module_ids_,
            info.module_ids_ + info.module_ids_count_
        );
    }

    // request side
    request_consumer_info_.reserve(channel_map_info.request_consumer_info_count_);
    for (uint32_t i = 0; i < channel_map_info.request_consumer_info_count_; ++i)
    {
        auto& info = channel_map_info.request_consumer_info_[i];
        request_consumer_info_.emplace_back(
            info.module_ids_,
            info.module_ids_ + info.module_ids_count_
        );
    }
}

ModuleWrapper::~ModuleWrapper() {}



bool ModuleWrapper::threadStart(uint32_t timeout_ms)
{
    return _threadStart(timeout_ms);
}



bool ModuleWrapper::threadStop(uint32_t timeout_ms)
{
    return _threadStop(timeout_ms);
}



void ModuleWrapper::sendMessage(uint64_t publish_producer_id, message::MessageHeader message)
{
    message.timestamp_ns_ = nowNs();
    core_->sendMessage(module_id_, publish_producer_id, message);
}



void ModuleWrapper::sendResponse(uint64_t response_producer_id, message::MessageHeader message)
{
    message.timestamp_ns_ = nowNs();
    core_->sendResponse(module_id_, response_producer_id, message);
}



uint64_t ModuleWrapper::sendRequest(uint64_t request_consumer_id, uint64_t module_id, message::MessageHeader message)
{
    message.id_ = request_id_++;
    message.timestamp_ns_ = nowNs();
    core_->sendRequest(module_id_, request_consumer_id, module_id, message);
    return message.id_;
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
                    TRY_CATCH_LOG("processMessageImpl", processMessageImpl(processing_data.source_id_, processing_data.module_id_, processing_data.message_);)
                    break;
                case ProcessingData::Type::REQUEST:
                    TRY_CATCH_LOG("processRequestImpl", processRequestImpl(processing_data.source_id_, processing_data.message_);)
                    break;
                case ProcessingData::Type::RESPONSE:
                    TRY_CATCH_LOG("processResponseImpl", processResponseImpl(processing_data.source_id_, processing_data.module_id_, processing_data.message_);)
                    break;
            }
        }
        else
        {
            break;
        }
    }
    
    // Call module's cycle function
    TRY_CATCH_LOG("cycleImpl", cycleImpl();)
}



void ModuleWrapper::log(logging::LogType type, const char* message)
{
    logger_->log(type, message);
}



InputChannelMapInfo::IndividualChannelInfo ModuleWrapper::getSubscribeChannelInfo(uint32_t channel_id)
{
    if (channel_id >= subscribe_consumer_info_.size())
    {
        return InputChannelMapInfo::IndividualChannelInfo {
            .module_ids_ = nullptr,
            .module_ids_count_ = 0
        };
    }

    return InputChannelMapInfo::IndividualChannelInfo {
        .module_ids_ = subscribe_consumer_info_[channel_id].data(),
        .module_ids_count_ = (uint32_t)subscribe_consumer_info_[channel_id].size()
    };
}



InputChannelMapInfo::IndividualChannelInfo ModuleWrapper::getRequestChannelInfo(uint32_t channel_id)
{
    if (channel_id >= request_consumer_info_.size())
    {
        return InputChannelMapInfo::IndividualChannelInfo {
            .module_ids_ = nullptr,
            .module_ids_count_ = 0
        };
    }

    return InputChannelMapInfo::IndividualChannelInfo {
        .module_ids_ = request_consumer_info_[channel_id].data(),
        .module_ids_count_ = (uint32_t)request_consumer_info_[channel_id].size()
    };
}



std::unique_ptr<Allocator> ModuleWrapper::createDynamicAllocator()
{
    IAllocatorCore* allocator = core_->createDynamicAllocator();
    return std::make_unique<Allocator>(core_, allocator);
}



std::unique_ptr<Allocator> ModuleWrapper::createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots)
{
    IAllocatorCore* allocator = core_->createBufferAllocator(slot_size_bytes, number_of_slots);
    return std::make_unique<Allocator>(core_, allocator);
}