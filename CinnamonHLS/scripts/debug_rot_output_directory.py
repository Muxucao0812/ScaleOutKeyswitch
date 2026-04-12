from __future__ import annotations

import pathlib
import sys
from typing import Dict

ROOT_DIR = pathlib.Path(__file__).resolve().parents[1]
REPO_DIR = ROOT_DIR.parent

for entry in (
    REPO_DIR / "build" / "cinnamon_compiler_local" / "python",
    REPO_DIR / "build" / "cinnamon_emulator_local" / "python",
    ROOT_DIR / "python",
):
    path = str(entry)
    if path not in sys.path:
        sys.path.insert(0, path)

import cinnamon_emulator
import cinnamon_fpga
from cinnamon.compiler import cinnamon_compile
from cinnamon.dsl import CiphertextInput, CinnamonProgram, Output
from cinnamon_fpga.api import _parse_payload_control_layout
from cinnamon_fpga.parser import encode_stream_token

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
SCALE_BITS = 56
LEVEL = 51
ROTATE_STEPS = 8


def build_program(num_chips: int = 1, rotate_steps: int = 8) -> CinnamonProgram:
    program = CinnamonProgram("Rotate8", rns_bit_size=28, num_chips=num_chips)
    with program:
        x = CiphertextInput("x", SCALE_BITS, LEVEL)
        z = (x >> rotate_steps)
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


def collect_output_directory(control_words: list[int]) -> Dict[int, int]:
    layout = _parse_payload_control_layout(control_words)
    pairs: Dict[int, int] = {}
    for index in range(layout.output_token_count):
        cursor = layout.output_directory_offset + (index * 2)
        token = int(control_words[cursor])
        handle = int(control_words[cursor + 1])
        pairs[token] = handle
    return pairs


def main() -> int:
    out_dir = ROOT_DIR / "build" / "cinnamon_tutorial_rot8_fpga"
    out_dir.mkdir(parents=True, exist_ok=True)

    num_chips = 1
    register_file_size = 1024
    slots = 64
    scale = 1 << SCALE_BITS

    program = build_program(num_chips, ROTATE_STEPS)
    cinnamon_compile(program, LEVEL, num_chips, register_file_size, str(out_dir) + "/")

    context = cinnamon_emulator.Context(slots, PRIMES)
    encryptor = build_encryptor(context, slots)
    raw_inputs = {"x": ([float(i) for i in range(slots)], scale)}
    output_scales = {"z": float(scale)}

    runtime = cinnamon_fpga.Emulator(
        context,
        target="sw_emu",
        xclbin_path=ROOT_DIR / "build" / "sw_emu" / "cinnamon_fpga.sw_emu.xclbin",
        board_indices=[0],
        require_kernel_execution=True,
        verify_kernel_results=False,
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
    runtime.run_program(str(out_dir / "instructions"), num_chips, register_file_size)

    if not runtime.last_dispatch:
        raise RuntimeError("No dispatches recorded")
    final_control = [int(word) for word in runtime.last_dispatch[-1].control_words]
    output_directory = collect_output_directory(final_control)

    expected_terms = [f"v5({idx})" for idx in range(51)] + [f"v24({idx})" for idx in range(51)]
    missing = []
    populated = []
    for term in expected_terms:
        token = int(encode_stream_token(term))
        handle = int(output_directory.get(token, 0))
        if handle == 0:
            missing.append(term)
        else:
            populated.append((term, handle))

    print(f"output_pairs_total={len(output_directory)}")
    print(f"expected_terms={len(expected_terms)}")
    print(f"populated_terms={len(populated)}")
    print(f"missing_terms={len(missing)}")
    print(f"first_missing={missing[:20]}")
    print(f"first_populated={populated[:20]}")

    for idx, dispatch in enumerate(runtime.last_dispatch):
        control_words = [int(word) for word in dispatch.control_words]
        output_dir = collect_output_directory(control_words)
        populated_count = sum(1 for handle in output_dir.values() if int(handle) != 0)
        if dispatch.kernel_name.endswith("memory"):
            print(
                "memory_stage",
                idx,
                "status",
                int(dispatch.output_words[0]) if dispatch.output_words else None,
                "executed",
                int(dispatch.output_words[1]) if len(dispatch.output_words) > 1 else None,
                "populated_outputs",
                populated_count,
            )

    decrypted = runtime.get_decrypted_outputs(encryptor, output_scales)
    z_values = list(decrypted.get("z", []))
    print(f"decrypted_preview={z_values[:8]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
