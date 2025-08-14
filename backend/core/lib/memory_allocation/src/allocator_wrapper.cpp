#include "utils/memory_allocation/allocator_wrapper.h"

using namespace aergo::core::memory_allocation;



AllocatorWrapper::AllocatorWrapper(std::unique_ptr<ICoreAllocator> allocator)
: allocator_(std::move(allocator)) {}



aergo::module::message::SharedDataBlob AllocatorWrapper::allocate(uint64_t number_of_bytes) noexcept
{
    aergo::module::ISharedData* data = allocator_->allocate(number_of_bytes);
    if (data)
    {
        return aergo::module::message::SharedDataBlob(data, this);
    }
    else
    {
        return aergo::module::message::SharedDataBlob();
    }
}



void AllocatorWrapper::addOwner(aergo::module::ISharedData* data) noexcept
{
    allocator_->addOwner(data);
}



void AllocatorWrapper::removeOwner(aergo::module::ISharedData* data) noexcept
{
    allocator_->removeOwner(data);
}