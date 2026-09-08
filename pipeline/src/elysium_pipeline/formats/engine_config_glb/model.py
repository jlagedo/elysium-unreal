"""Identity and stable-name rules for the engine-config GLB seam.

The engine-config seam owns the facts; this module is their code. A unit
is one console script, table or binary state file the seam names explicitly -- there is no
directory scan, because the member list itself is the seam's own closed vocabulary.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract.origin import SourceMember
from elysium_pipeline.formats.unit_contract.references import asset_id as _unit_asset_id
from elysium_pipeline.formats.unit_contract.references import normalize_key as _normalize_key

KIND = "engine-config"
KIND_TITLE = "Engine-config"
SCHEMA_VERSION = "1.0.0"
ENGINE_CONFIG_EXTENSION = "ELYSIUM_vtmb_engine_config"

GRAMMAR_CONSOLE_SCRIPT = "console-script"
GRAMMAR_RAD_ROWS = "rad-rows"
GRAMMAR_KEYVALUES = "keyvalues"
GRAMMAR_LINE_LIST = "line-list"
GRAMMAR_KEY_EQUALS_VALUE = "key-equals-value"
GRAMMAR_LOCALIZED_LIST = "localized-list"
GRAMMAR_BINARY = "binary"
GRAMMAR_SAVE_FRAGMENT = "save-fragment"

GRAMMARS = frozenset(
    {
        GRAMMAR_CONSOLE_SCRIPT,
        GRAMMAR_RAD_ROWS,
        GRAMMAR_KEYVALUES,
        GRAMMAR_LINE_LIST,
        GRAMMAR_KEY_EQUALS_VALUE,
        GRAMMAR_LOCALIZED_LIST,
        GRAMMAR_BINARY,
        GRAMMAR_SAVE_FRAGMENT,
    }
)

#: The extension-root keys each grammar publishes non-null, the single source both the exporter
#: (for `coverage.mapped`) and the validator (for the null/non-null check) read -- so the two
#: never drift apart on which fields a grammar owns.
GRAMMAR_FIELDS: dict[str, tuple[str, ...]] = {
    GRAMMAR_CONSOLE_SCRIPT: ("commands", "bindings", "aliases", "cvars", "scriptExpressions"),
    GRAMMAR_RAD_ROWS: ("textureLights",),
    GRAMMAR_KEYVALUES: ("detailTypes",),
    GRAMMAR_LINE_LIST: ("maps",),
    GRAMMAR_KEY_EQUALS_VALUE: ("packer", "packerKeys"),
    GRAMMAR_LOCALIZED_LIST: ("categories",),
    GRAMMAR_BINARY: ("binary",),
    GRAMMAR_SAVE_FRAGMENT: ("saveFragment",),
}

#: Every grammar-specific extension-root key, over every grammar -- exactly one grammar's tuple
#: from `GRAMMAR_FIELDS` is non-null on any one unit.
ALL_GRAMMAR_KEYS = tuple(
    dict.fromkeys(key for fields in GRAMMAR_FIELDS.values() for key in fields)
)

#: The seven named console scripts under `cfg/`, `cfg/valve.rc` and `cfg/dummy.txt` -- every
#: member the seam reads with the console-script grammar.
CONSOLE_SCRIPT_KEYS = (
    "cfg/default.cfg",
    "cfg/config.cfg",
    "cfg/user.cfg",
    "cfg/autoexec.cfg",
    "cfg/language.cfg",
    "cfg/multiplayer.cfg",
    "cfg/skill1.cfg",
    "cfg/valve.rc",
    "cfg/dummy.txt",
)

#: Every member this seam publishes, mapped to its grammar. There is no wildcard: a member not in
#: this table is not this seam's (the `cfg/elysium_*.cfg` capture scripts, `cfg/joystick.cfg`
#: which ships in neither retail nor the patch).
KEY_GRAMMAR: dict[str, str] = {key: GRAMMAR_CONSOLE_SCRIPT for key in CONSOLE_SCRIPT_KEYS}
KEY_GRAMMAR.update(
    {
        "lights.rad": GRAMMAR_RAD_ROWS,
        "detail.vbsp": GRAMMAR_KEYVALUES,
        "maps/loadorder.txt": GRAMMAR_LINE_LIST,
        "pack_values.txt": GRAMMAR_KEY_EQUALS_VALUE,
        "localized_list.txt": GRAMMAR_LOCALIZED_LIST,
        "vidcfg.bin": GRAMMAR_BINARY,
        "voice_ban.dt": GRAMMAR_BINARY,
        "hl2.tmp": GRAMMAR_SAVE_FRAGMENT,
    }
)

#: Every key this seam publishes, in the seam doc's own listing order.
KNOWN_KEYS = tuple(KEY_GRAMMAR)

#: `hl2.tmp` is residue: the corpus index lists it, but no shipped code path reads it.
RESIDUE_KEYS = frozenset({"hl2.tmp"})

#: The seam's own family root, below `$ELYSIUM_EXPORT_V2_ROOT`.
FAMILY_ROOT = "engine-config"


class EngineConfigModelError(ValueError):
    """A key or identity does not fit the seam's closed vocabulary."""


