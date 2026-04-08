# Copyright (c) Siddharth Jayashankar. All rights reserved.
# Licensed under the MIT license.

add_library(Cinnamon STATIC
    cinnamon.cpp
    version.cpp
)

target_include_directories(Cinnamon
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(Cinnamon PUBLIC fmt::fmt)
target_compile_definitions(Cinnamon PRIVATE CINNAMON_VERSION_STR="${PROJECT_VERSION}")

add_subdirectory(util)
add_subdirectory(frontend)
add_subdirectory(backend)
