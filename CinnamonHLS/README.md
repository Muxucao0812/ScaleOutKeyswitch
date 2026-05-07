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

## Build HLS C++ Interface

```bash
cmake -S CinnamonHLS -B CinnamonHLS/build/cpu
cmake --build CinnamonHLS/build/cpu -j
```

## Main Script Entry Points

```bash
# 1. HLS csim only
CinnamonHLS/scripts/run_csim.sh

# 2. sw_emu: build + smoke
CinnamonHLS/scripts/run_sw_emu.sh

# 3. hw 50MHz build
CinnamonHLS/scripts/run_hw_50mhz.sh

# 4. hw inference run (queue + long runtime guard)
CinnamonHLS/scripts/run_hw_inference.sh
```

Platform default: `xilinx_u55c_gen3x16_xdma_3_202210_1`

`run_csim.sh` 说明：

- 只跑 HLS `csim`
- 默认覆盖全部 kernel
- 日志放到 `CinnamonHLS/build/csim/`
- HLS 工程目录生成在 `CinnamonHLS/.csim_prj_*`
- 不再生成 `.kernel_validation_prj_*`
- `automorphism` 支持用 `CINNAMON_AUTOMORPHISM_REPLAY_FILE=<fixture>` 直接重放真实 `sw_emu` dispatch

```bash
# 默认全量
CinnamonHLS/scripts/run_csim.sh

# 只跑部分 kernel
CINNAMON_CSIM_KERNELS=memory,arithmetic,automorphism CinnamonHLS/scripts/run_csim.sh

# 用 sw_emu 导出的真实 automorphism fixture 做 replay
CINNAMON_AUTOMORPHISM_REPLAY_FILE=/tmp/automorphism_replay_fixture.txt \
CINNAMON_CSIM_KERNELS=automorphism \
CinnamonHLS/scripts/run_csim.sh
```

`run_sw_emu.sh` 说明：

- `MODE=full`：编译 `sw_emu` 并跑 smoke
- `MODE=build`：只编译
- `MODE=run`：只跑 smoke
- `RUN_TARGET=smoke|tutorial_rot`
- 当 `RUN_TARGET=tutorial_rot` 且设置 `CINNAMON_AUTOMORPHISM_REPLAY_FILE=<fixture>` 时，会在真实 `automorphism` dispatch 后写出 replay fixture

```bash
CinnamonHLS/scripts/run_sw_emu.sh
MODE=build CinnamonHLS/scripts/run_sw_emu.sh
MODE=run CinnamonHLS/scripts/run_sw_emu.sh

# 跑真实 rotate workload，并导出 automorphism replay fixture
CINNAMON_AUTOMORPHISM_REPLAY_FILE=/tmp/automorphism_replay_fixture.txt \
RUN_TARGET=tutorial_rot \
MODE=run \
CinnamonHLS/scripts/run_sw_emu.sh
```

`run_hw_50mhz.sh` 说明：

- 固定 `hw`
- 默认 compile/link 都是 `50 MHz`
- 输出目录默认是 `CinnamonHLS/build/hw_50mhz`

```bash
CinnamonHLS/scripts/run_hw_50mhz.sh
BUILD_STAGE=compile CinnamonHLS/scripts/run_hw_50mhz.sh
BUILD_STAGE=link CinnamonHLS/scripts/run_hw_50mhz.sh
```

`run_hw_inference.sh` 说明：

- 跑 `benchmark_tutorial3_inference.py` 的 `hw` 路径
- 自动复用 `scripts/queue_hw_run.sh`（设备 busy 自动重试）
- 默认单次运行超时保护 `RUNTIME_TIMEOUT_SEC=28800`（8 小时）
- 默认单个 kernel dispatch 超时保护 `DISPATCH_WAIT_TIMEOUT_MS=300000`（5 分钟）

```bash
CinnamonHLS/scripts/run_hw_inference.sh

# 自定义超时、sample范围、并强制重新编译指令/输入
RUNTIME_TIMEOUT_SEC=43200 DISPATCH_WAIT_TIMEOUT_MS=600000 SAMPLE_START=1 SAMPLE_END=5 FORCE_RECOMPILE=1 \
CinnamonHLS/scripts/run_hw_inference.sh
```

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
- Input preparation (`generate_*`) still uses emulator in this phase.
- Multi-board mode uses `1 partition = 1 board` via `board_indices`.
