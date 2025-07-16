#include "utils/memory_allocation/dynamic_allocator.h"

using namespace aergo::core::memory_allocation;



DynamicAllocator::DynamicAllocator(aergo::core::logging::ILogger& logger, IMemoryAllocator* custom_allocator)
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



aergo::module::ISharedData* DynamicAllocator::allocate(uint64_t number_of_bytes) noexcept
{
    uint64_t new_id = allocation_id_++;

    auto [it, success] = allocated_data_.emplace(
        new_id,
        SharedDataCore::allocate(memory_allocator_, number_of_bytes, new_id)
    );

    if (!it->second.valid())
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to allocate memory.");
    }

    // success is guaranteed true since new_id never existed
    return &it->second; 
}



void DynamicAllocator::addOwner(aergo::module::ISharedData* data) noexcept
{
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



void DynamicAllocator::removeOwner(aergo::module::ISharedData* data) noexcept
{
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
    logger_.log(aergo::core::logging::SourceType::CORE, "DynamicAllocator", 0, log_type, message);
}