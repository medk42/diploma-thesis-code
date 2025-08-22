set(LIB_NAME helpers___activation_wrapper)

add_library(${LIB_NAME}
    src/activation_wrapper.cpp
    src/async_task.cpp
    src/parameter_description.cpp
)

target_include_directories(${LIB_NAME} PUBLIC
    include
)

target_link_libraries(${LIB_NAME} PRIVATE module_common helpers___helper_base)

# MSVC: force dynamic CRT
if (MSVC)
    target_compile_options(${LIB_NAME} PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()
# ELF: hide everything by default
set_target_properties(${LIB_NAME} PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)