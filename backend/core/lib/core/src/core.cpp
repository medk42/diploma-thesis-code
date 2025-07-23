#include "core/core.h"

#include <filesystem>

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

    for (const auto& entry : std::filesystem::directory_iterator(modules_dir))
    {
        auto module_path = entry.path();
        std::string module_path_str = module_path.string();

        if (!entry.exists() && module_path.has_stem())
        {
            std::string log_message = std::string("Following module file does not exist: ") + module_path_str;
            log(aergo::module::logging::LogType::ERROR, log_message.c_str());
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

                loaded_modules_.push_back({
                    .module_loader_ = std::move(*loaded_module),
                    .module_filename_ = std::move(data_path)
                });
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
            log(aergo::module::logging::LogType::ERROR, log_message.c_str());
        }
    }
}



void Core::log(aergo::module::logging::LogType log_type, const char* message)
{
    logger_->log(logging::SourceType::CORE, nullptr, 0, log_type, message);
}