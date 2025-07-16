#include "utils/logging/console_logger.h"

#include <sstream>
#include <iostream>
#include <chrono>
#include <ctime>

using namespace aergo::core::logging;
using namespace aergo::module::logging;

void ConsoleLogger::log(SourceType source_type, const char* source_name, uint64_t source_module_id, LogType log_type, const char* message)
{
    std::stringstream log_stream;

    switch (log_type)
    {
        case LogType::INFO:
            log_stream << "[INFO] ";
            break;
        case LogType::ERROR:
            log_stream << "[ERROR] ";
            break;
        case LogType::WARNING:
            log_stream << "[WARNING] ";
            break;
        default:
            log_stream << "[UNKNOWN] ";
            break;
    }

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm buf;
    #if defined(_WIN32) || defined(_WIN64)
        localtime_s(&buf, &in_time_t);       // Windows
    #else
        localtime_r(&in_time_t, &buf);       // POSIX
    #endif

    log_stream << std::put_time(&buf, "%Y/%m/%d %H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << ms.count() << " ";

    switch (source_type)
    {
        case SourceType::CORE:
            if (source_name == nullptr || *source_name == '\0')
                log_stream << "(CORE) ";
            else
                log_stream << "(CORE, " << source_name << ") ";
            break;
        case SourceType::MODULE:
            log_stream << "(" << (source_name ? source_name : "UNKNOWN_NAME") << ", ID: " << source_module_id << ") ";
            break;
        default:
            log_stream << "(UNKNOWN) ";
            break;
    }

    if (message)
    {
        log_stream << message;
    }

    std::cout << log_stream.str() << std::endl;
}