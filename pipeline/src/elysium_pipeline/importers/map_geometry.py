"""The map root unit's geometry and placements, read for the V2 map bake (R5.1, R7.4 water).

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
import math
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
#: 8 (R7.2): every `materials` row carries `decalAsset` (the `MI_<unit>_Decal` projector instance
#: the material lane staged beside the surface one, or `null`) and `isDecalSurface` -- the pair the
#: bake binds a `$decal` face group's mesh slot from, and keeps out of the Nanite buckets
#: (`docs/project/seam_migration.md` -> "R7.2 Decals", rulings 2 and 3).
#: 9 (R7.1): the manifest carries `water` -- one `volumes[]` row per real `LEAFWATERDATA` record
#: with its fog keys and its `CONTENTS_WATER` brushes as plane sets, plus the `dropped[]` rows that
#: name what produced none (`docs/architecture/water-architecture.md` -> section 5.1).
#: 10 (R7.4): the water lane is complete. Face groups split on two per-face facts vbsp published and
#: the bake never read -- `#underside` (the plane normal faces down, so the group binds the material
#: lane's `_Underside` twin) and `#style<n>` (the face carries a lightstyle, so the chunk gets the
#: runtime's style brightness) -- every `materials` row states which it is; each `water.volumes[]`
#: row carries the compiler's `fluid{}`, its convex `pieces[]`, its `leafBoxesCm[]` and the
#: `nearBoxesCm[]` the PVS derives; and `water.faces[]` is one row per water face with the fields
#: G11/G13/G22/G26 name (`docs/architecture/water-architecture.md`, AUDIT section 9).
#: 11 (R7.4, integrator): each `water.faces[]` row also carries `meshedAreaCm2`, the area this
#: stage actually meshed for the face. `faces[].area` beside it is what vbsp computed, so the
#: G26/verdict B3 area pin is answerable offline, per face and per section, without an editor
#: geometry query on a baked asset.
MANIFEST_VERSION = 11
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
#: R7.2 (ruling 2): the `decal` row is gone with the master it named -- `M_Decal` is retired and
#: the legacy bake's own decal branch with it, so a `$decal` world face falls through to the master
#: its blend selects on either lane. The V2 lane then rebinds it onto the projector instance
#: (`v2Class == "decal"`), which is the class change this report exists to state.
LEGACY_MASTER_RULES = (
    ("additive", "M_Additive"), ("refract", "M_Refract"),
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
#:
#: Read off the Sprite shader's own per-mode blend state (`stdshader_dx8.dll` `1000eca0`), which
#: is the authority here rather than the mode's Source *name*: **3** (`kRenderGlow`) and **9**
#: (`kRenderWorldGlow`) set `SRC_ALPHA, ONE` and disable the depth test -- a glow is additive, not
#: translucent, and the pre-fix rows drew every corona as a grey card over the light instead of
#: adding to it; **5** and **7** (`kRenderTransAdd`) are the same `SRC_ALPHA, ONE`; **8**
#: (`kRenderTransAlphaAdd`) is `ONE, INV_SRC_ALPHA`, premultiplied -- Unreal's `AlphaComposite`,
#: not `Additive`; **1, 2, 4** are the ordinary `SRC_ALPHA, INV_SRC_ALPHA`.
#: No premultiply term is wired for the additive rows: Unreal's additive base pass already
#: multiplies the colour by Opacity (`BasePassPixelShader.usf:2326`), which is `SRC_ALPHA, ONE`.
SPRITE_BLEND_BY_MODE = {
    0: "Opaque", 1: "Translucent", 2: "Translucent", 3: "Additive", 4: "Translucent",
    5: "Additive", 7: "Additive", 8: "AlphaComposite", 9: "Additive",
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

#: `CONTENTS_SOLID` (`bspflags.h`). vbsp writes one dummy solid leaf per map at index 0 -- cluster
#: 0, a zero-extent box -- and it rides into any cluster-keyed set that names cluster 0. A leaf no
#: eye can be in is not near water, and VtMB's own annotation agrees: 0 of the 97 `sm_hub_1` and 126
#: `sp_soc_3` leaves that carry the near-water bit are solid.
CONTENTS_SOLID = 0x1
#: `CONTENTS_WATER` (`bspflags.h`). A brush is a water volume's when it carries the bit AND at
#: least one non-bevel side whose material authors `%compilewater`: `0x18000120` shadow casters
#: sided entirely with `tools/tools_shadow` carry the bit too and are not water, while
#: `0x18000020` `func_detail` water is (`water-architecture.md` section 5.1).
CONTENTS_WATER = 0x20
#: The `%compilewater` key vbsp reads to give a brush that content bit; carried on the material
#: unit's own VMT provenance, so a patched instance only shows it through its `patchBase`.
COMPILE_WATER_KEY = "%compilewater"
#: R7.5 look pass: a `%compilenodraw` water brush is a volume with no drawn surface (VtMB's own
#: behaviour; `water/invisible_water`, the pier's swimmable ocean under the `blackwater` card).
COMPILE_NODRAW_KEY = "%compilenodraw"
#: How far a brush's horizontal top plane may sit from a `LEAFWATERDATA` row's `surfaceZ` and still
#: be that row's brush: one Source inch, the grid vbsp snapped both to.
WATER_SURFACE_TOLERANCE_CM = 2.54
#: How flat a plane's Unreal normal must be to be read as a brush's water surface. The corpus's
#: water tops are axis-aligned; the bound keeps a steep bank side from ever being mistaken for one.
WATER_TOP_NORMAL_Z = 0.99

#: R7.4 ruling E (revised 2026-09-04, per-face underside; `water-architecture.md` section 1 -- N is
#: the four standing divergences). `Mod_LoadFaces` (engine.dll `FUN_200b73d0`) tests the face's
#: own plane normal against the .rdata constant `_DAT_201734e8 = 0.0` and, when it is negative,
#: undefines `$reflecttexture` on the material -- so an underside water face is not "the same
#: material flipped", it is the same material with the reflection pass gone. The test is on the
#: PLANE row the face names, not on the side-flipped normal: measured over the three water maps,
#: the raw normal reproduces the compiler's own split exactly (`sm_hub_1` 23 up / 24 down,
#: `sm_pier_1` 18 down + 23 vertical + 9 up on the `_depth_33` patch, `sp_soc_3` 27 up + 4 vertical
#: / 27 down + 4 vertical), while the side-flipped one calls every one of them up.
UNDERSIDE_NORMAL_Z = 0.0
#: The suffix a down-facing face group's key carries. The bake binds `<instance>_Underside`, the
#: twin the material lane stages beside the surface instance (the R7.2 decal-twin shape).
UNDERSIDE_SUFFIX = "#underside"
#: The suffix a lightstyle-bearing face group's key carries, `<key>#style<n>`.
LIGHTSTYLE_SUFFIX = "#style"

#: `dface_t.styles[0..3]`: `0` is "the base style, always on", `255` is the empty slot. Everything
#: else names a `CWorld::vfunc104` pattern (`vampire.dll 0x1023c020`) the light rig already clocks.
#: Read in slot order, which is VtMB's own: the pier's 34 `objects/surf` foam cards carry style 1
#: first and 21 of them carry the switchable style 32 second.
LIGHTSTYLE_BASE = 0
LIGHTSTYLE_EMPTY = 255
LIGHTSTYLE_SLOTS = 4

#: `dprimitive_t.type` (lump 37): `PRIM_TRILIST` / `PRIM_TRISTRIP`, the two modes
#: `Shader_DrawSurfaceDynamic` (`engine.dll FUN_2007d4e0`) begins its dynamic mesh with
#: (`MATERIAL_TRIANGLES` / `MATERIAL_TRIANGLE_STRIP`). Any other value is a record the engine
#: refuses to draw (`else return`), and so is this lane.
PRIM_TRILIST = 0
PRIM_TRISTRIP = 1


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
    #: construction -- `group_key` is a function of the raw key alone, and R7.4's two suffixes are
    #: functions of the face's own plane and styles.
    units: dict[str, str] = field(default_factory=dict)
    #: R7.4: one row per `%compilewater` face this scene meshed (`_water_face_row`). Collected only
    #: when the caller supplied the `%compilewater` predicate -- the key lives on the material
    #: lane's staged provenance and nothing else in this module can answer it.
    water_faces: list[dict[str, Any]] = field(default_factory=list)

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
    placement of the same model draw the same `/ElysiumBaked/Models/<dir>/SM_<base>`. `sway` is the
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
class WaterBrush:
    """One convex of a volume, as the plane set the runtime tests a point against (R7.1).

    Both carriers use this row: the volume's own `CONTENTS_WATER` brushes, and R7.4's `pieces` --
    the compiler's convex decomposition of the same solid (G18). `planes` are Unreal centimetres,
    outward normals, `n . p - d <= 0` inside; `bounds_min`/`bounds_max` are the AABB, the cheap
    test the runtime takes first, and every row has one. For a brush it is the AABB of the hull the
    stage solved; for a piece it is the AABB of the ledge's own vertices, which bounds that convex
    exactly -- vbsp publishes no bounds for a ledge, so the stage measures them."""

    planes: tuple[tuple[float, float, float, float], ...]
    bounds_min: tuple[float, float, float]
    bounds_max: tuple[float, float, float]

    def as_row(self) -> dict[str, Any]:
        return {
            "planes": [list(plane) for plane in self.planes],
            "boundsCm": {"min": list(self.bounds_min), "max": list(self.bounds_max)},
        }


@dataclass(frozen=True)
class WaterFluid:
    """The compiler's `fluid { }` block, the half of a water volume the physics reads (R7.4, G7).

    Decoded into the unit since R2 (`formats.map_glb.physics`) and read by nothing until now.
    `index` names `physics.models[0].solids[index]` -- the convex decomposition of the fluid, which
    is what `pieces` is built from -- and it is also the creation guard: `vampire.dll FUN_10158600`
    tests `fluid.index > 0` on the first dword of `ParseFluid`'s output and nothing else, so both
    owner maps get a controller (verdict B5, closing AUDIT section 12 unknown 7, which had guessed
    the guard was on `contents`).

    `surface_plane` is the authored plane in the bake's frame -- `n . p - d = 0`, centimetres --
    and lands on the volume's own `surface_z_cm` on all three maps. `current_velocity_cm` is
    centimetres per second; no map in the corpus authors a non-zero one and no brush or leaf carries
    a `CONTENTS_CURRENT_*` bit (G21), so the lane exists for the map that would.

    A key the author left out is `None`, never a substituted default: vphysics' own default density
    is 1000 kg/m3 and `sm_pier_1` authors exactly that, but the hub and `sp_soc_3` author none and
    saying `1000` here would state an authoring that did not happen. `contents` is parsed by the
    game DLL and read by nothing in it (verdict B5).
    """

    index: int
    density: float | None
    damping: float | None
    surface_plane: tuple[float, float, float, float]
    current_velocity_cm: tuple[float, float, float]
    contents: int | None
    #: The hard-coded literal the controller binds (`vampire.dll 10158743`), when the block names
    #: one -- provenance for the surfaceprop the impact and step sounds come off.
    surface_prop: str | None

    def as_row(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "density": self.density,
            "damping": self.damping,
            "surfacePlane": list(self.surface_plane),
            "currentVelocityCm": list(self.current_velocity_cm),
            "contents": self.contents,
            "surfaceProp": self.surface_prop,
        }


@dataclass(frozen=True)
class WaterVolume:
    """One real `LEAFWATERDATA` record joined to its material and its brushes (R7.1,
    `docs/architecture/water-architecture.md` section 5.1).

    `index` is the lump ordinal, `surface_z_cm`/`min_z_cm` the record's own two floats in Unreal
    centimetres. `fog_color` is carried **as authored** -- `{r g b}` already divided by 255, `[r g
    b]` verbatim -- undecoded, the same convention the `.env` sidecar and every staged `FogColor`
    vector use; the gamma decode is the shader's. `fog_start_cm`/`fog_end_cm` are the authored
    Source inches in centimetres, because the actor stores final values.
    """

    index: int
    surface_z_cm: float
    min_z_cm: float
    material: str
    fog_enable: bool
    fog_color: tuple[float, float, float]
    fog_start_cm: float
    fog_end_cm: float
    brushes: tuple[WaterBrush, ...]
    #: R7.4 (G7): the `fluid { }` block whose surface plane stands at this volume's own surface, or
    #: `None` where the compiler authored no fluid for it.
    fluid: WaterFluid | None = None
    #: R7.4 (G18): the compiler's own convex decomposition of the fluid solid, one plane set per
    #: ledge, in the same `n . p - d <= 0` convention as `WaterBrush.planes`. The runtime tests the
    #: carve rather than the box: the hub's water is 5 pieces and its single brush AABB spills
    #: 66-77 inches into the sewer walls.
    pieces: tuple[WaterBrush, ...] = ()
    #: R7.4 (G10): the AABBs of the leaves whose `leafWaterDataID` names this record -- the engine's
    #: own "the eye is under water" answer, and a tighter hull than the brush.
    leaf_boxes_cm: tuple[tuple[tuple[float, float, float], tuple[float, float, float]], ...] = ()
    #: R7.4 (G9/G23): the AABBs of every leaf in `union(PVS(water cluster))` -- VtMB's `0x200`
    #: annotation, derived from the visibility sub-unit instead of read off the leaf because the
    #: Unofficial-Patch recompile of `sm_pier_1` dropped the bit (retail 702 leaves, UP 0).
    near_boxes_cm: tuple[tuple[tuple[float, float, float], tuple[float, float, float]], ...] = ()
    #: R7.4 (G17): the physics `materialtable` row named `water`, the surfaceprop index a body
    #: moving in this volume reports. `None` on a map whose table has no water row -- `sm_hub_1`'s
    #: 16 rows do not, `sm_pier_1`'s 18 do (`WATER = 17`).
    material_table_water_index: int | None = None

    def as_row(self) -> dict[str, Any]:
        return {
            "index": self.index,
            "surfaceZCm": self.surface_z_cm,
            "minZCm": self.min_z_cm,
            "material": self.material,
            "fogEnable": self.fog_enable,
            "fogColor": list(self.fog_color),
            "fogStartCm": self.fog_start_cm,
            "fogEndCm": self.fog_end_cm,
            "brushes": [brush.as_row() for brush in self.brushes],
            "fluid": self.fluid.as_row() if self.fluid is not None else None,
            # Same row shape as `brushes[]`, deliberately: a piece is a convex the runtime tests a
            # point against exactly as it tests a brush, bounds-first, so both sides carry one
            # writer and one struct instead of two that drift.
            "pieces": [piece.as_row() for piece in self.pieces],
            "leafBoxesCm": [{"min": list(low), "max": list(high)}
                            for low, high in self.leaf_boxes_cm],
            "nearBoxesCm": [{"min": list(low), "max": list(high)}
                            for low, high in self.near_boxes_cm],
            "materialTableWaterIndex": self.material_table_water_index,
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
    #: R7.2 ruling 2: the `MI_<unit>_Decal` projector instance the material lane staged beside
    #: `asset`, or `None` when this unit never draws as a decal. Read off the ROOT unit's sidecar,
    #: like `master`/`blend_mode`: the material stage stages a twin for the root unit only, so a
    #: patched `$decal` unit (10 in the corpus -- `glass/libwndwf`, `objects/blastdoortrim`, on
    #: `hw_warrens_5` and `la_library_1`) binds its root's twin. That loses nothing the patch
    #: carried: a patch delta cannot reach the projector master (VBSP patches `$envmap`, which
    #: `M_V2_Decal` has no pin for, and the only non-empty patched delta in the whole manifest is
    #: `WaterDepth`). It also keeps `isDecalSurface` on one source, so the report's
    #: `decalSurfaces` and `decalProjectorsBound` counts are the same read.
    decal_asset: str | None = None
    #: R7.2 ruling 3: this face group is a `$decal 1` surface (`isDecalSurface`), so it binds
    #: `decal_asset` as its MESH slot and draws in the mesh-decal pass -- coplanar with the wall,
    #: no z-fight, lit as the wall, which is what a lightmapped `$decal` face did.
    is_decal_surface: bool = False
    #: R7.4 ruling E (revised): every face in this group faces down (`#underside`), so it binds
    #: `underside_asset` -- the material lane's `_Underside` twin, which is this instance with the
    #: reflection pass gone, exactly what `Mod_LoadFaces` does by undefining `$reflecttexture`.
    underside: bool = False
    #: R7.4 (G6, owner decision 4): every face in this group animates on this lightstyle, so the
    #: bake tags the chunk `ElysiumBakedTags::LightStyle(style)` and the light rig writes its
    #: brightness into CPD slot 6. `None` is a group nothing modulates.
    light_style: int | None = None

    @property
    def opaque(self) -> bool:
        # R7.2 ruling 3: a decal-surface group is never Nanite. Its bound instance is the
        # deferred-decal-domain projector, which draws in the mesh-decal pass only from a
        # non-Nanite section (`PostProcessMeshDecals.cpp` 255) and whose master sets no
        # `used_with_nanite`; a Nanite chunk would fall back to the default material at render.
        if self.is_decal_surface:
            return False
        return self.blend_mode in NANITE_BLEND_MODES and self.master in NANITE_CAPABLE_MASTERS

    @property
    def underside_asset(self) -> str | None:
        """The `_Underside` twin this group binds, or `None` when it is not an underside group.

        The name is the surface instance's plus the suffix, in the same folder -- the decal twin's
        own shape (`MI_<unit>_Decal`), restated so the bake needs no second naming rule.
        """

        return f"{self.asset}_Underside" if self.underside else None

    @property
    def slot_asset(self) -> str | None:
        """The instance the bake binds into this face group's material slot: the projector twin
        for a `$decal` surface (ruling 3), the surface instance for everything else. `None` when
        the unit says it is a decal surface and the material lane staged no projector for it --
        a named bake failure, never a silent rebind onto the surface instance."""

        if self.is_decal_surface:
            return self.decal_asset
        return self.underside_asset or self.asset

    def as_row(self) -> dict[str, Any]:
        return {
            "unit": self.unit, "asset": self.asset, "decalAsset": self.decal_asset,
            "isDecalSurface": self.is_decal_surface, "master": self.master,
            "blendMode": self.blend_mode, "opaque": self.opaque, "patched": self.patched,
            "provenance": self.provenance,
            "underside": self.underside, "undersideAsset": self.underside_asset,
            "lightStyle": self.light_style,
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

    def water_face_rows(self) -> list[dict[str, Any]]:
        """Every scene's `%compilewater` face rows, in face order (R7.4, `_water_face_row`)."""

        rows = [row for scene in (self.world, self.sky, *self.brushes.values())
                for row in scene.water_faces]
        return sorted(rows, key=lambda row: row["index"])

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


