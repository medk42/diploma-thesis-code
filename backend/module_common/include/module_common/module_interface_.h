#pragma once

#include <cstdint>
#include <typeinfo>
#include <vector>
#include <string>
#include <tuple>
#include <span>
#include <span>
#include <cstring>

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
            
            /// Only for ResponseProducer; if true, requests to this response channel are prioritized (processed by separate thread) 
            /// over non-prioritized channels. ONLY for channels that need low latency even after load (e.g. control commands, GUI etc).
            bool prioritized_ = false;
            uint16_t message_queue_capacity_ = 4; // maximum number of waiting requests in the queue (beyond that, new requests are dropped), min 1
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

            /// If true, responses/messages to this channel are prioritized (processed by separate thread) 
            /// over non-prioritized channels. ONLY for channels that need low latency even after load (e.g. control commands, GUI etc).
            bool prioritized_ = false;
            uint16_t message_queue_capacity_ = 4; // maximum number of waiting messages/responses in the queue (beyond that, new messages/responses are dropped), min 1
        };
    };

    struct ModuleInfo
    {
        // uniquely identifies module type (for inter-module communication), e.g. "camera_module"
        const char* module_type_identifier_;

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

        uint8_t prioritized_workers_count_ = 1;  // number of prioritized worker threads (for prioritized channels), min 1
        uint8_t regular_workers_count_ = 1;      // number of regular worker threads (for non-prioritized channels), min 1
    };

    struct ChannelIdentifier
    {
        uint64_t module_id_;    // ID of the module
        uint32_t local_channel_id_;   // ID of the channel inside the module

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
            template <typename ByteT>
            static MessageHeader Message(std::span<const ByteT> bytes)
            {
                static_assert(sizeof(ByteT) == 1, "ByteT must be 1 byte");
                return MessageHeader
                { // TODO const_cast is not nice, but would require changes in many places - maybe fix later
                    .data_ = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(bytes.data())),
                    .data_len_ = static_cast<uint64_t>(bytes.size()),
                    .blobs_ = nullptr,
                    .blob_count_ = 0,
                    .id_ = 0,
                    .timestamp_ns_ = 0,
                    .success_ = true
                };
            }

            template <typename T>
            static MessageHeader Message(T* data)
            {
                static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
                static_assert(std::is_standard_layout_v<T>, "T must be standard-layout");
                return MessageHeader
                {
                    .data_ = reinterpret_cast<uint8_t*>(data),
                    .data_len_ = sizeof(T),
                    .blobs_ = nullptr,
                    .blob_count_ = 0,
                    .id_ = 0,
                    .timestamp_ns_ = 0,
                    .success_ = true
                };
            }

            template <typename T>
            static MessageHeader Message(T* data, SharedDataBlob* blobs, uint64_t blob_count = 1)
            {
                static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
                static_assert(std::is_standard_layout_v<T>, "T must be standard-layout");
                return MessageHeader
                {
                    .data_ = reinterpret_cast<uint8_t*>(data),
                    .data_len_ = sizeof(T),
                    .blobs_ = blobs,
                    .blob_count_ = blob_count,
                    .id_ = 0,
                    .timestamp_ns_ = 0,
                    .success_ = true
                };
            }

            static MessageHeader Failure()
            {
                return MessageHeader
                {
                    .data_ = nullptr,
                    .data_len_ = 0,
                    .blobs_ = nullptr,
                    .blob_count_ = 0,
                    .id_ = 0,
                    .timestamp_ns_ = 0,
                    .success_ = false
                };
            }

            template<typename T>
            bool readAs(T& out_data) const
            {
                static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
                static_assert(std::is_standard_layout_v<T>, "T must be standard-layout");

                if (!success_ || data_len_ != sizeof(T) || data_ == nullptr)
                {
                    return false; // invalid data
                }
                std::memcpy(&out_data, data_, sizeof(T)); // safe for alignment & aliasing
                return true;
            }

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

        /// @brief Allocate shared data from existing data buffer.
        /// @return SharedDataBlob, check for validity by calling the valid() function
        template <typename ByteT>
        message::SharedDataBlob allocateFromData(std::span<ByteT> data) noexcept
        {
            static_assert(sizeof(ByteT) == 1, "ByteT must be 1 byte");

            auto blob = allocate(static_cast<uint64_t>(data.size()));
            if (!blob.valid() || blob.size() < data.size())
            {
                return message::SharedDataBlob(); // allocation failed, return invalid blob
            }

            std::memcpy(blob.data(), data.data(), data.size()); // safe for alignment & aliasing
            return blob;
        }

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

        /// @brief Mapping of input channels (subscribe/request) to other modules and their output channels (publish/response). Invalid if module does not exist.
        /// Structure is {uint32_t subscribe_consumer_count, {uint32_t channel_identifier_count, ChannelIdentifier[channel_identifier_count]}[subscribe_consumer_count],
        ///               uint32_t request_consumer_count, {uint32_t channel_identifier_count, ChannelIdentifier[channel_identifier_count]}[request_consumer_count]}
        virtual message::SharedDataBlob getRunningModulesChannelMap(uint64_t running_module_id) noexcept = 0;

        /// @brief Returns the number of created modules over the lifetime of this object (even if they were later destroyed).
        /// For example if we create A,B,C,D,E -> 5; if we now remove C, D -> 5; if we add F -> 6.
        virtual uint64_t getRunningModulesCount() noexcept = 0;

        /// @brief ID of the module mapping state. ID increases by one when modules get created or destroyed.
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

        /// @brief Save state of the core, the module mapping, created modules and their internal state.
        virtual message::SharedDataBlob save() noexcept = 0;

        /// @brief Attempt to load state of the core, the module mapping, created modules and their internal state.
        /// Loading first removes all created modules (aside from auto_create modules) and then attempts to load the saved state.
        /// State is loaded by first checking if all loaded modules required by the saved state are still loaded. If not, loading fails.
        /// Then all modules are created in the order they were saved (original IDs are not saved, new IDs are assigned). Each module is
        /// loaded by creating it (calling its constructor), then starting its threads (threadStart method) and then calling its load() method
        /// with the data it saved on save(). If any of these steps fail, loading fails and all created modules are removed (aside from auto_create modules).
        /// Therefore, it is required to call this method only from an auto_create module (for example visualization) or from the outside (owner of the core object).
        /// After loading, it is guaranteed that:
        /// - all modules that were loaded while saving are also loaded after loading
        ///     - however, there may be new modules loaded too
        ///     - order of loaded modules may change (do not rely on loaded module IDs between save/load)
        /// - all modules that were created while saving are also created after loading
        ///     - however, there may be new modules created too (only auto_create modules, since they are created at start and not removed on load)
        ///     - order of created modules may change (do not rely on running module IDs between save/load)
        ///     - if module A depended on module B while saving, it is guaranteed that after loading module A will again depend on module B (however, B may have a different ID now)
        /// - internal state of modules after saving / loading is restored by calling save() / load() - it is up to the module to implement this correctly
        /// - mapping state ID can be different after loading
        /// - allocators are not saved / loaded, it is up to the module to create allocators again if needed
        /// - modules are loaded by calling the constructor, start threads and load
        virtual bool load(const uint8_t* data, uint64_t size) noexcept = 0;
    };

    /// @brief Reference to the core.
    class ICore : public ICoreBase, public ICoreControl
    {};

    struct ResponseData
    {
        /// @brief Create ResponseData from trivially copyable data. Blob list will be empty.
        /// @tparam T Trivially copyable data type.
        template<typename T>
        static ResponseData createResponse(T data)
        {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

            ResponseData response;
            response.success_ = true;

            const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(&data);
            response.data_.assign(data_ptr, data_ptr + sizeof(T));

            return response;
        }

        /// @brief Create ResponseData from trivially copyable data and optional blob. 
        /// If blob is empty (no data), an empty blob is still created.
        /// @tparam T Trivially copyable data type.
        template<typename T, typename ByteT>
        static ResponseData createResponse(T data, std::span<ByteT> blob, IAllocator* allocator)
        {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
            static_assert(sizeof(ByteT) == 1, "ByteT must be a byte type (size 1)");

            ResponseData response;
            response.success_ = true;

            // handle blob first (to avoid partial response on failure)
            if (!allocator)
            {
                response.success_ = false;
                return response;
            }

            // create shared data blob
            message::SharedDataBlob shared_blob = allocator->allocate(blob.size());
            if (!shared_blob.valid() || shared_blob.size() < blob.size())
            {
                response.success_ = false;
                return response;
            }

            // copy blob data
            std::memcpy(shared_blob.data(), blob.data(), blob.size());
            response.blobs_.emplace_back(std::move(shared_blob));

            // copy data
            const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(&data);
            response.data_.assign(data_ptr, data_ptr + sizeof(T));

            return response;
        }

        static ResponseData createFailure()
        {
            ResponseData response;
            response.success_ = false;
            return response;
        }

        bool success_;
        std::vector<uint8_t> data_;
        std::vector<message::SharedDataBlob> blobs_;
    };

    class IModuleBase
    {
    public:
        
        virtual ~IModuleBase() noexcept = default;

        /// @brief Process a message that came to subscribed channel "subscribe_consumer_id" from module "module_id".
        /// @param subscribe_consumer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source publish channel (module and channel ID)
        virtual void processMessage(uint32_t subscribe_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept = 0;

        /// @brief Process request that came for response producer channel "response_producer_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param response_producer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source request channel (module and channel ID)
        /// @return ResponseData containing success flag, response data and blobs.
        virtual ResponseData processRequest(uint32_t response_producer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept = 0;

        /// @brief Process response that came from request consumer channel "request_consumer_id" from module "module_id".
        /// Message request/response pair is identified by ID in MessageHeader. 
        /// @param request_consumer_id ID of this module's channel from which the message came
        /// @param source_channel identifies the source response channel (module and channel ID)
        virtual void processResponse(uint32_t request_consumer_id, ChannelIdentifier source_channel, message::MessageHeader message) noexcept = 0;
    };

    class ISerializableModule
    {
    public:
        struct SavedBlob
        {
            std::string name_;
            std::vector<uint8_t> data_;
        };

        struct SaveData 
        {
            bool success_ = true; // true if saving was successful, false otherwise
            bool supports_saving_ = false; // if false, module does not support saving/loading
            uint32_t schema_version_;
            std::string json_header_;
            std::vector<SavedBlob> blobs_;
        };

        inline virtual ~ISerializableModule() noexcept = default;

        /// @brief Save the module state. State is saved into a JSON header and a list of named binary blobs.
        /// Any large data should be saved into blobs, small POD data can be saved into the JSON header.
        /// The name in SavedBlob must be unique for each blob and is used to store the blob on the filesystem,
        /// therefore it should not contain special characters or path separators (for both Windows and Linux).
        /// The name_ however can contain a file extension (e.g. ".bin", ".dat", ".png" etc) if needed.
        /// Use schema_version_ to indicate version of the saved data, it can be used in load() to handle
        /// loading of different versions of saved data (or to refuse loading of unsupported versions).
        /// Use supports_saving_ to indicate if the module supports saving/loading at all.].
        /// @return shared data blob containing saved state or invalid blob if saving is not supported or failed.
        virtual SaveData save() noexcept = 0;

        /// @brief Load the module state. If saving is not supported always return true.
        /// Will be called immediately after module is created and its threads are started (threadStart() method).
        /// For auto_create modules, load() may be called multiple times during the lifetime of the module, because
        /// auto_create modules are not removed on core state load.
        virtual bool load(SaveData data) noexcept = 0;
    };

    class IModule : public IModuleBase, public ISerializableModule
    {  
    public:
        ~IModule() noexcept override = default;

        enum class ProcessingType { MESSAGE, REQUEST, RESPONSE };

        enum class QueueStatus 
        { 
            NORMAL,     // queue has space for this message
            QUEUE_FULL,  // queue is full, message can not be accepted (on ACCEPT response, message will be dropped)
            QUEUE_FULL_CAN_DROP  // queue is full, but processing has not yet started on at least one message, so oldest message can be dropped if needed
        };

        enum class IngressDecision
        { 
            ACCEPT,                   // accept message, push to processing queue (or drop if queue is full)
            DROP,                     // drop message, do not push to processing queue
            ACCEPT_DROP_QUEUE_FIRST,  // accept message, drop oldest message from processing queue and push this message to the back of the queue
            ACCEPT_REPLACE_QUEUE      // accept message, clear processing queue and push this message to the back of the queue (useful for data streams like video, only keep the latest data)
        };
        
        /// @brief True if module was successfully created, false otherwise. Please check after calling the constructor.
        virtual bool valid() noexcept = 0;

        /// @brief Query internal module for type. Module can implement for example IActivable and ISavable, query can be used to recover the correct
        /// interface from the base module.
        /// @return Pointer to requested interface or nullptr if module does not implement it.
        virtual void* query_capability(const std::type_info& id) noexcept = 0;

        /// @brief Called when a message/request/response is received, before it is pushed to a processing queue. This method must be non-blocking and return immediately.
        /// Queue can be in 3 states: NORMAL (has space), QUEUE_FULL_CAN_DROP (no available space, but processing has not yet started on at least one message, so oldest message can be dropped if needed),
        /// QUEUE_FULL (no available space, processing has started on all messages in the queue).
        ///
        /// In NORMAL state, message can be ACCEPTed (added to end of queue), DROPped (not added to queue), ACCEPT_DROP_QUEUE_FIRST (oldest message in queue dropped, new message added to end of queue)
        /// or ACCEPT_REPLACE_QUEUE (queue cleared, new message added to end of queue).
        ///
        /// In QUEUE_FULL_CAN_DROP state, message can be DROPped (not added to queue), ACCEPT_DROP_QUEUE_FIRST (oldest message in queue dropped, new message added to end of queue)
        /// or ACCEPT_REPLACE_QUEUE (queue cleared, new message added to end of queue). ACCEPT is not allowed, since queue is full - will be treated as DROP.
        ///
        /// In QUEUE_FULL state, all decisions will be treated as DROP, since queue is full and processing has started on all messages in the queue.
        /// @return decision on what to do with the message.
        virtual IngressDecision onIngress(ProcessingType kind, uint32_t local_channel_id, ChannelIdentifier src, const message::MessageHeader& msg, QueueStatus queue_status) noexcept = 0;

        /// @brief If module needs to start worker threads, it should do so here. This method should return within "timeout_ms" milliseconds at most.
        /// @return true if started within timeout_ms. false on fail to start / timeout.
        virtual bool threadStart(uint32_t timeout_ms) noexcept = 0;

        /// @brief If module has worker threads, it should stop them here. This method should return within "timeout_ms" milliseconds at most.
        /// @return true if the thread was running, stopped within "timeout_ms" milliseconds and joined. false otherwise.
        virtual bool threadStop(uint32_t timeout_ms) noexcept = 0;

        /// @brief Get module information corresponding to this module. Can not return nullptr.
        virtual const ModuleInfo* getModuleInfo() const noexcept = 0;

        /// @brief Query internal module for type. Module can implement for example IActivable and ISavable, query can be used to recover the correct
        /// interface from the base module.
        template<class T>
        T* query() noexcept {
            return static_cast<T*>(query_capability(typeid(T)));
        }
    };

    namespace save_toolkit
    {
        bool serializeSaveState(const std::string& json_header, const std::vector<std::tuple<std::string, std::vector<aergo::module::ISerializableModule::SavedBlob>>>& extra_blobs, std::vector<uint8_t>& out_data) noexcept;
        bool deserializeSaveState(const uint8_t* data, uint64_t size, std::string& out_json_header, std::vector<std::tuple<std::string, std::vector<aergo::module::ISerializableModule::SavedBlob>>>& out_blobs) noexcept;
    }
}