def normalize_key(raw: str) -> str:
    """The unit key: lower case, forward-slashed, the family root prefix tolerated and dropped.

    The singular export command tolerates the root prefix on its argument,
    so `engine-config/cfg/user.cfg` and `cfg/user.cfg` are one key.
    """

    normalized = _normalize_key(str(raw)).strip().strip("/")
    prefix = FAMILY_ROOT + "/"
    if normalized.startswith(prefix):
        normalized = normalized[len(prefix):]
    if normalized not in KEY_GRAMMAR:
        raise EngineConfigModelError(f"{raw!r} is not an engine-config member this seam publishes")
    return normalized


def grammar_for(key: str) -> str:
    return KEY_GRAMMAR[normalize_key(key)]


def is_residue(key: str) -> bool:
    return normalize_key(key) in RESIDUE_KEYS


def asset_id(key: str) -> str:
    return _unit_asset_id(KIND, normalize_key(key))


def output_relative_path(key: str) -> PurePosixPath:
    """The key keeps its own extension: `cfg/user.cfg` publishes `cfg/user.cfg.glb`."""

    return PurePosixPath(normalize_key(key) + ".glb")


def material_asset_id(texture: str) -> str:
    """The stable ID for a `lights.rad` texture name: a path below `materials/`, no extension."""

    normalized = str(texture).replace("\\", "/").strip().strip('"').lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    normalized = normalized.strip("/")
    if normalized.startswith("materials/"):
        normalized = normalized[len("materials/"):]
    if normalized.endswith(".vmt"):
        normalized = normalized[: -len(".vmt")]
    return "vtmb:material:" + normalized


def material_source_path(texture: str) -> str:
    normalized = str(texture).replace("\\", "/").strip().strip('"').lower().strip("/")
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    if normalized.startswith("materials/"):
        normalized = normalized[len("materials/"):]
    if not normalized.endswith(".vmt"):
        normalized += ".vmt"
    return "materials/" + normalized


def model_asset_id(path: str) -> str:
    """The stable ID for a `detail.vbsp` model path: a path below `models/`, no extension."""

    normalized = str(path).replace("\\", "/").strip().strip('"').lower().strip("/")
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    if normalized.startswith("models/"):
        normalized = normalized[len("models/"):]
    if normalized.endswith(".mdl"):
        normalized = normalized[: -len(".mdl")]
    return "vtmb:model:" + normalized


def model_source_path(path: str) -> str:
    normalized = str(path).replace("\\", "/").strip().strip('"').lower().strip("/")
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    if normalized.startswith("models/"):
        normalized = normalized[len("models/"):]
    if not normalized.endswith(".mdl"):
        normalized += ".mdl"
    return "models/" + normalized


def map_stem(path: str) -> str:
    """The `vtmb:map:` key: the `.bsp` stem, `maps/` and the extension dropped."""

    normalized = str(path).replace("\\", "/").strip().strip('"').lower().strip("/")
    if normalized.startswith("maps/"):
        normalized = normalized[len("maps/"):]
    if normalized.endswith(".bsp"):
        normalized = normalized[: -len(".bsp")]
    return normalized


def map_asset_id(path: str) -> str:
    return "vtmb:map:" + map_stem(path)


def map_source_path(path: str) -> str:
    return f"maps/{map_stem(path)}.bsp"


def dependency_asset_id(source_path: str) -> str:
    """The `vtmb:engine-config:` identity for an `exec` target, whether or not the install ships
    it -- `cfg/joystick.cfg` has this identity even though no member backs it, exactly as
    `cfg/language.cfg` would if this seam had never heard of it either."""

    return _unit_asset_id(KIND, _normalize_key(str(source_path)).strip("/"))


def engine_config_source_path(name: str) -> str:
    """The install-relative path an `exec <name>` argument resolves against.

    `exec` targets are authored bare (`language.cfg`, `joystick.cfg`) or already
    slash-qualified; the convention every shipped script uses is relative to `cfg/`.
    """

    normalized = str(name).replace("\\", "/").strip().strip('"').lower()
    if "/" in normalized:
        return normalized
    return f"cfg/{normalized}"


@dataclass(slots=True)
class EngineConfigModel:
    """The complete decode of one engine-config member."""

    key: str
    asset: str
    grammar: str
    member: SourceMember
    ledger_row: dict[str, Any]
    commands: list[dict[str, Any]] | None = None
    bindings: list[dict[str, Any]] | None = None
    aliases: list[dict[str, Any]] | None = None
    cvars: list[dict[str, Any]] | None = None
    script_expressions: list[dict[str, Any]] | None = None
    texture_lights: list[dict[str, Any]] | None = None
    detail_types: list[dict[str, Any]] | None = None
    maps: list[dict[str, Any]] | None = None
    packer: dict[str, Any] | None = None
    packer_keys: list[dict[str, Any]] | None = None
    categories: list[dict[str, Any]] | None = None
    binary: dict[str, Any] | None = None
    save_fragment: dict[str, Any] | None = None
    dependencies: list[dict[str, Any]] = field(default_factory=list)
    comments: list[dict[str, Any]] = field(default_factory=list)
    anomalies: list[dict[str, Any]] = field(default_factory=list)
    omissions: list[dict[str, Any]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
