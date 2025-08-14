#pragma once

#include "allocator_interface_core.h"
#include "module_common/module_interface_.h"

#include <memory>

namespace aergo::core::memory_allocation
{
    class AllocatorWrapper : public aergo::module::IAllocator
    {
    public:
        AllocatorWrapper(std::unique_ptr<ICoreAllocator> allocator);
        
        /// @brief Allocate "number_of_bytes" bytes of shared memory. If the allocator has fixed byte size, "number_of_bytes" parameter is ignored.
        /// @return SharedDataBlob, check for validity by calling the valid() function
        virtual aergo::module::message::SharedDataBlob allocate(uint64_t number_of_bytes) noexcept override final;

    protected:
        /// @brief Add owner for shared data object. Object removed when owners drop to zero.
        virtual void addOwner(aergo::module::ISharedData* data) noexcept override final;

        /// @brief Remove owner from shared data object. Object removed when owners drop to zero.
        virtual void removeOwner(aergo::module::ISharedData* data) noexcept override final;

    private:
        std::unique_ptr<ICoreAllocator> allocator_;
    };
};