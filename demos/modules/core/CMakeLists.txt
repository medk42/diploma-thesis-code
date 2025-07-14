include_directories(include)

# Add the executable
add_executable(demo_core 
    src/main.cpp
    src/module_loader.cpp
    src/logging/console_logger.cpp
    src/memory_allocation/dynamic_allocator.cpp
    src/memory_allocation/shared_data_core.cpp
)

target_link_libraries(demo_core PRIVATE module_common)

# MSVC: force dynamic CRT
if (MSVC)
    target_compile_options(demo_core PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()
# ELF: hide everything by default
set_target_properties(demo_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)

cmake_policy(SET CMP0177 NEW)
install(TARGETS demo_core DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../../binaries)