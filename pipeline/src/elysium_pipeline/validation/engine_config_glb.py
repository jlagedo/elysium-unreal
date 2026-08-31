"""Independent structural validator for engine-config GLB products.

This re-parses nothing through `exporters.engine_config_glb.build_document`: it checks the
published extension against the vocabulary `seam_map_engine_config.md` states and the byte-ledger
and container rules `formats.unit_contract` states, so a writer bug cannot pass its own check.
"""

from __future__ import annotations

from collections import Counter
from pathlib import Path
from typing import Any, Mapping

from elysium_pipeline.formats.engine_config_glb.model import (
    ALL_GRAMMAR_KEYS,
    ENGINE_CONFIG_EXTENSION,
    GRAMMAR_FIELDS,
    GRAMMARS,
    SCHEMA_VERSION,
    grammar_for,
    is_residue,
)
from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    completeness,
    read_glb,
    validate_capsules,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)

ASSET_PREFIX = "vtmb:engine-config:"

#: The grammar-specific keys the extension root always declares; exactly the one grammar's own
#: tuple (from `formats.engine_config_glb.GRAMMAR_FIELDS`, the exporter's own source) is non-null.
_GRAMMAR_FIELD = GRAMMAR_FIELDS
_ALL_GRAMMAR_KEYS = ALL_GRAMMAR_KEYS

_DEPENDENCY_ROLES = {
    "engine-config", "material", "model", "map", "vdata", "texture", "scene", "sound",
    "ui-resource",
}

_ANOMALY_ROLES = {
    "unterminated-quote",
    "repeated-bind",
    "repeated-alias",
    "mixed-line-endings",
    "malformed-rad-row",
    "valueless-key",
    "unclosed-block-at-end-of-file",
    "malformed-key-equals-value-row",
    "anonymous-block",
    "unmatched-close-brace",
    "orphan-key-value-pair",
    "unclassified-localized-path",
}

#: The three ranges this seam can prove contribute no payload byte. A localized `.lip` whose
#: audio the install does not ship is not one of them: it is an ordinary `resolved: false`
#: dependency row, per `seam_map_unit_contract.md` ("References between units"), so no omission
#: role names it and a document that carries one is rejected.
_OMISSION_ROLES = {"empty-member", "trailing-fill", "insignificant-whitespace"}


class EngineConfigGlbValidationError(UnitValidationError):
    pass


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise EngineConfigGlbValidationError(message)


def _check_commands(commands: Any) -> None:
    _require(isinstance(commands, list), "commands is not a list")
    for index, row in enumerate(commands):
        _require(isinstance(row, dict), f"commands[{index}] is not a record")
        _require(row.get("index") == index, f"commands[{index}] is not in source order")
        _require(isinstance(row.get("name"), str) and row["name"], f"commands[{index}] has no name")
        _require(isinstance(row.get("args"), list), f"commands[{index}] has no args list")
        for arg in row["args"]:
            _require(
                isinstance(arg, dict) and isinstance(arg.get("text"), str) and isinstance(arg.get("quoted"), bool),
                f"commands[{index}] carries a malformed argument",
            )
        _require(isinstance(row.get("offset"), int) and row["offset"] >= 0, f"commands[{index}] has no offset")
        _require(isinstance(row.get("length"), int) and row["length"] > 0, f"commands[{index}] has no length")


def _check_bindings(bindings: Any, commands: list) -> None:
    _require(isinstance(bindings, list), "bindings is not a list")
    for row in bindings:
        _require(isinstance(row, dict), "a binding is not a record")
        for field_name in ("key", "sourceKey", "command", "commands", "commandIndex"):
            _require(field_name in row, f"a binding is missing {field_name}")
        _require(isinstance(row["commandIndex"], int) and 0 <= row["commandIndex"] < len(commands),
                  "a binding names no command of this unit")
        _require(commands[row["commandIndex"]]["name"].strip().lower() == "bind",
                  "a binding's commandIndex does not name a bind command")


def _check_aliases(aliases: Any, commands: list) -> None:
    _require(isinstance(aliases, list), "aliases is not a list")
    for row in aliases:
        _require(isinstance(row, dict), "an alias is not a record")
        for field_name in ("name", "body", "commands", "uses", "commandIndex"):
            _require(field_name in row, f"an alias is missing {field_name}")
        _require(isinstance(row["commandIndex"], int) and 0 <= row["commandIndex"] < len(commands),
                  "an alias names no command of this unit")
        _require(commands[row["commandIndex"]]["name"].strip().lower() == "alias",
                  "an alias's commandIndex does not name an alias command")
        for use in row["uses"]:
            _require(isinstance(use, dict) and isinstance(use.get("name"), str) and use.get("resolved") is True,
                      "an alias use is not a resolved cross-reference")


