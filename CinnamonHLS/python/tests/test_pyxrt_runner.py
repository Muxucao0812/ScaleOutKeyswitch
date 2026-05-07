from __future__ import annotations

import os
import pathlib

import pytest

from cinnamon_fpga.pyxrt_runner import _assert_xclbin_is_current


def _set_mtime_ns(path: pathlib.Path, mtime_ns: int) -> None:
    os.utime(path, ns=(mtime_ns, mtime_ns))


def test_assert_xclbin_is_current_accepts_newer_xclbin(tmp_path: pathlib.Path) -> None:
    xclbin = tmp_path / "cinnamon_fpga.hw.xclbin"
    xo = tmp_path / "cinnamon_base_conv.xo"
    xclbin.write_bytes(b"xclbin")
    xo.write_bytes(b"xo")

    _set_mtime_ns(xo, 1_000_000_000)
    _set_mtime_ns(xclbin, 2_000_000_000)

    _assert_xclbin_is_current(xclbin)


def test_assert_xclbin_is_current_rejects_stale_xclbin(tmp_path: pathlib.Path) -> None:
    xclbin = tmp_path / "cinnamon_fpga.hw.xclbin"
    old_xo = tmp_path / "cinnamon_memory.xo"
    new_xo = tmp_path / "cinnamon_base_conv.xo"
    xclbin.write_bytes(b"xclbin")
    old_xo.write_bytes(b"old")
    new_xo.write_bytes(b"new")

    _set_mtime_ns(old_xo, 1_000_000_000)
    _set_mtime_ns(xclbin, 2_000_000_000)
    _set_mtime_ns(new_xo, 3_000_000_000)

    with pytest.raises(RuntimeError, match="stale relative to sibling xo artifacts"):
        _assert_xclbin_is_current(xclbin)
