# Copyright (c) Siddharth Jayashankar. All rights reserved.
# Licensed under the MIT license.
add_library(cinnamon_emulator STATIC
   emulator.cpp 
)

add_library(cinnamon_emulator_Internal SHARED
   galois.cpp 
   context.cpp 
   evaluator.cpp 
   ckks-encoder.cpp 
   ckks-encryptor.cpp 
   ntt.cpp 
)
set_target_properties(cinnamon_emulator PROPERTIES PUBLIC_HEADER "context.h")
set_target_properties(cinnamon_emulator_Internal PROPERTIES PUBLIC_HEADER "context.h")


target_link_libraries(cinnamon_emulator_Internal PUBLIC SEAL::seal)

message(STATUS "CPU: Build Interface: $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>")

target_include_directories(cinnamon_emulator_Internal
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)
target_compile_definitions(cinnamon_emulator_Internal PRIVATE)

target_link_libraries(cinnamon_emulator PUBLIC cinnamon_emulator_Internal)