#!/usr/bin/env python3
"""SW emulation test runner for tutorial_add, mult, rot."""
from __future__ import annotations

import math
import pathlib
import sys
import time
from typing import Any, Dict

import cinnamon_emulator
import cinnamon_fpga
from cinnamon.compiler import cinnamon_compile
from cinnamon.dsl import CiphertextInput, CinnamonProgram, Output

ROOT_DIR = pathlib.Path(__file__).resolve().parents[2]

PRIMES = [
    204865537, 205651969, 206307329, 207880193, 209059841, 210370561,
    211025921, 211812353, 214171649, 215482369, 215744513, 216137729,
    216924161, 217317377, 218628097, 219676673, 220594177, 221249537,
    222035969, 222167041, 222953473, 223215617, 224002049, 224133121,
    225574913, 228065281, 228458497, 228720641, 230424577, 230686721,
    230817793, 231473153, 232390657, 232652801, 234356737, 235798529,
    236584961, 236716033, 239337473, 239861761, 240648193, 241827841,
    244842497, 244973569, 245235713, 245760001, 246415361, 249561089,
    253100033, 253493249, 254279681, 256376833, 256770049, 257949697,
    258605057, 260571137, 260702209, 261488641, 261881857, 263323649,
    263454721, 264634369, 265420801, 268042241,
]

SW_EMU_XCLBIN = ROOT_DIR / "build" / "sw_emu" / "cinnamon_fpga.sw_emu.xclbin"


def to_real(value: Any) -> float:
    if isinstance(value, complex):
        return float(value.real)
    return float(value)


def to_imag(value: Any) -> float:
    if isinstance(value, complex):
        return float(value.imag)
    return 0.0


def compare_vectors(actual: Any, expected: Any, tolerance: float = 1e-3):
    max_error = 0.0
    bad = []
    for idx, (a, e) in enumerate(zip(actual, expected)):
        a_real = to_real(a)
        error = abs(a_real - e) if math.isfinite(a_real) else float("inf")
        max_error = max(max_error, error)
        if error > tolerance:
            bad.append((idx, a_real, float(e), error))
    return max_error, bad