def _check_cvars(cvars: Any) -> None:
    _require(isinstance(cvars, list), "cvars is not a list")
    for row in cvars:
        _require(isinstance(row, dict), "a cvar is not a record")
        for field_name in ("index", "name", "value", "archived"):
            _require(field_name in row, f"a cvar is missing {field_name}")
        _require(row["archived"] is True, "a cvar does not state it is archived")


def _check_texture_lights(rows: Any) -> None:
    _require(isinstance(rows, list), "textureLights is not a list")
    for index, row in enumerate(rows):
        _require(isinstance(row, dict) and row.get("index") == index, f"textureLights[{index}] is out of order")
        _require(isinstance(row.get("texture"), str), f"textureLights[{index}] has no texture")
        _require(isinstance(row.get("raw"), str), f"textureLights[{index}] has no raw text")


def _check_model_entry(entry: Any, where: str) -> None:
    _require(isinstance(entry, dict) and set(entry) == {"model", "modelNormalized", "amount"},
              f"{where} does not carry its own three fields")


def _check_group(entry: Any, where: str) -> None:
    _require(isinstance(entry, dict) and set(entry) == {"name", "alpha", "models"},
              f"{where} does not carry its own three fields")
    _require(isinstance(entry["models"], list), f"{where}.models is not a list")
    for index, model_entry in enumerate(entry["models"]):
        _check_model_entry(model_entry, f"{where}.models[{index}]")


def _check_detail_types(rows: Any) -> None:
    _require(isinstance(rows, list), "detailTypes is not a list")
    for index, row in enumerate(rows):
        where = f"detailTypes[{index}]"
        _require(isinstance(row, dict) and set(row) == {"name", "density", "groups"},
                  f"{where} does not carry its own three fields")
        _require(isinstance(row["groups"], list), f"{where}.groups is not a list")
        for group_index, group in enumerate(row["groups"]):
            _check_group(group, f"{where}.groups[{group_index}]")


def _check_maps(rows: Any) -> None:
    _require(isinstance(rows, list), "maps is not a list")
    for index, row in enumerate(rows):
        _require(isinstance(row, dict) and row.get("index") == index, f"maps[{index}] is out of order")
        _require(isinstance(row.get("asset"), str) and row["asset"].startswith("vtmb:map:"),
                  f"maps[{index}] names no stable map id")
        _require(isinstance(row.get("included"), bool), f"maps[{index}] does not state inclusion")
        _require(isinstance(row.get("resolved"), bool), f"maps[{index}] does not state resolution")


def _check_packer(packer: Any) -> None:
    _require(isinstance(packer, dict), "packer is not a record")
    expected = {"pack_folder", "max_size", "exclude", "skip", "separate", "localized_file"}
    _require(set(packer) == expected, "packer does not carry its own six keys")
    for list_key in ("exclude", "skip", "separate"):
        _require(isinstance(packer[list_key], list), f"packer.{list_key} is not a list")


def _check_packer_keys(rows: Any) -> None:
    _require(isinstance(rows, list), "packerKeys is not a list")
    for index, row in enumerate(rows):
        _require(isinstance(row, dict) and row.get("index") == index, f"packerKeys[{index}] is out of order")
        for field_name in ("key", "value", "offset"):
            _require(field_name in row, f"packerKeys[{index}] is missing {field_name}")


def _check_categories(rows: Any) -> None:
    _require(isinstance(rows, list), "categories is not a list")
    for index, row in enumerate(rows):
        _require(isinstance(row, dict) and row.get("index") == index, f"categories[{index}] is out of order")
        _require(isinstance(row.get("name"), str) and row["name"], f"categories[{index}] has no name")
        _require(isinstance(row.get("paths"), list), f"categories[{index}] has no paths list")
        for path_index, path_row in enumerate(row["paths"]):
            _require(isinstance(path_row, dict) and path_row.get("index") == path_index,
                      f"categories[{index}].paths[{path_index}] is out of order")
            _require(isinstance(path_row.get("resolved"), bool),
                      f"categories[{index}].paths[{path_index}] does not state resolution")


def _check_binary(binary: Any) -> None:
    _require(isinstance(binary, dict), "binary is not a record")
    for field_name in ("bytes", "byteLength", "candidateReading"):
        _require(field_name in binary, f"binary is missing {field_name}")
    _require(isinstance(binary["bytes"], str), "binary.bytes is not hex text")
    _require(len(binary["bytes"]) == binary["byteLength"] * 2, "binary.bytes disagrees with byteLength")


