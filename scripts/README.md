# Cinnamon Setup Scripts

## No-sudo SEAL + Emulator

```bash
./scripts/setup_seal.sh
./scripts/build_emulator_local.sh
```

## Python 3.10 FPGA Environment

```bash
./scripts/setup_conda_fpga_env.sh cinnamon-fpga-py310
conda activate cinnamon-fpga-py310
export PYTHONPATH=/opt/xilinx/xrt/python:$PYTHONPATH
```

## Compiler Build (Python 3.10)

```bash
./scripts/build_compiler_local.sh
```

## Toolchain Check

```bash
./scripts/check_fpga_toolchain.sh
```

## Tutorial Backend Validation (Python API)

```bash
# sw_emu / hw_emu / hw
CinnamonHLS/scripts/run_fpga_backend_validation.sh sw_emu
```