def face_underside(units: sidecars.MapUnits, face: dict[str, Any]) -> bool:
    """Does this face's own plane point down in the Unreal frame (`UNDERSIDE_NORMAL_Z`)?

    Ruling E (revised): underside is a fact about a FACE, never about a material.
    `sm_pier_1` is the map that settles it -- the patched `_depth_33` instance faces up and its
    `invisible_water` parent faces down, one unit each way -- and VtMB agrees, because
    `Mod_LoadFaces` reads the plane row and not the VMT. The unit publishes plane normals in the
    glTF frame, whose `y` is Unreal's `z`.
    """

    planes = units.root.get("planes") or []
    index = int(face.get("plane", -1))
    if not 0 <= index < len(planes):
        return False
    return float(planes[index]["normal"][1]) < UNDERSIDE_NORMAL_Z


def face_light_styles(face: dict[str, Any]) -> list[int]:
    """Every style the face names, in slot order -- `0` (the always-on base) and `255` (the empty
    slot) are not styles and never appear."""

    return [
        int(value) for value in list(face.get("styles") or ())[:LIGHTSTYLE_SLOTS]
        if int(value) not in (LIGHTSTYLE_BASE, LIGHTSTYLE_EMPTY)
    ]


def face_light_style(face: dict[str, Any]) -> int | None:
    """The lightstyle this face animates on, or `None` for a face that animates on none.

    VtMB sums one lightmap page per `styles[]` slot, each scaled by that style's pattern; the port
    has no lightmap (owner decision 4 -- Lumen replaced it) and one brightness scalar per chunk, so
    one style has to be named. **The lowest** one is: Quake's inherited animated patterns are styles
    1-11 and a named `light` entity's switchable style is 32-63, so the lowest non-base style is the
    one that actually moves, and the switchable one defaults to full brightness anyway. Slot order
    would decide it arbitrarily -- measured on `sm_pier_1`'s 34 `objects/surf` foam cards, all 34
    name style 1 but 18 of them name 32 in the earlier slot, which would have split one waterline
    into two chunks flickering on different patterns.

    Every style the face names stays on the row (`face_light_styles`), so a chunk that wants the
    switchable one too has it without re-reading the lump.
    """

    styles = face_light_styles(face)
    return min(styles) if styles else None


