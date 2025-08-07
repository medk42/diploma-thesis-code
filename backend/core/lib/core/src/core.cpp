#include "core/core.h"
#include "core/defaults.h"
#include "utils/memory_allocation/dynamic_allocator.h"
#include "utils/memory_allocation/static_allocator.h"

#include <cstring>
#include <algorithm>

using namespace aergo::core;



Core::Core(logging::ILogger* logger)
: logger_(logger), initialized_(false), module_mapping_state_id_(0) {}



Core::~Core()
{
    for (auto& module : running_modules_)
    {
        module->module_->threadStop(defaults::module_thread_timeout_ms_);
    }
}



void Core::initialize(const char* modules_dir, const char* data_dir)
{
    std::lock_guard<std::mutex> lock(core_mutex_);

    if (initialized_)
    {
        return;
    }
    initialized_ = true;

    loadModules(modules_dir, data_dir);
    autoCreateModules();
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
                default:
                    log_message += "(UNKNOWN): " + module_filename;
            }
            log(aergo::module::logging::LogType::WARNING, log_message.c_str());
        }
    }
}



void Core::autoCreateModules()
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
                }
            }
            else
            {
                std::string failure_message = std::string("Failed to auto-create module due to invalid mapping (all subscribe/request consumers are required to be AUTO_ALL for auto_create modules): ") + loaded_modules_[i].getModuleUniqueName();
                log(aergo::module::logging::LogType::WARNING, failure_message.c_str());
            }
        }
    }
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
            .producer_module_id_ = module_id,
            .producer_channel_id_ = channel_id
        });
    }

    for (uint32_t channel_id = 0; channel_id < module_info->response_producer_count_; ++channel_id)
    {
        existing_response_channels_[module_info->response_producers_[channel_id].channel_type_identifier_].push_back({
            .producer_module_id_ = module_id,
            .producer_channel_id_ = channel_id
        });
    }

    for (uint32_t channel_id = 0; channel_id < module_info->subscribe_consumer_count_; ++channel_id)
    {
        if (module_info->subscribe_consumers_[channel_id].count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
        {
            existing_subscribe_auto_all_channels_[module_info->subscribe_consumers_[channel_id].channel_type_identifier_].push_back({
                .producer_module_id_ = module_id,
                .producer_channel_id_ = channel_id
            });
        }
    }

    for (uint32_t channel_id = 0; channel_id < module_info->request_consumer_count_; ++channel_id)
    {
        if (module_info->request_consumers_[channel_id].count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
        {
            existing_request_auto_all_channels_[module_info->request_consumers_[channel_id].channel_type_identifier_].push_back({
                .producer_module_id_ = module_id,
                .producer_channel_id_ = channel_id
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

            auto& mapping_producer = (consumer_type == ConsumerType::SUBSCRIBE) ? running_modules_[channel_identifier.producer_module_id_]->mapping_publish_ : running_modules_[channel_identifier.producer_module_id_]->mapping_response_;
            auto& mapping_consumer = (consumer_type == ConsumerType::SUBSCRIBE) ? running_module->mapping_subscribe_ : running_module->mapping_request_;

            mapping_producer[channel_identifier.producer_channel_id_].push_back({
                .producer_module_id_ = module_id,
                .producer_channel_id_ = channel_id
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
                if (producer_channel_identifier.producer_module_id_ >= running_modules_.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid producer_channel_identifier in registerToProducersAutoAll, module id out of bounds.");
                    std::terminate();
                }

                aergo::core::structures::ModuleData* producer_module_data = running_modules_[producer_channel_identifier.producer_module_id_].get();
                if (producer_module_data == nullptr)
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid producer_channel_identifier in registerToProducersAutoAll, source module is already destroyed.");
                    std::terminate();
                }

                auto& mapping_producer = (consumer_type == ConsumerType::SUBSCRIBE) ? producer_module_data->mapping_publish_ : producer_module_data->mapping_response_;
                auto& mapping_consumer = (consumer_type == ConsumerType::SUBSCRIBE) ? running_module->mapping_subscribe_ : running_module->mapping_request_;

                if (producer_channel_identifier.producer_channel_id_ >= mapping_producer.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid producer_channel_identifier in registerToProducersAutoAll, channel id out of bounds.");
                    std::terminate();
                }
                
                mapping_producer[producer_channel_identifier.producer_channel_id_].push_back({
                    .producer_module_id_ = module_id,
                    .producer_channel_id_ = channel_id
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
                if (other_channel_id.producer_module_id_ >= running_modules_.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid other_channel_id in registerToConsumersAutoAll, module id out of bounds.");
                    std::terminate();
                }

                aergo::core::structures::ModuleData* consumer_module_data = running_modules_[other_channel_id.producer_module_id_].get();
                if (consumer_module_data == nullptr)
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid other_channel_id in registerToConsumersAutoAll, source module is already destroyed.");
                    std::terminate();
                }

                auto& mapping_producer = (consumer_type == ConsumerType::SUBSCRIBE) ? running_module->mapping_publish_ : running_module->mapping_response_;
                auto& mapping_consumer = (consumer_type == ConsumerType::SUBSCRIBE) ? consumer_module_data->mapping_subscribe_ : consumer_module_data->mapping_request_;

                if (other_channel_id.producer_channel_id_ >= mapping_consumer.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid other_channel_id in registerToConsumersAutoAll, channel id out of bounds.");
                    std::terminate();
                }

                mapping_consumer[other_channel_id.producer_channel_id_].push_back({
                    .producer_module_id_ = module_id,
                    .producer_channel_id_ = channel_id
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



const aergo::module::ModuleInfo* Core::getLoadedModulesInfo(uint64_t loaded_module_id)
{
    std::lock_guard<std::mutex> lock(core_mutex_);

    if (loaded_module_id < loaded_modules_.size())
    {
        return loaded_modules_[loaded_module_id]->readModuleInfo();
    }
    else
    {
        return nullptr;
    }
}



uint64_t Core::getLoadedModulesCount()
{
    std::lock_guard<std::mutex> lock(core_mutex_);

    return (uint64_t)loaded_modules_.size();
}



structures::ModuleData* Core::getRunningModulesInfo(uint64_t running_module_id)
{
    std::lock_guard<std::mutex> lock(core_mutex_);

    if (running_module_id < running_modules_.size())
    {
        return running_modules_[running_module_id].get();
    }
    else
    {
        return nullptr;
    }
}



uint64_t Core::getRunningModulesCount()
{
    std::lock_guard<std::mutex> lock(core_mutex_);

    return (uint64_t)running_modules_.size();
}



uint64_t Core::getModulesMappingStateId()
{
    std::lock_guard<std::mutex> lock(core_mutex_);

    return module_mapping_state_id_;
}



const std::vector<aergo::module::ChannelIdentifier>& Core::getExistingPublishChannels(const char* channel_type_identifier)
{
    std::lock_guard<std::mutex> lock(core_mutex_);
    return getExistingPublishChannelsImpl(channel_type_identifier);
}



const std::vector<aergo::module::ChannelIdentifier>& Core::getExistingResponseChannels(const char* channel_type_identifier)
{
    std::lock_guard<std::mutex> lock(core_mutex_);
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
    std::lock_guard<std::mutex> lock(core_mutex_);

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
    for (size_t module_id_p_1 = dependent_modules.size(); module_id_p_1 > 0; --module_id_p_1)
    {
        size_t module_id = module_id_p_1 - 1;

        removeMappingProducers(module_id, ConsumerType::SUBSCRIBE);
        removeMappingProducers(module_id, ConsumerType::REQUEST);
        removeMappingSubscribers(module_id, ConsumerType::SUBSCRIBE);
        removeMappingSubscribers(module_id, ConsumerType::REQUEST);
        
        auto module_data = running_modules_[module_id].get();
        auto module_info = (*module_data->module_loader_data_)->readModuleInfo();

        removeFromExistingMap(
            module_id, module_info->publish_producer_count_, 
            [module_info](uint32_t channel_id) { return module_info->publish_producers_[channel_id].channel_type_identifier_; }, 
            existing_publish_channels_
        );
        removeFromExistingMap(
            module_id, module_info->response_producer_count_, 
            [module_info](uint32_t channel_id) { return module_info->response_producers_[channel_id].channel_type_identifier_; }, 
            existing_response_channels_
        );
        removeFromExistingMap(
            module_id, module_info->subscribe_consumer_count_, 
            [module_info](uint32_t channel_id) { return module_info->subscribe_consumers_[channel_id].channel_type_identifier_; }, 
            existing_subscribe_auto_all_channels_
        );
        removeFromExistingMap(
            module_id, module_info->request_consumer_count_, 
            [module_info](uint32_t channel_id) { return module_info->request_consumers_[channel_id].channel_type_identifier_; }, 
            existing_request_auto_all_channels_
        );

        bool res = running_modules_[module_id]->module_->threadStop(defaults::module_thread_timeout_ms_);
        stop_success = stop_success && res;    // stop_success: T->{T,F}; F->F (never F->T)
        running_modules_[module_id] = nullptr; // only if stop successful? 
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
    std::lock_guard<std::mutex> lock(core_mutex_);

    if (id >= running_modules_.size() || running_modules_[id].get() == nullptr)
    {
        return std::vector<uint64_t>();
    }

    return collectDependentModulesImpl(id);
}



std::vector<uint64_t> Core::collectDependentModulesImpl(uint64_t id)
{
    std::vector<uint64_t> dependent_modules;
    dependent_modules.push_back(id);

    for (size_t i = 0; i < dependent_modules.size(); ++i)
    {
        uint64_t module_id = dependent_modules[i];
        structures::ModuleData* module_data = running_modules_[module_id].get();

        collectDependentModulesHelper(module_data, dependent_modules, ConsumerType::SUBSCRIBE);
        collectDependentModulesHelper(module_data, dependent_modules, ConsumerType::REQUEST);
    }

    return dependent_modules;
}



void Core::collectDependentModulesHelper(structures::ModuleData* module_data, std::vector<uint64_t>& dependent_modules, ConsumerType consumer_type)
{
    std::vector<std::vector<aergo::module::ChannelIdentifier>>& mapping_producer = (consumer_type == ConsumerType::SUBSCRIBE) ? module_data->mapping_publish_ : module_data->mapping_response_;

    for (auto& connected_channels : mapping_producer)
    {
        for (auto channel_identifier : connected_channels)
        {
            if (channel_identifier.producer_module_id_ >= running_modules_.size())
            {
                log(aergo::module::logging::LogType::ERROR, "producer_module_id_ too large in collectDependentModulesHelper, terminating!");
                std::terminate();
            }
            if (running_modules_[channel_identifier.producer_module_id_].get() == nullptr)
            {
                log(aergo::module::logging::LogType::ERROR, "producer_module_id_ references destroyed module in collectDependentModulesHelper, terminating!");
                std::terminate();
            }

            const aergo::module::ModuleInfo* other_module_info = (*running_modules_[channel_identifier.producer_module_id_]->module_loader_data_)->readModuleInfo();

            uint32_t consumer_count = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->subscribe_consumer_count_ : other_module_info->request_consumer_count_;
            const aergo::module::communication_channel::Consumer* consumers = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->subscribe_consumers_ : other_module_info->request_consumers_;

            if (channel_identifier.producer_channel_id_ >= consumer_count)
            {
                log(aergo::module::logging::LogType::ERROR, "producer_channel_id_ too large in hasDependenciesHelper, terminating!");
                std::terminate();
            }

            if (consumers[channel_identifier.producer_channel_id_].count_ != aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
            {
                dependent_modules.push_back(channel_identifier.producer_module_id_);
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
            if (other_channel_identifier.producer_module_id_ >= running_modules_.size())
            {
                log(aergo::module::logging::LogType::ERROR, "producer_module_id_ too large in removeMappingProducers, terminating!");
                std::terminate();
            }
            if (running_modules_[other_channel_identifier.producer_module_id_].get() == nullptr)
            {
                log(aergo::module::logging::LogType::ERROR, "producer_module_id_ references destroyed module in removeMappingProducers, terminating!");
                std::terminate();
            }

            structures::ModuleData* other_module_data = running_modules_[other_channel_identifier.producer_module_id_].get();
            const aergo::module::ModuleInfo* other_module_info = (*other_module_data->module_loader_data_)->readModuleInfo();
            aergo::module::ChannelIdentifier our_channel {
                .producer_module_id_ = module_id,
                .producer_channel_id_ = (uint32_t)channel_id
            };

            std::vector<std::vector<aergo::module::ChannelIdentifier>>& mapping_consumer = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_data->mapping_subscribe_ : other_module_data->mapping_request_;
            uint32_t consumer_count = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->subscribe_consumer_count_ : other_module_info->request_consumer_count_;
            const aergo::module::communication_channel::Consumer* consumers = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->subscribe_consumers_ : other_module_info->request_consumers_;

            if (other_channel_identifier.producer_channel_id_ >= mapping_consumer.size() || other_channel_identifier.producer_channel_id_ >= consumer_count)
            {
                log(aergo::module::logging::LogType::ERROR, "producer_channel_id_ too large in removeMappingProducers, terminating!");
                std::terminate();
            }

            if (consumers[other_channel_identifier.producer_channel_id_].count_ != aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
            {
                log(aergo::module::logging::LogType::ERROR, "count_ not AUTO_ALL in removeMappingProducers, terminating!");
                std::terminate();
            }

            std::vector<aergo::module::ChannelIdentifier>& single_channel_map = mapping_consumer[other_channel_identifier.producer_channel_id_];
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
            if (other_channel_identifier.producer_module_id_ >= running_modules_.size())
            {
                log(aergo::module::logging::LogType::ERROR, "producer_module_id_ too large in removeMappingProducers, terminating!");
                std::terminate();
            }
            if (running_modules_[other_channel_identifier.producer_module_id_].get() == nullptr)
            {
                log(aergo::module::logging::LogType::ERROR, "producer_module_id_ references destroyed module in removeMappingProducers, terminating!");
                std::terminate();
            }

            structures::ModuleData* other_module_data = running_modules_[other_channel_identifier.producer_module_id_].get();
            const aergo::module::ModuleInfo* other_module_info = (*other_module_data->module_loader_data_)->readModuleInfo();
            aergo::module::ChannelIdentifier our_channel {
                .producer_module_id_ = module_id,
                .producer_channel_id_ = (uint32_t)channel_id
            };

            std::vector<std::vector<aergo::module::ChannelIdentifier>>& other_mapping_producer = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_data->mapping_publish_ : other_module_data->mapping_response_;

            if (other_channel_identifier.producer_channel_id_ >= other_mapping_producer.size())
            {
                log(aergo::module::logging::LogType::ERROR, "producer_channel_id_ too large in removeMappingProducers, terminating!");
                std::terminate();
            }

            std::vector<aergo::module::ChannelIdentifier>& single_channel_map = other_mapping_producer[other_channel_identifier.producer_channel_id_];
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



void Core::removeFromExistingMap(uint64_t module_id, uint32_t channel_count, std::function<const char*(uint32_t)> channel_type_identifier_function, std::map<std::string, std::vector<aergo::module::ChannelIdentifier>>& existing_channels)
{
    for (uint32_t channel_id = 0; channel_id < channel_count; ++channel_id)
    {
        const char* channel_type_identifier = channel_type_identifier_function(channel_id);
        auto it = existing_channels.find(channel_type_identifier);

        if (it == existing_channels.end())
        {
            log(aergo::module::logging::LogType::ERROR, "channel_type_identifier not found in existing_producer_channels in removeFromExistingMap, terminating!");
            std::terminate();
        }

        auto& single_channel = it->second;
        single_channel.erase(std::remove_if(single_channel.begin(), single_channel.end(), [module_id](const aergo::module::ChannelIdentifier channel_identifier) {
            return channel_identifier.producer_module_id_ == module_id;
        }));
    }
}



bool Core::addModule(uint64_t loaded_module_id, aergo::module::InputChannelMapInfo channel_map_info)
{
    std::lock_guard<std::mutex> lock(core_mutex_);

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



        const char* expected_type_identifier = module_info_consumer.channel_type_identifier_;
        for (uint32_t channel_id = 0; channel_id < channel_map_consumers[consumer_id].channel_identifier_count_; ++channel_id)
        {
            aergo::module::ChannelIdentifier channel_identifier = channel_map_consumers[consumer_id].channel_identifier_[channel_id];
            
            if (channel_identifier.producer_module_id_ >= running_modules_.size() || running_modules_[channel_identifier.producer_module_id_].get() == nullptr)
            {
                return false;
            }

            structures::ModuleData* other_module_data = running_modules_[channel_identifier.producer_module_id_].get();
            const aergo::module::ModuleInfo* other_module_info = (*other_module_data->module_loader_data_)->readModuleInfo();

            uint32_t producers_count = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->publish_producer_count_ : other_module_info->response_producer_count_;
            const aergo::module::communication_channel::Producer* producers = (consumer_type == ConsumerType::SUBSCRIBE) ? other_module_info->publish_producers_ : other_module_info->response_producers_;

            if (channel_identifier.producer_channel_id_ >= producers_count)
            {
                return false;
            }

            const char* producer_type_identifier = producers[channel_identifier.producer_channel_id_].channel_type_identifier_;
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
    std::lock_guard<std::mutex> lock(core_mutex_);

    if (source_channel.producer_module_id_ >= running_modules_.size() || running_modules_[source_channel.producer_module_id_].get() == nullptr)
    {
        log(aergo::module::logging::LogType::WARNING, "Module identified by producer_module_id_ does not exist, discarding message, in sendMessage");
        return;
    }

    auto module_data = running_modules_[source_channel.producer_module_id_].get();

    if (source_channel.producer_channel_id_ >= module_data->mapping_publish_.size())
    {
        log(aergo::module::logging::LogType::WARNING, "Channel identified by producer_channel_id_ does not exist, discarding message, in sendMessage");
        return;
    }

    for (auto other_channel_id : module_data->mapping_publish_[source_channel.producer_channel_id_])
    {
        if (other_channel_id.producer_module_id_ >= running_modules_.size() || running_modules_[other_channel_id.producer_module_id_].get() == nullptr)
        {
            log(aergo::module::logging::LogType::WARNING, "Other module identified by producer_module_id_ does not exist, in sendMessage");
            continue;
        }

        auto other_module_data = running_modules_[other_channel_id.producer_module_id_].get();

        if (other_channel_id.producer_channel_id_ >= other_module_data->mapping_subscribe_.size())
        {
            log(aergo::module::logging::LogType::WARNING, "Other channel identified by producer_channel_id_ does not exist, in sendMessage");
            continue;
        }

        other_module_data->module_->processMessage(other_channel_id.producer_channel_id_, source_channel, message);
    }
}



void Core::sendResponse(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept
{
    std::lock_guard<std::mutex> lock(core_mutex_);

    if (source_channel.producer_module_id_ >= running_modules_.size() || running_modules_[source_channel.producer_module_id_].get() == nullptr
     || target_channel.producer_module_id_ >= running_modules_.size() || running_modules_[target_channel.producer_module_id_].get() == nullptr)
    {
        log(aergo::module::logging::LogType::WARNING, "Source or target module identified by producer_module_id_ does not exist, discarding message, in sendResponse");
        return;
    }

    auto source_module_data = running_modules_[source_channel.producer_module_id_].get();
    auto target_module_data = running_modules_[target_channel.producer_module_id_].get();

    if (source_channel.producer_channel_id_ >= source_module_data->mapping_response_.size() || target_channel.producer_channel_id_ >= target_module_data->mapping_request_.size())
    {
        log(aergo::module::logging::LogType::WARNING, "Source or target channel identified by producer_channel_id_ does not exist, discarding message, in sendResponse");
        return;
    }
    
    target_module_data->module_->processResponse(target_channel.producer_channel_id_, source_channel, message);
}



void Core::sendRequest(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept
{
    std::lock_guard<std::mutex> lock(core_mutex_);

    if (source_channel.producer_module_id_ >= running_modules_.size() || running_modules_[source_channel.producer_module_id_].get() == nullptr
     || target_channel.producer_module_id_ >= running_modules_.size() || running_modules_[target_channel.producer_module_id_].get() == nullptr)
    {
        log(aergo::module::logging::LogType::WARNING, "Source or target module identified by producer_module_id_ does not exist, discarding message, in sendRequest");
        return;
    }

    auto source_module_data = running_modules_[source_channel.producer_module_id_].get();
    auto target_module_data = running_modules_[target_channel.producer_module_id_].get();

    if (source_channel.producer_channel_id_ >= source_module_data->mapping_request_.size() || target_channel.producer_channel_id_ >= target_module_data->mapping_response_.size())
    {
        log(aergo::module::logging::LogType::WARNING, "Source or target channel identified by producer_channel_id_ does not exist, discarding message, in sendRequest");
        return;
    }
    
    target_module_data->module_->processRequest(target_channel.producer_channel_id_, source_channel, message);
}



aergo::module::IAllocatorCore* Core::createDynamicAllocator() noexcept
{
    std::lock_guard<std::mutex> lock(core_mutex_);

    auto allocator = std::make_unique<memory_allocation::DynamicAllocator>(logger_);
    aergo::module::IAllocatorCore* raw_ptr = allocator.get();
    allocators_.push_back(std::move(allocator));
    return raw_ptr;
}



aergo::module::IAllocatorCore* Core::createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots) noexcept
{
    std::lock_guard<std::mutex> lock(core_mutex_);

    auto allocator = std::make_unique<memory_allocation::StaticAllocator>(slot_size_bytes, number_of_slots, logger_);
    aergo::module::IAllocatorCore* raw_ptr = allocator.get();
    allocators_.push_back(std::move(allocator));
    return raw_ptr;
}



void Core::deleteAllocator(aergo::module::IAllocatorCore* allocator) noexcept
{
    std::lock_guard<std::mutex> lock(core_mutex_);

    auto it = std::find_if(allocators_.begin(), allocators_.end(), [allocator](auto& ptr) { return allocator == ptr.get(); });

    if (it != allocators_.end())
    {
        allocators_.erase(it);
    }
}


