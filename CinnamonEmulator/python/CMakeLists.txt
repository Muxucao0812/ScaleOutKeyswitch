# Copyright (c) Siddharth Jayashankar. All rights reserved.
# Licensed under the MIT license.
configure_file(setup.py.in setup.py.after_configure)
file(GENERATE OUTPUT setup.py INPUT ${CMAKE_CURRENT_BINARY_DIR}/setup.py.after_configure)

add_subdirectory(cinnamon_emulator)

add_custom_target(python ALL DEPENDS cinnamon_emulator_py)
add_custom_command(TARGET python
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/cinnamon_emulator
            ${CMAKE_CURRENT_BINARY_DIR}/cinnamon_emulator
)
