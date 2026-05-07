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

## Queue HW Run (wait + auto-retry on busy)

```bash
# wait for a known occupying PID, then run your HW test
./scripts/queue_hw_run.sh --wait-pid 2260205 --poll-seconds 30 -- \
  bash -lc 'PYTHONPATH=build/cinnamon_compiler_local/python:build/cinnamon_emulator_local/python:CinnamonHLS/python:/opt/xilinx/xrt/python \
  python3.10 CinnamonHLS/python/examples/benchmark_tutorial3_inference.py --target hw --xclbin CinnamonHLS/build/hw_50mhz/cinnamon_fpga.hw.xclbin --chips 1 --boards 0 --sample-start 1 --sample-end 1'

# or do not bind to a PID, only retry when runtime reports device busy
./scripts/queue_hw_run.sh --poll-seconds 30 -- \
  bash -lc 'PYTHONPATH=... python3.10 CinnamonHLS/python/examples/tutorial_add_fpga.py'
```

## HW Inference Runner (Long Runtime Default)

```bash
# default runtime timeout is 8h (RUNTIME_TIMEOUT_SEC=28800)
# default per-dispatch wait timeout is 5min (DISPATCH_WAIT_TIMEOUT_MS=300000)
CinnamonHLS/scripts/run_hw_inference.sh

# customize runtime timeout / sample range
RUNTIME_TIMEOUT_SEC=43200 DISPATCH_WAIT_TIMEOUT_MS=600000 SAMPLE_START=1 SAMPLE_END=5 \
CinnamonHLS/scripts/run_hw_inference.sh
```

## CinnamonHLS Entry Scripts

```bash
CinnamonHLS/scripts/run_csim.sh
CinnamonHLS/scripts/run_sw_emu.sh
CinnamonHLS/scripts/run_hw_50mhz.sh
```
