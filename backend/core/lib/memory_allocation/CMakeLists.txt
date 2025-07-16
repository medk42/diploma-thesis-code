add_subdirectory(tests)

add_library(utils___memory_allocation
    src/dynamic_allocator.cpp
    src/memory_allocator.cpp
    src/shared_data_core.cpp
)

target_include_directories(utils___memory_allocation PUBLIC
    include
)

target_link_libraries(utils___memory_allocation PRIVATE module_common utils___logging)

# MSVC: force dynamic CRT
if (MSVC)
    target_compile_options(utils___memory_allocation PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()
# ELF: hide everything by default
set_target_properties(utils___memory_allocation PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)