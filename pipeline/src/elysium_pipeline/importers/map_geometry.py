"""The map root unit's geometry and placements, read for the V2 map bake (R5.1).

`docs/project/seam_migration.md` -> "Roadmap -- one pipeline" R5.1 asks for a map authored from the
published map root unit -- its `world`, `brushModels`, `displacements` and `placements` scenes --
instead of the legacy `<map>.obj` / `<map>_sky.obj` / `brushes/*.obj` / `<map>.props` sidecars. The
ruling this module executes is `docs/architecture/seam_map_map.md` -> "## Import -- geometry and
placements (R5.1)"; nothing here decides anything that section does not already state.

This is the **offline half**, and it has to be: reading the unit needs `numpy` (the sky-area BSP
walk, the accessor decode) and Unreal's embedded CPython does not carry it. So this module runs in
the `uv` interpreter, writes one staged manifest plus one packed vertex file per map under
`$ELYSIUM_WORK_ROOT/import/map_geometry/<map>/`, and `pipeline/unreal/bake_map_v2.py` reads that pair
with nothing but `json`, `struct` and `array`. It is the same offline-stage / editor-import shape the
model, material and texture lanes already use, and it is what lets a pytest case walk a real map with
no Unreal process at all.

**One classifier, not two.** The world/3D-sky/brush-model split, the `tools/` drop, the
`func_areaportalwindow` backing drop and the sky-area membership test are the R3.2 producer's
(`exporters.UE_map_sidecars.prepare_join`), imported rather than re-derived: two implementations of
"which faces are the miniature" is exactly the divergence R3.3 exists to catch, and the map bake and
the `.hulls`/`.ents` sidecars must agree by construction.

**The frame.** The unit publishes glTF metres, Y-up, right-handed; the bake wants Unreal
centimetres, Z-up, left-handed. That is `(x, y, z)_gltf -> (x, z, y)_unreal * 100`, a reflection, so
every triangle's winding is reversed on the way out -- the same reversal `UE_bsp_to_scene` applied at
OBJ-write time, for the same reason (`formats.bsp.source_to_unreal`).

**Materials (R5.4).** Every face group also names its material *unit* -- the `vtmb:material:<key>`
the root unit's `textures[]` row resolved, a PAKFILE-patched face by its `maps/<map>/...` id -- and
the stage resolves each one through the material lane's own staged provenance sidecars
(`$ELYSIUM_WORK_ROOT/import/materials/<key>.provenance.json`) to the imported `MI_` asset path
(`importers.materials.asset_path_for`, the same pure function that named the asset), the root
master and the blend mode (`seam_map_map.md` -> "## Import -- materials (R5.4)"). The sidecars,
not `manifest.json`: that manifest describes the material lane's LAST run, which a `--select`
narrows to one directory, while every staged unit's sidecar stays on disk until its own scope
prunes it. The editor half binds exactly that asset; no `<map>.mtl` table and no per-map material
package is consulted for a surface. A unit the material lane has not staged is a loud
`MapGeometryError`, never a silently unbound slot.
"""

from __future__ import annotations

from array import array
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path
import sys
from typing import Any, Sequence

import numpy as np

from elysium_pipeline import paths, shared_corpus
from elysium_pipeline.exporters import UE_map_sidecars as sidecars
from elysium_pipeline.formats.bsp import source_to_unreal

#: The staged pair the editor half reads, below `$ELYSIUM_WORK_ROOT/import/map_geometry/<map>/`.
FAMILY = "map_geometry"
MANIFEST_NAME = "manifest.json"
VERTEX_NAME = "geometry.bin"
MANIFEST_SCHEMA = "elysium.map-geometry"
#: 2 (R5.4): the manifest carries `materials`, one row per face group, and `materialReport`.
#: 3 (R5.5): the manifest carries `cubemaps`, one row per lump-42 sample, the reflection-capture
#: placements (`docs/architecture/seam_map_map.md` -> "## Import -- reflection captures (R5.5)").
#: 4 (R5.6): the manifest carries `lights`, one row per lump-15 `worldLights[]` record in lump
#: order (`UE_map_sidecars.light_rows`, the `.lights` producer's own rows), the light placements
#: (`docs/architecture/seam_map_map_lighting.md` -> "## Import" -> "Lights final (R5.6)").
#: 5 (R6.3): the manifest carries `details`, the `dprp` game lump as `models[]` (the model
#: dictionary resolved to R1 stems) and `records[]` (one row per detail record, lump order), the
#: instanced placements (`docs/architecture/seam_map_map.md` -> "Detail props (R6.3)").
#: 6 (R6.1): the manifest carries `sprites`, one row per `env_sprite` in lump order, resolved to
#: the imported `MI_`, the texture size and the blend the entity's `rendermode` selects, the
#: billboard placements (`docs/architecture/seam_map_map.md` -> "Sprites (R6.1)").
#: 7 (R7.3): the manifest carries `effects`, `particleTrees`, `dustmotes`, `steam`, `beams` and
#: `effectStats` -- every effects entity joined to its particle closure, brush bounds and sprite
#: textures (`importers.effects`; `docs/architecture/seam_map_map.md` -> "Import -- effects
#: (R7.3)").
MANIFEST_VERSION = 7
#: The R5.4 material report beside the manifest -- every material the map binds, classified from
#: the import lane's provenance against the legacy `.mtl` lane's own master choice.
MATERIAL_REPORT_NAME = "materials_report.json"

#: The V2 blend modes a Nanite chunk may carry (Nanite is opaque/masked only), i.e. the
#: `basePropertyOverrides.blendMode` values of the root entry the legacy `MatDef.opaque` predicate
#: answered from the corpus flags.
NANITE_BLEND_MODES = frozenset({"Opaque", "Masked"})

#: The V2 masters `make_v2_materials.py` builds with `used_with_nanite=True`
#: (`_make_lit_master` for `M_V2_Lit`/`M_V2_LitTranslucent`, `make_unlit`, `make_two_texture`).
#: `M_V2_Water`, `M_V2_Refract`, `M_V2_Sprite`, `M_V2_Decal` and `M_V2_Eyes` deliberately do not
#: set the flag (review fix, R5.4): a face on one of those masters can never draw in a Nanite
#: chunk regardless of its instance's `blendMode` override, or Unreal falls back to the default
#: material at render time (`LogMaterial: ... missing usage flag Nanite!`), a silent-looking
#: failure the material-slot audit cannot see because it never touches Nanite usage.
NANITE_CAPABLE_MASTERS = frozenset({
    "M_V2_Lit", "M_V2_LitTranslucent", "M_V2_Unlit", "M_V2_TwoTexture",
})

#: The legacy `bake_map.Bake._master_for` selection, restated over a `shared/materials.json` record
#: so the R5.4 report can say what master a surface WAS on before the rebind (data, not a look
#: judgement). Same order as the bake's own if/elif chain.
LEGACY_MASTER_RULES = (
    ("decal", "M_Decal"), ("additive", "M_Additive"), ("refract", "M_Refract"),
    ("glass", "M_World_Glass"), ("water", "M_World_Translucent"), ("blend", "M_World_Translucent"),
    ("scissor", "M_World_Masked"),
)
LEGACY_MASTER_DEFAULT = "M_World_Opaque"

