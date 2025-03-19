include_directories(include)
include_directories(../pen_calib/include)

# Add the executable
add_executable(pen_tracking 
    src/main.cpp
    src/marker_tracker.cpp
    ../pen_calib/src/pen_calibration_helper.cpp
)

add_executable(pen_tracking_performance_test 
    src/performance_test.cpp
    src/marker_tracker.cpp
    ../pen_calib/src/pen_calibration_helper.cpp
)

# Link against OpenCV (already found by the root CMakeLists.txt)
target_link_libraries(pen_tracking ${OpenCV_LIBS})
target_link_libraries(pen_tracking_performance_test ${OpenCV_LIBS})

cmake_policy(SET CMP0177 NEW)
install(TARGETS pen_tracking DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)
install(TARGETS pen_tracking_performance_test DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)