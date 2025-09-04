# Find OpenCV
find_package(OpenCV 4 REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Find Ceres Solver
find_package(Ceres CONFIG REQUIRED)

# Add the executable
add_executable(pen_calib 
    src/main.cpp
    src/pen_calibration.cpp
    src/pen_calibration_helper.cpp
)

target_include_directories(pen_calib PRIVATE include)

# Link against OpenCV (already found by the root CMakeLists.txt)
target_link_libraries(pen_calib ${OpenCV_LIBS} Ceres::ceres)

cmake_policy(SET CMP0177 NEW)
install(
    DIRECTORY ${CMAKE_BINARY_DIR}/demos/pen_calib/Debug/ 
    DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries/pen_calib
)