#: The proxy kinds the V2 masters implement live (`seam_map_material.md` -> "Proxy policy"); a
#: material carrying one of these on its V2 instance is animated now where the legacy `.mtl` lane
#: flattened it to a static bind. `animatedtexture` only animates when its frames array staged --
#: the provenance omission `animatedFramesArrayUnavailable` says when it did not.
ANIMATED_PROXY_KINDS = frozenset({"sine", "animatedtexture", "texturescroll"})
FRAMES_UNAVAILABLE_OMISSION = "animatedFramesArrayUnavailable"

#: The appearance CLASS a master + blend pair renders as, on either lane, so the report can say
#: whether the rebind moved a surface between classes (translucent -> opaque, glass ->
#: translucent, ...) rather than merely renamed its master. Data, never a look judgement.
LEGACY_MASTER_CLASS = {
    "M_World_Opaque": "opaque", "M_World_Masked": "masked", "M_World_Translucent": "translucent",
    "M_World_Glass": "translucent", "M_Refract": "refract", "M_Additive": "additive",
    "M_Decal": "decal",
}

#: glTF metres -> Unreal centimetres. One Source inch is 0.0254 glTF metres and 2.54 centimetres.
GLTF_TO_UNREAL = 100.0

#: `DStaticPropV4.flags` bit 0: this placement fades out with distance (`fadeMinDist`/`fadeMaxDist`
#: are Source inches; every other bit is a lighting/flashlight hint this lane does not consume).
STATIC_PROP_FLAG_FADES = 0x1

#: R6.1 (`seam_map_map.md` -> "Sprites (R6.1)"): the blend state an `env_sprite`'s `rendermode`
#: selects, the `$spriterendermode` row table of `seam_map_material.md` -> `M_V2_Sprite` (VtMB's
#: client writes the entity's mode into that material var at draw). Mode 6 (`kRenderEnvironmental`)
#: has no program and is a named failure; every other value is a mode the table does not name.
SPRITE_BLEND_BY_MODE = {
    0: "Opaque", 1: "Translucent", 2: "Translucent", 3: "Translucent", 4: "Translucent",
    5: "Additive", 7: "Additive", 8: "Additive", 9: "Translucent",
}
#: `kRenderGlow` / `kRenderWorldGlow`: the two modes `C_Sprite::DrawModel` routes through Source's
#: glow rule (screen-constant size, `19000 / dist^2`, the pixel-visibility fade).
SPRITE_GLOW_MODES = frozenset({3, 9})
#: `CSprite::Spawn` (vampire.dll 1042e550) clamps `scale` to this range (`DAT_10516cec`).
SPRITE_MAX_SCALE = 8.0
#: `env_sprite` spawnflag 1, "Start On": a named sprite without it spawns undrawn (`EF_NODRAW`).
SF_SPRITE_START_ON = 0x1

#: A `DISP_VERT` alpha is published as the lump's own 0..255 byte value; the bake's blend channel
#: (vertex `COLOR.r`, the `WorldVertexTransition` tex1/tex2 mix) is 0..1, exactly as the legacy
#: `.blend` sidecar wrote it.
DISP_ALPHA_FULL = 255.0

#: `SolidType_t` (`docs/vtmb/phy_vphysics.md` -> "Which entities get a collision model"). The bake
#: only distinguishes `SOLID_NONE` from the rest -- see the ruling in `seam_map_map.md`.
SOLID_NONE = 0


class MapGeometryError(ValueError):
    """The map root unit is missing something this lane cannot proceed without."""


# --------------------------------------------------------------------------------- the frame


def gltf_position_to_unreal(point: Sequence[float]) -> tuple[float, float, float]:
    """One glTF-metre position as Unreal centimetres."""

    return (
        float(point[0]) * GLTF_TO_UNREAL,
        float(point[2]) * GLTF_TO_UNREAL,
        float(point[1]) * GLTF_TO_UNREAL,
    )


def gltf_quat_to_unreal(quat: Sequence[float]) -> tuple[float, float, float, float]:
    """One glTF rotation quaternion `(x, y, z, w)` as the same rotation in the Unreal frame.

    The unit publishes `(x, y, z, w)_gltf = (x, z, -y, w)_source`, and
    `formats.bsp.source_quat_to_unreal` carries a Source quaternion into the reflected Unreal frame
    as `(-x, y, -z, w)`. Composing the two gives `(-x, -z, -y, w)` -- which names the same rotation
    as the legacy `.props` line, up to the overall sign a quaternion is free in.
    """

    return (-float(quat[0]), -float(quat[2]), -float(quat[1]), float(quat[3]))


def gltf_length_to_unreal(value: float) -> float:
    return float(value) * GLTF_TO_UNREAL


def source_inches_to_unreal(value: float) -> float:
    """A raw Source-inch scalar (a `sprp` fade distance) as centimetres."""

    return float(value) * 2.54


# --------------------------------------------------------------------------------- structures


@dataclass
class Scene:
    """One mesh-able scene in the shape `bake_lib.ObjModel` carries, plus its blend channel.

    `positions`/`uvs`/`blend` are parallel, one entry per emitted corner; `groups` maps the bake's
    material group key to a flat list of vertex indices, three per triangle, in Unreal winding.
    Corners are shared exactly where the source shared them -- within one displacement grid and
    within one face's triangle fan, never across a face boundary -- which is what makes recomputed
    normals come out flat across a BSP face boundary and smooth inside a displacement.
    """

    positions: list[tuple[float, float, float]] = field(default_factory=list)
    uvs: list[tuple[float, float]] = field(default_factory=list)
    blend: list[float] = field(default_factory=list)
    groups: dict[str, list[int]] = field(default_factory=dict)
    #: R5.4: group key -> the material unit key its faces resolved (`vtmb:material:<key>` without
    #: the prefix; a patched face keeps its `maps/<map>/...` id). One unit per group by
    #: construction -- `group_key` is a function of the raw key alone.
    units: dict[str, str] = field(default_factory=dict)

    @property
    def tri_count(self) -> int:
        return sum(len(indices) for indices in self.groups.values()) // 3

    def emit(self, position: Sequence[float], uv: Sequence[float], alpha: float = 0.0) -> int:
        self.positions.append(gltf_position_to_unreal(position))
        self.uvs.append((float(uv[0]), float(uv[1])))
        self.blend.append(float(alpha))
        return len(self.positions) - 1


@dataclass(frozen=True)
class Placement:
    """One `staticProps.props[]` record, resolved for the bake.

    `stem` is the R1 corpus stem (`shared_corpus.static_stem`'s fold of the whole model path), which
    is the asset name under `/ElysiumBaked/Meshes` and also the key the legacy `.props` sidecar used,
    so the two lanes place the same asset by the same name.
    """

    index: int
    stem: str
    model_path: str
    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]
    solid: int
    flags: int
    skin: int
    fade_min_cm: float
    fade_max_cm: float
    sky: bool

    @property
    def solid_blocks(self) -> bool:
        """Does this placement block? The placement record is the authority, not the model."""

        return self.solid != SOLID_NONE

    @property
    def fades(self) -> bool:
        return bool(self.flags & STATIC_PROP_FLAG_FADES) and self.fade_max_cm > 0.0


@dataclass(frozen=True)
class DetailPlacement:
    """One `detailProps.records[]` record (`dprp` v2), resolved for the bake (R6.3,
    `seam_map_map.md` -> "Detail props (R6.3)").

    `model` is the record's index into the lump's own dictionary and `stem` that entry's R1 corpus
    stem -- the same `static_stem` fold a static prop uses, so the instanced component and a static
    placement of the same model draw the same `/ElysiumBaked/Meshes/SM_<stem>`. `sway` is the
    record's `swayAmount` byte, raw (0..255); the editor half normalises it into the per-instance
    custom data float. The record's `lighting`/`lightStyles` are VRAD's baked answer for the 2004
    renderer and are not carried: the V2 lane lights the instances through the R5.6 light actors.
    """

    index: int
    model: int
    stem: str
    model_path: str
    position: tuple[float, float, float]
    rotation: tuple[float, float, float, float]
    sway: int
    sky: bool


