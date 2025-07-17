#pragma once

#include "utils/logging/console_logger.h"
#include "module_common/module_interface_threads.h"

#include <vector>

class TestLogger : public aergo::core::logging::ILogger
{
public:
    void log(aergo::core::logging::SourceType source_type, const char* source_name, uint64_t source_module_id, aergo::module::logging::LogType log_type, const char* message) override
    {
        console_logger_.log(source_type, source_name, source_module_id, log_type, message);
        logs_types_.push_back(log_type);
    }

    std::vector<aergo::module::logging::LogType>& logs()
    {
        return logs_types_;
    }

private:
    aergo::core::logging::ConsoleLogger console_logger_;
    std::vector<aergo::module::logging::LogType> logs_types_;
};