#pragma once

#include "module_common/module_interface_threads.h"
#include "utils/logging/logger.h"
#include "shared_data_core.h"

#include <vector>
#include <deque>
#include <set>
#include <optional>
#include <mutex>


namespace aergo::core::memory_allocation
{
    class StaticAllocator : public aergo::module::IAllocatorCore
    {
    public:
        class StaticAllocatorInitializationException : public std::exception {};

        /// @throws StaticAllocatorInitializationException if data could not be allocated.
        StaticAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots, aergo::core::logging::ILogger* logger, IMemoryAllocator* custom_allocator = nullptr);

        StaticAllocator(const StaticAllocator& other) = delete;
        StaticAllocator(StaticAllocator&& other) noexcept = default;

        StaticAllocator& operator=(const StaticAllocator& other) = delete;
        StaticAllocator& operator=(StaticAllocator&& other) noexcept = default;

        /// @param number_of_bytes parameter is ignored, since we have fixed slots
        virtual aergo::module::ISharedData* allocate(uint64_t number_of_bytes) noexcept override final;
        virtual void addOwner(aergo::module::ISharedData* data) noexcept override final;
        virtual void removeOwner(aergo::module::ISharedData* data) noexcept override final;

        // separate for testing that it does not throw exceptions
        aergo::module::ISharedData* allocateImpl();
        void addOwnerImpl(aergo::module::ISharedData* data);
        void removeOwnerImpl(aergo::module::ISharedData* data);

    private:
        void log(aergo::module::logging::LogType log_type, const char* message);

        IMemoryAllocator* memory_allocator_;
        DefaultAllocator default_memory_allocator_;
        aergo::core::logging::ILogger* logger_;

        std::vector<SharedDataCore> preallocated_data_;

        std::deque<std::size_t> free_memory_slot_ids_;
        std::set<std::size_t> allocated_memory_slots_; // for checking pointer validity in addOwnerImpl / removeOwnerImpl

        std::mutex mutex_;
    };
}