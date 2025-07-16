find_package(Catch2 3 CONFIG REQUIRED)

add_executable(memory_allocation_tests
    # memory_allocation/dynamic_allocator_test.cpp
    memory_allocation/shared_data_core_test.cpp
)
target_link_libraries(memory_allocation_tests PRIVATE Catch2::Catch2WithMain module_common)

include(CTest)
include(Catch)
catch_discover_tests(memory_allocation_tests)