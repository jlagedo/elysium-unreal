"""Identity, number semantics and the reference vocabulary of the map-entities sub-unit.

The map-entities seam owns the rules; this module is their code. The unit
is one BSP's ENTITIES lump (lump 0) read as an addressable table, so what lives here is what the
decoder, the exporter and the validator must all agree on without agreeing on each other: the
key rule, the stable IDs the entity keyvalues name, C `atof` semantics, and the coordinate
transform the unit contract states for a position and a `QAngle`.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import math
from pathlib import PurePosixPath
import re
from typing import Any

from elysium_pipeline.formats.map_glb import model as map_model
from elysium_pipeline.formats.unit_contract import asset_id as _asset_id
from elysium_pipeline.formats.unit_contract import extension_name

#: The kind this seam publishes, the extension it declares and the file suffix beside the root.
KIND = "map-entities"
KIND_TITLE = "Map-entities"
MAP_ENTITIES_EXTENSION = extension_name(KIND)
SCHEMA_VERSION = "1.0.0"
UNIT_SUFFIX = "entities"

#: The lump this unit is cut from, and the lump whose record count `*N` is validated against.
ENTITIES_LUMP = 0
MODELS_LUMP = 14
#: `dmodel_t` is 48 bytes, so the MODELS lump's length is the brush-model index space.
MODEL_STRIDE = 48

#: `Source inches, Z-up, right-handed` -> `glTF metres, Y-up, right-handed`.
INCH_TO_METRE = map_model.INCH_TO_METRE

#: A keyvalue whose name matches this is written the way an output is written. Whether it *is*
#: one is the class's datamap's answer, which `NOT_OUTPUT_KEYS` and `DISABLED_KEY_SUFFIX` carry.
OUTPUT_KEY = re.compile(r"^(?:On|Out)[A-Z]")

#: `(classname, key)` pairs the engine's datamap does not declare as outputs, folded. The maps
#: stamp them and retail silently drops them (`docs/vtmb/entity_io.md` -> "Output format").
NOT_OUTPUT_KEYS = frozenset({("trigger_player_activity_level", "ontrigger")})

#: Outputs a class's datamap declares under a name `OUTPUT_KEY` does not match, keyed by folded
#: classname and folded key. `CGameUI`'s datamap (`datamap_CGameUI_builder`, vampire.dll
#: `0x101183f0`) declares its fourteen button events this way, and `la_hub_1` authors three of
#: them with output payloads; without the class table they would read as repeated scalars.
OUTPUT_KEYS_BY_CLASS: dict[str, frozenset[str]] = {
    "game_ui": frozenset({
        "playeron", "playeroff", "pressedany", "pressedmoveleft", "pressedmoveright",
        "pressedforward", "pressedback", "pressedattack", "pressedattack2", "pressedfeed",
        "xaxis", "yaxis", "attackaxis", "attack2axis",
    }),
}

#: The Unofficial Patch disables a key by renaming it; no datamap declares the renamed spelling.
DISABLED_KEY_SUFFIX = "-wesp"

#: The seven comma-joined fields retail writes; it consumes the first six.
OUTPUT_FIELDS = ("target", "input", "parameter", "delay", "times", "python", "extra")
OUTPUT_FIELD_COUNT = 7
#: The parser initializes `times` to -1 and rewrites an authored 0 to -1; both mean unlimited.
UNLIMITED_TIMES = -1

#: The class whose `python_script` keyvalue is a Python expression rather than a plain value.
PYTHON_CHECK_CLASS = "logic_pythoncheck"
PYTHON_CHECK_KEY = "python_script"

#: The six faces a `skyname` names, in the order the engine appends them.
SKYBOX_FACES = ("ft", "bk", "lf", "rt", "up", "dn")

#: File extensions a keyvalue value can name. A value carrying one under a key the reference
#: vocabulary does not type is an `untyped-file-reference` anomaly, never a silent drop.
KNOWN_EXTENSIONS = (
    ".wav", ".mp3", ".mdl", ".vmt", ".vtf", ".vcd", ".txt", ".dlg", ".bsp", ".spr", ".tga",
    ".ttz", ".tth", ".py", ".pyc", ".nod", ".vdf",
)

#: Every anomaly role this seam can publish, and every omission role.
ANOMALY_ROLES = frozenset({
    "duplicate-scalar-key", "unterminated-block", "non-ascii-byte", "atof-truncated-number",
    "output-field-count", "untyped-file-reference", "terminator-count", "line-end-cr",
    "vector-field-count", "brush-model-index", "closing-brace-without-data", "unquoted-token",
    "stray-token", "comment-line", "missing-classname", "unterminated-quoted-string",
})
OMISSION_ROLES = frozenset({"bytes-after-terminator", "empty-member"})

#: The dependency roles the reference vocabulary can produce.
DEPENDENCY_ROLES = frozenset({
    "model", "brush-model", "sound-scheme", "sound", "vdata", "particle", "scene", "material",
    "dialogue",
})


class MapEntitiesModelError(ValueError):
    """A key or identity does not fit the seam's rules."""


