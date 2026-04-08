# Cinnamon: A Framework for Scale-Out Encrypted AI

This repository contains the Cinnamon DSL and Compiler based on the ASPLOS 2025 paper [Cinnamon: A Framework for scale out Encrytped AI](https://dl.acm.org/doi/10.1145/3669940.3707260).

Checkout out the other repositories in the Cinnamon Framework
- [Cinnamon Emulator]()
- [Cinnamon Architectural Simulator]()
- [Cinnamon Accelerator RTL]()

The Cinnamon DSL and Compiler are forked from [Microsoft EVA](https:://github.com/microsoft/EVA).

## Getting Started

The Cinnamon Compiler is a native library written in C++17 with bindings for Python. 

### Building and Installing the Cinnamon Compiler

#### Building Cinnamon

Cinnamon builds with CMake version ≥ 3.15:
```
git submodule update --init
mkdir build
cd build
cmake -S .. -B .
cmake --build . --target all
```
The build process creates a `setup.py` file in `build/python`. To install the package for development with PIP:
```
python3 -m pip install -e build/python/
```
To create a Python Wheel package for distribution in `dist/`:
```
python3 build/python/setup.py bdist_wheel --dist-dir='.'
```

## Emulating Programs

For correctness testing, the compiled outputs of the Cinnamon Compiler can be emulated using the Cinnamon Emulator.

## Simulating Performance of Programs

The compiled outputs of the Cinnamon Compiler can be run on the Cinnamon Architectural Simulator.

## Publications

Siddharth Jayashankar, Edward Chen, Tom Tang, Wenting Zheng, and Dimitrios Skarlatos. 2025. *Cinnamon: A Framework for Scale-Out Encrypted AI*. ASPLOS 2025. [DOI](https://doi.org/10.1145/3669940.3707260)