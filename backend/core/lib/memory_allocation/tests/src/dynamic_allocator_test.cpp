#include <catch2/catch_test_macros.hpp>

#include "test_memory_allocator.h"
#include "test_logger.h"
#include "utils/memory_allocation/dynamic_allocator.h"

#include <algorithm>

using namespace aergo::core::memory_allocation;
using namespace aergo::core::logging;




TEST_CASE( "DynamicAllocator", "[dynamic_allocator]" )
{
    SECTION("broken allocator")
    {
        TestMemoryAllocator memory_allocator(false);

        TestLogger logger;
        DynamicAllocator dynamic_allocator(&logger, &memory_allocator);
        
        SECTION( "add/remove owner on invalid data" )
        {
            REQUIRE(memory_allocator.operations().size() == 0);
            REQUIRE(logger.logs().size() == 0);

            REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl((aergo::module::ISharedData*) nullptr));

            REQUIRE(memory_allocator.operations().size() == 0);
            REQUIRE(logger.logs().size() == 1);
            REQUIRE(logger.logs()[0] == aergo::module::logging::LogType::ERROR);

            REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl((aergo::module::ISharedData*) nullptr));

            REQUIRE(memory_allocator.operations().size() == 0);
            REQUIRE(logger.logs().size() == 2);
            REQUIRE(logger.logs()[1] == aergo::module::logging::LogType::ERROR);
            

            REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl((aergo::module::ISharedData*) 1));
            REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl((aergo::module::ISharedData*) 2));
            REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl((aergo::module::ISharedData*) 3));
            REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl((aergo::module::ISharedData*) 1));
            REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl((aergo::module::ISharedData*) 2));
            REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl((aergo::module::ISharedData*) 3));

            REQUIRE(memory_allocator.operations().size() == 0);
            REQUIRE(logger.logs().size() == 8);
            for (int i = 3; i < 8; ++i)
                REQUIRE(logger.logs()[i] == aergo::module::logging::LogType::ERROR);
        }

        SECTION( "data allocation tests" )
        {
            uint64_t alloc_size = 1000;
            aergo::module::ISharedData* data;
            REQUIRE_NOTHROW(data = dynamic_allocator.allocateImpl(alloc_size));
            REQUIRE(data == nullptr);
            
            REQUIRE(memory_allocator.operations().size() == 1);
            REQUIRE(memory_allocator.operations()[0].type_ == TestMemoryAllocator::Op::Type::MALLOC);
            REQUIRE(memory_allocator.operations()[0].size_ == alloc_size);
            REQUIRE(memory_allocator.operations()[0].address_ == 0);
            REQUIRE(logger.logs().size() == 1);
            REQUIRE(logger.logs()[0] == aergo::module::logging::LogType::ERROR);
        }
    }

    SECTION("working allocator")
    {
        TestMemoryAllocator memory_allocator(true);

        TestLogger logger;
        DynamicAllocator dynamic_allocator(&logger, &memory_allocator);

        uint64_t alloc_size = 1000;
        aergo::module::ISharedData* data;
        REQUIRE_NOTHROW(data = dynamic_allocator.allocateImpl(alloc_size));
        REQUIRE(data != nullptr);

        REQUIRE(data->valid());
        REQUIRE(data->data() == (uint8_t*)1);
        REQUIRE(data->size() == alloc_size);
        REQUIRE(memory_allocator.operations().size() == 1);
        REQUIRE(memory_allocator.operations()[0].type_ == TestMemoryAllocator::Op::Type::MALLOC);
        REQUIRE(memory_allocator.operations()[0].size_ == alloc_size);
        REQUIRE(memory_allocator.operations()[0].address_ == 1);
        REQUIRE(memory_allocator.operations()[0].address_ == (uint64_t)data->data());
        REQUIRE(logger.logs().size() == 0);

        SharedDataCore* data_core = dynamic_cast<SharedDataCore*>(data);

        REQUIRE(data_core->counter() == 0);
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data));
        REQUIRE(data_core->counter() == 1);
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data));
        REQUIRE(data_core->counter() == 2);
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data));
        REQUIRE(data_core->counter() == 1);
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data));
        REQUIRE(data_core->counter() == 2);
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data));
        REQUIRE(data_core->counter() == 3);
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data));
        REQUIRE(data_core->counter() == 1);
        REQUIRE(memory_allocator.operations().size() == 1);
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data));
        REQUIRE(memory_allocator.operations().size() == 2);
        REQUIRE(memory_allocator.operations()[1].type_ == TestMemoryAllocator::Op::Type::FREE);
        REQUIRE(memory_allocator.operations()[1].address_ == 1);

        aergo::module::ISharedData *data1, *data2, *data3;
        
        REQUIRE_NOTHROW(data1 = dynamic_allocator.allocateImpl(alloc_size));
        REQUIRE(data1 != nullptr);
        REQUIRE(data1->valid());
        REQUIRE(data1->data() == (uint8_t*)2);
        REQUIRE(data1->size() == alloc_size);
        REQUIRE(memory_allocator.operations().size() == 3);
        REQUIRE(memory_allocator.operations()[2].type_ == TestMemoryAllocator::Op::Type::MALLOC);
        REQUIRE(memory_allocator.operations()[2].size_ == alloc_size);
        REQUIRE(memory_allocator.operations()[2].address_ == (uint64_t)data1->data());
        REQUIRE(logger.logs().size() == 0);
        
        REQUIRE_NOTHROW(data2 = dynamic_allocator.allocateImpl(alloc_size));
        REQUIRE(data2 != nullptr);
        REQUIRE(data2->valid());
        REQUIRE(data2->data() == (uint8_t*)3);
        REQUIRE(data2->size() == alloc_size);
        REQUIRE(memory_allocator.operations().size() == 4);
        REQUIRE(memory_allocator.operations()[3].type_ == TestMemoryAllocator::Op::Type::MALLOC);
        REQUIRE(memory_allocator.operations()[3].size_ == alloc_size);
        REQUIRE(memory_allocator.operations()[3].address_ == (uint64_t)data2->data());
        REQUIRE(logger.logs().size() == 0);
        
        REQUIRE_NOTHROW(data3 = dynamic_allocator.allocateImpl(alloc_size));
        REQUIRE(data3 != nullptr);
        REQUIRE(data3->valid());
        REQUIRE(data3->data() == (uint8_t*)4);
        REQUIRE(data3->size() == alloc_size);
        REQUIRE(memory_allocator.operations().size() == 5);
        REQUIRE(memory_allocator.operations()[4].type_ == TestMemoryAllocator::Op::Type::MALLOC);
        REQUIRE(memory_allocator.operations()[4].size_ == alloc_size);
        REQUIRE(memory_allocator.operations()[4].address_ == (uint64_t)data3->data());
        REQUIRE(logger.logs().size() == 0);

        SharedDataCore* data1_core = dynamic_cast<SharedDataCore*>(data1);
        SharedDataCore* data2_core = dynamic_cast<SharedDataCore*>(data2);
        SharedDataCore* data3_core = dynamic_cast<SharedDataCore*>(data3);
        REQUIRE(data1_core->counter() == 0);
        REQUIRE(data2_core->counter() == 0);
        REQUIRE(data3_core->counter() == 0);
        REQUIRE(memory_allocator.operations().size() == 5);

        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data2));
        REQUIRE(memory_allocator.operations().size() == 6);
        REQUIRE(memory_allocator.operations()[5].type_ == TestMemoryAllocator::Op::Type::FREE);
        REQUIRE(memory_allocator.operations()[5].address_ == 3);
        REQUIRE(logger.logs().size() == 0);


        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data1));
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data3));
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data1));
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data1));
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data3));
        REQUIRE(logger.logs().size() == 0);
        REQUIRE(data1_core->counter() == 3);
        REQUIRE(data3_core->counter() == 2);

        
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data3));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data1));
        REQUIRE(logger.logs().size() == 0);
        REQUIRE(data1_core->counter() == 2);
        REQUIRE(data3_core->counter() == 1);

        
        REQUIRE(memory_allocator.operations().size() == 6);
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data3));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data3));
        REQUIRE(logger.logs().size() == 1);
        REQUIRE(data1_core->counter() == 2);
        REQUIRE(memory_allocator.operations().size() == 7);
        REQUIRE(memory_allocator.operations()[6].type_ == TestMemoryAllocator::Op::Type::FREE);
        REQUIRE(memory_allocator.operations()[6].address_ == 4);
        
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data1));
        REQUIRE(data1_core->counter() == 1);
        REQUIRE(logger.logs().size() == 1);

        REQUIRE(memory_allocator.operations().size() == 7);
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data1));
        REQUIRE(logger.logs().size() == 1);
        REQUIRE(memory_allocator.operations().size() == 8);
        REQUIRE(memory_allocator.operations()[7].type_ == TestMemoryAllocator::Op::Type::FREE);
        REQUIRE(memory_allocator.operations()[7].address_ == 2);
        


        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(nullptr));
        REQUIRE(logger.logs().size() == 2);
        REQUIRE(logger.logs()[0] == aergo::module::logging::LogType::ERROR);
    }

    SECTION("real memory allocation test")
    {
        TestLogger logger;
        DynamicAllocator dynamic_allocator(&logger);

        aergo::module::ISharedData *data100, *data42, *data1100, *data100000;
        REQUIRE_NOTHROW(data100 = dynamic_allocator.allocateImpl(100));
        REQUIRE_NOTHROW(data42 = dynamic_allocator.allocateImpl(42));
        REQUIRE_NOTHROW(data1100 = dynamic_allocator.allocateImpl(1100));
        REQUIRE_NOTHROW(data100000 = dynamic_allocator.allocateImpl(100000));

        REQUIRE(data100->valid());
        REQUIRE(data42->valid());
        REQUIRE(data1100->valid());
        REQUIRE(data100000->valid());

        REQUIRE(data100->size() == 100);
        REQUIRE(data42->size() == 42);
        REQUIRE(data1100->size() == 1100);
        REQUIRE(data100000->size() == 100000);

        REQUIRE_NOTHROW(std::fill(data100->data(), data100->data() + 100, 100));
        REQUIRE_NOTHROW(std::fill(data42->data(), data42->data() + 42, 42));
        REQUIRE_NOTHROW(std::fill(data1100->data(), data1100->data() + 1100, 13));
        REQUIRE_NOTHROW(std::fill(data100000->data(), data100000->data() + 100000, 191));

        REQUIRE(std::all_of(data100->data(), data100->data() + 100, [](uint8_t x){ return x == 100; }));
        REQUIRE(std::all_of(data42->data(), data42->data() + 42, [](uint8_t x){ return x == 42; }));
        REQUIRE(std::all_of(data1100->data(), data1100->data() + 1100, [](uint8_t x){ return x == 13; }));
        REQUIRE(std::all_of(data100000->data(), data100000->data() + 100000, [](uint8_t x){ return x == 191; }));

        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data100));
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data100));
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data42));
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data1100));
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data1100));
        REQUIRE_NOTHROW(dynamic_allocator.addOwnerImpl(data1100));
        

        REQUIRE(std::all_of(data100->data(), data100->data() + 100, [](uint8_t x){ return x == 100; }));
        REQUIRE(std::all_of(data42->data(), data42->data() + 42, [](uint8_t x){ return x == 42; }));
        REQUIRE(std::all_of(data1100->data(), data1100->data() + 1100, [](uint8_t x){ return x == 13; }));
        REQUIRE(std::all_of(data100000->data(), data100000->data() + 100000, [](uint8_t x){ return x == 191; }));

        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data100));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data42));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data1100));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data100000));

        REQUIRE(logger.logs().size() == 0);

        REQUIRE(std::all_of(data100->data(), data100->data() + 100, [](uint8_t x){ return x == 100; }));
        REQUIRE(std::all_of(data1100->data(), data1100->data() + 1100, [](uint8_t x){ return x == 13; }));

        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data100));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data42));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data1100));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data100000));

        REQUIRE(logger.logs().size() == 2);

        REQUIRE(std::all_of(data1100->data(), data1100->data() + 1100, [](uint8_t x){ return x == 13; }));

        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data100));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data42));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data1100));
        REQUIRE_NOTHROW(dynamic_allocator.removeOwnerImpl(data100000));

        REQUIRE(logger.logs().size() == 5);
    }
}