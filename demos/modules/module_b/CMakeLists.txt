include_directories(
    include
)

# Add the executable
add_library(demo_module_b SHARED
    src/main.cpp
)

target_link_libraries(demo_module_b PRIVATE module_common)

# MSVC: force dynamic CRT
if (MSVC)
    target_compile_options(demo_module_b PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()
# ELF: hide everything by default
set_target_properties(demo_module_b PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)
target_link_options(demo_module_b PRIVATE /NOIMPLIB)


cmake_policy(SET CMP0177 NEW)
install(TARGETS demo_module_b DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../../binaries/demo_modules)