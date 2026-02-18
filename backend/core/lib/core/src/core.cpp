#include "core/core.h"
#include "core/defaults.h"
#include "module_common/serialization_helper.h"
#include "utils/memory_allocation/dynamic_allocator.h"
#include "utils/memory_allocation/static_allocator.h"
#include "utils/memory_allocation/allocator_wrapper.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <algorithm>
#include <cstddef>
#include <span>

using namespace aergo::core;
using json = nlohmann::json;



Core::Core(logging::ILogger* logger)
: logger_(logger), initialized_(false), module_mapping_state_id_(0)
{
    core_dynamic_allocator_ = std::move(std::unique_ptr<aergo::module::IAllocator, std::function<void(aergo::module::IAllocator*)>>(
        createDynamicAllocator(),
        [this](aergo::module::IAllocator* allocator_ref) { deleteAllocator(allocator_ref); }
    ));
}



Core::~Core()
{
    for (auto& module : running_modules_)
    {
        if (module.get() != nullptr)
        {
            module->module_->threadStop(defaults::module_thread_timeout_ms_);
        }
    }

    running_modules_.clear();
}



bool Core::initialize(const char* modules_dir, const char* data_dir)
{
    std::unique_lock<std::shared_mutex> lock(core_mutex_);

    if (initialized_)
    {
        return true; // already initialized, ignore subsequent calls
    }
    initialized_ = true;

    loadModules(modules_dir, data_dir);
    return autoCreateModules();
}



void Core::loadModules(const char* modules_dir, const char* data_dir)
{
    std::filesystem::directory_entry modules_dir_entry(modules_dir);
    if (!modules_dir_entry.exists())
    {
        std::string log_msg = std::string("Attempting to load modules from directory that does not exist: ") + std::filesystem::absolute(modules_dir_entry).string();
        log(aergo::module::logging::LogType::WARNING, log_msg.c_str());
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(modules_dir_entry))
    {
        auto module_path = entry.path();
        std::string module_path_str = module_path.string();

        if (!entry.exists() && module_path.has_stem())
        {
            std::string log_message = std::string("Following module file does not exist: ") + module_path_str;
            log(aergo::module::logging::LogType::WARNING, log_message.c_str());
        }

        
        std::string module_filename = module_path.stem().string();
        if (module_path.extension() == ".so" && module_filename.starts_with("lib"))
        {
            module_filename = module_filename.substr(3); // remove "lib" prefix for .so files
        }
        
        std::filesystem::path data_path = std::filesystem::path(data_dir) / module_filename;

        auto loaded_module = ModuleLoader::loadModule(module_path_str.c_str());
        if (loaded_module)
        {
            uint64_t module_api_version = (*loaded_module)->readPluginApiVersion();
            if (module_api_version != CORE_API_VERSION)
            {
                std::stringstream log_message;
                log_message << "Failed to load module, mismatched interface API version (core=" << CORE_API_VERSION << " / module=" << module_api_version << "): " << module_filename;
            }
            else
            {
                std::string log_message = std::string("Module loaded successfully: ") + module_filename;
                log(aergo::module::logging::LogType::INFO, log_message.c_str());

                loaded_modules_.emplace_back(
                    std::move(*loaded_module),
                    std::move(data_path.string()),
                    std::move(module_filename)
                );
            }
        }
        else
        {
            std::string log_message = "Failed to load module due to error ";
            switch(loaded_module.error())
            {
                case ModuleLoadError::FAILED_TO_LOAD:
                    log_message += "(FAILED_TO_LOAD): " + module_filename;
                    break;
                case ModuleLoadError::FAILED_TO_MAP_METHODS:
                    log_message += "(FAILED_TO_MAP_METHODS): " + module_filename;
                    break;
                default:
                    log_message += "(UNKNOWN): " + module_filename;
            }
            log(aergo::module::logging::LogType::WARNING, log_message.c_str());
        }
    }
}



