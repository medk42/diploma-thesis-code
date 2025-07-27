
#pragma once
#include <stdint.h>

namespace aergo::module
{
    class IAllocatorCore;
    class ICore;
    class ISharedData;


    
    namespace communication_channel
    {
        /// @brief 2 types:
        /// PublishProducer publishes data, binds to SubscribeConsumer.
        /// ResponseProducer provides response for requests, binds to RequestConsumer.
        struct Producer
        {
            const char* channel_type_identifier_;   // uniquely identifies channel type (for inter-module communication), e.g. "image_rgb"
            const char* display_name_;              // human-friendly displayed channel name, e.g. "Camera #1"
            const char* display_description_;       // human-friendly displayed channel description, e.g. "First camera, raw video without any image processing"
        };

        /// @brief 2 types:
        /// SubscribeConsumer subscribes to published data, binds to PublishProducer.
        /// RequestConsumer send request, expects response, binds to ResponseProducer
        struct Consumer
        {
            enum class Count { 
                SINGLE,   // single consumer
                RANGE,    // range of consumers [min, max]
                AUTO_ALL  // automatically bind subscribe/request to all available producers
            };
            
            Count count_;   // requested count of consumers
            uint64_t min_;  // minimum amount of consumers (for RANGE only)
            uint64_t max_;  // maximum amount of consumers (for RANGE only)
            
            const char* channel_type_identifier_;   // uniquely identifies channel type (for inter-module communication), e.g. "image_rgb"
            const char* display_name_;              // human-friendly displayed channel name, e.g. "Camera #1"
            const char* display_description_;       // human-friendly displayed channel description, e.g. "First camera, raw video without any image processing"
        };
    };

    namespace message
    {
        class SharedDataBlob
        {
        public:
            SharedDataBlob();
            SharedDataBlob(ISharedData* data, IAllocatorCore* allocator);

            ~SharedDataBlob();
            SharedDataBlob(const SharedDataBlob& other);
            SharedDataBlob& operator=(SharedDataBlob& other);
            SharedDataBlob(SharedDataBlob&& other) noexcept;
            SharedDataBlob& operator=(SharedDataBlob&& other) noexcept;

            /// @brief Is data valid. Do not call other functions/methods if data is invalid.
            bool valid();

            /// @brief Return pointer to the data. Behavior not specified when invalid.
            uint8_t* data();

            /// @brief Size of the data. Behavior not specified when invalid.
            uint64_t size();
             
        private:
            ISharedData* data_;
            IAllocatorCore* allocator_;
        };

        struct MessageHeader
        {
            uint8_t* data_;               // copyable data (POD) only, small size, will be copied
            uint64_t data_len_;
            SharedDataBlob* blobs_;       // array of blobs, use for big data that should not be copied
            uint64_t blob_count_;

            uint64_t id_;
            uint64_t timestamp_ns_;
            bool success_;
        };
    };

    struct ModuleInfo
    {
        // human-friendly displayed module name, e.g. "Camera"
        const char* display_name_;

        // human-friendly displayed module description, e.g. "Provides raw camera data from a connected camera"
        const char* display_description_;

        // list of publish producers provided by module (module provides pen position)
        const communication_channel::Producer* publish_producers_;
        uint32_t publish_producer_count_;

        // list of response producers provided by module (module provides 3d pose on request)
        const communication_channel::Producer* response_producers_;
        uint32_t response_producer_count_;

        // list of subscribe consumers required by module (module needs camera data)
        const communication_channel::Consumer* subscribe_consumers_;
        uint32_t subscribe_consumer_count_;

        // list of request consumers required by module (module needs to be able to request a 3d pose)
        const communication_channel::Consumer* request_consumers_;
        uint32_t request_consumer_count_;

        /// @brief If true, automatically create a single instance of module. Can be used for example for visualizer modules that need to exist to set up other modules.
        bool auto_create_;
    };

    /// @brief Identifies the module and its channel (without distinction between publish/subscribe/request/response channels)
    struct ChannelIdentifier
    {
        uint64_t producer_module_id_;    // ID of the module
        uint32_t producer_channel_id_;   // ID of the channel inside the module
    };

    struct InputChannelMapInfo
    {
        struct IndividualChannelInfo
        {
            // list of channel identifiers mapped to the input channel
            ChannelIdentifier* channel_identifier_;
            uint32_t channel_identifier_count_;
        };

        // ids of modules bound to each subscribe channel
        IndividualChannelInfo* subscribe_consumer_info_;
        uint32_t subscribe_consumer_info_count_;

        // ids of modules bound to each request channel
        IndividualChannelInfo* request_consumer_info_;
        uint32_t request_consumer_info_count_;
    };


    class Allocator
    {
    public:
        explicit Allocator(ICore* core, IAllocatorCore* allocator);
        ~Allocator();

        Allocator(const Allocator&)            = delete;
        Allocator& operator=(const Allocator&) = delete;
        Allocator(Allocator&&)                 = delete;
        Allocator& operator=(Allocator&&)      = delete;

        /// @brief Allocate "number_of_bytes" bytes of shared memory. If the allocator has fixed byte size, "number_of_bytes" parameter is ignored.
        message::SharedDataBlob allocate(uint64_t number_of_bytes);

        /// @brief Returns true if allocator is valid and ready to be used. Do not use allocator if invalid. 
        /// Only checks that internal pointers are not null - valid() check can be only performed once.
        bool valid();

    private:
        ICore* core_;
        IAllocatorCore* allocator_;
    };
}