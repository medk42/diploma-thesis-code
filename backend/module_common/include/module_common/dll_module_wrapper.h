#pragma once

#include <cstdint>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

#include "module_interface_.h"
#include "dll_interface_threads.h"
#include "base_module.h"
#include "dll_module_metrics.h"

namespace aergo::module::dll
{
    class DllModuleWrapper : public IDllModule
    {
    public:
        /// @brief module must be non-nullptr and valid (check IModule::valid()), module_info must be non-nullptr.
        DllModuleWrapper(std::unique_ptr<aergo::module::IModule> module, const aergo::module::ModuleInfo* module_info, const aergo::module::logging::ILogger* logger);

        ~DllModuleWrapper() override = default;

        /// @brief Start the worker threads.
        /// @param timeout_ms Wait up to "timeout_ms" milliseconds for the threads to start.
        /// @return true if started within timeout_ms. false on fail to start / timeout. Thread may exist if false.
        bool threadStart(uint32_t timeout_ms) noexcept override;

        /// @brief Stop and join the worker threads.
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

        aergo::module::IModule* getModule();

    private:
        struct ProcessingData
        {
            aergo::module::IModule::ProcessingType processing_type_;
            uint32_t local_channel_id_;
            ChannelIdentifier source_channel_;

            message::MessageHeader message_;
            std::vector<uint8_t> data_;
            std::vector<message::SharedDataBlob> blobs_;
        };

        void pushProcessingData(aergo::module::IModule::ProcessingType type, uint32_t local_channel_id, ChannelIdentifier source_channel, message::MessageHeader message);

        void regularWorkerThreadFunc();
        void prioritizedWorkerThreadFunc();

        bool regularQueuesEmpty(); // true if all regular queues are empty
        bool prioritizedQueuesEmpty(); // true if all prioritized queues are empty

        bool popRegularProcessingData(ProcessingData& data); // pops data from any non-empty regular queue, returns false if all queues are empty
        bool popPrioritizedProcessingData(ProcessingData& data); // pops data from any non-empty prioritized queue, returns false if all queues are empty

        int64_t nowMs();

        std::mutex mutex_;

        std::vector<std::queue<ProcessingData>> prioritized_queues_; // one queue per prioritized message/request/response channel
        std::vector<std::queue<ProcessingData>> regular_queues_;     // one queue per regular message/request/response channel

        std::vector<bool> is_queue_prioritized_;                     // true if channel is prioritized, false otherwise
        std::vector<uint16_t> queue_capacities_;                      // maximum number of waiting messages/requests/responses in the queue (beyond that, new messages/requests/responses are dropped)

        uint32_t next_prioritized_queue_idx_ = 0;                    // index of next prioritized queue to check for data (round-robin)
        uint32_t next_regular_queue_idx_ = 0;                        // index of next regular queue to check for data (round-robin)

        uint32_t messages_channel_count_;                            // number of channels for receiving messages
        uint32_t requests_channel_count_;                            // number of channels for receiving requests
        uint32_t responses_channel_count_;                           // number of channels for receiving responses

        
        std::vector<std::thread> regular_worker_threads_;
        std::vector<std::thread> prioritized_worker_threads_;

        std::atomic<uint8_t> regular_worker_running_count_{0};
        std::atomic<uint8_t> prioritized_worker_running_count_{0};

        std::condition_variable regular_worker_cv_;
        std::condition_variable prioritized_worker_cv_;

        std::atomic<bool> stop_threads_{false};


        std::unique_ptr<aergo::module::IModule> module_;
        const aergo::module::ModuleInfo* module_info_;
        const aergo::module::logging::ILogger* logger_;

        Metrics metrics_;
    };
}