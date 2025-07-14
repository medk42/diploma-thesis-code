include_directories(include)
include_directories(../pen_calib/include)

# Find SimpleBLE
find_package(simpleble REQUIRED CONFIG)

# Find Pangolin
find_package(Pangolin CONFIG REQUIRED)

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

add_executable(pen_tracking_accuracy_test
    src/accuracy_test.cpp
    src/marker_tracker.cpp
    ../pen_calib/src/pen_calibration_helper.cpp
)

add_executable(ble_test
    src/main_ble_test.cpp
    src/ble_reader.cpp
)

add_executable(pen_visualization_test
    src/pen_visualization_test.cpp
    src/ble_reader.cpp
)

add_executable(pen_drawing_test
    src/pen_drawing_test.cpp
    src/ble_reader.cpp
    src/marker_tracker.cpp
    ../pen_calib/src/pen_calibration_helper.cpp
)

# Link against OpenCV (already found by the root CMakeLists.txt)
target_link_libraries(pen_tracking ${OpenCV_LIBS})
target_link_libraries(pen_tracking_performance_test ${OpenCV_LIBS})
target_link_libraries(pen_tracking_accuracy_test ${OpenCV_LIBS})
target_link_libraries(ble_test simpleble::simpleble ${OpenCV_LIBS})
target_link_libraries(pen_visualization_test simpleble::simpleble ${OpenCV_LIBS})
target_link_libraries(pen_drawing_test simpleble::simpleble ${OpenCV_LIBS} ${Pangolin_LIBRARIES})

cmake_policy(SET CMP0177 NEW)
install(TARGETS pen_tracking DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)
install(TARGETS pen_tracking_performance_test DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)
install(TARGETS pen_tracking_accuracy_test DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)
install(TARGETS ble_test DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)
install(TARGETS pen_visualization_test DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)
install(TARGETS pen_drawing_test DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)