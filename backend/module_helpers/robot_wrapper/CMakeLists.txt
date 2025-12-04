set(LIB_NAME helpers___robot_wrapper)
set(REQUIRED_HELPER_LIBS
    helpers___robot_interface
    helpers___mixed_buffer_allocator
    helpers___synchronous_request_helper
)

# find all sources
file(GLOB_RECURSE PROJECT_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*
)

add_library(${LIB_NAME} STATIC ${PROJECT_SOURCES})

target_include_directories(${LIB_NAME} PUBLIC include)

target_link_libraries(${LIB_NAME} PUBLIC ${REQUIRED_HELPER_LIBS})


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