def run_tutorial(name: str) -> bool:
    print(f"\n{'='*60}")
    print(f"  SW Test: tutorial_{name}")
    print(f"{'='*60}")

    slots = 64
    scale = 1 << 28
    register_file_size = 1024
    num_chips = 1

    out_dir = ROOT_DIR / "build" / f"sw_test_tutorial_{name}"
    out_dir.mkdir(parents=True, exist_ok=True)

    # Build program
    t0 = time.time()
    if name == "add":
        program = CinnamonProgram("Add", rns_bit_size=28, num_chips=num_chips)
        with program:
            x = CiphertextInput("x", 28, 51)
            y = CiphertextInput("y", 28, 51)
            z = x + y
            Output("z", z)
        raw_inputs: Dict[str, Any] = {
            "x": ([float(i) for i in range(slots)], scale),
            "y": ([float(2 * i) for i in range(slots)], scale),
        }
        expected = [float(3 * i) for i in range(slots)]
        output_scales = {"z": float(scale)}

    elif name == "mult":
        level = 22
        program = CinnamonProgram("Mul", rns_bit_size=28, num_chips=num_chips)
        with program:
            x = CiphertextInput("x", 28, level)
            y = CiphertextInput("y", 28, level)
            z = x * y
            Output("z", z)
        raw_inputs = {
            "x": ([float(i) for i in range(slots)], scale),
            "y": ([float(2 * i) for i in range(slots)], scale),
        }
        expected = [float(2 * i * i) for i in range(slots)]
        pr = PRIMES[min(level-1, len(PRIMES)-1)]
        pr2 = PRIMES[min(level-2, len(PRIMES)-1)]
        output_scales = {"z": float(scale * scale / (pr * pr2))}

    elif name == "rot":
        rotate_steps = 2
        program = CinnamonProgram("Rot", rns_bit_size=28, num_chips=num_chips)
        with program:
            x = CiphertextInput("x", 28, 51)
            z = x >> rotate_steps
            Output("z", z)
        raw_inputs = {
            "x": ([float(i) for i in range(slots)], scale),
        }
        x_vals = [float(i) for i in range(slots)]
        expected = x_vals[rotate_steps:] + x_vals[:rotate_steps]
        output_scales = {"z": float(scale)}
    else:
        print(f"Unknown tutorial: {name}")
        return False

    print(f"[LOG] Compiling {name} program...")
    cinnamon_compile(program, 51, num_chips, register_file_size, str(out_dir) + "/")
    print(f"[LOG] Compile done in {time.time() - t0:.1f}s")

    # CKKS context
    context = cinnamon_emulator.Context(slots, PRIMES)
    secret_key = [0] * (2 * slots)
    for i in range(0, 2 * slots, 4):
        secret_key[i + 2] = 1
        secret_key[i + 3] = -1

    encryptor_params = [0, 1, 2, 3, 4, 5, 6, 7]
    encryptor = cinnamon_emulator.CKKSEncryptor(
        context, secret_key, encryptor_params,
    )

    print(f"[LOG] Creating sw_emu runtime...")
    runtime = cinnamon_fpga.Emulator(
        context,
        target="sw_emu",
        xclbin_path=SW_EMU_XCLBIN,
        board_indices=[0],
        require_kernel_execution=True,
        verify_kernel_results=True,
    )

    print(f"[LOG] Generating eval keys...")
    runtime.generate_and_serialize_evalkeys(
        str(out_dir / "evalkeys"),
        str(out_dir / "program_inputs"),
        encryptor,
    )
    print(f"[LOG] Generating inputs...")
    runtime.generate_inputs(
        str(out_dir / "program_inputs"),
        str(out_dir / "evalkeys"),
        raw_inputs,
        encryptor,
    )
    print(f"[LOG] Running program on sw_emu...")
    t1 = time.time()
    runtime.run_program(
        str(out_dir / "instructions"),
        num_chips,
        register_file_size,
    )
    kernel_time = time.time() - t1
    print(f"[LOG] Kernel execution done in {kernel_time:.1f}s")

    dispatch = cinnamon_fpga.describe_last_dispatch(runtime)
    print(f"[LOG] dispatch_count: {len(dispatch)}")
    print(f"[LOG] dispatch_preview: {dispatch[:6]}")
    timing = runtime.last_timing_summary.get("global", {})
    print(f"[LOG] timing_summary: {timing}")

    print(f"[LOG] Decrypting outputs...")
    decrypted = runtime.get_decrypted_outputs(encryptor, output_scales)
    z_values = list(decrypted.get("z", []))

    print(f"\n[LOG] decrypted z first 16: {z_values[:16]}")
    print(f"[LOG] expected first 16: {expected[:16]}")

    max_error, bad = compare_vectors(z_values[:slots], expected[:slots], tolerance=1e-2)
    print(f"\n[LOG] max_error = {max_error:.6e}")
    print(f"[LOG] mismatch_count = {len(bad)}")

    if bad:
        print("\n[LOG] First 10 mismatches:")
        for idx, actual, exp, err in bad[:10]:
            print(f"  idx={idx:2d}  actual={actual:.9f}  expected={exp:.1f}  err={err:.3e}")
        print(f"\n[RESULT] tutorial_{name}: FAIL")
        return False
    else:
        print(f"\n[RESULT] tutorial_{name}: PASS (max_error={max_error:.6e})")
        return True


def main():
    tests = ["add", "mult", "rot"]
    results = {}
    for name in tests:
        try:
            results[name] = run_tutorial(name)
        except Exception as e:
            print(f"\n[RESULT] tutorial_{name}: ERROR - {e}")
            import traceback
            traceback.print_exc()
            results[name] = False

    print(f"\n{'='*60}")
    print(f"  SW Test Summary")
    print(f"{'='*60}")
    for name, ok in results.items():
        status = "PASS" if ok else "FAIL"
        print(f"  tutorial_{name}: {status}")

    all_pass = all(results.values())
    print(f"\n  OVERALL: {'PASS' if all_pass else 'FAIL'}")
    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
