find_package(Catch2 3 CONFIG REQUIRED)

set(TEST_NAME "module_common_tests")

add_executable(${TEST_NAME}
    src/shared_data_blob_tests.cpp
)

target_include_directories("${TEST_NAME}" PRIVATE include)

target_link_libraries("${TEST_NAME}" PRIVATE Catch2::Catch2WithMain module_common)

include(CTest)
include(Catch)
catch_discover_tests("${TEST_NAME}")