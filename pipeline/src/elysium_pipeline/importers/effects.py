"""The per-map effects product for the V2 map bake (R7.3).

The map bake's effects import contract and the particle seam's own unit rules are what this
module executes; nothing here decides anything those two contracts do not state.

The stage joins each effects entity row of the entities unit (`env_particle`, `func_particle`,
`func_dustmotes`, `env_steam`, `env_beam`) to its references -- the particle units of the root's
closure, the root unit's `models[]` for a brush bound, the texture lane's staged sidecar for a
sprite's `T_` path and size, the material lane's sidecar for a beam's `MI_` -- and publishes five
tables plus a stats block, every field named, in centimetres / degrees / seconds / 0..1, with its
source. The closure of a placed root is kept as a **tree**: node 0 is the root, every other node is
one definition reached through one block of its parent, and a definition reached through two blocks
is two nodes.

Runs in the `uv` interpreter (it is called from `importers.map_geometry.stage_map`, which needs
numpy for the rest of the manifest); the editor half (`pipeline/unreal/bake_map_v2._place_effects`)
reads the staged tables with `json` alone.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import Any, Callable, Sequence

from elysium_pipeline import paths
from elysium_pipeline.formats.particle_glb.model import (
    PARTICLE_EXTENSION,
    normalize_particle_key,
    output_relative_path as particle_output_relative_path,
)
from elysium_pipeline.formats.unit_contract import read_glb

#: One Source unit (inch) in Unreal centimetres.
INCH_TO_CM = 2.54
#: The colour keys are 0..255 in the definition and 0..1 in the product.
COLOUR_FULL = 255.0
#: `CParticleManager`'s `fps` default; `frames` defaults to `fps` (a one-second life).
DEFAULT_FPS = 30.0
#: `CEnvParticle::m_fSpawnBounds` (`+0x49c`) default, Source units.
DEFAULT_SPAWNBOUNDS_IN = 512.0
#: `CFuncParticle::Activate`: `sizeScalar = |dx|*|dy|*|dz| * 2^-21`, clamped to this range.
VOLUME_SCALE_UNIT = 2.0 ** -21
VOLUME_SCALE_MIN = 0.01
VOLUME_SCALE_MAX = 100.0
#: `CFuncParticle::Activate` forces the brush-emitter attach mode whatever the key says.
FUNC_PARTICLE_ATTACH = 15

EFFECT_CLASSES = ("env_particle", "func_particle")
DUST_CLASS = "func_dustmotes"
STEAM_CLASS = "env_steam"
BEAM_CLASS = "env_beam"

#: The particle-body keys (`docs/vtmb/effects.md` section 2.4, the runtime's own table): staged
#: name, default, unit multiplier. Colours fold `/255`; lengths and speeds `x 2.54`.
PARTICLE_RAMPS: dict[str, tuple[str, float, float]] = {
    "size": ("size_cm", 1.0, INCH_TO_CM),
    "width": ("width", 1.0, 1.0),
    "height": ("height", 1.0, 1.0),
    "rotation": ("rotation_deg", 0.0, 1.0),
    "red": ("red", COLOUR_FULL, 1.0 / COLOUR_FULL),
    "green": ("green", COLOUR_FULL, 1.0 / COLOUR_FULL),
    "blue": ("blue", COLOUR_FULL, 1.0 / COLOUR_FULL),
    "color": ("color", COLOUR_FULL, 1.0 / COLOUR_FULL),
    "mask": ("mask", COLOUR_FULL, 1.0 / COLOUR_FULL),
    "refract": ("refract", 1.0, 1.0),
    "radius_speed": ("radius_speed_cm_s", 0.0, INCH_TO_CM),
    "theta_speed": ("theta_speed_deg_s", 0.0, 1.0),
    "phi_speed": ("phi_speed_deg_s", 0.0, 1.0),
    "x_speed": ("x_speed_cm_s", 0.0, INCH_TO_CM),
    "y_speed": ("y_speed_cm_s", 0.0, INCH_TO_CM),
    "z_speed": ("z_speed_cm_s", 0.0, INCH_TO_CM),
    "elevation_speed": ("elevation_speed_cm_s", 0.0, INCH_TO_CM),
    "parent_speed": ("parent_speed", 1.0, 1.0),
}
#: The spawn-block keys, same shape. `timescale` and `distance` are scalars, listed apart.
SPAWN_RAMPS: dict[str, tuple[str, float, float]] = {
    "rate": ("rate", 0.0, 1.0),
    "burst": ("burst", 0.0, 1.0),
    "radius": ("radius_cm", 0.0, INCH_TO_CM),
    "theta": ("theta_deg", 0.0, 1.0),
    "phi": ("phi_deg", 0.0, 1.0),
    "x": ("x_cm", 0.0, INCH_TO_CM),
    "y": ("y_cm", 0.0, INCH_TO_CM),
    "z": ("z_cm", 0.0, INCH_TO_CM),
    "elevation": ("elevation_cm", 0.0, INCH_TO_CM),
    "rotation": ("rotation_deg", 0.0, 1.0),
    "width": ("width", 1.0, 1.0),
    "height": ("height", 1.0, 1.0),
    "size": ("size", 1.0, 1.0),
    "red": ("red", COLOUR_FULL, 1.0 / COLOUR_FULL),
    "green": ("green", COLOUR_FULL, 1.0 / COLOUR_FULL),
    "blue": ("blue", COLOUR_FULL, 1.0 / COLOUR_FULL),
    "color": ("color", COLOUR_FULL, 1.0 / COLOUR_FULL),
    "mask": ("mask", COLOUR_FULL, 1.0 / COLOUR_FULL),
    "refract": ("refract", 1.0, 1.0),
}
#: Definition-scope scalars and flags the tree reads directly.
DEFINITION_KEYS = frozenset({
    "fps", "frames", "min_frames", "max_frames", "loop", "precipitation", "sprite", "normal",
    "movealign", "flat", "sortfront", "no_z_test", "lighting", "depth_offset",
    "surface_color", "use_surface_color", "ignore_surface_color",
})
#: Keys the corpus authors that the runtime's tables have no row for (the particle seam's
#: "Vocabulary" table, the "authored, unread" row): carried nowhere, never a warning.
UNREAD_PARTICLE_KEYS = frozenset({"rotate", "radius", "burst"})
UNREAD_SPAWN_KEYS = frozenset({"loop", "frames", "depth_offset"})
#: The four collide scalars the corpus writes inside a nested `collide { spawn { } }` block
#: (`raindrops2`, `drip`): read onto the collide record flagged `nested`, so not a warning here.
NESTED_COLLIDE_KEYS = frozenset({"bounce", "friction", "gravity", "drag"})
SPAWN_SCALARS = frozenset({"particle", "timescale", "distance"})
COLLIDE_KEYS = frozenset({
    "bounce", "friction", "gravity", "drag", "self", "vdecal_first", "vdecal_last",
    "vdecal_angle_spread",
})
DECAL_KEYS = frozenset({"particle", "angle_spread"})

#: The Valve classes' keyfield defaults (`docs/vtmb/effects.md` section 3.1; the FGD's own).
DUST_DEFAULTS = {
    "SpawnRate": 500.0, "Color": "255 255 255", "Alpha": 255.0, "SpeedMax": 20.0,
    "SizeMin": 100.0, "SizeMax": 200.0, "LifetimeMin": 3.0, "LifetimeMax": 5.0,
    "DistMax": 1024.0, "Frozen": 0.0, "StartDisabled": 0.0,
}
STEAM_DEFAULTS = {
    "type": 0.0, "InitialState": 0.0, "SpreadSpeed": 15.0, "Speed": 120.0, "StartSize": 10.0,
    "EndSize": 25.0, "Rate": 26.0, "JetLength": 80.0, "rendercolor": "255 255 255",
    "renderamt": 255.0,
}
BEAM_DEFAULTS = {
    "BoltWidth": 2.0, "NoiseAmplitude": 0.0, "TextureScroll": 35.0, "Radius": 256.0,
    "life": 1.0, "StrikeTime": 1.0, "damage": 0.0, "rendercolor": "255 255 255",
    "renderamt": 100.0, "spawnflags": 0.0, "framerate": 0.0, "framestart": 0.0, "renderfx": 0.0,
}
#: `CEnvBeam`'s taper: every VtMB beam ends at a tenth of its width.
BEAM_END_WIDTH_FRACTION = 0.1


#: A ramp element the unit could not number but the engine's `atof` would (`255!`).
_NUMBER_PREFIX = __import__("re").compile(r"[-+]?(?:\d+\.?\d*|\.\d+)")


class EffectsStageError(ValueError):
    """A unit or a sidecar the effects lane cannot proceed without."""


# --------------------------------------------------------------------------------- helpers


def _atof(token: str) -> float:
    from elysium_pipeline.exporters.UE_map_sidecars import atof

    return atof(token)


def _key_values(entity: dict[str, Any]) -> dict[str, str]:
    """The entity's keyvalues folded case-insensitively, last spelling wins."""

    out: dict[str, str] = {}
    for row in entity.get("keyValues") or []:
        out[str(row.get("key") or "").lower()] = str(row.get("value") or "")
    return out


