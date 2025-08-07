#pragma once

#include "module_common/module_interface_threads.h"
#include "utils/logging/logger.h"
#include "core_structures.h"

#include <map>
#include <mutex>

namespace aergo::core
{
    class Core : public aergo::module::ICore
    {
    public:
        enum class RemoveResult { SUCCESS, DOES_NOT_EXIST, HAS_DEPENDENCIES, FAILED_TO_STOP_THREADS };

        Core(logging::ILogger* logger);
        ~Core() override;

        /// @brief Load all available modules from the modules_dir. Data for each module is in data_dir (with same folder name as the module library filename).
        /// For example "${modules_dir}/module_a.dll" will have data directory pointed to "${data_dir}/module_a". Auto-create modules with flag auto_create_.
        /// Only call this method once. Subsequent calls are ignored.
        void initialize(const char* modules_dir, const char* data_dir);

        // TODO don't forget, these will be called ASYNC from modules, they need lock-free operation or synchronization
        virtual void sendMessage(aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override final;
        virtual void sendResponse(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept override final;
        virtual void sendRequest(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept override final;
        virtual aergo::module::IAllocatorCore* createDynamicAllocator() noexcept override final;
        virtual aergo::module::IAllocatorCore* createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots) noexcept override final;
        virtual void deleteAllocator(aergo::module::IAllocatorCore* allocator) noexcept override final;

        /// @return nullptr if out of range
        const aergo::module::ModuleInfo* getLoadedModulesInfo(uint64_t loaded_module_id);
        uint64_t getLoadedModulesCount();

        /// @return nullptr if out of range or module with specified ID was destroyed
        structures::ModuleData* getCreatedModulesInfo(uint64_t running_module_id);

        /// @brief Returns the number of created modules over the lifetime of this object (even if they were later destroyed).
        /// For example if we create A,B,C,D,E -> 5; if we now remove C, D -> 5; if we add F -> 6.
        uint64_t getCreatedModulesCount();

        /// @brief ID of the module mapping state. ID changes when modules get created or destroyed.
        uint64_t getModulesMappingStateId();

        /// @brief Get existing publish channels for specified channel type identifier. 
        /// @return Returns a list of modules and channels inside the modules or empty vector if specified identifier is not tied to any channels yet.
        const std::vector<aergo::module::ChannelIdentifier>& getExistingPublishChannels(const char* channel_type_identifier);

        /// @brief Get existing response channels for specified channel type identifier.
        /// @return Returns a list of modules and channels inside the modules or empty vector if specified identifier is not tied to any channels yet.
        const std::vector<aergo::module::ChannelIdentifier>& getExistingResponseChannels(const char* channel_type_identifier);

        /// @brief Return module specified by ID. Module will only be removed if it exists (id < getCreatedModulesCount() and wasn't yet removed)
        /// and it does not have dependencies (modules connected to its outputs). If it has dependencies and recursive is true, module and all 
        /// of its (recursive) dependencies will be removed. AUTO_ALL dependencies are not considered / removed, only SINGLE and RANGE.
        /// @param id id of the module to remove
        /// @param recursive remove all dependencies too, if module has dependencies and recursive is false, module will not be removed and method will return false
        /// @return true if module (and possibly dependencies, if recursive is true) was removed, false otherwise 
        /// (module with id does not exist, was already removed or has dependencies if recursive is false).
        RemoveResult removeModule(uint64_t id, bool recursive);

        /// @brief Attempt to create a new module. Creation fails if loaded_module_id is out of range of loaded modules,
        /// channel_map_info input mapping does not match the module's creation requirements or the createModule call returned
        /// nullptr.
        /// @param loaded_module_id ID of the module to add.
        /// @param channel_map_info Communication mapping.
        /// @return true on success (module added), false otherwise (module not added)
        bool addModule(uint64_t loaded_module_id, aergo::module::InputChannelMapInfo channel_map_info);

        /// @brief Find all dependent modules and return them in a vector. Vector includes the calling module 
        /// (if no dependent modules, the vector will have size 1 and contain only the calling id).
        std::vector<uint64_t> collectDependentModules(uint64_t id);

    private:
        enum class ConsumerType { SUBSCRIBE, REQUEST };

        void log(aergo::module::logging::LogType log_type, const char* message);
        void loadModules(const char* modules_dir, const char* data_dir);
        void autoCreateModules();
        uint64_t getNextModuleId();
        void registerModuleChannelNames(uint64_t module_id, const aergo::module::ModuleInfo* module_info);  // register to existing_publish_channels_, existing_response_channels_, existing_subscribe_auto_all_channels_ and existing_request_auto_all_channels_

        /// @brief register to publishing / response module mappings and module's own mappings.
        /// method expects that InputChannelMapInfo is correctly mapped (corresponds to module definition and types match)
        void registerModuleConnections(uint64_t module_id, aergo::module::InputChannelMapInfo channel_map_info);
        void registerConsumers(uint64_t module_id, aergo::module::InputChannelMapInfo channel_map_info, ConsumerType consumer_type);
        void registerToProducersAutoAll(uint64_t module_id, aergo::module::InputChannelMapInfo channel_map_info, ConsumerType consumer_type);
        void registerToConsumersAutoAll(uint64_t module_id, aergo::module::InputChannelMapInfo channel_map_info, ConsumerType consumer_type);
        bool checkChannelMapValidity(aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::ModuleInfo* module_info); // return true if channel_map_info matched module_info
        bool checkChannelMapValidityArrayCheck(
            aergo::module::InputChannelMapInfo& channel_map_info, const aergo::module::ModuleInfo* module_info, ConsumerType consumer_type
        );

        const std::vector<aergo::module::ChannelIdentifier>& getExistingPublishChannelsImpl(const char* channel_type_identifier);
        const std::vector<aergo::module::ChannelIdentifier>& getExistingResponseChannelsImpl(const char* channel_type_identifier);
        
        /// @brief Attempt to create and start module identified by loaded_module_id.
        /// @return true on success, false on failure
        bool createAndStartModule(uint64_t loaded_module_id, aergo::module::InputChannelMapInfo channel_map_info, uint32_t module_thread_timeout_ms); 

        std::vector<uint64_t> collectDependentModulesImpl(uint64_t id);
        void collectDependentModulesHelper(structures::ModuleData* module, std::vector<uint64_t>& dependent_modules, ConsumerType consumer_type);
        void removeMappingProducers(uint64_t module_id, ConsumerType consumer_type);
        void removeMappingSubscribers(uint64_t module_id, ConsumerType consumer_type);
        void removeFromExistingMap(uint64_t module_id, uint32_t channel_count, std::function<const char*(uint32_t)> channel_type_identifier_function, std::map<std::string, std::vector<aergo::module::ChannelIdentifier>>& existing_channels);

        bool initialized_;
        std::vector<structures::ModuleLoaderData> loaded_modules_;
        std::vector<std::unique_ptr<structures::ModuleData>> running_modules_;
        std::vector<std::unique_ptr<aergo::module::IAllocatorCore>> allocators_;
        uint64_t module_mapping_state_id_;

        std::map<std::string, std::vector<aergo::module::ChannelIdentifier>> existing_publish_channels_;
        std::map<std::string, std::vector<aergo::module::ChannelIdentifier>> existing_response_channels_;
        std::map<std::string, std::vector<aergo::module::ChannelIdentifier>> existing_subscribe_auto_all_channels_;
        std::map<std::string, std::vector<aergo::module::ChannelIdentifier>> existing_request_auto_all_channels_;

        std::mutex core_mutex_;

        logging::ILogger* logger_;
    };
}