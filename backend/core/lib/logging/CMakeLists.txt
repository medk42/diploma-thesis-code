add_library(utils___logging
    src/console_logger.cpp
)

target_include_directories(utils___logging PUBLIC
    include
)

target_link_libraries(utils___logging PRIVATE module_common)

# MSVC: force dynamic CRT
if (MSVC)
    target_compile_options(utils___logging PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()
# ELF: hide everything by default
set_target_properties(utils___logging PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)