# --- identity ---------------------------------------------------------------------------------


def normalize_key(key: str) -> str:
    """The unit key: the `.bsp` stem below `maps/`, folded, as the map root spells it."""

    return map_model.normalize_key(key)


def source_path(key: str) -> str:
    """The one install-relative member every unit of this map is cut from."""

    return map_model.source_path(key)


def member_path(key: str) -> str:
    """The span member's own identity: the file it was cut from and the row that cut it.

    The contract's ledger table is keyed by member path and two members may not share one, so a
    span names its lump the way the sibling map sub-units name theirs.
    """

    return f"{source_path(key)}#lump{ENTITIES_LUMP}"


def asset_id(key: str) -> str:
    return map_model.sub_asset_id(KIND, key)


def map_asset_id(key: str) -> str:
    """The root unit whose `models[]` this unit's `*N` references index into."""

    return map_model.asset_id(key)


def output_relative_path(key: str) -> PurePosixPath:
    """`maps/<map>.entities.glb`, beside the root."""

    return map_model.sub_output_relative_path(UNIT_SUFFIX, key)


# --- the identities a keyvalue can name -------------------------------------------------------


def _normalized(value: str) -> str:
    """One authored value read as a path: forward slashes, no quotes, blanks or empty segments.

    The case is left exactly as the map authored it, because the unit contract's rule on
    references between units defines a dependency's `sourcePath` as the install-relative path
    the referrer authored, spelling preserved. Only the asset key and the install lookup fold.
    """

    text = str(value).replace("\\", "/").strip().strip('"')
    while "//" in text:
        text = text.replace("//", "/")
    return text.strip("/")


def _folded(value: str) -> str:
    """`_normalized`, case-folded: the form an asset key is built from."""

    return _normalized(value).lower()


def _below(path: str, root: str) -> str:
    """What `path` names below the (folded) `root` prefix, spelling preserved."""

    text = _normalized(path)
    if text.lower().startswith(root):
        text = text[len(root):]
    return text.strip("/")


def _without(path: str, suffix: str) -> str:
    """`path` without a trailing `suffix`, matched case-insensitively."""

    return path[: -len(suffix)] if path.lower().endswith(suffix) else path


def _with(path: str, suffix: str) -> str:
    """`path` carrying `suffix`, keeping the authored spelling when it already ends with one."""

    return path if path.lower().endswith(suffix) else path + suffix


def _under(value: str, root: str, suffix: str) -> str:
    """The install-relative path one authored value names, anchored under `root`.

    Every segment the value carries keeps its authored spelling, the root prefix included when
    the value already carried one; only a prefix or an extension the value left implicit is
    added, and those are spelled the way the install spells them.
    """

    text = _with(_normalized(value), suffix)
    return text if text.lower().startswith(root) else root + text


