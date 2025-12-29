set(MODULE_NAME pen_tracking_multicam_module)

# gather sources (updates when files change)
file(GLOB_RECURSE PROJECT_SOURCES
  CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# Find OpenCV
find_package(OpenCV 4 REQUIRED core calib3d aruco imgproc imgcodecs)
find_package(Ceres REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Find SimpleBLE
find_package(simpleble REQUIRED CONFIG)

add_library(${MODULE_NAME} MODULE ${PROJECT_SOURCES})

target_link_libraries(${MODULE_NAME} PRIVATE
    module_common
    helpers___calibrated_camera_world_messages
    helpers___visualization_3d_interface
    helpers___pen_messages
    helpers___pose_utils
    helpers___mixed_buffer_allocator
    

    opencv_core
    opencv_calib3d
    opencv_aruco
    opencv_imgproc
    opencv_imgcodecs
    Ceres::ceres
    simpleble::simpleble
)

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
