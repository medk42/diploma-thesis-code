#pragma once

#include "module_common/module_interface_.h"
#include "shared_data_core.h"

namespace aergo::core::memory_allocation
{
    class ICoreAllocator
    {
    public:
        inline virtual ~ICoreAllocator() = default;

        /// @brief Allocate "number_of_bytes" bytes of shared memory. If the allocator has fixed byte size, "number_of_bytes" parameter is ignored.
        /// @return Pointer to data or nullptr on failure.
        virtual aergo::module::ISharedData* allocate(uint64_t number_of_bytes) noexcept = 0;

        /// @brief Add owner for shared data object. Object removed when owners drop to zero.
        virtual void addOwner(aergo::module::ISharedData* data) noexcept = 0;

        /// @brief Remove owner from shared data object. Object removed when owners drop to zero.
        virtual void removeOwner(aergo::module::ISharedData* data) noexcept = 0;
    };
}