def _check_save_fragment(fragment: Any) -> None:
    _require(isinstance(fragment, dict), "saveFragment is not a record")
    for field_name in ("totalBytes", "blocks"):
        _require(field_name in fragment, f"saveFragment is missing {field_name}")
    _require(isinstance(fragment["blocks"], list) and fragment["blocks"], "saveFragment has no blocks")
    for index, block in enumerate(fragment["blocks"]):
        _require(isinstance(block, dict) and block.get("index") == index,
                  f"saveFragment.blocks[{index}] is out of order")
        for field_name in ("offset", "length", "header", "body", "strings"):
            _require(field_name in block, f"saveFragment.blocks[{index}] is missing {field_name}")


def _check_dependencies(dependencies: list) -> None:
    seen: set[str] = set()
    for index, row in enumerate(dependencies):
        _require(isinstance(row, dict), f"dependency {index} is not a record")
        role = row.get("role")
        _require(role in _DEPENDENCY_ROLES, f"dependency {index} names an unknown role {role!r}")
        asset = row.get("asset")
        _require(asset not in seen, f"dependency {asset} is declared twice")
        seen.add(asset)


def _check_anomalies(anomalies: list) -> None:
    for row in anomalies:
        _require(isinstance(row, dict) and row.get("role") in _ANOMALY_ROLES,
                  f"unknown source anomaly {row!r}")


def _check_omissions(omissions: list) -> None:
    for row in omissions:
        _require(isinstance(row, dict) and row.get("role") in _OMISSION_ROLES,
                  f"unknown source omission {row!r}")


def _check_against_decode(root: Mapping[str, Any], source_members, resolver) -> None:
    """Re-decode the selected member independently and compare every published field.

    `seam_map_engine_config.md` ("Coverage and validation"): "Export-time validation
    re-tokenizes every console script, re-splits every alias body and binding string, and
    compares every table row and dependency with the writer's output." A decode bug that also
    lives in `exporters.engine_config_glb.build_document` would pass its own check, so this
    imports `decode_engine_config` instead of that module.
    """

    from elysium_pipeline.formats.engine_config_glb.decode import decode_engine_config
    from elysium_pipeline.formats.engine_config_glb.source import EngineConfigSourceClosure

    member = next(iter(source_members), None)
    if member is None:
        raise EngineConfigGlbValidationError("prepublication source carries no member")
    identity = root.get("identity") or {}
    key = str(identity.get("sourcePath", ""))
    asset = str(identity.get("asset", ""))
    grammar = str(root.get("grammar", ""))

    if resolver is None:
        # Standalone re-validation (or a caller with no install index) has no way to answer
        # "does this path exist in the install"; falling back to the writer's own declared
        # resolutions still proves the *decode* -- tokenizing, splitting, table shape -- is
        # reproducible byte-for-byte, which is what a decode bug would break.
        declared = {
            str(row.get("sourcePath")): bool(row.get("resolved")) for row in root.get("dependencies") or []
        }
        resolver = lambda path: declared.get(path, False)  # noqa: E731

    closure = EngineConfigSourceClosure(
        key=key, asset=asset, grammar=grammar, residue=is_residue(key), member=member,
    )
    fresh = decode_engine_config(closure, resolver=resolver)

    def _same(label: str, declared_value: Any, fresh_value: Any) -> None:
        _require(declared_value == fresh_value, f"{label} disagrees with an independent re-decode")

    _same("commands", root.get("commands"), fresh.commands)
    _same("bindings", root.get("bindings"), fresh.bindings)
    _same("aliases", root.get("aliases"), fresh.aliases)
    _same("cvars", root.get("cvars"), fresh.cvars)
    _same("scriptExpressions", root.get("scriptExpressions"), fresh.script_expressions)
    _same("textureLights", root.get("textureLights"), fresh.texture_lights)
    _same("detailTypes", root.get("detailTypes"), fresh.detail_types)
    _same("maps", root.get("maps"), fresh.maps)
    _same("packer", root.get("packer"), fresh.packer)
    _same("packerKeys", root.get("packerKeys"), fresh.packer_keys)
    _same("categories", root.get("categories"), fresh.categories)
    _same("binary", root.get("binary"), fresh.binary)
    _same("saveFragment", root.get("saveFragment"), fresh.save_fragment)
    _same("dependencies", root.get("dependencies"), fresh.dependencies)
    _same("comments", root.get("comments"), fresh.comments)
    _same("anomalies", root.get("anomalies"), fresh.anomalies)
    _same("omissions", root.get("omissions"), fresh.omissions)


