# Copyright (c) Siddharth Jayashankar. All rights reserved.
# Licensed under the MIT license.
cmake_minimum_required(VERSION 3.13)
cmake_policy(SET CMP0079 NEW)
cmake_policy(SET CMP0076 NEW)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(cinnamon_emulator
    VERSION 1.0.1
    LANGUAGES CXX
)

find_package(SEAL 4.1 REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development)

if(NOT Python3_VERSION_MAJOR EQUAL 3)
    message(FATAL_ERROR "Cinnamon Emulator requires Python 3. Please ensure you have it
        installed in a location searched by CMake.")
endif()

add_subdirectory(third_party/pybind11)

add_subdirectory(cinnamon_emulator)
add_subdirectory(python)
