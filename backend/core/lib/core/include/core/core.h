#pragma once

#include "module_common/module_interface_threads.h"
#include "utils/module_interface/module_loader.h"
#include "utils/logging/logger.h"

namespace aergo::core
{
    class Core : public aergo::module::ICore
    {
    public:
        Core(logging::ILogger* logger);

        void initialize(const char* modules_dir, const char* data_dir);

        virtual void sendMessage(uint64_t source_module_id, uint64_t publish_producer_id, aergo::module::message::MessageHeader message) noexcept override final;
        virtual void sendResponse(uint64_t source_module_id, uint64_t response_producer_id, aergo::module::message::MessageHeader message) noexcept override final;
        virtual void sendRequest(uint64_t source_module_id, uint64_t request_consumer_id, uint64_t target_module_id, aergo::module::message::MessageHeader message) noexcept override final;
        virtual aergo::module::IAllocatorCore* createDynamicAllocator() noexcept override final;
        virtual aergo::module::IAllocatorCore* createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots) noexcept override final;
        virtual void deleteAllocator(aergo::module::IAllocatorCore* allocator) noexcept override final;

    private:
        struct ModuleLoaderData
        {
            std::unique_ptr<ModuleLoader> module_loader_;
            std::filesystem::path module_filename_;
        };

        void log(aergo::module::logging::LogType log_type, const char* message);



        std::vector<ModuleLoaderData> loaded_modules_;

        logging::ILogger* logger_;
    };
}