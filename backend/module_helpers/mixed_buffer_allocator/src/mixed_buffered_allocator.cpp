#include "module_helpers/mixed_buffer_allocator/mixed_buffered_allocator.h"

#include <iostream>

using namespace aergo::module::helpers::mixed_buffer_allocator;


MixedBufferedAllocator::~MixedBufferedAllocator()
{
    std::cout << "MixedBufferedAllocator Statistics (slot size: " << static_slot_size_bytes_ << " bytes, number of slots: " << number_of_slots_ << "):\n";
    std::cout << "  Allocations served from static buffered allocator: " << allocated_from_static_ << "\n";
    std::cout << "  Allocations served from dynamic allocator due to static exhaustion: " << allocated_from_dynamic_exhaustion_ << "\n";
    std::cout << "  Allocations served from dynamic allocator due to large request: " << allocated_from_dynamic_large_request_ << "\n";
    std::cout << "  Total allocation failures: " << allocation_failures_ << "\n";
    std::cout << "  Maximum allocated size: " << max_allocated_size_ << " bytes\n";
}


std::unique_ptr<MixedBufferedAllocator> MixedBufferedAllocator::create(aergo::module::BaseModule* base_module, uint64_t slot_size_bytes, uint32_t number_of_slots)
{
    auto static_allocator = base_module->createBufferAllocator(slot_size_bytes, number_of_slots);
    if (!static_allocator)
        return nullptr;

    auto dynamic_allocator = base_module->createDynamicAllocator();
    if (!dynamic_allocator)
        return nullptr;

    return std::unique_ptr<MixedBufferedAllocator>(new MixedBufferedAllocator(std::move(static_allocator), std::move(dynamic_allocator), slot_size_bytes, number_of_slots)); // MixerBufferedAllocator only has a private constructor
}


aergo::module::message::SharedDataBlob MixedBufferedAllocator::allocate(uint64_t number_of_bytes) noexcept
{
    if (number_of_bytes > max_allocated_size_)
        max_allocated_size_ = number_of_bytes;

    if (number_of_bytes <= static_slot_size_bytes_)
    {
        auto blob = static_allocator_->allocate(number_of_bytes);
        if (blob.valid())
        {
            ++allocated_from_static_;
            return std::move(blob);
        }
    }

    auto blob = dynamic_allocator_->allocate(number_of_bytes);

    if (blob.valid())
    {
        if (number_of_bytes <= static_slot_size_bytes_)
            ++allocated_from_dynamic_exhaustion_;
        else
            ++allocated_from_dynamic_large_request_;
    }
    else
        ++allocation_failures_;

    return std::move(blob);
}