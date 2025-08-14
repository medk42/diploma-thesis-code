#pragma once

#include "module_common/module_interface_.h"
#include "utils/logging/logger.h"
#include "shared_data_core.h"
#include "allocator_interface_core.h"

#include <map>
#include <set>
#include <mutex>


namespace aergo::core::memory_allocation
{
    class DynamicAllocator : public ICoreAllocator
    {
    public:
        DynamicAllocator(aergo::core::logging::ILogger* logger, IMemoryAllocator* custom_allocator = nullptr);

        virtual aergo::module::ISharedData* allocate(uint64_t number_of_bytes) noexcept override final;
        virtual void addOwner(aergo::module::ISharedData* data) noexcept override final;
        virtual void removeOwner(aergo::module::ISharedData* data) noexcept override final;

        // separate for testing that it does not throw exceptions
        aergo::module::ISharedData* allocateImpl(uint64_t number_of_bytes);
        void addOwnerImpl(aergo::module::ISharedData* data);
        void removeOwnerImpl(aergo::module::ISharedData* data);

    private:
        void log(aergo::module::logging::LogType log_type, const char* message);

        IMemoryAllocator* memory_allocator_;
        DefaultAllocator default_memory_allocator_;
        aergo::core::logging::ILogger* logger_;

        std::map<uint64_t, SharedDataCore> allocated_data_;
        std::set<std::size_t> allocated_memory_slots_;
        uint64_t allocation_id_;

        std::mutex mutex_;
    };
}