@dataclass(frozen=True)
class SpriteRecord:
    """One `env_sprite` block of the entity lump, read for the bake (R6.1, `seam_map_map.md` ->
    "Sprites (R6.1)") before its material is resolved.

    `index` is the block's lump ordinal -- the entity's `FElysiumEntityHandle::Index`, the tag the
    baked actor carries and the key the leaf's visibility writes land on. `material` is the
    `model` key folded to its install key (`shared_corpus.material_key`); `scale` is already
    clamped to `0..SPRITE_MAX_SCALE` with 0 (or absent) read as 1, `CSprite::Spawn`'s own reading;
    `hidden` is `CSprite::Spawn`'s rule over `start_hidden`, the name and spawnflag 1.
    """

    index: int
    name: str
    material: str
    position: tuple[float, float, float]
    scale: float
    mode: int
    color: tuple[int, int, int]
    alpha: int
    fx: int
    hidden: bool
    sky: bool


@dataclass(frozen=True)
class CubemapSample:
    """One `cubemaps[]` row (lump 42), resolved to where the V2 bake stands a reflection capture
    (R5.5). `origin` is the row's own Source-inch integer triple -- the probe file name's, kept for
    provenance -- and `position` is the node translation in the bake's frame, the same
    `gltf_position_to_unreal` every placement takes. `sky` is the 3D-skybox area rule every content
    class shares, so a miniature sample takes the sky transform like a miniature light. The row's
    `size` (Source's capture resolution; 0 = default on the whole working corpus) is not carried:
    the capture's radius is the editor knob `UElysiumSurfaceSettings.CaptureRadius`, never a
    transcription.
    """

    index: int
    origin: tuple[int, int, int]
    position: tuple[float, float, float]
    sky: bool

    def as_row(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "origin": list(self.origin),
            "position": list(self.position),
            "sky": self.sky,
        }


@dataclass(frozen=True)
class MaterialBinding:
    """One face group's material, resolved through the material lane's staged manifest (R5.4).

    `asset` is the imported `MI_` the editor half binds; `master`/`blend_mode` are the ROOT unit's
    (a patched `maps/<map>/...` instance parents to its base instance, which parents to a master),
    so `opaque` answers the Nanite question the legacy `MatDef.opaque` answered from the corpus
    flags -- gated on the master's own Nanite capability (`NANITE_CAPABLE_MASTERS`) as well as the
    blend, since an instance's `blendMode` override (e.g. `water/sewer_water` on `M_V2_Water`,
    forced Opaque) cannot make a non-Nanite master's chunk drawable. `provenance` is the root
    unit's key (its sidecar is `<key>.provenance.json` below the material staging root).
    """

    key: str
    unit: str
    asset: str
    master: str
    blend_mode: str
    patched: bool
    provenance: str

    @property
    def opaque(self) -> bool:
        return self.blend_mode in NANITE_BLEND_MODES and self.master in NANITE_CAPABLE_MASTERS

    def as_row(self) -> dict[str, Any]:
        return {
            "unit": self.unit, "asset": self.asset, "master": self.master,
            "blendMode": self.blend_mode, "opaque": self.opaque, "patched": self.patched,
            "provenance": self.provenance,
        }


@dataclass
class MapGeometry:
    """One map's root unit, as much of it as the V2 bake authors."""

    map_name: str
    unit_path: Path
    unit_sha256: str
    world: Scene
    sky: Scene
    brushes: dict[int, Scene]
    placements: list[Placement]
    #: R6.3: every `dprp` record in lump order, resolved to its R1 stem (`DetailPlacement`).
    details: list[DetailPlacement]
    #: R6.1: every `env_sprite` block in lump order (`SpriteRecord`), unresolved -- `stage_map`
    #: joins each one to the material and texture lanes' sidecars (`resolve_sprite_table`).
    sprites: list[SpriteRecord]
    cubemaps: list[CubemapSample]
    #: R5.6: `UE_map_sidecars.light_rows` verbatim -- one dict per lump-15 record, the same rows
    #: `<map>.lights` is formatted from, so the staged table and the sidecar agree by construction.
    lights: list[dict[str, Any]]
    sky_scale: float
    sky_origin: tuple[float, float, float]
    sky_ok: bool
    counts: dict[str, int]
    #: R7.3: the producer join the reader walked (`UE_map_sidecars.MapJoin`), kept so the effects
    #: stage reads the same entities unit, models and sky scope without a second decode.
    join: Any = None

    def brush_stems(self) -> dict[int, str]:
        """`{model index: "brush_<n>"}` -- the stems `.ents`'s `brush_mesh` names, so the runtime
        body finds the same asset the legacy lane authored."""

        return {index: f"brush_{index}" for index in sorted(self.brushes)}

    def detail_models(self) -> list[dict[str, Any]]:
        """The `dprp` dictionary as the staged `details.models[]` rows: one per dictionary entry
        the records actually use, in dictionary order, `{"model", "stem", "modelPath", "count"}`."""

        by_model: dict[int, dict[str, Any]] = {}
        for detail in self.details:
            row = by_model.setdefault(detail.model, {
                "model": detail.model, "stem": detail.stem, "modelPath": detail.model_path,
                "count": 0})
            row["count"] += 1
        return [by_model[model] for model in sorted(by_model)]

    def material_units(self) -> dict[str, str]:
        """Every face group over every scene -> its material unit key (R5.4). Two scenes naming
        one group key name one unit, by `group_key`'s construction; anything else is a defect."""

        merged: dict[str, str] = {}
        for scene in (self.world, self.sky, *self.brushes.values()):
            for key, unit in scene.units.items():
                if merged.setdefault(key, unit) != unit:
                    raise MapGeometryError(
                        f"{self.map_name}: face group {key!r} resolves two material units "
                        f"({merged[key]!r}, {unit!r})")
        return merged


# --------------------------------------------------------------------------------- accessors


