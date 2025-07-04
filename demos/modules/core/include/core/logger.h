#pragma once

#include "module_interface_threads.h"

namespace aergo::core::logging
{
    enum class SourceType { CORE, MODULE };

    class ILogger
    {
    public:
        virtual void log(SourceType source_type, const char* source_name, uint64_t source_module_id, aergo::module::logging::LogType log_type, const char* message) = 0;
    };
}