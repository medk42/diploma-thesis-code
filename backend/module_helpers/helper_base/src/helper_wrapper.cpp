#include "module_helpers/helper_base/helper_wrapper.h"

using namespace aergo::module::helpers::helper_base;

HelperWrapper::HelperWrapper(std::unique_ptr<aergo::module::BaseModule> base_module, std::vector<std::unique_ptr<IHelperBase>> helper_wrappers)
: base_module_(std::move(base_module)), helper_wrappers_(std::move(helper_wrappers)) {}



bool HelperWrapper::initialize(aergo::module::ModuleInfo module_info)
{
    if (base_module_.get() == nullptr)
    {
        return false;
    }

    for (size_t i = 0; i < helper_wrappers_.size(); ++i)
    {
        RegisterInfo register_info = helper_wrappers_[i]->initializeAndRegister(base_module_.get(), module_info);

        for (auto id : register_info.process_message_ids_)
        {
            if (process_message_map_.contains(id))
            {
                return false;
            }
            process_message_map_[id] = i;
        }

        for (auto id : register_info.process_request_ids_)
        {
            if (process_request_map_.contains(id))
            {
                return false;
            }
            process_request_map_[id] = i;
        }

        for (auto id : register_info.process_response_ids_)
        {
            if (process_response_map_.contains(id))
            {
                return false;
            }
            process_response_map_[id] = i;
        }
    }

    return true;
}



void HelperWrapper::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    auto it = process_message_map_.find(subscribe_consumer_id);
    if (it != process_message_map_.end())
    {
        size_t wrapper_id = it->second;
        helper_wrappers_[wrapper_id]->processMessage(subscribe_consumer_id, source_channel, message);
    }
    else
    {
        base_module_->processMessage(subscribe_consumer_id, source_channel, message);
    }
}



void HelperWrapper::processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    auto it = process_request_map_.find(response_producer_id);
    if (it != process_request_map_.end())
    {
        size_t wrapper_id = it->second;
        helper_wrappers_[wrapper_id]->processRequest(response_producer_id, source_channel, message);
    }
    else
    {
        base_module_->processRequest(response_producer_id, source_channel, message);
    }
}



void HelperWrapper::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    auto it = process_response_map_.find(request_consumer_id);
    if (it != process_response_map_.end())
    {
        size_t wrapper_id = it->second;
        helper_wrappers_[wrapper_id]->processResponse(request_consumer_id, source_channel, message);
    }
    else
    {
        base_module_->processResponse(request_consumer_id, source_channel, message);
    }
}



void HelperWrapper::cycleImpl() noexcept
{
    base_module_->cycleImpl();
}



void* HelperWrapper::query_capability(const std::type_info& id) noexcept
{
    return base_module_->query_capability(id);
}