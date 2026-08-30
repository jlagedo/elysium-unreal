"""The checks every published unit answers to, whatever kind it is.

Validation splits in two. Export-time validation receives the selected source members and is the
only place a `-zero` claim can be proven; standalone validation reads a published unit with no
install present. Both run through these functions -- the export-time one simply passes the
members it re-read -- so a seam cannot be strict in one path and lax in the other.
"""

from __future__ import annotations

from typing import Any, Mapping, Sequence

from elysium_pipeline.formats.unit_contract.coverage import ROOT_KEYS
from elysium_pipeline.formats.unit_contract.ledger import ByteLedgerError, verify_ledger_row
from elysium_pipeline.formats.unit_contract.origin import SOURCE_POLICY, SourceMember
from elysium_pipeline.formats.unit_contract.references import (
    DEPENDENCY_OPTIONAL_KEYS,
    DEPENDENCY_REQUIRED_KEYS,
)


class UnitValidationError(ValueError):
    """A published unit contradicts the contract."""


#: The core glTF arrays a unit whose source holds nothing drawable never declares.
FORBIDDEN_CORE = ("scenes", "nodes", "meshes", "images", "textures", "samplers")

#: `animations` and `skins` are core glTF a spatial kind is entitled to; a scene-less unit has no
#: node for either to address, so declaring one there is a decode that leaked into the core.
SCENELESS_FORBIDDEN = FORBIDDEN_CORE + ("animations", "skins")

#: Key spellings that would smuggle an opaque copy of the source into the product.
OPAQUE_KEYS = frozenset(
    {"rawdata", "sourcebytes", "opaquebytes", "base64", "sourcetext", "rawbytes"}
)

#: Below this a member's bytes are short enough to occur inside an unrelated JSON string by
#: coincidence, so containment there proves nothing; the BIN chunk is still checked.
OPAQUE_JSON_MINIMUM = 64


#: The two keys of one row the corpus index writes into a field no unit can fill about itself --
#: `identity.referencedBy`, `selectedBy[]` -- naming the referring unit and the role it named
#: the target under.
INDEX_ROW_KEYS = ("from", "role")


def validate_index_rows(value: Any, field: str) -> None:
    """A field the corpus index back-fills: empty as the seam exported it, `{from, role}` rows
    once the index has written it.

    A published unit is read back in both states -- the seam writes the unit, the index rewrites
    it in place as its last step -- so validation accepts the index's shape and nothing else.
    """

    if not isinstance(value, list):
        raise UnitValidationError(f"{field} is not a list")
    for row in value:
        if not isinstance(row, Mapping) or tuple(row) != INDEX_ROW_KEYS:
            raise UnitValidationError(
                f"{field} carries a row that is not the index's {{from, role}}"
            )
        if not str(row["from"]).startswith("vtmb:") or not str(row["role"]):
            raise UnitValidationError(f"{field} carries a row naming no referrer and role")

_COMPONENT_BYTES = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
_TYPE_WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}


def validate_container(document: Mapping[str, Any], binary: bytes) -> None:
    """One buffer, every view on it, aligned and in range -- or no buffer machinery at all."""

    buffers = list(document.get("buffers") or [])
    views = list(document.get("bufferViews") or [])
    accessors = list(document.get("accessors") or [])
    if not binary:
        if buffers or views or accessors:
            raise UnitValidationError("a declared buffer, view or accessor has no BIN chunk")
        return
    if len(buffers) != 1:
        raise UnitValidationError(
            f"a unit with a BIN chunk declares one buffer, not {len(buffers)}"
        )
    declared = int(buffers[0].get("byteLength", -1))
    # `binary` is the BIN chunk, which the container pads to four bytes; the buffer declares the
    # payload, so the two agree exactly or differ only by that padding.
    if not 0 <= len(binary) - declared < 4:
        raise UnitValidationError(
            f"buffer declares {declared} bytes against a {len(binary)}-byte BIN chunk"
        )
    if not views:
        raise UnitValidationError("a BIN chunk is present that no bufferView references")
    for index, view in enumerate(views):
        if view.get("buffer") != 0:
            raise UnitValidationError(f"bufferView {index} does not use buffer 0")
        offset = int(view.get("byteOffset", 0))
        length = int(view.get("byteLength", -1))
        if offset % 4:
            raise UnitValidationError(f"bufferView {index} starts at unaligned offset {offset}")
        if offset < 0 or length < 0 or offset + length > len(binary):
            raise UnitValidationError(f"bufferView {index} runs outside the BIN chunk")


def validate_sceneless(document: Mapping[str, Any]) -> None:
    """A unit whose source holds nothing a general consumer can draw or play declares no core."""

    declared = [name for name in SCENELESS_FORBIDDEN if document.get(name)]
    if declared:
        raise UnitValidationError(
            f"a scene-less unit declares no {', '.join(declared)}"
        )


