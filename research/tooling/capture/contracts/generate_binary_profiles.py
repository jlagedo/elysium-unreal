"""Validate binary profiles and generate native and Python registry views."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import pprint
import re


ROOT = Path(__file__).resolve().parent
REPOSITORY_ROOT = ROOT.parents[3]
REGISTRY = ROOT / "binary_profiles.json"
RECORD_SCHEMAS = ROOT / "record_schemas.json"
PYTHON_OUTPUT = ROOT.parent / "generated_binary_profiles.py"
CPP_OUTPUT = ROOT.parent / "native" / "generated_binary_profiles.h"

SHA256 = re.compile(r"[0-9a-f]{64}")
KINDS = {"symbol": "Symbol", "inline": "Inline", "vtable": "Vtable"}
CONVENTIONS = {
    "cdecl": "Cdecl",
    "stdcall": "Stdcall",
    "thiscall": "Thiscall",
    "fastcall": "Fastcall",
}


def _integer(value: object, label: str, maximum: int = 0xFFFFFFFF) -> int:
    if isinstance(value, str):
        result = int(value, 0)
    elif isinstance(value, int) and not isinstance(value, bool):
        result = value
    else:
        raise ValueError(f"{label} must be an integer")
    if result < 0 or result > maximum:
        raise ValueError(f"{label} is out of range")
    return result


def _record_registry() -> dict[str, dict[str, object]]:
    document = json.loads(RECORD_SCHEMAS.read_text(encoding="utf-8"))
    return {record["name"]: record for record in document["records"]}


def _schema_refs(
    values: object,
    records: dict[str, dict[str, object]],
    label: str,
) -> list[dict[str, int | str]]:
    if not isinstance(values, list):
        raise ValueError(f"{label} must be a list")
    result: list[dict[str, int | str]] = []
    seen: set[tuple[int, int]] = set()
    for index, source in enumerate(values):
        if not isinstance(source, dict):
            raise ValueError(f"{label}[{index}] must be an object")
        name = source.get("name")
        version = source.get("version")
        if name not in records:
            raise ValueError(f"{label}[{index}] names unknown record {name!r}")
        record = records[name]
        if version != record["schema_version"]:
            raise ValueError(
                f"{label}[{index}] version {version!r} does not match "
                f"{name} version {record['schema_version']}"
            )
        key = (int(record["id"]), int(version))
        if key in seen:
            raise ValueError(f"{label} duplicates {name} version {version}")
        seen.add(key)
        result.append({"name": name, "id": key[0], "version": key[1]})
    return result


def _validate_source_spec(profile: dict[str, object]) -> None:
    source = profile.get("source_spec")
    if source is None:
        return
    path = REPOSITORY_ROOT / str(source)
    specification = json.loads(path.read_text(encoding="utf-8"))
    binary = specification["binary"]
    if specification["program"].casefold() != str(profile["module"]).casefold():
        raise ValueError(f"{profile['id']} source_spec module mismatch")
    if int(binary["size"]) != profile["file_size"]:
        raise ValueError(f"{profile['id']} source_spec size mismatch")
    if binary["sha256"] != profile["sha256"]:
        raise ValueError(f"{profile['id']} source_spec SHA-256 mismatch")
    if _integer(binary["image_base"], "source image base") != profile["pe"][
        "preferred_image_base"
    ]:
        raise ValueError(f"{profile['id']} source_spec image-base mismatch")
    functions = {
        function["label"]: int(str(function["address"]), 16)
        for function in specification.get("functions", [])
    }
    image_base = profile["pe"]["preferred_image_base"]
    for target in profile["targets"]:
        source_label = target.get("source_function_label")
        if source_label is None:
            continue
        expected_address = image_base + target["rva"]
        if functions.get(source_label) != expected_address:
            raise ValueError(
                f"{profile['id']} target {target['semantic_label']} "
                "does not match its source specification"
            )


def load_registry() -> dict[str, object]:
    document = json.loads(REGISTRY.read_text(encoding="utf-8"))
    if document.get("registry_version") != 1:
        raise ValueError("unsupported binary profile registry version")
    profiles = document.get("profiles")
    if not isinstance(profiles, list) or not profiles:
        raise ValueError("binary profile registry must contain profiles")
    records = _record_registry()
    profile_ids: set[str] = set()
    identities: set[tuple[str, str]] = set()
    semantic_labels: set[str] = set()
    normalized: list[dict[str, object]] = []
    for profile_index, source_profile in enumerate(profiles):
        if not isinstance(source_profile, dict):
            raise ValueError(f"profiles[{profile_index}] must be an object")
        profile = dict(source_profile)
        profile_id = profile.get("id")
        module = profile.get("module")
        digest = profile.get("sha256")
        if not isinstance(profile_id, str) or not profile_id:
            raise ValueError(f"profiles[{profile_index}].id is invalid")
        if profile_id in profile_ids:
            raise ValueError(f"duplicate profile id {profile_id}")
        profile_ids.add(profile_id)
        if not isinstance(module, str) or not module or "/" in module or "\\" in module:
            raise ValueError(f"{profile_id}.module must be a filename")
        if not isinstance(digest, str) or not SHA256.fullmatch(digest):
            raise ValueError(f"{profile_id}.sha256 is invalid")
        identity = (module.casefold(), digest)
        if identity in identities:
            raise ValueError(f"duplicate binary identity for {module}")
        identities.add(identity)
        profile["file_size"] = _integer(
            profile.get("file_size"), f"{profile_id}.file_size", 0xFFFFFFFFFFFFFFFF
        )
        pe_source = profile.get("pe")
        if not isinstance(pe_source, dict):
            raise ValueError(f"{profile_id}.pe must be an object")
        pe = {
            "machine": _integer(pe_source.get("machine"), f"{profile_id}.pe.machine", 0xFFFF),
            "timestamp": _integer(pe_source.get("timestamp"), f"{profile_id}.pe.timestamp"),
            "optional_magic": _integer(
                pe_source.get("optional_magic"),
                f"{profile_id}.pe.optional_magic",
                0xFFFF,
            ),
            "preferred_image_base": _integer(
                pe_source.get("preferred_image_base"),
                f"{profile_id}.pe.preferred_image_base",
            ),
            "size_of_image": _integer(
                pe_source.get("size_of_image"), f"{profile_id}.pe.size_of_image"
            ),
            "checksum": _integer(
                pe_source.get("checksum"), f"{profile_id}.pe.checksum"
            ),
            "section_count": _integer(
                pe_source.get("section_count"),
                f"{profile_id}.pe.section_count",
                0xFFFF,
            ),
            "characteristics": _integer(
                pe_source.get("characteristics"),
                f"{profile_id}.pe.characteristics",
                0xFFFF,
            ),
        }
        profile["pe"] = pe
        profile_schemas = _schema_refs(
            profile.get("supported_record_schemas"),
            records,
            f"{profile_id}.supported_record_schemas",
        )
        profile["supported_record_schemas"] = profile_schemas
        profile_schema_keys = {
            (schema["id"], schema["version"]) for schema in profile_schemas
        }
        targets = profile.get("targets")
        if not isinstance(targets, list):
            raise ValueError(f"{profile_id}.targets must be a list")
        normalized_targets: list[dict[str, object]] = []
        for target_index, source_target in enumerate(targets):
            if not isinstance(source_target, dict):
                raise ValueError(f"{profile_id}.targets[{target_index}] must be an object")
            target = dict(source_target)
            label = target.get("semantic_label")
            kind = target.get("kind")
            convention = target.get("calling_convention")
            expected = target.get("expected_bytes")
            if not isinstance(label, str) or not label:
                raise ValueError(f"{profile_id}.targets[{target_index}] label is invalid")
            if label in semantic_labels:
                raise ValueError(f"duplicate semantic label {label}")
            semantic_labels.add(label)
            if kind not in KINDS:
                raise ValueError(f"{label}.kind is unsupported")
            if convention not in CONVENTIONS:
                raise ValueError(f"{label}.calling_convention is unsupported")
            target["rva"] = _integer(target.get("rva"), f"{label}.rva")
            if target["rva"] >= pe["size_of_image"]:
                raise ValueError(f"{label}.rva lies outside the image")
            if (
                not isinstance(expected, str)
                or len(expected) % 2
                or not expected
                or len(expected) > 64
            ):
                raise ValueError(f"{label}.expected_bytes is invalid")
            try:
                target["expected_bytes"] = bytes.fromhex(expected)
            except ValueError as error:
                raise ValueError(f"{label}.expected_bytes is invalid") from error
            target_schemas = _schema_refs(
                target.get("supported_record_schemas"),
                records,
                f"{label}.supported_record_schemas",
            )
            if any(
                (schema["id"], schema["version"]) not in profile_schema_keys
                for schema in target_schemas
            ):
                raise ValueError(f"{label} uses a schema unsupported by its module")
            target["supported_record_schemas"] = target_schemas
            vtable = target.get("vtable")
            if kind == "vtable":
                if not isinstance(vtable, dict):
                    raise ValueError(f"{label}.vtable must be an object")
                target["vtable"] = {
                    "object_rva": _integer(vtable.get("object_rva"), f"{label}.object_rva"),
                    "expected_vtable_rva": _integer(
                        vtable.get("expected_vtable_rva"),
                        f"{label}.expected_vtable_rva",
                    ),
                    "slot": _integer(vtable.get("slot"), f"{label}.slot"),
                }
            elif vtable is not None:
                raise ValueError(f"{label}.vtable is only valid for vtable targets")
            normalized_targets.append(target)
        profile["targets"] = normalized_targets
        _validate_source_spec(profile)
        normalized.append(profile)
    return {"registry_version": 1, "profiles": normalized}


def _hex_bytes(data: bytes) -> str:
    return ", ".join(f"0x{value:02x}" for value in data)


def _cpp_schema_array(name: str, schemas: list[dict[str, object]]) -> list[str]:
    if not schemas:
        return []
    lines = [f"inline constexpr RecordSchemaSupport {name}[] = {{"]
    lines.extend(
        f"    {{{schema['id']}u, {schema['version']}u}}," for schema in schemas
    )
    lines.append("};")
    return lines


def render_cpp(registry: dict[str, object]) -> str:
    lines = [
        "// Generated by contracts/generate_binary_profiles.py; do not edit.",
        "#pragma once",
        "",
        '#include "binary_profile_contract.h"',
        "",
        "namespace elysium::capture::profiles {",
        "",
        f"inline constexpr std::uint32_t RegistryVersion = {registry['registry_version']}u;",
        "",
    ]
    profile_names: list[str] = []
    for profile_index, profile in enumerate(registry["profiles"]):
        prefix = f"Profile{profile_index}"
        profile_names.append(prefix)
        lines.extend(
            _cpp_schema_array(
                f"{prefix}Schemas", profile["supported_record_schemas"]
            )
        )
        if profile["supported_record_schemas"]:
            lines.append("")
        target_names: list[str] = []
        for target_index, target in enumerate(profile["targets"]):
            target_prefix = f"{prefix}Target{target_index}"
            target_names.append(target_prefix)
            lines.extend(
                _cpp_schema_array(
                    f"{target_prefix}Schemas",
                    target["supported_record_schemas"],
                )
            )
            if target["supported_record_schemas"]:
                lines.append("")
            lines.extend(
                [
                    f"inline constexpr std::uint8_t {target_prefix}ExpectedBytes[] = {{",
                    f"    {_hex_bytes(target['expected_bytes'])}",
                    "};",
                    "",
                ]
            )
        if target_names:
            lines.append(f"inline constexpr BinaryTargetProfile {prefix}Targets[] = {{")
            for target_name, target in zip(target_names, profile["targets"], strict=True):
                vtable = target.get("vtable", {})
                schemas = target["supported_record_schemas"]
                lines.extend(
                    [
                        "    {",
                        f'        "{target["semantic_label"]}",',
                        f"        BinaryTargetKind::{KINDS[target['kind']]},",
                        f"        CallingConvention::{CONVENTIONS[target['calling_convention']]},",
                        f"        0x{target['rva']:08x}u,",
                        f"        {target_name}ExpectedBytes,",
                        f"        {len(target['expected_bytes'])}u,",
                        f"        0x{vtable.get('object_rva', 0):08x}u,",
                        f"        0x{vtable.get('expected_vtable_rva', 0):08x}u,",
                        f"        {vtable.get('slot', 0)}u,",
                        f"        {'%sSchemas' % target_name if schemas else 'nullptr'},",
                        f"        {f'{len(schemas)}u' if schemas else '0u'},",
                        "    },",
                    ]
                )
            lines.extend(["};", ""])
        digest = bytes.fromhex(profile["sha256"])
        pe = profile["pe"]
        profile_schemas = profile["supported_record_schemas"]
        lines.extend(
            [
                f"inline constexpr BinaryProfile {prefix} = {{",
                f'    "{profile["id"]}",',
                f'    L"{profile["module"]}",',
                f"    {profile['file_size']}ull,",
                f"    {{{_hex_bytes(digest)}}},",
                "    {",
                f"        0x{pe['machine']:04x}u,",
                f"        0x{pe['optional_magic']:04x}u,",
                f"        {pe['section_count']}u,",
                f"        0x{pe['characteristics']:04x}u,",
                f"        0x{pe['timestamp']:08x}u,",
                f"        0x{pe['preferred_image_base']:08x}u,",
                f"        0x{pe['size_of_image']:08x}u,",
                f"        0x{pe['checksum']:08x}u,",
                "    },",
                f"    {f'{prefix}Schemas' if profile_schemas else 'nullptr'},",
                f"    {f'{len(profile_schemas)}u' if profile_schemas else '0u'},",
                f"    {f'{prefix}Targets' if target_names else 'nullptr'},",
                f"    {f'{len(target_names)}u' if target_names else '0u'},",
                "};",
                "",
            ]
        )
    lines.append("inline constexpr BinaryProfile Registry[] = {")
    lines.extend(f"    {name}," for name in profile_names)
    lines.extend(
        [
            "};",
            "",
            "}  // namespace elysium::capture::profiles",
            "",
        ]
    )
    return "\n".join(lines)


def _jsonable(registry: dict[str, object]) -> dict[str, object]:
    result = {"registry_version": registry["registry_version"], "profiles": []}
    for source_profile in registry["profiles"]:
        profile = dict(source_profile)
        profile["sha256"] = str(profile["sha256"])
        targets = []
        for source_target in profile["targets"]:
            target = dict(source_target)
            target["expected_bytes"] = target["expected_bytes"].hex()
            targets.append(target)
        profile["targets"] = targets
        result["profiles"].append(profile)
    return result


def render_python(registry: dict[str, object]) -> str:
    payload = pprint.pformat(
        _jsonable(registry),
        sort_dicts=False,
        width=88,
    )
    return "\n".join(
        [
            '"""Generated by contracts/generate_binary_profiles.py; do not edit."""',
            "",
            f"REGISTRY = {payload}",
            "REGISTRY_VERSION = REGISTRY['registry_version']",
            "PROFILES = REGISTRY['profiles']",
            "PROFILES_BY_ID = {profile['id']: profile for profile in PROFILES}",
            "",
            "def profiles_for_module(module_name: str) -> list[dict[str, object]]:",
            "    folded = module_name.casefold()",
            "    return [",
            "        profile",
            "        for profile in PROFILES",
            "        if str(profile['module']).casefold() == folded",
            "    ]",
            "",
            "def match_profile(",
            "    module_name: str, file_size: int, sha256: str",
            ") -> dict[str, object]:",
            "    matches = [",
            "        profile",
            "        for profile in profiles_for_module(module_name)",
            "        if profile['file_size'] == file_size and profile['sha256'] == sha256",
            "    ]",
            "    if len(matches) != 1:",
            "        raise LookupError(",
            "            f'no exact binary profile for {module_name} size={file_size} '",
            "            f'sha256={sha256}'",
            "        )",
            "    return matches[0]",
            "",
            "def target(",
            "    profile: dict[str, object], semantic_label: str",
            ") -> dict[str, object]:",
            "    matches = [",
            "        item",
            "        for item in profile['targets']",
            "        if item['semantic_label'] == semantic_label",
            "    ]",
            "    if len(matches) != 1:",
            "        raise LookupError(",
            "            f\"profile {profile['id']} has no unique target {semantic_label}\"",
            "        )",
            "    return matches[0]",
            "",
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    registry = load_registry()
    outputs = {
        PYTHON_OUTPUT: render_python(registry),
        CPP_OUTPUT: render_cpp(registry),
    }
    stale = [
        path
        for path, content in outputs.items()
        if not path.exists() or path.read_text(encoding="utf-8") != content
    ]
    if args.check:
        if stale:
            parser.error(
                "generated binary profiles are stale: "
                + ", ".join(str(path) for path in stale)
            )
        return 0
    for path, content in outputs.items():
        path.write_text(content, encoding="utf-8")
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
