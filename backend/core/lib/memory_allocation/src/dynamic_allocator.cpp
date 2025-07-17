#include "utils/memory_allocation/dynamic_allocator.h"

using namespace aergo::core::memory_allocation;



DynamicAllocator::DynamicAllocator(aergo::core::logging::ILogger* logger, IMemoryAllocator* custom_allocator)
: allocation_id_(0), logger_(logger)
{
    if (custom_allocator)
    {
        memory_allocator_ = custom_allocator;
    }
    else
    {
        memory_allocator_ = &default_memory_allocator_;
    }
}



aergo::module::ISharedData* DynamicAllocator::allocate(uint64_t number_of_bytes) noexcept { return allocateImpl(number_of_bytes); }
void DynamicAllocator::addOwner(aergo::module::ISharedData* data) noexcept { addOwnerImpl(data); }
void DynamicAllocator::removeOwner(aergo::module::ISharedData* data) noexcept { removeOwnerImpl(data); }


aergo::module::ISharedData* DynamicAllocator::allocateImpl(uint64_t number_of_bytes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t new_id = allocation_id_++;

    SharedDataCore new_data(SharedDataCore::allocate(memory_allocator_, number_of_bytes, new_id));

    if (new_data.valid())
    {
        // success is guaranteed true since new_id never existed
        auto [it, success] = allocated_data_.emplace(
            new_id,
            std::move(new_data)  
        );
        allocated_memory_slots_.insert((std::size_t)(&it->second));

        return &it->second; 
    }
    else
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to allocate memory.");
        return nullptr;
    }
    
}



void DynamicAllocator::addOwnerImpl(aergo::module::ISharedData* data)
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
        auto it = allocated_data_.find(data_core->id());
        if (it != allocated_data_.end() && (data == &it->second))
        {
            data_core->increaseCounter();
        }
        else
        {
            log(aergo::module::logging::LogType::ERROR, "Attempting to add owner on unowned data.");
        }
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



void DynamicAllocator::removeOwnerImpl(aergo::module::ISharedData* data)
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
        auto it = allocated_data_.find(data_core->id());
        if (it != allocated_data_.end() && (data == &it->second))
        {
            data_core->decreaseCounter();
            if (data_core->counter() == 0)
            {
                allocated_data_.erase(it);
                allocated_memory_slots_.erase((std::size_t)data);
            }
        }
        else
        {
            log(aergo::module::logging::LogType::ERROR, "Attempting to remove owner on unowned data.");
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



void DynamicAllocator::log(aergo::module::logging::LogType log_type, const char* message)
{
    logger_->log(aergo::core::logging::SourceType::CORE, "DynamicAllocator", 0, log_type, message);
}