bool Core::autoCreateModules()
{
    aergo::module::InputChannelMapInfo empty_channel_info
    {
        .subscribe_consumer_info_ = nullptr,
        .subscribe_consumer_info_count_ = 0,
        .request_consumer_info_ = nullptr,
        .request_consumer_info_count_ = 0
    };


    for (size_t i = 0; i < loaded_modules_.size(); ++i)
    {
        const aergo::module::ModuleInfo* module_info = loaded_modules_[i]->readModuleInfo();
        if (module_info->auto_create_)
        {
            bool valid_mapping = true;
            for (uint32_t subscribe_id = 0; subscribe_id < module_info->subscribe_consumer_count_; ++subscribe_id)
            {
                if (module_info->subscribe_consumers_[subscribe_id].count_ != aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
                {
                    valid_mapping = false;
                    break;
                }
            }
            for (uint32_t request_id = 0; request_id < module_info->request_consumer_count_ && valid_mapping; ++request_id)
            {
                if (module_info->request_consumers_[request_id].count_ != aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
                {
                    valid_mapping = false;
                    break;
                }
            }

            if (valid_mapping)
            {
                if (createAndStartModule(i, empty_channel_info, defaults::module_thread_timeout_ms_))
                {
                    std::string success_message = std::string("Successfully auto-created module: ") + loaded_modules_[i].getModuleUniqueName();
                    log(aergo::module::logging::LogType::INFO, success_message.c_str());
                }
                else
                {
                    std::string failure_message = std::string("Failed to auto-create module: ") + loaded_modules_[i].getModuleUniqueName();
                    log(aergo::module::logging::LogType::WARNING, failure_message.c_str());
                    return false;
                }
            }
            else
            {
                std::string failure_message = std::string("Failed to auto-create module due to invalid mapping (all subscribe/request consumers are required to be AUTO_ALL for auto_create modules): ") + loaded_modules_[i].getModuleUniqueName();
                log(aergo::module::logging::LogType::WARNING, failure_message.c_str());
            }
        }
    }

    return true;
}



bool Core::createAndStartModule(uint64_t loaded_module_id, aergo::module::InputChannelMapInfo channel_map_info, uint32_t module_thread_timeout_ms)
{
    if (loaded_module_id >= loaded_modules_.size())
    {
        return false;
    }

    const char* data_path;
    if (std::filesystem::exists(loaded_modules_[loaded_module_id].getModuleDataPath()))
    {
        data_path = loaded_modules_[loaded_module_id].getModuleDataPath().c_str();
    }
    else
    {
        data_path = nullptr;
    }

    
    uint64_t next_module_id = getNextModuleId();
    auto module_data = std::make_unique<structures::ModuleData>(
        structures::ModuleLogger(
            logger_, 
            loaded_modules_[loaded_module_id].getModuleUniqueName(), 
            next_module_id
        ), 
        &(loaded_modules_[loaded_module_id])
    );

    ModuleLoader::ModulePtr created_module(loaded_modules_[loaded_module_id]->createModule(data_path, this, channel_map_info, &(module_data->logger_), next_module_id));

    if (created_module.get() == nullptr)
    {
        std::string error_message = std::string("Failed to create module (createModule call failed) for module: ") + module_data->module_loader_data_->getModuleUniqueName();
        log(aergo::module::logging::LogType::WARNING, error_message.c_str());
        return false;
    }
    else
    {
        bool result = created_module->threadStart(module_thread_timeout_ms);
        if (!result)
        {
            bool result2 = created_module->threadStop(module_thread_timeout_ms);

            std::string error_message = std::string("Failed to start thread for module: \"") + module_data->module_loader_data_->getModuleUniqueName() + std::string("\", stop success: ") + (result2 ? "TRUE" : "false");
            log(aergo::module::logging::LogType::WARNING, error_message.c_str());

            return false;
        }
        else
        {
            module_data->module_ = std::move(created_module);
            running_modules_.push_back(std::move(module_data));

            registerModuleChannelNames(next_module_id, loaded_modules_[loaded_module_id]->readModuleInfo());
            registerModuleConnections(next_module_id, channel_map_info);

            return true;
        }
    }
}



uint64_t Core::getNextModuleId()
{
    return running_modules_.size();
}



void Core::registerModuleChannelNames(uint64_t module_id, const aergo::module::ModuleInfo* module_info)
{
    for (uint32_t channel_id = 0; channel_id < module_info->publish_producer_count_; ++channel_id)
    {
        existing_publish_channels_[module_info->publish_producers_[channel_id].channel_type_identifier_].push_back({
            .module_id_ = module_id,
            .local_channel_id_ = channel_id
        });
    }

    for (uint32_t channel_id = 0; channel_id < module_info->response_producer_count_; ++channel_id)
    {
        existing_response_channels_[module_info->response_producers_[channel_id].channel_type_identifier_].push_back({
            .module_id_ = module_id,
            .local_channel_id_ = channel_id
        });
    }

    for (uint32_t channel_id = 0; channel_id < module_info->subscribe_consumer_count_; ++channel_id)
    {
        if (module_info->subscribe_consumers_[channel_id].count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
        {
            existing_subscribe_auto_all_channels_[module_info->subscribe_consumers_[channel_id].channel_type_identifier_].push_back({
                .module_id_ = module_id,
                .local_channel_id_ = channel_id
            });
        }
    }

    for (uint32_t channel_id = 0; channel_id < module_info->request_consumer_count_; ++channel_id)
    {
        if (module_info->request_consumers_[channel_id].count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
        {
            existing_request_auto_all_channels_[module_info->request_consumers_[channel_id].channel_type_identifier_].push_back({
                .module_id_ = module_id,
                .local_channel_id_ = channel_id
            });
        }
    }
}



void Core::registerModuleConnections(uint64_t module_id, aergo::module::InputChannelMapInfo channel_map_info)
{
    if (module_id >= running_modules_.size())
    {
        log(aergo::module::logging::LogType::ERROR, "registerModuleConnections called with wrong module_id");
        return;
    }

    auto& running_module = running_modules_[module_id];

    if (running_module.get() == nullptr)
    {
        log(aergo::module::logging::LogType::ERROR, "registerModuleConnections called with nullptr module");
        return;
    }
    
    registerConsumers(module_id, channel_map_info, ConsumerType::SUBSCRIBE);
    registerConsumers(module_id, channel_map_info, ConsumerType::REQUEST);
    
    registerToProducersAutoAll(module_id, channel_map_info, ConsumerType::SUBSCRIBE);
    registerToProducersAutoAll(module_id, channel_map_info, ConsumerType::REQUEST);
    
    registerToConsumersAutoAll(module_id, channel_map_info, ConsumerType::SUBSCRIBE);
    registerToConsumersAutoAll(module_id, channel_map_info, ConsumerType::REQUEST);
}



void Core::registerConsumers(uint64_t module_id, aergo::module::InputChannelMapInfo channel_map_info, ConsumerType consumer_type)
{
    auto& running_module = running_modules_[module_id];

    uint32_t consumer_info_count;
    aergo::module::InputChannelMapInfo::IndividualChannelInfo* consumer_info;

    if (consumer_type == ConsumerType::SUBSCRIBE)
    {
        consumer_info_count = channel_map_info.subscribe_consumer_info_count_;
        consumer_info = channel_map_info.subscribe_consumer_info_;
    }
    else if (consumer_type == ConsumerType::REQUEST)
    {
        consumer_info_count = channel_map_info.request_consumer_info_count_;
        consumer_info = channel_map_info.request_consumer_info_;
    }
    else
    {
        log(aergo::module::logging::LogType::ERROR, "Unexpected enum type in registerConsumers, terminating!");
        std::terminate();
    }

    for (uint32_t channel_id = 0; channel_id < consumer_info_count; ++channel_id)
    {
        aergo::module::InputChannelMapInfo::IndividualChannelInfo consumer_channel_info = consumer_info[channel_id];
        for (uint32_t channel_i = 0; channel_i < consumer_channel_info.channel_identifier_count_; ++channel_i)
        {
            aergo::module::ChannelIdentifier channel_identifier = consumer_channel_info.channel_identifier_[channel_i];

            auto& mapping_producer = (consumer_type == ConsumerType::SUBSCRIBE) ? running_modules_[channel_identifier.module_id_]->mapping_publish_ : running_modules_[channel_identifier.module_id_]->mapping_response_;
            auto& mapping_consumer = (consumer_type == ConsumerType::SUBSCRIBE) ? running_module->mapping_subscribe_ : running_module->mapping_request_;

            mapping_producer[channel_identifier.local_channel_id_].push_back({
                .module_id_ = module_id,
                .local_channel_id_ = channel_id
            });

            mapping_consumer[channel_id].push_back(channel_identifier);
        }
    }
}



void Core::registerToProducersAutoAll(uint64_t module_id, aergo::module::InputChannelMapInfo channel_map_info, ConsumerType consumer_type)
{
    auto& running_module = running_modules_[module_id];
    const aergo::module::ModuleInfo* module_info = (*running_module->module_loader_data_)->readModuleInfo();

    uint32_t module_info_consumers_count;
    const aergo::module::communication_channel::Consumer* module_info_consumers;

    if (consumer_type == ConsumerType::SUBSCRIBE)
    {
        module_info_consumers_count = module_info->subscribe_consumer_count_;
        module_info_consumers = module_info->subscribe_consumers_;
    }
    else if (consumer_type == ConsumerType::REQUEST)
    {
        module_info_consumers_count = module_info->request_consumer_count_;
        module_info_consumers = module_info->request_consumers_;
    }
    else
    {
        log(aergo::module::logging::LogType::ERROR, "Unexpected enum type in registerToProducersAutoAll, terminating!");
        std::terminate();
    }

    for (uint32_t channel_id = 0; channel_id < module_info_consumers_count; ++channel_id)
    {
        aergo::module::communication_channel::Consumer consumer_info = module_info_consumers[channel_id];
        if (consumer_info.count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
        {
            const std::vector<aergo::module::ChannelIdentifier>& existing_channels = (consumer_type == ConsumerType::SUBSCRIBE) ? getExistingPublishChannelsImpl(consumer_info.channel_type_identifier_) : getExistingResponseChannelsImpl(consumer_info.channel_type_identifier_);
            for (aergo::module::ChannelIdentifier producer_channel_identifier : existing_channels)
            {
                if (producer_channel_identifier.module_id_ >= running_modules_.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid producer_channel_identifier in registerToProducersAutoAll, module id out of bounds.");
                    std::terminate();
                }

                aergo::core::structures::ModuleData* producer_module_data = running_modules_[producer_channel_identifier.module_id_].get();
                if (producer_module_data == nullptr)
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid producer_channel_identifier in registerToProducersAutoAll, source module is already destroyed.");
                    std::terminate();
                }

                auto& mapping_producer = (consumer_type == ConsumerType::SUBSCRIBE) ? producer_module_data->mapping_publish_ : producer_module_data->mapping_response_;
                auto& mapping_consumer = (consumer_type == ConsumerType::SUBSCRIBE) ? running_module->mapping_subscribe_ : running_module->mapping_request_;

                if (producer_channel_identifier.local_channel_id_ >= mapping_producer.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid producer_channel_identifier in registerToProducersAutoAll, channel id out of bounds.");
                    std::terminate();
                }
                
                mapping_producer[producer_channel_identifier.local_channel_id_].push_back({
                    .module_id_ = module_id,
                    .local_channel_id_ = channel_id
                });

                mapping_consumer[channel_id].push_back(producer_channel_identifier);
            }
        }
    }
}



void Core::registerToConsumersAutoAll(uint64_t module_id, aergo::module::InputChannelMapInfo channel_map_info, ConsumerType consumer_type)
{
    auto& running_module = running_modules_[module_id];
    const aergo::module::ModuleInfo* module_info = (*running_module->module_loader_data_)->readModuleInfo();

    uint32_t module_info_producer_count_;
    const aergo::module::communication_channel::Producer* module_info_producers;

    if (consumer_type == ConsumerType::SUBSCRIBE)
    {
        module_info_producer_count_ = module_info->publish_producer_count_;
        module_info_producers = module_info->publish_producers_;
    }
    else if (consumer_type == ConsumerType::REQUEST)
    {
        module_info_producer_count_ = module_info->response_producer_count_;
        module_info_producers = module_info->response_producers_;
    }
    else
    {
        log(aergo::module::logging::LogType::ERROR, "Unexpected enum type in registerToConsumersAutoAll, terminating!");
        std::terminate();
    }

    for (uint32_t channel_id = 0; channel_id < module_info_producer_count_; ++channel_id)
    {
        const aergo::module::communication_channel::Producer producer_info = module_info_producers[channel_id];

        auto& existing_consumer_auto_all_channels = (consumer_type == ConsumerType::SUBSCRIBE) ? existing_subscribe_auto_all_channels_ : existing_request_auto_all_channels_;

        auto it = existing_consumer_auto_all_channels.find(producer_info.channel_type_identifier_);
        if (it != existing_consumer_auto_all_channels.end())
        {
            for (aergo::module::ChannelIdentifier other_channel_id : it->second)
            {
                if (other_channel_id.module_id_ >= running_modules_.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid other_channel_id in registerToConsumersAutoAll, module id out of bounds.");
                    std::terminate();
                }

                aergo::core::structures::ModuleData* consumer_module_data = running_modules_[other_channel_id.module_id_].get();
                if (consumer_module_data == nullptr)
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid other_channel_id in registerToConsumersAutoAll, source module is already destroyed.");
                    std::terminate();
                }

                auto& mapping_producer = (consumer_type == ConsumerType::SUBSCRIBE) ? running_module->mapping_publish_ : running_module->mapping_response_;
                auto& mapping_consumer = (consumer_type == ConsumerType::SUBSCRIBE) ? consumer_module_data->mapping_subscribe_ : consumer_module_data->mapping_request_;

                if (other_channel_id.local_channel_id_ >= mapping_consumer.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid other_channel_id in registerToConsumersAutoAll, channel id out of bounds.");
                    std::terminate();
                }

                mapping_consumer[other_channel_id.local_channel_id_].push_back({
                    .module_id_ = module_id,
                    .local_channel_id_ = channel_id
                });
                
                mapping_producer[channel_id].push_back(other_channel_id);
            }
        }
    }
}



void Core::log(aergo::module::logging::LogType log_type, const char* message)
{
    logger_->log(logging::SourceType::CORE, nullptr, 0, log_type, message);
}



const aergo::module::ModuleInfo* Core::getLoadedModulesInfo(uint64_t loaded_module_id) noexcept
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    if (loaded_module_id < loaded_modules_.size())
    {
        return loaded_modules_[loaded_module_id]->readModuleInfo();
    }
    else
    {
        return nullptr;
    }
}



uint64_t Core::getLoadedModulesCount() noexcept
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    return (uint64_t)loaded_modules_.size();
}



structures::ModuleData* Core::getCreatedModulesInfo(uint64_t running_module_id)
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    if (running_module_id < running_modules_.size())
    {
        return running_modules_[running_module_id].get();
    }
    else
    {
        return nullptr;
    }
}



