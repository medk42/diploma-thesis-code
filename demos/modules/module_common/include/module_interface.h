#pragma once

#include "stdint.h"

namespace aergo::module
{
    namespace communication_channel
    {
        struct Producer
        {
            const char* channel_name_;  // name of the communication channel
        };

        // publishes data, binds to SubscribeConsumer
        struct PublishProducer : public Producer {};

        // provides response for requests, binds to RequestConsumer
        struct ResponseProducer : public Producer {};

        struct Consumer
        {
            enum class Count { 
                SINGLE,   // single consumer
                RANGE,    // range of consumers [min, max]
                AUTO_ALL  // automatically bind subscribe/request to all available producers
            };
            
            Count count_;                // requested count of consumers
            uint64_t min_;               // minimum amount of consumers (for RANGE only)
            uint64_t max_;               // maximum amount of consumers (for RANGE only)
            const char* channel_name_;   // name of the communication channel
        };

        // subscribes to published data, binds to PublishProducer
        struct SubscribeConsumer : public Consumer {};

        // send request, expects response, binds to ResponseProducer
        struct RequestConsumer : public Consumer {};
    };

    namespace message
    {
        class SharedDataBlob
        {
            void* data_;                  // TODO think how to do this
        };

        struct MessageHeader
        {
            uint64_t id_;
            uint64_t timestamp_ns_;

            uint8_t* data_;               // copyable data (POD) only, small size, will be copied
            uint64_t data_len_;
            SharedDataBlob* blobs_;       // array of blobs, use for big data that should not be copied
            uint64_t blob_count_;
        };
    };

    namespace logging {
        enum class LogType { INFO, WARNING, ERROR };

        class ILogger
        {
        public:
            /// @brief Log message of specific type (info, warning, error).
            virtual void log(LogType type, const char* message) const = 0;
        };
    };

    struct ModuleInfo
    {
        const char* name_;
        const char* description_;

        // list of publish producers provided by module (module provides pen position)
        const communication_channel::PublishProducer* publish_producers_;
        uint32_t publish_producer_count_;

        // list of response producers provided by module (module provides 3d pose on request)
        const communication_channel::ResponseProducer* response_producers_;
        uint32_t response_producer_count_;

        // list of subscribe consumers required by module (module needs camera data)
        const communication_channel::SubscribeConsumer* subscribe_consumers_;
        uint32_t subscribe_consumer_count_;

        // list of request consumers required by module (module needs to be able to request a 3d pose)
        const communication_channel::RequestConsumer* request_consumers_;
        uint32_t request_consumer_count_;

        /// @brief If true, automatically create a single instance of module. Can be used for example for visualizer modules that need to exist to set up other modules.
        bool auto_create_;
    };

    class IModule
    {
    public:
        inline virtual ~IModule() {}

        /// @brief Start the background thread.
        /// @param timeout_ms Wait up to "timeout_ms" milliseconds for the thread to start.
        /// @return true if started within timeout_ms. false on fail to start / timeout. Thread may exist if false.
        virtual bool threadStart(uint32_t timeout_ms) = 0;

        /// @brief Stop and join the background thread.
        /// @return true if the thread was running, stopped within "timeout_ms" milliseconds and joined. false otherwise. 
        virtual bool threadStop(uint32_t timeout_ms) = 0;

        /// @brief Process a message that came to subscribed channel "subscribe_consumer_id" from module "module_id".
        /// @param module_id there may be multiple consumers in one channel, they can be differentiated by "module_id"
        virtual void processMessage(uint64_t subscribe_consumer_id, uint64_t module_id, message::MessageHeader message) = 0;

        /// @brief Process request that came for response producer channel "response_producer_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        virtual void processRequest(uint64_t response_producer_id, message::MessageHeader message) = 0;

        /// @brief Process response that came from request consumer channel "request_consumer_id" from module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param module_id there may be multiple consumers in one channel, they can be differentiated by "module_id"
        virtual void processResponse(uint64_t request_consumer_id, uint64_t module_id, message::MessageHeader message) = 0;
    };
}