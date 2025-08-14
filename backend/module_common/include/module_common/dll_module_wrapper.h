#pragma once

#include <cstdint>
#include <vector>
#include <queue>
#include <mutex>

#include "module_interface_.h"
#include "dll_interface_threads.h"
#include "periodic_thread.h"
#include "base_module.h"

namespace aergo::module::dll
{
    class DllModuleWrapper : public thread::PeriodicThread, public IDllModule
    {
    public:
        /// @param thread_sleep_ms sleep time for thread cycle, use 0 for no sleep (sleep handled by module).
        DllModuleWrapper(std::unique_ptr<BaseModule> module, uint32_t thread_sleep_ms);

        ~DllModuleWrapper() override = default;

        /// @brief Start the background thread.
        /// @param timeout_ms Wait up to "timeout_ms" milliseconds for the thread to start.
        /// @return true if started within timeout_ms. false on fail to start / timeout. Thread may exist if false.
        bool threadStart(uint32_t timeout_ms) noexcept override;

        /// @brief Stop and join the background thread.
        /// @return true if the thread was running, stopped within "timeout_ms" milliseconds and joined. false otherwise. 
        bool threadStop(uint32_t timeout_ms) noexcept override;

        /// @brief Process a message that came to subscribed channel "subscribe_consumer_id" from module "module_id".
        /// @param subscribe_consumer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source publish channel (module and channel ID)
        void processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;

        /// @brief Process request that came for response producer channel "response_producer_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param response_producer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source request channel (module and channel ID)
        void processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;

        /// @brief Process response that came from request consumer channel "request_consumer_id" from module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param request_consumer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source response channel (module and channel ID)
        void processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept override;

        BaseModule* getBaseModule();

    protected:
        void _threadInit() override final;

        void _threadCycle() override final;

        void _threadDeinit() override final;

    private:
        struct ProcessingData
        {
            enum class Type { MESSAGE, REQUEST, RESPONSE };

            Type type_;
            uint32_t local_channel_id_;
            ChannelIdentifier source_channel_;

            message::MessageHeader message_;
            std::vector<uint8_t> data_;
            std::vector<message::SharedDataBlob> blobs_;
        };

        void pushProcessingData(ProcessingData::Type type, uint32_t local_channel_id, ChannelIdentifier source_channel, message::MessageHeader message);

        std::mutex processing_data_queue_mutex_;
        std::queue<ProcessingData> processing_data_queue_;

        std::unique_ptr<BaseModule> module_;
    };
}