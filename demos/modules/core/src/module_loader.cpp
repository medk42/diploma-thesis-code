#include "module_loader.h"

using namespace aergo::core;



ModuleLoader::~ModuleLoader()
{
    ModuleLibrary_closeLib(handle_);
}



ModuleLoader::ModuleLoader(ModuleLibrary_LibHandle handle, ModuleLibrary_Api api)
: handle_(handle), api_(api) {}



std::string ModuleLoader::getName()
{
    return api_.readPluginName();
}



uint64_t ModuleLoader::getApiVersion()
{
    return api_.readPluginApiVersion();
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

    api.readPluginApiVersion();
    api.readPluginName();

    return std::unique_ptr<ModuleLoader>(
        new ModuleLoader(handle, api)
    );
}