def section_key(key: str, *, underside: bool = False, light_style: int | None = None) -> str:
    """The group key one face lands in: its material's, plus what the face itself is.

    Two suffixes, always in this order, so a key is parsed by splitting and never by guessing:
    `<material>[@<cubemap>][#underside][#style<n>]`. Both name a BINDING the bake makes on the
    section -- the `_Underside` twin instance, and the lightstyle tag plus CPD slot -- and both are
    per-face facts that the old one-group-per-material split had nowhere to put.
    """

    if underside:
        key += UNDERSIDE_SUFFIX
    if light_style is not None:
        key += f"{LIGHTSTYLE_SUFFIX}{int(light_style)}"
    return key


def split_section_key(key: str) -> tuple[str, bool, int | None]:
    """`section_key`'s inverse: `(material group key, underside, lightStyle)`."""

    style: int | None = None
    head, sep, tail = key.rpartition(LIGHTSTYLE_SUFFIX)
    if sep and tail.isdigit():
        key, style = head, int(tail)
    underside = key.endswith(UNDERSIDE_SUFFIX)
    if underside:
        key = key[:-len(UNDERSIDE_SUFFIX)]
    return key, underside, style


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


def _source_to_gltf(point: Sequence[float]) -> tuple[float, float, float]:
    """Source inches to glTF metres -- `_gltf_to_source` the other way, for a lump this lane reads
    raw (lump 38's primitive vertices are published as the BSP's own `Vector`s, untransformed)."""

    scale = sidecars.GLTF_SCALE
    return (float(point[0]) * scale, float(point[2]) * scale, -float(point[1]) * scale)


