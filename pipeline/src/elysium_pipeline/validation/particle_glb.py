"""Independent structural validator for particle-definition GLB products.

Standalone validation checks the container and the extension root's shape without the install
present. Export-time validation additionally slices the source bytes each `keys[]`/`blocks[]`
byte-ledger owner claims and re-tokenizes that slice alone (`_verify_owner_ranges`), which is
independent of the decoder's own offset arithmetic; and re-decodes the selected source member
through the same `formats.particle_glb.decode_particle` the writer calls, using the document's own
`dependencies` rows (rather than the install) to answer what a child or sprite resolved against,
and compares the result field by field (`_redecode_and_compare`) -- so a decoder that drifted from
what it wrote is caught here, never by re-running `exporters.particle_glb.build_document`.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from elysium_pipeline.formats.particle_glb import (
    PARTICLE_EXTENSION,
    SCHEMA_VERSION,
    decode_particle,
    lexer,
    material_asset_id,
    sprite_asset_id,
)
from elysium_pipeline.formats.particle_glb.model import BLOCK_KINDS as _BLOCK_KINDS
from elysium_pipeline.formats.particle_glb.model import asset_id as particle_asset_id
from elysium_pipeline.formats.particle_glb.source import ParticleSourceClosure
from elysium_pipeline.formats.unit_contract import (
    SourceMember,
    UnitValidationError,
    completeness,
    read_glb as _read_glb,
    reject_opaque_source,
    validate_accessors,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)

ANOMALY_ROLES = frozenset(
    {"unbalanced-braces", "repeated-scalar-key", "root-not-particle", "empty-definition",
     "frames-zero-on-wrapper", "unterminated-quoted-string", "empty-key"}
)
OMISSION_ROLES = frozenset(
    {"unparsed-region", "empty-member", "keyvalues-insignificant-whitespace"}
)
DEPENDENCY_ROLES = frozenset({"particle", "image", "material"})
ROLE_VALUES = frozenset({"drawing", "emitter", "both", "neither"})
VALUE_KINDS = frozenset({"scalar", "range", "ramp", "text"})
BLOCK_KINDS = frozenset(_BLOCK_KINDS)


class ParticleGlbValidationError(UnitValidationError):
    pass


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    return _read_glb(path)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ParticleGlbValidationError(message)


def _check_keys(keys: Any, block_count: int) -> None:
    _require(isinstance(keys, list), "the unit declares no keys table")
    for position, row in enumerate(keys):
        _require(isinstance(row, dict), f"keys[{position}] is not a record")
        _require(row.get("index") == position, f"keys[{position}] is out of source order")
        block = row.get("block")
        _require(
            block is None or (isinstance(block, int) and 0 <= block < block_count),
            f"keys[{position}] names no block of this unit",
        )
        key = row.get("key")
        # An empty quoted key (`"" "1"`) is a real, if malformed, pairing the lexer still resolves
        # cleanly (named by an `empty-key` anomaly row); this seam never rejects a source on its
        # spelling alone, only on the shape of the record.
        _require(isinstance(key, str), f"keys[{position}] has no key")
        source_key = row.get("sourceKey")
        _require(
            isinstance(source_key, str) and source_key.strip().lower() == key,
            f"keys[{position}] key disagrees with its source spelling",
        )
        _require(isinstance(row.get("value"), str), f"keys[{position}] has no value")
        _require(
            isinstance(row.get("offset"), int) and row["offset"] >= 0,
            f"keys[{position}] has no source offset",
        )
        _require(
            isinstance(row.get("length"), int) and row["length"] > 0,
            f"keys[{position}] has no source length",
        )
        _require(isinstance(row.get("quotedKey"), bool), f"keys[{position}] does not state key quoting")
        _require(
            isinstance(row.get("quotedValue"), bool), f"keys[{position}] does not state value quoting"
        )
        parsed = row.get("parsed")
        _require(isinstance(parsed, dict), f"keys[{position}] carries no parsed value")
        _require(parsed.get("kind") in VALUE_KINDS, f"keys[{position}].parsed has no valid kind")
        _require(
            isinstance(parsed.get("values"), list) and isinstance(parsed.get("positions"), list)
            and len(parsed["values"]) == len(parsed["positions"]),
            f"keys[{position}].parsed values and positions disagree in length",
        )


def _check_blocks(blocks: Any, key_count: int) -> None:
    _require(isinstance(blocks, list), "the unit declares no blocks table")
    for position, row in enumerate(blocks):
        _require(isinstance(row, dict), f"blocks[{position}] is not a record")
        _require(row.get("index") == position, f"blocks[{position}] is out of source order")
        _require(row.get("kind") in BLOCK_KINDS, f"blocks[{position}] has no recognized kind")
        parent = row.get("parent")
        _require(
            parent is None or (isinstance(parent, int) and 0 <= parent < position),
            f"blocks[{position}] names no earlier block as its parent",
        )
        for index in row.get("keys") or []:
            _require(
                isinstance(index, int) and 0 <= index < key_count,
                f"blocks[{position}] names no key of this unit",
            )


def _check_dependencies(dependencies: Any) -> None:
    seen: set[str] = set()
    for position, row in enumerate(dependencies):
        _require(isinstance(row, dict), f"dependencies[{position}] is not a record")
        role = row.get("role")
        _require(role in DEPENDENCY_ROLES, f"dependencies[{position}] has an unknown role {role!r}")
        asset = str(row.get("asset") or "")
        prefix = {"particle": "vtmb:particle:", "image": "vtmb:image:", "material": "vtmb:material:"}
        _require(
            asset.startswith(prefix[role]), f"dependencies[{position}] asset disagrees with its role"
        )
        _require(asset not in seen, f"dependencies[{position}] repeats {asset}")
        seen.add(asset)


def _dependency_lookup(dependencies: list[dict[str, Any]], role: str):
    resolved = {
        str(row["asset"]): bool(row["resolved"]) for row in dependencies if row.get("role") == role
    }

    def exists(key: str) -> bool:
        if role == "particle":
            asset = particle_asset_id(key)
        elif role == "image":
            asset = sprite_asset_id(key)
        else:
            asset = material_asset_id(key)
        return resolved.get(asset, False)

    return exists


def _verify_owner_ranges(root: dict[str, Any], member: SourceMember) -> None:
    """Ground truth, not a second opinion: slice the source bytes a `keys[i].key`/`.value` or
    `blocks[i].name` owner claims and re-tokenize that slice alone, independent of whatever offset
    arithmetic the decoder used to publish the row. `_redecode_and_compare` re-runs the same
    decoder over the same bytes, so a systematic offset bug in `lexer`/`decode_particle` would
    reproduce identically in both and never be caught there; this check instead answers whether the
    bytes actually sitting at the row's own `offset`/`length` are the token the row claims."""

    data = member.data
    ledger_rows = ((root.get("coverage") or {}).get("byteLedger") or [{}])[0].get("ranges") or []
    by_owner: dict[str, list[dict[str, Any]]] = {}
    for row in ledger_rows:
        by_owner.setdefault(str(row.get("owner")), []).append(row)

    def _slice(owner: str) -> bytes | None:
        ranges = by_owner.get(owner)
        if not ranges or len(ranges) != 1:
            return None
        span = ranges[0]
        offset, length = int(span["offset"]), int(span["length"])
        return data[offset:offset + length]

    def _only_string_token(raw: bytes):
        tokens = [t for t in lexer.tokenize(lexer.decode_text(raw)) if t.kind == "string"]
        return tokens[0] if len(tokens) == 1 else None

    for key_row in root.get("keys") or []:
        index = key_row["index"]
        key_bytes = _slice(f"keys[{index}].key")
        value_bytes = _slice(f"keys[{index}].value")
        _require(key_bytes is not None, f"keys[{index}].key owns no single byte range")
        _require(value_bytes is not None, f"keys[{index}].value owns no single byte range")
        key_token = _only_string_token(key_bytes)
        value_token = _only_string_token(value_bytes)
        _require(
            key_token is not None and key_token.text.strip().lower() == key_row.get("key"),
            f"keys[{index}].key's own byte range does not retokenize to its published key",
        )
        _require(
            value_token is not None and value_token.text == key_row.get("value"),
            f"keys[{index}].value's own byte range does not retokenize to its published value",
        )

    for block_row in root.get("blocks") or []:
        index = block_row["index"]
        name_bytes = _slice(f"blocks[{index}].name")
        _require(name_bytes is not None, f"blocks[{index}].name owns no single byte range")
        name_token = _only_string_token(name_bytes)
        _require(
            name_token is not None and name_token.text.strip().lower() == block_row.get("kind"),
            f"blocks[{index}].name's own byte range does not retokenize to its published kind",
        )


