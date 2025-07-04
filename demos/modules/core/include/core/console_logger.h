#pragma once

#include "logger.h"

namespace aergo::core::logging
{
    class ConsoleLogger : public ILogger
    {
    public:
        virtual void log(SourceType source_type, const char* source_name, uint64_t source_module_id, aergo::module::logging::LogType log_type, const char* message) override;
    };
}