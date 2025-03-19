include_directories(include)

find_package(Ceres CONFIG REQUIRED)

# Add the executable
add_executable(pen_calib 
    src/main.cpp
    src/pen_calibration.cpp
    src/pen_calibration_helper.cpp
)

# Link against OpenCV (already found by the root CMakeLists.txt)
target_link_libraries(pen_calib ${OpenCV_LIBS} Ceres::ceres)

cmake_policy(SET CMP0177 NEW)
install(TARGETS pen_calib DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)