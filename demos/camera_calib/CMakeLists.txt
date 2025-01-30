# Add the executable
add_executable(camera_calib src/main.cpp)
install(TARGETS camera_calib DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/../binaries)

# Link against OpenCV (already found by the root CMakeLists.txt)
# target_link_libraries(camera_calib ${OpenCV_LIBS})