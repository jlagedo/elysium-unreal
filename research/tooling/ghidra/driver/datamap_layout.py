# -*- coding: utf-8 -*-
"""A class's data layout as its datamaps state it: every saved member, flattened down the base chain.

`ApplyDatamapTypes` replays each class's datamap builder into
`$ELYSIUM_WORK_ROOT/research/ghidra/types/datamap_records-<module>.json`: one record per
`typedescription_t` with its VtMB `fieldType`, offset, count, flags, key and embedded class. The
corpus's `fields` table keeps only a name, an offset and a width Ghidra derived from adjacent
offsets (`undefined4`), so the type a declaration needs is read from the records here.

Two things the records do not state are supplied, and each says so where it is used:

* **Widths.** A `fieldType` has one width in VtMB (`report-<module>.txt`, derived from adjacent
  offsets over every class). An `EMBEDDED` record is its class's extent, bounded by the next
  declared offset; a `CUSTOM` record is `COutputEvent` (0x18, the spacing of every `OUTPUT`
  record) or a `CUtlVector` (its save-restore ops name it), and otherwise extends to the next
  declared offset.
* **Interiors.** `COutputEvent`, `CUtlVector` and `variant_t` have no datamap. Their members
  are Source SDK 2013's, whose spacing matches the retail records; the tier says so.

The type code table is VtMB's own `fieldtype_t` (`docs/vtmb/python_bridge.md` § "Divergence —
VtMB's `fieldtype_t` is not modern Source's"): no `FIELD_QUATERNION`, so every code from 4 up is
one below modern Source's.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path

# VtMB `fieldtype_t` -> (FIELD_ name, Source C++ spelling, width in bytes; None = not fixed).
FIELD_TYPES: dict[str, tuple[str, str, int | None]] = {
    "void": ("FIELD_VOID", "void", None),
    "float": ("FIELD_FLOAT", "float", 4),
    "string": ("FIELD_STRING", "string_t", 4),
    "vector": ("FIELD_VECTOR", "Vector", 12),
    "int": ("FIELD_INTEGER", "int", 4),
    "bool": ("FIELD_BOOLEAN", "bool", 1),
    "short": ("FIELD_SHORT", "short", 2),
    "char": ("FIELD_CHARACTER", "char", 1),
    "color32": ("FIELD_COLOR32", "color32", 4),
    "embedded": ("FIELD_EMBEDDED", "", None),
    "custom": ("FIELD_CUSTOM", "", None),
    "classptr": ("FIELD_CLASSPTR", "CBaseEntity*", 4),
    "ehandle": ("FIELD_EHANDLE", "EHANDLE", 4),
    "edict": ("FIELD_EDICT", "edict_t*", 4),
    "position": ("FIELD_POSITION_VECTOR", "Vector", 12),
    "time": ("FIELD_TIME", "float", 4),
    "modelname": ("FIELD_MODELNAME", "string_t", 4),
    "soundname": ("FIELD_SOUNDNAME", "string_t", 4),
    "input": ("FIELD_INPUT", "inputfunc_t", 4),
    "function": ("FIELD_FUNCTION", "BASEPTR", 4),
}

FIELD_TYPE_CODE_RE = re.compile(r"fieldType (\d+)")


def type_from_code(code: int) -> str:
    """VtMB `fieldtype_t` code -> the records' type name (the enum's order is `FIELD_TYPES`'s)."""
    names = list(FIELD_TYPES)
    return names[code] if 0 <= code < len(names) else ""


OUTPUT_WIDTH = 0x18
# `CBaseEntityOutput` (SDK 2013 `entityoutput.h`): `variant_t m_Value` (a 12-byte union, the
# handle, the type) then the action list head. 0x18 is the spacing of every retail OUTPUT record.
OUTPUT_INTERIOR = ((0x0, "m_Value.<union>", "int", 12), (0xC, "m_Value.eVal", "EHANDLE", 4),
                   (0x10, "m_Value.fieldType", "fieldtype_t", 4),
                   (0x14, "m_ActionList", "CEventAction*", 4))
