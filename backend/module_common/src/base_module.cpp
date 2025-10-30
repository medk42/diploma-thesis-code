#include "module_common/base_module.h"



using namespace aergo::module;



BaseModule::BaseModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, const logging::ILogger* logger, uint64_t module_id, const ModuleInfo* module_info)
: core_(core), logger_(logger), module_id_(module_id), request_id_(0), module_info_(module_info)
{
    if (data_path)
    {
        data_path_ = std::string(data_path);
    }
    else
    {
        data_path_ = std::string();
    }

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



void BaseModule::log(logging::LogType type, std::string message)
{
    logger_->log(type, message.c_str());
}



void BaseModule::sendMessage(uint32_t publish_producer_id, message::MessageHeader message)
{
    message.timestamp_ns_ = nowNs();
    message.success_ = true; // only response to request can have success flag false
    
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
    message.success_ = true; // only response to request can have success flag false

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



const std::string& BaseModule::getDataPath()
{
    return data_path_;
}



bool BaseModule::getSubscribeChannelByName(const char* name, uint32_t& out_channel_id) noexcept
{
    if (!module_info_ || !name)
    {
        return false;
    }

    for (uint32_t subscribe_consumer_id = 0; subscribe_consumer_id < module_info_->subscribe_consumer_count_; ++subscribe_consumer_id)
    {
        if (module_info_->subscribe_consumers_[subscribe_consumer_id].channel_type_identifier_ == nullptr)
        {
            continue;
        }
        
        std::string channel_type_identifier = module_info_->subscribe_consumers_[subscribe_consumer_id].channel_type_identifier_;
        if (channel_type_identifier == name)
        {
            out_channel_id = subscribe_consumer_id;
            return true;
        }
    }

    return false;
}



bool BaseModule::getRequestChannelByName(const char* name, uint32_t& out_channel_id) noexcept
{
    if (!module_info_ || !name)
    {
        return false;
    }

    for (uint32_t request_consumer_id = 0; request_consumer_id < module_info_->request_consumer_count_; ++request_consumer_id)
    {
        if (module_info_->request_consumers_[request_consumer_id].channel_type_identifier_ == nullptr)
        {
            continue;
        }
        
        std::string channel_type_identifier = module_info_->request_consumers_[request_consumer_id].channel_type_identifier_;
        if (channel_type_identifier == name)
        {
            out_channel_id = request_consumer_id;
            return true;
        }
    }

    return false;
}



bool BaseModule::getPublishChannelByName(const char* name, uint32_t& out_channel_id) noexcept
{
    if (!module_info_ || !name)
    {
        return false;
    }

    for (uint32_t publish_producer_id = 0; publish_producer_id < module_info_->publish_producer_count_; ++publish_producer_id)
    {
        if (module_info_->publish_producers_[publish_producer_id].channel_type_identifier_ == nullptr)
        {
            continue;
        }
        
        std::string channel_type_identifier = module_info_->publish_producers_[publish_producer_id].channel_type_identifier_;
        if (channel_type_identifier == name)
        {
            out_channel_id = publish_producer_id;
            return true;
        }
    }

    return false;
}



bool BaseModule::getResponseChannelByName(const char* name, uint32_t& out_channel_id) noexcept
{
    if (!module_info_ || !name)
    {
        return false;
    }

    for (uint32_t response_producer_id = 0; response_producer_id < module_info_->response_producer_count_; ++response_producer_id)
    {
        if (module_info_->response_producers_[response_producer_id].channel_type_identifier_ == nullptr)
        {
            continue;
        }
        
        std::string channel_type_identifier = module_info_->response_producers_[response_producer_id].channel_type_identifier_;
        if (channel_type_identifier == name)
        {
            out_channel_id = response_producer_id;
            return true;
        }
    }

    return false;
}