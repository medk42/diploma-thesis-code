#include "module_common/module_interface.h"
#include "module_common/module_interface_threads.h"

#include <utility>

using namespace aergo::module;
using namespace message;



Allocator::Allocator(ICore* core, IAllocatorCore* allocator)
: core_(core), allocator_(allocator) {}



Allocator::~Allocator()
{
    core_->deleteAllocator(allocator_);
}



SharedDataBlob Allocator::allocate(uint64_t number_of_bytes)
{
    ISharedData* data = allocator_->allocate(number_of_bytes);
    if (data)
    {
        return SharedDataBlob(data, allocator_);
    }
    else
    {
        return SharedDataBlob();
    }
}


SharedDataBlob::SharedDataBlob()
: data_(nullptr), allocator_(nullptr) {}

SharedDataBlob::SharedDataBlob(ISharedData* data, IAllocatorCore* allocator)
: data_(data), allocator_(allocator)
{
    if (allocator_)
    {
        allocator_->addOwner(data_);
    }
}



SharedDataBlob::~SharedDataBlob()
{
    if (allocator_)
    {
        allocator_->removeOwner(data_);
    }
}



SharedDataBlob::SharedDataBlob(const SharedDataBlob& other)
: data_(other.data_), allocator_(other.allocator_)
{
    if (allocator_)
    {
        allocator_->addOwner(data_);
    }
}



SharedDataBlob& SharedDataBlob::operator=(SharedDataBlob& other)
{
    allocator_ = other.allocator_;
    data_ = other.data_;
    if (allocator_)
    {
        allocator_->addOwner(data_);
    }

    return *this;
}



SharedDataBlob::SharedDataBlob(SharedDataBlob&& other) noexcept
: data_(other.data_), allocator_(other.allocator_)
{
    other.data_ = nullptr;
    other.allocator_ = nullptr;
}



SharedDataBlob& SharedDataBlob::operator=(SharedDataBlob&& other) noexcept
{
    if (this != &other)
    {
        data_ = other.data_;
        allocator_ = other.allocator_;

        other.data_ = nullptr;
        other.allocator_ = nullptr;
    }

    return *this;
}



bool SharedDataBlob::valid()
{
    return allocator_ != nullptr && data_ != nullptr && data_->valid();
}



uint8_t* SharedDataBlob::data()
{
    return data_->data();
}



uint64_t SharedDataBlob::size()
{
    return data_->size();
}