def _number(keys: dict[str, str], name: str, default: float) -> float:
    name = name.lower()
    return _atof(keys[name]) if name in keys else default


def _flag(keys: dict[str, str], name: str, default: bool = False) -> bool:
    name = name.lower()
    return bool(_number(keys, name, 1.0 if default else 0.0))


def _colour(keys: dict[str, str], name: str, default: str) -> list[float]:
    tokens = keys.get(name.lower(), default).split()
    values = [_atof(t) for t in tokens] + [255.0] * 3
    return [max(0.0, min(COLOUR_FULL, v)) / COLOUR_FULL for v in values[:3]]


def _rotation(entity: dict[str, Any]) -> tuple[list[float], list[float]]:
    """`(rotation quaternion Unreal, angles_deg Source)` off the row's `angles` block."""

    from elysium_pipeline.importers.map_geometry import gltf_quat_to_unreal

    angles = entity.get("angles") or {}
    gltf = angles.get("gltf")
    quat = list(gltf_quat_to_unreal(gltf)) if gltf else [0.0, 0.0, 0.0, 1.0]
    source = angles.get("source") or [0.0, 0.0, 0.0]
    return quat, [float(v) for v in source]


def _origin(entity: dict[str, Any]) -> tuple[list[float] | None, list[float] | None]:
    """`(origin_cm Unreal, origin Source inches)`; `(None, None)` when the row has no origin."""

    from elysium_pipeline.importers.map_geometry import gltf_position_to_unreal

    origin = entity.get("origin") or {}
    gltf = origin.get("gltf")
    if not gltf:
        return None, None
    source = origin.get("source") or [0.0, 0.0, 0.0]
    return list(gltf_position_to_unreal(gltf)), [float(v) for v in source]


