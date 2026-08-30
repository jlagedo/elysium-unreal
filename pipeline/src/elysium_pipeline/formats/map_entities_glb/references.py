"""The closed vocabulary of keyvalues that name another unit.

`docs/architecture/seam_map_map_entities.md` -> "References" owns this table. It is the vocabulary
observed over the 108 maps and it is closed at export: a value carrying a known file extension
under a key this module does not type produces an `untyped-file-reference` anomaly, so the
vocabulary grows by recorded evidence rather than by guesswork at read time.

Key names are matched folded, because the maps spell one key several ways (`scenefile` beside
`SceneFile`, `BaseAnim` beside `baseanim`); the authored spelling is preserved in the reference's
`sourcePath`.
"""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Any

from elysium_pipeline.formats.map_entities_glb import model as entity_model


@dataclass(frozen=True, slots=True)
class ReferenceSpec:
    """One resolved join: the role the referrer needs, the identity, and the authored path.

    `source_path` keeps the spelling the map authored, because that is what a `dependencies` row
    publishes; `lookup_path` is the folded form the (lower-cased) install index is asked for.
    """

    role: str
    asset: str
    source_path: str
    index: int | None = None

    @property
    def lookup_path(self) -> str:
        return self.source_path.lower()


#: `*N` names the root's `models[N]`.
BRUSH_MODEL = re.compile(r"^\*(\d+)$")
#: A value the engine reads as a number rather than as a name.
NUMERIC = re.compile(r"^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)$")

SOUND_EXTENSIONS = (".wav", ".mp3")


def extension_of(value: str) -> str:
    """The known file extension the value carries, folded, or `""` for none."""

    folded = str(value).strip().strip('"').lower().replace("\\", "/")
    for suffix in entity_model.KNOWN_EXTENSIONS:
        if folded.endswith(suffix):
            return suffix
    return ""


def is_sound_key(key: str) -> bool:
    """`message`, `noise*`, `*sound*` and `soundgroup`, folded."""

    return key == "message" or key.startswith("noise") or "sound" in key or key == "soundgroup"


def is_particle_key(key: str) -> bool:
    """`particle*` and `emitter`, folded."""

    return key.startswith("particle") or key == "emitter"


def is_material_key(key: str) -> bool:
    return key in ("material", "texture", "decal")


def is_scene_key(key: str) -> bool:
    return key == "scenefile"


def is_dialogue_key(key: str, value: str) -> bool:
    """`dialog_file`, and any key whose value names a `.dlg`."""

    return key == "dialog_file" or extension_of(value) == ".dlg"


def _names_a_path(value: str) -> bool:
    """A value that names a file rather than an enum the class reads as a number."""

    return bool(value) and not NUMERIC.match(value.strip())


def model_field(value: str, brush_model_count: int) -> tuple[dict[str, Any], ReferenceSpec | None]:
    """The `model` keyvalue's descriptor and the reference it makes, if any.

    `*N` is a brush model of this map's own root; a studio path is a model unit; a `.vmt` path is
    the sprite an `env_sprite` draws. A `.spr` is carried as a sprite with no join claimed -- no
    seam publishes the legacy sprite format and no retail map authors one -- so the decoder's
    growth-by-evidence guard names it an `untyped-file-reference`; anything else is carried by
    path with no join claimed.
    """

    text = str(value).strip()
    brush = BRUSH_MODEL.match(text)
    if brush is not None:
        index = int(brush.group(1))
        return (
            {"kind": "brush", "index": index, "withinModelCount": index < brush_model_count},
            None,
        )
    extension = extension_of(text)
    if extension == ".mdl":
        return (
            {"kind": "model", "asset": entity_model.model_asset_id(text), "path": text},
            ReferenceSpec(
                "model", entity_model.model_asset_id(text), entity_model.model_source_path(text)
            ),
        )
    if extension in (".vmt", ".spr"):
        spec = None
        if extension == ".vmt":
            spec = ReferenceSpec(
                "material",
                entity_model.material_asset_id(text),
                entity_model.material_source_path(text),
            )
        return ({"kind": "sprite", "path": text}, spec)
    return ({"kind": "other", "path": text}, None)


def reference_for(key: str, value: str) -> ReferenceSpec | None:
    """The unit one keyvalue names, or `None` when the key is outside the vocabulary.

    `key` is folded; `value` is the authored spelling. `model` is not answered here: its
    descriptor and its reference are `model_field`'s, because a `*N` needs the map's own model
    count to be checkable.
    """

    text = str(value).strip()
    if not text:
        return None
    extension = extension_of(text)
    if key == "scheme_file":
        return ReferenceSpec(
            "sound-scheme",
            entity_model.sound_scheme_asset_id(text),
            entity_model.sound_scheme_source_path(text),
        )
    if is_sound_key(key) and extension in SOUND_EXTENSIONS:
        return ReferenceSpec(
            "sound", entity_model.sound_asset_id(text), entity_model.sound_source_path(text)
        )
    if key == "definition_file":
        return ReferenceSpec(
            "vdata",
            entity_model.vdata_asset_id(text, "signs"),
            entity_model.vdata_source_path(text, "signs"),
        )
    if key in ("hackterminal", "terminal_file"):
        return ReferenceSpec(
            "vdata",
            entity_model.vdata_asset_id(text, "hackterminals"),
            entity_model.vdata_source_path(text, "hackterminals"),
        )
    if is_particle_key(key) and _names_a_path(text):
        return ReferenceSpec(
            "particle",
            entity_model.particle_asset_id(text),
            entity_model.particle_source_path(text),
        )
    if is_scene_key(key):
        return ReferenceSpec(
            "scene", entity_model.scene_asset_id(text), entity_model.scene_source_path(text)
        )
    if is_material_key(key) and (_names_a_path(text) and ("/" in text or extension == ".vmt")):
        return ReferenceSpec(
            "material",
            entity_model.material_asset_id(text),
            entity_model.material_source_path(text),
        )
    if is_dialogue_key(key, text):
        return ReferenceSpec(
            "dialogue",
            entity_model.dialogue_asset_id(text),
            entity_model.dialogue_source_path(text),
        )
    return None


def skybox_references(name: str) -> list[ReferenceSpec]:
    """The six materials `worldspawn.skyname` names."""

    text = str(name).strip()
    if not text:
        return []
    return [
        ReferenceSpec(
            "material",
            entity_model.skybox_asset_id(text, face),
            entity_model.skybox_source_path(text, face),
        )
        for face in entity_model.SKYBOX_FACES
    ]


__all__ = [
    "BRUSH_MODEL",
    "ReferenceSpec",
    "extension_of",
    "is_dialogue_key",
    "is_material_key",
    "is_particle_key",
    "is_scene_key",
    "is_sound_key",
    "model_field",
    "reference_for",
    "skybox_references",
]
