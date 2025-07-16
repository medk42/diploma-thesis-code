#pragma once

namespace aergo::core::memory_allocation
{
    class IMemoryAllocator
    {
    public:
        virtual ~IMemoryAllocator() = default;

        virtual void* malloc(size_t size) = 0;
        virtual void free(void* block) = 0;
    };

    class DefaultAllocator : public IMemoryAllocator
    {
    public:
        void* malloc(size_t size) override;
        void free(void* block) override;
    };
}