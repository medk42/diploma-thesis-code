set(MODULE_NAME robot_stereo_camera_calibration_module)
set(REQUIRED_HELPER_LIBS
    module_common
    helpers___activation_wrapper
    helpers___camera_messages
    helpers___camera_pose_helper
    helpers___calibrated_camera_world_messages
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


set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)

if(IS_DIRECTORY ${TEST_DIR})
    add_executable(${MODULE_NAME}_tests
        ${PROJECT_SOURCES}
        ${TEST_DIR}/main.cpp
    )

    target_include_directories(${MODULE_NAME}_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(${MODULE_NAME}_tests PRIVATE
        ${REQUIRED_HELPER_LIBS_TESTS}
    )
else()
    message(STATUS "${MODULE_NAME}_tests compilation: \"${TEST_DIR}\" directory does not exist, skipping.")
endif()


install(TARGETS "${MODULE_NAME}" DESTINATION ${CMAKE_SOURCE_DIR}/binaries/modules)