def _redecode_and_compare(root: dict[str, Any], member: SourceMember) -> None:
    identity = root["identity"]
    closure = ParticleSourceClosure(
        key=str(identity.get("key", "")),
        asset_id=str(identity["asset"]),
        member=member,
    )
    dependencies = root.get("dependencies") or []
    model = decode_particle(
        closure,
        particle_exists=_dependency_lookup(dependencies, "particle"),
        sprite_exists=_dependency_lookup(dependencies, "image"),
        material_exists=_dependency_lookup(dependencies, "material"),
    )
    _require(model.root == root.get("root"), "a fresh decode disagrees on the root token")
    _require(model.role == root.get("role"), "a fresh decode disagrees on the unit's role")
    _require(
        model.precipitation == root.get("precipitation"),
        "a fresh decode disagrees on precipitation",
    )
    fresh_keys = [
        {
            "index": entry.index, "block": entry.block, "key": entry.key,
            "sourceKey": entry.source_key, "value": entry.value, "parsed": entry.parsed.to_json(),
            "offset": entry.offset, "length": entry.length,
            "quotedKey": entry.quoted_key, "quotedValue": entry.quoted_value,
        }
        for entry in model.keys
    ]
    _require(fresh_keys == root.get("keys"), "a fresh decode disagrees on the keys table")
    fresh_blocks = [
        {
            "index": block.index, "kind": block.kind, "parent": block.parent,
            "offset": block.offset, "length": block.length, "keys": list(block.keys),
        }
        for block in model.blocks
    ]
    _require(fresh_blocks == root.get("blocks"), "a fresh decode disagrees on the blocks table")
    _require(model.projection == root.get("projection"), "a fresh decode disagrees on projection")
    _require(model.comments == root.get("comments"), "a fresh decode disagrees on comments")
    _require(model.dependencies == dependencies, "a fresh decode disagrees on dependencies")
    _require(model.anomalies == root.get("anomalies"), "a fresh decode disagrees on anomalies")
    _require(model.omissions == root.get("omissions"), "a fresh decode disagrees on omissions")
    coverage = root.get("coverage") or {}
    _require(
        model.typed_unidentified == coverage.get("typedUnidentified"),
        "a fresh decode disagrees on typedUnidentified",
    )
    fresh_ledger = model.byte_ledger[0]
    published_ledger = (coverage.get("byteLedger") or [{}])[0]
    _require(
        fresh_ledger.get("ranges") == published_ledger.get("ranges"),
        "a fresh decode partitions the source bytes differently",
    )


