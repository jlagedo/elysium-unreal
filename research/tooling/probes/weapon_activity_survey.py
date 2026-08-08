# -*- coding: utf-8 -*-
"""Decode every retail weapon activity-translation table and current-map use.

The pinned 32-bit ``vampire.dll`` exposes a weapon's flattened ``acttable_t``
through vtable slots ``+0x5a8`` (pointer) and ``+0x5ac`` (count).  This probe
walks MSVC RTTI to find every ``CBaseCombatWeapon`` subclass, follows the slot
thunks, decodes the ordered 12-byte ``{base, weapon, required}`` rows, recovers
entity class aliases from the constructors, and joins the result to authored
``additionalequipment`` / ``alternateequipment`` fields in the exported maps.

The table order is retained because VtMB's ``ActivityOverride`` walks it from
front to back and can continue from an unavailable class-specific row to a
later duplicate-base fallback.  The third dword is exported even though the
pinned server implementation does not read it.

Read-only unless ``--json`` is supplied.  Generated rows are game-derived and
belong below ``$ELYSIUM_WORK_ROOT/research``; never commit the report.

Usage::

    uv run elysium research weapon_activity_survey
    uv run elysium research weapon_activity_survey --json <external-path>
"""
from __future__ import print_function

import argparse
import collections
import glob
import hashlib
import json
import os
import re
import struct

from elysium_pipeline.paths import export_root, vtmb_root


PINNED_SHA256 = "c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f"
IMAGE_BASE = 0x10000000
NORMAL_ACTIVITY_REGISTER_THUNK = 0x1000CA0E
SPECIAL_ACTIVITY_REGISTER_THUNK = 0x10012102
WEAPON_TABLE_SLOT = 0x5A8
WEAPON_COUNT_SLOT = 0x5AC
CONSTRUCTOR_ALIAS_RADIUS = 384
TYPE_DESCRIPTOR_RE = re.compile(rb"\.\?A[UV][A-Za-z0-9_?$@]+@@\x00")


class PEImage(object):
    """Small read-only PE32 mapper sufficient for the pinned retail DLL."""

    def __init__(self, data):
        self.data = data
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise ValueError("not a PE image")
        section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
        optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
        optional = pe_offset + 24
        magic = struct.unpack_from("<H", data, optional)[0]
        if magic != 0x10B:
            raise ValueError("weapon survey requires a PE32 image")
        self.image_base = struct.unpack_from("<I", data, optional + 28)[0]
        section_table = optional + optional_size
        self.sections = []
        for index in range(section_count):
            offset = section_table + index * 40
            name = data[offset:offset + 8].split(b"\0", 1)[0].decode("ascii")
            virtual_size, rva, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, offset + 8)
            self.sections.append({
                "name": name,
                "rva": rva,
                "size": max(virtual_size, raw_size),
                "raw_size": raw_size,
                "raw_offset": raw_offset,
            })

    def section(self, name):
        for section in self.sections:
            if section["name"] == name:
                return section
        raise ValueError("PE section %s is absent" % name)

    def va_to_offset(self, va):
        rva = int(va) - self.image_base
        for section in self.sections:
            if section["rva"] <= rva < section["rva"] + section["size"]:
                return section["raw_offset"] + rva - section["rva"]
        return None

    def offset_to_va(self, offset):
        for section in self.sections:
            start = section["raw_offset"]
            if start <= offset < start + section["raw_size"]:
                return self.image_base + section["rva"] + offset - start
        return None

    def read_u32_va(self, va):
        offset = self.va_to_offset(va)
        if offset is None or offset + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, offset)[0]

    def read_cstring_va(self, va):
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

    def section_bytes(self, name):
        section = self.section(name)
        start = section["raw_offset"]
        return start, self.data[start:start + section["raw_size"]]


