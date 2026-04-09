from __future__ import annotations

import argparse
import json
import math
import pathlib
import time
from datetime import datetime
from typing import Any, Dict, List, Sequence

import numpy as np
import cinnamon_emulator
import cinnamon_fpga
from cinnamon.compiler import cinnamon_compile
from cinnamon.dsl import CiphertextInput, CinnamonProgram, Output, PlaintextInput

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

RNS_BIT_SIZE = 28
SCALE_BITS = 56
LEVEL = 51
DEFAULT_SLOTS = 1024


def parse_sizes(value: str) -> List[int]:
    sizes = [int(v.strip()) for v in value.split(",") if v.strip()]
    if not sizes:
        raise ValueError(f"invalid sizes: {value!r}")
    for n in sizes:
        if n <= 0:
            raise ValueError(f"size must be positive, got {n}")
    return sizes


def choose_bsgs(n: int) -> tuple[List[int], List[int]]:
    """
    Standard baby-step giant-step split for n cyclic diagonals.
    """
    baby = int(math.ceil(math.sqrt(n)))
    giant = int(math.ceil(n / baby))
    babysteps = list(range(baby))
    giantsteps = [g * baby for g in range(giant)]
    return babysteps, giantsteps


def get_bsgs_plaintexts(
    name_base: str,
    babysteps: Sequence[int],
    giantsteps: Sequence[int],
    scale_bits: int,
    level: int,
) -> List[PlaintextInput]:
    return [
        PlaintextInput(f"{name_base}_{bs}_{gs}", scale_bits, level)
        for gs in giantsteps
        for bs in babysteps
    ]


def bsgs(inp, plain_diags: Sequence[PlaintextInput], babysteps: Sequence[int], giantsteps: Sequence[int]):
    """
    Same structure as tutorial3:
      rotate_babysteps = [inp if bs == 0 else (inp >> bs)]
      each giant step aggregates baby terms, then shifts back by gs.
    """
    rotate_babysteps = [inp if bs == 0 else (inp >> bs) for bs in babysteps]
    prod = None
    for g, gs in enumerate(giantsteps):
        sum_bs = None
        for b, _ in enumerate(babysteps):
            idx = g * len(babysteps) + b
            term = plain_diags[idx] * rotate_babysteps[b]
            sum_bs = term if sum_bs is None else (sum_bs + term)
        gterm = sum_bs if gs == 0 else (sum_bs << gs)
        prod = gterm if prod is None else (prod + gterm)
    return prod


def build_program(n: int, num_chips: int = 1) -> CinnamonProgram:
    """
    Dense square matrix-vector multiply:
      y = A @ x
    under cyclic diagonal packing.
    """
    babysteps, giantsteps = choose_bsgs(n)

    program = CinnamonProgram(
        f"BSGS_MatVec_{n}x{n}",
        rns_bit_size=RNS_BIT_SIZE,
        num_chips=num_chips,
    )
    with program:
        x = CiphertextInput("x", SCALE_BITS, LEVEL)
        diags = get_bsgs_plaintexts("A", babysteps, giantsteps, SCALE_BITS, x.level())
        y = bsgs(x, diags, babysteps, giantsteps)

        # Plaintext * ciphertext accumulation, same style as tutorial3
        y = y.rescale()

        Output("y", y)
    return program


def build_encryptor(context: cinnamon_emulator.Context, slots: int) -> cinnamon_emulator.CKKSEncryptor:
    secret_key = [0] * (2 * slots)
    for i in range(0, 2 * slots, 4):
        secret_key[i + 2] = 1
        secret_key[i + 3] = -1
    return cinnamon_emulator.CKKSEncryptor(
        context,
        secret_key,
        [0, 1, 2, 3, 4, 5, 6, 7],
    )


def to_real(value: Any) -> float:
    if isinstance(value, complex):
        return float(value.real)
    return float(value)


def to_imag(value: Any) -> float:
    if isinstance(value, complex):
        return float(value.imag)
    return 0.0


