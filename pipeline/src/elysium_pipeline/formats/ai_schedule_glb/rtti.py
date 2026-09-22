"""MSVC RTTI, enough of it to answer "which classes derive from `CAI_BaseNPC`, and what is in
their vtables".

The seam needs this for exactly one thing, and it is not a convenience: four pairs of classes SHARE
one `CAI_LocalIdSpace`, and the second of each pair has no init body at all. Nothing in the init
bodies says so. What says so is slot 580 -- `GetClassScheduleIdSpace`, a one-line
`MOV EAX, imm32 / RET` -- answering the same address from two different vtables. Reading the
vtables is therefore the only way to publish the second class of each pair without typing it, and a
class this seam cannot place is a class whose spawned NPC would find no schedules.

The walk is the one `research/tooling/probes/weapon_activity_survey.py` uses for
`CBaseCombatWeapon`, generalised to any base and moved here because the pipeline never imports from
`research/`. Its shape, for a reader who has not met MSVC's layout:

    type descriptor        `.?AVCNPC_VBrujah@@`, a decorated name with an 8-byte header
    complete object locator   {signature, offset, cdOffset, typeDescriptor, classHierarchy}
    class hierarchy        {..., baseCount, baseArray}
    vtable                 the pointer ARRAY that sits immediately after a pointer to the locator

So the walk runs backwards: find the name, find what points at it 12 bytes in (the locator), read
its hierarchy for the base list, then find what points at the locator and take the four bytes after
it as the vtable's first method.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
import struct
from typing import Iterator

from elysium_pipeline.formats.ai_schedule_glb.image import PEImage

#: A decorated MSVC type name: `.?AV<name>@@` for a class, `.?AU<name>@@` for a struct.
_TYPE_DESCRIPTOR = re.compile(rb"\.\?A[UV][A-Za-z0-9_?$@]+@@\x00")

#: `CAI_BaseNPC::GetClassScheduleIdSpace`. The oracle's slot number; the offset is four bytes a slot.
SCHEDULE_ID_SPACE_SLOT = 580


class RttiError(ValueError):
    """The image's RTTI does not have the shape this walk reads."""


@dataclass(frozen=True, slots=True)
class RttiClass:
    """One class, its base chain and the vtable the walk found for it."""

    name: str
    bases: tuple[str, ...]
    vtable_va: int

    def slot(self, image: PEImage, index: int) -> int | None:
        """The method at `index`, or None where the vtable is shorter than that."""

        return image.read_u32_va(self.vtable_va + index * 4)


def _find_all(data: bytes, needle: bytes) -> Iterator[int]:
    start = 0
    while True:
        position = data.find(needle, start)
        if position < 0:
            return
        yield position
        start = position + 1


def _undecorate(name: str) -> str:
    return name[4:-2] if name.startswith(".?A") and name.endswith("@@") else name


def _bases(image: PEImage, hierarchy_va: int) -> tuple[str, ...]:
    count = image.read_u32_va(hierarchy_va + 8)
    array_va = image.read_u32_va(hierarchy_va + 12)
    if count is None or array_va is None or not 0 < count <= 256:
        return ()
    names: list[str] = []
    for index in range(count):
        descriptor_va = image.read_u32_va(array_va + index * 4)
        type_va = image.read_u32_va(descriptor_va) if descriptor_va else None
        name = image.read_cstring_va(type_va + 8) if type_va else ""
        if not name:
            return ()
        names.append(_undecorate(name))
    return tuple(names)


def classes_deriving_from(image: PEImage, base_name: str) -> list[RttiClass]:
    """Every class whose RTTI base chain names `base_name`, itself included.

    Sorted by name, and deduplicated: MSVC emits one type descriptor per class but a class with
    multiple inheritance carries several locators, and only the primary vtable is wanted.
    """

    # An image with no read-only data section carries no RTTI, and so names no classes. That is
    # an answer, not a failure: the synthetic images the tests build have a `.text` and a `.data`
    # and nothing else, and refusing them here would make this walk a reason the seam needs a real
    # DLL to be testable at all.
    if not image.has_section(".rdata") or not image.has_section(".text"):
        return []

    rdata_section = image.section(".rdata")
    rdata_offset, rdata = image.section_bytes(".rdata")
    text_section = image.section(".text")
    text_start = text_section["raw_offset"]
    text_end = text_start + text_section["raw_size"]

    found: dict[str, RttiClass] = {}
    for match in _TYPE_DESCRIPTOR.finditer(image.data):
        type_va = image.offset_to_va(match.start() - 8)
        if type_va is None:
            continue
        name = _undecorate(match.group(0)[:-1].decode("ascii"))
        if name in found:
            continue
        for type_ref in _find_all(rdata, struct.pack("<I", type_va)):
            if type_ref < 12:
                continue
            locator_offset = rdata_offset + type_ref - 12
            signature, object_offset, cd_offset = struct.unpack_from(
                "<III", image.data, locator_offset
            )
            # The primary locator only: signature 0 is 32-bit RTTI, and a non-zero object offset is
            # a secondary base's vtable rather than the class's own.
            if signature != 0 or object_offset != 0 or cd_offset >= 0x1000:
                continue
            locator_va = image.offset_to_va(locator_offset)
            if locator_va is None:
                continue
            hierarchy_va = image.read_u32_va(locator_va + 16)
            bases = _bases(image, hierarchy_va) if hierarchy_va else ()
            if base_name not in bases:
                continue
            for locator_ref in _find_all(rdata, struct.pack("<I", locator_va)):
                pointer_offset = rdata_offset + locator_ref + 4
                if pointer_offset + 4 > rdata_offset + rdata_section["raw_size"]:
                    continue
                first_method = struct.unpack_from("<I", image.data, pointer_offset)[0]
                method_offset = image.va_to_offset(first_method)
                if method_offset is None or not text_start <= method_offset < text_end:
                    continue
                vtable_va = image.offset_to_va(pointer_offset)
                if vtable_va is None:
                    continue
                found[name] = RttiClass(name=name, bases=bases, vtable_va=int(vtable_va))
                break
            if name in found:
                break
    return [found[name] for name in sorted(found)]


__all__ = ["RttiClass", "RttiError", "SCHEDULE_ID_SPACE_SLOT", "classes_deriving_from"]
