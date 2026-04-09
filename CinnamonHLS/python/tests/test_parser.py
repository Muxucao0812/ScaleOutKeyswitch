from __future__ import annotations

import pathlib

from cinnamon_fpga.parser import (
    encode_instruction_descriptor,
    encode_instruction_line,
    encode_stream_token,
    expected_module_output_words,
    module_for_opcode,
    parse_instruction_file,
    split_stream_by_module,
    expected_kernel_checksum,
    load_instruction_streams,
    parse_program_inputs,
    summarize_opcodes,
)


def test_encode_instruction_line_is_stable() -> None:
    line = "add r1: r2, r3 | 10"
    assert encode_instruction_line(line) == encode_instruction_line(line)


def test_encode_stream_token_is_structured() -> None:
    assert encode_stream_token("i1(0)") == encode_stream_token("i1(0)")
    assert encode_stream_token("i1(0){F}") != encode_stream_token("i1(0)")


def test_parse_instruction_and_program_inputs(tmp_path: pathlib.Path) -> None:
    instructions = tmp_path / "instructions0"
    instructions.write_text(
        """Instruction Stream 0:
load r0: i1(0)
add r1: r0, r0 | 0
store r1: o0(0)
""",
        encoding="utf-8",
    )

    program_inputs = tmp_path / "program_inputs"
    program_inputs.write_text(
        """Ciphertext Stream:
i1 | x:c0 | [0,1]
;
Plaintext Stream:
;
Scalar Stream:
;
Output Stream:
v1 | z:c0 | [0,1]
;
Evalkey Stream:
;
""",
        encoding="utf-8",
    )

    streams = load_instruction_streams(tmp_path / "instructions", num_partitions=1)
    assert len(streams) == 1
    assert streams[0].partition_id == 0
    assert streams[0].opcodes["load"] == 1
    assert streams[0].opcodes["add"] == 1
    assert streams[0].opcodes["store"] == 1

    summary = summarize_opcodes(streams)
    assert summary["add"] == 1

    parsed = parse_program_inputs(program_inputs)
    assert len(parsed.sections["ciphertext"]) == 1
    assert len(parsed.sections["output"]) == 1
    assert len(parsed.words) == 2


def test_expected_kernel_checksum_is_deterministic() -> None:
    ins = [1, 2, 3]
    inp = [10, 20]
    assert expected_kernel_checksum(ins, inp) == expected_kernel_checksum(ins, inp)


def test_opcode_module_mapping_covers_all_major_paths() -> None:
    assert module_for_opcode("load") == "memory"
    assert module_for_opcode("loas") == "memory"
    assert module_for_opcode("spill") == "memory"
    assert module_for_opcode("snd") == "memory"
    assert module_for_opcode("add") == "arithmetic"
    assert module_for_opcode("mul") == "montgomery"
    assert module_for_opcode("ntt") == "ntt"
    assert module_for_opcode("bci") == "base_conv"
    assert module_for_opcode("bcw") == "base_conv"
    assert module_for_opcode("rsi") == "base_conv"
    assert module_for_opcode("mod") == "base_conv"
    assert module_for_opcode("rot") == "automorphism"
    assert module_for_opcode("rec") == "memory"
    assert module_for_opcode("syn") == "host_comm"


def test_split_stream_by_module(tmp_path: pathlib.Path) -> None:
    instructions = tmp_path / "instructions0"
    instructions.write_text(
        """Instruction Stream 0:
load r0: i1(0)
add r1: r0, r0 | 0
mul r2: r1, r1 | 0
ntt r3: r2 | 0
rot r4: r3 | 0
bci B0: [1,2], [0]
rec r6: i1(0) | 0
syn @ 1:1 :
""",
        encoding="utf-8",
    )
    stream = parse_instruction_file(instructions, partition_id=0)
    buckets = split_stream_by_module(stream)
    assert len(buckets["memory"].instruction_words) == 8
    assert len(buckets["arithmetic"].instruction_words) == 4
    assert len(buckets["montgomery"].instruction_words) == 4
    assert len(buckets["ntt"].instruction_words) == 4
    assert len(buckets["automorphism"].instruction_words) == 4
    assert len(buckets["base_conv"].instruction_words) == 4
    assert len(buckets["transpose"].instruction_words) == 0
    assert len(buckets["host_comm"].instruction_words) == 4


def test_extended_isa_syntax_parses_without_fallback(tmp_path: pathlib.Path) -> None:
    instructions = tmp_path / "instructions0"
    instructions.write_text(
        """Instruction Stream 0:
loas s0: p0(0)
spill r1: o0(0)
bcw B0: r2 | 3
rsi {r1, r2, r3}
rsv {r4, r5}: r3: [0,1] | 7
mod r6: {r4, r5} | 7
snd r6: o0(1) | 7
joi @ 10:2 r7: r6 | 7
rcv @ 10:2 r8:
dis @ 10:2 : r8
""",
        encoding="utf-8",
    )
    stream = parse_instruction_file(instructions, partition_id=0)
    buckets = split_stream_by_module(stream)
    assert stream.opcodes["loas"] == 1
    assert stream.opcodes["spill"] == 1
    assert stream.opcodes["bcw"] == 1
    assert stream.opcodes["rsi"] == 1
    assert stream.opcodes["rsv"] == 1
    assert stream.opcodes["mod"] == 1
    assert stream.opcodes["snd"] == 1
    assert len(buckets["memory"].instruction_words) == 12
    assert len(buckets["base_conv"].instruction_words) == 16
    assert len(buckets["host_comm"].instruction_words) == 12


def test_expected_module_output_words_shape() -> None:
    ins = encode_instruction_descriptor("add r1: r0, 7 | 0")
    outputs = expected_module_output_words(
        module_name="arithmetic",
        instruction_words=ins,
        input_words=[0x43494E4E414D4F4E, 4, 97, 11, 12, 13, 14],
        output_count=10,
        partition_id=2,
    )
    assert len(outputs) == 10
    assert outputs[0] == 0
    assert outputs[1] == 1
    assert outputs[2] == 4
    assert outputs[3] == 2
    assert outputs[4] == 2
    assert outputs[6:] == [11, 18, 13, 14]


def test_expected_module_output_words_rot_negative_uses_absolute_rotation() -> None:
    ins = encode_instruction_descriptor("rot -8 r60: r20 | 63")
    state = [1000 + i for i in range(64)]
    outputs = expected_module_output_words(
        module_name="automorphism",
        instruction_words=ins,
        input_words=state,
        output_count=80,
        partition_id=0,
    )

    assert outputs[1] == 1
    assert outputs[2] == 64
    # src=20, rot=-8 => abs(rot)=8 so mapped index is 28.
    assert outputs[6 + 60] == state[28]
    assert outputs[6 + 60] != state[12]
