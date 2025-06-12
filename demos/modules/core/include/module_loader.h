#pragma once

#include <string>
#include <expected>
#include <memory>

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
        ~ModuleLoader();

        static std::expected<std::unique_ptr<ModuleLoader>, ModuleLoadError> loadModule(const char* path);

        std::string getName();
        uint64_t getApiVersion();

    private:
        ModuleLoader(ModuleLibrary_LibHandle handle, ModuleLibrary_Api api);

        ModuleLibrary_LibHandle handle_;
        ModuleLibrary_Api api_;
    };

} // namespace aergo::core