from __future__ import annotations

import math
import pathlib
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


def build_program(num_chips: int = 1) -> CinnamonProgram:
    program = CinnamonProgram("Sub", rns_bit_size=28, num_chips=num_chips)
    with program:
        x = CiphertextInput("x", 28, 51)
        y = CiphertextInput("y", 28, 51)
        z = x - y
        Output("z", z)
    return program


def to_real(value: Any) -> float:
    if isinstance(value, complex):
        return float(value.real)
    return float(value)


def compare_vectors(actual: Any, expected: Any, tolerance: float = 1e-3) -> tuple[float, list[tuple[int, float, float, float]]]:
    max_error = 0.0
    bad: list[tuple[int, float, float, float]] = []
    for idx, (a, e) in enumerate(zip(actual, expected)):
        a_real = to_real(a)
        error = abs(a_real - e) if math.isfinite(a_real) else float("inf")
        if not math.isfinite(error):
            error = float("inf")
        max_error = max(max_error, error)
        if error > tolerance:
            bad.append((idx, a_real, float(e), error))
    return max_error, bad


def main() -> None:
    out_dir = ROOT_DIR / "build" / "cinnamon_tutorial_sub_fpga"
    out_dir.mkdir(parents=True, exist_ok=True)

    num_chips = 1
    register_file_size = 1024
    slots = 64
    scale = 1 << 28

    program = build_program(num_chips)
    cinnamon_compile(program, 51, num_chips, register_file_size, str(out_dir) + "/")

    context = cinnamon_emulator.Context(slots, PRIMES)
    secret_key = [0] * (2 * slots)
    for i in range(0, 2 * slots, 4):
        secret_key[i + 2] = 1
        secret_key[i + 3] = -1

    encryptor = cinnamon_emulator.CKKSEncryptor(
        context,
        secret_key,
        [0, 1, 2, 3, 4, 5, 6, 7],
    )

    raw_inputs: Dict[str, Any] = {
        "x": ([float(i) for i in range(slots)], scale),
        "y": ([float(2 * i) for i in range(slots)], scale),
    }
    expected = [float(-i) for i in range(slots)]
    output_scales = {"z": float(scale)}

    runtime = cinnamon_fpga.Emulator(
        context,
        target="sw_emu",
        xclbin_path=ROOT_DIR / "build" / "sw_emu" / "cinnamon_fpga.sw_emu.xclbin",
        board_indices=[0],
        require_kernel_execution=True,
        verify_kernel_results=True,
    )

    runtime.generate_and_serialize_evalkeys(
        str(out_dir / "evalkeys"),
        str(out_dir / "program_inputs"),
        encryptor,
    )
    runtime.generate_inputs(
        str(out_dir / "program_inputs"),
        str(out_dir / "evalkeys"),
        raw_inputs,
        encryptor,
    )
    runtime.run_program(
        str(out_dir / "instructions"),
        num_chips,
        register_file_size,
    )

    dispatch = cinnamon_fpga.describe_last_dispatch(runtime)
    print("dispatch_count:", len(dispatch))
    print("dispatch_preview:", dispatch[:6])
    print("timing_summary:", runtime.last_timing_summary.get("global", {}))
    print("output ciphertexts:", runtime.get_output_ciphertexts())

    decrypted = runtime.get_decrypted_outputs(encryptor, output_scales)
    z_values = list(decrypted.get("z", []))
    max_error, bad = compare_vectors(z_values[:slots], expected[:slots], tolerance=1e-3)

    print("\ndecrypted z first 16:")
    print(z_values[:16])
    print("\nexpected first 16:")
    print(expected[:16])
    print(f"\nmax_error = {max_error:.6e}")
    print(f"mismatch_count = {len(bad)}")
    if bad:
        print("\nFirst 10 mismatches:")
        for idx, actual, exp, err in bad[:10]:
            print(
                f"idx={idx:2d}  actual={actual:.9f}  expected={exp:.1f}  err={err:.3e}"
            )
        print("\nFAIL: subtraction result does not match expected values.")
    else:
        print("\nPASS: subtraction result matches expected values.")


if __name__ == "__main__":
    main()
