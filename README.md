# Cinnamon: A Framework for Scale-Out Encrypted AI

This repository contains links to all repositories of the Cinnamon Framework from the ASPLOS 2025 paper [Cinnamon: A Framework for scale out Encrytped AI](https://dl.acm.org/doi/10.1145/3669940.3707260).

Checkout out the other repositories in the Cinnamon Framework
- [Cinnamon Compiler]()
- [Cinnamon Emulator]()
- [Cinnamon Architectural Simulator]()
- [Cinnamon Accelerator RTL]()

## Getting Started

### Local No-sudo Setup

```bash
./scripts/setup_seal.sh
./scripts/build_emulator_local.sh
./scripts/setup_conda_fpga_env.sh cinnamon-fpga-py310
```

See [scripts/README.md](scripts/README.md) and [CinnamonHLS/README.md](CinnamonHLS/README.md) for FPGA runtime (`sw_emu`/`hw_emu`/`hw`) usage.

## Publications

Siddharth Jayashankar, Edward Chen, Tom Tang, Wenting Zheng, and Dimitrios Skarlatos. 2025. *Cinnamon: A Framework for Scale-Out Encrypted AI*. ASPLOS 2025. [DOI](https://doi.org/10.1145/3669940.3707260)