uint64_t Core::getCreatedModulesCount()
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    return (uint64_t)running_modules_.size();
}



uint64_t Core::getModulesMappingStateId() noexcept
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    return module_mapping_state_id_;
}



const std::vector<aergo::module::ChannelIdentifier>& Core::getExistingPublishChannels(const char* channel_type_identifier)
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);
    return getExistingPublishChannelsImpl(channel_type_identifier);
}



const std::vector<aergo::module::ChannelIdentifier>& Core::getExistingResponseChannels(const char* channel_type_identifier)
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);
    return getExistingResponseChannelsImpl(channel_type_identifier);
}



const std::vector<aergo::module::ChannelIdentifier>& Core::getExistingPublishChannelsImpl(const char* channel_type_identifier)
{
    static const std::vector<aergo::module::ChannelIdentifier> empty{};

    auto it = existing_publish_channels_.find(channel_type_identifier);
    if (it != existing_publish_channels_.end())
    {
        return it->second;
    }
    else
    {
        return empty;
    }
}



const std::vector<aergo::module::ChannelIdentifier>& Core::getExistingResponseChannelsImpl(const char* channel_type_identifier)
{
    static const std::vector<aergo::module::ChannelIdentifier> empty{};

    auto it = existing_response_channels_.find(channel_type_identifier);
    if (it != existing_response_channels_.end())
    {
        return it->second;
    }
    else
    {
        return empty;
    }
}



Core::RemoveResult Core::removeModule(uint64_t id, bool recursive)
{
    std::lock_guard<std::mutex> add_remove_lock(add_remove_mutex_);
    std::unique_lock<std::shared_mutex> lock(core_mutex_);

    return removeModuleImpl(id, recursive, lock);
}



Core::RemoveResult Core::removeModuleImpl(uint64_t id, bool recursive, std::unique_lock<std::shared_mutex>& external_lock)
{
    if (id >= running_modules_.size() || running_modules_[id].get() == nullptr)
    {
        return Core::RemoveResult::DOES_NOT_EXIST;
    }
    
    std::vector<uint64_t> dependent_modules = collectDependentModulesImpl(id);

    if (dependent_modules.size() > 1 && !recursive)
    {
        return Core::RemoveResult::HAS_DEPENDENCIES;
    }

    bool stop_success = true;
    for (size_t dependent_modules_id_p_1 = dependent_modules.size(); dependent_modules_id_p_1 > 0; --dependent_modules_id_p_1)
    {
        size_t dependent_modules_id = dependent_modules_id_p_1 - 1;
        uint64_t module_id = dependent_modules[dependent_modules_id];

        // remove the module first, before removing mappings (otherwise module may still try to send messages)
        if (running_modules_[module_id] != nullptr)
        {
            running_modules_[module_id]->destruction_in_progress_ = true;
            // TODO may be a race condition here
            external_lock.unlock(); // we need to unlock here, because module may be stuck on core_mutex_ lock
            bool res = running_modules_[module_id]->module_->threadStop(defaults::module_thread_timeout_ms_);
            if (!res)
            {
                logger_->log(
                    logging::SourceType::CORE,
                    nullptr,
                    0,
                    aergo::module::logging::LogType::WARNING,
                    "Module failed to stop within timeout during removal, continued removal may cause undefined behavior."
                );
            }
            stop_success = stop_success && res;    // stop_success: T->{T,F}; F->F (never F->T)
            running_modules_[module_id]->module_ = nullptr; // only if stop successful? 
            external_lock.lock();
        }

        removeMappingProducers(module_id, ConsumerType::SUBSCRIBE);
        removeMappingProducers(module_id, ConsumerType::REQUEST);
        removeMappingSubscribers(module_id, ConsumerType::SUBSCRIBE);
        removeMappingSubscribers(module_id, ConsumerType::REQUEST);
        
        auto module_data = running_modules_[module_id].get();
        auto module_info = (*module_data->module_loader_data_)->readModuleInfo();

        removeFromExistingMap(
            module_id, module_info->publish_producer_count_, 
            [module_info](uint32_t channel_id) { return std::make_pair(
                module_info->publish_producers_[channel_id].channel_type_identifier_, 
                true
            ); }, 
            existing_publish_channels_
        );
        removeFromExistingMap(
            module_id, module_info->response_producer_count_, 
            [module_info](uint32_t channel_id) { return std::make_pair(
                module_info->response_producers_[channel_id].channel_type_identifier_,
                true
            ); }, 
            existing_response_channels_
        );
        removeFromExistingMap(
            module_id, module_info->subscribe_consumer_count_, 
            [module_info](uint32_t channel_id) { return std::make_pair(
                module_info->subscribe_consumers_[channel_id].channel_type_identifier_,
                module_info->subscribe_consumers_[channel_id].count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL
            ); }, 
            existing_subscribe_auto_all_channels_
        );
        removeFromExistingMap(
            module_id, module_info->request_consumer_count_, 
            [module_info](uint32_t channel_id) { return std::make_pair(
                module_info->request_consumers_[channel_id].channel_type_identifier_,
                module_info->request_consumers_[channel_id].count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL
            ); }, 
            existing_request_auto_all_channels_
        );

        running_modules_[module_id] = nullptr;
    }

    ++module_mapping_state_id_;

    if (!stop_success)
    {
        return Core::RemoveResult::FAILED_TO_STOP_THREADS;   
    }

    return Core::RemoveResult::SUCCESS;
}



std::vector<uint64_t> Core::collectDependentModules(uint64_t id)
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    if (id >= running_modules_.size() || running_modules_[id].get() == nullptr)
    {
        return std::vector<uint64_t>();
    }

    return collectDependentModulesImpl(id);
}



std::vector<uint64_t> Core::collectDependentModulesImpl(uint64_t id)
{
    std::vector<uint64_t> dependent_modules;
    std::map<uint64_t, size_t> explored_modules;
    dependent_modules.push_back(id);

    for (size_t i = 0; i < dependent_modules.size(); ++i)
    {
        uint64_t module_id = dependent_modules[i];
        auto it = explored_modules.find(module_id);
        if (it != explored_modules.end())
        {
            it->second = i;
            continue;
        }

        structures::ModuleData* module_data = running_modules_[module_id].get();

        collectDependentModulesHelper(module_data, dependent_modules, ConsumerType::SUBSCRIBE);
        collectDependentModulesHelper(module_data, dependent_modules, ConsumerType::REQUEST);

        explored_modules[module_id] = i;
    }

    std::vector<uint64_t> result;
    result.reserve(dependent_modules.size());
    for (size_t i = 0; i < dependent_modules.size(); ++i)
    {
        uint64_t module_id = dependent_modules[i];
        if (explored_modules[module_id] == i)
        {
            result.push_back(module_id);
        }
    }

    return result;
}



