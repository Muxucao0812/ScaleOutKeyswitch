from __future__ import annotations

import argparse
import pathlib

import cinnamon_fpga

ROOT_DIR = pathlib.Path(__file__).resolve().parents[2]


def _write_instruction_fixture(path: pathlib.Path) -> None:
    path.write_text(
        "\n".join(
            [
                "Instruction Stream 0:",
                "load r0: i0(0)",
                "loas s0: p0(0)",
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
        )
        + "\n",
        encoding="utf-8",
    )


def _write_program_inputs_fixture(path: pathlib.Path) -> None:
    path.write_text(
        "\n".join(
            [
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
        )
        + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a full-opcode dispatch check against Cinnamon FPGA kernels"
    )
    parser.add_argument(
        "--xclbin",
        required=True,
        type=pathlib.Path,
        help="Path to cinnamon_fpga.<target>.xclbin",
    )
    parser.add_argument("--target", choices=["sw_emu", "hw_emu", "hw"], default="sw_emu")
    parser.add_argument("--board", type=int, default=0)
    parser.add_argument(
        "--work-dir",
        type=pathlib.Path,
        default=ROOT_DIR / "build" / "cinnamon_full_opcode_dispatch",
    )
    args = parser.parse_args()

    args.work_dir.mkdir(parents=True, exist_ok=True)
    instructions = args.work_dir / "instructions"
    program_inputs = args.work_dir / "program_inputs"
    _write_instruction_fixture(instructions)
    _write_program_inputs_fixture(program_inputs)

    runtime = cinnamon_fpga.Emulator(
        context=object(),
        target=args.target,
        xclbin_path=args.xclbin,
        board_indices=[args.board],
        require_kernel_execution=True,
        verify_kernel_results=True,
    )
    runtime.run_program(str(instructions), num_partitions=1, register_file_size=1024)
    outputs = runtime.get_kernel_outputs()

    modules = [m["module"] for m in outputs[0]["module_results"]]
    expected = ["memory", "arithmetic", "montgomery", "ntt", "base_conv", "automorphism"]
    if modules != expected:
        raise RuntimeError(f"Unexpected module dispatch order: {modules}, expected {expected}")

    host_counts = outputs[0]["host_sync"]["opcode_counts"]
    for op in ("syn", "rcv", "dis", "drm"):
        if host_counts.get(op, 0) != 1:
            raise RuntimeError(f"Host opcode {op} expected count=1 but got {host_counts.get(op, 0)}")

    print("Full opcode dispatch passed")
    print(f"modules={modules}")
    print(f"host_sync={outputs[0]['host_sync']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
