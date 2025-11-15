# Find OpenCV
find_package(OpenCV 4 REQUIRED world)
include_directories(${OpenCV_INCLUDE_DIRS})

# Add the executable
add_executable(aruco_test
    src/aruco_test.cpp
)

target_include_directories(aruco_test PRIVATE include)

# Link against OpenCV (already found by the root CMakeLists.txt)
target_link_libraries(aruco_test PRIVATE opencv_world)

install(TARGETS aruco_test DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries/aruco_tests)