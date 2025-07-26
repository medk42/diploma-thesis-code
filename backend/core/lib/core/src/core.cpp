#include "core/core.h"
#include "core/defaults.h"

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
        if (loaded_modules_[i]->readModuleInfo()->auto_create_)
        {
            if (createAndStartModule(i, empty_channel_info, defaults::module_thread_timeout_ms_))
            {
                std::string success_message = std::string("Successfully auto-created module: ") + loaded_modules_[i].getModuleUniqueName();
                log(aergo::module::logging::LogType::INFO, success_message.c_str());
            }
            else
            {
                std::string success_message = std::string("Failed to auto-create module: ") + loaded_modules_[i].getModuleUniqueName();
                log(aergo::module::logging::LogType::WARNING, success_message.c_str());
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

            registerModule(next_module_id, loaded_modules_[loaded_module_id]->readModuleInfo());

            return true;
        }
    }
}



uint64_t Core::getNextModuleId()
{
    return running_modules_.size();
}



void Core::registerModule(uint64_t module_id, const aergo::module::ModuleInfo* module_info)
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



std::optional<std::vector<aergo::module::ChannelIdentifier>&> Core::getExistingPublishChannels(const char* channel_type_identifier)
{
    if (existing_publish_channels_.contains(channel_type_identifier))
    {
        return existing_publish_channels_[channel_type_identifier];
    }
    else
    {
        return std::nullopt;
    }
}



std::optional<std::vector<aergo::module::ChannelIdentifier>&> Core::getExistingResponseChannels(const char* channel_type_identifier)
{
    if (existing_response_channels_.contains(channel_type_identifier))
    {
        return existing_response_channels_[channel_type_identifier];
    }
    else
    {
        return std::nullopt;
    }
}



bool Core::removeModule(uint64_t id, bool recursive)
{
    // TODO remove module, dependencies and all existing mappings (existing_response_channels_ and publish)
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
    if (module_info->subscribe_consumer_count_ != channel_map_info.subscribe_consumer_info_count_)
    {
        return false;
    }

    if (module_info->request_consumer_count_ != channel_map_info.request_consumer_info_count_)
    {
        return false;
    }

    for (uint32_t subscriber_id = 0; subscriber_id < channel_map_info.subscribe_consumer_info_count_; ++subscriber_id)
    {
        switch (module_info->subscribe_consumers_[subscriber_id].count_)
        {
        case aergo::module::communication_channel::SubscribeConsumer::Count::SINGLE:
            if (channel_map_info.subscribe_consumer_info_[subscriber_id].channel_identifier_count_ != 1)
            {
                return false;
            }
            break;
        case aergo::module::communication_channel::SubscribeConsumer::Count::RANGE:
            if (channel_map_info.subscribe_consumer_info_[subscriber_id].channel_identifier_count_ < module_info->subscribe_consumers_[subscriber_id].min_
             || channel_map_info.subscribe_consumer_info_[subscriber_id].channel_identifier_count_ > module_info->subscribe_consumers_[subscriber_id].max_)
            {
                return false;
            }
            break;
        case aergo::module::communication_channel::SubscribeConsumer::Count::AUTO_ALL:
            break;
        default:
            log(aergo::module::logging::LogType::ERROR, "Unexpected enum type in checkChannelMapValidity, terminating!");
            std::terminate();
            break;
        }
    }

    for (uint32_t request_id = 0; request_id < channel_map_info.request_consumer_info_count_; ++request_id)
    {
        switch (module_info->request_consumers_[request_id].count_)
        {
        case aergo::module::communication_channel::RequestConsumer::Count::SINGLE:
            if (channel_map_info.request_consumer_info_[request_id].channel_identifier_count_ != 1)
            {
                return false;
            }
            break;
        case aergo::module::communication_channel::RequestConsumer::Count::RANGE:
            if (channel_map_info.request_consumer_info_[request_id].channel_identifier_count_ < module_info->request_consumers_[request_id].min_
             || channel_map_info.request_consumer_info_[request_id].channel_identifier_count_ > module_info->request_consumers_[request_id].max_)
            {
                return false;
            }
            break;
        case aergo::module::communication_channel::RequestConsumer::Count::AUTO_ALL:
            break;
        default:
            log(aergo::module::logging::LogType::ERROR, "Unexpected enum type in checkChannelMapValidity, terminating!");
            std::terminate();
            break;
        }
    }

    return true;
}



void Core::sendMessage(aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept
{
    std::terminate();
}



void Core::sendResponse(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept
{
    std::terminate();
}



void Core::sendRequest(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept
{
    std::terminate();
}



aergo::module::IAllocatorCore* Core::createDynamicAllocator() noexcept
{
    std::terminate();
}



aergo::module::IAllocatorCore* Core::createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots) noexcept
{
    std::terminate();
}



void Core::deleteAllocator(aergo::module::IAllocatorCore* allocator) noexcept
{
    std::terminate();
}