class _Primitives:
    """Per-mesh accessor cache: a map's world mesh has hundreds of primitives and thousands of
    faces index into them, so each accessor is decoded once."""

    def __init__(self, units: sidecars.MapUnits) -> None:
        self._units = units
        self._cache: dict[tuple[int, int], tuple[np.ndarray, np.ndarray, np.ndarray]] = {}

    def get(self, mesh: int, primitive: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """`(positions, uvs, indices)` for one primitive, as numpy arrays."""

        key = (mesh, primitive)
        hit = self._cache.get(key)
        if hit is None:
            prim = self._units.document["meshes"][mesh]["primitives"][primitive]
            attributes = prim["attributes"]
            positions = self._units.accessor(attributes["POSITION"])
            uv_index = attributes.get("TEXCOORD_0")
            uvs = (
                self._units.accessor(uv_index)
                if uv_index is not None
                else np.zeros((len(positions), 2), dtype=np.float32)
            )
            indices = self._units.accessor(prim["indices"])[:, 0]
            hit = (positions, uvs, indices)
            self._cache[key] = hit
        return hit

    def displacement(self, mesh: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """`(positions, alpha, indices)` for a displacement mesh's single primitive. A displacement
        primitive carries `_ALPHA` and no `TEXCOORD_0`: the unit states the sculpted geometry, and
        the albedo UV is the face's own planar projection, recomputed here."""

        prim = self._units.document["meshes"][mesh]["primitives"][0]
        positions = self._units.accessor(prim["attributes"]["POSITION"])
        alpha_index = prim["attributes"].get("_ALPHA")
        alpha = (
            self._units.accessor(alpha_index)[:, 0]
            if alpha_index is not None
            else np.zeros(len(positions), dtype=np.float32)
        )
        indices = self._units.accessor(prim["indices"])[:, 0]
        return positions, alpha, indices


# --------------------------------------------------------------------------------- group keys


def group_key(units: sidecars.MapUnits, face: dict[str, Any], map_name: str) -> str | None:
    """The bake's material group key for one face: `<material>` or `<material>@<cubemap>`.

    VBSP patches an `$envmap` face to a per-map copy of its material, so the same authored material
    near two `env_cubemap` samples is two surfaces with two baked cubes. `UE_bsp_to_scene` split the
    OBJ group on exactly that, and the `.mtl` the bake reads is keyed by the split name; reproducing
    it here is what lets a V2 mesh bind the same `MaterialInstanceConstant` the legacy mesh did.
    """

    raw = sidecars._face_material(units, face)
    if raw is None:
        return None
    base = shared_corpus.base_material(raw)
    cube = shared_corpus.cubemap_of(raw, map_name)
    return f"{base}@{cube}" if cube else base


# --------------------------------------------------------------------------------- scenes


def _texture_vectors(units: sidecars.MapUnits, face: dict[str, Any]) -> tuple[Any, int, int]:
    """The face's `texinfo` albedo vectors and the TEXDATA size they were authored against."""

    texinfo = units.root["texinfos"][int(face["texInfo"])]
    texture = units.root["textures"][int(texinfo["texData"])]
    return texinfo["textureVecs"], int(texture["width"]), int(texture["height"])


def _planar_uv(
    point: Sequence[float], vectors: Sequence[Sequence[float]], width: int, height: int
) -> tuple[float, float]:
    """`TEXCOORD_0` for one Source-space point, the same projection the unit publishes for a flat
    face (`formats.map_glb.geometry.texture_coordinates`)."""

    u = point[0] * vectors[0][0] + point[1] * vectors[0][1] + point[2] * vectors[0][2] + vectors[0][3]
    v = point[0] * vectors[1][0] + point[1] * vectors[1][1] + point[2] * vectors[1][2] + vectors[1][3]
    return (u / width if width else u, v / height if height else v)


def _gltf_to_source(point: Sequence[float]) -> tuple[float, float, float]:
    """glTF metres back to Source inches -- `(x, y, z)_gltf = (x, z, -y)_source * 0.0254`."""

    scale = 1.0 / sidecars.GLTF_SCALE
    return (float(point[0]) * scale, -float(point[2]) * scale, float(point[1]) * scale)


def _append_face(
    scene: Scene,
    tris: list[int],
    units: sidecars.MapUnits,
    prims: _Primitives,
    mesh: int,
    face: dict[str, Any],
) -> None:
    """One flat face: its own vertex run out of the primitive, its own triangles, winding reversed."""

    positions, uvs, indices = prims.get(mesh, int(face["primitive"]))
    first_vertex = int(face["firstVertex"])
    count = int(face["vertexCount"])
    local = {}
    for offset in range(count):
        source_index = first_vertex + offset
        local[source_index] = scene.emit(positions[source_index], uvs[source_index])
    first_index = int(face["firstIndex"])
    span = int(face["indexCount"])
    for base in range(first_index, first_index + span, 3):
        a, b, c = (int(indices[base + k]) for k in range(3))
        # Reversed: the glTF frame is right-handed and Unreal's is left-handed, so a triangle
        # authored (a, b, c) there is (a, c, b) here or it draws back-to-front.
        tris.extend((local[a], local[c], local[b]))


def _append_displacement(
    scene: Scene,
    tris: list[int],
    units: sidecars.MapUnits,
    prims: _Primitives,
    face: dict[str, Any],
) -> None:
    """One sculpted displacement grid, from its own mesh in the `displacements` scene."""

    row = units.root["displacements"][int(face["dispInfo"])]
    positions, alpha, indices = prims.displacement(int(row["mesh"]))
    vectors, width, height = _texture_vectors(units, face)
    local = [
        scene.emit(
            positions[index],
            _planar_uv(_gltf_to_source(positions[index]), vectors, width, height),
            float(alpha[index]) / DISP_ALPHA_FULL,
        )
        for index in range(len(positions))
    ]
    for base in range(0, len(indices), 3):
        a, b, c = (int(indices[base + k]) for k in range(3))
        tris.extend((local[a], local[c], local[b]))


def _build_scene(
    units: sidecars.MapUnits,
    prims: _Primitives,
    mesh: int,
    face_indices: Sequence[int],
    map_name: str,
) -> Scene:
    scene = Scene()
    faces = units.root["faces"]
    for face_index in face_indices:
        face = faces[face_index]
        key = group_key(units, face, map_name)
        if key is None:
            continue
        raw = sidecars._face_material(units, face)
        if scene.units.setdefault(key, raw) != raw:
            raise MapGeometryError(
                f"{units.name}: face group {key!r} resolves two material units "
                f"({scene.units[key]!r}, {raw!r})")
        tris = scene.groups.setdefault(key, [])
        if int(face["dispInfo"]) >= 0:
            _append_displacement(scene, tris, units, prims, face)
        elif face.get("primitive") is not None:
            _append_face(scene, tris, units, prims, mesh, face)
    # A group that meshed nothing would otherwise become an empty material slot on the asset.
    scene.groups = {key: tris for key, tris in scene.groups.items() if tris}
    scene.units = {key: unit for key, unit in scene.units.items() if key in scene.groups}
    return scene


# --------------------------------------------------------------------------------- placements


def _placements(units: sidecars.MapUnits, sky: sidecars.SkyScope) -> list[Placement]:
    """Every `staticProps.props[]` record, in lump order, resolved to the R1 corpus stem.

    Detail props (`dprp`, the `placements` scene's other node family) are R7.3's, not this lane's:
    they are 143,412 cards over 41 models and want instancing, not one actor each.
    """

    block = units.root.get("staticProps") or {}
    dictionary = [str(row.get("name") or "") for row in block.get("dictionary") or []]
    nodes = units.document["nodes"]
    out: list[Placement] = []
    for record in block.get("props") or []:
        node = nodes[int(record["node"])]
        model_path = ""
        asset = str(record.get("asset") or "")
        prefix = "vtmb:model:"
        if asset.startswith(prefix):
            model_path = f"models/{asset[len(prefix):]}.mdl"
        if not model_path:
            index = int(record.get("propType", -1))
            if 0 <= index < len(dictionary):
                model_path = dictionary[index]
        if not model_path:
            raise MapGeometryError(
                f"{units.name}: staticProp {record['index']} names no model"
            )
        translation = node.get("translation") or [0.0, 0.0, 0.0]
        rotation = node.get("rotation") or [0.0, 0.0, 0.0, 1.0]
        out.append(
            Placement(
                index=int(record["index"]),
                stem=shared_corpus.static_stem(model_path),
                model_path=model_path,
                position=gltf_position_to_unreal(translation),
                rotation=gltf_quat_to_unreal(rotation),
                solid=int(record.get("solid", 0)),
                flags=int(record.get("flags", 0)),
                skin=int(record.get("skin", 0)),
                fade_min_cm=source_inches_to_unreal(record.get("fadeMinDist", 0.0)),
                fade_max_cm=source_inches_to_unreal(record.get("fadeMaxDist", 0.0)),
                sky=sky.is_sky(sidecars.source_position(translation)),
            )
        )
    return out


def _detail_placements(units: sidecars.MapUnits, sky: sidecars.SkyScope) -> list[DetailPlacement]:
    """Every `detailProps.records[]` record, in lump order, resolved to the R1 corpus stem (R6.3).

    The record's node in the `placements` scene carries the transform (glTF metres, Y-up), read
    through the same frame a static prop's node is. The dictionary entry is the model path; a
    record whose `detailModel` is outside the dictionary is the decoder's own
    `detail-prop-dictionary-range` anomaly and has no node, so it is a loud error here rather than
    a silently skipped instance.
    """

    block = units.root.get("detailProps") or {}
    dictionary = [str(row.get("name") or "") for row in block.get("dictionary") or []]
    nodes = units.document["nodes"]
    out: list[DetailPlacement] = []
    for record in block.get("records") or []:
        model = int(record.get("detailModel", -1))
        node_index = record.get("node")
        if not (0 <= model < len(dictionary)) or node_index is None:
            raise MapGeometryError(
                f"{units.name}: detailProp {record['index']} names no model (detailModel {model})"
            )
        model_path = dictionary[model].replace("\\", "/")
        node = nodes[int(node_index)]
        translation = node.get("translation") or [0.0, 0.0, 0.0]
        rotation = node.get("rotation") or [0.0, 0.0, 0.0, 1.0]
        out.append(
            DetailPlacement(
                index=int(record["index"]),
                model=model,
                stem=shared_corpus.static_stem(model_path),
                model_path=model_path,
                position=gltf_position_to_unreal(translation),
                rotation=gltf_quat_to_unreal(rotation),
                sway=int(record.get("swayAmount", 0)),
                sky=sky.is_sky(sidecars.source_position(translation)),
            )
        )
    return out


def _number(keys: dict[str, str], name: str, default: float) -> float:
    """One keyvalue as C `atof` reads it (`UE_map_sidecars.atof`), the default when absent."""

    return sidecars.atof(keys[name]) if name in keys else default


def _sprite_records(
    blocks: Sequence[Sequence[tuple[str, str]]], sky: sidecars.SkyScope,
) -> list[SpriteRecord]:
    """Every `env_sprite` block of the entity lump, in lump order, as a `SpriteRecord` (R6.1).

    `blocks` is the producer join's own `pair_blocks` (`prepare_join`), so a record's `index` is
    the same ordinal `build_entities` writes to `.ents` and the runtime hands the leaf. Keys fold
    case-insensitively, last spelling wins -- Source's `KeyValue` is called per pair in order.
    """

    out: list[SpriteRecord] = []
    for index, pairs in enumerate(blocks):
        keys = {key.lower(): value for key, value in pairs}
        if keys.get("classname", "").lower() != "env_sprite":
            continue
        tokens = keys.get("origin", "").split()
        origin_src = [sidecars.atof(t) for t in tokens] if len(tokens) == 3 else [0.0, 0.0, 0.0]
        scale = _number(keys, "scale", 1.0)
        scale = min(max(scale, 0.0), SPRITE_MAX_SCALE)
        if scale == 0.0:
            scale = 1.0
        rgb = keys.get("rendercolor", "").split()
        color = tuple(
            max(0, min(255, int(sidecars.atof(rgb[i])))) if i < len(rgb) else 255 for i in range(3))
        alpha = max(0, min(255, int(_number(keys, "renderamt", 255.0))))
        name = keys.get("targetname", "")
        spawnflags = int(_number(keys, "spawnflags", 0.0))
        hidden = keys.get("starthidden", "0") == "1" or (
            bool(name) and not (spawnflags & SF_SPRITE_START_ON))
        out.append(SpriteRecord(
            index=index,
            name=name,
            material=shared_corpus.material_key(keys.get("model", "")),
            position=tuple(float(v) for v in source_to_unreal(*origin_src)),
            scale=scale,
            mode=int(_number(keys, "rendermode", 0.0)),
            color=color,  # type: ignore[arg-type]
            alpha=alpha,
            fx=int(_number(keys, "renderfx", 0.0)),
            hidden=hidden,
            sky=sky.is_sky(origin_src),
        ))
    return out


def _cubemaps(units: sidecars.MapUnits, sky: sidecars.SkyScope) -> list[CubemapSample]:
    """Every `cubemaps[]` sample, in lump order, as a capture placement (R5.5).

    The node holds the position (glTF metres); the row holds the Source-inch origin the compiler
    named the probe by. Both are carried, and the position is the one the bake spawns at.
    """

    nodes = units.document["nodes"]
    out: list[CubemapSample] = []
    for record in units.root.get("cubemaps") or []:
        node = nodes[int(record["node"])]
        translation = node.get("translation") or [0.0, 0.0, 0.0]
        origin = record.get("origin") or [0, 0, 0]
        out.append(
            CubemapSample(
                index=int(record["index"]),
                origin=(int(origin[0]), int(origin[1]), int(origin[2])),
                position=gltf_position_to_unreal(translation),
                sky=sky.is_sky(sidecars.source_position(translation)),
            )
        )
    return out


# --------------------------------------------------------------------------------- entry point


def read_geometry(map_name: str, root: Path | None = None) -> MapGeometry:
    """Read one map's root unit into the structures the V2 bake authors from."""

    join = sidecars.prepare_join(map_name, root)
    units = join.units
    prims = _Primitives(units)
    nodes = units.document["nodes"]
    models = {int(row["index"]): row for row in units.root["models"]}
    world_mesh = int(nodes[int(models[0]["node"])]["mesh"])

    world = _build_scene(units, prims, world_mesh, join.scenes["world"], map_name)
    sky_scene = _build_scene(units, prims, world_mesh, join.scenes["sky"], map_name)
    brushes: dict[int, Scene] = {}
    for model_index, face_indices in sorted(join.scenes["brush"].items()):
        mesh = int(nodes[int(models[model_index]["node"])]["mesh"])
        scene = _build_scene(units, prims, mesh, face_indices, map_name)
        if scene.groups:
            brushes[model_index] = scene

    sky_origin = (0.0, 0.0, 0.0)
    if join.sky.origin_src is not None:
        sky_origin = tuple(
            value * 2.54 * sign
            for value, sign in zip(join.sky.origin_src, (1.0, -1.0, 1.0))
        )

    unit_path = sidecars.unit_paths(map_name, root)["root"]
    return MapGeometry(
        map_name=map_name,
        unit_path=unit_path,
        unit_sha256=_sha256(unit_path),
        world=world,
        sky=sky_scene,
        brushes=brushes,
        placements=_placements(units, join.sky),
        details=_detail_placements(units, join.sky),
        sprites=_sprite_records(join.pair_blocks, join.sky),
        cubemaps=_cubemaps(units, join.sky),
        lights=sidecars.light_rows(units, join.sky),
        sky_scale=float(join.sky.scale),
        sky_origin=sky_origin,
        sky_ok=bool(join.sky.ok),
        join=join,
        counts={
            "worldFaces": len(join.scenes["world"]),
            "skyFaces": len(join.scenes["sky"]),
            "brushModels": len(brushes),
            "worldTriangles": world.tri_count,
            "skyTriangles": sky_scene.tri_count,
            "brushTriangles": sum(scene.tri_count for scene in brushes.values()),
            "cubemaps": len(units.root.get("cubemaps") or []),
            "worldLights": len(units.lighting.get("worldLights") or []),
            "detailProps": len((units.root.get("detailProps") or {}).get("records") or []),
            "sprites": sum(
                1 for pairs in join.pair_blocks
                if dict((k.lower(), v) for k, v in pairs).get("classname", "").lower()
                == "env_sprite"),
            "effects": sum(
                1 for row in units.entities["entities"]
                if str(row.get("classname") or "").lower() in (
                    "env_particle", "func_particle", "func_dustmotes", "env_steam", "env_beam")),
        },
    )


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


# --------------------------------------------------------------------------------- materials


def material_staging_root(work_root: Path | None = None) -> Path:
    """The material lane's own staging tree (`importers.materials.staging_root`), whose per-unit
    `<key>.provenance.json` sidecars say what each `vtmb:material:*` id became."""

    from elysium_pipeline.importers import materials as material_lane

    root = Path(work_root) if work_root is not None else paths.work_root()
    return material_lane.staging_root(root)


def sidecar_reader(staging: Path):
    """`unit key -> provenance dict | None` over one material staging tree, cached per key."""

    from elysium_pipeline.importers import materials as material_lane

    cache: dict[str, dict[str, Any] | None] = {}

    def read(unit_key: str) -> dict[str, Any] | None:
        if unit_key in cache:
            return cache[unit_key]
        path = staging / (unit_key + material_lane.PROVENANCE_SUFFIX)
        document = None
        if path.is_file():
            with open(path, "r", encoding="utf-8") as handle:
                document = json.load(handle)
        cache[unit_key] = document
        return document

    return read


def texture_sidecar_reader(staging: Path):
    """`texture key -> provenance dict | None` over the texture lane's staging tree
    (`importers.textures.staging_root`), cached per key -- the sprite rows read a texture's
    `width`/`height`/`assetPath` here (R6.1)."""

    from elysium_pipeline.importers import textures as texture_lane

    cache: dict[str, dict[str, Any] | None] = {}

    def read(texture_key: str) -> dict[str, Any] | None:
        if texture_key in cache:
            return cache[texture_key]
        path = staging / (texture_key + texture_lane.PROVENANCE_SUFFIX)
        document = None
        if path.is_file():
            with open(path, "r", encoding="utf-8") as handle:
                document = json.load(handle)
        cache[texture_key] = document
        return document

    return read


def sprite_row(record: SpriteRecord, material: dict[str, Any], texture: dict[str, Any],
               asset: str) -> dict[str, Any]:
    """One staged `sprites[]` row (R6.1): the record joined to its material sidecar (the
    `$spriteorientation` parameter row) and its base texture's sidecar (`width`/`height`), with
    the blend the mode selects. Pure; `resolve_sprite_table` does the lookups and the failures."""

    if record.mode not in SPRITE_BLEND_BY_MODE:
        raise MapGeometryError(
            f"env_sprite {record.index} ({record.material}) has rendermode {record.mode}, which "
            f"names no sprite program")
    upright = False
    for row in material.get("parameters") or []:
        if str(row.get("key") or "").lower() == "$spriteorientation":
            upright = str(row.get("value") or "").strip().lower() == "parallel_upright"
    return {
        "index": record.index,
        "name": record.name,
        "material": record.material,
        "asset": asset,
        "texture": str(texture.get("assetPath") or ""),
        "width": int(texture.get("width") or 0),
        "height": int(texture.get("height") or 0),
        "position": list(record.position),
        "scale": record.scale,
        "mode": record.mode,
        "blend": SPRITE_BLEND_BY_MODE[record.mode],
        "glow": record.mode in SPRITE_GLOW_MODES,
        "color": list(record.color),
        "alpha": record.alpha,
        "fx": record.fx,
        "upright": upright,
        "hidden": record.hidden,
        "sky": record.sky,
    }


def resolve_sprite_table(
    records: Sequence[SpriteRecord], read_sidecar, read_texture, *, map_name: str = "",
) -> list[dict[str, Any]]:
    """Every `SpriteRecord` as its staged row (R6.1), through the material lane's provenance
    sidecar (the imported `MI_`'s path by `asset_path_for`, the VMT's `$spriteorientation`) and
    the texture lane's sidecar for the `BaseTexture` binding (its size). A sprite whose material
    or texture the lanes have not staged fails the whole map with every missing key named -- the
    owner's ruling draws all of them, so a silently empty bulb is exactly the defect."""

    from elysium_pipeline.importers import materials as material_lane

    rows: list[dict[str, Any]] = []
    missing_materials: list[str] = []
    missing_textures: list[str] = []
    for record in records:
        document = read_sidecar(record.material)
        if document is None:
            missing_materials.append(record.material)
            continue
        texture_key = ""
        for binding in document.get("textureBindings") or []:
            if binding.get("parameter") == "BaseTexture":
                texture_key = str(binding.get("asset") or "")
        prefix = "vtmb:texture:"
        texture = read_texture(texture_key[len(prefix):]) if texture_key.startswith(prefix) else None
        if not texture or not int(texture.get("width") or 0) or not int(texture.get("height") or 0):
            missing_textures.append(f"{record.material} -> {texture_key or 'no BaseTexture'}")
            continue
        rows.append(sprite_row(
            record, document, texture, material_lane.asset_path_for(record.material)))
    if missing_materials or missing_textures:
        parts = []
        if missing_materials:
            parts.append(f"{len(missing_materials)} sprite material(s) not staged by the material "
                         f"lane (run: uv run elysium import materials): "
                         + ", ".join(sorted(set(missing_materials))[:8]))
        if missing_textures:
            parts.append(f"{len(missing_textures)} sprite texture(s) not staged by the texture "
                         f"lane (run: uv run elysium import textures): "
                         + ", ".join(sorted(set(missing_textures))[:8]))
        raise MapGeometryError(f"{map_name or 'map'}: " + "; ".join(parts))
    return rows


def resolve_material_table(
    units_by_group: dict[str, str], read_sidecar, *, map_name: str = "",
) -> dict[str, MaterialBinding]:
    """Every face group's `MI_`, master and blend mode, through the material lane's staged
    provenance sidecars (R5.4).

    `read_sidecar(unit key)` returns the unit's provenance document or None. The asset path is
    `importers.materials.asset_path_for`, the pure function that named the asset at import; the
    master is the sidecar's own `master` (the import lane already walks a patched unit's `patch`
    chain to its root master); the blend mode is the root unit's, reached through `patchBase`. A
    unit with no sidecar -- or a patch chain that leaves the staging tree -- fails the whole map
    with every missing key named: binding nothing would draw the master's placeholder with no line
    in the log to say why.
    """

    from elysium_pipeline.importers import materials as material_lane

    table: dict[str, MaterialBinding] = {}
    missing: list[str] = []
    broken: list[str] = []
    for key in sorted(units_by_group):
        unit_key = units_by_group[key]
        document = read_sidecar(unit_key)
        if document is None:
            missing.append(unit_key)
            continue
        root_key = unit_key
        root = document
        hops = 0
        while root is not None and root.get("patched"):
            base = str(root.get("patchBase") or "")
            if not base.startswith("vtmb:material:") or hops > 8:
                root = None
                break
            root_key = base[len("vtmb:material:"):]
            root = read_sidecar(root_key)
            hops += 1
        master = str((root or {}).get("master") or document.get("master") or "").rsplit("/", 1)[-1]
        if root is None or not master:
            broken.append(unit_key)
            continue
        table[key] = MaterialBinding(
            key=key, unit=f"vtmb:material:{unit_key}",
            asset=material_lane.asset_path_for(unit_key), master=master,
            blend_mode=str(root.get("blendMode") or "Opaque"),
            patched=bool(document.get("patched")), provenance=root_key,
        )
    if missing or broken:
        parts = []
        if missing:
            parts.append(f"{len(missing)} material unit(s) not staged by the material lane "
                         f"(run: uv run elysium import materials): "
                         + ", ".join(missing[:8]))
        if broken:
            parts.append(f"{len(broken)} patched unit(s) whose base chain leaves the staging "
                         f"tree or names no master: " + ", ".join(broken[:8]))
        raise MapGeometryError(f"{map_name or 'map'}: " + "; ".join(parts))
    return table


def legacy_master_for(record: dict[str, Any] | None) -> str | None:
    """The legacy world master a `shared/materials.json` record selected (`Bake._master_for`)."""

    if record is None:
        return None
    for flag, master in LEGACY_MASTER_RULES:
        if record.get(flag):
            return master
    return LEGACY_MASTER_DEFAULT


def appearance_class(master: str, blend_mode: str) -> str:
    """The class a V2 (master, blend) pair renders as -- the blend mode, except where the master
    itself is the distinction (`M_V2_Refract`, `M_V2_Water`, `M_V2_Decal`)."""

    if master == "M_V2_Refract":
        return "refract"
    if master == "M_V2_Water":
        return "water"
    if master == "M_V2_Decal":
        return "decal"
    return str(blend_mode or "Opaque").lower()


def classify_material(
    binding: MaterialBinding, provenance: dict[str, Any] | None,
    legacy_record: dict[str, Any] | None,
) -> dict[str, Any]:
    """One report row: what the surface binds now, what it bound before, and whether the rebind
    changed its appearance CLASS -- the blend/master class, or a proxy the legacy `.mtl` lane
    flattened to a static bind and the V2 instance runs live. Data only; no look judgement.

    `provenance` is the ROOT unit's sidecar (the instance whose proxies the animation lanes were
    written from), `legacy_record` the corpus row the legacy `.mtl` resolved for the same base
    material.
    """

    provenance = provenance or {}
    proxies = [str(row.get("kind") or "") for row in provenance.get("proxies") or ()]
    omissions = {str(row.get("reason") or "") for row in provenance.get("omissions") or ()}
    frames_unavailable = FRAMES_UNAVAILABLE_OMISSION in omissions
    live = sorted({
        kind for kind in proxies
        if kind in ANIMATED_PROXY_KINDS and not (kind == "animatedtexture" and frames_unavailable)
    })
    animated_now = bool(live)
    legacy_master = legacy_master_for(legacy_record)
    legacy_class = LEGACY_MASTER_CLASS.get(legacy_master or "", None)
    v2_class = appearance_class(binding.master, binding.blend_mode)
    return {
        "key": binding.key,
        "unit": binding.unit,
        "asset": binding.asset,
        "patched": binding.patched,
        "v2Master": binding.master,
        "v2BlendMode": binding.blend_mode,
        "v2Class": v2_class,
        "legacyMaster": legacy_master,
        "legacyClass": legacy_class,
        "legacyRecordFound": legacy_record is not None,
        "classChanged": legacy_class is not None and legacy_class != v2_class,
        "proxies": proxies,
        "liveProxies": live,
        "animatedNow": animated_now,
        "animatedFramesUnavailable": frames_unavailable and "animatedtexture" in proxies,
        "surfaceClass": provenance.get("surfaceClass"),
        "wetnessScale": provenance.get("wetnessScale"),
        "wetnessDriven": provenance.get("wetnessScale") is not None
        and binding.master in ("M_V2_Lit", "M_V2_LitTranslucent"),
        "isDecalSurface": bool(provenance.get("isDecalSurface")),
    }


def material_report(
    map_name: str, table: dict[str, MaterialBinding], read_sidecar, *,
    legacy_materials: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """The R5.4 provenance report for one map: every bound material classified
    (`classify_material`) plus the summary counts the bake prints. Written beside the staged pair
    as `materials_report.json` by `stage_map`."""

    if legacy_materials is None:
        legacy_materials = _legacy_material_records(map_name)

    rows = []
    for key in sorted(table):
        binding = table[key]
        base_key = shared_corpus.base_material(binding.unit[len("vtmb:material:"):])
        rows.append(classify_material(
            binding, read_sidecar(binding.provenance), legacy_materials.get(base_key)))

    animated = [row["key"] for row in rows if row["animatedNow"]]
    changed = [row for row in rows if row["classChanged"]]
    return {
        "schema": "elysium.map-materials-report",
        "version": 1,
        "map": map_name,
        "counts": {
            "materials": len(rows),
            "patched": sum(1 for row in rows if row["patched"]),
            "animatedNow": len(animated),
            "classChanged": len(changed),
            "wetnessDriven": sum(1 for row in rows if row["wetnessDriven"]),
            "decalSurfaces": sum(1 for row in rows if row["isDecalSurface"]),
            "byV2Master": _count_by(rows, "v2Master"),
            "byV2Class": _count_by(rows, "v2Class"),
            "byLegacyClass": _count_by(rows, "legacyClass"),
            "legacyRecordMissing": sum(1 for row in rows if not row["legacyRecordFound"]),
        },
        "animatedNow": [
            {"key": row["key"], "liveProxies": row["liveProxies"], "v2Master": row["v2Master"]}
            for row in rows if row["animatedNow"]
        ],
        "classChanged": [
            {"key": row["key"], "legacyMaster": row["legacyMaster"], "legacyClass": row["legacyClass"],
             "v2Master": row["v2Master"], "v2Class": row["v2Class"]}
            for row in changed
        ],
        "materials": rows,
    }


def _count_by(rows: list[dict[str, Any]], field_name: str) -> dict[str, int]:
    counts: dict[str, int] = {}
    for row in rows:
        counts[str(row.get(field_name))] = counts.get(str(row.get(field_name)), 0) + 1
    return dict(sorted(counts.items()))


def _legacy_material_records(map_name: str) -> dict[str, Any]:
    """The corpus `shared/materials.json` rows plus the map's own PAKFILE-only rows, keyed as the
    legacy `.mtl`'s `mat` lines key them. Empty when the legacy export is not on this machine --
    the report then states `legacyRecordFound: false` rather than failing the stage."""

    records: dict[str, Any] = {}
    try:
        export = paths.export_root()
    except RuntimeError:
        return records
    corpus = shared_corpus.materials_path(export)
    if corpus.is_file():
        with open(corpus, "r", encoding="utf-8") as handle:
            records.update(json.load(handle).get("materials") or {})
    local = export / map_name / f"{map_name}.materials.json"
    if local.is_file():
        with open(local, "r", encoding="utf-8") as handle:
            records.update(json.load(handle).get("materials") or {})
    return records


# --------------------------------------------------------------------------------- staging


#: The staged `details.records[]` row layout (R6.3): compact lists rather than one dict per
#: record, because a map carries thousands (6,031 on `sp_tutorial_1`) and the editor half reads
#: them positionally (`bake_map_v2._DetailPlacement`). `sway` is the raw `swayAmount` byte.
DETAIL_RECORD_FIELDS = ("index", "model", "px", "py", "pz", "qx", "qy", "qz", "qw", "sway", "sky")


def detail_record_row(detail: DetailPlacement) -> list[Any]:
    """One `DetailPlacement` as its staged `details.records[]` row (`DETAIL_RECORD_FIELDS`)."""

    return [detail.index, detail.model, *detail.position, *detail.rotation, detail.sway,
            int(detail.sky)]


def staging_root(work_root: Path | None = None) -> Path:
    """`$ELYSIUM_WORK_ROOT/import/map_geometry` -- beside the model lane's own staging tree."""

    return (Path(work_root) if work_root is not None else paths.work_root()) / "import" / FAMILY


def staging_dir(map_name: str, work_root: Path | None = None) -> Path:
    return staging_root(work_root) / map_name


def manifest_path(map_name: str, work_root: Path | None = None) -> Path:
    return staging_dir(map_name, work_root) / MANIFEST_NAME


def _scene_block(scene: Scene, buffer: bytearray) -> dict[str, Any]:
    """Append one scene's four arrays to `buffer` and describe them as byte spans.

    Packed rather than JSON because these are the bulk of the payload -- 41,901 vertices on
    `sm_hub_1` -- and because `array.array` reads a span back on the editor side in one call, with
    no parse. Every value is little-endian float32 except the indices, which are uint32.
    """

    def append(values, typecode: str) -> list[int]:
        block = array(typecode, values)
        if sys.byteorder != "little":
            block.byteswap()   # the staged pair is little-endian, whatever host wrote it
        offset = len(buffer)
        buffer.extend(block.tobytes())
        return [offset, len(block)]

    positions = append((value for point in scene.positions for value in point), "f")
    uvs = append((value for uv in scene.uvs for value in uv), "f")
    blend = append(scene.blend, "f")
    groups: dict[str, list[int]] = {}
    flat: list[int] = []
    for key in sorted(scene.groups):
        indices = scene.groups[key]
        groups[key] = [len(flat), len(indices)]
        flat.extend(indices)
    indices_span = append(flat, "I")
    return {
        "vertexCount": len(scene.positions),
        "positions": positions,
        "uvs": uvs,
        "blend": blend,
        "indices": indices_span,
        "groups": groups,
        "triangleCount": scene.tri_count,
    }


def stage_map(map_name: str, root: Path | None = None,
              work_root: Path | None = None) -> dict[str, Any]:
    """Read one map's root unit and write the staged pair the editor bake reads.

    Returns the manifest. Overwrites whatever was staged before: the manifest describes exactly the
    bytes beside it, so a half-written pair from a crashed run can never be read as current.

    R5.4: the manifest also carries `materials` -- every face group resolved to its imported `MI_`
    through the material lane's staged manifest -- and the material report lands beside it.
    R5.5: and `cubemaps` -- every lump-42 sample as a reflection-capture placement.
    R5.6: and `lights` -- every lump-15 record as the `.lights` producer's own row, the light
    placements the editor half derives final actor values from.
    R6.3: and `details` -- the `dprp` dictionary as `models[]` and every record as one compact
    `records[]` row, `[index, model, px, py, pz, qx, qy, qz, qw, swayAmount, sky]`, the instanced
    placements (`DETAIL_RECORD_FIELDS`).
    R6.1: and `sprites` -- every `env_sprite` block as one row, the billboard placements
    (`resolve_sprite_table`).
    R7.3: and `effects` / `particleTrees` / `dustmotes` / `steam` / `beams` / `effectStats` --
    every effects entity joined to its particle closure (kept as a tree), its brush bounds and the
    texture lane's sprite assets (`importers.effects.stage_effects_for_join`).
    """

    from elysium_pipeline.importers import effects as effects_lane
    from elysium_pipeline.importers import textures as texture_lane

    geometry = read_geometry(map_name, root)
    staging = material_staging_root(work_root)
    if not staging.is_dir():
        raise MapGeometryError(
            f"no material staging tree at {staging} (run: uv run elysium import materials)")
    read_sidecar = sidecar_reader(staging)
    materials = resolve_material_table(geometry.material_units(), read_sidecar, map_name=map_name)
    texture_staging = texture_lane.staging_root(
        Path(work_root) if work_root is not None else paths.work_root())
    sprites = resolve_sprite_table(
        geometry.sprites, read_sidecar, texture_sidecar_reader(texture_staging),
        map_name=map_name)
    report = material_report(map_name, materials, read_sidecar)
    effects = effects_lane.stage_effects_for_join(
        geometry.join, read_texture=texture_sidecar_reader(texture_staging),
        read_material=read_sidecar, export_v2_root=root, map_name=map_name)
    buffer = bytearray()
    scenes = {
        "world": _scene_block(geometry.world, buffer),
        "sky": _scene_block(geometry.sky, buffer),
    }
    stems = geometry.brush_stems()
    for index, scene in sorted(geometry.brushes.items()):
        scenes[stems[index]] = _scene_block(scene, buffer)

    manifest = {
        "schema": MANIFEST_SCHEMA,
        "version": MANIFEST_VERSION,
        "map": geometry.map_name,
        "unit": str(geometry.unit_path),
        "unitSha256": geometry.unit_sha256,
        "vertexFile": VERTEX_NAME,
        "vertexBytes": len(buffer),
        "sky": {
            "ok": geometry.sky_ok,
            "scale": geometry.sky_scale,
            "origin": list(geometry.sky_origin),
        },
        "scenes": scenes,
        "brushStems": {str(index): stem for index, stem in stems.items()},
        "materials": {key: binding.as_row() for key, binding in sorted(materials.items())},
        "materialReport": dict(report["counts"]),
        "placements": [
            {
                "index": placement.index,
                "stem": placement.stem,
                "modelPath": placement.model_path,
                "position": list(placement.position),
                "rotation": list(placement.rotation),
                "solid": placement.solid,
                "flags": placement.flags,
                "skin": placement.skin,
                "fadeMinCm": placement.fade_min_cm,
                "fadeMaxCm": placement.fade_max_cm,
                "sky": placement.sky,
            }
            for placement in geometry.placements
        ],
        "cubemaps": [sample.as_row() for sample in geometry.cubemaps],
        "lights": list(geometry.lights),
        "details": {
            "fields": list(DETAIL_RECORD_FIELDS),
            "models": geometry.detail_models(),
            "records": [detail_record_row(detail) for detail in geometry.details],
        },
        "sprites": sprites,
        "effects": effects["effects"],
        "particleTrees": effects["particleTrees"],
        "dustmotes": effects["dustmotes"],
        "steam": effects["steam"],
        "beams": effects["beams"],
        "effectStats": effects["effectStats"],
        "counts": dict(geometry.counts),
    }

    out_dir = staging_dir(map_name, work_root)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / VERTEX_NAME).write_bytes(bytes(buffer))
    (out_dir / MANIFEST_NAME).write_text(
        json.dumps(manifest, indent=1, sort_keys=False), encoding="utf-8")
    (out_dir / MATERIAL_REPORT_NAME).write_text(
        json.dumps(report, indent=1, sort_keys=False), encoding="utf-8")
    return manifest


def stage_maps(map_names: Sequence[str], root: Path | None = None,
               work_root: Path | None = None) -> dict[str, dict[str, Any]]:
    return {name: stage_map(name, root, work_root) for name in map_names}
