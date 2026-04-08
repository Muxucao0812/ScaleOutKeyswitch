# Cinnamon: A Framework for Scale-Out Encrypted AI

This repository contains the Cinnamon Emulator based on the ASPLOS 2025 paper [Cinnamon: A Framework for scale out Encrytped AI](https://dl.acm.org/doi/10.1145/3669940.3707260).

Checkout out the other repositories in the Cinnamon Framework
- [Cinnamon Compiler]()
- [Cinnamon Architectural Simulator]()
- [Cinnamon Accelerator RTL]()

## Getting Started

The Cinnamon Emulator is a native library written in C++17 with bindings for Python. 

### Building and Installing the Cinnamon Emulator

### Installing Dependencies

To install dependencies on Ubuntu 22.04:

install Microsoft SEAL version 4.1.1:
```
git clone -b v4.1.1 https://github.com/microsoft/SEAL.git
cd SEAL
mkdir build
cd build
cmake -DSEAL_THROW_ON_TRANSPARENT_CIPHERTEXT=OFF -DCMAKE_BUILD_TYPE=Release -S .. -B .
make -j
sudo make install
```
*Note that SEAL has to be installed with transparent ciphertext checking turned off, as it is not possible in general to statically ensure a program will not produce a transparent ciphertext. This does not affect the security of ciphertexts encrypted with SEAL.*

### Building and Installing the Cinnamon Emulator

#### Building the Cinnamon Emulator

EVA builds with CMake version ≥ 3.15:
```
git submodule update --init
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --target all
```
The build process creates a `setup.py` file in `python/`. To install the package for development with PIP:
```
python3 -m pip install -e build/python/
```

## Publications

Siddharth Jayashankar, Edward Chen, Tom Tang, Wenting Zheng, and Dimitrios Skarlatos. 2025. *Cinnamon: A Framework for Scale-Out Encrypted AI*. ASPLOS 2025. [DOI](https://doi.org/10.1145/3669940.3707260)