def _call_target(image, instruction_offset):
    instruction_va = image.offset_to_va(instruction_offset)
    if instruction_va is None or image.data[instruction_offset] != 0xE8:
        return None
    displacement = struct.unpack_from("<i", image.data, instruction_offset + 1)[0]
    return instruction_va + 5 + displacement


def decode_activity_registry(image):
    """Recover both ordinary and special global ACT_* registrations."""
    text_offset, text = image.section_bytes(".text")
    registrations = []
    accepted = {
        NORMAL_ACTIVITY_REGISTER_THUNK: "ordinary",
        SPECIAL_ACTIVITY_REGISTER_THUNK: "special",
    }
    for relative in range(10, len(text) - 5):
        offset = text_offset + relative
        if image.data[offset] != 0xE8:
            continue
        target = _call_target(image, offset)
        if target not in accepted or image.data[offset - 5] != 0x68:
            continue
        name_va = struct.unpack_from("<I", image.data, offset - 4)[0]
        name = image.read_cstring_va(name_va)
        if not name.startswith("ACT_"):
            continue
        if image.data[offset - 10] == 0x68:
            activity_id = struct.unpack_from("<I", image.data, offset - 9)[0]
        elif image.data[offset - 7] == 0x6A:
            activity_id = struct.unpack_from("<b", image.data, offset - 6)[0]
        else:
            raise ValueError("unknown activity-registration push at 0x%x" %
                             image.offset_to_va(offset))
        registrations.append({
            "id": activity_id,
            "name": name,
            "kind": accepted[target],
            "call_va": "0x%x" % image.offset_to_va(offset),
        })
    return registrations


def _find_all(data, needle):
    start = 0
    while True:
        position = data.find(needle, start)
        if position < 0:
            return
        yield position
        start = position + 1


def _undecorate_type(name):
    return name[4:-2] if name.startswith(".?A") and name.endswith("@@") else name


def _rtti_bases(image, class_hierarchy_va):
    count = image.read_u32_va(class_hierarchy_va + 8)
    array_va = image.read_u32_va(class_hierarchy_va + 12)
    if count is None or array_va is None or not 0 < count <= 256:
        return []
    bases = []
    for index in range(count):
        descriptor_va = image.read_u32_va(array_va + index * 4)
        type_va = image.read_u32_va(descriptor_va) if descriptor_va else None
        name = image.read_cstring_va(type_va + 8) if type_va else ""
        if not name:
            return []
        bases.append(_undecorate_type(name))
    return bases


def find_weapon_classes(image):
    """Return primary RTTI/vtable records for all CBaseCombatWeapon subclasses."""
    rdata_section = image.section(".rdata")
    rdata_offset, rdata = image.section_bytes(".rdata")
    classes = []
    seen = set()
    for match in TYPE_DESCRIPTOR_RE.finditer(image.data):
        decorated = match.group(0)[:-1].decode("ascii")
        type_va = image.offset_to_va(match.start() - 8)
        if type_va is None:
            continue
        for type_ref in _find_all(rdata, struct.pack("<I", type_va)):
            if type_ref < 12:
                continue
            col_offset = rdata_offset + type_ref - 12
            signature, object_offset, cd_offset = struct.unpack_from(
                "<III", image.data, col_offset)
            if signature != 0 or object_offset != 0 or cd_offset >= 0x1000:
                continue
            col_va = image.offset_to_va(col_offset)
            hierarchy_va = image.read_u32_va(col_va + 16)
            bases = _rtti_bases(image, hierarchy_va) if hierarchy_va else []
            if "CBaseCombatWeapon" not in bases:
                continue
            vtable_va = None
            for col_ref in _find_all(rdata, struct.pack("<I", col_va)):
                pointer_offset = rdata_offset + col_ref + 4
                if pointer_offset + 4 > rdata_offset + rdata_section["raw_size"]:
                    continue
                first_method = struct.unpack_from("<I", image.data, pointer_offset)[0]
                method_offset = image.va_to_offset(first_method)
                if method_offset is not None and image.section(".text")["raw_offset"] <= \
                        method_offset < (image.section(".text")["raw_offset"] +
                                         image.section(".text")["raw_size"]):
                    vtable_va = image.offset_to_va(pointer_offset)
                    break
            name = _undecorate_type(decorated)
            if vtable_va is not None and name not in seen:
                seen.add(name)
                classes.append({
                    "cpp_class": name,
                    "direct_base": bases[1] if len(bases) > 1 else "",
                    "bases": bases,
                    "type_descriptor_va": "0x%x" % type_va,
                    "vtable_va": vtable_va,
                })
            break
    classes.sort(key=lambda row: row["cpp_class"])
    return classes