# `CUtlVector<T>` (SDK 2013 `utlvector.h` over `utlmemory.h`). Retail confirms capacity +4 and
# count +0xc (`docs/vtmb/activity_enum.md`); `m_pElements` is 2013's debugger mirror.
UTLVECTOR_INTERIOR = ((0x0, "m_Memory.m_pMemory", "T*", 4), (0x4, "m_Memory.m_nAllocationCount", "int", 4),
                      (0x8, "m_Memory.m_nGrowSize", "int", 4), (0xC, "m_Size", "int", 4),
                      (0x10, "m_pElements", "T*", 4))
VECTOR_INTERIOR = ((0x0, "x", "float", 4), (0x4, "y", "float", 4), (0x8, "z", "float", 4))

UTLVECTOR_OPS = re.compile(r"_U(\w+?)__V__CUtlMemory|_V(\w+?)__V__CHandle|__CUtlVectorDataOps")


@dataclass
class Member:
    """One member of a flattened layout."""
    off: int
    name: str
    owner: str                 # the class whose datamap declares it
    field_type: str            # VtMB type name (`int`, `time`, `embedded`…)
    count: int
    flags: tuple[str, ...]
    key: str | None
    embedded: str | None
    ops: str | None
    width: int | None = None   # bytes, None where only the next member bounds it
    width_source: str = "type"
    parent: str = ""           # for a member expanded out of an embedded record
    interior: tuple = ()       # SDK sub-layout for a record the datamaps do not describe
    source_type: str = ""

    @property
    def end(self) -> int | None:
        return None if self.width is None else self.off + self.width


@dataclass
class Records:
    by_class: dict[str, dict]
    path: Path

    def base(self, cls: str) -> str | None:
        entry = self.by_class.get(cls)
        return entry.get("base") if entry else None

    def chain(self, cls: str) -> list[str]:
        """`cls` and every base the datamaps state, most-derived first."""
        out: list[str] = []
        while cls and cls in self.by_class and cls not in out:
            out.append(cls)
            cls = self.by_class[cls].get("base")
        return out

    def own(self, cls: str) -> list[dict]:
        rows = []
        seen = set()
        for record in self.by_class.get(cls, {}).get("records", []):
            flags = record.get("flagNames") or []
            if "FUNCTIONTABLE" in flags or ("INPUT" in flags and "SAVE" not in flags):
                continue   # an input handler or think function: a name, not a member
            key = (record["offset"], record["name"])
            if key in seen:
                continue
            seen.add(key)
            rows.append(record)
        return rows

    def extent(self, cls: str) -> int:
        """End of the last member any record in the chain states."""
        members = flatten(self, cls)
        return max((m.off + (m.width or 4) for m in members), default=0)


def load(research_root: Path, module: str) -> Records | None:
    path = research_root / "ghidra" / "types" / f"datamap_records-{module}.json"
    if not path.is_file():
        return None
    return Records(json.loads(path.read_text(encoding="utf-8")), path)


def utlvector_type(ops: str | None) -> str:
    if not ops or "CUtlVectorDataOps" not in ops:
        return ""
    inner = re.search(r"_U(\w+?)__V__CUtlMemory", ops)
    if inner:
        return f"CUtlVector<{inner.group(1)}>"
    handle = re.search(r"_V(\w+?)__V__CHandle", ops)
    if handle:
        return f"CUtlVector<CHandle<{handle.group(1)}>>"
    return "CUtlVector<?>"


def _type_width(records: Records, record: dict, depth: int = 0) -> tuple[int | None, str]:
    name = record["typeName"]
    count = max(int(record.get("count") or 1), 1)
    flags = record.get("flagNames") or []
    if "PTR" in flags:
        return 4, "pointer to a separately allocated object"
    if name == "embedded":
        inner = record.get("embedded")
        if inner and inner in records.by_class and depth < 4:
            ends = [r["offset"] + (_type_width(records, r, depth + 1)[0] or 4)
                    for r in records.own(inner)]
            if ends:
                return max(ends) * count, f"extent of `{inner}`'s records (a lower bound)"
        return None, "embedded class without records"
    if name == "custom":
        if "OUTPUT" in flags:
            return OUTPUT_WIDTH * count, "COutputEvent spacing"
        return None, "custom save-restore ops"
    width = FIELD_TYPES.get(name, ("", "", None))[2]
    return (None if width is None else width * count), "fieldType width"


