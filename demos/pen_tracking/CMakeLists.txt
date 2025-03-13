include_directories(include)
include_directories(../pen_calib/include)

# Add the executable
add_executable(pen_tracking 
    src/main.cpp
    ../pen_calib/src/pen_calibration_helper.cpp
)

# Link against OpenCV (already found by the root CMakeLists.txt)
target_link_libraries(pen_tracking ${OpenCV_LIBS})

install(TARGETS pen_tracking DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)