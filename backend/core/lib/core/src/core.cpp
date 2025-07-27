#include "core/core.h"
#include "core/defaults.h"

#include <cstring>

using namespace aergo::core;



Core::Core(logging::ILogger* logger)
: logger_(logger), initialized_(false), module_mapping_state_id_(0) {}



void Core::initialize(const char* modules_dir, const char* data_dir)
{
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
    for (const auto& entry : std::filesystem::directory_iterator(modules_dir))
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
    
    // handle subscribe mapping
    for (uint32_t subscribe_id = 0; subscribe_id < channel_map_info.subscribe_consumer_info_count_; ++subscribe_id)
    {
        aergo::module::InputChannelMapInfo::IndividualChannelInfo subscribe_channel_info = channel_map_info.subscribe_consumer_info_[subscribe_id];
        for (uint32_t channel_i = 0; channel_i < subscribe_channel_info.channel_identifier_count_; ++channel_i)
        {
            aergo::module::ChannelIdentifier channel_identifier = subscribe_channel_info.channel_identifier_[channel_i];
            running_modules_[channel_identifier.producer_module_id_]->mapping_publish_[channel_identifier.producer_channel_id_].push_back({
                .producer_module_id_ = module_id,
                .producer_channel_id_ = subscribe_id
            });

            running_module->mapping_subscribe_[subscribe_id].push_back(channel_identifier);
        }
    }

    // handle request mapping
    for (uint32_t request_id = 0; request_id < channel_map_info.request_consumer_info_count_; ++request_id)
    {
        aergo::module::InputChannelMapInfo::IndividualChannelInfo request_channel_info = channel_map_info.request_consumer_info_[request_id];
        for (uint32_t channel_i = 0; channel_i < request_channel_info.channel_identifier_count_; ++channel_i)
        {
            aergo::module::ChannelIdentifier channel_identifier = request_channel_info.channel_identifier_[channel_i];
            running_modules_[channel_identifier.producer_module_id_]->mapping_response_[channel_identifier.producer_channel_id_].push_back({
                .producer_module_id_ = module_id,
                .producer_channel_id_ = request_id
            });

            running_module->mapping_request_[request_id].push_back(channel_identifier);
        }
    }
    
    // handle AUTO_ALL subscribe mapping
    const aergo::module::ModuleInfo* module_info = (*running_module->module_loader_data_)->readModuleInfo();
    for (uint32_t subscribe_id = 0; subscribe_id < module_info->subscribe_consumer_count_; ++subscribe_id)
    {
        aergo::module::communication_channel::Consumer consumer_info = module_info->subscribe_consumers_[subscribe_id];
        if (consumer_info.count_ == aergo::module::communication_channel::Consumer::Count::AUTO_ALL)
        {
            for (aergo::module::ChannelIdentifier producer_channel_identifier : getExistingPublishChannels(consumer_info.channel_type_identifier_))
            {
                if (producer_channel_identifier.producer_module_id_ >= running_modules_.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid producer_channel_identifier in registerModuleConnections, module id out of bounds.");
                    std::terminate();
                }

                aergo::core::structures::ModuleData* producer_module_data = running_modules_[producer_channel_identifier.producer_module_id_].get();
                if (producer_module_data == nullptr)
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid producer_channel_identifier in registerModuleConnections, source module is already destroyed.");
                    std::terminate();
                }

                if (producer_channel_identifier.producer_channel_id_ >= producer_module_data->mapping_publish_.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid producer_channel_identifier in registerModuleConnections, channel id out of bounds.");
                    std::terminate();
                }
                
                producer_module_data->mapping_publish_[producer_channel_identifier.producer_channel_id_].push_back({
                    .producer_module_id_ = module_id,
                    .producer_channel_id_ = subscribe_id
                });

                running_module->mapping_subscribe_[subscribe_id].push_back(producer_channel_identifier);
            }
        }
    }

    // connect publish to all AUTO_ALL subscribers
    for (uint32_t publish_id = 0; publish_id < module_info->publish_producer_count_; ++publish_id)
    {
        const aergo::module::communication_channel::Producer producer_info = module_info->publish_producers_[publish_id];

        auto it = existing_subscribe_auto_all_channels_.find(producer_info.channel_type_identifier_);
        if (it != existing_subscribe_auto_all_channels_.end())
        {
            for (aergo::module::ChannelIdentifier other_channel_id : it->second)
            {
                if (other_channel_id.producer_module_id_ >= running_modules_.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid other_channel_id in registerModuleConnections, module id out of bounds.");
                    std::terminate();
                }

                aergo::core::structures::ModuleData* subscriber_module_data = running_modules_[other_channel_id.producer_module_id_].get();
                if (subscriber_module_data == nullptr)
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid other_channel_id in registerModuleConnections, source module is already destroyed.");
                    std::terminate();
                }

                if (other_channel_id.producer_channel_id_ >= subscriber_module_data->mapping_subscribe_.size())
                {
                    log(aergo::module::logging::LogType::ERROR, "Invalid other_channel_id in registerModuleConnections, channel id out of bounds.");
                    std::terminate();
                }

                subscriber_module_data->mapping_subscribe_[other_channel_id.producer_channel_id_].push_back({
                    .producer_module_id_ = module_id,
                    .producer_channel_id_ = publish_id
                });
                
                running_module->mapping_publish_[publish_id].push_back(other_channel_id);
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
    if (loaded_module_id < loaded_modules_.size())
    {
        return loaded_modules_[loaded_module_id]->readModuleInfo();
    }
    else
    {
        return nullptr;
    }
}



uint64_t Core::getLoadedModulesCount() const
{
    return (uint64_t)loaded_modules_.size();
}



structures::ModuleData* Core::getRunningModulesInfo(uint64_t running_module_id)
{
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
    return (uint64_t)running_modules_.size();
}



uint64_t Core::getModulesMappingStateId()
{
    return module_mapping_state_id_;
}



const std::vector<aergo::module::ChannelIdentifier>& Core::getExistingPublishChannels(const char* channel_type_identifier) const
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



const std::vector<aergo::module::ChannelIdentifier>& Core::getExistingResponseChannels(const char* channel_type_identifier) const
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



bool Core::removeModule(uint64_t id, bool recursive)
{
    if (id >= running_modules_.size() || running_modules_[id].get() == nullptr)
    {
        return false;
    }

    auto& running_module = running_modules_[id];

    // TODO remove module, dependencies and all existing mappings (existing_response_channels_ and publish)
    // if (!recursive)
    // {
    //     if (running_module->mapping_publish_)
    // }

    return true;
}



bool Core::addModule(uint64_t loaded_module_id, aergo::module::InputChannelMapInfo channel_map_info)
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
    if (checkChannelMapValidityArrayCheck(channel_map_info, module_info, false))
    {
        return false;
    }

    if (checkChannelMapValidityArrayCheck(channel_map_info, module_info, true))
    {
        return false;
    }

    return true;
}