def brush_bounds(model_row: dict[str, Any], origin_cm: Sequence[float] | None
                 ) -> tuple[dict[str, list[float]], float]:
    """`(bounds_cm {min, max} in Unreal cm, volume_scale)` for one `models[N]` row.

    The row's `mins`/`maxs` are glTF metres; the reflection into the Unreal frame swaps axes, so
    the extremes are taken after the transform. A brush entity that carries an `origin` has its
    brushes re-centred by vbsp, so the world box is the model box plus that origin. The volume
    scalar is `CFuncParticle::Activate`'s, over the Source-unit extents, clamped.
    """

    from elysium_pipeline.exporters.UE_map_sidecars import source_position
    from elysium_pipeline.importers.map_geometry import gltf_position_to_unreal

    a = gltf_position_to_unreal(model_row["mins"])
    b = gltf_position_to_unreal(model_row["maxs"])
    shift = list(origin_cm) if origin_cm is not None else [0.0, 0.0, 0.0]
    low = [min(a[k], b[k]) + shift[k] for k in range(3)]
    high = [max(a[k], b[k]) + shift[k] for k in range(3)]
    src_a = source_position(model_row["mins"])
    src_b = source_position(model_row["maxs"])
    volume = 1.0
    for k in range(3):
        volume *= abs(src_b[k] - src_a[k])
    scale = max(VOLUME_SCALE_MIN, min(VOLUME_SCALE_MAX, volume * VOLUME_SCALE_UNIT))
    return {"min": low, "max": high}, scale


def _model_index(entity: dict[str, Any]) -> int | None:
    model = entity.get("model") or {}
    if model.get("kind") == "brush":
        return int(model["index"])
    return None


def _particle_reference(entity: dict[str, Any]) -> tuple[str | None, bool]:
    """`(vtmb:particle:<key>, resolved)` off the row's `references[]`, or `(None, False)`."""

    for row in entity.get("references") or []:
        if row.get("role") == "particle":
            return str(row.get("asset") or "") or None, bool(row.get("resolved"))
    return None, False


# --------------------------------------------------------------------------------- ramps


def ramp_of(parsed: dict[str, Any] | None, frames: float, scale: float,
            default: float) -> list[list[float]] | None:
    """One key's `parsed` value as the staged keyframe list `[[t, lo, hi], ...]`.

    A scalar is `[[0, v, v]]`, a range `[[0, a, b]]`, a ramp one keyframe per element: a `v(n)`
    position is a frame index normalised by `frames` (negative wraps from the end), an element
    without one is spaced evenly over `[0, 1]` by its index (the particle seam's own "Ramps" rule;
    INFERRED for the unpinned form). Returns None for a value with no number in it, so the caller
    can warn rather than drop it.
    """

    if parsed is None:
        return [[0.0, default, default]]
    kind = parsed.get("kind")
    values = parsed.get("values") or []
    positions = parsed.get("positions") or []
    if kind == "scalar" and values:
        v = float(values[0]) * scale
        return [[0.0, v, v]]
    if kind == "range" and len(values) == 2:
        return [[0.0, float(values[0]) * scale, float(values[1]) * scale]]
    if kind != "ramp" or not values:
        return None
    count = len(values)
    keys: list[list[float]] = []
    for index, value in enumerate(values):
        position = positions[index] if index < len(positions) else None
        if position is not None and frames > 0:
            n = float(position)
            t = (frames + n) / frames if n < 0 else n / frames
        else:
            t = index / (count - 1) if count > 1 else 0.0
        t = max(0.0, min(1.0, t))
        if isinstance(value, dict) and value.get("kind") == "range":
            lo, hi = (float(value["values"][0]) * scale, float(value["values"][1]) * scale)
        elif isinstance(value, (int, float)):
            lo = hi = float(value) * scale
        elif isinstance(value, str) and _NUMBER_PREFIX.match(value.strip()):
            # `255!` (the warrens sparks): the engine's `atof` reads the numeric prefix.
            lo = hi = _atof(value) * scale
        else:
            return None
        keys.append([t, lo, hi])
    return keys


def _scalar_of(parsed: dict[str, Any] | None, default: float) -> float:
    if not parsed:
        return default
    values = parsed.get("values") or []
    if parsed.get("kind") in ("scalar", "range", "ramp") and values:
        first = values[0]
        if isinstance(first, dict):
            first = (first.get("values") or [default])[0]
        try:
            return float(first)
        except (TypeError, ValueError):
            return default
    return default


def _text_of(parsed: dict[str, Any] | None, raw: str) -> str:
    return raw.strip().strip('"')


# --------------------------------------------------------------------------------- units


@dataclass
class _Unit:
    """One particle unit's extension, indexed by block for the tree walk."""

    key: str
    extension: dict[str, Any]
    keys: list[dict[str, Any]]
    blocks: list[dict[str, Any]]

    @classmethod
    def of(cls, key: str, extension: dict[str, Any]) -> "_Unit":
        return cls(key, extension, list(extension.get("keys") or []),
                   list(extension.get("blocks") or []))

    def scope(self, block: int | None) -> dict[str, dict[str, Any]]:
        """`{key: keys[] row}` for one block (None = the definition body), last wins."""

        out: dict[str, dict[str, Any]] = {}
        for row in self.keys:
            if row.get("block") == block:
                out[str(row.get("key") or "").lower()] = row
        return out

    def children_of(self, parent: int | None, kind: str) -> list[dict[str, Any]]:
        return [b for b in self.blocks if b.get("parent") == parent and b.get("kind") == kind]

    def dependency(self, role: str, key_hint: str | None = None) -> dict[str, Any] | None:
        for row in self.extension.get("dependencies") or []:
            if row.get("role") != role:
                continue
            if key_hint is None or key_hint in str(row.get("asset") or ""):
                return row
        return None

    @property
    def role(self) -> str:
        return str(self.extension.get("role") or "neither")