def model_asset_id(value: str) -> str:
    return _asset_id("model", _without(_below(value, "models/").lower(), ".mdl"))


def model_source_path(value: str) -> str:
    return _under(value, "models/", ".mdl")


def material_asset_id(value: str) -> str:
    return _asset_id("material", _without(_below(value, "materials/").lower(), ".vmt"))


def material_source_path(value: str) -> str:
    return _under(value, "materials/", ".vmt")


def sound_asset_id(value: str) -> str:
    """The sound seam keeps the authored extension, because both spellings of a stem ship."""

    return _asset_id("sound", _below(value, "sound/").lower())


def sound_source_path(value: str) -> str:
    return _under(value, "sound/", "")


def sound_scheme_asset_id(value: str) -> str:
    return _asset_id("sound-scheme", _without(_below(value, "sound/schemes/").lower(), ".txt"))


def sound_scheme_source_path(value: str) -> str:
    return _under(value, "sound/schemes/", ".txt")


def _vdata_path(value: str, subtree: str) -> str:
    """`vdata/<subtree>/<name>.txt`; a bare name is placed in the subtree its key names."""

    text = _with(_normalized(value), ".txt")
    if text.lower().startswith("vdata/"):
        return text
    return "vdata/" + (text if "/" in text else f"{subtree}/{text}")


def vdata_asset_id(value: str, subtree: str) -> str:
    key = _below(_vdata_path(value, subtree), "vdata/").lower()
    return _asset_id("vdata", _without(key, ".txt"))


def vdata_source_path(value: str, subtree: str) -> str:
    return _vdata_path(value, subtree)


def particle_asset_id(value: str) -> str:
    return _asset_id("particle", _without(_below(value, "particles/").lower(), ".txt"))


def particle_source_path(value: str) -> str:
    return _under(value, "particles/", ".txt")


def scene_asset_id(value: str) -> str:
    return _asset_id("scene", _without(_below(value, "sound/").lower(), ".vcd"))


def scene_source_path(value: str) -> str:
    return _under(value, "sound/", ".vcd")


def dialogue_asset_id(value: str) -> str:
    return _asset_id("dialogue", _without(_below(value, "dlg/").lower(), ".dlg"))


def dialogue_source_path(value: str) -> str:
    return _under(value, "dlg/", ".dlg")


def skybox_asset_id(name: str, face: str) -> str:
    return _asset_id("material", f"skybox/{_folded(name)}{face}")


def skybox_source_path(name: str, face: str) -> str:
    return f"materials/skybox/{_normalized(name)}{face}.vmt"


# --- numbers ----------------------------------------------------------------------------------

#: C `atof`: leading blanks, then the longest prefix that can continue a decimal number.
_ATOF = re.compile(
    r"[ \t\n\r\v\f]*[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?"
)


def atof(text: str) -> tuple[float, str]:
    """`(value, consumed)` the way the engine reads a keyvalue number.

    A string with no numeric prefix reads as `0.0` and consumes nothing, which is what `atof`
    returns for `"none"`; a comma decimal (`-3496,92`) stops at the comma, so the engine -- and
    therefore this unit -- reads `-3496`.
    """

    match = _ATOF.match(str(text))
    if match is None or not match.group(0).strip():
        return 0.0, ""
    consumed = match.group(0)
    try:
        value = float(consumed)
    except ValueError:                                  # pragma: no cover - regex forbids it
        return 0.0, ""
    if not math.isfinite(value):
        return 0.0, consumed
    return value, consumed


#: C `atoi`: leading blanks, then the longest integer prefix. `times` is read with it.
_ATOI = re.compile(r"[ \t\n\r\v\f]*[+-]?[0-9]+")


def atoi(text: str) -> int:
    """The way the engine reads an output's `times` field: the longest integer prefix, or 0."""

    match = _ATOI.match(str(text))
    return int(match.group(0)) if match is not None else 0


def number_row(raw: str) -> dict[str, Any]:
    """`{raw, value}`: every numeric field publishes the string beside what it resolved to."""

    value, _ = atof(raw)
    return {"raw": str(raw), "value": value}