def _primitive_triangles(kind: int, indices: Sequence[int]) -> list[tuple[int, int, int]]:
    """One primitive's index run as triangles, in the mode `dprimitive_t.type` names.

    `PRIM_TRISTRIP` is unwound here rather than carried: the staged pair is a triangle list, and a
    strip's own alternating winding is exactly what `MATERIAL_TRIANGLE_STRIP` gives the rasterizer.
    Degenerate triangles (a strip's stitch) are dropped -- Unreal's builder would drop them anyway,
    and the area pin counts what is drawn.
    """

    out: list[tuple[int, int, int]] = []
    if kind == PRIM_TRILIST:
        for base in range(0, len(indices) - 2, 3):
            out.append((indices[base], indices[base + 1], indices[base + 2]))
        return out
    if kind == PRIM_TRISTRIP:
        for base in range(len(indices) - 2):
            a, b, c = indices[base], indices[base + 1], indices[base + 2]
            if a == b or b == c or a == c:
                continue
            out.append((a, b, c) if base % 2 == 0 else (a, c, b))
        return out
    return out


def _append_primitives(
    scene: Scene,
    tris: list[int],
    units: sidecars.MapUnits,
    face: dict[str, Any],
) -> bool:
    """One face's compiled primitive grid, when vbsp built it one (G13). True when it meshed.

    `$subdivsize` makes vbsp tessellate a water face into a regular grid and store it in lumps
    37/38/39; `Shader_DrawSurfaceDynamic` reads `numPrims` as its first act and, when it is
    non-zero, draws the grid **to the exclusion of** both the adaptive-subdivision branch and the
    surfedge fan -- so on the ~9 maps that carry one, the compiled grid *is* VtMB's water mesh
    (U2, verdict A2). Every one of the 512 primitive-bearing faces in the corpus binds a material
    authoring `$subdivsize 64`, and none of them is on a map in the R7.4 scope, so this path is
    pinned on synthetic units and gates nothing (verdict B4).

    Two rules come straight off `Mod_LoadPrimVerts` (`FUN_200b71e0`), which zero-fills a 28-byte
    runtime record and copies only the 12-byte position: lump 38 holds **positions only**, so the
    UVs are re-derived from the texinfo vectors here exactly as `BuildMSurfaceVerts` does for an
    ordinary face; and the strip carries the face's PLANE normal, constant across the grid, which
    this lane gets for free by emitting one coplanar grid per face (the bake recomputes normals and
    a coplanar section has one).
    """

    count = int(face.get("numPrims") or 0)
    if count <= 0:
        return False
    block = units.root.get("primitives") or {}
    rows = block.get("primitives") or []
    verts = block.get("verts") or []
    index_table = (block.get("indices") or {}).get("values") or []
    first = int(face.get("firstPrimID") or 0)
    if first + count > len(rows):
        return False
    vectors, width, height = _texture_vectors(units, face)
    meshed = False
    for row in rows[first:first + count]:
        first_vertex, vertex_count = int(row["firstVert"]), int(row["vertCount"])
        first_index, index_count = int(row["firstIndex"]), int(row["indexCount"])
        if first_vertex + vertex_count > len(verts):
            continue
        run = [int(value) for value in index_table[first_index:first_index + index_count]]
        if not run:
            continue
        # `BuildMSurfacePrimIndices` indexes the primitive's own vertex run; a compiler that wrote
        # them absolute into lump 38 is read the same way rather than meshed inside out.
        base = first_vertex if max(run) >= vertex_count else 0
        local: dict[int, int] = {}
        for offset in range(vertex_count):
            point = verts[first_vertex + offset]["point"]
            local[offset] = scene.emit(
                _source_to_gltf(point), _planar_uv(point, vectors, width, height))
        for a, b, c in _primitive_triangles(int(row["type"]), [value - base for value in run]):
            if not all(0 <= corner < vertex_count for corner in (a, b, c)):
                continue
            # Reversed for the same reason `_append_face` reverses: the frame is a reflection.
            tris.extend((local[a], local[c], local[b]))
            meshed = True
    return meshed


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


#: One Source square inch in square centimetres -- `faces[].area` is the compiler's own area and
#: the staged meshes are centimetres.
SOURCE_AREA_TO_CM2 = 2.54 * 2.54


def meshed_area_cm2(positions: Sequence[Sequence[float]], indices: Sequence[int]) -> float:
    """The surface area, in square centimetres, of a flat index run over `positions`.

    Half the cross-product magnitude per triangle, summed -- winding-independent, so the Unreal
    reversal `_append_face` applies changes nothing. This is the measured half of the G26/verdict
    B3 area pin: `faces[].area` is what vbsp computed for the face, and this is what the stage
    actually meshed for it, so the two disagreeing means the mesh path dropped or duplicated
    geometry (the primitive grid vs the shard fan, U2's `Shader_DrawSurfaceDynamic` order).
    """
    total = 0.0
    for base in range(0, len(indices) - 2, 3):
        a, b, c = (positions[indices[base + offset]] for offset in range(3))
        ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
        vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
        cx, cy, cz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        total += 0.5 * math.sqrt(cx * cx + cy * cy + cz * cz)
    return total


def _water_face_row(
    units: sidecars.MapUnits,
    face: dict[str, Any],
    face_index: int,
    key: str,
    unit: str,
    scene_name: str,
    *,
    underside: bool,
    style: int | None,
    triangles: int,
    meshed_area_cm2: float,
) -> dict[str, Any]:
    """One `water.faces[]` row: what vbsp published about a water face and the port never read.

    - `underside` / `lightStyle` are the two facts the group key was split on, restated per face so
      a reader never has to parse a key (rulings E revised and M, G6).
    - `surfaceFogVolumeID` is the compiler's own statement of which fog volume this face bounds --
      `0` on exactly the pier's 27 water faces and `0xFFFF` on its other 5,423 (G11). Downgraded to
      a cross-check by the audit, not a source: the engine forces `0xFFFF` on every non-WARP face
      and the values exist only in the Unofficial-Patch recompile.
    - `texdata` is the per-surface identity vbsp split the sheet on -- 36 rows all named
      `WATER/INVISIBLE_WATER` on the pier, 35 of them naming exactly one texinfo and one face (G22).
    - `primitive` is `{first, count}` from `dface+100/102`, the compiled grid this face draws as
      when the count is non-zero (G13).
    - `area` is the compiler's own square inches, `areaCm2` the same number in the bake's units,
      and `meshedAreaCm2` is what this stage actually produced for the face. Together they ARE the
      area pin (G26/verdict B3), answered offline: `originalFaces[].area` is 0.0 on all 2,182 rows
      in the corpus, so a rebuilt or re-tessellated water mesh is checked against `faces[].area`
      and never against the pre-CSG original's.
    """

    texinfos = units.root["texinfos"]
    tex_info = int(face["texInfo"])
    tex_data = int(texinfos[tex_info]["texData"]) if 0 <= tex_info < len(texinfos) else -1
    planes = units.root.get("planes") or []
    plane_index = int(face.get("plane", -1))
    normal = (
        gltf_position_to_unreal(planes[plane_index]["normal"])
        if 0 <= plane_index < len(planes) else (0.0, 0.0, 0.0)
    )
    area = float(face.get("area") or 0.0)
    return {
        "index": face_index,
        "scene": scene_name,
        "group": key,
        "unit": f"vtmb:material:{unit}",
        "underside": underside,
        "lightStyle": style,
        "lightStyles": face_light_styles(face),
        "surfaceFogVolumeID": int(face.get("surfaceFogVolumeID", 0xFFFF)),
        "texInfo": tex_info,
        "texdata": tex_data,
        "plane": plane_index,
        "side": int(face.get("side", 0)),
        # The plane row is a direction, so it takes the frame's permutation and no scale; the
        # length is 1 either way and this is what `underside` was decided on.
        "normal": [round(value / GLTF_TO_UNREAL, 6) for value in normal],
        "primitive": {"first": int(face.get("firstPrimID") or 0),
                      "count": int(face.get("numPrims") or 0)},
        "area": round(area, 4),
        "areaCm2": round(area * SOURCE_AREA_TO_CM2, 4),
        "meshedAreaCm2": round(float(meshed_area_cm2), 4),
        "triangles": triangles,
    }