void Core::collectDependentModulesHelper(structures::ModuleData* module_data, std::vector<uint64_t>& dependent_modules, ConsumerType consumer_type)
{
    std::vector<std::vector<aergo::module::ChannelIdentifier>>& mapping_producer = (consumer_type == ConsumerType::SUBSCRIBE) ? module_data->mapping_publish_ : module_data->mapping_response_;

    for (auto& connected_channels : mapping_producer)
    {
        for (auto channel_identifier : connected_channels)
        {
            if (channel_identifier.module_id_ >= running_modules_.size())
            {
                log(aergo::module::logging::LogType::ERROR, "module_id_ too large in collectDependentModulesHelper, terminating!");
                std::terminate();
            }
            if (running_modules_[channel_identifier.module_id_].get() == nullptr)
            {
                log(aergo::module::logging::LogType::ERROR, "module_id_ references destroyed module in collectDependentModulesHelper, terminating!");
                std::terminate();
            }

            const aergo::module::ModuleInfo* other_module_info = (*running_modules_[channel_identifier.module_id_]->module_loader_data_)->readModuleInfo();

            uint32_t consumer_count = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->subscribe_consumer_count_ : other_module_info->request_consumer_count_;
            const aergo::module::communication_channel::Consumer* consumers = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->subscribe_consumers_ : other_module_info->request_consumers_;

            if (channel_identifier.local_channel_id_ >= consumer_count)
            {
                log(aergo::module::logging::LogType::ERROR, "local_channel_id_ too large in hasDependenciesHelper, terminating!");
                std::terminate();
            }

            if (consumers[channel_identifier.local_channel_id_].count_ != aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
            {
                dependent_modules.push_back(channel_identifier.module_id_);
            }
        }
    }
}



void Core::removeMappingProducers(uint64_t module_id, ConsumerType consumer_type)
{
    structures::ModuleData* module_data = running_modules_[module_id].get();

    std::vector<std::vector<aergo::module::ChannelIdentifier>>& mapping_producer = (consumer_type == ConsumerType::SUBSCRIBE) ? module_data->mapping_publish_ : module_data->mapping_response_;

    for (size_t channel_id = 0; channel_id < mapping_producer.size(); ++channel_id)
    {
        for (auto other_channel_identifier : mapping_producer[channel_id])
        {
            if (other_channel_identifier.module_id_ >= running_modules_.size())
            {
                log(aergo::module::logging::LogType::ERROR, "module_id_ too large in removeMappingProducers, terminating!");
                std::terminate();
            }
            if (running_modules_[other_channel_identifier.module_id_].get() == nullptr)
            {
                log(aergo::module::logging::LogType::ERROR, "module_id_ references destroyed module in removeMappingProducers, terminating!");
                std::terminate();
            }

            structures::ModuleData* other_module_data = running_modules_[other_channel_identifier.module_id_].get();
            const aergo::module::ModuleInfo* other_module_info = (*other_module_data->module_loader_data_)->readModuleInfo();
            aergo::module::ChannelIdentifier our_channel {
                .module_id_ = module_id,
                .local_channel_id_ = (uint32_t)channel_id
            };

            std::vector<std::vector<aergo::module::ChannelIdentifier>>& mapping_consumer = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_data->mapping_subscribe_ : other_module_data->mapping_request_;
            uint32_t consumer_count = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->subscribe_consumer_count_ : other_module_info->request_consumer_count_;
            const aergo::module::communication_channel::Consumer* consumers = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->subscribe_consumers_ : other_module_info->request_consumers_;

            if (other_channel_identifier.local_channel_id_ >= mapping_consumer.size() || other_channel_identifier.local_channel_id_ >= consumer_count)
            {
                log(aergo::module::logging::LogType::ERROR, "local_channel_id_ too large in removeMappingProducers, terminating!");
                std::terminate();
            }

            if (consumers[other_channel_identifier.local_channel_id_].count_ != aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
            {
                log(aergo::module::logging::LogType::ERROR, "count_ not AUTO_ALL in removeMappingProducers, terminating!");
                std::terminate();
            }

            std::vector<aergo::module::ChannelIdentifier>& single_channel_map = mapping_consumer[other_channel_identifier.local_channel_id_];
            auto it = std::find(single_channel_map.begin(), single_channel_map.end(), our_channel);
            
            if (it == single_channel_map.end())
            {
                log(aergo::module::logging::LogType::ERROR, "our_channel not found in other_module in removeMappingProducers, terminating!");
                std::terminate();
            }

            single_channel_map.erase(it);
        }
    }
}



void Core::removeMappingSubscribers(uint64_t module_id, ConsumerType consumer_type)
{
    structures::ModuleData* module_data = running_modules_[module_id].get();

    std::vector<std::vector<aergo::module::ChannelIdentifier>>& mapping_consumer = (consumer_type == ConsumerType::SUBSCRIBE) ? module_data->mapping_subscribe_ : module_data->mapping_request_;

    for (size_t channel_id = 0; channel_id < mapping_consumer.size(); ++channel_id)
    {
        for (auto other_channel_identifier : mapping_consumer[channel_id])
        {
            if (other_channel_identifier.module_id_ >= running_modules_.size())
            {
                log(aergo::module::logging::LogType::ERROR, "module_id_ too large in removeMappingProducers, terminating!");
                std::terminate();
            }
            if (running_modules_[other_channel_identifier.module_id_].get() == nullptr)
            {
                log(aergo::module::logging::LogType::ERROR, "module_id_ references destroyed module in removeMappingProducers, terminating!");
                std::terminate();
            }

            structures::ModuleData* other_module_data = running_modules_[other_channel_identifier.module_id_].get();
            const aergo::module::ModuleInfo* other_module_info = (*other_module_data->module_loader_data_)->readModuleInfo();
            aergo::module::ChannelIdentifier our_channel {
                .module_id_ = module_id,
                .local_channel_id_ = (uint32_t)channel_id
            };

            std::vector<std::vector<aergo::module::ChannelIdentifier>>& other_mapping_producer = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_data->mapping_publish_ : other_module_data->mapping_response_;

            if (other_channel_identifier.local_channel_id_ >= other_mapping_producer.size())
            {
                log(aergo::module::logging::LogType::ERROR, "local_channel_id_ too large in removeMappingProducers, terminating!");
                std::terminate();
            }

            std::vector<aergo::module::ChannelIdentifier>& single_channel_map = other_mapping_producer[other_channel_identifier.local_channel_id_];
            auto it = std::find(single_channel_map.begin(), single_channel_map.end(), our_channel);
            
            if (it == single_channel_map.end())
            {
                log(aergo::module::logging::LogType::ERROR, "our_channel not found in other_module in removeMappingProducers, terminating!");
                std::terminate();
            }

            single_channel_map.erase(it);
        }
    }
}



void Core::removeFromExistingMap(uint64_t module_id, uint32_t channel_count, std::function<std::pair<const char*, bool>(uint32_t)> channel_type_identifier_function, std::map<std::string, std::vector<aergo::module::ChannelIdentifier>>& existing_channels)
{
    for (uint32_t channel_id = 0; channel_id < channel_count; ++channel_id)
    {
        std::pair<const char*, bool> channel_info = channel_type_identifier_function(channel_id);

        if (!channel_info.second) // does not need to be removed
        {
            continue;
        }

        const char* channel_type_identifier = channel_info.first;
        auto it = existing_channels.find(channel_type_identifier);

        if (it == existing_channels.end())
        {
            log(aergo::module::logging::LogType::ERROR, "channel_type_identifier not found in existing_producer_channels in removeFromExistingMap, terminating!");
            std::terminate();
        }

        auto& single_channel = it->second;
        single_channel.erase(std::remove_if(single_channel.begin(), single_channel.end(), [module_id](const aergo::module::ChannelIdentifier channel_identifier) {
            return channel_identifier.module_id_ == module_id;
        }));
    }
}



bool Core::addModule(uint64_t loaded_module_id, aergo::module::InputChannelMapInfo channel_map_info) noexcept
{
    std::lock_guard<std::mutex> add_remove_lock(add_remove_mutex_);
    std::unique_lock<std::shared_mutex> lock(core_mutex_);

    return addModuleImpl(loaded_module_id, channel_map_info);
}



bool Core::addModuleImpl(uint64_t loaded_module_id, aergo::module::InputChannelMapInfo channel_map_info) noexcept
{
    if (loaded_module_id >= loaded_modules_.size())
    {
        return false;
    }

    if (!checkChannelMapValidity(channel_map_info, loaded_modules_[loaded_module_id]->readModuleInfo()))
    {
        return false;
    }

    bool res = createAndStartModule(loaded_module_id, channel_map_info, defaults::module_thread_timeout_ms_);
    if (res)
    {
        ++module_mapping_state_id_;
    }

    return res;
}



bool Core::checkChannelMapValidity(aergo::module::InputChannelMapInfo channel_map_info, const aergo::module::ModuleInfo* module_info)
{
    if (!checkChannelMapValidityArrayCheck(channel_map_info, module_info, ConsumerType::REQUEST))
    {
        return false;
    }

    if (!checkChannelMapValidityArrayCheck(channel_map_info, module_info, ConsumerType::SUBSCRIBE))
    {
        return false;
    }

    return true;
}


bool Core::checkChannelMapValidityArrayCheck(
    aergo::module::InputChannelMapInfo& channel_map_info, const aergo::module::ModuleInfo* module_info, ConsumerType consumer_type
)
{
    uint32_t channel_map_consumers_count, module_info_consumers_count;
    aergo::module::InputChannelMapInfo::IndividualChannelInfo* channel_map_consumers;
    const aergo::module::communication_channel::Consumer* module_info_consumers;

    if (consumer_type == ConsumerType::SUBSCRIBE)
    {
        channel_map_consumers_count = channel_map_info.subscribe_consumer_info_count_;
        channel_map_consumers = channel_map_info.subscribe_consumer_info_;

        module_info_consumers_count = module_info->subscribe_consumer_count_;
        module_info_consumers = module_info->subscribe_consumers_;
    }
    else if (consumer_type == ConsumerType::REQUEST)
    {
        channel_map_consumers_count = channel_map_info.request_consumer_info_count_;
        channel_map_consumers = channel_map_info.request_consumer_info_;

        module_info_consumers_count = module_info->request_consumer_count_;
        module_info_consumers = module_info->request_consumers_;
    }
    else
    {        
        log(aergo::module::logging::LogType::ERROR, "Unexpected enum type in checkChannelMapValidityArrayCheck, terminating!");
        std::terminate();
    }



    if (channel_map_consumers_count > 0 && channel_map_consumers == nullptr)
    {
        return false;
    }

    if (channel_map_consumers_count != module_info_consumers_count)
    {
        return false;
    }

    for (uint32_t consumer_id = 0; consumer_id < channel_map_consumers_count; ++consumer_id)
    {
        auto module_info_consumer = module_info_consumers[consumer_id];
        auto channel_map_consumer = channel_map_consumers[consumer_id];

        switch (module_info_consumer.count_)
        {
        case aergo::module::communication_channel::Consumer::Count::SINGLE:
            if (channel_map_consumer.channel_identifier_count_ != 1)
            {
                return false;
            }
            break;
        case aergo::module::communication_channel::Consumer::Count::RANGE:
            if (channel_map_consumer.channel_identifier_count_ < module_info_consumer.min_
            || channel_map_consumer.channel_identifier_count_ > module_info_consumer.max_)
            {
                return false;
            }
            break;
        case aergo::module::communication_channel::Consumer::Count::AUTO_ALL:
            if (channel_map_consumer.channel_identifier_count_ != 0)
            {
                return false;
            }
            break;
        default:
            log(aergo::module::logging::LogType::ERROR, "Unexpected enum type in checkChannelMapValidity, terminating!");
            std::terminate();
            break;
        }


        if (channel_map_consumers[consumer_id].channel_identifier_count_ > 0 && channel_map_consumers[consumer_id].channel_identifier_ == nullptr)
        {
            return false;
        }

        const char* expected_type_identifier = module_info_consumer.channel_type_identifier_;
        for (uint32_t channel_id = 0; channel_id < channel_map_consumers[consumer_id].channel_identifier_count_; ++channel_id)
        {
            aergo::module::ChannelIdentifier channel_identifier = channel_map_consumers[consumer_id].channel_identifier_[channel_id];
            
            if (channel_identifier.module_id_ >= running_modules_.size() || running_modules_[channel_identifier.module_id_].get() == nullptr)
            {
                return false;
            }

            structures::ModuleData* other_module_data = running_modules_[channel_identifier.module_id_].get();
            const aergo::module::ModuleInfo* other_module_info = (*other_module_data->module_loader_data_)->readModuleInfo();

            uint32_t producers_count = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->publish_producer_count_ : other_module_info->response_producer_count_;
            const aergo::module::communication_channel::Producer* producers = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->publish_producers_ : other_module_info->response_producers_;

            if (channel_identifier.local_channel_id_ >= producers_count)
            {
                return false;
            }

            const char* producer_type_identifier = producers[channel_identifier.local_channel_id_].channel_type_identifier_;
            if (std::strcmp(expected_type_identifier, producer_type_identifier) != 0)
            {
                return false;
            }
        }
    }

    return true;
}



void Core::sendMessage(aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    if (source_channel.module_id_ >= running_modules_.size() || running_modules_[source_channel.module_id_].get() == nullptr || running_modules_[source_channel.module_id_]->destruction_in_progress_)
    {
        log(aergo::module::logging::LogType::WARNING, "Module identified by module_id_ does not exist, discarding message, in sendMessage");
        return;
    }

    auto module_data = running_modules_[source_channel.module_id_].get();

    if (source_channel.local_channel_id_ >= module_data->mapping_publish_.size())
    {
        log(aergo::module::logging::LogType::WARNING, "Channel identified by local_channel_id_ does not exist, discarding message, in sendMessage");
        return;
    }

    for (auto other_channel_id : module_data->mapping_publish_[source_channel.local_channel_id_])
    {
        if (other_channel_id.module_id_ >= running_modules_.size() || running_modules_[other_channel_id.module_id_].get() == nullptr || running_modules_[other_channel_id.module_id_]->destruction_in_progress_)
        {
            log(aergo::module::logging::LogType::WARNING, "Other module identified by module_id_ does not exist, in sendMessage");
            continue;
        }

        auto other_module_data = running_modules_[other_channel_id.module_id_].get();

        if (other_channel_id.local_channel_id_ >= other_module_data->mapping_subscribe_.size())
        {
            log(aergo::module::logging::LogType::WARNING, "Other channel identified by local_channel_id_ does not exist, in sendMessage");
            continue;
        }

        other_module_data->module_->processMessage(other_channel_id.local_channel_id_, source_channel, message);
    }
}



void Core::sendResponse(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    if (source_channel.module_id_ >= running_modules_.size() || running_modules_[source_channel.module_id_].get() == nullptr || running_modules_[source_channel.module_id_]->destruction_in_progress_
     || target_channel.module_id_ >= running_modules_.size() || running_modules_[target_channel.module_id_].get() == nullptr || running_modules_[target_channel.module_id_]->destruction_in_progress_)
    {
        log(aergo::module::logging::LogType::WARNING, "Source or target module identified by module_id_ does not exist, discarding message, in sendResponse");
        return;
    }

    auto source_module_data = running_modules_[source_channel.module_id_].get();
    auto target_module_data = running_modules_[target_channel.module_id_].get();

    if (source_channel.local_channel_id_ >= source_module_data->mapping_response_.size() || target_channel.local_channel_id_ >= target_module_data->mapping_request_.size())
    {
        log(aergo::module::logging::LogType::WARNING, "Source or target channel identified by local_channel_id_ does not exist, discarding message, in sendResponse");
        return;
    }
    
    target_module_data->module_->processResponse(target_channel.local_channel_id_, source_channel, message);
}



void Core::sendRequest(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    if (source_channel.module_id_ >= running_modules_.size() || running_modules_[source_channel.module_id_].get() == nullptr || running_modules_[source_channel.module_id_]->destruction_in_progress_
     || target_channel.module_id_ >= running_modules_.size() || running_modules_[target_channel.module_id_].get() == nullptr || running_modules_[target_channel.module_id_]->destruction_in_progress_)
    {
        sendResponse(target_channel, source_channel, { // send failure response
            .data_ = nullptr,
            .data_len_ = 0,
            .blobs_ = nullptr,
            .blob_count_ = 0,
            .id_ = message.id_,
            .timestamp_ns_ = nowNs(),
            .success_ = false
        });


        log(aergo::module::logging::LogType::WARNING, "Source or target module identified by module_id_ does not exist, discarding message, in sendRequest");
        return;
    }

    auto source_module_data = running_modules_[source_channel.module_id_].get();
    auto target_module_data = running_modules_[target_channel.module_id_].get();

    if (source_channel.local_channel_id_ >= source_module_data->mapping_request_.size() || target_channel.local_channel_id_ >= target_module_data->mapping_response_.size())
    {
        sendResponse(target_channel, source_channel, { // send failure response
            .data_ = nullptr,
            .data_len_ = 0,
            .blobs_ = nullptr,
            .blob_count_ = 0,
            .id_ = message.id_,
            .timestamp_ns_ = nowNs(),
            .success_ = false
        });

        log(aergo::module::logging::LogType::WARNING, "Source or target channel identified by local_channel_id_ does not exist, discarding message, in sendRequest");
        return;
    }
    
    target_module_data->module_->processRequest(target_channel.local_channel_id_, source_channel, message);
}



aergo::module::IAllocator* Core::createDynamicAllocator() noexcept
{
    auto allocator = std::make_unique<memory_allocation::DynamicAllocator>(logger_);
    auto allocator_wrapper = std::make_unique<memory_allocation::AllocatorWrapper>(std::move(allocator));
    aergo::module::IAllocator* raw_ptr = allocator_wrapper.get();
    {
        std::unique_lock<std::mutex> lock(allocator_mutex_);
        allocators_.push_back(std::move(allocator_wrapper));
    }
    return raw_ptr;
}



aergo::module::IAllocator* Core::createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots) noexcept
{
    auto allocator = std::make_unique<memory_allocation::StaticAllocator>(slot_size_bytes, number_of_slots, logger_);
    auto allocator_wrapper = std::make_unique<memory_allocation::AllocatorWrapper>(std::move(allocator));
    aergo::module::IAllocator* raw_ptr = allocator_wrapper.get();
    {
        std::unique_lock<std::mutex> lock(allocator_mutex_);
        allocators_.push_back(std::move(allocator_wrapper));
    }
    return raw_ptr;
}



void Core::deleteAllocator(aergo::module::IAllocator* allocator) noexcept
{
    std::unique_lock<std::mutex> lock(allocator_mutex_);

    auto it = std::find_if(allocators_.begin(), allocators_.end(), [allocator](auto& ptr) { return allocator == ptr.get(); });

    if (it != allocators_.end())
    {
        allocators_.erase(it);
    }
    else
    {
        log(aergo::module::logging::LogType::WARNING, "Attempting to remove non-existing allocator.");
    }
}



aergo::module::RunningModuleInfo Core::getRunningModulesInfo(uint64_t running_module_id) noexcept
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    if (running_module_id >= running_modules_.size() || running_modules_[running_module_id].get() == nullptr)
    {
        return { .exists_ = false };
    }

    auto module_data = running_modules_[running_module_id].get();

    return aergo::module::RunningModuleInfo {
        .exists_ = true,
        .module_info_ = (*module_data->module_loader_data_)->readModuleInfo()
    };
}



