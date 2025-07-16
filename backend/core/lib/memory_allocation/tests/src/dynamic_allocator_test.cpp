#include <catch2/catch_test_macros.hpp>

#include "test_memory_allocator.h"
#include "utils/memory_allocation/dynamic_allocator.h"
#include "utils/logging/console_logger.h"

using namespace aergo::core::memory_allocation;
using namespace aergo::core::logging;



class TestLogger : public ILogger
{
public:
    void log(SourceType source_type, const char* source_name, uint64_t source_module_id, aergo::module::logging::LogType log_type, const char* message) override
    {
        console_logger_.log(source_type, source_name, source_module_id, log_type, message);
        logs_types_.push_back(log_type);
    }

    std::vector<aergo::module::logging::LogType>& logs()
    {
        return logs_types_;
    }

private:
    ConsoleLogger console_logger_;
    std::vector<aergo::module::logging::LogType> logs_types_;
};



TEST_CASE( "DynamicAllocator, broken allocator", "[dynamic_allocator]" )
{
    TestMemoryAllocator memory_allocator(false);

    TestLogger logger;
    DynamicAllocator dynamic_allocator(logger, &memory_allocator);
    
    REQUIRE(memory_allocator.operations().size() == 0);
    REQUIRE(logger.logs().size() == 0);

    REQUIRE_NOTHROW(dynamic_allocator.addOwner((aergo::module::ISharedData*) 0));

    REQUIRE(memory_allocator.operations().size() == 0);
    REQUIRE(logger.logs().size() == 1);
    REQUIRE(logger.logs()[0] == aergo::module::logging::LogType::ERROR);

    REQUIRE_NOTHROW(dynamic_allocator.removeOwner((aergo::module::ISharedData*) 0));

    REQUIRE(memory_allocator.operations().size() == 0);
    REQUIRE(logger.logs().size() == 2);
    REQUIRE(logger.logs()[1] == aergo::module::logging::LogType::ERROR);

    REQUIRE_NOTHROW(dynamic_allocator.removeOwner((aergo::module::ISharedData*) 1));
    REQUIRE_NOTHROW(dynamic_allocator.removeOwner((aergo::module::ISharedData*) 2));
    REQUIRE_NOTHROW(dynamic_allocator.addOwner((aergo::module::ISharedData*) 3));

    REQUIRE(memory_allocator.operations().size() == 0);
    REQUIRE(logger.logs().size() == 5);
    REQUIRE(logger.logs()[4] == aergo::module::logging::LogType::ERROR);
    
    uint64_t alloc_size = 1000;
    aergo::module::ISharedData* data;
    REQUIRE_NOTHROW(data = dynamic_allocator.allocate(alloc_size));
    REQUIRE(!data->valid());
    REQUIRE(data->data() == nullptr);
    REQUIRE(data->size() == 0);
    REQUIRE(memory_allocator.operations().size() == 1);
    REQUIRE(memory_allocator.operations()[0].type_ == TestMemoryAllocator::Op::Type::MALLOC);
    REQUIRE(memory_allocator.operations()[0].size_ == alloc_size);
    REQUIRE(memory_allocator.operations()[0].address_ == 0);
    REQUIRE(logger.logs().size() == 6);
    REQUIRE(logger.logs()[5] == aergo::module::logging::LogType::ERROR);

    SharedDataCore* data_core = dynamic_cast<SharedDataCore*>(data);

    REQUIRE(data_core->counter() == 0);
    REQUIRE_NOTHROW(dynamic_allocator.addOwner(data));
    REQUIRE(data_core->counter() == 1);
    REQUIRE_NOTHROW(dynamic_allocator.addOwner(data));
    REQUIRE(data_core->counter() == 2);
    REQUIRE_NOTHROW(dynamic_allocator.removeOwner(data));
    REQUIRE(data_core->counter() == 1);
    REQUIRE_NOTHROW(dynamic_allocator.addOwner(data));
    REQUIRE(data_core->counter() == 2);
    REQUIRE_NOTHROW(dynamic_allocator.addOwner(data));
    REQUIRE(data_core->counter() == 3);
    REQUIRE_NOTHROW(dynamic_allocator.removeOwner(data));
    REQUIRE_NOTHROW(dynamic_allocator.removeOwner(data));
    REQUIRE(data_core->counter() == 1);
    REQUIRE_NOTHROW(dynamic_allocator.removeOwner(data));
    REQUIRE(logger.logs().size() == 6);
    REQUIRE(memory_allocator.operations().size() == 1);

    aergo::module::ISharedData *data1, *data2, *data3;
    
    REQUIRE_NOTHROW(data1 = dynamic_allocator.allocate(alloc_size));
    REQUIRE(!data1->valid());
    REQUIRE(data1->data() == nullptr);
    REQUIRE(data1->size() == 0);
    REQUIRE(memory_allocator.operations().size() == 2);
    REQUIRE(memory_allocator.operations()[1].type_ == TestMemoryAllocator::Op::Type::MALLOC);
    REQUIRE(memory_allocator.operations()[1].size_ == alloc_size);
    REQUIRE(memory_allocator.operations()[1].address_ == 0);
    REQUIRE(logger.logs().size() == 7);

    REQUIRE_NOTHROW(data2 = dynamic_allocator.allocate(alloc_size));
    REQUIRE(!data2->valid());
    REQUIRE(data2->data() == nullptr);
    REQUIRE(data2->size() == 0);
    REQUIRE(memory_allocator.operations().size() == 3);
    REQUIRE(memory_allocator.operations()[2].type_ == TestMemoryAllocator::Op::Type::MALLOC);
    REQUIRE(memory_allocator.operations()[2].size_ == alloc_size);
    REQUIRE(memory_allocator.operations()[2].address_ == 0);
    REQUIRE(logger.logs().size() == 8);

    REQUIRE_NOTHROW(data3 = dynamic_allocator.allocate(alloc_size));
    REQUIRE(!data3->valid());
    REQUIRE(data3->data() == nullptr);
    REQUIRE(data3->size() == 0);
    REQUIRE(memory_allocator.operations().size() == 4);
    REQUIRE(memory_allocator.operations()[3].type_ == TestMemoryAllocator::Op::Type::MALLOC);
    REQUIRE(memory_allocator.operations()[3].size_ == alloc_size);
    REQUIRE(memory_allocator.operations()[3].address_ == 0);
    REQUIRE(logger.logs().size() == 9);

    SharedDataCore* data1_core = dynamic_cast<SharedDataCore*>(data1);
    SharedDataCore* data2_core = dynamic_cast<SharedDataCore*>(data2);
    SharedDataCore* data3_core = dynamic_cast<SharedDataCore*>(data3);
    REQUIRE(data1_core->counter() == 0);
    REQUIRE(data2_core->counter() == 0);
    REQUIRE(data3_core->counter() == 0);

    REQUIRE_NOTHROW(dynamic_allocator.removeOwner(data2));
    REQUIRE(memory_allocator.operations().size() == 4);
    REQUIRE(logger.logs().size() == 9);


    REQUIRE_NOTHROW(dynamic_allocator.addOwner(data1));
    REQUIRE_NOTHROW(dynamic_allocator.addOwner(data3));
    REQUIRE_NOTHROW(dynamic_allocator.addOwner(data1));
    REQUIRE_NOTHROW(dynamic_allocator.addOwner(data1));
    REQUIRE_NOTHROW(dynamic_allocator.addOwner(data3));
    REQUIRE(logger.logs().size() == 9);
    REQUIRE(data1_core->counter() == 3);
    REQUIRE(data3_core->counter() == 2);

    
    REQUIRE_NOTHROW(dynamic_allocator.removeOwner(data3));
    REQUIRE_NOTHROW(dynamic_allocator.removeOwner(data1));
    REQUIRE(logger.logs().size() == 9);
    REQUIRE(data1_core->counter() == 2);
    REQUIRE(data3_core->counter() == 1);

    
    REQUIRE_NOTHROW(dynamic_allocator.removeOwner(data3));
    REQUIRE_NOTHROW(dynamic_allocator.removeOwner(data3));
    REQUIRE(logger.logs().size() == 9);
    REQUIRE(data1_core->counter() == 2);
    
    REQUIRE_NOTHROW(dynamic_allocator.removeOwner(data1));
    REQUIRE(data1_core->counter() == 1);
    REQUIRE(logger.logs().size() == 9);

    REQUIRE_NOTHROW(dynamic_allocator.removeOwner(data1));
    REQUIRE(logger.logs().size() == 9);
}