def _build_scene(
    units: sidecars.MapUnits,
    prims: _Primitives,
    mesh: int,
    face_indices: Sequence[int],
    map_name: str,
    compile_water=None,
    scene_name: str = "",
) -> Scene:
    scene = Scene()
    faces = units.root["faces"]
    for face_index in face_indices:
        face = faces[face_index]
        base_key = group_key(units, face, map_name)
        if base_key is None:
            continue
        raw = sidecars._face_material(units, face)
        # Underside is a WATER split. Every ceiling in the map has a downward plane normal too, and
        # `Mod_LoadFaces` only reads the normal to decide whether to undefine `$reflecttexture` --
        # a key only a water shader registers. Splitting any other family would ask the material
        # lane for an `_Underside` twin it stages for water units alone.
        water = compile_water is not None and compile_water(raw)
        underside = water and face_underside(units, face)
        style = face_light_style(face)
        key = section_key(base_key, underside=underside, light_style=style)
        if scene.units.setdefault(key, raw) != raw:
            raise MapGeometryError(
                f"{units.name}: face group {key!r} resolves two material units "
                f"({scene.units[key]!r}, {raw!r})")
        tris = scene.groups.setdefault(key, [])
        before = len(tris)
        if int(face["dispInfo"]) >= 0:
            _append_displacement(scene, tris, units, prims, face)
        # `Shader_DrawSurfaceDynamic`'s own order: `numPrims` first and to the exclusion of the
        # fan, then the fan for everything the compiler left untessellated (G13/U2).
        elif not _append_primitives(scene, tris, units, face) and face.get("primitive") is not None:
            _append_face(scene, tris, units, prims, mesh, face)
        if water:
            scene.water_faces.append(_water_face_row(
                units, face, face_index, key, raw, scene_name,
                underside=underside, style=style, triangles=(len(tris) - before) // 3,
                meshed_area_cm2=meshed_area_cm2(scene.positions, tris[before:])))
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


def read_geometry(map_name: str, root: Path | None = None, compile_water=None) -> MapGeometry:
    """Read one map's root unit into the structures the V2 bake authors from.

    `compile_water(base material key) -> bool` is the material lane's answer to "did vbsp read
    `%compilewater` off this unit", and it decides two things: which `SURF_NODRAW` faces the
    producer keeps (`sidecars.meshed_faces`, owner decision 2) and which faces get a `water.faces[]`
    row. `None` is the reader with no material staging tree behind it -- the legacy scene split, and
    no water face rows.
    """

    join = sidecars.prepare_join(map_name, root, compile_water)
    units = join.units
    prims = _Primitives(units)
    nodes = units.document["nodes"]
    models = {int(row["index"]): row for row in units.root["models"]}
    world_mesh = int(nodes[int(models[0]["node"])]["mesh"])

    world = _build_scene(units, prims, world_mesh, join.scenes["world"], map_name,
                         compile_water, "world")
    sky_scene = _build_scene(units, prims, world_mesh, join.scenes["sky"], map_name,
                             compile_water, "sky")
    brushes: dict[int, Scene] = {}
    for model_index, face_indices in sorted(join.scenes["brush"].items()):
        mesh = int(nodes[int(models[model_index]["node"])]["mesh"])
        scene = _build_scene(units, prims, mesh, face_indices, map_name,
                             compile_water, f"brush_{model_index}")
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

    R7.4: a key carrying `#underside` or `#style<n>` (`section_key`) resolves the same unit as its
    base key and states the face fact on the row -- one lookup, one binding rule, and the suffix
    parsed in exactly one place.

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
    unprojected: list[str] = []
    for key in sorted(units_by_group):
        unit_key = units_by_group[key]
        _base_key, underside, light_style = split_section_key(key)
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
            decal_asset=(str(root.get("decalAsset")) if root.get("decalAsset") else None),
            is_decal_surface=bool(root.get("isDecalSurface")),
            underside=underside, light_style=light_style,
        )
        # R7.2 ruling 3: a `$decal 1` face group binds the projector twin as its mesh slot, so a
        # sidecar that says `isDecalSurface` and names no `decalAsset` has no slot to bind at all.
        # Named here, per map, rather than discovered as a grey wall in the editor.
        if table[key].slot_asset is None:
            unprojected.append(unit_key)
    if missing or broken or unprojected:
        parts = []
        if missing:
            parts.append(f"{len(missing)} material unit(s) not staged by the material lane "
                         f"(run: uv run elysium import materials): "
                         + ", ".join(missing[:8]))
        if broken:
            parts.append(f"{len(broken)} patched unit(s) whose base chain leaves the staging "
                         f"tree or names no master: " + ", ".join(broken[:8]))
        if unprojected:
            parts.append(f"{len(unprojected)} $decal surface unit(s) whose sidecar names no "
                         f"decalAsset (re-run: uv run elysium import materials): "
                         + ", ".join(unprojected[:8]))
        raise MapGeometryError(f"{map_name or 'map'}: " + "; ".join(parts))
    return table


def _unit_parameters(read_sidecar, unit_key: str) -> dict[str, str] | None:
    """One material unit's authored VMT keys, its base's first and every patch delta over them.

    A PAKFILE-patched unit's provenance carries only its own delta -- `maps/ch_fulab_1/water/
    cheap_water_1318_1990_273` is `include` plus one `insert` block -- so `%compilewater` and the
    four fog keys are reachable only through `patchBase`. The walk and its `hops > 8` guard are
    `resolve_material_table`'s; a chain that leaves the staging tree stops where it stops and a key
    nobody authored is simply absent, which is `SetFogVolumeState`'s own answer for it.

    `None` is "the material lane staged no document for this unit", which is the only case an
    operator can fix by re-running the lane; `{}` is a staged unit that authors none of these keys,
    which is `_water_fog`'s `fogEnable: false` and not an error.
    """

    chain: list[dict[str, Any]] = []
    document = read_sidecar(unit_key)
    if document is None:
        return None
    hops = 0
    while document is not None:
        chain.append(document)
        if not document.get("patched"):
            break
        base = str(document.get("patchBase") or "")
        if not base.startswith("vtmb:material:") or hops > 8:
            break
        document = read_sidecar(base[len("vtmb:material:"):])
        hops += 1
    values: dict[str, str] = {}
    for document in reversed(chain):
        for row in document.get("parameters") or []:
            values[str(row.get("key") or "").lower()] = str(row.get("value") or "")
    return values