aergo::module::message::SharedDataBlob Core::getRunningModulesChannelMap(uint64_t running_module_id) noexcept
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    aergo::core::structures::ModuleData* module_data;
    if (running_module_id < running_modules_.size())
    {
        module_data = running_modules_[running_module_id].get();
    }
    else
    {
        return aergo::module::message::SharedDataBlob(); // return invalid blob
    }

    if (module_data == nullptr)
    {
        return aergo::module::message::SharedDataBlob(); // return invalid blob
    }


    const aergo::module::ModuleInfo* module_info = (*module_data->module_loader_data_)->readModuleInfo();

    uint64_t required_memory = 0;

    required_memory += sizeof(uint32_t); // uint32_t for subscribe_consumer_count_
    for (uint32_t i = 0; i < module_info->subscribe_consumer_count_; ++i)
    {
        required_memory += sizeof(uint32_t); // uint32_t for vector size
        required_memory += module_data->mapping_subscribe_[i].size() * sizeof(aergo::module::ChannelIdentifier); // vector content for single subscriber
    }

    required_memory += sizeof(uint32_t); // uint32_t for request_consumer_count_
    for (uint32_t i = 0; i < module_info->request_consumer_count_; ++i)
    {
        required_memory += sizeof(uint32_t); // uint32_t for vector size
        required_memory += module_data->mapping_request_[i].size() * sizeof(aergo::module::ChannelIdentifier); // vector content for single request consumer
    }

    aergo::module::message::SharedDataBlob blob = core_dynamic_allocator_->allocate(required_memory);
    if (!blob.valid())
    {
        return aergo::module::message::SharedDataBlob(); // return invalid blob
    }

    uint8_t* data_ptr = blob.data();
    uint32_t* data_as_uint32 = reinterpret_cast<uint32_t*>(data_ptr);

    *data_as_uint32 = module_info->subscribe_consumer_count_; // log number of subscribe channels
    ++data_as_uint32;
    for (uint32_t i = 0; i < module_info->subscribe_consumer_count_; ++i)
    {
        *data_as_uint32 = (uint32_t)module_data->mapping_subscribe_[i].size(); // log number of subscribers for this channel
        ++data_as_uint32;
        aergo::module::ChannelIdentifier* data_as_channel_identifier = reinterpret_cast<aergo::module::ChannelIdentifier*>(data_as_uint32);
        for (size_t j = 0; j < module_data->mapping_subscribe_[i].size(); ++j)
        {
            *data_as_channel_identifier = module_data->mapping_subscribe_[i][j];
            ++data_as_channel_identifier;
        }
        data_as_uint32 = reinterpret_cast<uint32_t*>(data_as_channel_identifier);
    }

    *data_as_uint32 = module_info->request_consumer_count_; // log number of request channels
    ++data_as_uint32;
    for (uint32_t i = 0; i < module_info->request_consumer_count_; ++i)
    {
        *data_as_uint32 = (uint32_t)module_data->mapping_request_[i].size(); // log number of request consumers for this channel
        ++data_as_uint32;
        aergo::module::ChannelIdentifier* data_as_channel_identifier = reinterpret_cast<aergo::module::ChannelIdentifier*>(data_as_uint32);
        for (size_t j = 0; j < module_data->mapping_request_[i].size(); ++j)
        {
            *data_as_channel_identifier = module_data->mapping_request_[i][j];
            ++data_as_channel_identifier;
        }
        data_as_uint32 = reinterpret_cast<uint32_t*>(data_as_channel_identifier);
    }

    return blob;
}