def compare_vectors(actual: Sequence[Any], expected: Sequence[float], tolerance: float = 1e-3):
    max_error = 0.0
    bad = []
    for i, (a, e) in enumerate(zip(actual, expected)):
        a_real = to_real(a)
        err = abs(a_real - e)
        max_error = max(max_error, err)
        if err > tolerance:
            bad.append((i, a_real, e, err))
    return max_error, bad


def dense_matrix_to_bsgs_plain_inputs(
    matrix: np.ndarray,
    *,
    n: int,
    slots: int,
    scale: int,
) -> Dict[str, tuple[List[float], int]]:
    """
    Correct cyclic-diagonal mapping for dense square matvec.

    Want:
      y[r] = sum_c A[r, c] * x[c]

    With x >> k meaning "pick x[r+k]" under cyclic slot convention,
    diagonal k should hold:
      diag_k[r] = A[r, (r + k) mod n]

    This is the classic diagonal method.
    """
    babysteps, giantsteps = choose_bsgs(n)

    plain_inputs: Dict[str, tuple[List[float], int]] = {}
    for gs in giantsteps:
        for bs in babysteps:
            k = gs + bs
            name = f"A_{bs}_{gs}"
            diag = np.zeros(slots, dtype=np.float64)
            if k < n:
                for r in range(n):
                    c = (r + k) % n
                    diag[r] = float(matrix[r, c])
            plain_inputs[name] = (diag.tolist(), scale)
    return plain_inputs


def make_case_matrix(case: str, n: int, rng: np.random.Generator) -> np.ndarray:
    """
    Three useful test modes:
      identity     : A = I
      one_diag     : single cyclic diagonal
      random       : dense random
    """
    if case == "identity":
        return np.eye(n, dtype=np.float64)

    if case == "one_diag":
        # One cyclic diagonal with shift=3 and value=0.5
        A = np.zeros((n, n), dtype=np.float64)
        shift = 3 % n
        for r in range(n):
            c = (r + shift) % n
            A[r, c] = 0.5
        return A

    if case == "random":
        return rng.normal(loc=0.0, scale=0.02, size=(n, n)).astype(np.float64)

    raise ValueError(f"unknown case: {case}")


def run_plain_reference(matrix: np.ndarray, vector: np.ndarray) -> np.ndarray:
    return matrix @ vector


