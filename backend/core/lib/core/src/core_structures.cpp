#include "core/core_structures.h"

using namespace aergo::core::structures;
using namespace aergo::core;



ModuleLoaderData::ModuleLoaderData(std::unique_ptr<ModuleLoader>&& module_loader, std::string module_data_path, std::string module_file_name)
: module_loader_(std::move(module_loader)), module_data_path_(std::move(module_data_path)), module_file_name_(std::move(module_file_name)) {}



ModuleLoader* ModuleLoaderData::operator->()
{
    return module_loader_.get();
}



const ModuleLoader* ModuleLoaderData::operator->() const
{
    return module_loader_.get();
}



const std::string& ModuleLoaderData::getModuleUniqueName() const
{
    return module_file_name_;
}



const std::string& ModuleLoaderData::getModuleDataPath() const
{
    return module_data_path_;
}


ModuleData::ModuleData(ModuleLogger&& logger, ModuleLoaderData* module_loader_data)
: logger_(std::move(logger)), module_loader_data_(module_loader_data) {}



ModuleLogger::ModuleLogger(aergo::core::logging::ILogger* core_logger, std::string module_name, uint64_t module_id)
: core_logger_(core_logger), module_name_(std::move(module_name)), module_id_(module_id) {}



void ModuleLogger::log(aergo::module::logging::LogType type, const char* message) const noexcept
{
    core_logger_->log(aergo::core::logging::SourceType::MODULE, module_name_.c_str(), module_id_, type, message);
}