def _source_type(record: dict) -> str:
    name = record["typeName"]
    flags = record.get("flagNames") or []
    if name == "embedded":
        inner = record.get("embedded") or "?"
        return f"{inner}*" if "PTR" in flags else inner
    if name == "custom":
        if "OUTPUT" in flags:
            return "COutputEvent"
        return utlvector_type(record.get("ops")) or "custom"
    return FIELD_TYPES.get(name, ("", name, None))[1]


def _interior(member: Member) -> tuple:
    if member.field_type == "custom":
        if "OUTPUT" in member.flags:
            return OUTPUT_INTERIOR
        if utlvector_type(member.ops):
            return UTLVECTOR_INTERIOR
    if member.field_type in ("vector", "position"):
        return VECTOR_INTERIOR
    return ()


def flatten(records: Records, cls: str, expand: bool = True) -> list[Member]:
    """Every member of `cls`, bases first, embedded records expanded in place (their own members
    carry `parent`). Widths a type does not fix are bounded by the next member's offset."""
    members: list[Member] = []
    for owner in reversed(records.chain(cls)):
        for record in records.own(owner):
            width, why = _type_width(records, record)
            member = Member(record["offset"], record["name"], owner, record["typeName"],
                            max(int(record.get("count") or 1), 1),
                            tuple(record.get("flagNames") or ()), record.get("external"),
                            record.get("embedded"), record.get("ops"), width, why)
            member.source_type = _source_type(record)
            member.interior = _interior(member)
            members.append(member)
            if expand and record["typeName"] == "embedded" and "PTR" not in member.flags:
                members.extend(_expand(records, member, 1))
    members.sort(key=lambda m: (m.off, m.parent != "", m.name))
    # Bound the open widths by the next top-level member.
    tops = sorted({m.off for m in members if not m.parent})
    for m in members:
        if m.width is None and not m.parent:
            later = [o for o in tops if o > m.off]
            if later:
                m.width = later[0] - m.off
                m.width_source += "; bounded by the next member"
    return members


def _expand(records: Records, outer: Member, depth: int) -> list[Member]:
    inner_cls = outer.embedded
    if not inner_cls or inner_cls not in records.by_class or depth > 4:
        return []
    out: list[Member] = []
    stride = (outer.width // outer.count) if outer.width else None
    for index in range(outer.count if stride else 1):
        base = outer.off + index * (stride or 0)
        prefix = f"{outer.name}[{index}]" if outer.count > 1 else outer.name
        for record in records.own(inner_cls):
            width, why = _type_width(records, record, depth)
            name = record["name"]
            member = Member(base + record["offset"], f"{prefix}.{name}", outer.owner,
                            record["typeName"], max(int(record.get("count") or 1), 1),
                            tuple(record.get("flagNames") or ()), record.get("external"),
                            record.get("embedded"), record.get("ops"), width, why,
                            parent=prefix)
            member.source_type = _source_type(record)
            member.interior = _interior(member)
            out.append(member)
            if record["typeName"] == "embedded" and "PTR" not in member.flags:
                out.extend(_expand(records, member, depth + 1))
    return out


def covering(members: list[Member], off: int) -> list[Member]:
    """Members whose extent contains `off`, outermost first."""
    hits = [m for m in members if m.width and m.off <= off < m.off + m.width]
    return sorted(hits, key=lambda m: (m.parent.count(".") if m.parent else -1, m.parent != "", -(m.width or 0)))


def interior_name(member: Member, off: int) -> tuple[str, str, str] | None:
    """(name, type, tier) for an offset inside a member the datamaps do not describe inside."""
    delta = off - member.off
    stride = (member.width // member.count) if member.width and member.count > 1 else None
    if stride:
        index, delta = divmod(delta, stride)
        prefix = f"{member.name}[{index}]"
    else:
        prefix = member.name
    if delta == 0 and stride:
        return prefix, member.source_type, "datamap array element"
    for sub_off, sub_name, sub_type, sub_width in member.interior:
        if sub_off <= delta < sub_off + sub_width:
            suffix = "" if delta == sub_off else f"+0x{delta - sub_off:x}"
            tier = "SDK 2013 interior" if member.interior is not VECTOR_INTERIOR else "Vector component"
            return f"{prefix}.{sub_name}{suffix}", sub_type, tier
    if stride:
        return f"{prefix}+0x{delta:x}", member.source_type, "inside an array element"
    return None
