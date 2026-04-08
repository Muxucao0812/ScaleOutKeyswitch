# Cinnamon: A Framework for Scale-Out Encrypted AI

This repository contains the Cinnamon Acclerator Simulator based on the ASPLOS 2025 paper [Cinnamon: A Framework for scale out Encrytped AI](https://dl.acm.org/doi/10.1145/3669940.3707260).

Checkout out the other repositories in the Cinnamon Framework
- [Cinnamon Compiler]()
- [Cinnamon Emulator]()
- [Cinnamon Accelerator RTL]()


# Build
A sketch of the commands to build SST Core and SST Elements is given.
You may need addtional dependent packages installed.
Refer to the [official instructions](http://sst-simulator.org/SSTPages/SSTBuildAndInstall_11dot1dot0_SeriesDetailedBuildInstructions/) for details.

## Build SST Core, Elements and Custom-Elements
```bash
git submodule update --init --recursive
mkdir build
cd build
cmake -S .. -B .
cmake --build . --target sst-core 
cmake --build . --target sst-elements
```

## Build the Cinnamon element
```
cmake --build . --target install
```

# Documents
- [Official tutorials](http://sst-simulator.org/SSTPages/SSTTopDocTutorial/)
