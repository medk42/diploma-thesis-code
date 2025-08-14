#pragma once

#include "utils/module_interface/module_loader.h"
#include "utils/logging/logger.h"

#include <filesystem>
#include <vector>



namespace aergo::core::structures
{
    class ModuleLoaderData
    {
    public:
        ModuleLoaderData(std::unique_ptr<aergo::core::ModuleLoader>&& module_loader, std::string module_data_path, std::string module_file_name);

        /// @brief Access to module loader's data.
        aergo::core::ModuleLoader* operator->();
        
        /// @brief Access to module loader's data.
        const aergo::core::ModuleLoader* operator->() const;

        /// @brief Get unique name for module, filesystem friendly. Taken from the library filename.
        const std::string& getModuleUniqueName() const;

        /// @brief Path to modules data. Can be invalid if data folder does not exist.
        const std::string& getModuleDataPath() const;

    private:
        std::unique_ptr<aergo::core::ModuleLoader> module_loader_;
        std::string module_data_path_;
        std::string module_file_name_;
    };

    class ModuleLogger : public aergo::module::logging::ILogger
    {
    public:
        ModuleLogger(aergo::core::logging::ILogger* core_logger, std::string module_name, uint64_t module_id);

        virtual void log(aergo::module::logging::LogType type, const char* message) const noexcept override final;

    private:
        aergo::core::logging::ILogger* core_logger_;
        std::string module_name_;
        uint64_t module_id_;
    };

    struct ModuleData
    {
        ModuleData(ModuleLogger&& logger, ModuleLoaderData* module_loader_data);

        aergo::core::ModuleLoader::ModulePtr module_;
        ModuleLogger logger_;
        ModuleLoaderData* module_loader_data_;
        
        std::vector<std::vector<aergo::module::ChannelIdentifier>> mapping_subscribe_;  // for visualization
        std::vector<std::vector<aergo::module::ChannelIdentifier>> mapping_request_;    // for visualization
        std::vector<std::vector<aergo::module::ChannelIdentifier>> mapping_publish_;    // for sending messages + cascade destruction
        std::vector<std::vector<aergo::module::ChannelIdentifier>> mapping_response_;   // for cascade destruction
    };
    
}