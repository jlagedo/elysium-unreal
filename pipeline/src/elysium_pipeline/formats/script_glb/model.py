"""Identity and semantic records for one VtMB level-script unit.

One `python/<path>.py` is one unit, joined by its compiled `.pyc` twin where the install ships
one; a `.pyc` with no source sibling is a unit on its own. The key is the path below `python/`
with the extension dropped, so the two members of one script answer to one identity.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import (
    SourceMember,
    asset_id as _asset_id,
    extension_name,
    normalize_key,
)

KIND = "script"
KIND_TITLE = "Script"
SCRIPT_EXTENSION = extension_name(KIND)
SCHEMA_VERSION = "1.0.0"

#: The install root the seam reads. The family directory it publishes into is the caller's,
#: exactly as every shipped seam has it: `output_relative_path` is relative to that directory.
SCRIPT_ROOT = "python/"

#: The extensions the seam reads. `.py` selects the unit; `.pyc` joins it, or stands alone.
SOURCE_EXTENSION = ".py"
COMPILED_EXTENSION = ".pyc"

#: What the install held for one key. A unit states which of the three it is.
SOURCE_KINDS = ("py+pyc", "py", "pyc-only")

#: The dependency roles a script literal can produce, exactly the seam's table.
DEPENDENCY_ROLES = ("dialogue", "sound", "map", "model", "vdata")

#: The named departures the decode is allowed to record.
ANOMALY_ROLES = (
    "mixed-line-endings",
    "tab-space-indent-mix",
    "non-ascii-byte",
    "duplicate-definition",
    "pyc-source-drift",
    "pyc-magic-mismatch",
    "trailing-bytes-after-code",
)


class ScriptIdentityError(ValueError):
    """The argument does not name a script the seam can publish."""


def normalize_script_key(argument: str) -> str:
    """The unit key for a path, a key, or either spelling of the two extensions.

    The command surface tolerates the root prefix and the source extension on its argument, so
    `python/tutorial/tutorial.py`, `tutorial/tutorial.pyc` and `tutorial/tutorial` are one key.
    """

    key = normalize_key(str(argument).strip())
    while key.startswith("/"):
        key = key[1:]
    if key.startswith(SCRIPT_ROOT):
        key = key[len(SCRIPT_ROOT):]
    for extension in (COMPILED_EXTENSION, SOURCE_EXTENSION):
        if key.endswith(extension):
            key = key[: -len(extension)]
            break
    while "//" in key:
        key = key.replace("//", "/")
    if not key or key.startswith("/") or key.endswith("/"):
        raise ScriptIdentityError(f"invalid script key {argument!r}")
    return key


def asset_id(key: str) -> str:
    return _asset_id(KIND, normalize_script_key(key))


def source_path(key: str) -> str:
    return SCRIPT_ROOT + normalize_script_key(key) + SOURCE_EXTENSION


def compiled_path(key: str) -> str:
    return SCRIPT_ROOT + normalize_script_key(key) + COMPILED_EXTENSION


def output_relative_path(key: str) -> PurePosixPath:
    return PurePosixPath(normalize_script_key(key) + ".glb")


@dataclass(frozen=True, slots=True)
class SourceLine:
    """One physical line of the source, as a byte span of the member."""

    index: int
    offset: int
    length: int
    terminator: str

    def to_json(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "offset": self.offset,
            "length": self.length,
            "terminator": self.terminator,
        }


@dataclass(frozen=True, slots=True)
class ScriptToken:
    """One token of the module, with the byte offset the source spells it at."""

    index: int
    type: str
    string: str
    line: int
    col: int
    offset: int

    def to_json(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "type": self.type,
            "string": self.string,
            "line": self.line,
            "col": self.col,
            "offset": self.offset,
        }


@dataclass(frozen=True, slots=True)
class ImportRecord:
    """One `import` or `from ... import ...` statement."""

    kind: str                     # "import" | "from"
    module: str
    names: tuple[str, ...]
    aliases: tuple[str, ...]
    line: int
    token: int

    def to_json(self) -> dict[str, Any]:
        return {
            "kind": self.kind,
            "module": self.module,
            "names": list(self.names),
            "aliases": list(self.aliases),
            "line": self.line,
            "token": self.token,
        }


@dataclass(frozen=True, slots=True)
class ArgumentRecord:
    """One formal parameter, and the source spelling of its default where it has one."""

    name: str
    form: str                     # "positional" | "star" | "double-star"
    default: str | None

    def to_json(self) -> dict[str, Any]:
        return {"name": self.name, "form": self.form, "default": self.default}


@dataclass(frozen=True, slots=True)
class FunctionRecord:
    """One `def`, its parameters and the block it owns."""

    name: str
    line_span: tuple[int, int]
    token: int
    args: tuple[ArgumentRecord, ...]
    defaults: tuple[str, ...]
    nested: tuple["FunctionRecord", ...]
    shadows: dict[str, Any] | None

    def to_json(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "lineSpan": [self.line_span[0], self.line_span[1]],
            "token": self.token,
            "args": [argument.to_json() for argument in self.args],
            "defaults": list(self.defaults),
            "nested": [nested.to_json() for nested in self.nested],
            "shadows": self.shadows,
        }


@dataclass(frozen=True, slots=True)
class ClassRecord:
    """One `class`, its bases and the methods its block declares."""

    name: str
    line_span: tuple[int, int]
    token: int
    bases: tuple[str, ...]
    methods: tuple[FunctionRecord, ...]
    shadows: dict[str, Any] | None

    def to_json(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "lineSpan": [self.line_span[0], self.line_span[1]],
            "token": self.token,
            "bases": list(self.bases),
            "methods": [method.to_json() for method in self.methods],
            "shadows": self.shadows,
        }


@dataclass(frozen=True, slots=True)
class AssignmentRecord:
    """One module-level assignment, by the targets it binds."""

    targets: tuple[str, ...]
    operator: str
    line: int
    token: int

    def to_json(self) -> dict[str, Any]:
        return {
            "targets": list(self.targets),
            "operator": self.operator,
            "line": self.line,
            "token": self.token,
        }


@dataclass(frozen=True, slots=True)
class ReferenceRecord:
    """One string literal that names an install member, and the unit it resolves to."""

    token: int
    literal: str
    kind: str
    asset: str
    source_path: str
    resolved: bool
    sentinel_reason: str | None
    source_offset: int

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {
            "token": self.token,
            "literal": self.literal,
            "kind": self.kind,
            "asset": self.asset,
            "sourcePath": self.source_path,
            "resolved": self.resolved,
            "sourceOffset": self.source_offset,
        }
        if self.sentinel_reason is not None:
            row["omittedProven"] = self.sentinel_reason
        return row


@dataclass(frozen=True, slots=True)
class EntityNameRecord:
    """One literal handed to an entity lookup, and the spelling of the call that took it."""

    token: int
    name: str
    call: str
    line: int
    source_offset: int

    def to_json(self) -> dict[str, Any]:
        return {
            "token": self.token,
            "name": self.name,
            "call": self.call,
            "line": self.line,
            "sourceOffset": self.source_offset,
        }


@dataclass(frozen=True, slots=True)
class ScriptModel:
    """Everything one script unit publishes, decoded and ready to be written."""

    key: str
    asset: str
    source_kind: str
    members: tuple[SourceMember, ...]
    source: dict[str, Any]
    tokens: tuple[ScriptToken, ...]
    structure: dict[str, Any]
    references: tuple[ReferenceRecord, ...]
    entity_names: tuple[EntityNameRecord, ...]
    pyc: dict[str, Any] | None
    dependencies: tuple[dict[str, Any], ...]
    anomalies: tuple[dict[str, Any], ...]
    omissions: tuple[dict[str, Any], ...]
    coverage: dict[str, Any] = field(default_factory=dict)