def validate_accessors(document: Mapping[str, Any], binary: bytes) -> None:
    """Every accessor's element extent fits the view it reads through."""

    views = list(document.get("bufferViews") or [])
    for index, accessor in enumerate(document.get("accessors") or []):
        view_index = accessor.get("bufferView")
        if not isinstance(view_index, int) or not 0 <= view_index < len(views):
            raise UnitValidationError(f"accessor {index} names no bufferView of this unit")
        component = accessor.get("componentType")
        shape = accessor.get("type")
        if component not in _COMPONENT_BYTES or shape not in _TYPE_WIDTH:
            raise UnitValidationError(
                f"accessor {index} has component/type {component}/{shape}"
            )
        count = accessor.get("count")
        if not isinstance(count, int) or count < 0:
            raise UnitValidationError(f"accessor {index} has invalid count {count!r}")
        start = int(accessor.get("byteOffset", 0))
        if start < 0 or start % 4:
            raise UnitValidationError(f"accessor {index} starts at unaligned offset {start}")
        required = count * _COMPONENT_BYTES[component] * _TYPE_WIDTH[shape]
        available = int(views[view_index].get("byteLength", 0)) - start
        if required > available:
            raise UnitValidationError(
                f"accessor {index} needs {required} bytes; its view offers {available}"
            )


def _check_dependency(row: Any, index: int) -> None:
    if not isinstance(row, Mapping):
        raise UnitValidationError(f"dependency {index} is not a record")
    missing = [key for key in DEPENDENCY_REQUIRED_KEYS if key not in row]
    if missing:
        raise UnitValidationError(f"dependency {index} is missing {', '.join(missing)}")
    unknown = [
        key
        for key in row
        if key not in DEPENDENCY_REQUIRED_KEYS and key not in DEPENDENCY_OPTIONAL_KEYS
    ]
    if unknown:
        raise UnitValidationError(f"dependency {index} carries {', '.join(sorted(unknown))}")
    if not str(row["role"]):
        raise UnitValidationError(f"dependency {index} names no role")
    if not str(row["asset"]).startswith("vtmb:"):
        raise UnitValidationError(f"dependency {index} names no stable identity")
    if not isinstance(row["sourcePath"], str):
        raise UnitValidationError(f"dependency {index} has no authored source path")
    if not isinstance(row["resolved"], bool):
        raise UnitValidationError(f"dependency {index} does not state whether it resolved")


def validate_extension_root(
    document: Mapping[str, Any],
    extension: str,
    *,
    asset_prefix: str,
    schema_version: str | None = None,
) -> dict[str, Any]:
    """The extension is declared, its root opens with the contract's keys, and it owns an
    identity in `asset_prefix`. Returns the root so the caller keeps checking from there."""

    if document.get("asset", {}).get("version") != "2.0":
        raise UnitValidationError("asset.version is not 2.0")
    used = set(document.get("extensionsUsed") or [])
    required = set(document.get("extensionsRequired") or [])
    if extension not in used:
        raise UnitValidationError(f"{extension} is not declared in extensionsUsed")
    if extension not in required:
        raise UnitValidationError(f"{extension} is not declared in extensionsRequired")
    root = (document.get("extensions") or {}).get(extension)
    if not isinstance(root, dict):
        raise UnitValidationError(f"{extension} carries no extension root")
    if tuple(list(root)[: len(ROOT_KEYS)]) != ROOT_KEYS:
        raise UnitValidationError(
            f"{extension} root opens with {list(root)[: len(ROOT_KEYS)]}, not {list(ROOT_KEYS)}"
        )
    if schema_version is not None and root.get("schemaVersion") != schema_version:
        raise UnitValidationError(
            f"{extension} declares schema {root.get('schemaVersion')!r}, not {schema_version!r}"
        )
    identity = root.get("identity")
    if not isinstance(identity, Mapping):
        raise UnitValidationError(f"{extension} carries no identity")
    asset = str(identity.get("asset", ""))
    if not asset.startswith(asset_prefix):
        raise UnitValidationError(f"{extension} identity {asset!r} is outside {asset_prefix!r}")
    if identity.get("sourcePolicy") != SOURCE_POLICY:
        raise UnitValidationError(f"{extension} identity does not resolve {SOURCE_POLICY}")
    if "sourcePath" not in identity and "sourcePaths" not in identity:
        raise UnitValidationError(f"{extension} identity names no source path")
    resolution = root.get("sourceResolution")
    if not isinstance(resolution, Mapping) or resolution.get("policy") != SOURCE_POLICY:
        raise UnitValidationError(f"{extension} does not resolve its members {SOURCE_POLICY}")
    if not isinstance(resolution.get("members"), list):
        raise UnitValidationError(f"{extension} publishes no source member table")
    dependencies = root.get("dependencies")
    if not isinstance(dependencies, list):
        raise UnitValidationError(f"{extension} publishes no dependency table")
    for index, row in enumerate(dependencies):
        _check_dependency(row, index)
    if not isinstance(root.get("coverage"), Mapping):
        raise UnitValidationError(f"{extension} publishes no coverage")
    return root


