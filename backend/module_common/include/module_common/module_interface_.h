#pragma once

#include <cstdint>
#include <typeinfo>

namespace aergo::module
{
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

    struct ChannelIdentifier
    {
        uint64_t producer_module_id_;    // ID of the module
        uint32_t producer_channel_id_;   // ID of the channel inside the module

        constexpr bool operator==(const ChannelIdentifier&) const = default;
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

    /// @brief Reference to shared data in the core. 
    class ISharedData
    {
    public:
        inline virtual ~ISharedData() = default;

        /// @brief Is data valid. Do not call other functions/methods if data is invalid.
        virtual bool valid() noexcept = 0;

        /// @brief Return pointer to the data. Behavior not specified when invalid.
        virtual uint8_t* data() noexcept = 0;

        /// @brief Size of the data. Behavior not specified when invalid.
        virtual uint64_t size() noexcept = 0;
    };
    
    class IAllocator;

    namespace message
    {        
        class SharedDataBlob
        {
        public:
            SharedDataBlob();
            SharedDataBlob(ISharedData* data, IAllocator* allocator);

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
            IAllocator* allocator_;
        };

        struct MessageHeader
        {
            uint8_t* data_;               // copyable data (POD) only, small size, will be copied
            uint64_t data_len_;
            SharedDataBlob* blobs_;       // array of blobs, use for big data that should not be copied
            uint64_t blob_count_;

            uint64_t id_;
            uint64_t timestamp_ns_;
            bool success_;                // indicates successful processing of request
        };
    };

    /// @brief Reference to shared data allocator in the core.
    class IAllocator
    {
    public:
        inline virtual ~IAllocator() = default;

        /// @brief Allocate "number_of_bytes" bytes of shared memory. If the allocator has fixed byte size, "number_of_bytes" parameter is ignored.
        /// @return SharedDataBlob, check for validity by calling the valid() function
        virtual message::SharedDataBlob allocate(uint64_t number_of_bytes) noexcept = 0;

    protected:
        /// @brief Add owner for shared data object. Object removed when owners drop to zero.
        virtual void addOwner(ISharedData* data) noexcept = 0;

        /// @brief Remove owner from shared data object. Object removed when owners drop to zero.
        virtual void removeOwner(ISharedData* data) noexcept = 0;

        friend class message::SharedDataBlob;
    };

    namespace logging {
        enum class LogType { INFO, WARNING, ERROR };

        class ILogger
        {
        public:
            inline virtual ~ILogger() = default;

            /// @brief Log message of specific type (info, warning, error).
            virtual void log(LogType type, const char* message) const noexcept = 0;
        };
    };

    struct RunningModuleInfo
    {
        bool exists_;                      // does the module exist (was not destroyed)
        const ModuleInfo* module_info_;    // reference to module info (loaded module)

        /// @brief Mapping of input channels (subscribe/request) to other modules and their output channels (publish/response).
        /// Structure is {uint32_t subscribe_consumer_count, {uint32_t channel_identifier_count, ChannelIdentifier[channel_identifier_count]}[subscribe_consumer_count],
        ///               uint32_t request_consumer_count, {uint32_t channel_identifier_count, ChannelIdentifier[channel_identifier_count]}[request_consumer_count]}
        message::SharedDataBlob channel_map_; 
    };

    /// @brief Interface provided by the core to modules.
    class ICoreBase
    {
    public:	
        inline virtual ~ICoreBase() = default;

        /// @brief Publish message to channel "publish_producer_id".
        /// @param source_channel identifies the source publish channel (module and channel ID)
        virtual void sendMessage(ChannelIdentifier source_channel, message::MessageHeader message) noexcept = 0;

        /// @brief Send response to channel "response_producer_id". 
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param source_channel identifies the source response channel (module and channel ID)
        /// @param target_channel identifies the target request channel (module and channel ID)
        virtual void sendResponse(ChannelIdentifier source_channel, ChannelIdentifier target_channel, message::MessageHeader message) noexcept = 0;
        
        /// @brief Send request to channel "request_consumer_id" to module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param source_channel identifies the source request channel (module and channel ID)
        /// @param target_channel identifies the target response channel (module and channel ID)
        virtual void sendRequest(ChannelIdentifier source_channel, ChannelIdentifier target_channel, message::MessageHeader message) noexcept = 0;

        /// @brief Create dynamic allocator for shared data (to avoid copying large data). Each allocate call creates new memory.
        /// @return New allocator or nullptr on failure.
        virtual IAllocator* createDynamicAllocator() noexcept = 0;

        /// @brief Create buffered allocator for shared data (to avoid copying large data). Allocation happens on a buffer.
        /// Memory is pre-allocated. Allocation can fail if all buffer space is used.
        /// @param slot_size_bytes Fixed allocation size in bytes.
        /// @param number_of_slots Number of "size_bytes" sized slots.
        /// @return New allocator or nullptr on failure.
        virtual IAllocator* createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots) noexcept = 0;

