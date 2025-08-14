#include "module_common/base_module.h"



using namespace aergo::module;



BaseModule::BaseModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, logging::ILogger* logger, uint64_t module_id)
: data_path_(data_path), core_(core), logger_(logger), module_id_(module_id), request_id_(0)
{
    // subscribe side
    subscribe_consumer_info_.reserve(channel_map_info.subscribe_consumer_info_count_);
    for (uint32_t i = 0; i < channel_map_info.subscribe_consumer_info_count_; ++i)
    {
        auto& info = channel_map_info.subscribe_consumer_info_[i];
        subscribe_consumer_info_.emplace_back(
            info.channel_identifier_,
            info.channel_identifier_ + info.channel_identifier_count_
        );
    }

    // request side
    request_consumer_info_.reserve(channel_map_info.request_consumer_info_count_);
    for (uint32_t i = 0; i < channel_map_info.request_consumer_info_count_; ++i)
    {
        auto& info = channel_map_info.request_consumer_info_[i];
        request_consumer_info_.emplace_back(
            info.channel_identifier_,
            info.channel_identifier_ + info.channel_identifier_count_
        );
    }
}



void BaseModule::log(logging::LogType type, const char* message)
{
    logger_->log(type, message);
}



void BaseModule::sendMessage(uint32_t publish_producer_id, message::MessageHeader message)
{
    message.timestamp_ns_ = nowNs();
    
    core_->sendMessage(
        {
            .producer_module_id_ = module_id_, 
            .producer_channel_id_ = publish_producer_id
        }, 
        message
    );
}



void BaseModule::sendResponse(uint32_t response_producer_id, ChannelIdentifier target_channel, uint64_t request_id, message::MessageHeader message)
{
    message.id_ = request_id;
    message.timestamp_ns_ = nowNs();

    core_->sendResponse(
        {
            .producer_module_id_ = module_id_, 
            .producer_channel_id_ = response_producer_id
        }, 
        target_channel,
        message
    );
}



uint64_t BaseModule::sendRequest(uint32_t request_consumer_id, ChannelIdentifier target_channel, message::MessageHeader message)
{
    message.id_ = request_id_++;
    message.timestamp_ns_ = nowNs();

    core_->sendRequest(
        {
            .producer_module_id_ = module_id_, 
            .producer_channel_id_ = request_consumer_id
        }, 
        target_channel, 
        message
    );

    return message.id_;
}



BaseModule::AllocatorPtr BaseModule::createDynamicAllocator()
{
    return std::unique_ptr<aergo::module::IAllocator, std::function<void(IAllocator*)>>(
        core_->createDynamicAllocator(),
        [this](IAllocator* allocator_ref) { core_->deleteAllocator(allocator_ref); }
    );
}



BaseModule::AllocatorPtr BaseModule::createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots)
{
    return std::unique_ptr<aergo::module::IAllocator, std::function<void(IAllocator*)>>(
        core_->createBufferAllocator(slot_size_bytes, number_of_slots),
        [this](IAllocator* allocator_ref) { core_->deleteAllocator(allocator_ref); }
    );
}



InputChannelMapInfo::IndividualChannelInfo BaseModule::getSubscribeChannelInfo(uint32_t channel_id)
{
    if (channel_id >= subscribe_consumer_info_.size())
    {
        return InputChannelMapInfo::IndividualChannelInfo {
            .channel_identifier_ = nullptr,
            .channel_identifier_count_ = 0
        };
    }

    return InputChannelMapInfo::IndividualChannelInfo {
        .channel_identifier_ = subscribe_consumer_info_[channel_id].data(),
        .channel_identifier_count_ = (uint32_t)subscribe_consumer_info_[channel_id].size()
    };
}



InputChannelMapInfo::IndividualChannelInfo BaseModule::getRequestChannelInfo(uint32_t channel_id)
{
    if (channel_id >= request_consumer_info_.size())
    {
        return InputChannelMapInfo::IndividualChannelInfo {
            .channel_identifier_ = nullptr,
            .channel_identifier_count_ = 0
        };
    }

    return InputChannelMapInfo::IndividualChannelInfo {
        .channel_identifier_ = request_consumer_info_[channel_id].data(),
        .channel_identifier_count_ = (uint32_t)request_consumer_info_[channel_id].size()
    };
}