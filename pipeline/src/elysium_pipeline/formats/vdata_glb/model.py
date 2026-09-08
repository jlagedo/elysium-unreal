"""Identity and stable-name rules for the vdata GLB seam.

A unit is one
`vdata/<subtree>/<name>.txt` KeyValues (or, for `experience_table.txt`, pipe-delimited) file, so
every name here folds to the lower-cased path below `vdata/`, which is the seam's identity key.
The install already lower-cases every key it indexes (`formats/install.py`), so the key a unit
publishes is exactly the index key with the `vdata/` prefix and `.txt` suffix trimmed.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import extension_name
from elysium_pipeline.formats.unit_contract import asset_id as _asset_id
from elysium_pipeline.formats.unit_contract.origin import SourceMember

KIND = "vdata"
KIND_TITLE = "Vdata"
#: 1.1.0 added the source capsule: the member's exact bytes travel in the unit's BIN chunk.
SCHEMA_VERSION = "1.1.0"
VDATA_EXTENSION = extension_name(KIND)

#: The install directory every unit is cut from, and the extension every one of its files carries.
SOURCE_ROOT = "vdata/"
SOURCE_SUFFIX = ".txt"

#: The variant a filename's ` - vampire` / ` - hunter` suffix names, or `"base"` for none.
VARIANT_SUFFIXES = ("vampire", "hunter")

#: The one file in the corpus that uses the pipe-delimited grammar rather than KeyValues.
DELIMITED_KEY = "system/experience_table"


class VdataModelError(ValueError):
    """A name or identity does not fit the seam's rules."""


def normalize_key(raw: str) -> str:
    """The unit key: lower case, forward-slashed, `vdata/` prefix and `.txt` suffix dropped
    whether or not the caller supplied them.

    The singular export command "tolerates the root prefix and the source extension on its
    argument", so a bare `<subtree>/<name>` and a full install path
    both resolve to the same key.
    """

    if not raw:
        raise VdataModelError("a vdata key is empty")
    normalized = str(raw).strip().replace("\\", "/").lower()
    if normalized.startswith(SOURCE_ROOT):
        normalized = normalized[len(SOURCE_ROOT):]
    if normalized.endswith(SOURCE_SUFFIX):
        normalized = normalized[: -len(SOURCE_SUFFIX)]
    normalized = normalized.strip("/")
    if not normalized or "/" not in normalized:
        raise VdataModelError(f"invalid vdata key {raw!r}: expected <subtree>/<name>")
    return normalized


def source_path(key: str) -> str:
    """The install-relative path one key resolves against."""

    return f"{SOURCE_ROOT}{normalize_key(key)}{SOURCE_SUFFIX}"


def subtree_of(key: str) -> str:
    return normalize_key(key).split("/", 1)[0]


def name_of(key: str) -> str:
    return normalize_key(key).rsplit("/", 1)[-1]


def variant_of(key: str) -> str:
    """`"vampire"`, `"hunter"` or `"base"`, from the filename's ` - <variant>` suffix."""

    name = name_of(key)
    for variant in VARIANT_SUFFIXES:
        if name.endswith(f" - {variant}"):
            return variant
    return "base"


def is_delimited(key: str) -> bool:
    return normalize_key(key) == DELIMITED_KEY


def asset_id(key: str) -> str:
    return _asset_id(KIND, key)


def output_relative_path(key: str) -> PurePosixPath:
    """The unit's path below the seam's own family root (`vdata/<subtree>/<name>.glb`).

    Like every sibling GLB seam's `output_relative_path`, this does not itself prepend the kind's
    own name (`vdata/`) -- the caller's `output_root` already names the family root, so `export`
    writes to `output_root/<subtree>/<name>.glb`, and the wiring phase points that root at
    `$ELYSIUM_EXPORT_V2_ROOT/vdata`.
    """

    return PurePosixPath(normalize_key(key) + ".glb")


@dataclass(slots=True)
class VdataModel:
    """The complete decode of one `vdata/<subtree>/<name>.txt` file."""

    key: str
    asset: str
    subtree: str
    variant: str
    member: SourceMember
    grammar: str                       # "keyvalues" | "delimited" | "freeform"
    root_key: str | None
    tree: dict[str, Any] | None
    rows: list[dict[str, Any]]
    projection: dict[str, Any]
    comments: list[dict[str, Any]]
    dependencies: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
    ledger_row: dict[str, Any] | None = None
