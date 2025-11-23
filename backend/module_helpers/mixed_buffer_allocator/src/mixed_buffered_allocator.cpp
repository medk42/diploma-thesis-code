#include "module_helpers/mixed_buffer_allocator/mixed_buffered_allocator.h"

using namespace aergo::module::helpers::mixed_buffer_allocator;



std::unique_ptr<MixedBufferedAllocator> MixedBufferedAllocator::create(aergo::module::BaseModule* base_module, uint64_t slot_size_bytes, uint32_t number_of_slots)
{
    auto static_allocator = base_module->createBufferAllocator(slot_size_bytes, number_of_slots);
    if (!static_allocator)
        return nullptr;

    auto dynamic_allocator = base_module->createDynamicAllocator();
    if (!dynamic_allocator)
        return nullptr;

    return std::unique_ptr<MixedBufferedAllocator>(new MixedBufferedAllocator(std::move(static_allocator), std::move(dynamic_allocator), slot_size_bytes)); // MixerBufferedAllocator only has a private constructor
}


aergo::module::message::SharedDataBlob MixedBufferedAllocator::allocate(uint64_t number_of_bytes) noexcept
{
    if (number_of_bytes <= static_slot_size_bytes_)
    {
        auto blob = static_allocator_->allocate(number_of_bytes);
        if (blob.valid())
            return blob;
    }

    return dynamic_allocator_->allocate(number_of_bytes);
}