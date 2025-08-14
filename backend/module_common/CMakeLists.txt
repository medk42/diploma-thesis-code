# Add the library
add_library(module_common STATIC
    src/dll_module_wrapper.cpp
    src/periodic_thread.cpp
    src/module_interface.cpp
    src/base_module.cpp
)

target_include_directories(module_common PUBLIC include)

# MSVC: force dynamic CRT
if (MSVC)
    target_compile_options(module_common PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()
# ELF: hide everything by default
set_target_properties(module_common PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)