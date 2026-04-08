# Copyright (c) Siddharth Jayashankar. All rights reserved.
# Licensed under the MIT license.

pybind11_add_module(cinnamon_py
    wrapper.cpp
)

target_compile_features(cinnamon_py PUBLIC cxx_std_17)
target_link_libraries(cinnamon_py PRIVATE Cinnamon)
if (MSVC)
    target_link_libraries(cinnamon_py PUBLIC bcrypt)
endif()
set_target_properties(cinnamon_py PROPERTIES OUTPUT_NAME _cinnamon)
