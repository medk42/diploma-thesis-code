#pragma once

#include "module_common/module_interface_threads.h"

#include "core/logging/logger.h"
#include "shared_data_core.h"
#include <map>


namespace aergo::core::memory_allocation
{
    template <typename shared_data_t>
    class DynamicAllocator : public aergo::module::IAllocatorCore
    {
    public:
        DynamicAllocator(aergo::core::logging::ILogger& logger) : allocation_id_(0), logger_(logger) {}

        virtual aergo::module::ISharedData* allocate(uint64_t number_of_bytes) override final;
        virtual void addOwner(aergo::module::ISharedData* data) override final;
        virtual void removeOwner(aergo::module::ISharedData* data) override final;

    private:
        void log(aergo::module::logging::LogType log_type, const char* message);

        std::map<uint64_t, shared_data_t> allocated_data_;
        uint64_t allocation_id_;
        aergo::core::logging::ILogger& logger_;
    };
}



/// IMPL ///



template <typename shared_data_t>
aergo::module::ISharedData* aergo::core::memory_allocation::DynamicAllocator<shared_data_t>::allocate(uint64_t number_of_bytes)
{
    uint64_t new_id = allocation_id_++;

    auto [it, success] = allocated_data_.emplace(
        new_id,
        shared_data_t::allocate(number_of_bytes, new_id)
    );

    if (!it->second.valid())
    {
        log(aergo::module::logging::LogType::ERROR, "Failed to allocate memory.");
    }

    // success is guaranteed true since new_id never existed
    return &it->second; 
}



template <typename shared_data_t>
void aergo::core::memory_allocation::DynamicAllocator<shared_data_t>::addOwner(aergo::module::ISharedData* data)
{
    static_assert(std::is_base_of_v<aergo::module::ISharedData, shared_data_t>, "shared_data_t must derive from ISharedData");

    shared_data_t* data_core = dynamic_cast<shared_data_t*>(data);

    if (data_core && data_core->valid())
    {
        auto it = allocated_data_.find(data_core.id());
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



template <typename shared_data_t>
void aergo::core::memory_allocation::DynamicAllocator<shared_data_t>::removeOwner(aergo::module::ISharedData* data)
{
    static_assert(std::is_base_of_v<aergo::module::ISharedData, shared_data_t>, "shared_data_t must derive from ISharedData");

    shared_data_t* data_core = dynamic_cast<shared_data_t*>(data);

    if (data_core && data_core->valid())
    {
        auto it = allocated_data_.find(data_core.id());
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



template <typename shared_data_t>
void aergo::core::memory_allocation::DynamicAllocator<shared_data_t>::log(aergo::module::logging::LogType log_type, const char* message)
{
    logger_.log(aergo::core::logging::SourceType::CORE, "DynamicAllocator", 0, log_type, message);
}