def particle_unit_reader(root: Path | None = None) -> Callable[[str], dict[str, Any] | None]:
    """`root key -> particle extension | None` over `$ELYSIUM_EXPORT_V2_ROOT/particles/`, cached."""

    base = (Path(root) if root is not None else paths.export_v2_root()) / "particles"
    cache: dict[str, dict[str, Any] | None] = {}

    def read(key: str) -> dict[str, Any] | None:
        folded = normalize_particle_key(key)
        if folded in cache:
            return cache[folded]
        path = base / Path(*particle_output_relative_path(folded).parts)
        document = None
        if path.is_file():
            glb, _binary = read_glb(path)
            document = (glb.get("extensions") or {}).get(PARTICLE_EXTENSION)
        cache[folded] = document
        return document

    return read


def sprite_texture_key(sprite: str) -> str:
    """The texture lane's key for a particle sprite: `particles/<stem>`, folded."""

    stem = normalize_particle_key(sprite)
    if stem.endswith(".tga"):
        stem = stem[:-4]
    return f"particles/{stem}"


# --------------------------------------------------------------------------------- the tree


@dataclass
class TreeBuilder:
    """Walks one root's closure into the staged `particleTrees{}` entry.

    `read_unit(key)` returns a particle unit's extension or None; `read_texture(texture key)`
    the texture lane's staged sidecar (`width`, `height`, `assetPath`) or None. A sprite the
    texture lane has not imported is collected in `missing_textures` and fails the stage at the
    end (the sprite would otherwise draw nothing, silently). Keys with no home in the runtime's
    tables land in `unread_keys` -- a warning naming the definition, the offset and the key.
    """

    read_unit: Callable[[str], dict[str, Any] | None]
    read_texture: Callable[[str], dict[str, Any] | None]
    missing_textures: list[str] = field(default_factory=list)
    unread_keys: list[dict[str, Any]] = field(default_factory=list)
    unresolved_children: list[dict[str, Any]] = field(default_factory=list)
    keyframe_counts: dict[int, int] = field(default_factory=dict)

    # -- sprites --

    def _sprite(self, unit_key: str, value: str) -> dict[str, Any] | None:
        if not value:
            return None
        if value.lower().startswith("materials/"):
            # No shipped definition names a material; the unit would publish a material
            # dependency and the seam has no sprite for it. Recorded, never drawn.
            self.unread_keys.append({"definition": unit_key, "key": "sprite", "value": value,
                                     "reason": "a materials/ sprite has no texture unit"})
            return None
        stem = sprite_texture_key(value)
        sidecar = self.read_texture(stem)
        width = int((sidecar or {}).get("width") or 0)
        height = int((sidecar or {}).get("height") or 0)
        if sidecar is None or not width or not height:
            self.missing_textures.append(stem)
            return None
        longest = float(max(width, height))
        return {
            "id": f"vtmb:image:{stem}.tga",
            "texture": str(sidecar.get("assetPath") or ""),
            "size_px": [width, height],
            "aspect": [0.5 * width / longest, 0.5 * height / longest],
        }

    # -- ramps --

    def _ramps(self, unit_key: str, scope: dict[str, dict[str, Any]],
               table: dict[str, tuple[str, float, float]], frames: float) -> dict[str, Any]:
        out: dict[str, Any] = {}
        for key, (name, default, scale) in table.items():
            row = scope.get(key)
            ramp = ramp_of(row.get("parsed") if row else None, frames, scale, default * scale)
            if ramp is None:
                self.unread_keys.append({
                    "definition": unit_key, "key": key, "offset": row.get("offset"),
                    "value": row.get("value"), "reason": "value carries no number"})
                ramp = [[0.0, default * scale, default * scale]]
            self.keyframe_counts[len(ramp)] = self.keyframe_counts.get(len(ramp), 0) + 1
            out[name] = ramp
        return out

    def _warn_unknown(self, unit_key: str, scope: dict[str, dict[str, Any]],
                      known: frozenset[str] | set[str], unread: frozenset[str] = frozenset()
                      ) -> None:
        for key, row in scope.items():
            if key in known or key in unread:
                continue
            self.unread_keys.append({
                "definition": unit_key, "key": key, "offset": row.get("offset"),
                "value": row.get("value"), "reason": "no row in the runtime's key tables"})

    # -- blocks --

    def _spawn_block(self, unit: _Unit, block: dict[str, Any], frames: float) -> dict[str, Any]:
        scope = unit.scope(int(block["index"]))
        self._warn_unknown(unit.key, scope, set(SPAWN_RAMPS) | SPAWN_SCALARS,
                           UNREAD_SPAWN_KEYS | NESTED_COLLIDE_KEYS)
        spawn = self._ramps(unit.key, scope, SPAWN_RAMPS, frames)
        timescale_row = scope.get("timescale")
        spawn["timescale"] = _scalar_of(timescale_row.get("parsed") if timescale_row else None, 1.0)
        distance_row = scope.get("distance")
        spawn["distance"] = bool(_scalar_of(distance_row.get("parsed") if distance_row else None,
                                            0.0))
        return spawn

    def _collide(self, unit: _Unit, block: dict[str, Any]) -> tuple[dict[str, Any], list[dict]]:
        """The collide record (minus its `spawn` node indexes, filled by the walk) and the
        nested spawn blocks that become children."""

        index = int(block["index"])
        scope = unit.scope(index)
        self._warn_unknown(unit.key, scope, COLLIDE_KEYS)
        nested_spawns = unit.children_of(index, "spawn")
        nested = False
        found: dict[str, dict[str, Any]] = dict(scope)
        for spawn_block in nested_spawns:
            inner = unit.scope(int(spawn_block["index"]))
            for key in ("bounce", "friction", "gravity", "drag"):
                if key in inner and key not in found:
                    found[key] = inner[key]
                    nested = True

        def number(key: str, default: float) -> float:
            row = found.get(key)
            return _scalar_of(row.get("parsed") if row else None, default)

        decals: list[dict[str, Any]] = []
        vdecal: dict[str, Any] | None = None
        for decal_block in unit.children_of(index, "decal"):
            decal_scope = unit.scope(int(decal_block["index"]))
            self._warn_unknown(unit.key, decal_scope, DECAL_KEYS | COLLIDE_KEYS)
            particle_row = decal_scope.get("particle")
            spread_row = decal_scope.get("angle_spread")
            if particle_row is not None:
                decal_key = normalize_particle_key(str(particle_row.get("value") or ""))
                decal_unit = self.read_unit(decal_key)
                texture = ""
                if decal_unit is not None:
                    sprite_row = _Unit.of(decal_key, decal_unit).scope(None).get("sprite")
                    sprite = self._sprite(decal_key, str(sprite_row.get("value") or "")
                                          if sprite_row else "")
                    texture = (sprite or {}).get("texture", "")
                decals.append({
                    "id": f"vtmb:particle:{decal_key}",
                    "texture": texture,
                    "angle_spread": _scalar_of(spread_row.get("parsed") if spread_row else None,
                                               0.0),
                })
            for key in ("vdecal_first", "vdecal_last", "vdecal_angle_spread"):
                if key in decal_scope and key not in found:
                    found[key] = decal_scope[key]
        if "vdecal_first" in found or "vdecal_last" in found:
            vdecal = {
                "first": str((found.get("vdecal_first") or {}).get("value") or ""),
                "last": str((found.get("vdecal_last") or {}).get("value") or ""),
                "angle_spread": number("vdecal_angle_spread", 0.0),
            }
        record = {
            "bounce": number("bounce", 1.0),
            "friction": number("friction", 1.0),
            "gravity": number("gravity", 0.0),
            "drag": number("drag", 1.0),
            "self": bool(number("self", 0.0)),
            "nested": nested,
            "spawn": [],
            "decals": decals,
            "vdecal": vdecal,
        }
        return record, nested_spawns

    # -- nodes --

    def _node(self, unit: _Unit | None, key: str, name: str, index: int, parent: int | None,
              via: str, block_index: int | None, depth: int, spawn: dict[str, Any] | None,
              timescale: float) -> dict[str, Any]:
        node: dict[str, Any] = {
            "index": index, "id": f"vtmb:particle:{key}", "name": name,
            "kind": "root" if index == 0 else "leaf", "draws": False, "spawns": False,
            "parent": parent, "via": via, "blockIndex": block_index, "depth": depth,
            "resolved": unit is not None, "fps": DEFAULT_FPS,
            "lifetime_s": 1.0, "lifetime_min_s": 1.0, "lifetime_max_s": 1.0, "loop": False,
        }
        if unit is None:
            node.update({name_: [[0.0, d * s, d * s]] for _, (name_, d, s) in PARTICLE_RAMPS.items()})
            node.update({
                "spawn": spawn, "movealign": False, "flat": False, "sortfront": False,
                "no_z_test": False, "lighting": False, "precipitation": False,
                "depth_offset_cm": 0.0, "surface_color_optout": False, "sprite": None,
                "normal": None, "collide": None,
            })
            return node
        scope = unit.scope(None)
        self._warn_unknown(unit.key, scope, set(PARTICLE_RAMPS) | DEFINITION_KEYS,
                           UNREAD_PARTICLE_KEYS)

        def number(key_: str, default: float) -> float:
            row = scope.get(key_)
            return _scalar_of(row.get("parsed") if row else None, default)

        fps = number("fps", DEFAULT_FPS) or DEFAULT_FPS
        frames = number("frames", fps)
        min_frames = number("min_frames", frames)
        max_frames = number("max_frames", frames)
        divisor = fps * (timescale if timescale else 1.0)
        role = unit.role
        draws = role in ("drawing", "both")
        spawns = role in ("emitter", "both")
        if index == 0:
            kind = "root"
        elif role == "both":
            kind = "both"
        elif role == "drawing":
            kind = "leaf"
        else:
            kind = "spawn"
        sprite_row = scope.get("sprite")
        normal_row = scope.get("normal")
        node.update({
            "kind": kind, "draws": draws, "spawns": spawns, "fps": fps,
            "lifetime_s": frames / divisor,
            "lifetime_min_s": min_frames / divisor,
            "lifetime_max_s": max_frames / divisor,
            "loop": bool(number("loop", 0.0)),
        })
        node.update(self._ramps(unit.key, scope, PARTICLE_RAMPS, frames))
        node.update({
            "spawn": spawn,
            "movealign": bool(number("movealign", 0.0)),
            "flat": bool(number("flat", 0.0)),
            "sortfront": bool(number("sortfront", 0.0)),
            "no_z_test": bool(number("no_z_test", 0.0)),
            "lighting": bool(number("lighting", 0.0)),
            "precipitation": bool(number("precipitation", 0.0)),
            "depth_offset_cm": number("depth_offset", 0.0) * INCH_TO_CM,
            "surface_color_optout": (
                ("surface_color" in scope and number("surface_color", 1.0) == 0.0)
                or ("use_surface_color" in scope and number("use_surface_color", 1.0) == 0.0)
                or ("ignore_surface_color" in scope and number("ignore_surface_color", 0.0) != 0.0)
            ),
            "sprite": self._sprite(unit.key, str(sprite_row.get("value") or ""))
            if sprite_row else None,
            "normal": self._sprite(unit.key, str(normal_row.get("value") or ""))
            if normal_row and normal_row.get("parsed", {}).get("kind") == "text" else None,
            "collide": None,
        })
        return node

    def build(self, root_key: str, root_name: str) -> dict[str, Any]:
        """The `particleTrees{}` entry for one root: nodes root first, parents before children."""

        root_key = normalize_particle_key(root_key)
        nodes: list[dict[str, Any]] = []
        max_depth = 0

        def walk(key: str, name: str, parent: int | None, via: str, block_index: int | None,
                 depth: int, spawn: dict[str, Any] | None, timescale: float,
                 path: frozenset[str]) -> int:
            nonlocal max_depth
            extension = self.read_unit(key)
            unit = _Unit.of(key, extension) if extension is not None else None
            index = len(nodes)
            node = self._node(unit, key, name, index, parent, via, block_index, depth, spawn,
                              timescale)
            nodes.append(node)
            max_depth = max(max_depth, depth)
            if unit is None:
                if index > 0:
                    self.unresolved_children.append({"root": f"vtmb:particle:{root_key}",
                                                     "node": index, "name": name})
                return index
            frames = node["fps"] * node["lifetime_s"] * (timescale if timescale else 1.0)
            for block in unit.children_of(None, "spawn"):
                child_row = unit.scope(int(block["index"])).get("particle")
                if child_row is None:
                    continue
                child_name = str(child_row.get("value") or "")
                child_key = normalize_particle_key(child_name)
                if not child_key or child_key in path:
                    continue
                child_spawn = self._spawn_block(unit, block, frames)
                walk(child_key, child_name, index, "spawn", int(block["index"]), depth + 1,
                     child_spawn, float(child_spawn["timescale"]), path | {child_key})
            for block in unit.children_of(None, "collide"):
                record, nested_spawns = self._collide(unit, block)
                node["collide"] = record
                for spawn_block in nested_spawns:
                    child_row = unit.scope(int(spawn_block["index"])).get("particle")
                    if child_row is None:
                        continue
                    child_name = str(child_row.get("value") or "")
                    child_key = normalize_particle_key(child_name)
                    if not child_key or child_key in path:
                        continue
                    child_spawn = self._spawn_block(unit, spawn_block, frames)
                    child = walk(child_key, child_name, index, "collide",
                                 int(spawn_block["index"]), depth + 1, child_spawn,
                                 float(child_spawn["timescale"]), path | {child_key})
                    record["spawn"].append(child)
            return index

        walk(root_key, root_name, None, "spawn", None, 0, None, 1.0, frozenset({root_key}))
        max_keyframes = 0
        for node in nodes:
            for value in node.values():
                if isinstance(value, list) and value and isinstance(value[0], list):
                    max_keyframes = max(max_keyframes, len(value))
            for value in (node.get("spawn") or {}).values():
                if isinstance(value, list) and value and isinstance(value[0], list):
                    max_keyframes = max(max_keyframes, len(value))
        return {
            "root": f"vtmb:particle:{root_key}",
            "name": root_name,
            "nodes": nodes,
            "stats": {
                "leafCount": sum(1 for node in nodes if node["draws"]),
                "depth": max_depth,
                "maxKeyframes": max_keyframes,
            },
        }


