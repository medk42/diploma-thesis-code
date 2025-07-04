#include "core/module_loader.h"

using namespace aergo::core;



ModuleLoader::~ModuleLoader()
{
    ModuleLibrary_closeLib(handle_);
}



ModuleLoader::ModuleLoader(ModuleLibrary_LibHandle handle, ModuleLibrary_Api api)
: handle_(handle), api_(api) {}



uint64_t ModuleLoader::readPluginApiVersion()
{
    return api_.readPluginApiVersion();
}



const aergo::module::ModuleInfo* ModuleLoader::readModuleInfo()
{
    return api_.readModuleInfo();
}



std::unique_ptr<aergo::module::IModule, std::function<void()>> ModuleLoader::createModule(aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, aergo::module::logging::ILogger* logger, uint64_t module_id)
{
    aergo::module::IModule* module_ref = api_.createModule(core, channel_map_info, logger, module_id);
    return std::unique_ptr<aergo::module::IModule, std::function<void()>>(
        module_ref,
        [this, module_ref]() { api_.destroyModule(module_ref); }
    );
}



std::expected<std::unique_ptr<ModuleLoader>, ModuleLoadError> ModuleLoader::loadModule(const char* path)
{
    ModuleLibrary_LibHandle handle = ModuleLibrary_openLib(path);
    if (!handle)
    {
        return std::unexpected(ModuleLoadError::FAILED_TO_LOAD);
    }

    ModuleLibrary_Api api;
    bool success = ModuleLibrary_fillApi(api, handle);
    if (!success)
    {
        return std::unexpected(ModuleLoadError::FAILED_TO_MAP_METHODS);
    }

    return std::unique_ptr<ModuleLoader>(
        new ModuleLoader(handle, api)
    );
}