def dropped_suffix(raw: str) -> str:
    """What `atof` refused to read, or `""` when it read all the non-blank content."""

    _, consumed = atof(raw)
    remainder = str(raw)[len(consumed):]
    return remainder if remainder.strip() else ""


def position(x: float, y: float, z: float) -> list[float]:
    """Source inches, Z-up -> glTF metres, Y-up."""

    return [x * INCH_TO_METRE, z * INCH_TO_METRE, -y * INCH_TO_METRE]


def quaternion_from_angles(pitch: float, yaw: float, roll: float) -> list[float]:
    """A Source `QAngle` as the glTF rotation of the same placement.

    Built the way `AngleQuaternion` does, then carried across by the contract's
    `(x, y, z, w)_gltf = (x, z, -y, w)_source` and normalized.
    """

    half_yaw, half_pitch, half_roll = (
        math.radians(yaw) * 0.5, math.radians(pitch) * 0.5, math.radians(roll) * 0.5
    )
    sin_yaw, cos_yaw = math.sin(half_yaw), math.cos(half_yaw)
    sin_pitch, cos_pitch = math.sin(half_pitch), math.cos(half_pitch)
    sin_roll, cos_roll = math.sin(half_roll), math.cos(half_roll)
    x = sin_roll * cos_pitch * cos_yaw - cos_roll * sin_pitch * sin_yaw
    y = cos_roll * sin_pitch * cos_yaw + sin_roll * cos_pitch * sin_yaw
    z = cos_roll * cos_pitch * sin_yaw - sin_roll * sin_pitch * cos_yaw
    w = cos_roll * cos_pitch * cos_yaw + sin_roll * sin_pitch * sin_yaw
    gltf = [x, z, -y, w]
    length = math.sqrt(sum(value * value for value in gltf))
    if not length or not math.isfinite(length):
        raise MapEntitiesModelError(f"QAngle ({pitch}, {yaw}, {roll}) is not a rotation")
    return [value / length for value in gltf]


# --- records ----------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Claim:
    """One byte range of the lump span and the record that paid for it."""

    offset: int
    length: int
    state: str
    owner: str


@dataclass(frozen=True, slots=True)
class KeyValue:
    """One `"key" "value"` pair exactly as the block authors it.

    `offset` and `length` are the pair's span inside the lump span, from the key's first byte
    through the value's last, which is the range the unit's ledger claims for it. They are
    published as `sourceOffset`/`byteLength`, the names the contract gives a number a record read
    from an offset keeps, against the same origin the ledger is written in.
    """

    index: int
    key: str
    source_key: str
    value: str | None
    quoted_key: bool
    quoted_value: bool
    offset: int
    length: int
    output_like: bool = False

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {
            "index": self.index,
            "key": self.key,
            "sourceKey": self.source_key,
            "value": self.value,
            "quotedKey": self.quoted_key,
            "quotedValue": self.quoted_value,
            "sourceOffset": self.offset,
            "byteLength": self.length,
        }
        if self.output_like:
            row["outputLike"] = True
        return row


@dataclass(frozen=True, slots=True)
class Output:
    """One parsed output row: the six fields retail consumes, plus the residue it does not."""

    index: int
    key_value: int
    key: str
    target: str
    input: str
    parameter: str
    delay: dict[str, Any]
    times: dict[str, Any]
    python: str
    extra: str | None
    field_count: int
    raw: str

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {
            "index": self.index,
            "keyValue": self.key_value,
            "key": self.key,
            "target": self.target,
            "input": self.input,
            "parameter": self.parameter,
            "delay": self.delay,
            "times": self.times,
            "python": self.python,
        }
        if self.extra is not None:
            row["extra"] = self.extra
        row["fieldCount"] = self.field_count
        row["raw"] = self.raw
        return row


