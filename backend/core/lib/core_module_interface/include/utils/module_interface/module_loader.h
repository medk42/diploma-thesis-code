#pragma once

#include <string>
#include <expected>
#include <memory>
#include <functional>

#include "module_api.h"

namespace aergo::core
{

    enum class ModuleLoadError {
        FAILED_TO_LOAD,
        FAILED_TO_MAP_METHODS
    };

    class ModuleLoader
    {
    public:
        using ModulePtr = std::unique_ptr<aergo::module::dll::IDllModule, std::function<void(aergo::module::dll::IDllModule*)>>;

        ~ModuleLoader();

        static std::expected<std::unique_ptr<ModuleLoader>, ModuleLoadError> loadModule(const char* path);

        /// @brief Read version of module api to prevent loading modules with incompatible api.
        uint64_t readPluginApiVersion();

        /// @brief Return pointer to statically allocated module info.
        const aergo::module::ModuleInfo* readModuleInfo();

        /// @brief Create a new module, using allocated memory. Module is automatically destroyed when the unique_ptr goes out of scope.
        /// @param data_path path to the data folder for the module (if it exists, otherwise nullptr)
        /// @param core reference to functions of the core (sending messages and allocating memory)
        /// @param channel_map_info ids of modules bound to subscribe and request channels, so the module knows what is on its input
        /// @param logger object for logging messages from the core
        /// @param module_id unique ID of this module received from the core
        ModulePtr createModule(const char* data_path, aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, aergo::module::logging::ILogger* logger, uint64_t module_id);

    private:
        ModuleLoader(ModuleLibrary_LibHandle handle, ModuleLibrary_Api api);

        ModuleLibrary_LibHandle handle_;
        ModuleLibrary_Api api_;
    };

} // namespace aergo::core