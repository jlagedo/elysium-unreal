"""Semantic records for the two shader-program GLB unit kinds.

One package carries both kinds because they are two faces of one corpus: `materials/dxshaders/`
holds the readable ps.1.x assembly a `shaders/psh/` bundle was compiled from, and the compiled
bundle is cross-checked against it. Each kind keeps its own extension, its own identity namespace
and its own family directory below `shader-programs/`.

The token and instruction rows a compiled bundle publishes are plain dictionaries rather than
dataclasses: one member holds up to half a million tokens, and `dataclasses.asdict` copies every
one of them again on the way out.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import SourceMember, extension_name, normalize_key
from elysium_pipeline.formats.unit_contract import asset_id as contract_asset_id

SHADER_SOURCE_KIND = "shader-source"
SHADER_PROGRAM_KIND = "shader-program"

SHADER_SOURCE_EXTENSION = extension_name(SHADER_SOURCE_KIND)
SHADER_PROGRAM_EXTENSION = extension_name(SHADER_PROGRAM_KIND)

SHADER_SOURCE_ASSET_PREFIX = "vtmb:" + SHADER_SOURCE_KIND + ":"
SHADER_PROGRAM_ASSET_PREFIX = "vtmb:" + SHADER_PROGRAM_KIND + ":"

SCHEMA_VERSION = "1.0.0"

#: Where the readable ps.1.x assembly lives, and the one extension it ships under.
SOURCE_DIR = "materials/dxshaders"
SOURCE_SUFFIX = ".psh"

#: Where the compiled combo bundles live, and the three subdirectories that partition them.
PROGRAM_DIR = "shaders"
PROGRAM_SUFFIX = ".vcs"
PROGRAM_SUBDIRS = ("psh", "vsh", "fxc")

#: The family subdirectory the readable sources publish into, beside `psh/`, `vsh/` and `fxc/`.
SOURCE_FAMILY_DIR = "source"

#: The generator titles the two kinds sign their products with.
SOURCE_GENERATOR_TITLE = "Shader-source"
PROGRAM_GENERATOR_TITLE = "Shader-program"


class ShaderKeyError(ValueError):
    """A unit key does not name a member of this seam's corpus."""


def normalize_source_key(key: str) -> str:
    """The `shader-source` unit key: a bare stem, folded to lower case.

    The singular command tolerates the root prefix and the source extension, so
    `materials/dxshaders/Eyes.psh`, `dxshaders/eyes` and `eyes` all name one unit.
    """

    normalized = normalize_key(key).strip().strip('"')
    if normalized.endswith(SOURCE_SUFFIX):
        normalized = normalized[: -len(SOURCE_SUFFIX)]
    for prefix in (SOURCE_DIR + "/", "dxshaders/"):
        if normalized.startswith(prefix):
            normalized = normalized[len(prefix):]
    normalized = normalized.strip("/")
    if not normalized or "/" in normalized:
        raise ShaderKeyError(f"invalid shader-source key {key!r}")
    return normalized


def normalize_program_key(key: str) -> str:
    """The `shader-program` unit key: `<subdir>/<stem>`, folded to lower case.

    The `.vcs` extension is dropped -- one kind, one extension -- and the `shaders/` root is
    tolerated on the way in, so `shaders/psh/LightmappedGeneric.vcs` and `psh/lightmappedgeneric`
    name one unit.
    """

    normalized = normalize_key(key).strip().strip('"')
    if normalized.endswith(PROGRAM_SUFFIX):
        normalized = normalized[: -len(PROGRAM_SUFFIX)]
    if normalized.startswith(PROGRAM_DIR + "/"):
        normalized = normalized[len(PROGRAM_DIR) + 1:]
    normalized = normalized.strip("/")
    parts = normalized.split("/")
    if len(parts) != 2 or not parts[1]:
        raise ShaderKeyError(f"invalid shader-program key {key!r}")
    if parts[0] not in PROGRAM_SUBDIRS:
        raise ShaderKeyError(
            f"shader-program key {key!r} names subdirectory {parts[0]!r}, "
            f"not one of {', '.join(PROGRAM_SUBDIRS)}"
        )
    return normalized


def source_asset_id(key: str) -> str:
    return contract_asset_id(SHADER_SOURCE_KIND, normalize_source_key(key))


def program_asset_id(key: str) -> str:
    return contract_asset_id(SHADER_PROGRAM_KIND, normalize_program_key(key))


def source_member_path(key: str) -> str:
    return f"{SOURCE_DIR}/{normalize_source_key(key)}{SOURCE_SUFFIX}"


def program_member_path(key: str) -> str:
    return f"{PROGRAM_DIR}/{normalize_program_key(key)}{PROGRAM_SUFFIX}"


def source_output_relative_path(key: str) -> PurePosixPath:
    """`source/<stem>.glb`, below the seam's `shader-programs/` family root."""

    return PurePosixPath(SOURCE_FAMILY_DIR) / (normalize_source_key(key) + ".glb")


def program_output_relative_path(key: str) -> PurePosixPath:
    """`<subdir>/<stem>.glb`, below the seam's `shader-programs/` family root."""

    return PurePosixPath(normalize_program_key(key) + ".glb")


