add_library(utils___core_module_interface
    src/module_loader.cpp
)

target_include_directories(utils___core_module_interface PUBLIC
    include
)

target_link_libraries(utils___core_module_interface PRIVATE module_common)

# MSVC: force dynamic CRT
if (MSVC)
    target_compile_options(utils___core_module_interface PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()
# ELF: hide everything by default
set_target_properties(utils___core_module_interface PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)