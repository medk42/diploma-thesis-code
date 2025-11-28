set(MODULE_NAME robot_module_kassow)

add_subdirectory(rpc)

file(GLOB_RECURSE PROJECT_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(${MODULE_NAME} MODULE ${PROJECT_SOURCES})

find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(${MODULE_NAME} PRIVATE 
    module_common 
    kassow_rpc 
    
    helpers___robot_interface 
    helpers___activation_wrapper 
    helpers___serialization_helper
    helpers___visualization_3d_interface

    nlohmann_json::nlohmann_json 
)

target_include_directories(${MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/rpc/include
)

if (WIN32)
    target_link_libraries(${MODULE_NAME} PRIVATE ws2_32)
endif()

if (MSVC)
    target_compile_options("${MODULE_NAME}" PRIVATE /MD$<$<CONFIG:Debug>:d>)
endif()

set_target_properties("${MODULE_NAME}" PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
)

install(TARGETS "${MODULE_NAME}" DESTINATION ${CMAKE_SOURCE_DIR}/binaries/modules)
