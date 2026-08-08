# -*- coding: utf-8 -*-
"""Decode the retail NPC activity-translation class surface and current use.

The pinned 32-bit ``vampire.dll`` calls two activity translators through
``CAI_BaseNPC`` vtable slots ``+0x5dc`` and ``+0x5e0``.  Two context-sensitive
branches delegate again through ``+0x8e4`` and ``+0x8e8``.  This probe walks
MSVC RTTI for every ``CAI_BaseNPC`` descendant, follows jump thunks at all four
slots, recovers entity aliases, and joins direct NPCs plus ``npc_maker``
``NPCTypE`` demands from the exported map corpus.

Read-only unless ``--json`` is supplied.  The generated class ledger is
game-derived and belongs below ``$ELYSIUM_WORK_ROOT/research``; never commit it.

Usage::

    uv run elysium research npc_translation_survey
    uv run elysium research npc_translation_survey --json <external-path>
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
from research.tooling.probes.weapon_activity_survey import (
    PEImage,
    PINNED_SHA256,
    TYPE_DESCRIPTOR_RE,
    _find_all,
    _rtti_bases,
    _undecorate_type,
    decode_activity_registry,
    follow_jump,
)


NPC_BASE = "CAI_BaseNPC"
PRE_TRANSLATE_SLOT = 0x5DC
CLASS_TRANSLATE_SLOT = 0x5E0
COVER_ACTIVITY_SLOT = 0x8E4
RELOAD_ACTIVITY_SLOT = 0x8E8
CONSTRUCTOR_ALIAS_RADIUS = 512
ENTITY_PREFIXES = ("npc_", "monster_")
GRAPPLE_VARIANTS = (
    "attacker_short_victim_front",
    "attacker_tall_victim_front",
    "victim_short_attacker_front",
    "victim_tall_attacker_front",
    "attacker_short_victim_back",
    "attacker_tall_victim_back",
    "victim_short_attacker_back",
    "victim_tall_attacker_back",
)


def _primary_vtable(image, complete_object_locator_va):
    """Return the primary vtable following one RTTI complete-object locator."""
    rdata_offset, rdata = image.section_bytes(".rdata")
    rdata_section = image.section(".rdata")
    text_section = image.section(".text")
    for reference in _find_all(rdata, struct.pack("<I", complete_object_locator_va)):
        pointer_offset = rdata_offset + reference + 4
        if pointer_offset + 4 > rdata_offset + rdata_section["raw_size"]:
            continue
        first_method = struct.unpack_from("<I", image.data, pointer_offset)[0]
        method_offset = image.va_to_offset(first_method)
        if method_offset is not None and text_section["raw_offset"] <= method_offset < (
                text_section["raw_offset"] + text_section["raw_size"]):
            return image.offset_to_va(pointer_offset)
    return None


def _constructor_aliases(image, vtable_va):
    """Recover nearby registered entity class strings for one primary vtable."""
    text_offset, text = image.section_bytes(".text")
    aliases = set()
    for relative in _find_all(text, struct.pack("<I", vtable_va)):
        reference = text_offset + relative
        start = max(text_offset, reference - CONSTRUCTOR_ALIAS_RADIUS)
        end = min(text_offset + len(text), reference + CONSTRUCTOR_ALIAS_RADIUS)
        window = image.data[start:end]
        for push in re.finditer(rb"\x68(.{4})", window, re.DOTALL):
            candidate_va = struct.unpack("<I", push.group(1))[0]
            candidate = image.read_cstring_va(candidate_va)
            if candidate.casefold().startswith(ENTITY_PREFIXES):
                aliases.add(candidate)
    return sorted(aliases, key=str.casefold)


def find_npc_classes(image):
    """Return primary RTTI/vtable records for all CAI_BaseNPC descendants."""
    rdata_offset, rdata = image.section_bytes(".rdata")
    classes = []
    seen = set()
    for match in TYPE_DESCRIPTOR_RE.finditer(image.data):
        decorated = match.group(0)[:-1].decode("ascii")
        type_va = image.offset_to_va(match.start() - 8)
        if type_va is None:
            continue
        for type_reference in _find_all(rdata, struct.pack("<I", type_va)):
            if type_reference < 12:
                continue
            locator_offset = rdata_offset + type_reference - 12
            signature, object_offset, cd_offset = struct.unpack_from(
                "<III", image.data, locator_offset)
            if signature != 0 or object_offset != 0 or cd_offset >= 0x1000:
                continue
            locator_va = image.offset_to_va(locator_offset)
            hierarchy_va = image.read_u32_va(locator_va + 16)
            bases = _rtti_bases(image, hierarchy_va) if hierarchy_va else []
            if NPC_BASE not in bases:
                continue
            vtable_va = _primary_vtable(image, locator_va)
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


def decode_translation_slots(image, classes):
    """Attach the four effective virtual bodies and recovered aliases."""
    for row in classes:
        vtable_va = row["vtable_va"]
        functions = {}
        for name, slot in (
                ("pre_translate", PRE_TRANSLATE_SLOT),
                ("class_translate", CLASS_TRANSLATE_SLOT),
                ("cover_activity", COVER_ACTIVITY_SLOT),
                ("reload_activity", RELOAD_ACTIVITY_SLOT)):
            entry = image.read_u32_va(vtable_va + slot)
            if entry is None:
                raise ValueError("missing vtable slot 0x%x on %s" %
                                 (slot, row["cpp_class"]))
            functions[name] = "0x%x" % follow_jump(image, entry)
        row.update({
            "vtable_va": "0x%x" % vtable_va,
            "entity_classnames": _constructor_aliases(image, vtable_va),
            "translation_functions": functions,
        })
    return classes


def _entity_fields(entity):
    fields = {key: value for key, value in entity.items()
              if key not in ("outputs", "keys")}
    if isinstance(entity.get("keys"), dict):
        fields.update(entity["keys"])
    return {str(key).casefold(): value for key, value in fields.items()}


def _alias_index(classes):
    aliases = collections.defaultdict(list)
    by_class = {row["cpp_class"]: row for row in classes}
    for row in classes:
        for alias in row["entity_classnames"]:
            aliases[alias.casefold()].append(row["cpp_class"])
    return aliases, by_class


def _most_derived(candidates, by_class):
    return sorted(
        candidate for candidate in candidates
        if not any(candidate in by_class[other]["bases"][1:]
                   for other in candidates if other != candidate)
    )


def resolve_entity_classname(classname, classes):
    """Resolve a retail entity alias, preferring the most-derived RTTI class."""
    aliases, by_class = _alias_index(classes)
    candidates = _most_derived(aliases.get(classname.casefold(), []), by_class)
    resolution = "constructor_alias"
    if not candidates and classname.casefold().startswith("npc_"):
        suffix = classname[4:]
        canonical = "CNPC_" + suffix
        folded = {name.casefold(): name for name in by_class}
        if canonical.casefold() in folded:
            candidates = [folded[canonical.casefold()]]
            resolution = "canonical_rtti_name"
    if not candidates:
        return [], "entity_class_not_recovered", "none"
    if len(candidates) > 1:
        return candidates, "ambiguous_entity_class", resolution
    return candidates, "resolved", resolution


def survey_current_classes(root, classes):
    """Join direct NPC and npc_maker spawn demands to the RTTI class surface."""
    by_class = {row["cpp_class"]: row for row in classes}
    demands = []
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
            source_class = str(fields.get("classname", "")).strip()
            requested = ""
            demand_kind = ""
            if source_class.casefold() == "npc_maker":
                requested = str(fields.get("npctype", "")).strip()
                demand_kind = "maker_spawn"
            elif source_class.casefold().startswith(ENTITY_PREFIXES):
                requested = source_class
                demand_kind = "map_entity"
            if not requested:
                continue
            candidates, status, resolution = resolve_entity_classname(requested, classes)
            bodies = by_class[candidates[0]]["translation_functions"] if len(candidates) == 1 else {}
            demands.append({
                "map": map_name,
                "entity_index": entity_index,
                "source_class": source_class,
                "source_name": str(fields.get("targetname", "")),
                "demand_kind": demand_kind,
                "requested_class": requested,
                "cpp_classes": candidates,
                "status": status,
                "resolution": resolution,
                "translation_functions": bodies,
            })
    return demands


def build_grapple_families(registrations):
    """Expand every specially registered base into its eight contiguous roles."""
    by_id = {row["id"]: row for row in registrations}
    families = []
    for base in sorted((row for row in registrations if row["kind"] == "special"),
                       key=lambda row: row["id"]):
        variants = []
        for offset, role in enumerate(GRAPPLE_VARIANTS, start=1):
            variant = by_id.get(base["id"] + offset, {})
            variants.append({
                "offset": offset,
                "role": role,
                "id": base["id"] + offset,
                "activity": variant.get("name", ""),
            })
        families.append({
            "base_id": base["id"],
            "base_activity": base["name"],
            "variants": variants,
        })
    return families


def summarize(classes, demands, grapple_families):
    slots = ("pre_translate", "class_translate", "cover_activity", "reload_activity")
    function_counts = {
        slot: len({row["translation_functions"][slot] for row in classes})
        for slot in slots
    }
    status = collections.Counter(row["status"] for row in demands)
    distinct_status = collections.defaultdict(set)
    for row in demands:
        distinct_status[row["status"]].add(row["requested_class"].casefold())
    current_bodies = {
        slot: len({row["translation_functions"].get(slot) for row in demands
                   if row["status"] == "resolved"})
        for slot in slots
    }
    return {
        "npc_subclasses": len(classes),
        "distinct_translation_functions": function_counts,
        "current_demands": len(demands),
        "distinct_current_classnames": len({row["requested_class"].casefold()
                                             for row in demands}),
        "current_status": dict(status.most_common()),
        "distinct_current_status": {
            key: len(value) for key, value in sorted(distinct_status.items())
        },
        "distinct_current_translation_functions": current_bodies,
        "grapple_base_activities": len(grapple_families),
        "grapple_role_variants": sum(len(row["variants"])
                                     for row in grapple_families),
        "unknown_grapple_variant_names": sum(
            int(not variant["activity"])
            for row in grapple_families for variant in row["variants"]),
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
    grapple_families = build_grapple_families(registrations)
    classes = decode_translation_slots(image, find_npc_classes(image))
    demands = survey_current_classes(root, classes)
    report = {
        "binary": {
            "path": binary_path,
            "size": len(data),
            "sha256": digest,
            "image_base": "0x%x" % image.image_base,
        },
        "slots": {
            "pre_translate": "0x%x" % PRE_TRANSLATE_SLOT,
            "class_translate": "0x%x" % CLASS_TRANSLATE_SLOT,
            "cover_activity": "0x%x" % COVER_ACTIVITY_SLOT,
            "reload_activity": "0x%x" % RELOAD_ACTIVITY_SLOT,
        },
        "base_grapple_translator": {
            "early_translate_address": "0x10328030",
            "translator_address": "0x10328380",
            "membership_address": "0x104126a0",
            "selection": "base+1, plus 1 for tall counterpart, plus 2 for victim role, "
                         "plus 4 for back position; unresolved grapple state preserves base",
        },
        "grapple_activity_families": grapple_families,
        "npc_classes": classes,
        "current_class_demands": demands,
    }
    report["summary"] = summarize(classes, demands, grapple_families)
    return report


def print_report(report):
    summary = report["summary"]
    print("=" * 78)
    print("VtMB NPC activity translation survey")
    print("=" * 78)
    print("binary: %s" % report["binary"]["path"])
    print("sha256: %s" % report["binary"]["sha256"])
    print("NPC subclasses: %(npc_subclasses)d" % summary)
    print("distinct bodies: pre=%(pre_translate)d class=%(class_translate)d "
          "cover=%(cover_activity)d reload=%(reload_activity)d" %
          summary["distinct_translation_functions"])
    print("base grapple activities: %(grapple_base_activities)d | role variants: "
          "%(grapple_role_variants)d" % summary)
    print()
    print("Current exported class demand")
    print("  references: %(current_demands)d | distinct classnames: "
          "%(distinct_current_classnames)d" % summary)
    for status, count in summary["current_status"].items():
        print("  %-38s %5d" % (status, count))
    for row in report["current_class_demands"]:
        if row["status"] != "resolved":
            print("  missing: %(map)s %(source_name)s %(demand_kind)s=%(requested_class)s" % row)


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
