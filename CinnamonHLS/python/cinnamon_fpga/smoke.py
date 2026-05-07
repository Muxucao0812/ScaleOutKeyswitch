from __future__ import annotations

import argparse
import pathlib

from .parser import module_kernels
from .pyxrt_runner import DispatchConfig, run_payload_partition_dispatch

_PAYLOAD_CONTROL_MAGIC = 0x43494E4E5041594C
_PAYLOAD_CONTROL_VERSION = 1
_TOKEN_OPERAND_ID = 0xFFE
_IN_TOKEN = 0x100
_OUT_TOKEN = 0x200
_OUTPUT_HEADER_WORDS = 6


def _encode_word0(
    opcode: int,
    dst: int,
    src0: int,
    src1: int,
    rns: int,
    flags: int,
) -> int:
    return (
        (opcode & 0xFF)
        | ((dst & 0xFFF) << 8)
        | ((src0 & 0xFFF) << 20)
        | ((src1 & 0xFFF) << 32)
        | ((rns & 0xFFF) << 44)
        | ((flags & 0xFF) << 56)
    )


def _encode_word1(imm0: int, imm1: int) -> int:
    return ((imm0 & 0xFFFFFFFF) | ((imm1 & 0xFFFFFFFF) << 32)) & 0xFFFFFFFFFFFFFFFF


def main() -> int:
    parser = argparse.ArgumentParser(description="cinnamon_fpga sw_emu smoke test")
    parser.add_argument("--xclbin", required=True)
    parser.add_argument("--kernel", default="cinnamon_memory")
    parser.add_argument("--board", type=int, default=0)
    parser.add_argument("--partition", type=int, default=0)
    args = parser.parse_args()

    if args.kernel != "cinnamon_memory":
        raise RuntimeError(
            "smoke currently validates cinnamon_memory payload ABI only; "
            f"got kernel={args.kernel}"
        )

    instruction_words = [
        _encode_word0(1, 3, _TOKEN_OPERAND_ID, 0, 0, 0),
        _encode_word1(0, 0),
        _IN_TOKEN,
        0,
        _encode_word0(3, 4, 3, 0, 0, 0),
        _encode_word1(0, 0),
        0,
        0,
        _encode_word0(2, 0, 4, _TOKEN_OPERAND_ID, 0, 0),
        _encode_word1(0, 0),
        _OUT_TOKEN,
        0,
    ]

    register_count = 8
    coeff_count = 4
    handle_capacity = 4
    control_words = [0] * (
        9
        + (1 * 2)
        + register_count
        + (handle_capacity * 2)
        + (1 * 2)
        + (1 * 2)
    )
    control_words[0] = _PAYLOAD_CONTROL_MAGIC
    control_words[1] = _PAYLOAD_CONTROL_VERSION
    control_words[2] = register_count
    control_words[3] = coeff_count
    control_words[4] = 1  # rns table count
    control_words[5] = 1  # handle count
    control_words[6] = handle_capacity
    control_words[7] = 1  # input token count
    control_words[8] = 1  # output token count
    control_words[9] = 0  # rns id
    control_words[10] = 97  # rns modulus
    control_words[19] = 0  # handle1 rns id
    control_words[20] = 1  # handle1 flags (ntt form in fixture)
    control_words[27] = _IN_TOKEN
    control_words[28] = 1
    control_words[29] = _OUT_TOKEN
    control_words[30] = 0

    payload_words = [0] * (handle_capacity * coeff_count)
    payload_words[0:4] = [11, 22, 33, 44]

    cfg = DispatchConfig(
        xclbin_path=pathlib.Path(args.xclbin),
        kernel_name=args.kernel,
        output_word_count=_OUTPUT_HEADER_WORDS + register_count,
        word_alignment=8,
    )

    result = run_payload_partition_dispatch(
        cfg,
        board_index=args.board,
        partition_id=args.partition,
        instruction_words=instruction_words,
        control_words=control_words,
        payload_words=payload_words,
    )

    kernel_to_module = {kernel: module for module, kernel in module_kernels().items()}
    if args.kernel not in kernel_to_module:
        raise RuntimeError(
            f"Unknown module kernel: {args.kernel}. "
            f"Expected one of {sorted(kernel_to_module.keys())}"
        )
    module_name = kernel_to_module[args.kernel]
    module_id_map = {
        "memory": 1,
        "arithmetic": 2,
        "modmul": 3,
        "ntt": 4,
        "base_conv": 5,
        "automorphism": 6,
        "transpose": 7,
    }
    expected_module_id = module_id_map[module_name]

    if len(result.output_words) < _OUTPUT_HEADER_WORDS:
        raise RuntimeError(
            f"kernel output too short for smoke: {len(result.output_words)} words"
        )

    status = int(result.output_words[0])
    executed = int(result.output_words[1])
    register_count_returned = int(result.output_words[2])
    module_id_returned = int(result.output_words[3])
    partition_returned = int(result.output_words[4])

    if status != 0:
        raise RuntimeError(f"kernel returned non-zero status={status}")
    if executed != 3:
        raise RuntimeError(f"kernel executed={executed}, expected 3")
    if register_count_returned != register_count:
        raise RuntimeError(
            f"register_count mismatch: got {register_count_returned}, expected {register_count}"
        )
    if module_id_returned != expected_module_id:
        raise RuntimeError(
            f"module_id mismatch: got {module_id_returned}, expected {expected_module_id}"
        )
    if partition_returned != args.partition:
        raise RuntimeError(
            f"partition mismatch: got {partition_returned}, expected {args.partition}"
        )

    if len(result.output_words) < _OUTPUT_HEADER_WORDS + register_count:
        raise RuntimeError("memory smoke output does not include expected register state")
    reg3 = int(result.output_words[_OUTPUT_HEADER_WORDS + 3])
    reg4 = int(result.output_words[_OUTPUT_HEADER_WORDS + 4])
    if reg3 != 1 or reg4 != 1:
        raise RuntimeError(
            f"memory smoke register-handle mismatch: r3={reg3}, r4={reg4}, expected 1/1"
        )

    if len(result.control_words) <= 30:
        raise RuntimeError(
            f"memory smoke control output too short: {len(result.control_words)} words"
        )
    out_handle = int(result.control_words[30])
    if out_handle != 1:
        raise RuntimeError(
            f"memory smoke output token handle mismatch: got {out_handle}, expected 1"
        )

    print("cinnamon_fpga smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
