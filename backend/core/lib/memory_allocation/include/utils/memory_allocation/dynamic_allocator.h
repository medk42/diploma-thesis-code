#pragma once

#include "module_common/module_interface_threads.h"
#include "utils/logging/logger.h"
#include "shared_data_core.h"

#include <map>


namespace aergo::core::memory_allocation
{
    class DynamicAllocator : public aergo::module::IAllocatorCore
    {
    public:
        DynamicAllocator(aergo::core::logging::ILogger& logger, IMemoryAllocator* custom_allocator = nullptr);

        virtual aergo::module::ISharedData* allocate(uint64_t number_of_bytes) override final;
        virtual void addOwner(aergo::module::ISharedData* data) override final;
        virtual void removeOwner(aergo::module::ISharedData* data) override final;

    private:
        void log(aergo::module::logging::LogType log_type, const char* message);

        IMemoryAllocator* memory_allocator_;
        DefaultAllocator default_memory_allocator_;

        std::map<uint64_t, SharedDataCore> allocated_data_;
        uint64_t allocation_id_;
        aergo::core::logging::ILogger& logger_;
    };
}