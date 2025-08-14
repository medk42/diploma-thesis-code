#include <catch2/catch_test_macros.hpp>

#include "test_memory_allocator.h"
#include "test_logger.h"
#include "utils/memory_allocation/static_allocator.h"

#include <algorithm>
#include <array>

using namespace aergo::core::memory_allocation;



TEST_CASE( "StaticAllocator", "[static_allocator]" )
{
    TestLogger logger;

    uint64_t slot_size_bytes = 1000;
    const uint32_t number_of_slots = 100;

    SECTION("broken allocator")
    {
        TestMemoryAllocator memory_allocator(false);
        REQUIRE(logger.logs().size() == 0);
        REQUIRE_THROWS_AS(StaticAllocator(slot_size_bytes, number_of_slots, &logger, &memory_allocator), StaticAllocator::StaticAllocatorInitializationException);
        REQUIRE(logger.logs().size() == 1);
    }
    
    SECTION("working allocator")
    {
        TestMemoryAllocator memory_allocator(true);
        StaticAllocator static_allocator(slot_size_bytes, number_of_slots, &logger, &memory_allocator);

        REQUIRE(logger.logs().size() == 0);
        
        std::array<aergo::module::message::SharedDataBlob, number_of_slots> allocated_data;
        for (uint32_t i = 0; i < number_of_slots; ++i)
        {
            aergo::module::message::SharedDataBlob data;
            REQUIRE_NOTHROW(allocated_data[i] = static_allocator.allocateImpl());
            REQUIRE(allocated_data[i].valid());
            REQUIRE(allocated_data[i].size() == slot_size_bytes);
            REQUIRE(allocated_data[i].data() == (uint8_t*)((uint64_t)i + 1));
        }

        {
            aergo::module::message::SharedDataBlob data;
            REQUIRE_NOTHROW(data = static_allocator.allocateImpl());
            REQUIRE(!data.valid());
        }

        REQUIRE(logger.logs().size() == 0);

        uint8_t* data_42 = allocated_data[42].data();
        REQUIRE_NOTHROW(static_allocator.removeOwnerImpl((aergo::module::ISharedData*)allocated_data[42].data()));
        allocated_data[42] = aergo::module::message::SharedDataBlob();

        REQUIRE_NOTHROW(allocated_data[42] = static_allocator.allocateImpl());
        REQUIRE(data_42 == allocated_data[42].data());

        aergo::module::message::SharedDataBlob data_null;
        REQUIRE_NOTHROW(data_null = static_allocator.allocateImpl());
        REQUIRE(!data_null.valid());

        for (uint32_t i = 0; i < number_of_slots; ++i)
        {
            REQUIRE_NOTHROW(static_allocator.addOwnerImpl((aergo::module::ISharedData*)allocated_data[i].data()));
            REQUIRE_NOTHROW(static_allocator.addOwnerImpl((aergo::module::ISharedData*)allocated_data[i].data()));
        }

        REQUIRE_NOTHROW(data_null = static_allocator.allocateImpl());
        REQUIRE(!data_null.valid());
        
        REQUIRE(logger.logs().size() == 0);

        for (uint32_t i = 0; i < number_of_slots; ++i)
        {
            REQUIRE_NOTHROW(static_allocator.removeOwnerImpl((aergo::module::ISharedData*)allocated_data[i].data()));
        }

        REQUIRE_NOTHROW(data_null = static_allocator.allocateImpl());
        REQUIRE(!data_null.valid());

        for (uint32_t i = 0; i < 50; ++i)
        {
            REQUIRE_NOTHROW(static_allocator.removeOwnerImpl((aergo::module::ISharedData*)allocated_data[i].data()));
        }

        REQUIRE_NOTHROW(allocated_data[0] = static_allocator.allocateImpl());
        REQUIRE(allocated_data[0].valid());
        
        REQUIRE(logger.logs().size() == 0);

        for (uint32_t i = 0; i < allocated_data.size(); ++i)
        {
            REQUIRE_NOTHROW(static_allocator.removeOwnerImpl((aergo::module::ISharedData*)allocated_data[i].data()));
        }
        
        REQUIRE(logger.logs().size() == 49);

        for (uint32_t i = 0; i < number_of_slots; ++i)
        {
            REQUIRE_NOTHROW(allocated_data[i] = static_allocator.allocateImpl());
            REQUIRE(allocated_data[i].valid());
            REQUIRE(allocated_data[i].size() == slot_size_bytes);
            for (uint32_t j = 0; j < i; ++j)
            {
                REQUIRE(allocated_data[j].data() != allocated_data[i].data());
            }
        }

        REQUIRE(logger.logs().size() == 49);

        for (uint32_t i = 0; i < allocated_data.size(); ++i)
        {
            REQUIRE_NOTHROW(static_allocator.removeOwnerImpl((aergo::module::ISharedData*)allocated_data[i].data()));
        }
        
        REQUIRE(logger.logs().size() == 49);
    }
}