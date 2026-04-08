#!/bin/bash

# Copyright (c) Siddharth Jayashankar. All rights reserved.
# Licensed under the MIT license.

BASE_DIR=$(dirname "$0")
PROJECT_ROOT_DIR=$BASE_DIR/../
shopt -s globstar
clang-format -i $PROJECT_ROOT_DIR/cinnamon/**/*.h
clang-format -i $PROJECT_ROOT_DIR/cinnamon/**/*.cpp
