set(LIB_NAME helpers___parameter_description)

# find cpp-base64
find_path(CPP_BASE64_INCLUDE_DIRS "cpp-base64/base64.cpp")

# find all sources
file(GLOB_RECURSE PROJECT_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*
)

add_library(${LIB_NAME} STATIC ${PROJECT_SOURCES})

target_include_directories(${LIB_NAME} PUBLIC include)
target_include_directories(${LIB_NAME} PRIVATE ${CPP_BASE64_INCLUDE_DIRS})

target_link_libraries(${LIB_NAME} PRIVATE module_common)


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