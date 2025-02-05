cmake_minimum_required(VERSION 3.10)
project(my_project)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)

# Find OpenCV
find_package(OpenCV 4 REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Include directories
include_directories(include)

# Add subdirectories
# add_subdirectory(src)
# add_subdirectory(modules)
add_subdirectory(demos)
# add_subdirectory(tests)

# Link against OpenCV
# target_link_libraries(${PROJECT_NAME} ${OpenCV_LIBS})