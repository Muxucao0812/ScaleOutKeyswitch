cmake_minimum_required(VERSION 3.16)
project(CinnamonHLS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cinnamon_hls INTERFACE)
target_include_directories(cinnamon_hls INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

enable_testing()

add_executable(test_arithmetic tests/test_arithmetic.cpp)
target_link_libraries(test_arithmetic PRIVATE cinnamon_hls)
add_test(NAME cinnamon_hls_arithmetic COMMAND test_arithmetic)

add_executable(test_montgomery tests/test_montgomery.cpp)
target_link_libraries(test_montgomery PRIVATE cinnamon_hls)
add_test(NAME cinnamon_hls_montgomery COMMAND test_montgomery)

add_executable(
    test_montgomery_kernel
    tests/test_montgomery_kernel.cpp
    kernels/cinnamon_montgomery_core.cpp
)
target_link_libraries(test_montgomery_kernel PRIVATE cinnamon_hls)
add_test(NAME cinnamon_hls_montgomery_kernel COMMAND test_montgomery_kernel)

add_executable(test_memory tests/test_memory.cpp)
target_link_libraries(test_memory PRIVATE cinnamon_hls)
add_test(NAME cinnamon_hls_memory COMMAND test_memory)

add_executable(test_ntt tests/test_ntt.cpp)
target_link_libraries(test_ntt PRIVATE cinnamon_hls)
add_test(NAME cinnamon_hls_ntt COMMAND test_ntt)

add_executable(test_base_conv tests/test_base_conv.cpp)
target_link_libraries(test_base_conv PRIVATE cinnamon_hls)
add_test(NAME cinnamon_hls_base_conv COMMAND test_base_conv)

add_executable(
    test_base_conv_kernels
    tests/test_base_conv_kernels.cpp
    kernels/cinnamon_base_conv_core.cpp
)
target_link_libraries(test_base_conv_kernels PRIVATE cinnamon_hls)
add_test(NAME cinnamon_hls_base_conv_kernels COMMAND test_base_conv_kernels)

add_executable(test_automorphism tests/test_automorphism.cpp)
target_link_libraries(test_automorphism PRIVATE cinnamon_hls)
add_test(NAME cinnamon_hls_automorphism COMMAND test_automorphism)

add_executable(test_transpose tests/test_transpose.cpp)
target_link_libraries(test_transpose PRIVATE cinnamon_hls)
add_test(NAME cinnamon_hls_transpose COMMAND test_transpose)

add_executable(
    test_kernel_descriptor
    tests/test_kernel_descriptor.cpp
    kernels/cinnamon_memory.cpp
    kernels/cinnamon_arithmetic.cpp
    kernels/cinnamon_montgomery.cpp
    kernels/cinnamon_ntt.cpp
    kernels/cinnamon_base_conv.cpp
    kernels/cinnamon_automorphism.cpp
    kernels/cinnamon_transpose.cpp
)
target_include_directories(test_kernel_descriptor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/kernels)
target_link_libraries(test_kernel_descriptor PRIVATE cinnamon_hls)
add_test(NAME cinnamon_hls_kernel_descriptor COMMAND test_kernel_descriptor)
