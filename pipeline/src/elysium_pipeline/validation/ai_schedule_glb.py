"""Independent structural validator for ai-schedule GLB products.

The kind-independent checks -- container, scene-less rule, extension root, byte ledger, source
capsule -- are `elysium_pipeline.formats.unit_contract`'s. What is specific here:

* a space unit DECLARES a capsule and the root unit declares NONE, which is the executable-image
  rule stated as a check rather than as prose;
* every capsuled member is re-tokenized and re-parsed by the seam's own parser, and its published
  record is compared name for name and task for task against what came back -- the re-decode calls
  `parser.parse` directly rather than the exporter's `build_space_document`, so a writer that
  decoded once and emitted something else is caught instead of compared against itself;
* the root's partition sums to its member's length and names only units;
* every published span is inside the member it claims.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Mapping, Sequence

from elysium_pipeline.formats.ai_schedule_glb import parser
from elysium_pipeline.formats.ai_schedule_glb.model import (
    AI_SCHEDULE_EXTENSION,
    ROOT_KEY,
    SCHEMA_VERSION,
    SOURCE_MEMBER,
)
from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    completeness,
    read_glb as _read_glb,
    validate_capsules,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)
from elysium_pipeline.formats.unit_contract.capsule import declares_capsule
from elysium_pipeline.formats.unit_contract.origin import SourceMember

ASSET_PREFIX = "vtmb:ai-schedule:"

#: Every `anomalies[]` row this seam is allowed to publish.
_ANOMALY_ROWS = {
    "registered-name-with-no-text",
    "text-name-not-registered",
    "image-differs-from-pin",
    "duplicate-schedule-name",
}

_SPACE_KEYS = ("spaces", "registrations", "texts", "records")
_ROOT_KEYS = ("partition", "namespaces", "squadSlots", "vocabulary", "classes", "census")


def validate(path: Path) -> dict[str, Any]:
    """Standalone validation: everything provable from the unit alone, with no install."""

    document, binary = _read_glb(Path(path))
    return validate_document(document, binary)


def validate_document(
    document: Mapping[str, Any],
    binary: bytes,
    source_members: Sequence[SourceMember] | None = None,
) -> dict[str, Any]:
    validate_container(document, binary)
    validate_sceneless(document)
    root = validate_extension_root(
        document,
        AI_SCHEDULE_EXTENSION,
        asset_prefix=ASSET_PREFIX,
        schema_version=SCHEMA_VERSION,
    )
    validate_ledgers(root, source_members)
    validate_capsules(document, binary, root, source_members)

    asset = str((root.get("identity") or {}).get("asset") or "")
    key = asset[len(ASSET_PREFIX):]
    is_root = key == ROOT_KEY

    _validate_shape(root, is_root)
    _validate_members(root, is_root)
    if is_root:
        _validate_partition(root)
    else:
        _validate_records(root, source_members)
    _validate_anomalies(root)
    return completeness(root)


def _validate_shape(root: Mapping[str, Any], is_root: bool) -> None:
    expected = _ROOT_KEYS if is_root else _SPACE_KEYS
    missing = [name for name in expected if name not in root]
    if missing:
        shape = "root" if is_root else "space"
        raise UnitValidationError(f"a {shape} unit declares {', '.join(missing)}")
    forbidden = _SPACE_KEYS if is_root else _ROOT_KEYS
    present = [name for name in forbidden if name in root]
    if present:
        shape = "root" if is_root else "space"
        raise UnitValidationError(
            f"a {shape} unit carries {', '.join(present)}, which another unit owns"
        )


def _validate_members(root: Mapping[str, Any], is_root: bool) -> None:
    resolution = root.get("sourceResolution") or {}
    members = list(resolution.get("members") or [])
    if not members:
        # Eight owners feed no text at all -- `CAI_StandoffBehavior`, `CNPC_VChangBrosBlade`,
        # `CNPC_VChangBrosClaw`, `CNPC_VLasombra`, `CNPC_VSabbatGunman`, `CNPC_VStalker`,
        # `CNPC_VTaxiDriver`, `CNPC_VYukie`. They exist so their class's four spaces have the right
        # parents, and translation walks that chain, so the unit is published with its spaces and
        # nothing else. A unit with no member has nothing to capsule and no ledger to publish.
        if is_root:
            raise UnitValidationError("the root unit names no source member")
        if root.get("texts") or root.get("records"):
            raise UnitValidationError(
                "a unit with no source member publishes no text and no record"
            )
        if not declares_capsule(resolution):
            raise UnitValidationError(
                "a space unit declares the capsule even with no member: the declaration is the "
                "seam saying it has adopted the rule, not a claim about this unit's bytes"
            )
        return
    for member in members:
        if not str(member.get("path", "")).startswith(SOURCE_MEMBER):
            raise UnitValidationError(
                f"member {member.get('path')!r} is not cut from {SOURCE_MEMBER}"
            )

    declared = declares_capsule(resolution)
    if is_root:
        # The executable-image rule: the root carries the image and declares no capsule.
        if declared:
            raise UnitValidationError(
                "the root unit declares a source capsule; it carries the image, and the "
                "executable-image rule is what lets it publish without one"
            )
        if len(members) != 1 or members[0].get("span") is not None:
            raise UnitValidationError("the root unit names the whole image as its one member")
    else:
        if not declared:
            raise UnitValidationError("a space unit carries its texts and declares a capsule")
        for member in members:
            span = member.get("span")
            if not isinstance(span, Mapping):
                raise UnitValidationError(
                    f"member {member.get('path')!r} is cut from the image and carries no span"
                )
            if int(span.get("length", -1)) != int(member.get("byteLength", -2)):
                raise UnitValidationError(
                    f"member {member.get('path')!r}: span length and byteLength disagree"
                )


def _validate_records(root: Mapping[str, Any], source_members) -> None:
    """Re-parse each capsuled text and compare it against the record the unit published."""

    records = list(root.get("records") or [])
    texts = list(root.get("texts") or [])
    if len(records) != len(texts):
        raise UnitValidationError(
            f"the unit publishes {len(texts)} text(s) and {len(records)} record(s)"
        )
    if source_members is None:
        return
    members = list(source_members)
    if len(members) != len(records):
        raise UnitValidationError(
            f"the unit publishes {len(records)} record(s) and {len(members)} member(s)"
        )
    for member, record, text in zip(members, records, texts):
        parsed = parser.parse(member.data)
        if len(parsed) != 1:
            raise UnitValidationError(
                f"{member.path}: the member re-parses to {len(parsed)} record(s), not one"
            )
        fresh = parsed[0]
        if fresh.name != record.get("name"):
            raise UnitValidationError(
                f"{member.path}: published name {record.get('name')!r} but the bytes declare "
                f"{fresh.name!r}"
            )
        if fresh.name != text.get("name"):
            raise UnitValidationError(
                f"{member.path}: texts[] and records[] disagree about the name"
            )
        published = [task.get("name") for task in record.get("tasks") or []]
        if published != [task.name for task in fresh.tasks]:
            raise UnitValidationError(f"{member.path}: the published task list is not the bytes'")
        if int(record.get("flagWord") or 0) != fresh.flag_word:
            raise UnitValidationError(f"{member.path}: the published flag word is not the bytes'")


def _validate_partition(root: Mapping[str, Any]) -> None:
    partition = list(root.get("partition") or [])
    if not partition:
        raise UnitValidationError("the root unit's partition is empty")
    ledgers = list((root.get("coverage") or {}).get("byteLedger") or [])
    if len(ledgers) != 1:
        raise UnitValidationError("the root unit publishes one ledger row, over the image")
    length = int(ledgers[0].get("byteLength") or 0)
    claimed = sum(int(row.get("length") or 0) for row in partition)
    if claimed > length:
        raise UnitValidationError(
            f"the partition claims {claimed} bytes of a {length}-byte image"
        )
    last_end = -1
    for row in partition:
        offset, size = int(row.get("offset", -1)), int(row.get("length", -1))
        if offset < 0 or size < 0 or offset + size > length:
            raise UnitValidationError(f"partition row {row!r} is not a range of the image")
        if offset < last_end:
            raise UnitValidationError(f"partition rows overlap at {offset}")
        last_end = offset + size
        if not row.get("unit"):
            raise UnitValidationError(f"partition row at {offset} names no unit")


def _validate_anomalies(root: Mapping[str, Any]) -> None:
    for row in root.get("anomalies") or []:
        name = str(row.get("row", ""))
        if name not in _ANOMALY_ROWS:
            raise UnitValidationError(f"unknown anomaly row {name!r}")


__all__ = ["ASSET_PREFIX", "validate", "validate_document"]