def benchmark_one_size(
    *,
    n: int,
    case: str,
    slots: int,
    num_chips: int,
    register_file_size: int,
    out_dir: pathlib.Path,
    seed: int,
    tolerance: float,
) -> Dict[str, Any]:
    if n > slots:
        raise ValueError(f"n must be <= slots, got n={n}, slots={slots}")

    scale = 1 << SCALE_BITS
    rng = np.random.default_rng(seed)

    matrix = make_case_matrix(case, n, rng)
    vector = rng.normal(loc=0.0, scale=0.05, size=(n,)).astype(np.float64)

    t_compile_0 = time.perf_counter()
    program = build_program(n, num_chips)
    cinnamon_compile(program, LEVEL, num_chips, register_file_size, str(out_dir) + "/")
    t_compile_1 = time.perf_counter()

    context = cinnamon_emulator.Context(slots, PRIMES)
    encryptor = build_encryptor(context, slots)

    x_values = np.zeros(slots, dtype=np.float64)
    x_values[:n] = vector

    raw_inputs: Dict[str, tuple[List[float], int]] = {
        "x": (x_values.tolist(), scale),
    }
    raw_inputs.update(
        dense_matrix_to_bsgs_plain_inputs(
            matrix,
            n=n,
            slots=slots,
            scale=scale,
        )
    )

    instructions_base = str(out_dir / "instructions")
    program_inputs = str(out_dir / "program_inputs")
    evalkeys = str(out_dir / "evalkeys")

    runtime = cinnamon_fpga.Emulator(
        context,
        target="sw_emu",
        xclbin_path=ROOT_DIR / "build" / "sw_emu" / "cinnamon_fpga.sw_emu.xclbin",
        board_indices=[0],
        require_kernel_execution=True,
        verify_kernel_results=True,
    )

    runtime.generate_and_serialize_evalkeys(evalkeys, program_inputs, encryptor)
    runtime.generate_inputs(program_inputs, evalkeys, raw_inputs, encryptor)

    t_run_0 = time.perf_counter()
    runtime.run_program(instructions_base, num_chips, register_file_size)
    t_run_1 = time.perf_counter()

    dispatch = cinnamon_fpga.describe_last_dispatch(runtime)
    kernel_outputs = runtime.get_kernel_outputs()

    cpu = cinnamon_emulator.Emulator(context)
    cpu.generate_and_serialize_evalkeys(evalkeys, program_inputs, encryptor)
    cpu.generate_inputs(program_inputs, evalkeys, raw_inputs, encryptor)
    cpu.run_program(instructions_base, num_chips, register_file_size)

    # One rescale after plaintext*ciphertext accumulation
    output_scales = {
        "y": float((scale * scale) / PRIMES[LEVEL - 1])
    }

    decrypted = cpu.get_decrypted_outputs(encryptor, output_scales)
    he_values = list(decrypted.get("y", []))[:n]
    plain_values = run_plain_reference(matrix, vector).tolist()

    max_error, bad = compare_vectors(he_values, plain_values, tolerance=tolerance)

    return {
        "size": n,
        "case": case,
        "compile_s": float(t_compile_1 - t_compile_0),
        "run_s": float(t_run_1 - t_run_0),
        "max_error": float(max_error),
        "mismatch_count": len(bad),
        "pass": len(bad) == 0,
        "plain_preview": [float(v) for v in plain_values[:8]],
        "he_preview_real": [to_real(v) for v in he_values[:8]],
        "he_preview_imag": [to_imag(v) for v in he_values[:8]],
        "first_bad": bad[:10],
        "dispatch": dispatch,
        "kernel_outputs": kernel_outputs,
    }


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Dense square BSGS matvec benchmark using sw_emu"
    )
    parser.add_argument(
        "--sizes",
        default="16,32,64,128",
        help="Comma-separated square sizes like 16,32,64,128",
    )
    parser.add_argument(
        "--case",
        choices=["identity", "one_diag", "random"],
        default="identity",
    )
    parser.add_argument("--slots", type=int, default=DEFAULT_SLOTS)
    parser.add_argument("--chips", type=int, default=1)
    parser.add_argument("--register-file-size", type=int, default=1024)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--tolerance", type=float, default=1e-3)
    parser.add_argument(
        "--work-root",
        type=pathlib.Path,
        default=ROOT_DIR / "build" / "cinnamon_bsgs_matmul_swemu",
    )
    args = parser.parse_args()

    sizes = parse_sizes(args.sizes)
    args.work_root.mkdir(parents=True, exist_ok=True)

    all_results: List[Dict[str, Any]] = []

    for n in sizes:
        print(f"\n=== Running BSGS matmul {n}x{n} ({args.case}) ===")
        out_dir = args.work_root / f"{args.case}_{n}x{n}"
        out_dir.mkdir(parents=True, exist_ok=True)

        result = benchmark_one_size(
            n=n,
            case=args.case,
            slots=args.slots,
            num_chips=args.chips,
            register_file_size=args.register_file_size,
            out_dir=out_dir,
            seed=args.seed + n,
            tolerance=args.tolerance,
        )
        all_results.append(result)

        print(f"compile_s       = {result['compile_s']:.6f}")
        print(f"run_s           = {result['run_s']:.6f}")
        print(f"max_error       = {result['max_error']:.6e}")
        print(f"mismatch_count  = {result['mismatch_count']}")
        print(f"pass            = {result['pass']}")
        print(f"plain_preview   = {result['plain_preview']}")
        print(f"he_preview_real = {result['he_preview_real']}")

    report = {
        "generated_at": datetime.now().isoformat(),
        "case": args.case,
        "scale_bits": SCALE_BITS,
        "level": LEVEL,
        "rns_bit_size": RNS_BIT_SIZE,
        "slots": args.slots,
        "sizes": sizes,
        "results": all_results,
    }

    report_path = args.work_root / f"report_{args.case}.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"\nReport written to: {report_path}")


if __name__ == "__main__":
    main()