#pragma once

#include <stdint.h>
#include <vector>
#include <queue>
#include <mutex>
#include <chrono>
#include <memory>

#include "module_interface.h"
#include "periodic_thread.h"
#include "module_interface_threads.h"

namespace aergo::module
{
    class ModuleWrapper : public IModule, public thread::PeriodicThread
    {
    public:
        /// @param core reference to functions of the core (sending messages and allocating memory)
        /// @param channel_map_info ids of modules bound to subscribe and request channels, so the module knows what is on its input
        /// @param logger object for logging messages from the core
        /// @param module_id unique ID of this module received from the core
        /// @param thread_sleep_ms sleep time for thread cycle, use 0 for no sleep (sleep handled by module).
        ModuleWrapper(ICore* core, InputChannelMapInfo channel_map_info, const logging::ILogger* logger, uint64_t module_id, uint32_t thread_sleep_ms);

        ~ModuleWrapper() override;

        /// @brief Start the background thread.
        /// @param timeout_ms Wait up to "timeout_ms" milliseconds for the thread to start.
        /// @return true if started within timeout_ms. false on fail to start / timeout. Thread may exist if false.
        bool threadStart(uint32_t timeout_ms) override final;

        /// @brief Stop and join the background thread.
        /// @return true if the thread was running, stopped within "timeout_ms" milliseconds and joined. false otherwise. 
        bool threadStop(uint32_t timeout_ms) override final;

        /// @brief Process a message that came to subscribed channel "subscribe_consumer_id" from module "module_id".
        /// @param module_id there may be multiple consumers in one channel, they can be differentiated by "module_id"
        void processMessage(uint64_t subscribe_consumer_id, uint64_t module_id, message::MessageHeader message) override final;

        /// @brief Process request that came for response producer channel "response_producer_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        void processRequest(uint64_t response_producer_id, message::MessageHeader message) override final;

        /// @brief Process response that came from request consumer channel "request_consumer_id" from module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param module_id there may be multiple consumers in one channel, they can be differentiated by "module_id"
        void processResponse(uint64_t request_consumer_id, uint64_t module_id, message::MessageHeader message) override final;

    protected:
        /// @brief Log message of specific type (info, warning, error).
        void log(logging::LogType type, const char* message);

        /// @brief get mapped module IDs for a subscribe channel
        InputChannelMapInfo::IndividualChannelInfo getSubscribeChannelInfo(uint32_t channel_id);

        /// @brief get mapped module IDs for a request channel
        InputChannelMapInfo::IndividualChannelInfo getRequestChannelInfo(uint32_t channel_id);

        /// @brief Create dynamic allocator for shared data (to avoid copying large data). Each allocate call creates new memory.
        std::unique_ptr<Allocator> createDynamicAllocator();

        /// @brief Create buffered allocator for shared data (to avoid copying large data). Allocation happens on a buffer.
        /// Memory is pre-allocated. Allocation can fail if all buffer space is used.
        /// @param slot_size_bytes Fixed allocation size in bytes.
        /// @param number_of_slots Number of "size_bytes" sized slots.
        std::unique_ptr<Allocator> createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots);

        /// @brief Publish message to channel "publish_producer_id".
        void sendMessage(uint64_t publish_producer_id, message::MessageHeader message);

        /// @brief Send response to channel "response_producer_id". 
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param request_id ID of the request for which is the response
        void sendResponse(uint64_t response_producer_id, uint64_t request_id, message::MessageHeader message);

        /// @brief Send request to channel "request_consumer_id" to module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param module_id there may be multiple consumers in one channel, they can be differentiated by "module_id"
        uint64_t sendRequest(uint64_t request_consumer_id, uint64_t module_id, message::MessageHeader message);

        /// @brief Process a message that came to subscribed channel "subscribe_consumer_id" from module "module_id".
        /// @param module_id there may be multiple consumers in one channel, they can be differentiated by "module_id"
        virtual void processMessageImpl(uint64_t subscribe_consumer_id, uint64_t module_id, message::MessageHeader message) = 0;

        /// @brief Process request that came for response producer channel "response_producer_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        virtual void processRequestImpl(uint64_t response_producer_id, message::MessageHeader message) = 0;

        /// @brief Process response that came from request consumer channel "request_consumer_id" from module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param module_id there may be multiple consumers in one channel, they can be differentiated by "module_id"
        virtual void processResponseImpl(uint64_t request_consumer_id, uint64_t module_id, message::MessageHeader message) = 0;

        /// @brief Cycle method of the module. Called in each period after all messages/request/responses are handled. 
        /// If cycleImpl contains sleep, consider disabling sleep in ModuleWrapper by passing thread_sleep_ms = 0 in ModuleWrapper's constructor.
        virtual void cycleImpl() = 0;

        void _threadInit() override final;

        void _threadCycle() override final;

        void _threadDeinit() override final;

        inline uint64_t nowNs() noexcept
        {
            using namespace std::chrono;
            return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
        }


    private:
        struct ProcessingData
        {
            enum class Type { MESSAGE, REQUEST, RESPONSE };

            Type type_;
            uint64_t source_id_;
            uint64_t module_id_;

            message::MessageHeader message_;
            std::vector<uint8_t> data_;
            std::vector<message::SharedDataBlob> blobs_;
        };

        void pushProcessingData(ProcessingData::Type type, uint64_t source_id, uint64_t module_id, message::MessageHeader message);

        std::mutex processing_data_queue_mutex_;
        std::queue<ProcessingData> processing_data_queue_;


        uint64_t request_id_;


        const uint64_t module_id_;                                      // ID of this module
        std::vector<std::vector<uint64_t>> subscribe_consumer_info_;    // module IDs for each subscribe channel
        std::vector<std::vector<uint64_t>> request_consumer_info_;      // module IDs for each request channel
        ICore* core_;                                                   // reference to the core object
        const logging::ILogger* logger_;                                // reference to a logger object
    };
}