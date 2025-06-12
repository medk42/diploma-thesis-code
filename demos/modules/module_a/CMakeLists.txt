include_directories(
    include
    ../modules_include
)

# Add the executable
add_library(demo_module_a SHARED
    src/main.cpp
)

# MSVC: force dynamic CRT
if (MSVC)
    target_compile_options(demo_module_a PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()
# ELF: hide everything by default
set_target_properties(demo_module_a PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)


cmake_policy(SET CMP0177 NEW)
install(TARGETS demo_module_a DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../../binaries/demo_modules)