def validate_ledgers(
    root: Mapping[str, Any],
    source_members: Sequence[SourceMember] | None = None,
) -> None:
    """One ledger row per source member, each re-checked without the writer that produced it."""

    members = list((root.get("sourceResolution") or {}).get("members") or [])
    ledgers = list((root.get("coverage") or {}).get("byteLedger") or [])
    if len(ledgers) != len(members):
        raise UnitValidationError(
            f"{len(ledgers)} byte ledger(s) against {len(members)} source member(s)"
        )
    identities = {str(member.get("path")): member for member in members}
    if len(identities) != len(members):
        raise UnitValidationError("two source members share one path")
    data = {member.path: member.data for member in source_members or ()}
    if source_members is not None and set(data) != set(identities):
        raise UnitValidationError("prepublication source members do not match sourceResolution")
    seen: set[str] = set()
    for row in ledgers:
        if not isinstance(row, Mapping):
            raise UnitValidationError("a byte ledger row is not a record")
        path = str(row.get("sourcePath", ""))
        if path in seen or path not in identities:
            raise UnitValidationError(f"invalid byte ledger source {path!r}")
        seen.add(path)
        identity = identities[path]
        if int(row.get("byteLength", -1)) != int(identity.get("byteLength", -2)):
            raise UnitValidationError(f"{path}: ledger length disagrees with its member")
        if str(row.get("sourceSha256", "")) != str(identity.get("sha256", "")):
            raise UnitValidationError(f"{path}: ledger digest disagrees with its member")
        try:
            verify_ledger_row(row, data.get(path) if source_members is not None else None)
        except ByteLedgerError as error:
            raise UnitValidationError(str(error)) from error


def _forbid_opaque_keys(value: Any, path: str = "$") -> None:
    if isinstance(value, Mapping):
        for key, item in value.items():
            if str(key).lower().replace("_", "").replace("-", "") in OPAQUE_KEYS:
                raise UnitValidationError(f"opaque source payload is forbidden at {path}.{key}")
            _forbid_opaque_keys(item, f"{path}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            _forbid_opaque_keys(item, f"{path}[{index}]")


def _strings(value: Any, path: str = "$"):
    if isinstance(value, Mapping):
        for key, item in value.items():
            yield from _strings(item, f"{path}.{key}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from _strings(item, f"{path}[{index}]")
    elif isinstance(value, str):
        yield path, value


def reject_opaque_source(
    document: Mapping[str, Any],
    binary: bytes,
    source_members: Sequence[SourceMember] | None = None,
) -> None:
    """No part of the product is a verbatim copy of a whole source member.

    The ledger is what makes the absence of a source mirror safe, so the mirror has to actually be
    absent: neither the BIN chunk nor any JSON string may contain a member end to end.
    """

    _forbid_opaque_keys(document)
    for member in source_members or ():
        data = member.data
        if not data:
            continue
        if binary and data in binary:
            raise UnitValidationError(
                f"the BIN chunk embeds the whole of {member.path} ({len(data)} bytes)"
            )
        if len(data) < OPAQUE_JSON_MINIMUM:
            continue
        for where, text in _strings(document):
            if data in text.encode("utf-8", "surrogatepass"):
                raise UnitValidationError(
                    f"{where} embeds the whole of {member.path} ({len(data)} bytes)"
                )


def completeness(root: Mapping[str, Any]) -> dict[str, int]:
    """What the unit could not account for. A complete unit answers zero to all three."""

    coverage = root.get("coverage") or {}
    return {
        "unresolved": len(coverage.get("unresolved") or []),
        "unsupported": len(coverage.get("unsupported") or []),
        "typedUnidentified": len(coverage.get("typedUnidentified") or []),
    }


def _phrase(row: Any) -> str:
    if isinstance(row, Mapping):
        for key in ("reason", "role", "field", "name", "path", "owner"):
            if row.get(key):
                return str(row[key])
        return ", ".join(f"{key}={row[key]!r}" for key in sorted(row))
    return str(row)


def warnings_for(root: Mapping[str, Any]) -> list[str]:
    """What a published unit could not resolve, phrased for the operator.

    A unit that is recoverable only in part publishes what the install holds and warns, so every
    incomplete row and every named departure from the format is spoken once here.
    """

    coverage = root.get("coverage") or {}
    warnings: list[str] = []
    for key, label in (
        ("unresolved", "unresolved"),
        ("unsupported", "unsupported"),
        ("typedUnidentified", "typed but unidentified"),
    ):
        for row in coverage.get(key) or []:
            warnings.append(f"{label}: {_phrase(row)}")
    for key, label in (("omissions", "omitted"), ("anomalies", "anomaly")):
        for row in root.get(key) or []:
            warnings.append(f"{label}: {_phrase(row)}")
    return warnings
