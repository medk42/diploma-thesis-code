include_directories(include)

# Add the executable
add_executable(camera_calib 
    src/main.cpp
    src/camera_calibration.cpp
)

# Link against OpenCV (already found by the root CMakeLists.txt)
target_link_libraries(camera_calib ${OpenCV_LIBS})

cmake_policy(SET CMP0177 NEW)
install(TARGETS camera_calib DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)