def follow_jump(image, va):
    """Follow the chain of five-byte relative JMP thunks at ``va``."""
    current = va
    for _ in range(16):
        offset = image.va_to_offset(current)
        if offset is None or image.data[offset] != 0xE9:
            return current
        displacement = struct.unpack_from("<i", image.data, offset + 1)[0]
        current = current + 5 + displacement
    raise ValueError("excessive thunk chain at 0x%x" % va)


def constant_return(image, va):
    """Decode ``xor eax,eax; ret`` or ``mov eax,imm32; ret`` after thunks."""
    body_va = follow_jump(image, va)
    offset = image.va_to_offset(body_va)
    if offset is None:
        return None, body_va
    code = image.data[offset:offset + 8]
    if code.startswith(b"\x33\xc0\xc3"):
        return 0, body_va
    if len(code) >= 6 and code[0] == 0xB8 and code[5] == 0xC3:
        return struct.unpack_from("<I", code, 1)[0], body_va
    return None, body_va


def _constructor_aliases(image, vtable_va):
    text_offset, text = image.section_bytes(".text")
    aliases = set()
    needle = struct.pack("<I", vtable_va)
    for relative in _find_all(text, needle):
        reference = text_offset + relative
        start = max(text_offset, reference - CONSTRUCTOR_ALIAS_RADIUS)
        end = min(text_offset + len(text), reference + CONSTRUCTOR_ALIAS_RADIUS)
        window = image.data[start:end]
        for push in re.finditer(rb"\x68(.{4})", window, re.DOTALL):
            candidate_va = struct.unpack("<I", push.group(1))[0]
            candidate = image.read_cstring_va(candidate_va)
            if candidate.startswith(("item_", "weapon_")):
                aliases.add(candidate)
    return sorted(aliases)


def decode_weapon_tables(image, classes, activity_by_id):
    for row in classes:
        vtable_va = row["vtable_va"]
        table_slot = image.read_u32_va(vtable_va + WEAPON_TABLE_SLOT)
        count_slot = image.read_u32_va(vtable_va + WEAPON_COUNT_SLOT)
        table_va, table_body = constant_return(image, table_slot)
        count, count_body = constant_return(image, count_slot)
        if table_va is None or count is None or count > 4096:
            raise ValueError("unsupported weapon table getter on %s" % row["cpp_class"])
        row.update({
            "vtable_va": "0x%x" % vtable_va,
            "table_getter_va": "0x%x" % table_body,
            "count_getter_va": "0x%x" % count_body,
            "table_va": "0x%x" % table_va if table_va else "0x0",
            "row_count": count,
            "entity_classnames": _constructor_aliases(image, vtable_va),
            "rows": [],
        })
        if not count:
            continue
        table_offset = image.va_to_offset(table_va)
        if table_offset is None or table_offset + count * 12 > len(image.data):
            raise ValueError("weapon table outside image on %s" % row["cpp_class"])
        for ordinal in range(count):
            base_id, weapon_id, required = struct.unpack_from(
                "<III", image.data, table_offset + ordinal * 12)
            row["rows"].append({
                "ordinal": ordinal,
                "base_id": base_id,
                "base_activity": activity_by_id.get(base_id, ""),
                "weapon_id": weapon_id,
                "weapon_activity": activity_by_id.get(weapon_id, ""),
                "required": bool(required & 0xFF),
                "required_raw": required,
            })
    return classes


