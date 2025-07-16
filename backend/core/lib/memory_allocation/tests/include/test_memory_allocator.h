#pragma once

#include "utils/memory_allocation/memory_allocator.h"

class TestMemoryAllocator : public aergo::core::memory_allocation::IMemoryAllocator
{
public:
    struct Op
    {
        enum class Type { MALLOC, FREE };

        Type type_;
        size_t size_;
        uint64_t address_;
    };

    TestMemoryAllocator(bool working) : given_address_(1), working_(working) {}

    void* malloc(size_t size) override
    {
        if (working_)
        {
            operations_.push_back({ Op::Type::MALLOC, size, given_address_ });
            return (void*)given_address_++;
        }
        else
        {
            operations_.push_back({ Op::Type::MALLOC, size, 0 });
            return nullptr;
        }
    }

    void free(void* block) override
    {
        operations_.push_back({ Op::Type::FREE, 0, (uint64_t)block });
    }

    std::vector<Op>& operations()
    {
        return operations_;
    }

private:
    uint64_t given_address_;
    std::vector<Op> operations_;
    bool working_;
};