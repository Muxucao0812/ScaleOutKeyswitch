# Cinnamon sw_emu Tutorial3 Inference Benchmark

Date: 2026-04-09 23:40:24 

## Config

- target: `sw_emu`
- xclbin: `/home/CONNECT/xmeng027/work/ScaleOutKeyswitch/CinnamonHLS/build/sw_emu/cinnamon_fpga.sw_emu.xclbin`
- chips: `1`
- warmup: `0`
- measured runs: `1`
- compute time definition: `sum(stage max(run.wait over partitions))`
- schedule time definition: `run_program_total - compute_wait_total`

## Prediction vs Label / Root Cause

| chips | label | fpga pred | fpga source | fpga correct | cpu pred | cpu source | cpu correct | kernel golden | root cause |
|---|---:|---:|---|---:|---:|---|---:|---:|---|
| 1 | 3 | 8 | fpga_decrypt_strict | FAIL | None | skipped | FAIL | PASS | cpu_skipped_or_unavailable |

## Runtime Summary

| chips | avg total (s) | avg compute (s) | avg schedule (s) | avg compute ratio | avg schedule ratio |
|---|---:|---:|---:|---:|---:|
| 1 | 1.741660 | 0.052732 | 1.681593 | 3.04% | 96.96% |

## Compare Artifact

- chips=1: `/home/CONNECT/xmeng027/work/ScaleOutKeyswitch/CinnamonHLS/build/logs/tutorial3_benchmark/20260409_233257/sample_10_cpu_fpga_compare.json`

## Breakdown Plots

- plot generation skipped.

