#include "core/logging/console_logger.h"

#include <sstream>
#include <iostream>

using namespace aergo::core::logging;
using namespace aergo::module::logging;

void ConsoleLogger::log(SourceType source_type, const char* source_name, uint64_t source_module_id, LogType log_type, const char* message)
{
    std::stringstream log_stream;

    switch (log_type)
    {
        case LogType::INFO:
            log_stream << "[INFO]    ";
            break;
        case LogType::ERROR:
            log_stream << "[ERROR]   ";
            break;
        case LogType::WARNING:
            log_stream << "[WARNING] ";
            break;
        default:
            log_stream << "[UNKNOWN] ";
            break;
    }

    switch (source_type)
    {
        case SourceType::CORE:
            log_stream << "(CORE) ";
            break;
        case SourceType::MODULE:
            log_stream << "(" << source_name << ", ID: " << source_module_id << ") ";
            break;
        default:
            log_stream << "(UNKNOWN) ";
            break;
    }

    log_stream << message;

    std::cout << log_stream.str() << std::endl;
}