include_directories(include)

# Add the executable
add_executable(pen_calib 
    src/main.cpp
)

# Link against OpenCV (already found by the root CMakeLists.txt)
target_link_libraries(pen_calib ${OpenCV_LIBS})

install(TARGETS pen_calib DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)