bool Core::removeModuleById(uint64_t id, bool recursive) noexcept
{
    return removeModule(id, recursive) == Core::RemoveResult::SUCCESS;
}



uint64_t Core::getRunningModulesCount() noexcept
{
    return getCreatedModulesCount();
}



aergo::module::message::SharedDataBlob Core::collectDependencies(uint64_t id) noexcept
{
    auto dependent_modules = collectDependentModules(id);
    aergo::module::message::SharedDataBlob blob = core_dynamic_allocator_->allocate(sizeof(uint64_t) + sizeof(uint64_t) * dependent_modules.size());
    if (!blob.valid())
    {
        return aergo::module::message::SharedDataBlob(); // return invalid blob
    }

    uint8_t* data_ptr = blob.data();
    uint64_t* data_as_uint64 = reinterpret_cast<uint64_t*>(data_ptr);
    data_as_uint64[0] = (uint64_t)dependent_modules.size();
    for (size_t i = 0; i < dependent_modules.size(); ++i)
    {
        data_as_uint64[i + 1] = dependent_modules[i];
    }

    return blob;
}



aergo::module::message::SharedDataBlob Core::getExistingPublishChannelsByName(const char* channel_type_identifier) noexcept
{
    auto& channels = getExistingPublishChannels(channel_type_identifier);

    std::vector<std::byte> buffer;
    aergo::module::serialize::pushExistingChannels(buffer, channels);

    // returns a copy of the data in SharedDataBlob or an invalid blob on failure
    return core_dynamic_allocator_->allocateFromData(std::span<const std::byte>(buffer.data(), buffer.size()));
}



aergo::module::message::SharedDataBlob Core::getExistingResponseChannelsByName(const char* channel_type_identifier) noexcept
{
    auto& channels = getExistingResponseChannels(channel_type_identifier);
    
    std::vector<std::byte> buffer;
    aergo::module::serialize::pushExistingChannels(buffer, channels);

    // returns a copy of the data in SharedDataBlob or an invalid blob on failure
    return core_dynamic_allocator_->allocateFromData(std::span<const std::byte>(buffer.data(), buffer.size()));
}



aergo::module::message::SharedDataBlob Core::save() noexcept
{
    std::shared_lock<std::shared_mutex> lock(core_mutex_);

    json state_data;
    state_data["format_version"] = FORMAT_VERSION;
    state_data["api_version"] = CORE_API_VERSION;
    state_data["core"]["core_version"] = CORE_VERSION;
    
    state_data["core"]["auto_created_modules"] = json::array();
    state_data["core"]["manual_created_modules"] = json::array();
    state_data["module_states"] = json::object();

    for (const auto& loaded_module : loaded_modules_)
    {
        state_data["core"]["expected_modules"].push_back(loaded_module.getModuleUniqueName());
    }

    std::map<std::string, size_t> running_module_counts;
    std::map<size_t, std::string> module_id_to_name;

    for (size_t running_module_id = 0; running_module_id < running_modules_.size(); ++running_module_id)
    {
        auto& running_modules = running_modules_[running_module_id];
        if (running_modules.get() == nullptr)
        {
            continue;
        }

        auto module_info = (*running_modules->module_loader_data_)->readModuleInfo();

        std::string module_name = running_modules->module_loader_data_->getModuleUniqueName();
        auto it = running_module_counts.find(module_name);
        if (it == running_module_counts.end())
        {
            running_module_counts[module_name] = 0;
            it = running_module_counts.find(module_name);
        }
        else
        {
            ++(it->second);
        }

        std::string instance_name = module_name + "#" + std::to_string(it->second);
        module_id_to_name[running_module_id] = instance_name;

        json module_json;
        module_json["loaded_name"] = module_name;
        module_json["instance_name"] = instance_name;
        module_json["subscribe_channels"] = json::array();
        module_json["request_channels"] = json::array();
        
        for (size_t i = 0; i < running_modules->mapping_subscribe_.size(); ++i)
        {
            json channel_json;
            channel_json["channel_type_identifier"] = module_info->subscribe_consumers_[i].channel_type_identifier_;
            channel_json["auto_all"] = (module_info->subscribe_consumers_[i].count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL);
            channel_json["mappings"] = json::array();
            if (!channel_json["auto_all"])
            {
                for (const auto& channel_identifier : running_modules->mapping_subscribe_[i])
                {
                    json mapping_json;
                    mapping_json["producer_module"] = module_id_to_name[channel_identifier.module_id_];
                    mapping_json["producer_channel_id"] = channel_identifier.local_channel_id_;
                    channel_json["mappings"].push_back(mapping_json);
                }
            }
            module_json["subscribe_channels"].push_back(channel_json);
        }

        for (size_t i = 0; i < running_modules->mapping_request_.size(); ++i)
        {
            json channel_json;
            channel_json["channel_type_identifier"] = module_info->request_consumers_[i].channel_type_identifier_;
            channel_json["auto_all"] = (module_info->request_consumers_[i].count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL);
            channel_json["mappings"] = json::array();
            if (!channel_json["auto_all"])
            {
                for (const auto& channel_identifier : running_modules->mapping_request_[i])
                {
                    json mapping_json;
                    mapping_json["producer_module"] = module_id_to_name[channel_identifier.module_id_];
                    mapping_json["producer_channel_id"] = channel_identifier.local_channel_id_;
                    channel_json["mappings"].push_back(mapping_json);
                }
            }
            module_json["request_channels"].push_back(channel_json);
        }

        if (module_info->auto_create_)
        {
            state_data["core"]["auto_created_modules"].push_back(module_json);
        }
        else
        {
            state_data["core"]["manual_created_modules"].push_back(module_json);
        }
    }

    std::vector<std::tuple<std::string, std::vector<aergo::module::ISerializableModule::SavedBlob>>> modules_binary_data;

    for (size_t running_module_id = 0; running_module_id < running_modules_.size(); ++running_module_id)
    {
        auto& running_module = running_modules_[running_module_id];
        if (running_module.get() == nullptr)
        {
            continue;
        }

        auto module_state_blob = running_module->module_->save(core_dynamic_allocator_.get());
        if (!module_state_blob.valid())
        {
            log(aergo::module::logging::LogType::ERROR, "Module state save failed, aborting core state save!");
            return aergo::module::message::SharedDataBlob(); // return invalid blob
        }
        aergo::module::ISerializableModule::SaveData module_state_data;
        if (!aergo::module::dll::save_toolkit::deserialize(module_state_blob.data(), module_state_blob.size(), module_state_data))
        {
            log(aergo::module::logging::LogType::ERROR, "Module state deserialization failed, aborting core state save!");
            return aergo::module::message::SharedDataBlob(); // return invalid blob
        }
        if (!module_state_data.success_)
        {
            log(aergo::module::logging::LogType::ERROR, "Module state indicates failure, aborting core state save!");
            return aergo::module::message::SharedDataBlob(); // return invalid blob
        }

        json module_state;

        std::string instance_name = module_id_to_name[running_module_id];

        module_state["supports_saving"] = module_state_data.supports_saving_;
        
        if (module_state_data.supports_saving_)
        {
            try
            {
                if (module_state_data.json_header_.empty())
                {
                    module_state["state_json"] = json::parse("{}");
                }
                else
                {
                    module_state["state_json"] = json::parse(module_state_data.json_header_);
                }
            }
            catch (json::parse_error& e)
            {
                log(aergo::module::logging::LogType::ERROR, "Module state JSON parsing failed, aborting core state save!");
                return aergo::module::message::SharedDataBlob(); // return invalid blob
            }
            
            module_state["schema_version"] = module_state_data.schema_version_;

            if (module_state_data.blobs_.size() > 0)
            {
                modules_binary_data.push_back({instance_name, std::move(module_state_data.blobs_)});
            }
        }
        
        state_data["module_states"][instance_name] = module_state;
    }

    std::string log_msg = "State data: " + state_data.dump(4);
    log(aergo::module::logging::LogType::INFO, log_msg.c_str());

    std::vector<uint8_t> serialized_data;
    std::string state_data_str = state_data.dump();
    aergo::module::save_toolkit::serializeSaveState(state_data_str, modules_binary_data, serialized_data);

    aergo::module::message::SharedDataBlob blob = core_dynamic_allocator_->allocate(serialized_data.size());
    if (!blob.valid() || blob.size() < serialized_data.size())
    {
        return aergo::module::message::SharedDataBlob(); // return invalid blob
    }
    
    std::memcpy(blob.data(), serialized_data.data(), serialized_data.size());

    std::string final_log_msg = "Final serialized state size: " + std::to_string(serialized_data.size()) + " bytes.";
    log(aergo::module::logging::LogType::INFO, final_log_msg.c_str());

    return blob;
}



