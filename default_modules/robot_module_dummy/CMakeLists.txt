set(MODULE_NAME robot_module_dummy)

# Haru/OpenGL/GLEW are required by the installed Wt package in this workspace.
find_package(unofficial-libharu CONFIG REQUIRED)
if (TARGET unofficial::libharu::hpdf AND NOT TARGET HPDF::HPDF)
  add_library(HPDF::HPDF ALIAS unofficial::libharu::hpdf)
endif()

find_package(OpenGL)
if (NOT TARGET OpenGL::GL)
  if (TARGET OpenGL::OpenGL)
    add_library(OpenGL::GL ALIAS OpenGL::OpenGL)
  elseif (WIN32)
    add_library(OpenGL::GL INTERFACE IMPORTED)
    target_link_libraries(OpenGL::GL INTERFACE opengl32)
  else()
    message(FATAL_ERROR "OpenGL::GL not found.")
  endif()
endif()

find_package(GLEW CONFIG QUIET)
if (NOT TARGET GLEW::GLEW)
  find_package(unofficial-glew CONFIG QUIET)
  if (TARGET unofficial::glew::glew)
    add_library(GLEW::GLEW ALIAS unofficial::glew::glew)
  else()
    message(FATAL_ERROR "GLEW not found. Install vcpkg port 'glew'.")
  endif()
endif()

find_package(Wt 4 REQUIRED COMPONENTS Wt HTTP)

file(GLOB_RECURSE PROJECT_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(${MODULE_NAME} MODULE ${PROJECT_SOURCES})

target_link_libraries(${MODULE_NAME} PRIVATE
    module_common

    helpers___robot_interface
    helpers___serialization_helper
    helpers___visualization_3d_interface
    Wt::Wt
    Wt::HTTP
)

target_include_directories(${MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

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
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/module_data/ DESTINATION ${CMAKE_SOURCE_DIR}/binaries/data/${MODULE_NAME})
