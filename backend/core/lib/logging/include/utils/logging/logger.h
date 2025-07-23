#pragma once

#include "module_common/module_interface_threads.h"

namespace aergo::core::logging
{
    enum class SourceType { CORE, MODULE };

    class ILogger
    {
    public:
        inline virtual ~ILogger() {}

        /// @brief Log a message.
        /// @param source_type Core or Module.
        /// @param source_name For module use module name, for core use name of part of core (memory allocation for example). Use nullptr for no source.
        /// @param source_module_id Only for module, ID module received from core. Use 0 for other source types.
        /// @param log_type Info, warning or error.
        /// @param message Log message.
        virtual void log(SourceType source_type, const char* source_name, uint64_t source_module_id, aergo::module::logging::LogType log_type, const char* message) = 0;
    };
}