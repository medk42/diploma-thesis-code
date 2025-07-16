#pragma once

#include "module_common/module_interface_threads.h"
#include "memory_allocator.h"

namespace aergo::core::memory_allocation
{
    class SharedDataCore : public aergo::module::ISharedData
    {
    public:
        SharedDataCore() noexcept;
        ~SharedDataCore() noexcept override;

        SharedDataCore(const SharedDataCore& other) = delete;
        SharedDataCore(SharedDataCore&& other) noexcept;

        SharedDataCore& operator=(const SharedDataCore& other) = delete;
        SharedDataCore& operator=(SharedDataCore&& other) noexcept;

        virtual bool valid() override final;
        virtual uint8_t* data() override final;
        virtual uint64_t size() override final;
        uint64_t id();

        uint64_t counter();
        void increaseCounter();

        /// @brief Does not decrease below zero. 
        void decreaseCounter();

        static SharedDataCore allocate(IMemoryAllocator* memory_allocator, uint64_t size, uint64_t id) noexcept;

    private:
        SharedDataCore(IMemoryAllocator* memory_allocator, uint8_t* data, uint64_t size, uint64_t id);
    
        IMemoryAllocator* memory_allocator_;
        uint8_t* data_;
        uint64_t size_;
        uint64_t id_;
        bool valid_;
        uint64_t counter_;
    };
}