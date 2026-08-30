"""Identity and shape for the Dialogue GLB seam.

`docs/architecture/seam_map_dialogue.md` owns the rules; this module states only the constants
and small helpers `source.py`, `decode.py` and the exporter share, plus the frozen records the
row lexer hands back.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import asset_id as _asset_id
from elysium_pipeline.formats.unit_contract import extension_name, normalize_key

KIND = "dialogue"
DIALOGUE_EXTENSION = extension_name(KIND)
SCHEMA_VERSION = "1.0.0"

#: The install subdirectory every dialogue unit is cut from.
DLG_ROOT = "dlg/"
DLG_SUFFIX = ".dlg"

#: The thirteen columns the physical format declares; a fourteenth is a carried anomaly.
FIELD_COUNT = 13

#: Columns the table never assigns; any non-empty content there is `typedUnidentified`.
RESERVED_COLUMNS = (6, 7, 8, 9, 10, 11)

#: The four language takes the audio path convention resolves.
AUDIO_LANGUAGES = ("e", "f", "m", "n")

ROLE_PADDING = "padding"
ROLE_NPC_LINE = "npc-line"
ROLE_PC_CHOICE = "pc-choice"

MARKER_AUTO_LINK = "auto-link"
MARKER_AUTO_END = "auto-end"
MARKER_STARTING_CONDITION = "starting-condition"


def normalize_dlg_key(raw: str) -> str:
    """The unit key: the path below `dlg/`, without `.dlg`, folded and forward-slashed.

    The argument tolerates the `dlg/` root prefix and the `.dlg` extension so the singular
    command's argument (`dlg/<path>.dlg`) and the bare key (`<path>`) both resolve.
    """

    key = normalize_key(raw)
    if key.startswith(DLG_ROOT):
        key = key[len(DLG_ROOT):]
    if key.endswith(DLG_SUFFIX):
        key = key[: -len(DLG_SUFFIX)]
    if not key:
        raise ValueError(f"invalid dialogue key {raw!r}")
    return key


def source_path_for(key: str) -> str:
    """The install-relative member path for one unit key."""

    return f"{DLG_ROOT}{normalize_dlg_key(key)}{DLG_SUFFIX}"


def asset_id(key: str) -> str:
    return _asset_id(KIND, normalize_dlg_key(key))


def output_relative_path(key: str) -> PurePosixPath:
    return PurePosixPath(normalize_dlg_key(key) + ".glb")


def audio_dir_for(key: str) -> str:
    """`character/dlg/<hub>/<dlg-stem>` -- the audio join's own key, install-relative below
    `sound/`."""

    return f"character/dlg/{normalize_dlg_key(key)}"


@dataclass(frozen=True, slots=True)
class CellSpan:
    """One row cell's `{` TAB content TAB `}` shape, or the best-effort span of a malformed one.

    Offsets are relative to the member's own bytes, which is what the unit's byte ledger runs
    over.
    """

    index: int
    offset: int
    length: int
    text: str
    well_formed: bool
    content_offset: int
    content_length: int


@dataclass(slots=True)
class DialogueModel:
    """The complete decode of one `.dlg` file: the ledger, the rows and the audio join."""

    key: str
    asset: str
    source_path: str
    member: Any
    lines: list[dict[str, Any]] = field(default_factory=list)
    expressions: list[dict[str, Any]] = field(default_factory=list)
    audio: dict[str, Any] = field(default_factory=dict)
    dependencies: list[dict[str, Any]] = field(default_factory=list)
    anomalies: list[dict[str, Any]] = field(default_factory=list)
    omissions: list[dict[str, Any]] = field(default_factory=list)
    coverage: dict[str, Any] = field(default_factory=dict)
