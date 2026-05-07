cmake_minimum_required(VERSION 3.16)
project(CinnamonHLS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cinnamon_hls INTERFACE)
target_include_directories(cinnamon_hls INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