def validate_document(
    document: dict, binary: bytes, *, source_members=None, resolver=None
) -> dict[str, Any]:
    root = validate_extension_root(
        document, ENGINE_CONFIG_EXTENSION, asset_prefix=ASSET_PREFIX, schema_version=SCHEMA_VERSION
    )
    validate_container(document, binary)
    validate_sceneless(document)
    validate_ledgers(root, source_members)
    validate_capsules(document, binary, root, source_members)

    incomplete = completeness(root)
    if incomplete["unresolved"] or incomplete["unsupported"]:
        raise EngineConfigGlbValidationError("engine-config extension is incomplete")

    grammar = root.get("grammar")
    _require(grammar in GRAMMARS, f"unknown grammar {grammar!r}")
    for key in _ALL_GRAMMAR_KEYS:
        _require(key in root, f"extension root is missing {key!r}")
    expected_present = set(_GRAMMAR_FIELD[grammar])
    for key in _ALL_GRAMMAR_KEYS:
        if key in expected_present:
            _require(root[key] is not None, f"grammar {grammar!r} declares no {key}")
        else:
            _require(root[key] is None, f"grammar {grammar!r} unexpectedly declares {key}")

    identity = root.get("identity") or {}
    asset = str(identity.get("asset", ""))
    key = asset[len(ASSET_PREFIX):]
    source_path = str(identity.get("sourcePath", ""))
    _require(source_path == key, "the identity disagrees with its own source path")
    _require(grammar_for(key) == grammar, "the declared grammar disagrees with the member's own")
    if key == "hl2.tmp":
        _require(identity.get("residue") is True, "hl2.tmp does not declare identity.residue")

    members = (root.get("sourceResolution") or {}).get("members") or []
    _require(
        len(members) == 1 and members[0].get("role") == "unit-selecting" and "span" not in members[0],
        "an engine-config unit owns exactly one whole file",
    )
    _require(members[0].get("path") == source_path, "the source member is not this unit's own file")

    commands = root.get("commands")
    if commands is not None:
        _check_commands(commands)
        _check_bindings(root.get("bindings"), commands)
        _check_aliases(root.get("aliases"), commands)
        _check_cvars(root.get("cvars"))
    if root.get("textureLights") is not None:
        _check_texture_lights(root["textureLights"])
    if root.get("detailTypes") is not None:
        _check_detail_types(root["detailTypes"])
    if root.get("maps") is not None:
        _check_maps(root["maps"])
    if root.get("packer") is not None:
        _check_packer(root["packer"])
        _check_packer_keys(root.get("packerKeys"))
    if root.get("categories") is not None:
        _check_categories(root["categories"])
    if root.get("binary") is not None:
        _check_binary(root["binary"])
    if root.get("saveFragment") is not None:
        _check_save_fragment(root["saveFragment"])

    dependencies = root.get("dependencies") or []
    _check_dependencies(dependencies)
    _check_anomalies(root.get("anomalies") or [])
    _check_omissions(root.get("omissions") or [])

    if source_members is not None:
        _check_against_decode(root, source_members, resolver)

    return {
        "asset": asset,
        "key": key,
        "grammar": grammar,
        "dependencies": len(dependencies),
        "unresolvedDependencies": sorted(
            str(row.get("asset")) for row in dependencies if not row.get("resolved")
        ),
        "anomalies": [row.get("role") for row in root.get("anomalies") or []],
        "omissions": [row.get("role") for row in root.get("omissions") or []],
        "typedUnidentified": len((root.get("coverage") or {}).get("typedUnidentified") or []),
        "sourceBytes": members[0].get("byteLength", 0),
        "byteCoveragePercent": 100.0,
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: Mapping[str, Any]) -> list[str]:
    warnings: list[str] = []
    missing = summary.get("unresolvedDependencies") or []
    if missing:
        warnings.append(
            "the install carries no member for "
            + ", ".join(missing[:4])
            + (f" and {len(missing) - 4} more" if len(missing) > 4 else "")
        )
    anomalies = summary.get("anomalies") or []
    if anomalies:
        counted = ", ".join(f"{role}x{count}" for role, count in sorted(Counter(anomalies).items()))
        warnings.append(f"the source departs from the grammar's conventions: {counted}")
    omissions = summary.get("omissions") or []
    if omissions:
        counted = ", ".join(f"{role}x{count}" for role, count in sorted(Counter(omissions).items()))
        warnings.append(f"the source carries a proven omission: {counted}")
    if summary.get("typedUnidentified"):
        warnings.append(f"{summary['typedUnidentified']} typed-but-unidentified range(s) carried")
    return warnings