@dataclass(frozen=True, slots=True)
class SourceLine:
    """One physical line of a `.psh`, as a byte span of the member.

    `length` spans the terminator, so the lines partition the file. `comment_offset` is where the
    line's `;` or `//` marker sits, which is the boundary the byte ledger cuts the line at.
    """

    index: int
    offset: int
    length: int
    text: str
    ending: str
    code: str
    marker: str | None
    comment_offset: int | None
    comment: str | None
    kind: str

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {
            "index": self.index,
            "sourceOffset": self.offset,
            "byteLength": self.length,
            "text": self.text,
            "ending": self.ending,
            "kind": self.kind,
        }
        if self.marker is not None:
            row["comment"] = {
                "marker": self.marker,
                "sourceOffset": self.comment_offset,
                "text": self.comment,
            }
        return row


@dataclass(frozen=True, slots=True)
class Register:
    """One register reference, in the ps.1.x register vocabulary."""

    register_class: str
    index: int

    def to_json(self) -> dict[str, Any]:
        return {"registerClass": self.register_class, "registerIndex": self.index}


@dataclass(frozen=True, slots=True)
class Destination:
    register: Register
    write_mask: str
    text: str

    def to_json(self) -> dict[str, Any]:
        row = self.register.to_json()
        row["writeMask"] = self.write_mask
        row["text"] = self.text
        return row


@dataclass(frozen=True, slots=True)
class Operand:
    """One source operand: the register, its selector and the modifiers spelled on it."""

    register: Register
    selector: str | None
    negate: bool
    complement: bool
    modifier: str | None
    text: str

    def to_json(self) -> dict[str, Any]:
        row = self.register.to_json()
        row["selector"] = self.selector
        row["negate"] = self.negate
        row["complement"] = self.complement
        row["modifier"] = self.modifier
        row["text"] = self.text
        return row


@dataclass(frozen=True, slots=True)
class Instruction:
    """One parsed instruction line of a `.psh`."""

    line: int
    source_offset: int
    co_issued: bool
    co_issue_marker: str | None
    opcode: str
    modifiers: tuple[str, ...]
    destination: Destination | None
    sources: tuple[Operand, ...]
    text: str

    def to_json(self) -> dict[str, Any]:
        return {
            "line": self.line,
            "sourceOffset": self.source_offset,
            "coIssued": self.co_issued,
            "coIssueMarker": self.co_issue_marker,
            "opcode": self.opcode,
            "modifiers": list(self.modifiers),
            "destination": self.destination.to_json() if self.destination else None,
            "sources": [operand.to_json() for operand in self.sources],
            "text": self.text,
        }


@dataclass(frozen=True, slots=True)
class Define:
    """One `def` line: the constant register and the four floats it declares."""

    line: int
    source_offset: int
    register: Register
    values: tuple[float, float, float, float]
    text: str

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {"line": self.line, "sourceOffset": self.source_offset}
        row.update(self.register.to_json())
        row["values"] = list(self.values)
        row["text"] = self.text
        return row


@dataclass(frozen=True, slots=True)
class ShaderVersion:
    major: int
    minor: int
    text: str
    line: int
    source_offset: int

    def to_json(self) -> dict[str, Any]:
        return {
            "major": self.major,
            "minor": self.minor,
            "text": self.text,
            "line": self.line,
            "sourceOffset": self.source_offset,
        }


@dataclass(slots=True)
class ShaderSourceModel:
    """Everything one `vtmb:shader-source:` unit publishes."""

    key: str
    asset_id: str
    members: tuple[SourceMember, ...]
    version: ShaderVersion | None
    defines: list[Define]
    instructions: list[Instruction]
    comments: list[dict[str, Any]]
    lines: list[SourceLine]
    encoding: str
    compiled_twin: dict[str, Any]
    dependencies: list[dict[str, Any]] = field(default_factory=list)
    anomalies: list[dict[str, Any]] = field(default_factory=list)
    omissions: list[dict[str, Any]] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
    byte_ledger: list[dict[str, Any]] = field(default_factory=list)

    @property
    def shader_model(self) -> str | None:
        """`ps_<major>_<minor>`, or None where the member holds no version line to read."""

        if self.version is None:
            return None
        return f"ps_{self.version.major}_{self.version.minor}"

    def source_block(self) -> dict[str, Any]:
        return {
            "encoding": self.encoding,
            "version": None if self.version is None else self.version.to_json(),
            "defines": [row.to_json() for row in self.defines],
            "instructions": [row.to_json() for row in self.instructions],
            "comments": list(self.comments),
            "lines": [row.to_json() for row in self.lines],
        }


@dataclass(slots=True)
class ShaderProgramModel:
    """Everything one `vtmb:shader-program:` unit publishes."""

    key: str
    asset_id: str
    members: tuple[SourceMember, ...]
    header: dict[str, Any] | None
    combo_table: dict[str, Any] | None
    combos: list[dict[str, Any]]
    source_comparison: dict[str, Any] | None
    readable_source: dict[str, Any]
    dependencies: list[dict[str, Any]] = field(default_factory=list)
    anomalies: list[dict[str, Any]] = field(default_factory=list)
    omissions: list[dict[str, Any]] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
    byte_ledger: list[dict[str, Any]] = field(default_factory=list)