# --------------------------------------------------------------------------------- rows


def effect_rows(entities: Sequence[dict[str, Any]], models: dict[int, dict[str, Any]],
                sky) -> list[dict[str, Any]]:
    """One `effects[]` row per `env_particle` / `func_particle` entity, lump order."""

    rows: list[dict[str, Any]] = []
    for entity in entities:
        classname = str(entity.get("classname") or "").lower()
        if classname not in EFFECT_CLASSES:
            continue
        keys = _key_values(entity)
        origin_cm, origin_src = _origin(entity)
        rotation, angles = _rotation(entity)
        model_index = _model_index(entity)
        particle, resolved = _particle_reference(entity)
        bounds = None
        volume_scale = 1.0
        if classname == "func_particle":
            attach = FUNC_PARTICLE_ATTACH
            if model_index is not None and model_index in models:
                bounds, volume_scale = brush_bounds(models[model_index], origin_cm)
        else:
            attach = int(_number(keys, "attach_type", 0.0))
        point = sky.entity_point(origin_src or [0.0, 0.0, 0.0], keys.get("model", ""))
        row = {
            "index": int(entity["index"]),
            "classname": classname,
            "targetname": keys.get("targetname") or None,
            "origin_cm": origin_cm or [0.0, 0.0, 0.0],
            "rotation": rotation,
            "angles_deg": angles,
            "attach_type": attach,
            "parentname": keys.get("parentname") or None,
            "bone": keys.get("bone") or None,
            "attach_point": int(_number(keys, "attach_point", 0.0)),
            "active": _flag(keys, "active", True),
            "start_hidden": _flag(keys, "StartHidden", False),
            "spawnbounds_cm": _number(keys, "spawnbounds", DEFAULT_SPAWNBOUNDS_IN) * INCH_TO_CM,
            "ramp_scale": _number(keys, "ramp_scale", 1.0),
            "ramp_time": _number(keys, "ramp_time", 0.0),
            "bounds_cm": bounds,
            "volume_scale": volume_scale,
            "particle": particle if resolved else None,
            "particle_definition": keys.get("particle_definition", ""),
            "sky": bool(sky.is_sky(point)),
            "spawnflags": int(_number(keys, "spawnflags", 0.0)),
        }
        if not resolved:
            row["unresolved"] = True
        rows.append(row)
    return rows


