#include <filesystem>

#include "logging.h"
#include "utils/module_interface/module_loader.h"

using namespace aergo::core;

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        LOG_ERROR("Expected usage: <program_name>.exe [path_to_modules]")
    }
    std::string module_path = argv[1];

    AERGO_LOG("Path to modules: " << module_path)

    for (const auto& entry : std::filesystem::directory_iterator(module_path)) {
        std::string module_path = entry.path().string();
        AERGO_LOG("Attempting to load module \"" << module_path << "\"");

        auto result = ModuleLoader::loadModule(module_path.c_str());

        if (!result)
        {
            switch (result.error())
            {
                case ModuleLoadError::FAILED_TO_LOAD:
                    AERGO_LOG("\tError, module failed to load!")
                    break;
                case ModuleLoadError::FAILED_TO_MAP_METHODS:
                    AERGO_LOG("\tError, failed to map methods!")
                    break;
            }
        }
        else
        {
            // AERGO_LOG("\tName: \"" << (*result)->getName() << "\"  API Version: " << (*result)->getApiVersion())
        }
    }
}