def validate_document(
    document: dict[str, Any], binary: bytes, *, source_members=None
) -> dict[str, Any]:
    root = validate_extension_root(
        document, PARTICLE_EXTENSION, asset_prefix="vtmb:particle:", schema_version=SCHEMA_VERSION
    )
    validate_container(document, binary)
    validate_sceneless(document)
    validate_accessors(document, binary)
    validate_ledgers(root, source_members)
    reject_opaque_source(document, binary, source_members)

    members = (root.get("sourceResolution") or {}).get("members") or []
    _require(len(members) == 1 and members[0].get("role") == "definition",
              "a particle unit owns exactly one definition member")

    root_token = root.get("root")
    _require(isinstance(root_token, str), "the unit carries no root spelling")
    _check_keys(root.get("keys"), len(root.get("blocks") or []))
    _check_blocks(root.get("blocks"), len(root.get("keys") or []))
    _require(root.get("role") in ROLE_VALUES, "the unit carries no recognized role")
    _require(isinstance(root.get("precipitation"), bool), "the unit carries no precipitation flag")
    _require(isinstance(root.get("projection"), dict), "the unit carries no projection")
    dependencies = root.get("dependencies")
    _require(isinstance(dependencies, list), "the unit declares no dependency table")
    _check_dependencies(dependencies)
    for row in root.get("anomalies") or []:
        _require(isinstance(row, dict) and row.get("role") in ANOMALY_ROLES,
                  f"unknown source anomaly {row!r}")
    for row in root.get("omissions") or []:
        _require(isinstance(row, dict) and row.get("role") in OMISSION_ROLES,
                  f"unknown source omission {row!r}")

    counts = completeness(root)
    _require(counts["unresolved"] == 0, "the unit carries unresolved coverage rows")
    _require(counts["unsupported"] == 0, "the unit carries unsupported coverage rows")

    if source_members is not None:
        member = next((m for m in source_members if m.role == "definition"), source_members[0])
        _verify_owner_ranges(root, member)
        _redecode_and_compare(root, member)

    return {
        "asset": root["identity"]["asset"],
        "key": root["identity"].get("key"),
        "root": root_token,
        "role": root.get("role"),
        "keys": len(root.get("keys") or []),
        "blocks": len(root.get("blocks") or []),
        "dependencies": len(dependencies),
        "anomalies": [str(row.get("role")) for row in root.get("anomalies") or []],
        "omissions": [str(row.get("role")) for row in root.get("omissions") or []],
        "typedUnidentified": counts["typedUnidentified"],
        "unresolved": counts["unresolved"],
        "unsupported": counts["unsupported"],
        "unresolvedDependencies": sorted(
            str(row.get("sourcePath")) for row in dependencies if not row.get("resolved")
        ),
        "byteCoveragePercent": (root["coverage"]["byteLedger"][0]["coveragePercent"]),
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    """What a published unit could not resolve, phrased for the operator."""

    warnings: list[str] = []
    missing = summary.get("unresolvedDependencies") or []
    if missing:
        shown = ", ".join(missing[:4])
        more = f" and {len(missing) - 4} more" if len(missing) > 4 else ""
        warnings.append(f"the install carries no member for {shown}{more}")
    for role in summary.get("anomalies") or []:
        warnings.append(f"anomaly: {role}")
    for role in summary.get("omissions") or []:
        warnings.append(f"omitted: {role}")
    if summary.get("typedUnidentified"):
        warnings.append(f"typed but unidentified: {summary['typedUnidentified']} key(s)")
    return warnings
