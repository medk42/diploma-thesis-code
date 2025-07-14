find_package(Catch2 3 CONFIG REQUIRED)

add_executable(catch2_demo_tests
    src/factorial_test.cpp
    src/fibonacci_test.cpp
)
target_link_libraries(catch2_demo_tests PRIVATE Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(catch2_demo_tests)