bool Core::load(const uint8_t* data, uint64_t size) noexcept
{
    std::lock_guard<std::mutex> add_remove_lock(add_remove_mutex_);
    std::unique_lock<std::shared_mutex> lock(core_mutex_);

    size_t last_auto_created_module_index = 0;
    // First, stop and remove all existing modules (if not auto-created)
    for (size_t running_module_id = 0; running_module_id < running_modules_.size(); ++running_module_id)
    {
        auto& running_module = running_modules_[running_module_id];
        if (running_module.get() == nullptr)
        {
            continue;
        }

        auto module_info = (*running_module->module_loader_data_)->readModuleInfo();
        if (!module_info->auto_create_)
        {
            removeModuleImpl(running_module_id, true, lock);
        }
        else
        {
            last_auto_created_module_index = running_module_id;
        }
    }

    running_modules_.resize(last_auto_created_module_index + 1); // remove all non-auto-created modules

    std::string state_data_str;
    std::vector<std::tuple<std::string, std::vector<aergo::module::ISerializableModule::SavedBlob>>> modules_binary_data;
    if (!aergo::module::save_toolkit::deserializeSaveState(data, size, state_data_str, modules_binary_data))
    {
        log(aergo::module::logging::LogType::ERROR, "State deserialization failed, aborting core state load!");
        return false;
    }

    json state_data;
    try
    {
        state_data = json::parse(state_data_str);
    }
    catch (json::parse_error& e)
    {
        log(aergo::module::logging::LogType::ERROR, "State JSON parsing failed, aborting core state load!");
        return false;
    }

    if (!state_data.contains("format_version") || !state_data["format_version"].is_number_unsigned()
     || !state_data.contains("api_version") || !state_data["api_version"].is_number_unsigned()
     || !state_data.contains("core") || !state_data["core"].is_object()
     || !state_data["core"].contains("core_version") || !state_data["core"]["core_version"].is_string()
     || !state_data["core"].contains("expected_modules") || !state_data["core"]["expected_modules"].is_array()
     || !state_data["core"].contains("auto_created_modules") || !state_data["core"]["auto_created_modules"].is_array()
     || !state_data["core"].contains("manual_created_modules") || !state_data["core"]["manual_created_modules"].is_array()
     || !state_data.contains("module_states") || !state_data["module_states"].is_object())
    {
        log(aergo::module::logging::LogType::ERROR, "State JSON structure invalid, aborting core state load!");
        return false;
    }

    if (state_data["format_version"].get<uint32_t>() != FORMAT_VERSION)
    {
        log(aergo::module::logging::LogType::ERROR, "State format version mismatch, aborting core state load!");
        return false;
    }

    if (state_data["api_version"].get<uint32_t>() != CORE_API_VERSION)
    {
        log(aergo::module::logging::LogType::ERROR, "State API version mismatch, aborting core state load!");
        return false;
    }

    if (state_data["core"]["core_version"].get<std::string>() != CORE_VERSION)
    {
        std::string log_msg = "State core version (" + state_data["core"]["core_version"].get<std::string>() + ") mismatch with current core version (" + CORE_VERSION + "), may cause issues.";
        log(aergo::module::logging::LogType::WARNING, log_msg.c_str());
    }

    std::map<std::string, uint64_t> loaded_name_to_id; // maps loaded module unique name to loaded module id (used for creating modules in manual creation)
    for (const auto& expected_module : state_data["core"]["expected_modules"])
    {
        if (!expected_module.is_string())
        {
            log(aergo::module::logging::LogType::ERROR, "State JSON structure invalid (expected_modules), aborting core state load!");
            return false;
        }

        std::string expected_module_name = expected_module.get<std::string>();

        bool found = false;
        for (size_t loaded_module_id = 0; loaded_module_id < loaded_modules_.size(); ++loaded_module_id)
        {
            if (loaded_modules_[loaded_module_id].getModuleUniqueName() == expected_module_name)
            {
                found = true;
                loaded_name_to_id[expected_module_name] = loaded_module_id;
                break;
            }
        }

        if (!found)
        {
            std::string log_msg = "Expected module " + expected_module_name + " not loaded, aborting core state load!";
            log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
            return false;
        }
    }

    std::map<std::string, size_t> instance_name_to_running_id; // maps instance name to running module id (used for mapping channels in manual creation)

    // Load data for auto-created modules (these were not removed, so they do not have to be re-created)
    for (const auto& auto_created_module : state_data["core"]["auto_created_modules"])
    {
        if (!auto_created_module.is_object()
         || !auto_created_module.contains("loaded_name") || !auto_created_module["loaded_name"].is_string()
         || !auto_created_module.contains("instance_name") || !auto_created_module["instance_name"].is_string()
         || !auto_created_module.contains("subscribe_channels") || !auto_created_module["subscribe_channels"].is_array()
         || !auto_created_module.contains("request_channels") || !auto_created_module["request_channels"].is_array())
        {
            log(aergo::module::logging::LogType::ERROR, "State JSON structure invalid (auto_created_modules), aborting core state load!");
            return false;
        }

        std::string instance_name = auto_created_module["instance_name"].get<std::string>();
        std::string loaded_name = auto_created_module["loaded_name"].get<std::string>();

        if (!state_data["module_states"].contains(instance_name) || !state_data["module_states"][instance_name].is_object() 
        || !state_data["module_states"][instance_name].contains("supports_saving") || !state_data["module_states"][instance_name]["supports_saving"].is_boolean())
        {
            std::string log_msg = "State for auto-created module " + instance_name + " not found or invalid, aborting core state load!";
            log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
            return false;
        }

        const auto& module_state = state_data["module_states"][instance_name];

        aergo::core::structures::ModuleData* module_data = nullptr;
        for (size_t running_module_id = 0; running_module_id < running_modules_.size(); ++running_module_id)
        {
            auto& running_module = running_modules_[running_module_id];
            if (running_module.get() == nullptr)
            {
                continue;
            }

            auto module_name = running_module->module_loader_data_->getModuleUniqueName();
            if (module_name == loaded_name)
            {
                instance_name_to_running_id[instance_name] = running_module_id;
                module_data = running_module.get();
                break;
            }
        }

        if (module_data == nullptr)
        {
            log(aergo::module::logging::LogType::ERROR, "No loaded module found for auto-created module, aborting core state load!");
            return false;
        }

        aergo::module::ISerializableModule::SaveData module_state_data {
            .success_ = true,
            .supports_saving_ = module_state["supports_saving"].get<bool>()
        };

        if (module_state_data.supports_saving_)
        {
            if (!module_state.contains("schema_version") || !module_state["schema_version"].is_number_unsigned()
             || !module_state.contains("state_json") || !module_state["state_json"].is_object())
            {
                std::string log_msg = "State for auto-created module " + instance_name + " invalid, aborting core state load!";
                log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
                return false;
            }

            module_state_data.schema_version_ = module_state["schema_version"].get<uint32_t>();
            module_state_data.json_header_ = module_state["state_json"].dump();

            auto it = std::find_if(modules_binary_data.begin(), modules_binary_data.end(), [&instance_name](const auto& tuple) {
                return std::get<0>(tuple) == instance_name;
            });

            if (it != modules_binary_data.end())
            {
                module_state_data.blobs_ = std::move(std::get<1>(*it));
            }
        }

        std::vector<uint8_t> serialized_state;
        if (!aergo::module::dll::save_toolkit::serialize(module_state_data, serialized_state))
        {
            std::string log_msg = "State for auto-created module " + instance_name + " serialization failed, aborting core state load!";
            log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
            return false;
        }

        if (!module_data->module_->load(serialized_state.data(), serialized_state.size()))
        {
            std::string log_msg = "State for auto-created module " + instance_name + " load failed, aborting core state load!";
            log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
            return false;
        }
    }


    // Load data for manually created modules (these have to be re-created)
    for (const auto& manual_created_module : state_data["core"]["manual_created_modules"])
    {
        if (!manual_created_module.is_object()
         || !manual_created_module.contains("loaded_name") || !manual_created_module["loaded_name"].is_string()
         || !manual_created_module.contains("instance_name") || !manual_created_module["instance_name"].is_string()
         || !manual_created_module.contains("subscribe_channels") || !manual_created_module["subscribe_channels"].is_array()
         || !manual_created_module.contains("request_channels") || !manual_created_module["request_channels"].is_array())
        {
            log(aergo::module::logging::LogType::ERROR, "State JSON structure invalid (manual_created_modules), aborting core state load!");
            return false;
        }

        std::string instance_name = manual_created_module["instance_name"].get<std::string>();
        std::string loaded_name = manual_created_module["loaded_name"].get<std::string>();

        const auto& it = loaded_name_to_id.find(loaded_name);
        if (it == loaded_name_to_id.end())
        {
            std::string log_msg = "Module " + loaded_name + " can not be created, because the corresponding loaded module does not exist";
            log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
            return false;
        }

        size_t loaded_module_id = it->second;

        
        auto load_mappings = [manual_created_module, instance_name_to_running_id, instance_name, this](const json& json_array, std::vector<std::vector<aergo::module::ChannelIdentifier>>& out_mappings) -> bool
        {
            for (const auto& subscribe_channel : json_array)
            {
                if (!subscribe_channel.is_object()
                || !subscribe_channel.contains("channel_type_identifier") || !subscribe_channel["channel_type_identifier"].is_string()
                || !subscribe_channel.contains("auto_all") || !subscribe_channel["auto_all"].is_boolean()
                || !subscribe_channel.contains("mappings") || !subscribe_channel["mappings"].is_array())
                {
                    log(aergo::module::logging::LogType::ERROR, "State JSON structure invalid (manual_created_modules/consume_channels), aborting core state load!");
                    return false;
                }

                std::string channel_type_identifier = subscribe_channel["channel_type_identifier"].get<std::string>();
                bool auto_all = subscribe_channel["auto_all"].get<bool>();
                std::vector<aergo::module::ChannelIdentifier> mappings;
                if (!auto_all)
                {
                    for (const auto& mapping : subscribe_channel["mappings"])
                    {
                        if (!mapping.is_object()
                        || !mapping.contains("producer_module") || !mapping["producer_module"].is_string()
                        || !mapping.contains("producer_channel_id") || !mapping["producer_channel_id"].is_number_unsigned())
                        {
                            log(aergo::module::logging::LogType::ERROR, "State JSON structure invalid (manual_created_modules/consume_channels/mappings), aborting core state load!");
                            return false;
                        }

                        std::string producer_module_name = mapping["producer_module"].get<std::string>();
                        uint32_t producer_channel_id = mapping["producer_channel_id"].get<uint32_t>();

                        auto it2 = instance_name_to_running_id.find(producer_module_name);
                        if (it2 == instance_name_to_running_id.end())
                        {
                            std::string log_msg = "Mapping for subscribe channel in module " + instance_name + " invalid, aborting core state load!";
                            log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
                            return false;
                        }

                        size_t producer_module_id = it2->second;

                        mappings.push_back(aergo::module::ChannelIdentifier {
                            .module_id_ = producer_module_id,
                            .local_channel_id_ = producer_channel_id
                        });
                    }
                }

                out_mappings.push_back(std::move(mappings));
            }

            return true;
        };

        std::vector<std::vector<aergo::module::ChannelIdentifier>> subscribe_mappings;
        if (!load_mappings(manual_created_module["subscribe_channels"], subscribe_mappings))
        {
            return false;
        }
        
        std::vector<std::vector<aergo::module::ChannelIdentifier>> request_mappings;
        if (!load_mappings(manual_created_module["request_channels"], request_mappings))
        {
            return false;
        }

        std::vector<aergo::module::InputChannelMapInfo::IndividualChannelInfo> subscribe_channel_info;
        for (auto& subscribe_channel : subscribe_mappings)
        {
            subscribe_channel_info.push_back(aergo::module::InputChannelMapInfo::IndividualChannelInfo {
                .channel_identifier_ = subscribe_channel.data(),
                .channel_identifier_count_ = (uint32_t)subscribe_channel.size()
            });
        }
        
        std::vector<aergo::module::InputChannelMapInfo::IndividualChannelInfo> request_channel_info;
        for (auto& request_channel : request_mappings)
        {
            request_channel_info.push_back(aergo::module::InputChannelMapInfo::IndividualChannelInfo {
                .channel_identifier_ = request_channel.data(),
                .channel_identifier_count_ = (uint32_t)request_channel.size()
            });
        }

        aergo::module::InputChannelMapInfo input_channel_map_info {
            .subscribe_consumer_info_ = subscribe_channel_info.data(),
            .subscribe_consumer_info_count_ = (uint32_t)subscribe_channel_info.size(),
            .request_consumer_info_ = request_channel_info.data(),
            .request_consumer_info_count_ = (uint32_t)request_channel_info.size()
        };


        if (!addModuleImpl(loaded_module_id, input_channel_map_info))
        {
            std::string log_msg = "Module " + instance_name + " could not be created, aborting core state load!";
            log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
            return false;
        }

        // register to loaded_name_to_id
        size_t new_running_module_id = running_modules_.size() - 1;
        instance_name_to_running_id[instance_name] = new_running_module_id;

        // load state
        if (!state_data["module_states"].contains(instance_name) || !state_data["module_states"][instance_name].is_object() 
        || !state_data["module_states"][instance_name].contains("supports_saving") || !state_data["module_states"][instance_name]["supports_saving"].is_boolean())
        {
            std::string log_msg = "State for manually created module " + instance_name + " not found or invalid, aborting core state load!";
            log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
            return false;
        }

        const auto& module_state = state_data["module_states"][instance_name];

        aergo::module::ISerializableModule::SaveData module_state_data {
            .success_ = true,
            .supports_saving_ = module_state["supports_saving"].get<bool>()
        };

        if (module_state_data.supports_saving_)
        {
            if (!module_state.contains("schema_version") || !module_state["schema_version"].is_number_unsigned()
             || !module_state.contains("state_json") || !module_state["state_json"].is_object())
            {
                std::string log_msg = "State for manually created module " + instance_name + " invalid, aborting core state load!";
                log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
                return false;
            }

            module_state_data.schema_version_ = module_state["schema_version"].get<uint32_t>();
            module_state_data.json_header_ = module_state["state_json"].dump();

            auto it = std::find_if(modules_binary_data.begin(), modules_binary_data.end(), [&instance_name](const auto& tuple) {
                return std::get<0>(tuple) == instance_name;
            });

            if (it != modules_binary_data.end())
            {
                module_state_data.blobs_ = std::move(std::get<1>(*it));
            }
        }

        std::vector<uint8_t> serialized_state;
        if (!aergo::module::dll::save_toolkit::serialize(module_state_data, serialized_state))
        {
            std::string log_msg = "State for manually created module " + instance_name + " serialization failed, aborting core state load!";
            log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
            return false;
        }

        aergo::core::structures::ModuleData* module_data = running_modules_[new_running_module_id].get();
        if (module_data == nullptr)
        {
            log(aergo::module::logging::LogType::ERROR, "No module data found for manually created module, aborting core state load!");
            return false;
        }

        if (!module_data->module_->load(serialized_state.data(), serialized_state.size()))
        {
            std::string log_msg = "State for manually created module " + instance_name + " load failed, aborting core state load!";
            log(aergo::module::logging::LogType::ERROR, log_msg.c_str());
            return false;
        }
    }


    return true;
}