import pytest

from cinnamon_fpga.api import Emulator, _resolve_board_indices


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
