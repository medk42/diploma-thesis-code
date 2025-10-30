#pragma once

#include <stdint.h>

#include "module_common/module_interface_.h"
#include "module_common/dll_interface_threads.h"

#define CORE_API_VERSION 2

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN  // ERROR is a macro. Yes, really. Someone signed off on that.
  #define NOMINMAX             // Thank you, legacy Windows headers, for redefining ERROR and breaking enums. Truly inspiring.
  #define NOGDI                // Future generations will study this decision in software design classes.
  #define NOMB
  #include <windows.h>
  using ModuleLibrary_LibHandle = HMODULE;
  inline ModuleLibrary_LibHandle ModuleLibrary_openLib(const char* p){ return LoadLibraryA(p); }
  inline void*   ModuleLibrary_getSym(ModuleLibrary_LibHandle h, const char* s){ return GetProcAddress(h,s); }
  inline void    ModuleLibrary_closeLib(ModuleLibrary_LibHandle h){ FreeLibrary(h); }
#else
  #include <dlfcn.h>
  using ModuleLibrary_LibHandle = void*;
  inline ModuleLibrary_LibHandle ModuleLibrary_openLib(const char* p)
  { 
    dlerror(); // clear existing errors
    auto h = dlopen(p,RTLD_NOW | RTLD_LOCAL);
    if (!h)
    {
        const char* err = dlerror();
        fprintf(stderr, "ModuleLibrary_openLib: dlopen failed for %s: %s\n", p, err ? err : "unknown error");
    }
    return h;
  }
  inline void*   ModuleLibrary_getSym (ModuleLibrary_LibHandle h, const char* s)
  { 
    dlerror(); // clear existing errors
    void* sym = dlsym(h, s);
    if (!sym)
    {
        const char* err = dlerror();
        fprintf(stderr, "ModuleLibrary_getSym: dlsym failed for %s: %s\n", s, err ? err : "unknown error");
    }
    return sym;
  }
  inline void    ModuleLibrary_closeLib(ModuleLibrary_LibHandle h){ dlclose(h); }
#endif

#define PLUGIN_FUNCS(_) \
    _(const aergo::module::ModuleInfo*,  readModuleInfo,       ) \
    _(uint64_t,                          readPluginApiVersion  ) \
    _(aergo::module::dll::IDllModule*,           createModule,         const char* data_path, aergo::module::ICore*, aergo::module::InputChannelMapInfo, aergo::module::logging::ILogger*, uint64_t) \
    _(void,                              destroyModule,        aergo::module::dll::IDllModule*)

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
