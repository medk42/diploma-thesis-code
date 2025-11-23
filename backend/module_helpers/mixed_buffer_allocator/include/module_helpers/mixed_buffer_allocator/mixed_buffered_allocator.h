#pragma once

#include <cstddef>
#include <memory>

#include "module_common/base_module.h"

namespace aergo::module::helpers::mixed_buffer_allocator
{
    class MixedBufferedAllocator : public aergo::module::IAllocator
    {
    public:
        /// @brief Create a mixed buffered allocator for shared data (to avoid copying large data). 
        /// Uses base_module to create a static buffered allocator for small allocations and a dynamic allocator for large allocations.
        /// On allocate, if requested size is less than or equal to slot_size_bytes, allocation is done from the static buffered allocator,
        /// otherwise (or if static allocation fails) allocation is done from the dynamic allocator.
        ///
        /// This maintains the benefits of both static buffered allocation (low latency, no fragmentation for small allocations)
        /// and dynamic allocation (can handle large allocations, no failures due to buffer exhaustion).
        /// @param base_module Base module to use for allocator creation.
        /// @param slot_size_bytes Fixed allocation size in bytes.
        /// @param number_of_slots Number of "size_bytes" sized slots.
        /// @return New allocator or nullptr on failure.
        static std::unique_ptr<MixedBufferedAllocator> create(aergo::module::BaseModule* base_module, uint64_t slot_size_bytes, uint32_t number_of_slots);

        ~MixedBufferedAllocator() override = default;

        /// @brief Allocate "number_of_bytes" bytes of shared memory. If number_of_bytes is less than or equal to slot_size_bytes,
        /// allocation is attempted from the static buffered allocator first, otherwise (or on failure) from the dynamic allocator.
        /// Only if both allocations fail, an invalid SharedDataBlob is returned.
        /// @param number_of_bytes Number of bytes to allocate. The returned SharedDataBlob will have size at least equal to number_of_bytes,
        /// possibly larger if allocated from the static buffered allocator.
        /// @return SharedDataBlob, check for validity by calling the valid() function. 
        message::SharedDataBlob allocate(uint64_t number_of_bytes) noexcept override;

    protected:
        // ownership handled by inner allocators, no action needed here
        virtual void addOwner(ISharedData* data) noexcept override {}
        virtual void removeOwner(ISharedData* data) noexcept override {}

    private:
        MixedBufferedAllocator(BaseModule::AllocatorPtr static_allocator, BaseModule::AllocatorPtr dynamic_allocator, uint64_t static_slot_size_bytes)
            : static_allocator_(std::move(static_allocator)), dynamic_allocator_(std::move(dynamic_allocator)), static_slot_size_bytes_(static_slot_size_bytes) {}

        BaseModule::AllocatorPtr static_allocator_;
        BaseModule::AllocatorPtr dynamic_allocator_;
        uint64_t static_slot_size_bytes_;
    };
}