#pragma once

#include "module_common/module_interface_threads.h"
#include "utils/logging/logger.h"
#include "core_structures.h"

namespace aergo::core
{
    class Core : public aergo::module::ICore
    {
    public:
        Core(logging::ILogger* logger);

        /// @brief Load all available modules from the modules_dir. Data for each module is in data_dir (with same folder name as the module library filename).
        /// For example "${modules_dir}/module_a.dll" will have data directory pointed to "${data_dir}/module_a". Auto-create modules with flag auto_create_.
        /// Only call this method once. Subsequent calls are ignored.
        void initialize(const char* modules_dir, const char* data_dir);

        virtual void sendMessage(aergo::module::ChannelIdentifier source_channel, aergo::module::message::MessageHeader message) noexcept override final;
        virtual void sendResponse(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept override final;
        virtual void sendRequest(aergo::module::ChannelIdentifier source_channel, aergo::module::ChannelIdentifier target_channel, aergo::module::message::MessageHeader message) noexcept override final;
        virtual aergo::module::IAllocatorCore* createDynamicAllocator() noexcept override final;
        virtual aergo::module::IAllocatorCore* createBufferAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots) noexcept override final;
        virtual void deleteAllocator(aergo::module::IAllocatorCore* allocator) noexcept override final;

        const aergo::module::ModuleInfo* getLoadedModulesInfo(size_t loaded_module_id);
        size_t getLoadedModulesCount() const;

    private:
        void log(aergo::module::logging::LogType log_type, const char* message);
        void loadModules(const char* modules_dir, const char* data_dir);
        void autoCreateModules();
        uint64_t getNextModuleId();


        bool initialized_;
        std::vector<structures::ModuleLoaderData> loaded_modules_;
        std::vector<std::unique_ptr<structures::ModuleData>> running_modules_;

        logging::ILogger* logger_;
    };
}