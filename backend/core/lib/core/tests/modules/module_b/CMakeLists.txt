include_directories(
    include
)

set(MODULE_NAME core_test_1_module_b)

# Add the executable
add_library("${MODULE_NAME}" SHARED
    src/module_b_contract.cpp
)

target_link_libraries("${MODULE_NAME}" PRIVATE module_common)

# MSVC: force dynamic CRT
if (MSVC)
    target_compile_options("${MODULE_NAME}" PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()
# ELF: hide everything by default
set_target_properties("${MODULE_NAME}" PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)
target_link_options("${MODULE_NAME}" PRIVATE /NOIMPLIB)


cmake_policy(SET CMP0177 NEW)
install(TARGETS "${MODULE_NAME}" DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../binaries/tests/test_core_1)