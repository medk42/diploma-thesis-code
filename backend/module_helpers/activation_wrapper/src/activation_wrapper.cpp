#include "module_helpers/activation_wrapper/activation_wrapper.h"



using namespace aergo::module::helpers::activation_wrapper;



void ActivationWrapper::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{

}



void ActivationWrapper::processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{

}



void ActivationWrapper::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{

}



aergo::module::helpers::helper_base::RegisterInfo ActivationWrapper::initializeAndRegister(aergo::module::BaseModule* base_module, ModuleInfo module_info) noexcept
{

}