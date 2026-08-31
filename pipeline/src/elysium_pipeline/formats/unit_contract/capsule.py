"""The source capsule: the exact winning source bytes, carried inside the unit that decoded them.

An `export_v2` unit is a self-contained capsule. Alongside the full decode it carries, byte for
byte, the source member the UP-first policy selected, so a reader holding one GLB holds everything
the exporter read and can reproduce the install file without the install. The capsule never
excuses the decode: the byte ledger and the independent re-decode stay mandatory, and the capsule
is checked against `sourceResolution`'s own `byteLength`/`sha256` on every read.

The encoding is the plainest one glTF admits. Each member's bytes are appended to buffer 0 -- the
BIN chunk -- at a four-byte aligned offset, one `bufferView` addresses them, and the member's row
in `sourceResolution.members[]` gains

```json
"capsule": {"bufferView": 3, "byteLength": 4096}
```

A zero-byte member declares `{"byteLength": 0}` and no view, because glTF has no zero-length
`bufferView` and an empty file has nothing to address. A unit all of whose members are empty
therefore still carries no BIN chunk at all.

`sourceResolution.capsule` is what says the seam has adopted the rule; a seam that has not
publishes no such key and is validated exactly as before (`seam_map_unit_contract.md`, "Source
capsule", owns the rollout).
"""

from __future__ import annotations

import hashlib
from typing import Any, Iterable, Mapping, Sequence

from elysium_pipeline.formats.unit_contract.origin import SOURCE_POLICY, SourceMember

#: The one capsule encoding: the member's bytes, unaltered and uncompressed.
CAPSULE_ENCODING = "raw"

#: glTF requires a `bufferView` to start on a four-byte boundary, so capsules are packed to it.
CAPSULE_ALIGNMENT = 4


class CapsuleError(ValueError):
    """A unit's capsule does not carry the member it names."""


def capsule_declaration() -> dict[str, str]:
    """The `sourceResolution.capsule` block a seam that has adopted the capsule publishes."""

    return {"encoding": CAPSULE_ENCODING}


def declares_capsule(resolution: Mapping[str, Any] | None) -> bool:
    """Whether a `sourceResolution` block says its members are capsuled."""

    return isinstance(resolution, Mapping) and resolution.get("capsule") is not None


def encapsulate(
    members: Sequence[SourceMember] | Iterable[SourceMember],
    *,
    binary: bytes = b"",
    buffer_views: Sequence[Mapping[str, Any]] = (),
) -> tuple[dict[str, Any], list[dict[str, Any]], bytes]:
    """`(sourceResolution, bufferViews, BIN payload)` for a unit that publishes its sources.

    `binary` and `buffer_views` are whatever the seam's own decode already put in the BIN chunk;
    the capsules are appended after them and the existing views keep their indices, so a seam with
    accessors adopts the capsule without renumbering anything it already emitted.

    The returned payload is unpadded: `buffers[0].byteLength` is its length, and the container
    pads the chunk itself.
    """

    payload = bytearray(binary)
    views = [dict(view) for view in buffer_views]
    rows: list[dict[str, Any]] = []
    for member in members:
        row = member.to_json()
        data = bytes(member.data)
        if data:
            payload.extend(b"\0" * (-len(payload) % CAPSULE_ALIGNMENT))
            offset = len(payload)
            payload.extend(data)
            views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data)})
            row["capsule"] = {"bufferView": len(views) - 1, "byteLength": len(data)}
        else:
            row["capsule"] = {"byteLength": 0}
        rows.append(row)
    resolution = {
        "policy": SOURCE_POLICY,
        "capsule": capsule_declaration(),
        "members": rows,
    }
    return resolution, views, bytes(payload)


def buffer_table(payload: bytes) -> list[dict[str, int]]:
    """The one-entry `buffers` array a BIN chunk needs, or none when there is no chunk."""

    return [{"byteLength": len(payload)}] if payload else []


def extract_source_member(
    document: Mapping[str, Any], binary: bytes, member: Mapping[str, Any]
) -> bytes:
    """The exact source bytes one `sourceResolution.members[]` row's capsule carries.

    Reads with no tolerance: a row whose capsule is absent, mis-addressed or the wrong length is
    an error rather than an empty result, because a caller deploying these bytes has no other
    place to learn that the unit did not carry them.
    """

    if not isinstance(member, Mapping):
        raise CapsuleError("a source member row is not a record")
    path = str(member.get("path", "?"))
    capsule = member.get("capsule")
    if not isinstance(capsule, Mapping):
        raise CapsuleError(f"{path}: the unit carries no source capsule")
    declared = capsule.get("byteLength")
    if not isinstance(declared, int) or isinstance(declared, bool) or declared < 0:
        raise CapsuleError(f"{path}: the capsule declares no byte length")
    index = capsule.get("bufferView")
    if index is None:
        if declared:
            raise CapsuleError(f"{path}: a {declared}-byte capsule names no bufferView")
        return b""
    views = list(document.get("bufferViews") or [])
    if not isinstance(index, int) or isinstance(index, bool) or not 0 <= index < len(views):
        raise CapsuleError(f"{path}: the capsule names no bufferView of this unit")
    view = views[index]
    if int(view.get("buffer", -1)) != 0:
        raise CapsuleError(f"{path}: the capsule's bufferView does not use buffer 0")
    offset = int(view.get("byteOffset", 0))
    length = int(view.get("byteLength", -1))
    if length != declared:
        raise CapsuleError(
            f"{path}: the capsule declares {declared} bytes and its view offers {length}"
        )
    if offset < 0 or length < 0 or offset + length > len(binary):
        raise CapsuleError(f"{path}: the capsule runs outside the BIN chunk")
    return bytes(binary[offset:offset + length])


def source_capsules(
    document: Mapping[str, Any], binary: bytes, root: Mapping[str, Any]
) -> dict[str, bytes]:
    """Every capsuled member of one unit, keyed by its install-relative source path.

    Each member's bytes are weighed against the `byteLength` and `sha256` the unit published for
    it, so a caller that writes what this returns is writing bytes the unit proved it holds.
    """

    resolution = root.get("sourceResolution") or {}
    if not declares_capsule(resolution):
        raise CapsuleError("the unit declares no source capsule")
    extracted: dict[str, bytes] = {}
    for member in resolution.get("members") or ():
        data = extract_source_member(document, binary, member)
        path = str(member.get("path", "?"))
        if len(data) != int(member.get("byteLength", -1)):
            raise CapsuleError(
                f"{path}: the capsule holds {len(data)} of "
                f"{member.get('byteLength')} declared bytes"
            )
        if hashlib.sha256(data).hexdigest() != str(member.get("sha256", "")):
            raise CapsuleError(f"{path}: the capsule's bytes are not the member it names")
        extracted[path] = data
    return extracted
