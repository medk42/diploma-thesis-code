#pragma once

#include "module_interface_.h"

#include <memory>
#include <functional>
#include <chrono>

namespace aergo::module
{
    class BaseModule : public IModule
    {
    public:
        BaseModule(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, const logging::ILogger* logger, uint64_t module_id);

        /// @brief Cycle method of the module. Called in each period after all messages/request/responses are handled. 
        /// If cycleImpl contains sleep, consider disabling sleep in ModuleWrapper by passing thread_sleep_ms = 0 in ModuleWrapper's constructor.
        virtual void cycleImpl() noexcept = 0;



    protected:
        using AllocatorPtr = std::unique_ptr<IAllocator, std::function<void(IAllocator*)>>;

        /// @brief Log message of specific type (info, warning, error).
        void log(logging::LogType type, const char* message);



        /// @brief Publish message to channel "publish_producer_id".
        /// @param publish_producer_id id of the channel to publish on
        void sendMessage(uint32_t publish_producer_id, message::MessageHeader message);

        /// @brief Send response to channel "response_producer_id". 
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param response_producer_id id of the channel to respond on
        /// @param target_channel identifies the target request channel (module and channel ID)
        void sendResponse(uint32_t response_producer_id, ChannelIdentifier target_channel, uint64_t request_id, message::MessageHeader message);

        /// @brief Send request to channel "request_consumer_id" to module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param request_consumer_id id of the channel to request on
        /// @param target_channel identifies the target response channel (module and channel ID)
        /// @return ID of the request (to match with response ID)
        uint64_t sendRequest(uint32_t request_consumer_id, ChannelIdentifier target_channel, message::MessageHeader message);



        /// @brief Create dynamic allocator for shared data (to avoid copying large data). Each allocate call creates new memory.
        /// @return New allocator or nullptr on failure.
        AllocatorPtr createDynamicAllocator();

        /// @brief Create buffered allocator for shared data (to avoid copying large data). Allocation happens on a buffer.
        /// Memory is pre-allocated. Allocation can fail if all buffer space is used.
        /// @param slot_size_bytes Fixed allocation size in bytes.
        /// @param number_of_slots Number of "size_bytes" sized slots.
        /// @return New allocator or nullptr on failure.
        AllocatorPtr createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots);



        /// @brief get mapped module IDs for a subscribe channel
        InputChannelMapInfo::IndividualChannelInfo getSubscribeChannelInfo(uint32_t channel_id);

        /// @brief get mapped module IDs for a request channel
        InputChannelMapInfo::IndividualChannelInfo getRequestChannelInfo(uint32_t channel_id);

        

        inline uint64_t nowNs() noexcept
        {
            using namespace std::chrono;
            return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
        }



    private:
        ICore* core_;
        const logging::ILogger* logger_;
        std::string data_path_;
        const uint64_t module_id_; 
        
        std::vector<std::vector<ChannelIdentifier>> subscribe_consumer_info_;    // module IDs for each subscribe channel
        std::vector<std::vector<ChannelIdentifier>> request_consumer_info_;      // module IDs for each request channel
        
        uint64_t request_id_;
    };
}