def compile_water_predicate(read_sidecar):
    """`material key -> bool`: did vbsp read `%compilewater` off this unit's own VMT?

    The key lives on the unit's authored parameters and reaches a PAKFILE-patched instance only
    through its `patchBase` (`_unit_parameters`), which is why the sidecar producer cannot answer it
    and this stage can.

    Two spellings, in this order, because the material lane stages patched units two ways and both
    appear on the water cast: the unit's own key first
    (`maps/sm_pier_1/water/invisible_water_depth_33` is staged in its own right and reaches
    `%compilewater` through its `patchBase`), then the base fold
    (`maps/sm_hub_1/dev/dev_waterbeneath2_-1612_-111_-5876` is an `$envmap` probe copy the lane
    folds into `dev/dev_waterbeneath2`, exactly as `group_key` folds it). Asking only for the base
    fold loses the pier's nine `_depth_33` faces -- the map's whole up-facing water surface.

    The predicate answers "is this a DRAWN water face": `%compilewater` and not `%compilenodraw`.
    Owner decision 2 ("surface on nodraw water") was withdrawn in the R7.5 look pass against the
    owner's own VtMB frames: `sm_pier_1`'s ocean is the `water/blackwater` card 21 in below the
    plane, and a surface drawn over the `%compilenodraw` volume hid it. VtMB draws nothing on a
    nodraw water brush and neither does the port; the brush is still a volume
    (`resolve_water_volumes` reads `%compilewater` on its own), so its fog, its body state and its
    events stay. A `%compilewater` face that is NOT nodraw is a water face and is never dropped
    for `SURF_NODRAW` (`sidecars.meshed_faces`).
    """

    lookup = _memoized_unit_parameters(read_sidecar)

    def compiles_water(material: str | None) -> bool:
        if not material:
            return False
        for key in (material, shared_corpus.base_material(material)):
            values = lookup(key)
            if values is not None:
                return COMPILE_WATER_KEY in values and COMPILE_NODRAW_KEY not in values
        return False

    return compiles_water


def _memoized_unit_parameters(read_sidecar):
    """`_unit_parameters` with one walk per unit key per map.

    Every non-bevel side of every `CONTENTS_WATER` brush asks the same question, and the content bit
    is deliberately wide (the `tools/tools_shadow` casters carry it), so the uncached reader re-walks
    -- and re-reads from disk -- one or two sidecars per side across the whole shadow-caster set.
    """

    cache: dict[str, dict[str, str] | None] = {}

    def lookup(unit_key: str) -> dict[str, str] | None:
        if unit_key not in cache:
            cache[unit_key] = _unit_parameters(read_sidecar, unit_key)
        return cache[unit_key]

    return lookup


def _water_fog(values: dict[str, str]) -> dict[str, Any]:
    """The four authored fog keys as a volume carries them: the colour undecoded (`{r g b}` is
    already divided by 255 by the material lane's own shape, `[r g b]` is verbatim -- the `.env`
    convention, the gamma decode is the shader's), start and end in centimetres."""

    from elysium_pipeline.importers import materials as material_lane

    colour = material_lane._VECTOR_SHAPE["FogColor"](values.get("$fogcolor", "[0 0 0]"))
    return {
        "fog_enable": material_lane._truthy(values.get("$fogenable", "")),
        "fog_color": tuple(colour[:3]),
        "fog_start_cm": round(material_lane._parse_scalar(values.get("$fogstart", "0")) * 2.54, 4),
        "fog_end_cm": round(material_lane._parse_scalar(values.get("$fogend", "0")) * 2.54, 4),
    }


def _water_brushes(units: sidecars.MapUnits, unit_parameters) -> list[tuple[float, WaterBrush]]:
    """Every `CONTENTS_WATER` brush a `%compilewater` side proves is water, paired with the Unreal
    height of its horizontal top plane -- the key a `LEAFWATERDATA` row joins on.

    The hull is not re-solved here: `UE_map_sidecars.source_planes` + `brush_hull` + `hull_vertices`
    are the shipped collision's own solver, tolerances included, so a volume's AABB and the
    `.hulls` sidecar's vertices come from one implementation.
    """

    collision = units.root.get("collision") or {}
    brush_rows = collision.get("brushes") or []
    side_rows = collision.get("brushSides") or []
    plane_rows = units.root.get("planes") or []
    source = sidecars.source_planes(plane_rows)
    found: list[tuple[float, WaterBrush]] = []
    for brush in brush_rows:
        if not int(brush["contents"]) & CONTENTS_WATER:
            continue
        first = int(brush["firstSide"])
        sides = side_rows[first:first + int(brush["numSides"])]
        planes: list[tuple[float, float, float, float]] = []
        top: float | None = None
        compiles_water = False
        for side in sides:
            if int(side["bevel"]):
                continue
            if not compiles_water:
                # One side proves the brush; the rest are here only for their planes.
                material = sidecars._face_material(units, side)
                values = unit_parameters(material) if material is not None else None
                compiles_water = values is not None and COMPILE_WATER_KEY in values
            row = plane_rows[int(side["plane"])]
            normal = row["normal"]
            # The unit publishes planes in the glTF frame; `gltf_position_to_unreal`'s permutation
            # is orthogonal, so the normal only permutes and the distance only scales.
            plane = (
                float(normal[0]), float(normal[2]), float(normal[1]),
                round(float(row["dist"]) * GLTF_TO_UNREAL, 4),
            )
            planes.append(plane)
            if plane[2] >= WATER_TOP_NORMAL_Z:
                top = plane[3]
        if not compiles_water or top is None:
            continue
        _, points = sidecars.brush_hull(source, sides, int(brush["contents"]))
        if points is None:
            continue
        flat = sidecars.hull_vertices(points)
        axes = [flat[axis::3] for axis in range(3)]
        found.append((top, WaterBrush(
            planes=tuple(planes),
            bounds_min=tuple(min(axis) for axis in axes),
            bounds_max=tuple(max(axis) for axis in axes),
        )))
    return found


def _physics_blocks(units: sidecars.MapUnits, kind: str) -> list[dict[str, str]]:
    """Every `physics.models[0].keyValues` block of one type, as a flat `{key: value}` dict.

    Model 0 is worldspawn's, which is the only model any of these blocks appear on: vbsp writes the
    `fluid` and `materialtable` blocks once, for the world.
    """

    models = (units.root.get("physics") or {}).get("models") or []
    if not models:
        return []
    out: list[dict[str, str]] = []
    for block in models[0].get("keyValues") or []:
        if str(block.get("type") or "").lower() != kind:
            continue
        out.append({str(pair.get("key") or "").lower(): str(pair.get("value") or "")
                    for pair in block.get("pairs") or []})
    return out


def _fluid_number(values: dict[str, str], key: str) -> float | None:
    """One authored fluid scalar, or `None` where the block does not carry the key at all."""

    if key not in values:
        return None
    try:
        return float(values[key])
    except ValueError:
        return None


def _water_fluid(units: sidecars.MapUnits) -> WaterFluid | None:
    """The map's `fluid { }` block in the bake's frame (G7), or `None` where it authored none.

    The plane and the velocity are Source inches: the plane normal is a direction, so it takes the
    frame's Y negation and no scale, and its distance takes the inch-to-centimetre scale like any
    length. `sm_hub_1`'s `0 0 1 -5881` lands on `surfaceZCm -14937.74`, which is the volume's own
    surface to four decimals -- the pin `resolve_water_volumes` joins the two rows on.
    """

    blocks = _physics_blocks(units, "fluid")
    if not blocks:
        return None
    values = blocks[0]
    # Verdict B5: the creation guard is `fluid.index > 0` and nothing else -- `vampire.dll
    # FUN_10158600` tests the first dword of `ParseFluid`'s output (`101586ff JLE skip`), so a
    # block naming index 0 makes no controller at all. Refusing the row here is what keeps
    # `resolve_water_volumes` from carving the volume out of `solids[0]`, the world's own collision
    # solid, whose ledges would then outrank the real water brushes in `ElysiumWater::FindVolumeAt`
    # and read the whole map as submerged. Both owner maps author `index "5"`.
    raw_index = _fluid_number(values, "index")
    index = int(raw_index) if raw_index is not None else 0
    if index <= 0:
        return None
    plane = [float(token) for token in values.get("surfaceplane", "").split()[:4]]
    if len(plane) != 4:
        plane = [0.0, 0.0, 1.0, 0.0]
    velocity = [float(token) for token in values.get("currentvelocity", "").split()[:3]]
    if len(velocity) != 3:
        velocity = [0.0, 0.0, 0.0]
    contents = values.get("contents")
    return WaterFluid(
        index=index,
        density=_fluid_number(values, "density"),
        damping=_fluid_number(values, "damping"),
        surface_plane=(
            round(plane[0], 6), round(-plane[1], 6), round(plane[2], 6),
            round(plane[3] * 2.54, 4),
        ),
        current_velocity_cm=tuple(round(value, 4) for value in source_to_unreal(*velocity)),
        contents=int(float(contents)) if contents else None,
        surface_prop=values.get("surfaceprop") or None,
    )


