#pragma once

#include <stdint.h>

#include "module_interface.h"
#include "module_interface_threads.h"

#define CORE_API_VERSION 1

#ifdef _WIN32
  #include <windows.h>
  using ModuleLibrary_LibHandle = HMODULE;
  inline ModuleLibrary_LibHandle ModuleLibrary_openLib(const char* p){ return LoadLibraryA(p); }
  inline void*   ModuleLibrary_getSym(ModuleLibrary_LibHandle h, const char* s){ return GetProcAddress(h,s); }
  inline void    ModuleLibrary_closeLib(ModuleLibrary_LibHandle h){ FreeLibrary(h); }
#else
  #include <dlfcn.h>
  using ModuleLibrary_LibHandle = void*;
  inline ModuleLibrary_LibHandle ModuleLibrary_openLib(const char* p){ return dlopen(p,RTLD_LAZY); }
  inline void*   ModuleLibrary_getSym (ModuleLibrary_LibHandle h, const char* s){ return dlsym(h,s); }
  inline void    ModuleLibrary_closeLib(ModuleLibrary_LibHandle h){ dlclose(h); }
#endif

#define PLUGIN_FUNCS(_) \
    _(const aergo::module::ModuleInfo*,  readModuleInfo,       ) \
    _(uint64_t,                          readPluginApiVersion  ) \
    _(aergo::module::IModule*,           createModule,         aergo::module::ICore*, aergo::module::InputChannelMapInfo, aergo::module::logging::ILogger*, uint64_t) \
    _(void,                              destroyModule,        aergo::module::IModule*)

struct ModuleLibrary_Api {
    #define _(ret,name,...) ret (*name)(__VA_ARGS__);
    PLUGIN_FUNCS(_)
    #undef _
};

inline bool ModuleLibrary_fillApi(ModuleLibrary_Api& a, ModuleLibrary_LibHandle h){
    #define _(ret,name,...) \
        a.name = (ret(*)(__VA_ARGS__))ModuleLibrary_getSym(h,#name); \
        if(!a.name) return false;
    PLUGIN_FUNCS(_)
    #undef _
    return true;
}
