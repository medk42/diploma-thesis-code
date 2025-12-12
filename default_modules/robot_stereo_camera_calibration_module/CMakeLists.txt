set(MODULE_NAME robot_stereo_camera_calibration_module)
set(REQUIRED_HELPER_LIBS
    module_common
    helpers___activation_wrapper
    helpers___camera_messages
    helpers___camera_pose_helper
    opencv_core
    opencv_calib3d
    opencv_aruco
    opencv_highgui
    Ceres::ceres
    nlohmann_json::nlohmann_json
)

set (REQUIRED_HELPER_LIBS_TESTS
    ${REQUIRED_HELPER_LIBS}
    opencv_imgproc
    opencv_imgcodecs
)

# Find OpenCV
find_package(OpenCV 4 REQUIRED core calib3d aruco imgproc imgcodecs highgui)
find_package(Ceres REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# add JSON library
find_package(nlohmann_json CONFIG REQUIRED)

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

add_executable(${MODULE_NAME}_tests
    ${PROJECT_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/main.cpp
)

target_include_directories(${MODULE_NAME}_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${MODULE_NAME}_tests PRIVATE ${REQUIRED_HELPER_LIBS_TESTS})


install(TARGETS "${MODULE_NAME}" DESTINATION ${CMAKE_SOURCE_DIR}/binaries/modules)
