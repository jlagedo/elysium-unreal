"""A read-only PE32 mapper, and the two primitives every recovery in this seam is built on.

The seam's source is code, so the decode starts where no other seam's does: at the section table.
`PEImage` is lifted from `research/tooling/probes/weapon_activity_survey.py`, which has carried the
same mapper for every image survey in the tree -- the pipeline never imports from `research/`, so
it is copied rather than shared, and the two are expected to stay identical.

Two primitives sit on top of it. `call_target` resolves a `CALL rel32`, which is how a call site is
attributed to the body it reaches. `thunks_to` enumerates the `JMP rel32` stubs that stand in front
of a body, which is what stops an anchor from being spelled as one address: the image reaches
`CAI_LocalIdSpace::Register` through at least two different thunks, and a decoder anchored on one of
them reports a class as registering 57 names when it registers 68.
"""

from __future__ import annotations

import struct
from typing import Iterator

#: The pinned retail `Vampire/dlls/vampire.dll` every recovered address in this seam was read from.
#: A different digest is an anomaly row, never a refusal: a seam that crashed on an unexpected image
#: would tell an owner nothing about which image they have.
PINNED_SHA256 = "c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f"

#: The pinned image's length, published beside the digest for the same reason.
PINNED_BYTE_LENGTH = 7_860_281

#: The preferred load address; every address in this seam and in `docs/vtmb/` is a VA at this base.
IMAGE_BASE = 0x10000000

_CALL_REL32 = 0xE8
_JMP_REL32 = 0xE9


class ImageError(ValueError):
    """The image is not the shape this seam can read."""


class PEImage:
    """A read-only PE32 mapper sufficient for the pinned retail DLL."""

    __slots__ = ("data", "image_base", "sections")

    def __init__(self, data: bytes) -> None:
        self.data = data
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise ImageError("not a PE image")
        section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
        optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
        optional = pe_offset + 24
        magic = struct.unpack_from("<H", data, optional)[0]
        if magic != 0x10B:
            raise ImageError("the ai-schedule seam reads a PE32 image; this one is not")
        self.image_base = struct.unpack_from("<I", data, optional + 28)[0]
        section_table = optional + optional_size
        self.sections: list[dict] = []
        for index in range(section_count):
            offset = section_table + index * 40
            name = data[offset:offset + 8].split(b"\0", 1)[0].decode("ascii")
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
            self.sections.append(
                {
                    "name": name,
                    "rva": rva,
                    "size": max(virtual_size, raw_size),
                    "raw_size": raw_size,
                    "raw_offset": raw_offset,
                }
            )

    def section(self, name: str) -> dict:
        for section in self.sections:
            if section["name"] == name:
                return section
        raise ImageError(f"PE section {name} is absent")

    def has_section(self, name: str) -> bool:
        return any(section["name"] == name for section in self.sections)

    def va_to_offset(self, va: int) -> int | None:
        rva = int(va) - self.image_base
        for section in self.sections:
            if section["rva"] <= rva < section["rva"] + section["size"]:
                return section["raw_offset"] + rva - section["rva"]
        return None

    def offset_to_va(self, offset: int) -> int | None:
        for section in self.sections:
            start = section["raw_offset"]
            if start <= offset < start + section["raw_size"]:
                return self.image_base + section["rva"] + offset - start
        return None

    def section_of_va(self, va: int) -> str | None:
        rva = int(va) - self.image_base
        for section in self.sections:
            if section["rva"] <= rva < section["rva"] + section["size"]:
                return str(section["name"])
        return None

    def read_u8_va(self, va: int) -> int | None:
        offset = self.va_to_offset(va)
        if offset is None or offset >= len(self.data):
            return None
        return self.data[offset]

    def read_u32_va(self, va: int) -> int | None:
        offset = self.va_to_offset(va)
        if offset is None or offset + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, offset)[0]

    def read_cstring_va(self, va: int) -> str:
        """The NUL-terminated ASCII at a VA, or `""` when the VA is unmapped or the run is not ASCII.

        The empty answer is deliberately indistinguishable from an empty string: every caller in
        this seam treats "no name here" and "an empty name" the same way, which is to refuse.
        """

        offset = self.va_to_offset(va)
        if offset is None:
            return ""
        end = self.data.find(b"\0", offset)
        if end < 0:
            return ""
        try:
            return self.data[offset:end].decode("ascii")
        except UnicodeDecodeError:
            return ""

    def section_bytes(self, name: str) -> tuple[int, bytes]:
        section = self.section(name)
        start = section["raw_offset"]
        return start, self.data[start:start + section["raw_size"]]


def call_target(image: PEImage, instruction_offset: int) -> int | None:
    """The VA a `CALL rel32` at this file offset reaches, or None when it is not one."""

    if instruction_offset < 0 or instruction_offset + 5 > len(image.data):
        return None
    if image.data[instruction_offset] != _CALL_REL32:
        return None
    instruction_va = image.offset_to_va(instruction_offset)
    if instruction_va is None:
        return None
    displacement = struct.unpack_from("<i", image.data, instruction_offset + 1)[0]
    return (instruction_va + 5 + displacement) & 0xFFFFFFFF


def follow_jump(image: PEImage, va: int) -> int:
    """One `JMP rel32` hop, or the address itself when it is not a thunk.

    One hop only: MSVC's incremental-link thunks are one deep, and a loop over an image that
    happened to chain them would be a decoder bug hiding as a feature.
    """

    offset = image.va_to_offset(va)
    if offset is None or offset + 5 > len(image.data):
        return va
    if image.data[offset] != _JMP_REL32:
        return va
    displacement = struct.unpack_from("<i", image.data, offset + 1)[0]
    return (va + 5 + displacement) & 0xFFFFFFFF


def thunks_to(image: PEImage, target: int) -> list[int]:
    """Every `JMP rel32` stub in `.text` that jumps to `target`, lowest VA first."""

    start, text = image.section_bytes(".text")
    found: list[int] = []
    for relative in range(0, len(text) - 5):
        if text[relative] != _JMP_REL32:
            continue
        va = image.offset_to_va(start + relative)
        if va is None:
            continue
        displacement = struct.unpack_from("<i", text, relative + 1)[0]
        if ((va + 5 + displacement) & 0xFFFFFFFF) == int(target):
            found.append(va)
    return sorted(found)


def entry_points(image: PEImage, target: int) -> frozenset[int]:
    """A body and every thunk that stands in front of it, as one set to match call targets against.

    Anchoring on a set rather than an address is the whole point: `0x102cadd0` registers
    `CAI_BaseNPC`'s schedule names through two different stubs, and a decoder that knew only the
    first reported 57 of the 68 names and left the rest looking unregistered.
    """

    body = follow_jump(image, int(target))
    return frozenset({int(target), body, *thunks_to(image, body)})


def iter_call_sites(image: PEImage, targets: frozenset[int]) -> Iterator[tuple[int, int]]:
    """Every `(call site VA, resolved target VA)` in `.text` whose target is in `targets`.

    One linear pass serves every anchor at once, because the image is 7.8 MB and the seam has a
    dozen anchors; scanning per anchor would read `.text` a dozen times for the same answer.
    """

    start, text = image.section_bytes(".text")
    for relative in range(0, len(text) - 5):
        if text[relative] != _CALL_REL32:
            continue
        offset = start + relative
        target = call_target(image, offset)
        if target is None:
            continue
        if target in targets:
            site = image.offset_to_va(offset)
            if site is not None:
                yield site, target
            continue
        hopped = follow_jump(image, target)
        if hopped in targets:
            site = image.offset_to_va(offset)
            if site is not None:
                yield site, hopped
