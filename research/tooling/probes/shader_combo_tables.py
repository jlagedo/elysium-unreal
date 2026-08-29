"""Read `stdshader_dx8.dll`'s combo name tables straight out of the shipped binary.

The decompiled selector functions index pointer tables by material flag bits. Ghidra prints the
table's symbol but not its contents, and the contents are the answer: two entries of the
LightmappedGeneric table collapse `BASEALPHAENVMAPMASK` onto the plain masked combo, which is a
precedence rule no filename convention states.

Usage:
    uv run python research/tooling/probes/shader_combo_tables.py <va> [count]
    uv run python research/tooling/probes/shader_combo_tables.py --strings <substring>
"""

from __future__ import annotations

import os
from pathlib import Path
import struct
import sys

#: Source material flags, from the bit positions the selectors test.
MATERIAL_FLAGS = {
    4: "VERTEXCOLOR",
    5: "VERTEXALPHA",
    6: "SELFILLUM",
    7: "ADDITIVE",
    8: "ALPHATEST",
    11: "MODEL",
    13: "NOCULL",
    16: "DECAL",
    17: "ENVMAPSPHERE",
    19: "ENVMAPCAMERASPACE",
    20: "BASEALPHAENVMAPMASK",
    21: "TRANSLUCENT",
    22: "NORMALMAPALPHAENVMAPMASK",
}


def _default_dll() -> Path:
    root = os.environ.get("ELYSIUM_VTMB_ROOT")
    if not root:
        raise SystemExit("ELYSIUM_VTMB_ROOT is not set")
    return Path(root) / "Bin" / "stdshader_dx8.dll"


class Image:
    """The minimum PE reader this probe needs: virtual address to file offset."""

    def __init__(self, path: Path):
        self.data = path.read_bytes()
        header = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[header:header + 4] != b"PE\0\0":
            raise SystemExit(f"{path} is not a PE image")
        sections, = struct.unpack_from("<H", self.data, header + 6)
        optional, = struct.unpack_from("<H", self.data, header + 20)
        self.base, = struct.unpack_from("<I", self.data, header + 24 + 28)
        self.sections = []
        for index in range(sections):
            offset = header + 24 + optional + index * 40
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from(
                "<IIII", self.data, offset + 8
            )
            self.sections.append(
                (virtual_address, max(virtual_size, raw_size), raw_pointer)
            )

    def offset(self, va: int) -> int | None:
        relative = va - self.base
        for virtual_address, size, raw_pointer in self.sections:
            if virtual_address <= relative < virtual_address + size:
                return raw_pointer + (relative - virtual_address)
        return None

    def dword(self, va: int) -> int:
        offset = self.offset(va)
        if offset is None:
            raise SystemExit(f"{va:08x} is outside the image")
        return struct.unpack_from("<I", self.data, offset)[0]

    def string(self, va: int) -> str | None:
        offset = self.offset(va)
        if offset is None:
            return None
        end = self.data.index(b"\0", offset)
        return self.data[offset:end].decode("latin-1")


def describe_index(index: int, bits: list[int]) -> str:
    """Name the flag combination one table index stands for, given the tested bit order."""

    names = [MATERIAL_FLAGS.get(bit, f"bit{bit}") for position, bit in enumerate(bits)
             if index >> position & 1]
    return "+".join(names) or "(none)"


def dump_table(image: Image, va: int, count: int, bits: list[int] | None = None) -> None:
    for index in range(count):
        target = image.dword(va + index * 4)
        label = describe_index(index, bits) if bits else str(index)
        print(f"  [{index}] {label:<48} -> {image.string(target)}")


def main(argv: list[str]) -> int:
    image = Image(_default_dll())
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "--strings":
        needle = argv[1].lower().encode("latin-1")
        for virtual_address, size, raw_pointer in image.sections:
            window = image.data[raw_pointer:raw_pointer + size]
            start = 0
            while True:
                found = window.lower().find(needle, start)
                if found < 0:
                    break
                begin = window.rfind(b"\0", 0, found) + 1
                end = window.find(b"\0", found)
                text = window[begin:end].decode("latin-1")
                if text.isprintable():
                    print(f"  {image.base + virtual_address + begin:08x}  {text}")
                start = end + 1
        return 0
    va = int(argv[0], 16)
    count = int(argv[1]) if len(argv) > 1 else 8
    bits = [int(bit) for bit in argv[2].split(",")] if len(argv) > 2 else None
    dump_table(image, va, count, bits)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
