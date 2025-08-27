#pragma once

#include "module_interface_.h"

namespace aergo::module::dll
{
    class IDllModule
    {
    public:
        virtual ~IDllModule() = default;

        /// @brief Process a message that came to subscribed channel "subscribe_consumer_id" from module "module_id".
        /// @param subscribe_consumer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source publish channel (module and channel ID)
        virtual void processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept = 0;

        /// @brief Process request that came for response producer channel "response_producer_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param response_producer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source request channel (module and channel ID)
        virtual void processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept = 0;

        /// @brief Process response that came from request consumer channel "request_consumer_id" from module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param request_consumer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source response channel (module and channel ID)
        virtual void processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept = 0;

        /// @brief Start the background thread.
        /// @param timeout_ms Wait up to "timeout_ms" milliseconds for the thread to start.
        /// @return true if started within timeout_ms. false on fail to start / timeout. Thread may exist if false.
        virtual bool threadStart(uint32_t timeout_ms) noexcept = 0;

        /// @brief Stop and join the background thread.
        /// @return true if the thread was running, stopped within "timeout_ms" milliseconds and joined. false otherwise. 
        virtual bool threadStop(uint32_t timeout_ms) noexcept = 0;
    };
}