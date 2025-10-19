set(LIB_NAME helpers___synchronous_request_helper)

add_library(${LIB_NAME} INTERFACE)
target_include_directories(${LIB_NAME} INTERFACE include)
target_link_libraries(${LIB_NAME} INTERFACE module_common)

# ELF: hide everything by default
set_target_properties(${LIB_NAME} PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN 1
)