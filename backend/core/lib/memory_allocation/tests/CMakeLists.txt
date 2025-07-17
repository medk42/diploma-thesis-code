find_package(Catch2 3 CONFIG REQUIRED)

add_executable(memory_allocation_tests
    src/dynamic_allocator_test.cpp
    src/static_allocator_test.cpp
    src/shared_data_core_test.cpp
)

target_include_directories(memory_allocation_tests PRIVATE include)

target_link_libraries(memory_allocation_tests PRIVATE Catch2::Catch2WithMain module_common utils___memory_allocation utils___logging)

include(CTest)
include(Catch)
catch_discover_tests(memory_allocation_tests)