@dataclass(frozen=True, slots=True)
class Reference:
    """One keyvalue that names another unit."""

    key_value: int
    role: str
    asset: str
    resolved: bool
    index: int | None = None

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {
            "keyValue": self.key_value,
            "role": self.role,
            "asset": self.asset,
            "resolved": self.resolved,
        }
        if self.index is not None:
            row["index"] = self.index
        return row


@dataclass(frozen=True, slots=True)
class Entity:
    """One `{ … }` block, in lump order, with every pair it authors."""

    index: int
    classname: str | None
    braces: dict[str, Any]
    key_values: list[KeyValue]
    outputs: list[Output]
    references: list[Reference]
    origin: dict[str, Any] | None = None
    angles: dict[str, Any] | None = None
    model: dict[str, Any] | None = None

    def to_json(self) -> dict[str, Any]:
        row: dict[str, Any] = {
            "index": self.index,
            "classname": self.classname,
            "braces": self.braces,
        }
        if self.origin is not None:
            row["origin"] = self.origin
        if self.angles is not None:
            row["angles"] = self.angles
        if self.model is not None:
            row["model"] = self.model
        row["keyValues"] = [pair.to_json() for pair in self.key_values]
        row["outputs"] = [output.to_json() for output in self.outputs]
        row["references"] = [reference.to_json() for reference in self.references]
        return row


@dataclass(slots=True)
class MapEntitiesModel:
    """Everything one map-entities unit publishes, already shaped for `build_document`."""

    key: str
    asset_id: str
    source_path: str
    member: Any                                     # unit_contract.origin.SourceMember
    map: dict[str, Any]
    entities: list[Entity]
    worldspawn_index: int | None
    class_census: list[dict[str, Any]]
    script_expressions: list[dict[str, Any]]
    dependencies: list[dict[str, Any]]
    comments: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    claims: list[Claim]
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)

    @property
    def worldspawn(self) -> dict[str, Any] | None:
        if self.worldspawn_index is None:
            return None
        return self.entities[self.worldspawn_index].to_json()

    def entity_rows(self) -> list[dict[str, Any]]:
        return [entity.to_json() for entity in self.entities]


__all__ = [
    "ANOMALY_ROLES",
    "DEPENDENCY_ROLES",
    "DISABLED_KEY_SUFFIX",
    "ENTITIES_LUMP",
    "INCH_TO_METRE",
    "KIND",
    "KIND_TITLE",
    "KNOWN_EXTENSIONS",
    "MAP_ENTITIES_EXTENSION",
    "MODELS_LUMP",
    "MODEL_STRIDE",
    "NOT_OUTPUT_KEYS",
    "OUTPUT_KEYS_BY_CLASS",
    "OMISSION_ROLES",
    "OUTPUT_FIELDS",
    "OUTPUT_FIELD_COUNT",
    "OUTPUT_KEY",
    "PYTHON_CHECK_CLASS",
    "PYTHON_CHECK_KEY",
    "SCHEMA_VERSION",
    "SKYBOX_FACES",
    "UNIT_SUFFIX",
    "UNLIMITED_TIMES",
    "Claim",
    "Entity",
    "KeyValue",
    "MapEntitiesModel",
    "MapEntitiesModelError",
    "Output",
    "Reference",
    "asset_id",
    "atof",
    "atoi",
    "dialogue_asset_id",
    "dialogue_source_path",
    "dropped_suffix",
    "map_asset_id",
    "material_asset_id",
    "material_source_path",
    "member_path",
    "model_asset_id",
    "model_source_path",
    "normalize_key",
    "number_row",
    "output_relative_path",
    "particle_asset_id",
    "particle_source_path",
    "position",
    "quaternion_from_angles",
    "scene_asset_id",
    "scene_source_path",
    "skybox_asset_id",
    "skybox_source_path",
    "sound_asset_id",
    "sound_scheme_asset_id",
    "sound_scheme_source_path",
    "sound_source_path",
    "source_path",
    "vdata_asset_id",
    "vdata_source_path",
]