def _material_table_water_index(units: sidecars.MapUnits) -> int | None:
    """The physics `materialtable`'s `water` row (G17), or `None` where the table has none."""

    for values in _physics_blocks(units, "materialtable"):
        if "water" in values:
            try:
                return int(float(values["water"]))
            except ValueError:
                return None
    return None


def _solid_pieces(units: sidecars.MapUnits, solid_index: int) -> list[WaterBrush]:
    """One physics solid's ledges as convex plane sets in the bake's frame (G18).

    A ledge is a leaf of the IVPS compact surface's own tree -- vbsp's convex decomposition of the
    volume, 12 triangles over 8 vertices for a box -- and the unit already publishes its vertices
    and triangle indices in two accessors (`physics.positionAccessor` / `indexAccessor`). The plane
    set is derived from the triangles rather than carried, because the lump states topology and not
    planes; each plane is oriented against the piece's own centroid, so `n . p - d <= 0` names the
    inside exactly as `WaterBrush.planes` does for a BSP brush, and the duplicate faces of one
    convex (the 12 triangles of a box are 6 planes) collapse.

    The IVP frame is the unit's `(x, y, z)_gltf = (x, -y, -z)_ivp`, unscaled metres, so composing it
    with `gltf_position_to_unreal` is `formats.phy`'s own empirically settled `(x, -z, -y) * 100`.
    Witnessed on `sm_pier_1`, whose single ledge lands on the staged brush AABB within a centimetre.
    """

    physics = units.root.get("physics") or {}
    models = physics.get("models") or []
    position_accessor = physics.get("positionAccessor")
    index_accessor = physics.get("indexAccessor")
    if not models or position_accessor is None or index_accessor is None:
        return []
    solids = models[0].get("solids") or []
    if not 0 <= solid_index < len(solids):
        return []
    positions = units.accessor(int(position_accessor))
    indices = units.accessor(int(index_accessor)).reshape(-1)
    pieces: list[WaterBrush] = []
    for ledge in solids[solid_index].get("ledges") or []:
        first_vertex, vertex_count = int(ledge["firstVertex"]), int(ledge["vertexCount"])
        first_index, index_count = int(ledge["firstIndex"]), int(ledge["indexCount"])
        if vertex_count < 4 or index_count < 3:
            continue
        if first_vertex + vertex_count > len(positions) or first_index + index_count > len(indices):
            continue
        points = np.array([
            gltf_position_to_unreal(positions[first_vertex + offset])
            for offset in range(vertex_count)
        ])
        centre = points.mean(axis=0)
        run = indices[first_index:first_index + index_count].astype(np.int64) - first_vertex
        planes: dict[tuple[float, float, float, float], None] = {}
        for base in range(0, len(run) - 2, 3):
            corners = run[base:base + 3]
            if corners.min() < 0 or corners.max() >= vertex_count:
                continue
            a, b, c = (points[int(corner)] for corner in corners)
            normal = np.cross(b - a, c - a)
            length = float(np.linalg.norm(normal))
            if length <= 1e-9:
                continue                      # a degenerate triangle states no plane
            normal = normal / length
            distance = float(np.dot(normal, a))
            if float(np.dot(normal, centre)) - distance > 0.0:
                normal, distance = -normal, -distance
            planes[(round(float(normal[0]), 5), round(float(normal[1]), 5),
                    round(float(normal[2]), 5), round(distance, 3))] = None
        if not planes:
            continue
        pieces.append(WaterBrush(
            planes=tuple(planes),
            bounds_min=tuple(round(float(value), 4) for value in points.min(axis=0)),
            bounds_max=tuple(round(float(value), 4) for value in points.max(axis=0)),
        ))
    return pieces


def _leaf_box(leaf: dict[str, Any]):
    """One BSP leaf's integer Source-inch AABB as an Unreal-centimetre one.

    `source_to_unreal` negates Y, so the corner that was the minimum on that axis is the maximum
    here; the box is rebuilt from both transformed corners rather than transformed corner-wise.
    """

    low = source_to_unreal(*[float(value) for value in leaf["mins"]])
    high = source_to_unreal(*[float(value) for value in leaf["maxs"]])
    return (
        tuple(round(min(a, b), 4) for a, b in zip(low, high)),
        tuple(round(max(a, b), 4) for a, b in zip(low, high)),
    )


def _leaf_water_boxes(units: sidecars.MapUnits, record_index: int):
    """The AABBs of the leaves whose `leafWaterDataID` names one `LEAFWATERDATA` record (G10)."""

    leafs = (units.root.get("bsp") or {}).get("leafs") or []
    return tuple(_leaf_box(leaf) for leaf in leafs
                 if int(leaf.get("leafWaterDataID", -1)) == record_index)


def _near_water_boxes(units: sidecars.MapUnits, visibility, record_index: int):
    """The AABBs of every leaf in `union(PVS(cluster))` over the record's own water leaves (G9).

    This is VtMB's near-water annotation, derived rather than read: measured set-equal to the
    `0x200` leaves on `sm_hub_1` (97) and `sp_soc_3` (126), and computable on `sm_pier_1`, whose
    Unofficial-Patch recompile carries the bit on no leaf at all. Empty when the map ships no
    visibility sub-unit -- a derivation this lane could not make, never a "nothing is near water".

    Solid leaves are excluded: vbsp's dummy leaf 0 is a zero-extent box in cluster 0, which the
    pier's water can see, and no leaf VtMB annotated on either map is solid.
    """

    if visibility is None:
        return ()
    leafs = (units.root.get("bsp") or {}).get("leafs") or []
    clusters = {int(leaf["cluster"]) for leaf in leafs
                if int(leaf.get("leafWaterDataID", -1)) == record_index
                and int(leaf.get("cluster", -1)) >= 0}
    if not clusters:
        return ()
    near = visibility.pvs_union(sorted(clusters))
    return tuple(_leaf_box(leaf) for leaf in leafs
                 if int(leaf.get("cluster", -1)) in near
                 and not int(leaf.get("contents", 0)) & CONTENTS_SOLID)


