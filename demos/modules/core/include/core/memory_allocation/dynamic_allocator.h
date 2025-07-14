#pragma once

#include "module_common/module_interface_threads.h"

#include "shared_data_core.h"
#include <map>


namespace aergo::core::memory_allocation
{
    template <typename shared_data_t>
    class DynamicAllocator : public aergo::module::IAllocatorCore
    {
    public:
        DynamicAllocator() : allocation_id_(0) {}

        virtual aergo::module::ISharedData* allocate(uint64_t number_of_bytes) override final;
        virtual void addOwner(aergo::module::ISharedData* data) override final;
        virtual void removeOwner(aergo::module::ISharedData* data) override final;

    private:
        std::map<uint64_t, shared_data_t> allocated_data_;
        uint64_t allocation_id_;
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

    // success is guaranteed true since new_id never existed
    return &it->second; 
}