cmake_minimum_required(VERSION 3.10)
project(aergo_diploma_thesis LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable testing
enable_testing()

# Add subdirectories
add_subdirectory(backend)
add_subdirectory(default_modules)
add_subdirectory(demos)