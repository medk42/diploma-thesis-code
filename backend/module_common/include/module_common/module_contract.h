#pragma once


#define PLUGIN_API_VERSION 2


#if defined(_WIN32)
  #define DLL_API extern "C" __declspec(dllexport)
#else
  #define DLL_API extern "C" __attribute__((visibility("default")))
#endif


#include <stdint.h>

#include "module_interface_.h"
#include "dll_module_wrapper.h"



/// @brief Read version of module api to prevent loading modules with incompatible api.
DLL_API uint64_t readPluginApiVersion()
{
  return PLUGIN_API_VERSION;
};

/// @brief Return pointer to statically allocated module info.
DLL_API const aergo::module::ModuleInfo* readModuleInfo();

/// @brief Create a new module, using allocated memory. Take care of disposing of the memory using destroyModule call.
/// @param data_path path to the data folder for the module (if it exists, otherwise nullptr), make a copy if needed
/// @param core reference to functions of the core (sending messages and allocating memory)
/// @param channel_map_info ids of modules bound to subscribe and request channels, so the module knows what is on its input, make a copy if needed
/// @param logger object for logging messages from the core
/// @param module_id unique ID of this module received from the core
/// @return create module or nullptr on failure
DLL_API aergo::module::dll::DllModuleWrapper* createModule(const char* data_path, aergo::module::ICore* core, aergo::module::InputChannelMapInfo channel_map_info, aergo::module::logging::ILogger* logger, uint64_t module_id);

/// @brief Destroy previously created module.  
DLL_API void destroyModule(aergo::module::dll::DllModuleWrapper* module);