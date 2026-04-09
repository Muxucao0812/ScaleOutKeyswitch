# CinnamonHLS

This directory contains the RTL-to-HLS migration baseline and FPGA runtime for Cinnamon.

## What Is Implemented

- HLS-compatible C++ models for RTL modules:
  - `arithmetic_modular` (add/sub/mul/barrett)
  - `montgomery` (NTT-friendly reduction/multiplication)
  - `ntt` (CKKS negacyclic four-step path with pre/post twist and explicit transpose)
  - `base_conv` (`change_rns_base`, `rns_resolve`)
  - `automorphism`
  - `transpose`
  - `memory_models` (`RectMem`, `Regfile` behavior)
- Generated negacyclic NTT root tables:
  - generator: `CinnamonHLS/scripts/generate_ntt_tables.py`
  - generated assets:
    - `CinnamonHLS/kernels/generated_ntt_tables.hpp`
    - `CinnamonHLS/python/cinnamon_fpga/generated_ntt_tables.py`
- Unit tests that mirror existing RTL test vectors.
- Multi-kernel Vitis xclbin:
  - `cinnamon_memory`
  - `cinnamon_arithmetic`
  - `cinnamon_montgomery`
  - `cinnamon_ntt`
  - `cinnamon_base_conv`
  - `cinnamon_automorphism`
  - `cinnamon_transpose`
  - `cinnamon_dispatch` (legacy smoke/diagnostics)
- Kernel code layout:
  - shared helpers: `kernels/kernel_common.hpp`
  - per-kernel execution logic: `kernels/kernel_*_module.hpp`
  - top wrappers: `kernels/cinnamon_*.cpp`
- Python runtime package (`cinnamon_fpga`) with kernel-only runtime methods:
  - `generate_and_serialize_evalkeys(...)`
  - `generate_inputs(...)`
  - `run_program(...)`
  - `get_kernel_outputs()`
- `pyxrt` host path for `sw_emu`, `hw_emu`, and `hw` execution modes.

## Build And Test HLS Models

```bash
cmake -S CinnamonHLS -B CinnamonHLS/build/cpu
cmake --build CinnamonHLS/build/cpu -j
ctest --test-dir CinnamonHLS/build/cpu --output-on-failure
```

## Build Kernel XCLBIN

```bash
# sw_emu (single U55C board)
CinnamonHLS/scripts/build_xclbin.sh sw_emu

# hw_emu
CinnamonHLS/scripts/build_xclbin.sh hw_emu

# hw
CinnamonHLS/scripts/build_xclbin.sh hw
```

Platform default: `xilinx_u55c_gen3x16_xdma_3_202210_1`

Link step默认会加载 `CinnamonHLS/config/hbm_memory_connectivity.cfg`，将
`cinnamon_memory_1` 的 `instructions/inputs/outputs` 端口绑定到独立 HBM bank。
如需切换配置，可在构建前设置 `CONNECTIVITY_CFG=/path/to/your.cfg`。

可选构建控制（便于硬件拥塞调参）：

```bash
# 仅编译 xo（不执行 link）
BUILD_STAGE=compile COMPILE_FREQUENCY_MHZ=160 CinnamonHLS/scripts/build_xclbin.sh hw

# 仅执行 link（复用已编译 xo）
BUILD_STAGE=link LINK_FREQUENCY_MHZ=160 CinnamonHLS/scripts/build_xclbin.sh hw

# 追加额外 link 配置（例如 vivado 实现策略）
EXTRA_LINK_CFG=CinnamonHLS/config/hw_impl_high_effort.cfg CinnamonHLS/scripts/build_xclbin.sh hw
```

## Cleanup Generated Artifacts

```bash
# Preview deletions
CinnamonHLS/scripts/cleanup_generated_artifacts.sh --dry-run

# Apply cleanup
CinnamonHLS/scripts/cleanup_generated_artifacts.sh
```

This removes generated logs and temporary HLS/Vitis project products while preserving
`CinnamonHLS/build/reports/` and committed source assets.

## Run Python sw_emu Smoke

```bash
export PYTHONPATH=/opt/xilinx/xrt/python:CinnamonHLS/python:$PYTHONPATH
CinnamonHLS/scripts/run_sw_emu_smoke.sh
```

## Run Tutorial3 sw_emu Debug Baseline

固定调试基线（`chips=1, boards=0, warmup=0, runs=1, sample_id=1, strict golden`）：

