#include "utils/memory_allocation/shared_data_core.h"

using namespace aergo::core::memory_allocation;



SharedDataCore::SharedDataCore() noexcept
: memory_allocator_(nullptr), data_(nullptr), size_(0), id_(0), valid_(false), counter_(0) {}



SharedDataCore::SharedDataCore(IMemoryAllocator* memory_allocator, uint8_t* data, uint64_t size, uint64_t id) 
: memory_allocator_(memory_allocator), data_(data), size_(size), id_(id), valid_(true), counter_(0) {}



SharedDataCore::~SharedDataCore()
{
    if (valid_)
    {
        memory_allocator_->free(data_);
        
        memory_allocator_ = nullptr;
        valid_ = false;
        data_ = nullptr;
        size_ = 0;
        id_ = 0;
        counter_ = 0;
    }
}



SharedDataCore::SharedDataCore(SharedDataCore&& other) noexcept
: memory_allocator_(other.memory_allocator_), data_(other.data_), size_(other.size_), id_(other.id_), valid_(other.valid_), counter_(other.counter_)
{
    other.memory_allocator_ = nullptr;
    other.data_ = nullptr;
    other.size_ = 0;
    other.id_ = 0;
    other.valid_ = false;
    other.counter_ = 0;
}



SharedDataCore& SharedDataCore::operator=(SharedDataCore&& other) noexcept
{
    if (this != &other) {
        memory_allocator_ = other.memory_allocator_;
        data_ = other.data_;
        size_ = other.size_;
        id_ = other.id_;
        valid_ = other.valid_;
        counter_ = other.counter_;
        other.memory_allocator_ = nullptr;
        other.data_ = nullptr;
        other.size_ = 0;
        other.id_ = 0;
        other.valid_ = false;
        other.counter_ = 0;
    }
    return *this;
}



bool SharedDataCore::valid() noexcept
{
    return valid_;
}



uint8_t* SharedDataCore::data() noexcept
{
    return data_;
}



uint64_t SharedDataCore::size() noexcept
{
    return size_;
}



uint64_t SharedDataCore::id()
{
    return id_;
}



uint64_t SharedDataCore::counter()
{
    return counter_;
}



void SharedDataCore::increaseCounter()
{
    if (valid_)
    {
        ++counter_;
    }
}



void SharedDataCore::decreaseCounter()
{
    if (counter_ > 0 && valid_)
    {
        --counter_;
    }
}



SharedDataCore SharedDataCore::allocate(IMemoryAllocator* allocator, uint64_t size, uint64_t id) noexcept
{
    void* data = allocator->malloc(size);
    if (data)
    {
        return SharedDataCore(allocator, (uint8_t*)data, size, id);
    }
    else
    {
        return SharedDataCore();
    }
}