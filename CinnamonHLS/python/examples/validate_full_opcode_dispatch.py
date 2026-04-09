from __future__ import annotations

import argparse
import pathlib
from typing import Any, Dict, List

import cinnamon_emulator
import cinnamon_fpga

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

DEFAULT_SLOTS = 64
DEFAULT_REGISTER_FILE_SIZE = 1024
EXPECTED_MODULE_ORDER = [
    "memory",
    "arithmetic",
    "montgomery",
    "ntt",
    "base_conv",
    "automorphism",
]
EXPECTED_HOST_SYNC_OPS = ("syn", "rcv", "dis", "drm")


def write_instruction_fixture(path: pathlib.Path) -> None:
    """Write a synthetic instruction stream that touches many opcode classes."""
    lines = [
        "Instruction Stream 0:",
        "load r0: i0(0)",
        "load s0: p0(0)",
        "store r0: o0(0)",
        "spill r0: o0(1)",
        "mov r1: r0",
        "add r2: r0, r1 | 0",
        "sub r3: r2, r1 | 0",
        "mul r4: r2, r3 | 0",
        "mup r5: r4, r3 | 0",
        "mus r6: r5, r4 | 0",
        "ads r7: r6, r5 | 0",
        "sus r8: r7, r6 | 0",
        "sud r9: r8, r7 | 0",
        "ntt r10: r9 | 0",
        "int r11: r10 | 0",
        "rot r12: r11 | 2",
        "con r13: r12, 1 | 0",
        "neg r14: r13 | 0",
        "bci B0: [0,1], [0]",
        "pl1 B0: r14 | 0",
        "bcw B0: r14 | 0",
        "rsi {r0, r1}",
        "rsv {r15, r16}: r14: [0,1] | 0",
        "mod r20: {r15, r16} | 0",
        "evg r17: k0(0)",
        "rec r21: i0(0) | 0",
        "snd r21: o0(0) | 0",
        "joi @ 10:1 r18: r17 | 0",
        "jol @ 11:1 :",
        "syn @ 12:1 :",
        "rcv @ 13:1 r19:",
        "dis @ 14:1 : r19",
        "drm @ 15:1 :",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_program_inputs_fixture(path: pathlib.Path) -> None:
    """Write a minimal synthetic program-input description."""
    lines = [
        "Ciphertext Stream:",
        "i0 | x:c0 | [0,1]",
        ";",
        "Plaintext Stream:",
        "p0 | p:c0 | [0,1]",
        ";",
        "Scalar Stream:",
        ";",
        "Output Stream:",
        "o0 | z:c0 | [0,1]",
        ";",
        "Evalkey Stream:",
        ";",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_context(slots: int) -> cinnamon_emulator.Context:
    """Create a valid Cinnamon emulator context."""
    return cinnamon_emulator.Context(slots, PRIMES)


def validate_outputs(outputs: List[Dict[str, Any]]) -> None:
    """Validate module dispatch order and host-sync opcode counts."""
    if not outputs:
        raise RuntimeError("No kernel outputs were produced.")

    if len(outputs) != 1:
        raise RuntimeError(f"Expected exactly one partition output, got {len(outputs)}")

    partition_output = outputs[0]
    module_results = partition_output.get("module_results", [])
    modules = [entry.get("module") for entry in module_results]

    if modules != EXPECTED_MODULE_ORDER:
        raise RuntimeError(
            f"Unexpected module dispatch order: {modules}, expected {EXPECTED_MODULE_ORDER}"
        )

    host_sync = partition_output.get("host_sync", {})
    host_counts = host_sync.get("opcode_counts", {})

    for op in EXPECTED_HOST_SYNC_OPS:
        actual = host_counts.get(op, 0)
        if actual != 1:
            raise RuntimeError(
                f"Host opcode {op!r} expected count=1 but got {actual}"
            )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a full-opcode dispatch check against Cinnamon FPGA kernels"
    )
    parser.add_argument(
        "--xclbin",
        type=pathlib.Path,
        default=ROOT_DIR / "build" / "sw_emu" / "cinnamon_fpga.sw_emu.xclbin",
        help="Path to cinnamon_fpga.<target>.xclbin",
    )
    parser.add_argument(
        "--target",
        choices=["sw_emu", "hw_emu", "hw"],
        default="sw_emu",
        help="Execution target",
    )
    parser.add_argument(
        "--board",
        type=int,
        default=0,
        help="Board index to use",
    )
    parser.add_argument(
        "--work-dir",
        type=pathlib.Path,
        default=ROOT_DIR / "build" / "cinnamon_full_opcode_dispatch",
        help="Working directory for generated fixtures",
    )
    parser.add_argument(
        "--slots",
        type=int,
        default=DEFAULT_SLOTS,
        help="Number of slots used to build a valid emulator context",
    )
    parser.add_argument(
        "--register-file-size",
        type=int,
        default=DEFAULT_REGISTER_FILE_SIZE,
        help="Register file size passed to runtime.run_program(...)",
    )
    args = parser.parse_args()

    args.work_dir.mkdir(parents=True, exist_ok=True)

    instructions_path = args.work_dir / "instructions"
    program_inputs_path = args.work_dir / "program_inputs"

    write_instruction_fixture(instructions_path)
    write_program_inputs_fixture(program_inputs_path)

    if not args.xclbin.exists():
        raise FileNotFoundError(f"xclbin not found: {args.xclbin}")

    context = build_context(args.slots)

    runtime = cinnamon_fpga.Emulator(
        context=context,
        target=args.target,
        xclbin_path=args.xclbin,
        board_indices=[args.board],
        require_kernel_execution=True,
        verify_kernel_results=True,
    )

    runtime.run_program(
        str(instructions_path),
        num_partitions=1,
        register_file_size=args.register_file_size,
    )

    outputs = runtime.get_kernel_outputs()
    validate_outputs(outputs)

    partition_output = outputs[0]
    modules = [entry["module"] for entry in partition_output["module_results"]]

    print("Full opcode dispatch passed")
    print(f"modules={modules}")
    print(f"host_sync={partition_output['host_sync']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())