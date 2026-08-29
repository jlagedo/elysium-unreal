"""The .vpk reader: tail-only directory indexing and per-thread pack handle reuse.

Every pack here is synthesised in-code -- data blobs at the front, the entry table and
the observed n-5 footer at the tail -- matching the layout `vpk.py` documents. Nothing
depends on the user's game install.
"""
import contextlib
import io
import os
import struct
import tempfile
import unittest
from unittest import mock

import pytest

from elysium_pipeline.formats import vpk


def pack_bytes(files, footer=None):
    """One synthetic pack: concatenated payloads, the entry table, then a footer whose
    u32 at n-5 points at the directory start (the observed retail layout)."""
    payload = b"".join(files.values())
    directory = bytearray()
    at = 0
    for name, blob in files.items():
        encoded = name.encode("ascii")
        directory += struct.pack("<I", len(encoded)) + encoded
        directory += struct.pack("<II", at, len(blob))
        at += len(blob)
    if footer is None:
        footer = struct.pack("<I", len(payload)) + b"\x00"
    return payload + bytes(directory) + footer


def reference_index(path):
    """The whole-file oracle: reads the complete pack into memory and parses the
    directory off that one buffer. The tail read must match it byte for byte."""
    with open(path, "rb") as f:
        data = f.read()
    n = len(data)
    pos = None
    for ptr in (n - 5, n - 4, n - 8, n - 9):
        if ptr < 0:
            continue
        cand = struct.unpack_from("<I", data, ptr)[0]
        if 0 < cand < n - 8:
            name_len = struct.unpack_from("<I", data, cand)[0]
            if (1 <= name_len <= 256
                    and all(32 <= c < 127 for c in data[cand + 4:cand + 4 + name_len])):
                pos = cand
                break
    if pos is None:
        raise ValueError("could not locate VPK directory")
    entries = {}
    while pos < n - 8:
        name_len = struct.unpack_from("<I", data, pos)[0]
        if name_len == 0 or name_len > 256:
            break
        name = data[pos + 4: pos + 4 + name_len]
        if not all(32 <= c < 127 for c in name):
            break
        pos += 4 + name_len
        offset, size = struct.unpack_from("<II", data, pos)
        pos += 8
        entries[name.decode("ascii").lower().replace("\\", "/")] = (path, offset, size)
    return entries


class _CountingFile:
    """A read-binary file whose reads and opens total into a shared counter."""

    def __init__(self, f, counter):
        self._f = f
        self._counter = counter
        counter["opens"] += 1

    def read(self, *args):
        data = self._f.read(*args)
        self._counter["bytes"] += len(data)
        return data

    def seek(self, *args):
        return self._f.seek(*args)

    @property
    def closed(self):
        return self._f.closed

    def close(self):
        self._f.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self._f.close()


def counting_open(counter):
    """A stand-in for the module-global `open` in `vpk` that wraps every handle."""
    return lambda path, mode="rb": _CountingFile(open(path, mode), counter)


class TailIndex(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = self._tmp.name

    def tearDown(self):
        vpk.close_handles()
        self._tmp.cleanup()

    def _write(self, name, blob):
        path = os.path.join(self.dir, name)
        with open(path, "wb") as f:
            f.write(blob)
        return path

    def test_index_matches_the_full_read_oracle(self):
        files = {
            "materials/Brick01.vmt": b"vmt one",
            "models\\Weapons\\Knife.mdl": b"knife bytes here",
            "maps/sm_hub_1.bsp": b"z" * 900,
        }
        path = self._write("pack000.vpk", pack_bytes(files))
        idx = vpk.index_vpk(path)
        assert idx == reference_index(path)
        assert idx["materials/brick01.vmt"] == (path, 0, 7)
        assert idx["models/weapons/knife.mdl"] == (path, 7, 16)
        assert idx["maps/sm_hub_1.bsp"] == (path, 23, 900)

    def test_a_directory_wider_than_the_footer_window_still_indexes(self):
        files = {f"materials/generated/texture_{i:05d}.vtf": struct.pack("<I", i)
                 for i in range(3000)}
        path = self._write("pack000.vpk", pack_bytes(files))
        idx = vpk.index_vpk(path)
        assert len(idx) == 3000
        assert idx == reference_index(path)

    def test_a_pointer_at_the_deepest_probe_position_resolves(self):
        files = {"materials/one.vmt": b"x" * 512}
        footer = struct.pack("<I", 512) + b"\x00" * 5   # pointer lands on the n-9 probe
        path = self._write("pack000.vpk", pack_bytes(files, footer=footer))
        idx = vpk.index_vpk(path)
        assert idx == reference_index(path)
        assert idx["materials/one.vmt"] == (path, 0, 512)

    def test_an_unindexable_pack_raises_without_a_tail_warning(self):
        path = self._write("pack000.vpk", b"\x00" * 32)
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            with pytest.raises(ValueError):
                vpk.index_vpk(path)
        with pytest.raises(ValueError):
            reference_index(path)
        assert stderr.getvalue() == ""

    def test_index_all_skips_the_incompatible_pack(self):
        good = self._write("pack000.vpk", pack_bytes({"scripts/a.txt": b"aaa"}))
        self._write("pack001.vpk", b"\x00" * 32)
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            merged = vpk.index_all(self.dir, verbose=True)
        assert merged == {"scripts/a.txt": (good, 0, 3)}
        assert "skip pack001.vpk" in stdout.getvalue()


class HandleReuse(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = self._tmp.name

    def tearDown(self):
        vpk.close_handles()
        self._tmp.cleanup()

    def _pack(self, name, files):
        path = os.path.join(self.dir, name)
        with open(path, "wb") as f:
            f.write(pack_bytes(files))
        return path

    def test_extract_reuses_one_handle_per_pack(self):
        files = {"a.txt": b"alpha", "b.txt": b"bravo", "c.txt": b"charlie"}
        path = self._pack("pack000.vpk", files)
        idx = vpk.index_vpk(path)
        counter = {"bytes": 0, "opens": 0}
        with mock.patch.object(vpk, "open", counting_open(counter), create=True):
            assert vpk.extract(idx["a.txt"]) == b"alpha"
            assert vpk.extract(idx["c.txt"]) == b"charlie"
            assert vpk.extract(idx["b.txt"]) == b"bravo"
            assert counter["opens"] == 1
            vpk.close_handles()
            assert vpk.extract(idx["b.txt"]) == b"bravo"
            assert counter["opens"] == 2

    def test_extract_still_raises_for_a_missing_pack(self):
        with pytest.raises(FileNotFoundError):
            vpk.extract((os.path.join(self.dir, "pack999.vpk"), 0, 4))
