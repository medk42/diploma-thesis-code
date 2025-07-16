#include <catch2/catch_test_macros.hpp>

#include "utils/memory_allocation/dynamic_allocator.h"
#include <vector>

using namespace aergo::core::memory_allocation;

class TestMemoryAllocator : public IMemoryAllocator
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


TEST_CASE( "SharedDataCore, broken allocator", "[shared_data_core]" )
{
    TestMemoryAllocator allocator(false);


    uint64_t alloc_size = 1000;
    {
        uint64_t alloc_id = 1;

        REQUIRE(allocator.operations().size() == 0);
        SharedDataCore data(SharedDataCore::allocate(&allocator, alloc_size, alloc_id));
        REQUIRE(!data.valid());
        REQUIRE(allocator.operations().size() == 1);
        REQUIRE(allocator.operations()[0].type_ == TestMemoryAllocator::Op::Type::MALLOC);
        REQUIRE(allocator.operations()[0].size_ == alloc_size);
        REQUIRE(allocator.operations()[0].address_ == 0);
    }

    REQUIRE(allocator.operations().size() == 1);

    

    {
        uint64_t alloc_size_2 = 42;
        uint64_t alloc_id = 23;

        REQUIRE(allocator.operations().size() == 1);
        SharedDataCore data(SharedDataCore::allocate(&allocator, alloc_size_2, alloc_id));
        REQUIRE(!data.valid());
        REQUIRE(allocator.operations().size() == 2);
        REQUIRE(allocator.operations()[0].type_ == TestMemoryAllocator::Op::Type::MALLOC);
        REQUIRE(allocator.operations()[0].size_ == alloc_size);
        REQUIRE(allocator.operations()[0].address_ == 0);
        REQUIRE(allocator.operations()[1].type_ == TestMemoryAllocator::Op::Type::MALLOC);
        REQUIRE(allocator.operations()[1].size_ == alloc_size_2);
        REQUIRE(allocator.operations()[1].address_ == 0);
    }

    REQUIRE(allocator.operations().size() == 2);
}

TEST_CASE( "SharedDataCore, working allocator", "[shared_data_core]" )
{
    TestMemoryAllocator allocator(true);


    {
        uint64_t alloc_size = 1000;
        uint64_t alloc_id = 1;

        REQUIRE(allocator.operations().size() == 0);
        SharedDataCore data(SharedDataCore::allocate(&allocator, alloc_size, alloc_id));

        REQUIRE(data.valid());
        REQUIRE(data.counter() == 0);
        REQUIRE(data.id() == alloc_id);
        REQUIRE(data.size() == alloc_size);

        REQUIRE(allocator.operations().size() == 1);
        REQUIRE(allocator.operations()[0].type_ == TestMemoryAllocator::Op::Type::MALLOC);
        REQUIRE(allocator.operations()[0].size_ == alloc_size);
        REQUIRE(allocator.operations()[0].address_ == 1);
        REQUIRE(allocator.operations()[0].address_ == (uint64_t)data.data());

        data.increaseCounter();
        REQUIRE(data.counter() == 1);
        REQUIRE(data.valid());
        data.increaseCounter();
        REQUIRE(data.counter() == 2);
        REQUIRE(data.valid());
        data.decreaseCounter();
        REQUIRE(data.counter() == 1);
        REQUIRE(data.valid());
        data.decreaseCounter();
        REQUIRE(data.counter() == 0);
        REQUIRE(data.valid());
        data.decreaseCounter();
        REQUIRE(data.counter() == 0);
        REQUIRE(data.valid());
        
        data.increaseCounter();

        SharedDataCore data2 = std::move(data);
        REQUIRE(!data.valid());
        REQUIRE(data.data() == nullptr);
        REQUIRE(data2.valid());
        REQUIRE(data2.id() == alloc_id);
        REQUIRE(data2.counter() == 1);
        REQUIRE(data2.size() == alloc_size);
        REQUIRE((uint64_t)data2.data() == allocator.operations()[0].address_);
        REQUIRE(allocator.operations().size() == 1);

        REQUIRE(data.counter() == 0);
        data.increaseCounter();
        REQUIRE(data.counter() == 0);

        data2.increaseCounter();

        SharedDataCore data3(std::move(data2));
        REQUIRE(!data2.valid());
        REQUIRE(data2.data() == nullptr);
        REQUIRE(data3.valid());
        REQUIRE(data3.id() == alloc_id);
        REQUIRE(data3.counter() == 2);
        REQUIRE(data3.size() == alloc_size);
        REQUIRE((uint64_t)data3.data() == allocator.operations()[0].address_);
        REQUIRE(allocator.operations().size() == 1);
    }

    REQUIRE(allocator.operations().size() == 2);
    REQUIRE(allocator.operations()[1].type_ == TestMemoryAllocator::Op::Type::FREE);
    REQUIRE(allocator.operations()[1].address_ == allocator.operations()[0].address_);

    {
        uint64_t alloc_size = 3242;
        uint64_t alloc_id = 23;
        
        REQUIRE(allocator.operations().size() == 2);
        SharedDataCore data(SharedDataCore::allocate(&allocator, alloc_size, alloc_id));

        REQUIRE(data.valid());
        REQUIRE(data.counter() == 0);
        REQUIRE(data.id() == alloc_id);
        REQUIRE(data.size() == alloc_size);

        REQUIRE(allocator.operations().size() == 3);
        REQUIRE(allocator.operations()[2].type_ == TestMemoryAllocator::Op::Type::MALLOC);
        REQUIRE(allocator.operations()[2].size_ == alloc_size);
        REQUIRE(allocator.operations()[2].address_ == 2);
        REQUIRE(allocator.operations()[2].address_ == (uint64_t)data.data());

        data.increaseCounter();

        SharedDataCore data3(std::move(data));
        REQUIRE(!data.valid());
        REQUIRE(data.data() == nullptr);
        REQUIRE(data3.valid());
        REQUIRE(data3.id() == alloc_id);
        REQUIRE(data3.counter() == 1);
        REQUIRE(data3.size() == alloc_size);
        REQUIRE((uint64_t)data3.data() == allocator.operations()[2].address_);
        REQUIRE(allocator.operations().size() == 3);
    }
    
    REQUIRE(allocator.operations().size() == 4);
    REQUIRE(allocator.operations()[3].type_ == TestMemoryAllocator::Op::Type::FREE);
    REQUIRE(allocator.operations()[3].address_ == allocator.operations()[2].address_);
}