add_subdirectory(tests)

add_library(core_project
    src/core.cpp
    src/core_structures.cpp
)

target_include_directories(core_project PUBLIC
    include
)

target_link_libraries(core_project PUBLIC module_common utils___logging utils___memory_allocation utils___core_module_interface)

# MSVC: force dynamic CRT
if (MSVC)
    target_compile_options(core_project PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()
# ELF: hide everything by default
set_target_properties(core_project PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)