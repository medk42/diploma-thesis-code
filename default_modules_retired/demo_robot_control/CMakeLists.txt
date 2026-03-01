set(MODULE_NAME demo_robot_control)
set(REQUIRED_HELPER_LIBS
    helpers___usecase_wrapper
    helpers___base_usecase
    helpers___robot_interface
    helpers___mixed_buffer_allocator
    helpers___synchronous_request_helper
    helpers___robot_wrapper
)

# gather sources (updates when files change)
file(GLOB_RECURSE PROJECT_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# Add the module
add_library(${MODULE_NAME} MODULE ${PROJECT_SOURCES})

# Link with libraries needed by this module
target_link_libraries(${MODULE_NAME} PRIVATE ${REQUIRED_HELPER_LIBS})

# Add include directories
target_include_directories(${MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# MSVC: force dynamic CRT
if (MSVC)
    target_compile_options("${MODULE_NAME}" PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()

# ELF: hide everything by default
set_target_properties("${MODULE_NAME}" PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
)


install(TARGETS "${MODULE_NAME}" DESTINATION ${CMAKE_SOURCE_DIR}/binaries/modules)