def dustmote_rows(entities: Sequence[dict[str, Any]], models: dict[int, dict[str, Any]],
                  sky) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for entity in entities:
        if str(entity.get("classname") or "").lower() != DUST_CLASS:
            continue
        keys = _key_values(entity)
        origin_cm, origin_src = _origin(entity)
        model_index = _model_index(entity)
        bounds = None
        if model_index is not None and model_index in models:
            bounds, _scale = brush_bounds(models[model_index], origin_cm)
        point = sky.entity_point(origin_src or [0.0, 0.0, 0.0], keys.get("model", ""))
        d = DUST_DEFAULTS
        rows.append({
            "index": int(entity["index"]),
            "targetname": keys.get("targetname") or None,
            "sky": bool(sky.is_sky(point)),
            "model": model_index,
            "bounds_cm": bounds,
            "spawn_rate": _number(keys, "SpawnRate", d["SpawnRate"]),
            "color": _colour(keys, "Color", d["Color"]),
            "alpha": max(0.0, min(COLOUR_FULL, _number(keys, "Alpha", d["Alpha"]))) / COLOUR_FULL,
            "speed_max_cm_s": _number(keys, "SpeedMax", d["SpeedMax"]) * INCH_TO_CM,
            "size_min_cm": _number(keys, "SizeMin", d["SizeMin"]) * INCH_TO_CM,
            "size_max_cm": _number(keys, "SizeMax", d["SizeMax"]) * INCH_TO_CM,
            "lifetime_min_s": _number(keys, "LifetimeMin", d["LifetimeMin"]),
            "lifetime_max_s": _number(keys, "LifetimeMax", d["LifetimeMax"]),
            "dist_max_cm": _number(keys, "DistMax", d["DistMax"]) * INCH_TO_CM,
            "frozen": _flag(keys, "Frozen", False),
            "start_disabled": _flag(keys, "StartDisabled", False),
            "sprite": "vtmb:material:particle/sparkles",
        })
    return rows


