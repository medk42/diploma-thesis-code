set(LIB_NAME helpers___base_usecase)

# find all sources
file(GLOB_RECURSE PROJECT_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*
)

add_library(${LIB_NAME} STATIC ${PROJECT_SOURCES})

target_include_directories(${LIB_NAME} PUBLIC include)

target_link_libraries(${LIB_NAME} PUBLIC module_common helpers___usecase_wrapper)

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