```bash
# args: [xclbin] [sample_id] [mismatch_dump_dir]
CinnamonHLS/scripts/run_tutorial3_swemu_debug.sh \
  CinnamonHLS/build/sw_emu/cinnamon_fpga.sw_emu.xclbin \
  1 \
  CinnamonHLS/build/reports/mismatch_swemu_debug
```

失败时会自动落盘：

- 完整 mismatch 证据 JSON（含 first mismatch + expected/actual 全量 output words）
- automorphism 回放 fixture（可用于 `test_kernel_descriptor` 单模块复现）
- instructions/program_inputs/evalkeys/golden 的时间戳归档目录

## Run Full-Opcode Dispatch Check

This check uses a synthetic instruction stream containing all supported opcodes and verifies
that runtime dispatch reaches all compute kernels:

- `memory`
- `arithmetic`
- `montgomery`
- `ntt`
- `base_conv`
- `automorphism`
- `transpose`

```bash
CinnamonHLS/scripts/run_full_opcode_dispatch.sh sw_emu \
  CinnamonHLS/build/sw_emu/cinnamon_fpga.sw_emu.xclbin \
  0
```

## Run End-To-End Backend Validation (Tutorial DSL -> Compiler -> FPGA Kernel -> Output Check)

```bash
# sw_emu: 1/2/4 partitions mapped to boards 0/1/2/3
CinnamonHLS/scripts/run_fpga_backend_validation.sh sw_emu \
  CinnamonHLS/build/sw_emu/cinnamon_fpga.sw_emu.xclbin \
  1,2,4 \
  0,1,2,3 \
  CinnamonHLS/golden/tutorial_add_kernel_outputs.json

# hw: single board first
CinnamonHLS/scripts/run_fpga_backend_validation.sh hw \
  CinnamonHLS/build/hw/cinnamon_fpga.hw.xclbin \
  1 \
  0 \
  CinnamonHLS/golden/tutorial_add_kernel_outputs.json

# hw: then 2 boards, then 4 boards
CinnamonHLS/scripts/run_fpga_backend_validation.sh hw \
  CinnamonHLS/build/hw/cinnamon_fpga.hw.xclbin \
  2 \
  0,1 \
  CinnamonHLS/golden/tutorial_add_kernel_outputs.json

CinnamonHLS/scripts/run_fpga_backend_validation.sh hw \
  CinnamonHLS/build/hw/cinnamon_fpga.hw.xclbin \
  4 \
  0,1,2,3 \
  CinnamonHLS/golden/tutorial_add_kernel_outputs.json
```

Validation script path:

`CinnamonHLS/python/examples/validate_fpga_backend.py`

## Tutorial Usage (FPGA Backend)

```python
import cinnamon_emulator
import cinnamon_fpga
from pathlib import Path

context = cinnamon_emulator.Context(SLOTS, RNS_PRIMES)
encryptor = cinnamon_emulator.CKKSEncryptor(context, secret_key)

fpga = cinnamon_fpga.Emulator(
    context,
    target="sw_emu",
    xclbin_path=Path("CinnamonHLS/build/sw_emu/cinnamon_fpga.sw_emu.xclbin"),
    board_indices=[0],
    require_kernel_execution=True,
    verify_kernel_results=True,
)

fpga.generate_and_serialize_evalkeys("evalkeys", "program_inputs", encryptor)
fpga.generate_inputs("program_inputs", "evalkeys", raw_inputs, encryptor)
fpga.run_program("instructions", num_partitions=1, register_file_size=1024)
kernel_outputs = fpga.get_kernel_outputs()
```

Minimal end-to-end example script:

```bash
PYTHONPATH=<compiler_build>/python:<emulator_build>/python:CinnamonHLS/python:/opt/xilinx/xrt/python \
python3.10 CinnamonHLS/python/examples/tutorial_add_fpga.py
```

## Notes

- Compiler output format is unchanged (`instructions*`, `program_inputs`).
- `run_program` always dispatches partitions to FPGA kernels. Runtime CPU fallback is disabled.
- `verify_kernel_results=True` checks per-module raw output words (status/header + full state image) and fails fast on mismatch.
- `run_fpga_backend_validation.sh` compares runtime kernel outputs with fixed golden data in `CinnamonHLS/golden/`.
- Input preparation (`generate_*`) still uses emulator in this phase.
- Multi-board mode uses `1 partition = 1 board` via `board_indices`.