def steam_rows(entities: Sequence[dict[str, Any]], sky) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for entity in entities:
        if str(entity.get("classname") or "").lower() != STEAM_CLASS:
            continue
        keys = _key_values(entity)
        origin_cm, origin_src = _origin(entity)
        rotation, angles = _rotation(entity)
        d = STEAM_DEFAULTS
        speed = _number(keys, "Speed", d["Speed"])
        jet_length = _number(keys, "JetLength", d["JetLength"])
        rows.append({
            "index": int(entity["index"]),
            "targetname": keys.get("targetname") or None,
            "origin_cm": origin_cm or [0.0, 0.0, 0.0],
            "rotation": rotation,
            "angles_deg": angles,
            "sky": bool(sky.is_sky(origin_src)),
            "type": int(_number(keys, "type", d["type"])),
            "initial_state": _flag(keys, "InitialState", False),
            "spread_speed_cm_s": _number(keys, "SpreadSpeed", d["SpreadSpeed"]) * INCH_TO_CM,
            "speed_cm_s": speed * INCH_TO_CM,
            "start_size_cm": _number(keys, "StartSize", d["StartSize"]) * INCH_TO_CM,
            "end_size_cm": _number(keys, "EndSize", d["EndSize"]) * INCH_TO_CM,
            "rate": _number(keys, "Rate", d["Rate"]),
            "jet_length_cm": jet_length * INCH_TO_CM,
            "lifetime_s": (jet_length / speed) if speed else 0.0,
            "color": _colour(keys, "rendercolor", d["rendercolor"]),
            "alpha": max(0.0, min(COLOUR_FULL, _number(keys, "renderamt", d["renderamt"])))
            / COLOUR_FULL,
        })
    return rows


def beam_rows(entities: Sequence[dict[str, Any]], sky,
              read_material: Callable[[str], dict[str, Any] | None],
              missing_materials: list[str]) -> list[dict[str, Any]]:
    from elysium_pipeline.formats.particle_glb.model import normalize_material_path
    from elysium_pipeline.importers import materials as material_lane

    rows: list[dict[str, Any]] = []
    for entity in entities:
        if str(entity.get("classname") or "").lower() != BEAM_CLASS:
            continue
        keys = _key_values(entity)
        origin_cm, origin_src = _origin(entity)
        d = BEAM_DEFAULTS
        width = _number(keys, "BoltWidth", d["BoltWidth"]) * INCH_TO_CM
        material_key = normalize_material_path(keys.get("texture", "sprites/beama"))
        if read_material(material_key) is None:
            missing_materials.append(material_key)
        rows.append({
            "index": int(entity["index"]),
            "targetname": keys.get("targetname") or None,
            "origin_cm": origin_cm or [0.0, 0.0, 0.0],
            "sky": bool(sky.is_sky(origin_src)),
            "start": keys.get("lightningstart") or None,
            "end": keys.get("lightningend") or None,
            "width_cm": width,
            "end_width_cm": width * BEAM_END_WIDTH_FRACTION,
            "noise_amplitude_cm": _number(keys, "NoiseAmplitude", d["NoiseAmplitude"]) * INCH_TO_CM,
            "texture": f"vtmb:material:{material_key}",
            "material": material_lane.asset_path_for(material_key),
            "texture_scroll": _number(keys, "TextureScroll", d["TextureScroll"]),
            "radius_cm": _number(keys, "Radius", d["Radius"]) * INCH_TO_CM,
            "life_s": _number(keys, "life", d["life"]),
            "strike_time_s": _number(keys, "StrikeTime", d["StrikeTime"]),
            "damage": _number(keys, "damage", d["damage"]),
            "color": _colour(keys, "rendercolor", d["rendercolor"]),
            "alpha": max(0.0, min(COLOUR_FULL, _number(keys, "renderamt", d["renderamt"])))
            / COLOUR_FULL,
            "spawnflags": int(_number(keys, "spawnflags", d["spawnflags"])),
            "start_hidden": _flag(keys, "StartHidden", False),
            "impact_particle": keys.get("impact_particle") or None,
            "faces_player": _flag(keys, "faces_player", False),
            "framerate": _number(keys, "framerate", d["framerate"]),
            "framestart": _number(keys, "framestart", d["framestart"]),
            "renderfx": int(_number(keys, "renderfx", d["renderfx"])),
        })
    return rows


