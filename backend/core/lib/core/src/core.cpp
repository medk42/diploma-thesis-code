#include "core/core.h"
#include "core/defaults.h"

using namespace aergo::core;



Core::Core(logging::ILogger* logger)
: logger_(logger), initialized_(false) {}



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

            const char* data_path;
            if (std::filesystem::exists(loaded_modules_[i].getModuleDataPath()))
            {
                data_path = loaded_modules_[i].getModuleDataPath().c_str();
            }
            else
            {
                data_path = nullptr;
            }

            
            uint64_t next_module_id = getNextModuleId();
            auto module_data = std::make_unique<structures::ModuleData>( // TODO this may be an issue, module_ does not have a default constructor maybe
                structures::ModuleLogger(
                    logger_, 
                    loaded_modules_[i].getModuleUniqueName(), 
                    next_module_id
                ), 
                &(loaded_modules_[i])
            );

            ModuleLoader::ModulePtr created_module(loaded_modules_[i]->createModule(data_path, this, empty_channel_info, &(module_data->logger_), next_module_id));

            if (created_module.get() == nullptr)
            {
                std::string error_message = std::string("Failed to create module (createModule call failed) for module: ") + module_data->module_loader_data_->getModuleUniqueName();
                log(aergo::module::logging::LogType::WARNING, error_message.c_str());
            }
            else
            {
                bool result = created_module->threadStart(defaults::module_thread_timeout_ms_);
                if (!result)
                {
                    bool result2 = created_module->threadStop(defaults::module_thread_timeout_ms_);

                    std::string error_message = std::string("Failed to start thread for module: \"") + module_data->module_loader_data_->getModuleUniqueName() + std::string("\", stop success: ") + (result2 ? "TRUE" : "false");
                    log(aergo::module::logging::LogType::WARNING, error_message.c_str());
                }
                else
                {
                    module_data->module_ = std::move(created_module);
                    running_modules_.push_back(std::move(module_data));

                    std::string success_message = std::string("Successfully auto-created module: ") + module_data->module_loader_data_->getModuleUniqueName();
                    log(aergo::module::logging::LogType::INFO, success_message.c_str());
                }
            }
        }
    }
}



uint64_t Core::getNextModuleId()
{
    return running_modules_.size();
}



void Core::log(aergo::module::logging::LogType log_type, const char* message)
{
    logger_->log(logging::SourceType::CORE, nullptr, 0, log_type, message);
}



const aergo::module::ModuleInfo* Core::getLoadedModulesInfo(size_t loaded_module_id)
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



size_t Core::getLoadedModulesCount() const
{
    return loaded_modules_.size();
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