def _entity_fields(entity):
    fields = {key: value for key, value in entity.items()
              if key not in ("outputs", "keys")}
    if isinstance(entity.get("keys"), dict):
        fields.update(entity["keys"])
    return {str(key).casefold(): value for key, value in fields.items()}


def survey_equipment(root, classes):
    aliases = collections.defaultdict(list)
    by_class = {row["cpp_class"]: row for row in classes}
    for row in classes:
        for alias in row["entity_classnames"]:
            aliases[alias.casefold()].append(row["cpp_class"])

    references = []
    pattern = os.path.join(os.fspath(root), "*", "*.ents")
    for path in sorted(glob.glob(pattern)):
        try:
            with open(path, "r", errors="replace") as handle:
                data = json.load(handle)
        except (OSError, ValueError):
            continue
        map_name = str(data.get("map") or os.path.splitext(os.path.basename(path))[0])
        for entity_index, entity in enumerate(data.get("entities", [])):
            fields = _entity_fields(entity)
            for field in ("additionalequipment", "alternateequipment"):
                value = str(fields.get(field, "")).strip()
                if not value or value == "0":
                    continue
                alias_candidates = aliases.get(value.casefold(), [])
                candidates = [
                    candidate for candidate in alias_candidates
                    if not any(candidate in by_class[other]["bases"][1:]
                               for other in alias_candidates if other != candidate)
                ]
                if not candidates:
                    status = "entity_class_not_recovered"
                elif len(candidates) > 1:
                    status = "ambiguous_entity_class"
                elif by_class[candidates[0]]["row_count"]:
                    status = "resolved_table"
                else:
                    status = "resolved_empty_table"
                references.append({
                    "map": map_name,
                    "entity_index": entity_index,
                    "source_class": str(entity.get("classname", "")),
                    "source_name": str(entity.get("targetname", "")),
                    "field": field,
                    "equipment": value,
                    "cpp_classes": candidates,
                    "discarded_base_aliases": [candidate for candidate in alias_candidates
                                               if candidate not in candidates],
                    "status": status,
                })
    return references


def summarize(registrations, classes, equipment):
    nonempty = [row for row in classes if row["row_count"]]
    unique_blobs = {}
    required_class_rows = collections.Counter()
    unknown_rows = 0
    for row in nonempty:
        for item in row["rows"]:
            required_class_rows["required" if item["required"] else "optional"] += 1
            unknown_rows += int(not item["base_activity"] or not item["weapon_activity"])
        unique_blobs.setdefault((row["table_va"], row["row_count"]), row)

    unique_rows = [item for row in unique_blobs.values() for item in row["rows"]]
    required_unique = collections.Counter(
        "required" if item["required"] else "optional" for item in unique_rows)
    duplicate_base_rows = sum(
        row["row_count"] - len({item["base_id"] for item in row["rows"]})
        for row in unique_blobs.values())
    equipment_status = collections.Counter(row["status"] for row in equipment)
    distinct_status = collections.defaultdict(set)
    for row in equipment:
        distinct_status[row["status"]].add(row["equipment"].casefold())
    registration_kind = collections.Counter(row["kind"] for row in registrations)
    ids = {row["id"] for row in registrations}
    holes = sorted(set(range(min(ids), max(ids) + 1)) - ids) if ids else []
    return {
        "activity_registrations": len(registrations),
        "activity_registration_kinds": dict(registration_kind.most_common()),
        "activity_id_min": min(ids) if ids else None,
        "activity_id_max": max(ids) if ids else None,
        "activity_id_holes": holes,
        "weapon_subclasses": len(classes),
        "nonempty_table_classes": len(nonempty),
        "empty_table_classes": len(classes) - len(nonempty),
        "unique_table_blobs": len(unique_blobs),
        "class_table_rows": sum(row["row_count"] for row in nonempty),
        "unique_table_rows": len(unique_rows),
        "duplicate_base_rows_in_unique_tables": duplicate_base_rows,
        "class_row_required_flags": dict(required_class_rows.most_common()),
        "unique_row_required_flags": dict(required_unique.most_common()),
        "unknown_activity_rows": unknown_rows,
        "equipment_references": len(equipment),
        "distinct_equipment": len({row["equipment"].casefold() for row in equipment}),
        "equipment_status": dict(equipment_status.most_common()),
        "distinct_equipment_status": {
            key: len(value) for key, value in sorted(distinct_status.items())
        },
    }


