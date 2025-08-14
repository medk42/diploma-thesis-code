#include "module_a/module_a.h"
#include "module_a/module_a_data_types.h"

#include <sstream>

using namespace aergo::demo_modules_1::module_a;
using namespace aergo::module;

#define LOG(__type, __message) { std::stringstream ss; ss << __message; std::string log_message = ss.str(); log(__type, log_message.c_str()); }



ModuleA::ModuleA(const char* data_path, ICore* core, InputChannelMapInfo channel_map_info, const logging::ILogger* logger, uint64_t module_id)
: BaseModule(data_path, core, channel_map_info, logger, module_id), next_small_message_(0), next_large_message_(0),
small_message_counter_(0), gen_(), dist_(0, 255)
{
    large_fixed_allocator_ = createBufferAllocator(1000, 10);
    request_dynamic_allocator_ = createDynamicAllocator();

    if (large_fixed_allocator_ == nullptr || request_dynamic_allocator_ == nullptr)
    {
        logger->log(logging::LogType::ERROR, "Failed to create allocators.");
        throw std::exception("Failed to create allocators.");
    }
}



void ModuleA::processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{}



void ModuleA::processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{
    if (response_producer_id == 0)
    {
        if (message.data_len_ != sizeof(messages::request_response::LargeVariableReq))
        {
            LOG(logging::LogType::WARNING, "Unexpected message data length: " << message.data_len_ << "B (expected " << sizeof(messages::request_response::LargeVariableReq) << "B)")
            sendResponse(0, source_channel, message.id_, {.success_ = false});
            return;
        }
        messages::request_response::LargeVariableReq* request = (messages::request_response::LargeVariableReq*)message.data_;

        if (message.blob_count_ != 0)
        {
            LOG(logging::LogType::WARNING, "Unexpected blob count: " << message.blob_count_ << " (expected 0)")
        }

        uint64_t now_ns = nowNs();
        LOG(logging::LogType::INFO, "Received request ID " << message.id_ << ", sent " << message.timestamp_ns_ << "ns, rcv " << now_ns << "ns, diff " << (now_ns - message.timestamp_ns_))



        messages::request_response::LargeVariableResp response;

        message::SharedDataBlob data_blob = request_dynamic_allocator_->allocate(request->requested_size_);
        if (!data_blob.valid())
        {
            log(logging::LogType::WARNING, "Allocated data blob is not valid!");
            sendResponse(0, source_channel, message.id_, {.success_ = false});
            return;
        }
        if (data_blob.size() != request->requested_size_)
        {
            LOG(logging::LogType::WARNING, "Data blob size is not " << request->requested_size_ << "B (actual size = " << data_blob.size() << "B)")
            sendResponse(0, source_channel, message.id_, {.success_ = false});
            return;
        }

        uint8_t* raw_data = data_blob.data();

        uint8_t counter = request->counter_start_;
        for (int i = 0; i < request->requested_size_; ++i, ++counter)
        {
            raw_data[i] = counter;
        }

        message::MessageHeader resp_message {
            .data_ = (uint8_t*)(&response),
            .data_len_ = sizeof(response),
            .blobs_ = &data_blob,
            .blob_count_ = 1,
            .success_ = true
        };
        sendResponse(0, source_channel, message.id_, resp_message);
    }
    else
    {
        LOG(logging::LogType::WARNING, "Unknown request source: " << response_producer_id)
        sendResponse(response_producer_id, source_channel, message.id_, {.success_ = false});
        return;
    }
}



void ModuleA::processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept
{}



void ModuleA::cycleImpl()
{
    uint64_t time_ms = nowMs();
    if (time_ms > next_small_message_)
    {
        next_small_message_ = time_ms + 20;

        messages::publish_consume::SmallMessage small_message
        {
            .counter1_ = small_message_counter_,
            .counter2_ = (uint8_t)small_message_counter_
        };
        ++small_message_counter_;

        message::MessageHeader message {
            .data_ = (uint8_t*)(&small_message),
            .data_len_ = sizeof(small_message),
            .blobs_ = nullptr,
            .blob_count_ = 0
        };
        sendMessage(0, message);
    }
    if (time_ms > next_large_message_)
    {
        next_large_message_ = time_ms + 50;

        messages::publish_consume::LargeMessage large_message
        {
            .counter_start_ = (uint8_t)dist_(gen_)
        };

        message::SharedDataBlob data_blob = large_fixed_allocator_->allocate(0);
        if (!data_blob.valid())
        {
            log(logging::LogType::WARNING, "Allocated data blob is not valid!");
            return;
        }
        if (data_blob.size() != 1000)
        {
            LOG(logging::LogType::WARNING, "Data blob size is not 1000B (actual size = " << data_blob.size() << "B)")
            return;
        }

        uint8_t* raw_data = data_blob.data();

        uint8_t counter = large_message.counter_start_;
        for (int i = 0; i < 1000; ++i, ++counter)
        {
            raw_data[i] = counter;
        }

        message::MessageHeader message {
            .data_ = (uint8_t*)(&large_message),
            .data_len_ = sizeof(large_message),
            .blobs_ = &data_blob,
            .blob_count_ = 1
        };
        sendMessage(1, message);
    }
}