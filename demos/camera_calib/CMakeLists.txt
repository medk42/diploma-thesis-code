# Find OpenCV
find_package(OpenCV 4 REQUIRED world)
include_directories(${OpenCV_INCLUDE_DIRS})

# Add the executable
add_executable(camera_calib 
    src/main.cpp
    src/camera_calibration.cpp
)

target_include_directories(camera_calib PRIVATE include)

# Link against OpenCV (already found by the root CMakeLists.txt)
target_link_libraries(camera_calib PRIVATE opencv_world)

cmake_policy(SET CMP0177 NEW)
install(
    DIRECTORY ${CMAKE_BINARY_DIR}/demos/camera_calib/Debug/ 
    DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries/camera_calib
)