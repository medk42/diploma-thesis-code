#pragma once

#include <stdint.h>

#include "module_interface.h"

namespace aergo::module
{
    /// @brief Reference to shared data in the core. 
    class ISharedData
    {
    public:
        inline virtual ~ISharedData() {}

        /// @brief Is data valid. Do not call other functions/methods if data is invalid.
        virtual bool valid() noexcept = 0;

        /// @brief Return pointer to the data. Behavior not specified when invalid.
        virtual uint8_t* data() noexcept = 0;

        /// @brief Size of the data. Behavior not specified when invalid.
        virtual uint64_t size() noexcept = 0;
    };

    /// @brief Reference to shared data allocator in the core.
    class IAllocatorCore
    {
    public:
        inline virtual ~IAllocatorCore() {}

        /// @brief Allocate "number_of_bytes" bytes of shared memory. If the allocator has fixed byte size, "number_of_bytes" parameter is ignored.
        /// @return pointer to valid allocated data or nullptr if data couldn't have been allocated
        virtual ISharedData* allocate(uint64_t number_of_bytes) noexcept = 0;

        /// @brief Add owner for shared data object. Object removed when owners drop to zero.
        virtual void addOwner(ISharedData* data) noexcept = 0;
        
        /// @brief Remove owner from shared data object. Object removed when owners drop to zero.
        virtual void removeOwner(ISharedData* data) noexcept = 0;
    };

    namespace logging {
        enum class LogType { INFO, WARNING, ERROR };

        class ILogger
        {
        public:
            inline virtual ~ILogger() {}

            /// @brief Log message of specific type (info, warning, error).
            virtual void log(LogType type, const char* message) const noexcept = 0;
        };
    };

    /// @brief Reference to the core.
    class ICore
    {
    public:
        inline virtual ~ICore() {}

        /// @brief Publish message to channel "publish_producer_id".
        /// @param source_module_id id of the sending module
        virtual void sendMessage(uint64_t source_module_id, uint64_t publish_producer_id, message::MessageHeader message) noexcept = 0;

        /// @brief Send response to channel "response_producer_id". 
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param source_module_id id of the sending module
        virtual void sendResponse(uint64_t source_module_id, uint64_t response_producer_id, message::MessageHeader message) noexcept = 0;

        /// @brief Send request to channel "request_consumer_id" to module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param source_module_id id of the sending module
        /// @param target_module_id there may be multiple consumers in one channel, they can be differentiated by "target_module_id"
        virtual void sendRequest(uint64_t source_module_id, uint64_t request_consumer_id, uint64_t target_module_id, message::MessageHeader message) noexcept = 0;

        /// @brief Create dynamic allocator for shared data (to avoid copying large data). Each allocate call creates new memory.
        /// @return New allocator or nullptr on failure.
        virtual IAllocatorCore* createDynamicAllocator() noexcept = 0;

        /// @brief Create buffered allocator for shared data (to avoid copying large data). Allocation happens on a buffer.
        /// Memory is pre-allocated. Allocation can fail if all buffer space is used.
        /// @param slot_size_bytes Fixed allocation size in bytes.
        /// @param number_of_slots Number of "size_bytes" sized slots.
        /// @return New allocator or nullptr on failure.
        virtual IAllocatorCore* createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots) noexcept = 0;

        /// @brief Delete previously created allocator.
        virtual void deleteAllocator(IAllocatorCore* allocator) noexcept = 0;
    };

    class IModule
    {
    public:
        inline virtual ~IModule() {}

        /// @brief Start the background thread.
        /// @param timeout_ms Wait up to "timeout_ms" milliseconds for the thread to start.
        /// @return true if started within timeout_ms. false on fail to start / timeout. Thread may exist if false.
        virtual bool threadStart(uint32_t timeout_ms) noexcept = 0;

        /// @brief Stop and join the background thread.
        /// @return true if the thread was running, stopped within "timeout_ms" milliseconds and joined. false otherwise. 
        virtual bool threadStop(uint32_t timeout_ms) noexcept = 0;

        /// @brief Process a message that came to subscribed channel "subscribe_consumer_id" from module "module_id".
        /// @param module_id there may be multiple consumers in one channel, they can be differentiated by "module_id"
        virtual void processMessage(uint64_t subscribe_consumer_id, uint64_t module_id, message::MessageHeader message) noexcept = 0;

        /// @brief Process request that came for response producer channel "response_producer_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        virtual void processRequest(uint64_t response_producer_id, message::MessageHeader message) noexcept = 0;

        /// @brief Process response that came from request consumer channel "request_consumer_id" from module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param module_id there may be multiple consumers in one channel, they can be differentiated by "module_id"
        virtual void processResponse(uint64_t request_consumer_id, uint64_t module_id, message::MessageHeader message) noexcept = 0;
    };
}