def build_report(binary_path=None, root=None):
    if binary_path is None:
        binary_path = vtmb_root() / "Vampire" / "dlls" / "vampire.dll"
    if root is None:
        root = export_root()
    binary_path = os.path.abspath(os.fspath(binary_path))
    with open(binary_path, "rb") as handle:
        data = handle.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != PINNED_SHA256:
        raise ValueError("unsupported vampire.dll SHA-256 %s" % digest)
    image = PEImage(data)
    registrations = decode_activity_registry(image)
    activity_by_id = {row["id"]: row["name"] for row in registrations}
    classes = decode_weapon_tables(image, find_weapon_classes(image), activity_by_id)
    equipment = survey_equipment(root, classes)
    report = {
        "binary": {
            "path": binary_path,
            "size": len(data),
            "sha256": digest,
            "image_base": "0x%x" % image.image_base,
        },
        "translator": {
            "address": "0x1024f210",
            "table_slot": "0x%x" % WEAPON_TABLE_SLOT,
            "count_slot": "0x%x" % WEAPON_COUNT_SLOT,
            "row_stride": 12,
            "required_field_read": False,
            "selection": "first matching row whose translated activity is available; later "
                         "duplicate-base rows remain fallbacks; no owner component returns the "
                         "first matching row immediately",
        },
        "activity_registry": registrations,
        "weapon_classes": classes,
        "current_equipment": equipment,
    }
    report["summary"] = summarize(registrations, classes, equipment)
    return report


def print_report(report):
    summary = report["summary"]
    print("=" * 78)
    print("VtMB weapon activity translation survey")
    print("=" * 78)
    print("binary: %s" % report["binary"]["path"])
    print("sha256: %s" % report["binary"]["sha256"])
    print("activity registry: %(activity_registrations)d entries | "
          "IDs %(activity_id_min)d..0x%(activity_id_max)x | %(holes)d holes" % {
              **summary, "holes": len(summary["activity_id_holes"]),
          })
    print("weapon subclasses: %(weapon_subclasses)d | non-empty tables: "
          "%(nonempty_table_classes)d | unique blobs: %(unique_table_blobs)d" % summary)
    print("ordered table rows: %(class_table_rows)d class rows | "
          "%(unique_table_rows)d unique-blob rows" % summary)
    print("required flag is retained as data but not read by server ActivityOverride")
    print()
    print("Current exported equipment demand")
    print("  references: %(equipment_references)d | distinct: %(distinct_equipment)d" % summary)
    for status, count in summary["equipment_status"].items():
        print("  %-38s %5d" % (status, count))
    missing = [row for row in report["current_equipment"]
               if row["status"] not in ("resolved_table", "resolved_empty_table")]
    for row in missing:
        print("  missing: %(map)s %(source_name)s %(field)s=%(equipment)s" % row)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", help="override the pinned retail vampire.dll path")
    parser.add_argument("--root", help="override the exported corpus root")
    parser.add_argument("--json", help="write the complete generated ledger here")
    args = parser.parse_args()
    report = build_report(args.binary, args.root)
    print_report(report)
    if args.json:
        parent = os.path.dirname(os.path.abspath(args.json))
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(args.json, "w") as handle:
            json.dump(report, handle, indent=1, sort_keys=True)
        print("\nwrote %s" % args.json)


if __name__ == "__main__":
    main()
