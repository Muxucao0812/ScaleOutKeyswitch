from __future__ import annotations

import pathlib

from cinnamon_fpga.api import Emulator
from cinnamon_fpga.parser import (
    expected_module_output_words,
    module_kernels,
)
from cinnamon_fpga.pyxrt_runner import PartitionDispatchResult


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


def test_runtime_dispatch_routes_all_modules(monkeypatch, tmp_path: pathlib.Path) -> None:
    instructions = tmp_path / "instructions"
    program_inputs = tmp_path / "program_inputs"
    fake_xclbin = tmp_path / "fake.xclbin"

    _write_instruction_fixture(instructions)
    _write_program_inputs_fixture(program_inputs)
    fake_xclbin.write_bytes(b"xclbin")

    kernel_to_module = {kernel: module for module, kernel in module_kernels().items()}
    dispatched_modules: list[str] = []

    def fake_run_dispatches(config, board_indices, partition_words, input_words, parallel):
        del parallel
        if input_words and isinstance(input_words[0], list):
            partition_inputs = input_words
        else:
            partition_inputs = [input_words for _ in range(len(partition_words))]

        module_name = kernel_to_module[config.kernel_name]
        dispatched_modules.append(module_name)

        results: list[PartitionDispatchResult] = []
        for partition_id, words in enumerate(partition_words):
            module_inputs = partition_inputs[partition_id]
            outputs = expected_module_output_words(
                module_name=module_name,
                instruction_words=words,
                input_words=module_inputs,
                output_count=config.output_word_count,
                partition_id=partition_id,
            )
            results.append(
                PartitionDispatchResult(
                    kernel_name=config.kernel_name,
                    partition_id=partition_id,
                    board_index=board_indices[partition_id],
                    instruction_count=len(words),
                    input_count=len(module_inputs),
                    output_words=outputs,
                )
            )
        return results

    monkeypatch.setattr("cinnamon_fpga.api.run_dispatches", fake_run_dispatches)

    runtime = Emulator(
        context=object(),
        target="sw_emu",
        xclbin_path=fake_xclbin,
        board_indices=[0],
        verify_kernel_results=True,
        require_kernel_execution=True,
    )
    runtime.run_program(str(instructions), num_partitions=1, register_file_size=1024)

    # Every compute module should be dispatched exactly once.
    assert sorted(dispatched_modules) == sorted(
        ["memory", "arithmetic", "montgomery", "ntt", "base_conv", "automorphism"]
    )

    outputs = runtime.get_kernel_outputs()
    assert len(outputs) == 1
    modules = [item["module"] for item in outputs[0]["module_results"]]
    assert sorted(modules) == sorted(dispatched_modules)
    assert "timing_breakdown" in outputs[0]
    assert "timing_summary" in outputs[0]
    timing = outputs[0]["timing_breakdown"]
    assert "compute_wait_total_s" in timing
    assert "schedule_total_s" in timing
    for module_entry in outputs[0]["module_results"]:
        assert "timing" in module_entry

    # Host communication opcodes stay host-managed and are visible in host_sync.
    host_counts = outputs[0]["host_sync"]["opcode_counts"]
    assert host_counts["syn"] == 1
    assert host_counts["rcv"] == 1
    assert host_counts["dis"] == 1
    assert host_counts["drm"] == 1
    assert host_counts["joi"] == 1
    assert host_counts["jol"] == 1


def test_runtime_dispatch_propagates_partition_state(monkeypatch, tmp_path: pathlib.Path) -> None:
    instructions = tmp_path / "instructions"
    program_inputs = tmp_path / "program_inputs"
    fake_xclbin = tmp_path / "fake.xclbin"

    instructions.write_text(
        "\n".join(
            [
                "Instruction Stream 0:",
                "load r0: i0(0)",
                "mov r1: r0",
                "add r2: r0, r1 | 0",
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    _write_program_inputs_fixture(program_inputs)
    fake_xclbin.write_bytes(b"xclbin")

    kernel_to_module = {kernel: module for module, kernel in module_kernels().items()}
    seen_inputs: dict[str, list[list[int]]] = {}

    def fake_run_dispatches(config, board_indices, partition_words, input_words, parallel):
        del parallel
        if input_words and isinstance(input_words[0], list):
            partition_inputs = input_words
        else:
            partition_inputs = [input_words for _ in range(len(partition_words))]

        module_name = kernel_to_module[config.kernel_name]
        seen_inputs[module_name] = [list(words) for words in partition_inputs]

        results: list[PartitionDispatchResult] = []
        for partition_id, words in enumerate(partition_words):
            module_inputs = partition_inputs[partition_id]
            outputs = expected_module_output_words(
                module_name=module_name,
                instruction_words=words,
                input_words=module_inputs,
                output_count=config.output_word_count,
                partition_id=partition_id,
            )
            results.append(
                PartitionDispatchResult(
                    kernel_name=config.kernel_name,
                    partition_id=partition_id,
                    board_index=board_indices[partition_id],
                    instruction_count=len(words),
                    input_count=len(module_inputs),
                    output_words=outputs,
                )
            )
        return results

    monkeypatch.setattr("cinnamon_fpga.api.run_dispatches", fake_run_dispatches)

    runtime = Emulator(
        context=object(),
        target="sw_emu",
        xclbin_path=fake_xclbin,
        board_indices=[0],
        verify_kernel_results=True,
        require_kernel_execution=True,
    )
    runtime.run_program(str(instructions), num_partitions=1, register_file_size=16)

    assert "memory" in seen_inputs
    assert "arithmetic" in seen_inputs

    memory_input = seen_inputs["memory"][0]
    arithmetic_input = seen_inputs["arithmetic"][0]
    assert arithmetic_input != memory_input

    outputs = runtime.get_kernel_outputs()[0]["module_results"]
    memory_output = next(entry for entry in outputs if entry["module"] == "memory")["output_words"]
    expected_next = [
        0x43494E4E414D4F4E,
        memory_output[2],
        memory_input[2],
        *memory_output[6 : 6 + memory_output[2]],
        *memory_input[3 + memory_output[2] :],
    ]
    assert arithmetic_input == expected_next
