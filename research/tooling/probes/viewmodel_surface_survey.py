"""Build the deterministic first-person viewmodel research census for RE42.

The report joins every authored source which can select a first-person model:
the patch-first hands tree, ``clandoc000.txt``, Python ``SetModel`` calls and
``vdata/items``.  It also decodes the skeleton, attachments, sequences and
events carried by the 21 hands and the 17 packed viewmodel models.

Generated JSON is game-derived evidence and therefore lives below
``$ELYSIUM_WORK_ROOT/research``.  Run it through the public wrapper::

    uv run elysium research viewmodel_surface_survey
    uv run elysium research viewmodel_surface_survey --json <path>
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from typing import Any, Callable, Iterable

from elysium_pipeline.formats import kv, mdl_skel
from elysium_pipeline.paths import research_root


SCHEMA = "elysium.research.first-person-viewmodel"
VERSION = 1
CLANDOC_KEY = "vdata/system/clandoc000.txt"
HANDS_PREFIX = "models/hands/"
ITEMS_PREFIX = "vdata/items/"
SCRIPT_PREFIXES = ("python/", "scripts/")
ATTACHMENT_STRIDE = 60

EXPECTED_HAND_COUNT = 21
EXPECTED_SHIELD_MODELS = {
    "models/hands/female/tremere/v_tremere_fem_hands_shield.mdl",
    "models/hands/male/tremere/v_tremere_male_hands_shield.mdl",
}
EXPECTED_DANGLING_CLANDOC = {
    "models/hands/female/gangrel/v_gangrel_fem_hands.mdl",
}
EXPECTED_FIREARM_FAMILIES = {
    "anaconda",
    "crossbow",
    "desert_eagle",
    "flamethrower",
    "m37",
    "pistol_glock",
    "rifle_rem700",
    "rifle_steyraug",
    "submachine_mac10",
    "submachine_uzi",
    "supershotgun",
    "thirtyeight",
}
EXPECTED_PACKED_VIEWMODELS = {
    "models/weapons/anaconda/view/v_anaconda.mdl",
    "models/weapons/breath/view/v_dragonbreath.mdl",
    "models/weapons/crossbow/view/v_crossbow.mdl",
    "models/weapons/desert_eagle/view/v_desert_eagle.mdl",
    "models/weapons/flamethrower/view/v_flamethrower.mdl",
    "models/weapons/grenade/pineapple/view/v_pineapple.mdl",
    "models/weapons/holylight/view/v_holylight.mdl",
    "models/weapons/lockpicks/view/v_lockpicks_ref.mdl",
    "models/weapons/m37/view/v_m37.mdl",
    "models/weapons/pistol_glock/view/v_pistol_glock.mdl",
    "models/weapons/rifle_rem700/view/v_rifle_rem700.mdl",
    "models/weapons/rifle_steyraug/view/v_rifle_steyraug.mdl",
    "models/weapons/submachine_mac10/view/v_submachine_mac10.mdl",
    "models/weapons/submachine_uzi/view/v_submachine_uzi.mdl",
    "models/weapons/supershotgun/view/v_supershotgun.mdl",
    "models/weapons/thaumaturgy/view/v_thaumaturgy.mdl",
    "models/weapons/thirtyeight/view/v_thirtyeight.mdl",
}

_SETMODEL_HANDS = re.compile(
    r"\.SetModel\s*\(\s*(['\"])(models[\\/]hands[\\/][^'\"]+?\.mdl)\1\s*\)",
    re.IGNORECASE,
)


def normalize_model(value: str) -> str:
    """Normalize an authored model value to a patch-first install key."""

    model = value.strip().replace("\\", "/").lower()
    if not model:
        return ""
    if not model.startswith("models/"):
        model = "models/" + model
    return model if model.endswith(".mdl") else model + ".mdl"


def string_values(value: Any) -> list[str]:
    """Flatten a KeyValues leaf while preserving repeated-key author order."""

    if isinstance(value, str):
        return [value]
    if isinstance(value, list):
        out: list[str] = []
        for item in value:
            out.extend(string_values(item))
        return out
    return []


def _first_string(value: Any) -> str:
    values = string_values(value)
    return values[-1] if values else ""


def clandoc_hand_references(data: bytes) -> list[dict[str, Any]]:
    """Return every active M_Hands/F_Hands leaf, including repeated keys."""

    document = kv.parse(data.decode("utf-8", errors="replace"))
    blocks = document.get("clandata", [])
    if not blocks and isinstance(document.get("general"), dict):
        blocks = [document]
    if isinstance(blocks, dict):
        blocks = [blocks]
    references: list[dict[str, Any]] = []
    for block_index, block in enumerate(blocks):
        if not isinstance(block, dict):
            continue
        general = block.get("general", {})
        if not isinstance(general, dict):
            continue
        clan = _first_string(general.get("clan", "")).strip()
        for key, sex in (("m_hands", "male"), ("f_hands", "female")):
            values = string_values(general.get(key))
            for value_index, value in enumerate(values):
                model = normalize_model(value)
                if not model:
                    continue
                references.append(
                    {
                        "clandoc_block": block_index,
                        "clan": clan,
                        "key": key,
                        "model": model,
                        "repeated": len(values) > 1,
                        "sex": sex,
                        "value_index": value_index,
                    }
                )
    return references


def script_hand_references(source_key: str, data: bytes) -> list[dict[str, Any]]:
    """Return exact Python ``SetModel`` hands literals with source line numbers."""

    text = data.decode("utf-8", errors="replace")
    references = []
    for match in _SETMODEL_HANDS.finditer(text):
        references.append(
            {
                "line": text.count("\n", 0, match.start()) + 1,
                "model": normalize_model(match.group(2)),
                "source": source_key,
            }
        )
    return references


def item_record(source_key: str, data: bytes) -> dict[str, Any]:
    """Decode the item fields which select first-person presentation."""

    document = kv.parse(data.decode("utf-8", errors="replace"))
    block = document.get("weapondata")
    if not isinstance(block, dict):
        block = document
    return {
        "anim_prefix": _first_string(block.get("anim_prefix", "")).strip(),
        "camera_class": _first_string(block.get("camera_class", "")).strip(),
        "classname": Path(source_key[len(ITEMS_PREFIX):]).stem,
        "reload_single": _first_string(block.get("reload_single", "")).strip(),
        "source": source_key,
        "viewmodel": normalize_model(_first_string(block.get("viewmodel", ""))),
    }


def _cstr(data: bytes, offset: int) -> str:
    if not 0 <= offset < len(data):
        raise ValueError(f"string offset outside model: {offset}")
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError(f"unterminated model string at {offset}")
    return data[offset:end].decode("ascii", errors="replace")


def read_attachments(data: bytes) -> list[dict[str, Any]]:
    """Decode the confirmed VtMB 60-byte StudioAttachment records."""

    count = struct.unpack_from("<i", data, 328)[0]
    base = struct.unpack_from("<i", data, 332)[0]
    if count < 0 or base < 0 or base + count * ATTACHMENT_STRIDE > len(data):
        raise ValueError(
            f"invalid attachment array: count={count} base={base} size={len(data)}"
        )
    attachments = []
    for index in range(count):
        record = base + index * ATTACHMENT_STRIDE
        name_relative, flags, bone = struct.unpack_from("<iii", data, record)
        attachments.append(
            {
                "bone": bone,
                "flags": flags,
                "index": index,
                "local_matrix_3x4": list(struct.unpack_from("<12f", data, record + 12)),
                "name": _cstr(data, record + name_relative) if name_relative else "",
            }
        )
    return attachments


def model_summary(model: str, data: bytes) -> dict[str, Any]:
    """Decode the stable structural surface required by PL14 and CCC10.1."""

    bones = mdl_skel.read_bones(data)
    sequences = mdl_skel.local_sequences(data)
    return {
        "attachments": read_attachments(data),
        "bones": [
            {
                "flags": bone.flags,
                "index": bone.index,
                "name": bone.name,
                "parent": bone.parent,
                "pose_to_bone": list(bone.pose_to_bone),
                "position": list(bone.pos),
                "quaternion_xyzw": list(bone.quat),
            }
            for bone in bones
        ],
        "model": model,
        "root_bones": [bone.index for bone in bones if bone.parent < 0],
        "sequences": [
            {
                "activity": sequence.activity,
                "events": [event._asdict() for event in sequence.events],
                "flags": sequence.flags,
                "fps": sequence.fps,
                "frames": sequence.frames,
                "label": sequence.label,
            }
            for sequence in sequences
        ],
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
    }


def reference_diagnostics(
    available_models: Iterable[str],
    clandoc_references: Iterable[dict[str, Any]],
    script_references: Iterable[dict[str, Any]],
    item_records: Iterable[dict[str, Any]],
) -> list[dict[str, Any]]:
    """Name every authored reference the patch-first index cannot resolve."""

    available = set(available_models)
    diagnostics = []
    surfaces = (
        ("clandoc", ((row["model"], row) for row in clandoc_references)),
        ("script", ((row["model"], row) for row in script_references)),
        (
            "item",
            ((row["viewmodel"], row) for row in item_records if row["viewmodel"]),
        ),
    )
    for surface, references in surfaces:
        for model, row in references:
            if model not in available:
                diagnostics.append(
                    {
                        "kind": "missing_model",
                        "model": model,
                        "source": surface,
                        "source_record": row,
                    }
                )
    return sorted(
        diagnostics,
        key=lambda row: (
            row["source"],
            row["model"],
            json.dumps(row["source_record"], sort_keys=True),
        ),
    )


def firearm_families(hand_summaries: Iterable[dict[str, Any]]) -> list[str]:
    """Recover the accepted family vocabulary from hands sequence labels."""

    found = set()
    for summary in hand_summaries:
        for sequence in summary["sequences"]:
            label = sequence["label"].lower()
            for family in EXPECTED_FIREARM_FAMILIES:
                if label == family or label.startswith(family + "_"):
                    found.add(family)
    return sorted(found)


def serialize_report(report: dict[str, Any]) -> str:
    """Stable tracked-instrument output; generated evidence itself stays untracked."""

    return json.dumps(report, indent=1, sort_keys=True) + "\n"


def build_report(
    index: dict[str, Any],
    reader: Callable[[dict[str, Any], str], bytes | None] | None = None,
) -> dict[str, Any]:
    """Build and validate the complete patch-first RE42 surface."""

    if reader is None:
        from elysium_pipeline.formats import install

        reader = install.read

    available_models = sorted(key for key in index if key.endswith(".mdl"))
    hand_models = sorted(
        key for key in available_models if key.startswith(HANDS_PREFIX)
    )

    clandoc_data = reader(index, CLANDOC_KEY)
    if clandoc_data is None:
        raise FileNotFoundError(CLANDOC_KEY)
    clandoc_references = clandoc_hand_references(clandoc_data)

    script_references = []
    script_inputs = []
    for key in sorted(index):
        if not key.startswith(SCRIPT_PREFIXES) or not key.endswith(".py"):
            continue
        data = reader(index, key)
        if data is None:
            continue
        references = script_hand_references(key, data)
        if references:
            script_references.extend(references)
            script_inputs.append(
                {
                    "path": key,
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "size": len(data),
                }
            )

    item_records = []
    for key in sorted(index):
        if not key.startswith(ITEMS_PREFIX) or not key.endswith(".txt"):
            continue
        data = reader(index, key)
        if data is not None:
            item_records.append(item_record(key, data))

    hand_summaries = []
    packed_summaries = []
    decode_errors = []
    for role, models, sink in (
        ("hands", hand_models, hand_summaries),
        ("packed_viewmodel", sorted(EXPECTED_PACKED_VIEWMODELS), packed_summaries),
    ):
        for model in models:
            data = reader(index, model)
            if data is None:
                decode_errors.append({"model": model, "reason": "not in install", "role": role})
                continue
            try:
                sink.append(model_summary(model, data))
            except (IndexError, struct.error, ValueError) as error:
                decode_errors.append({"model": model, "reason": str(error), "role": role})

    diagnostics = reference_diagnostics(
        available_models, clandoc_references, script_references, item_records
    )
    dangling_clandoc = {
        row["model"] for row in diagnostics if row["source"] == "clandoc"
    }
    script_models = {row["model"] for row in script_references}
    families = set(firearm_families(hand_summaries))
    packed_found = {row["model"] for row in packed_summaries}
    checks = {
        "clandoc_dangling_is_known_gangrel_path": dangling_clandoc
        == EXPECTED_DANGLING_CLANDOC,
        "firearm_families_are_complete": families == EXPECTED_FIREARM_FAMILIES,
        "hand_model_count_is_21": len(hand_models) == EXPECTED_HAND_COUNT,
        "packed_viewmodels_are_complete": packed_found == EXPECTED_PACKED_VIEWMODELS,
        "tremere_shields_are_in_install": EXPECTED_SHIELD_MODELS.issubset(hand_models),
        "tremere_shields_are_script_selected": EXPECTED_SHIELD_MODELS.issubset(script_models),
        "viewmodel_models_decode": not decode_errors,
    }

    return {
        "checks": checks,
        "census": {
            "clandoc_distinct_models": sorted({row["model"] for row in clandoc_references}),
            "firearm_families": sorted(families),
            "hand_models": hand_summaries,
            "item_records": item_records,
            "packed_viewmodels": packed_summaries,
            "script_selected_hands": script_references,
        },
        "diagnostics": diagnostics,
        "expected": {
            "dangling_clandoc_models": sorted(EXPECTED_DANGLING_CLANDOC),
            "firearm_families": sorted(EXPECTED_FIREARM_FAMILIES),
            "hand_model_count": EXPECTED_HAND_COUNT,
            "packed_viewmodels": sorted(EXPECTED_PACKED_VIEWMODELS),
            "shield_models": sorted(EXPECTED_SHIELD_MODELS),
        },
        "inputs": {
            "clandoc": {
                "path": CLANDOC_KEY,
                "sha256": hashlib.sha256(clandoc_data).hexdigest(),
                "size": len(clandoc_data),
            },
            "scripts": script_inputs,
        },
        "model_decode_errors": decode_errors,
        "schema": SCHEMA,
        "version": VERSION,
    }


def main() -> int:
    from elysium_pipeline.formats import install

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--json",
        type=Path,
        default=research_root() / "first-person-viewmodel" / "viewmodel-surface.json",
        help="generated report path below ELYSIUM_WORK_ROOT",
    )
    args = parser.parse_args()

    index = install.build_index(dirs=("models", "python", "scripts", "vdata"))
    report = build_report(index)
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(serialize_report(report), encoding="utf-8")

    checks = report["checks"]
    for name, passed in sorted(checks.items()):
        print(f"[{'ok' if passed else 'FAIL'}] {name}")
    print(args.json)
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
