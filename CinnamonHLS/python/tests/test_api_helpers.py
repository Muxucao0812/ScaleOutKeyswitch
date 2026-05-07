import pytest

from cinnamon_fpga.api import (
    Emulator,
    PayloadBciConfig,
    _build_payload_partition_buffers,
    _count_payload_allocations,
    _parse_payload_control_layout,
    _parse_payload_extra_layout,
    _resolve_board_indices,
    _remap_payload_base_conv_active_entries,
)


def test_board_index_default(monkeypatch) -> None:
    monkeypatch.delenv("CINNAMON_FPGA_BOARDS", raising=False)
    assert _resolve_board_indices(3, None) == [0, 1, 2]


def test_board_index_explicit(monkeypatch) -> None:
    monkeypatch.setenv("CINNAMON_FPGA_BOARDS", "9,10")
    assert _resolve_board_indices(2, [5, 6, 7])[:2] == [5, 6]


def test_run_program_requires_xclbin_path(tmp_path) -> None:
    inst = tmp_path / "instructions"
    inst.write_text("load r0: i0(0)\n", encoding="utf-8")
    runtime = Emulator(context=object(), require_kernel_execution=True)
    with pytest.raises(RuntimeError, match="xclbin_path"):
        runtime.run_program(str(inst), num_partitions=1, register_file_size=1024)


def test_runtime_has_no_decrypted_outputs_interface() -> None:
    runtime = Emulator(context=object())
    assert not hasattr(runtime, "get_decrypted_outputs")


def test_count_payload_allocations_budgets_sud_temp_handles() -> None:
    class _Stream:
        def __init__(self, lines):
            self.lines = lines

    stream = _Stream(
        [
            "add r1: r0, r2 | 0",
            "sud r3: r1, r2 | 0",
            "sud r4: r3, r2 | 0",
            "neg r5: r4 | 0",
        ]
    )
    bci_configs = {
        1: PayloadBciConfig(
            line_crc=1,
            bcu_id=0,
            source_base_ids=[0],
            dest_base_ids=[1, 2],
            factors=[1, 1],
        )
    }

    # add(1) + sud(2) + sud(2) + neg(1) + bci dest bases(2)
    assert _count_payload_allocations([stream], bci_configs=bci_configs) == 8


def test_remap_base_conv_active_entries_preserves_line_crc_state() -> None:
    class _Context:
        coeff_count = 4
        rns_moduli = [17, 19, 23]

    line_crc = 0x12345678
    bci_configs = {
        line_crc: PayloadBciConfig(
            line_crc=line_crc,
            bcu_id=0,
            source_base_ids=[0, 1],
            dest_base_ids=[2],
            factors=[1, 1],
        )
    }
    base_term = {
        "x(0)": {
            "rns_base_id": 0,
            "is_ntt_form": False,
            "coeffs": [1, 2, 3, 4],
        }
    }

    old_control_words, _ = _build_payload_partition_buffers(
        context=_Context(),
        register_file_size=8,
        materialized_memory=base_term,
        output_descriptors={},
        instruction_streams=[],
        known_terms=["x(0)"],
        bci_configs=bci_configs,
    )
    old_layout = _parse_payload_control_layout(old_control_words)
    old_extra = _parse_payload_extra_layout(old_control_words, old_layout)
    assert old_extra is not None

    # Simulate the state written back by the base-conv kernel after a BCI op.
    old_control_words = list(old_control_words)
    old_control_words[old_extra.active_offset] = line_crc

    new_control_words, _ = _build_payload_partition_buffers(
        context=_Context(),
        register_file_size=8,
        materialized_memory={
            **base_term,
            "x(1)": {
                "rns_base_id": 1,
                "is_ntt_form": False,
                "coeffs": [5, 6, 7, 8],
            },
        },
        output_descriptors={},
        instruction_streams=[],
        known_terms=["x(0)", "x(1)", "x(2)"],
        bci_configs=bci_configs,
    )
    new_layout = _parse_payload_control_layout(new_control_words)
    new_extra = _parse_payload_extra_layout(new_control_words, new_layout)
    assert new_extra is not None

    remapped = _remap_payload_base_conv_active_entries(
        old_control_words=old_control_words,
        new_control_words=new_control_words,
    )
    assert remapped[new_extra.active_offset] == line_crc
