from __future__ import annotations

import argparse
import pathlib
from typing import Any, Dict, List, Sequence

import cinnamon_emulator
import cinnamon_fpga
from cinnamon.compiler import cinnamon_compile
from cinnamon.dsl import CiphertextInput, CinnamonProgram, Output
from cinnamon_fpga.golden import compare_case, load_golden, normalize_kernel_outputs, write_golden

ROOT_DIR = pathlib.Path(__file__).resolve().parents[2]

# Reference primes used by Cinnamon tutorial examples.
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


def build_program(num_chips: int) -> CinnamonProgram:
    program = CinnamonProgram("Add", rns_bit_size=28, num_chips=num_chips)
    with program:
        x = CiphertextInput("x", 28, 51)
        y = CiphertextInput("y", 28, 51)
        z = x + y
        Output("z", z)
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


def parse_int_list(value: str) -> List[int]:
    values = [int(x.strip()) for x in value.split(",") if x.strip()]
    if not values:
        raise ValueError(f"invalid comma-separated integer list: {value!r}")
    return values


def run_case(
    *,
    target: str,
    xclbin: pathlib.Path,
    work_root: pathlib.Path,
    num_chips: int,
    board_indices: Sequence[int],
    slots: int,
    scale: int,
    register_file_size: int,
) -> List[Dict[str, Any]]:
    if len(board_indices) < num_chips:
        raise RuntimeError(
            f"need at least {num_chips} boards, got {len(board_indices)} ({board_indices})"
        )

    out_dir = work_root / target / f"chips_{num_chips}"
    out_dir.mkdir(parents=True, exist_ok=True)

    program = build_program(num_chips)
    cinnamon_compile(program, 51, num_chips, register_file_size, str(out_dir) + "/")

    context = cinnamon_emulator.Context(slots, PRIMES)
    encryptor = build_encryptor(context, slots)
    raw_inputs = {
        "x": ([float(i) for i in range(slots)], scale),
        "y": ([float(2 * i) for i in range(slots)], scale),
    }

    instructions_base = str(out_dir / "instructions")
    program_inputs = str(out_dir / "program_inputs")
    evalkeys = str(out_dir / "evalkeys")

    fpga = cinnamon_fpga.Emulator(
        context,
        target=target,
        xclbin_path=xclbin,
        board_indices=list(board_indices[:num_chips]),
        parallel_dispatch=not (target == "sw_emu" and num_chips > 1),
        require_kernel_execution=True,
        verify_kernel_results=True,
    )
    fpga.generate_and_serialize_evalkeys(evalkeys, program_inputs, encryptor)
    fpga.generate_inputs(program_inputs, evalkeys, raw_inputs, encryptor)
    fpga.run_program(instructions_base, num_chips, register_file_size)

    outputs = fpga.get_kernel_outputs()
    print(
        f"[chips={num_chips}] partition_count={len(outputs)} "
        f"boards={[entry['board_index'] for entry in outputs]}"
    )
    for entry in outputs:
        modules = [item["module"] for item in entry["module_results"]]
        print(
            f"[chips={num_chips}] partition={entry['partition_id']} "
            f"modules={modules} host_sync={entry['host_sync']}"
        )
    return outputs


def default_golden_path() -> pathlib.Path:
    return ROOT_DIR / "golden" / "tutorial_add_kernel_outputs.json"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate Cinnamon FPGA backend using fixed golden kernel outputs (no CPU runtime fallback)"
    )
    parser.add_argument("--target", choices=["sw_emu", "hw_emu", "hw"], required=True)
    parser.add_argument("--xclbin", required=True, type=pathlib.Path)
    parser.add_argument("--chips", default="1,2,4")
    parser.add_argument("--boards", default="0,1,2,3")
    parser.add_argument(
        "--work-root",
        default=ROOT_DIR / "build" / "cinnamon_fpga_backend_validate",
        type=pathlib.Path,
    )
    parser.add_argument("--slots", default=64, type=int)
    parser.add_argument("--scale", default=1 << 28, type=int)
    parser.add_argument("--register-file-size", default=1024, type=int)
    parser.add_argument("--golden", type=pathlib.Path, default=default_golden_path())
    parser.add_argument("--write-golden", action="store_true")
    args = parser.parse_args()

    chip_list = parse_int_list(args.chips)
    board_list = parse_int_list(args.boards)
    if not args.xclbin.exists():
        raise FileNotFoundError(f"xclbin not found: {args.xclbin}")

    print(
        f"target={args.target} xclbin={args.xclbin} chips={chip_list} "
        f"boards={board_list} golden={args.golden} write_golden={args.write_golden}"
    )

    actual_cases: Dict[str, List[Dict[str, Any]]] = {}
    for chips in chip_list:
        outputs = run_case(
            target=args.target,
            xclbin=args.xclbin,
            work_root=args.work_root,
            num_chips=chips,
            board_indices=board_list,
            slots=args.slots,
            scale=args.scale,
            register_file_size=args.register_file_size,
        )
        actual_cases[str(chips)] = normalize_kernel_outputs(outputs)

    if args.write_golden:
        payload = {
            "program": "tutorial_add",
            "target_reference": args.target,
            "chips": chip_list,
            "cases": actual_cases,
        }
        write_golden(args.golden, payload)
        print(f"Wrote golden file: {args.golden}")
        return 0

    golden_payload = load_golden(args.golden)
    golden_cases = golden_payload.get("cases", {})
    for key in actual_cases:
        if key not in golden_cases:
            raise RuntimeError(f"golden is missing chips={key} case")
        ok, message = compare_case(golden_cases[key], actual_cases[key])
        if not ok:
            raise RuntimeError(f"golden mismatch for chips={key}: {message}")

    print("All FPGA backend validation cases passed against fixed golden")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
