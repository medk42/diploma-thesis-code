#include "utils/memory_allocation/static_allocator.h"

using namespace aergo::core::memory_allocation;



StaticAllocator::StaticAllocator(uint64_t slot_size_bytes, uint32_t number_of_slots, aergo::core::logging::ILogger* logger, IMemoryAllocator* custom_allocator)
: logger_(logger)
{
    if (custom_allocator)
    {
        memory_allocator_ = custom_allocator;
    }
    else
    {
        memory_allocator_ = &default_memory_allocator_;
    }

    preallocated_data_.reserve(number_of_slots);

    for (uint32_t i = 0; i < number_of_slots; ++i)
    {
        preallocated_data_.emplace_back(std::move(SharedDataCore::allocate(memory_allocator_, slot_size_bytes, i)));
        if (!preallocated_data_[i].valid())
        {
            log(aergo::module::logging::LogType::ERROR, "Failed to initialize StaticAllocator");
            throw StaticAllocatorInitializationException();
        }

        free_memory_slot_ids_.push_back(i);
    }
}



aergo::module::message::SharedDataBlob StaticAllocator::allocate(uint64_t number_of_bytes) noexcept { return allocateImpl(); }
void StaticAllocator::addOwner(aergo::module::ISharedData* data) noexcept { addOwnerImpl(data); }
void StaticAllocator::removeOwner(aergo::module::ISharedData* data) noexcept { removeOwnerImpl(data); }



aergo::module::message::SharedDataBlob StaticAllocator::allocateImpl()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (free_memory_slot_ids_.empty())
    {
        return aergo::module::message::SharedDataBlob();
    }

    size_t free_id = free_memory_slot_ids_.front();
    free_memory_slot_ids_.pop_front();

    allocated_memory_slots_.insert((size_t)(&preallocated_data_[free_id]));

    return aergo::module::message::SharedDataBlob(&preallocated_data_[free_id], this);
}



void StaticAllocator::addOwnerImpl(aergo::module::ISharedData* data)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!allocated_memory_slots_.contains((std::size_t)data))
    {
        log(aergo::module::logging::LogType::ERROR, "Attempting to add owner on invalid or unowned data.");
        return;
    }
    
    SharedDataCore* data_core = dynamic_cast<SharedDataCore*>(data);

    if (data_core && data_core->valid())
    {
        data_core->increaseCounter();
    }
    else if (!data)
    {
        log(aergo::module::logging::LogType::ERROR, "Attempting to add owner to nullptr.");
    }
    else if (!data_core)
    {
        log(aergo::module::logging::LogType::ERROR, "Dynamic cast failed in add owner");
    }
    else if (!data_core->valid())
    {
        log(aergo::module::logging::LogType::ERROR, "Attempting to add owner to invalid data.");
    }
}



void StaticAllocator::removeOwnerImpl(aergo::module::ISharedData* data)
{
    std::lock_guard<std::mutex> lock(mutex_);

     if (!allocated_memory_slots_.contains((std::size_t)data))
    {
        log(aergo::module::logging::LogType::ERROR, "Attempting to remove owner from invalid or unowned data.");
        return;
    }

    SharedDataCore* data_core = dynamic_cast<SharedDataCore*>(data);

    if (data_core && data_core->valid())
    {
        data_core->decreaseCounter();
        if (data_core->counter() == 0)
        {
            free_memory_slot_ids_.push_back(data_core->id());
            allocated_memory_slots_.erase((std::size_t)data);
        }
    }
    else if (!data)
    {
        log(aergo::module::logging::LogType::ERROR, "Attempting to remove owner from nullptr.");
    }
    else if (!data_core)
    {
        log(aergo::module::logging::LogType::ERROR, "Dynamic cast failed in remove owner.");
    }
    else if (!data_core->valid())
    {
        log(aergo::module::logging::LogType::ERROR, "Attempting to remove owner from invalid data.");
    }
}



void StaticAllocator::log(aergo::module::logging::LogType log_type, const char* message)
{
    logger_->log(aergo::core::logging::SourceType::CORE, "StaticAllocator", 0, log_type, message);
}