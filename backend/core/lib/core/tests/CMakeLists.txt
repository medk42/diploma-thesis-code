add_subdirectory(modules)

find_package(Catch2 3 CONFIG REQUIRED)

set(TEST_NAME "core_tests")

add_executable(core_tests
    src/core_test_1.cpp
)

target_include_directories("${TEST_NAME}" PRIVATE include modules/common_include)

target_link_libraries("${TEST_NAME}" PRIVATE Catch2::Catch2WithMain core_project)

include(CTest)
include(Catch)
catch_discover_tests("${TEST_NAME}")

install(TARGETS core_test_1_module_a DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/test_core_1")
install(TARGETS core_test_1_module_b DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/test_core_1")
install(TARGETS core_test_1_module_c DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/test_core_1")
install(TARGETS core_test_1_module_d DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/test_core_1")
install(TARGETS core_test_1_module_e DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/test_core_1")