bool Core::checkChannelMapValidityArrayCheck(
    aergo::module::InputChannelMapInfo& channel_map_info, const aergo::module::ModuleInfo* module_info, bool check_subscribe
)
{
    uint32_t channel_map_consumers_count, module_info_consumers_count;
    aergo::module::InputChannelMapInfo::IndividualChannelInfo* channel_map_consumers;
    const aergo::module::communication_channel::Consumer* module_info_consumers;

    if (check_subscribe)
    {
        channel_map_consumers_count = channel_map_info.subscribe_consumer_info_count_;
        channel_map_consumers = channel_map_info.subscribe_consumer_info_;

        module_info_consumers_count = module_info->subscribe_consumer_count_;
        module_info_consumers = module_info->subscribe_consumers_;
    }
    else
    {
        channel_map_consumers_count = channel_map_info.request_consumer_info_count_;
        channel_map_consumers = channel_map_info.request_consumer_info_;

        module_info_consumers_count = module_info->request_consumer_count_;
        module_info_consumers = module_info->request_consumers_;
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
            
            if (channel_identifier.producer_module_id_ >= running_modules_.size() && running_modules_[channel_identifier.producer_module_id_].get() == nullptr)
            {
                return false;
            }

            structures::ModuleData* other_module_data = running_modules_[channel_identifier.producer_module_id_].get();
            const aergo::module::ModuleInfo* other_module_info = (*other_module_data->module_loader_data_)->readModuleInfo();

            uint32_t producers_count = check_subscribe ? other_module_info->publish_producer_count_ : other_module_info->response_producer_count_;
            const aergo::module::communication_channel::Producer* producers = check_subscribe ? other_module_info->publish_producers_ : other_module_info->response_producers_;

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
    // TODO implement
    std::terminate();
}



void Core::sendResponse(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept
{
    // TODO implement
    std::terminate();
}



void Core::sendRequest(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept
{
    // TODO implement
    std::terminate();
}



aergo::module::IAllocatorCore* Core::createDynamicAllocator() noexcept
{
    // TODO implement
    std::terminate();
}



aergo::module::IAllocatorCore* Core::createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots) noexcept
{
    // TODO implement
    std::terminate();
}



void Core::deleteAllocator(aergo::module::IAllocatorCore* allocator) noexcept
{
    // TODO implement
    std::terminate();
}


