set(MODULE_NAME frontend_module)


# Haru (Wt is broken without these)
find_package(unofficial-libharu CONFIG REQUIRED)
if (TARGET unofficial::libharu::hpdf AND NOT TARGET HPDF::HPDF)
  add_library(HPDF::HPDF ALIAS unofficial::libharu::hpdf)
endif()

# OpenGL (Wt is broken without these)
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

# GLEW (Wt is broken without these)
find_package(GLEW CONFIG QUIET)
if (NOT TARGET GLEW::GLEW)
  # Some toolchains export an unofficial name; try that and alias
  find_package(unofficial-glew CONFIG QUIET)
  if (TARGET unofficial::glew::glew)
    add_library(GLEW::GLEW ALIAS unofficial::glew::glew)
  else()
    message(FATAL_ERROR "GLEW not found. Install vcpkg port 'glew' for x64-windows-static or rebuild Wt without OpenGL.")
  endif()
endif()

# Finally Wt
find_package(Wt 4 REQUIRED COMPONENTS Wt HTTP)

# Find libzippp library
find_package(libzippp CONFIG REQUIRED)

# Find OpenCV
find_package(OpenCV 4 REQUIRED core imgcodecs)


# gather sources (updates when files change)
file(GLOB_RECURSE PROJECT_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# Add the module
add_library(${MODULE_NAME} MODULE ${PROJECT_SOURCES})

# Link with libraries needed by this module
target_link_libraries(${MODULE_NAME} PRIVATE module_common helpers___activation_wrapper helpers___visualization_3d_interface helpers___usecase_tree)
target_link_libraries(${MODULE_NAME} PRIVATE Wt::Wt Wt::HTTP libzippp::libzippp opencv_core opencv_imgcodecs)

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
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/module_data/ DESTINATION ${CMAKE_SOURCE_DIR}/binaries/data/${MODULE_NAME})