# --------------------------------------------------------------------------------- the stage


def stage_effects(
    entities: Sequence[dict[str, Any]],
    models: dict[int, dict[str, Any]],
    sky,
    *,
    read_unit: Callable[[str], dict[str, Any] | None],
    read_texture: Callable[[str], dict[str, Any] | None],
    read_material: Callable[[str], dict[str, Any] | None],
    map_name: str = "",
) -> dict[str, Any]:
    """The five staged tables plus `effectStats` for one map.

    `entities` are the entities unit's rows, `models` the root unit's `models[]` by index, `sky`
    the producer join's `SkyScope`. A sprite the texture lane has not imported, or a beam material
    the material lane has not staged, fails the whole map with every missing key named -- the
    owner's ruling draws all of them, so a silently empty emitter is exactly the defect.
    """

    effects = effect_rows(entities, models, sky)
    builder = TreeBuilder(read_unit, read_texture)
    trees: dict[str, dict[str, Any]] = {}
    unresolved_roots: list[dict[str, Any]] = []
    for row in effects:
        root = row.get("particle")
        if not root:
            unresolved_roots.append({"index": row["index"],
                                     "particle_definition": row.get("particle_definition", "")})
            continue
        if root not in trees:
            trees[root] = builder.build(root[len("vtmb:particle:"):], row["particle_definition"])
    dust = dustmote_rows(entities, models, sky)
    steam = steam_rows(entities, sky)
    missing_materials: list[str] = []
    beams = beam_rows(entities, sky, read_material, missing_materials)

    parts = []
    if builder.missing_textures:
        parts.append(f"{len(set(builder.missing_textures))} particle sprite(s) not staged by the "
                     f"texture lane (run: uv run elysium import textures): "
                     + ", ".join(sorted(set(builder.missing_textures))[:8]))
    if missing_materials:
        parts.append(f"{len(set(missing_materials))} beam material(s) not staged by the material "
                     f"lane (run: uv run elysium import materials): "
                     + ", ".join(sorted(set(missing_materials))[:8]))
    if parts:
        raise EffectsStageError(f"{map_name or 'map'}: " + "; ".join(parts))

    stats = {
        "rows": {"effects": len(effects), "dustmotes": len(dust), "steam": len(steam),
                 "beams": len(beams)},
        "roots": len(trees),
        "unresolvedRoots": unresolved_roots,
        "unresolvedChildren": builder.unresolved_children,
        "keyframeHistogram": {str(k): v for k, v in sorted(builder.keyframe_counts.items())},
        "maxDepth": max((t["stats"]["depth"] for t in trees.values()), default=0),
        "maxLeaves": max((t["stats"]["leafCount"] for t in trees.values()), default=0),
        "unreadKeys": builder.unread_keys,
    }
    return {
        "effects": effects,
        "particleTrees": trees,
        "dustmotes": dust,
        "steam": steam,
        "beams": beams,
        "effectStats": stats,
    }


def stage_effects_for_join(join, *, read_unit=None, read_texture=None, read_material=None,
                           work_root: Path | None = None, export_v2_root: Path | None = None,
                           map_name: str = "") -> dict[str, Any]:
    """`stage_effects` over one `UE_map_sidecars.MapJoin`, with the default readers: the particle
    units under the export_v2 root, the texture and material lanes' staged sidecars."""

    from elysium_pipeline.importers import map_geometry as MG
    from elysium_pipeline.importers import textures as texture_lane

    root = Path(work_root) if work_root is not None else paths.work_root()
    if read_unit is None:
        read_unit = particle_unit_reader(export_v2_root)
    if read_texture is None:
        read_texture = MG.texture_sidecar_reader(texture_lane.staging_root(root))
    if read_material is None:
        read_material = MG.sidecar_reader(MG.material_staging_root(root))
    units = join.units
    models = {int(row["index"]): row for row in units.root["models"]}
    return stage_effects(
        units.entities["entities"], models, join.sky,
        read_unit=read_unit, read_texture=read_texture, read_material=read_material,
        map_name=map_name or units.name)


def summary_line(product: dict[str, Any]) -> str:
    stats = product["effectStats"]
    rows = stats["rows"]
    return (f"{rows['effects']} effects over {stats['roots']} roots "
            f"({len(stats['unresolvedRoots'])} unresolved, "
            f"{len(stats['unresolvedChildren'])} unresolved children, "
            f"max depth {stats['maxDepth']}, max leaves {stats['maxLeaves']}, "
            f"keyframes {stats['keyframeHistogram']}), "
            f"{rows['dustmotes']} dustmotes, {rows['steam']} steam, {rows['beams']} beams, "
            f"{len(stats['unreadKeys'])} unread key(s)")


def dumps(product: dict[str, Any]) -> str:
    return json.dumps(product, indent=1, sort_keys=False)
