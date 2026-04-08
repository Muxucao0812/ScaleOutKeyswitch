from __future__ import annotations

import argparse
import pathlib

from .parser import (
    encode_instruction_descriptor,
    module_kernels,
)
from .pyxrt_runner import DispatchConfig, run_partition_dispatch


def main() -> int:
    parser = argparse.ArgumentParser(description="cinnamon_fpga sw_emu smoke test")
    parser.add_argument("--xclbin", required=True)
    parser.add_argument("--kernel", default="cinnamon_memory")
    parser.add_argument("--board", type=int, default=0)
    parser.add_argument("--partition", type=int, default=0)
    args = parser.parse_args()

    instruction_words = encode_instruction_descriptor("mov r1: r0")
    register_count = 8
    modulus = 268042241
    input_words = [0x43494E4E414D4F4E, register_count, modulus, 11, 7, 0, 0, 0, 0, 0, 0]

    cfg = DispatchConfig(
        xclbin_path=pathlib.Path(args.xclbin),
        kernel_name=args.kernel,
        output_word_count=6 + register_count,
    )

    result = run_partition_dispatch(
        cfg,
        board_index=args.board,
        partition_id=args.partition,
        instruction_words=instruction_words,
        input_words=input_words,
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
        "montgomery": 3,
        "ntt": 4,
        "base_conv": 5,
        "automorphism": 6,
        "transpose": 7,
    }
    expected_module_id = module_id_map[module_name]

    if len(result.output_words) < 6:
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
    if executed != 1:
        raise RuntimeError(f"kernel executed={executed}, expected 1")
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

    # For memory smoke instruction "mov r1: r0", state[0] and state[1] must both be 11.
    if module_name == "memory":
        if len(result.output_words) < 6 + 2:
            raise RuntimeError("memory smoke output does not include expected state words")
        state0 = int(result.output_words[6])
        state1 = int(result.output_words[7])
        if state0 != 11 or state1 != 11:
            raise RuntimeError(
                f"memory smoke state mismatch: state[0]={state0}, state[1]={state1}, expected 11/11"
            )

    print("cinnamon_fpga smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
