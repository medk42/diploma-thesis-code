cmake_minimum_required(VERSION 3.23)
project(aergo_diploma_thesis LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CMake policies
if(POLICY CMP0177)
  cmake_policy(SET CMP0177 NEW)
endif()

# Position independent code for all targets
if (UNIX AND NOT APPLE)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# What to build (controlled by presets)
option(AERGO_BUILD_BACKEND "Build backend" ON)
option(AERGO_BUILD_DEFAULT_MODULES "Build default modules" ON)
option(AERGO_BUILD_DEMOS "Build demos" OFF)

# Enable testing
enable_testing()

# Conditionally add subdirectories
if (AERGO_BUILD_BACKEND)
  add_subdirectory(backend)
endif()

if (AERGO_BUILD_DEFAULT_MODULES)
  add_subdirectory(default_modules)
endif()

if (AERGO_BUILD_DEMOS)
  add_subdirectory(demos)
endif()