def resolve_water_volumes(
    units: sidecars.MapUnits, read_sidecar, map_name: str = "", visibility=None,
) -> tuple[list[WaterVolume], list[dict[str, Any]]]:
    """The map's water volumes, and the `LEAFWATERDATA` rows that produced none (R7.1,
    `docs/architecture/water-architecture.md` -> section 5.1).

    One volume per real record, in lump order: a `surfaceTexInfoID` of -1 is vbsp's sentinel and is
    dropped, a record no water brush stands at is dropped and named (a `tools/tools_shadow` caster
    carries the water content bit but is not water, so `la_bradbury_3`'s row has nothing to carry).
    The fog keys come from the record's own material unit -- its VMT provenance, never the staged
    instance, because `invisible_water` and `cheap_water` land on masters with no fog lane at all
    and stage none of them.

    R7.4 completes the row. `visibility` is the map's visibility sub-unit
    (`importers.map_visibility.read_visibility`, `None` where the export carries none) and is the
    only source for `near_boxes_cm`. The compiler's `fluid { }` block, its convex `pieces` and the
    `materialtable` water index come off the physics model; the fluid joins a volume by its own
    surface plane standing at that volume's surface, within the same `WATER_SURFACE_TOLERANCE_CM`
    the brushes join on, because vbsp authors one fluid per map and the join has to be stated rather
    than assumed. A volume no fluid stands at carries `fluid: None` and no pieces -- the runtime
    then has the leaf carve and the brush, which is what a `%compilewater` volume with no controller
    is (verdict B5: the guard is `fluid.index > 0`, so both owner maps do get one).
    """

    rows = (units.root.get("water") or {}).get("leafData") or []
    volumes: list[WaterVolume] = []
    dropped: list[dict[str, Any]] = []
    if not rows:
        return volumes, dropped

    kept: list[tuple[int, float, float, int]] = []
    for row in rows:
        index = int(row["index"])
        tex_info = int(row["surfaceTexInfoID"])
        if tex_info < 0:
            dropped.append({"index": index, "reason": "sentinel"})
            continue
        kept.append((
            index,
            round(float(row["surfaceZ"]) * GLTF_TO_UNREAL, 4),
            round(float(row["minZ"]) * GLTF_TO_UNREAL, 4),
            tex_info,
        ))

    unit_parameters = _memoized_unit_parameters(read_sidecar)
    by_row: dict[int, list[WaterBrush]] = {index: [] for index, _z, _min, _tex in kept}
    for top, brush in _water_brushes(units, unit_parameters):
        for index, surface_z, _min_z, _tex_info in kept:
            # Lump order decides a tie: a brush stands in exactly one volume, and two records that
            # close on one height are one body of water read twice.
            if abs(top - surface_z) <= WATER_SURFACE_TOLERANCE_CM:
                by_row[index].append(brush)
                break

    fluid = _water_fluid(units)
    water_index = _material_table_water_index(units)
    unstaged: list[str] = []
    for index, surface_z, min_z, tex_info in kept:
        if not by_row[index]:
            dropped.append({"index": index, "reason": "no water brush"})
            continue
        material = sidecars._face_material(units, {"texInfo": tex_info})
        values = unit_parameters(material) if material is not None else None
        if values is None:
            # The volume's whole underwater look is these four keys; an unstaged unit would ship a
            # clear-water volume that nothing in the log explains. A unit that *is* staged and
            # authors none of them is not that case -- `$fogenable` absent is `fogEnable: false`,
            # which is `SetFogVolumeState`'s own answer and `_water_fog`'s.
            unstaged.append(material or f"texinfo {tex_info}")
            continue
        # One fluid per map in the whole corpus, joined on the surface it names rather than on
        # position in either list: `surfaceplane` is the authored plane and `surfaceZ` is the
        # compiler's, and on all three water maps they are the same number.
        mine = (fluid if fluid is not None
                and abs(fluid.surface_plane[3] - surface_z) <= WATER_SURFACE_TOLERANCE_CM
                else None)
        volumes.append(WaterVolume(
            index=index,
            surface_z_cm=surface_z,
            min_z_cm=min_z,
            material=f"vtmb:material:{material}",
            **_water_fog(values),
            brushes=tuple(by_row[index]),
            fluid=mine,
            pieces=tuple(_solid_pieces(units, mine.index)) if mine is not None else (),
            leaf_boxes_cm=_leaf_water_boxes(units, index),
            near_boxes_cm=_near_water_boxes(units, visibility, index),
            material_table_water_index=water_index,
        ))
    if unstaged:
        raise MapGeometryError(
            f"{map_name or 'map'}: {len(unstaged)} water volume material(s) not staged by the "
            f"material lane (run: uv run elysium import materials): " + ", ".join(unstaged[:8]))
    return volumes, dropped

def legacy_master_for(record: dict[str, Any] | None) -> str | None:
    """The legacy world master a `shared/materials.json` record selected (`Bake._master_for`)."""

    if record is None:
        return None
    for flag, master in LEGACY_MASTER_RULES:
        if record.get(flag):
            return master
    return LEGACY_MASTER_DEFAULT


def appearance_class(master: str, blend_mode: str, *, decal_bound: bool = False) -> str:
    """The class a V2 (master, blend) pair renders as -- the blend mode, except where the master
    itself is the distinction (`M_V2_Refract`, `M_V2_Water`, `M_V2_Decal`).

    R7.2: `decal` means "the projector instance is bound" -- a face group that binds
    `MI_<unit>_Decal` as its mesh slot (ruling 3), or a unit whose surface instance is itself on
    the projector master (the `decalmodulate` family). It is a statement about what draws, not
    about a flag in the `.mtl`.
    """

    if decal_bound:
        return "decal"
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
    v2_class = appearance_class(binding.master, binding.blend_mode,
                                decal_bound=binding.is_decal_surface and bool(binding.decal_asset))
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
        # R7.2 ruling 2/3: the projector instance staged for this unit, and the slot the bake
        # actually binds (the projector for a `$decal` surface, the surface instance otherwise).
        "decalAsset": binding.decal_asset,
        "slotAsset": binding.slot_asset,
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
            "decalProjectorsBound": sum(1 for row in rows if row["v2Class"] == "decal"),
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
    R7.1: and `water` -- every `LEAFWATERDATA` record as one volume with its fog keys and its
    `CONTENTS_WATER` brushes (`resolve_water_volumes`), the bake's `elysium.water` actor.
    R7.4: the volume rows gain the compiler's `fluid`, `pieces`, `leafBoxesCm` and `nearBoxesCm`,
    `water.faces[]` states every water face -- carrying `meshedAreaCm2` beside vbsp's own
    `faces[].area`, so the G26/verdict B3 area pin is answerable offline -- and the `materials`
    rows state the `#underside` and `#style<n>` sections the face groups now split on
    (`MANIFEST_VERSION` 11, the constant's own two-line changelog).
    """

    from elysium_pipeline.importers import effects as effects_lane
    from elysium_pipeline.importers import map_visibility as visibility_lane
    from elysium_pipeline.importers import textures as texture_lane

    staging = material_staging_root(work_root)
    if not staging.is_dir():
        raise MapGeometryError(
            f"no material staging tree at {staging} (run: uv run elysium import materials)")
    read_sidecar = sidecar_reader(staging)
    # The material lane is read BEFORE the geometry now: which `SURF_NODRAW` faces the producer
    # keeps is a question only the staged provenance can answer (R7.4, owner decision 2).
    geometry = read_geometry(map_name, root, compile_water_predicate(read_sidecar))
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
    water_volumes, water_dropped = resolve_water_volumes(
        geometry.join.units, read_sidecar, map_name,
        visibility_lane.read_visibility(map_name, root))
    water_faces = geometry.water_face_rows()
    counts = dict(geometry.counts)
    counts["waterVolumes"] = len(water_volumes)
    counts["waterFaces"] = len(water_faces)
    counts["waterUndersideFaces"] = sum(1 for row in water_faces if row["underside"])
    counts["lightStyleGroups"] = sum(
        1 for key in geometry.material_units() if split_section_key(key)[2] is not None)
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
        "water": {
            "volumes": [volume.as_row() for volume in water_volumes],
            "dropped": water_dropped,
            # R7.4: one row per `%compilewater` face, in face order. The bake's area pin, the
            # underside split's own evidence, and the compiler's per-face identity (G11/G13/G22/G26).
            "faces": water_faces,
        },
        "counts": counts,
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
