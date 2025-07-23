#include <filesystem>

#include "utils/logging/console_logger.h"
#include "core/core.h"

#include <iostream>
#include <sstream>

using namespace aergo::core;


void log(logging::ConsoleLogger& logger, aergo::module::logging::LogType type, std::stringstream ss)
{
    std::string str = ss.str();
    logger.log(logging::SourceType::CORE, "main", 0, type, str.c_str());
}



void printLoadedModules(logging::ConsoleLogger& logger, Core& core)
{
    size_t module_count = core.getLoadedModulesCount();

    log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "=== LOADED MODULES ===");
    log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\tCount: " << module_count);
    log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\tModules:");
    for (size_t i = 0; i < module_count; ++i)
    {
        const aergo::module::ModuleInfo* module_info = core.getLoadedModulesInfo(i);

        log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\tName: " << module_info->display_name_);
        log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\tDescription: " << module_info->display_description_);

        if (module_info->publish_producer_count_ > 0)
        {
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\tPublish producers: [");
            for (uint32_t j = 0; j < module_info->publish_producer_count_; ++j)
            {
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t{");
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tDisplay name: " << module_info->publish_producers_[j].display_name_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tDisplay description: " << module_info->publish_producers_[j].display_name_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tChannel type identifier: " << module_info->publish_producers_[j].channel_type_identifier_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t}");
            }
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t]");
        }
        else
        {
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\tPublish producers: NONE");
        }

        if (module_info->response_producer_count_ > 0)
        {
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\tResponse producers: [");
            for (uint32_t j = 0; j < module_info->response_producer_count_; ++j)
            {
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t{");
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tDisplay name: " << module_info->response_producers_[j].display_name_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tDisplay description: " << module_info->response_producers_[j].display_name_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tChannel type identifier: " << module_info->response_producers_[j].channel_type_identifier_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t}");
            }
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t]");
        }
        else
        {
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\tResponse producers: NONE");
        }

        if (module_info->subscribe_consumer_count_ > 0)
        {
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\tSubscribe consumers: [");
            for (uint32_t j = 0; j < module_info->subscribe_consumer_count_; ++j)
            {
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t{");
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tDisplay name: " << module_info->subscribe_consumers_[j].display_name_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tDisplay description: " << module_info->subscribe_consumers_[j].display_name_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tChannel type identifier: " << module_info->subscribe_consumers_[j].channel_type_identifier_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tCount: " << (int)module_info->subscribe_consumers_[j].count_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tMin: " << (int)module_info->subscribe_consumers_[j].min_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tMax: " << (int)module_info->subscribe_consumers_[j].max_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t}");
            }
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t]");
        }
        else
        {
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\tSubscribe consumers: NONE");
        }

        if (module_info->request_consumer_count_ > 0)
        {
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\tRequest consumers: [");
            for (uint32_t j = 0; j < module_info->request_consumer_count_; ++j)
            {
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t{");
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tDisplay name: " << module_info->request_consumers_[j].display_name_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tDisplay description: " << module_info->request_consumers_[j].display_name_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tChannel type identifier: " << module_info->request_consumers_[j].channel_type_identifier_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tCount: " << (int)module_info->request_consumers_[j].count_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tMin: " << (int)module_info->request_consumers_[j].min_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t\tMax: " << (int)module_info->request_consumers_[j].max_);
                log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t\t}");
            }
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\t]");
        }
        else
        {
            log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\tRequest consumers: NONE");
        }
        log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "\t\t\tAuto-create: " << (module_info->auto_create_ ? "TRUE" : "false"));
    }
}



int main(int argc, char** argv)
{
    logging::ConsoleLogger logger;

    if (argc != 3)
    {
        log(logger, aergo::module::logging::LogType::ERROR, std::stringstream() << "Expected usage: <program_name>.exe [path_to_modules] [path_to_module_data]");
        return -1;
    }

    std::string module_dir = argv[1];
    std::string data_dir = argv[2];

    log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "Module directory: " << module_dir);
    log(logger, aergo::module::logging::LogType::INFO, std::stringstream() << "Data directory: " << data_dir);
    
    Core core(&logger);
    core.initialize(module_dir.c_str(), data_dir.c_str());

    printLoadedModules(logger, core);
}