        /// @brief Delete previously created allocator.
        virtual void deleteAllocator(IAllocator* allocator) noexcept = 0;
    };

    /// @brief Interface provided by the core to control module management.
    class ICoreControl
    {
    public:
        inline virtual ~ICoreControl() = default;
        
        /// @brief Returns information about loaded module. If loaded_module_id is out of range, returns nullptr. 
        virtual const aergo::module::ModuleInfo* getLoadedModulesInfo(uint64_t loaded_module_id) noexcept = 0;

        /// @brief Returns the number of loaded modules.
        virtual uint64_t getLoadedModulesCount() noexcept = 0;

        /// @brief Returns information about created module. If module was destroyed, exists_ will be false.
        /// @return Check returned blob for validity by calling the valid() function.
        virtual RunningModuleInfo getRunningModulesInfo(uint64_t running_module_id) noexcept = 0;

        /// @brief Returns the number of created modules over the lifetime of this object (even if they were later destroyed).
        /// For example if we create A,B,C,D,E -> 5; if we now remove C, D -> 5; if we add F -> 6.
        virtual uint64_t getRunningModulesCount() noexcept = 0;

        /// @brief ID of the module mapping state. ID changes when modules get created or destroyed.
        /// Can be used to detect changes in module mapping and update UI.
        virtual uint64_t getModulesMappingStateId() noexcept = 0;

        /// @brief Attempt to create a new module. Creation fails if loaded_module_id is out of range of loaded modules,
        /// channel_map_info input mapping does not match the module's creation requirements or the createModule call returned
        /// nullptr.
        /// @param loaded_module_id ID of the module to add.
        /// @param channel_map_info Communication mapping.
        /// @return true on success (module added), false otherwise (module not added)
        virtual bool addModule(uint64_t loaded_module_id, aergo::module::InputChannelMapInfo channel_map_info) noexcept = 0;

        /// @brief Find all dependent modules and return them in a vector. Vector includes the calling module 
        /// (if no dependent modules, the vector will have size 1 and contain only the calling id).
        /// @return blob containing vector size (as uint64_t) and uint64_t array of module ids,
        /// structure is {uint64_t size, uint64_t ids[size]}. Check returned blob for validity by calling the valid() function.
        virtual message::SharedDataBlob collectDependencies(uint64_t id) noexcept = 0;

        /// @brief Remove module specified by ID. Module will only be removed if it exists (id < getCreatedModulesCount() and wasn't yet removed)
        /// and it does not have dependencies (modules connected to its outputs). If it has dependencies and recursive is true, module and all 
        /// of its (recursive) dependencies will be removed. AUTO_ALL dependencies are not considered / removed, only SINGLE and RANGE.
        /// @return true if module (and possibly dependencies, if recursive is true) was removed, false otherwise 
        /// (module with id does not exist, was already removed or has dependencies if recursive is false).
        virtual bool removeModuleById(uint64_t id, bool recursive) noexcept = 0;

        /// @brief Get existing publish channels for specified channel type identifier. 
        /// @return Returns a list of modules and channels inside the modules or empty vector if specified identifier is not tied to any channels yet.
        /// The return structure is {uint64_t size, ChannelIdentifier[size]}. Check returned blob for validity by calling the valid() function.
        virtual message::SharedDataBlob getExistingPublishChannelsByName(const char* channel_type_identifier) noexcept = 0;

        /// @brief Get existing response channels for specified channel type identifier.
        /// @return Returns a list of modules and channels inside the modules or empty vector if specified identifier is not tied to any channels yet.
        /// The return structure is {uint64_t size, ChannelIdentifier[size]}. Check returned blob for validity by calling the valid() function.
        virtual message::SharedDataBlob getExistingResponseChannelsByName(const char* channel_type_identifier) noexcept = 0;
    };

    /// @brief Reference to the core.
    class ICore : public virtual ICoreBase, public virtual ICoreControl
    {};

    class IModuleBase
    {
    public:
        
        virtual ~IModuleBase() = default;

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
    };

    class IModule : public virtual IModuleBase
    {
    public:

        /// @brief Cycle method of the module. Called in each period after all messages/request/responses are handled. 
        /// If cycleImpl contains sleep, consider disabling sleep in ModuleWrapper by passing thread_sleep_ms = 0 in ModuleWrapper's constructor.
        virtual void cycleImpl() noexcept = 0;
        
        virtual bool valid() noexcept = 0;

        virtual void* query_capability(const std::type_info& id) noexcept = 0;

        /// @brief Query internal module for type. Module can implement for example IActivable and ISavable, query can be used to recover the correct
        /// interface from the base module.
        template<class T>
        T* query() noexcept {
            return static_cast<T*>(query_capability(typeid(T)));
        }
    };
}