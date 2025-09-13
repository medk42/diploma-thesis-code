# Find OpenCV
find_package(OpenCV 4 REQUIRED world)
include_directories(${OpenCV_INCLUDE_DIRS})

# Find SimpleBLE
find_package(simpleble REQUIRED CONFIG)

# Find Pangolin
find_package(Pangolin CONFIG REQUIRED)



# Add the executables
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

set(OPENCV_NEEDED_LIBS 
    opencv_world
)

# Link against OpenCV (already found by the root CMakeLists.txt)
target_link_libraries(pen_tracking PRIVATE ${OPENCV_NEEDED_LIBS})
target_link_libraries(pen_tracking_performance_test PRIVATE ${OPENCV_NEEDED_LIBS})
target_link_libraries(pen_tracking_accuracy_test PRIVATE ${OPENCV_NEEDED_LIBS})
target_link_libraries(ble_test PRIVATE simpleble::simpleble ${OPENCV_NEEDED_LIBS})
target_link_libraries(pen_visualization_test PRIVATE simpleble::simpleble ${OPENCV_NEEDED_LIBS})
target_link_libraries(pen_drawing_test PRIVATE simpleble::simpleble ${OPENCV_NEEDED_LIBS} ${Pangolin_LIBRARIES})

target_include_directories(pen_tracking PRIVATE include ../pen_calib/include)
target_include_directories(pen_tracking_performance_test PRIVATE include ../pen_calib/include)
target_include_directories(pen_tracking_accuracy_test PRIVATE include ../pen_calib/include)
target_include_directories(ble_test PRIVATE include ../pen_calib/include)
target_include_directories(pen_visualization_test PRIVATE include ../pen_calib/include)
target_include_directories(pen_drawing_test PRIVATE include ../pen_calib/include)

cmake_policy(SET CMP0177 NEW)
install(
    DIRECTORY ${CMAKE_BINARY_DIR}/demos/pen_tracking/Debug/ 
    DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries/pen_tracking
)