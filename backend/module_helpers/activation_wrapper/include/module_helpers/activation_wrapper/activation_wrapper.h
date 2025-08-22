#pragma once

#include "module_common/module_interface_.h"
#include "module_helpers/helper_base/helper_base.h"
#include "activable_module.h"

#include <memory>

namespace aergo::module::helpers::activation_wrapper
{
    class ActivationWrapper : public aergo::module::helpers::helper_base::IHelperBase
    {
    public:
        /// @brief Process a message that came to subscribed channel "subscribe_consumer_id" from module "module_id".
        /// @param subscribe_consumer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source publish channel (module and channel ID)
        virtual void processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;

        /// @brief Process request that came for response producer channel "response_producer_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param response_producer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source request channel (module and channel ID)
        virtual void processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;

        /// @brief Process response that came from request consumer channel "request_consumer_id" from module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param request_consumer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source response channel (module and channel ID)
        virtual void processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;

        virtual aergo::module::helpers::helper_base::RegisterInfo initializeAndRegister(aergo::module::BaseModule* base_module, aergo::module::ModuleInfo module_info) noexcept override;

    private:
        IActivableModule* activable_module_ref_;
    };
}