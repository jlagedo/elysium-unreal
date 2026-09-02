# The V2 map bake lane: world, 3D-sky and brush-model geometry and static-prop placements authored
# from the published map root unit (`vtmb:map:<map>`) instead of the legacy `<map>.obj`,
# `<map>_sky.obj`, `brushes/*.obj` and `<map>.props` sidecars.
#
# Roadmap R5.1 (`docs/project/seam_migration.md` -> "Roadmap -- one pipeline"); the ruling is
# `docs/architecture/seam_map_map.md` -> "## Import -- geometry and placements (R5.1)".
#
# **Beside the legacy bake, not over it.** This module holds only the inputs and the stages that
# differ; everything else -- decals, fog stamping, the player start, pruning, recipes and
# receipts -- is `bake_map.Bake`'s, unchanged, and a map that is not on the R5.1 flag never
# reaches a line of this file. The 3D-skybox transform is this lane's own since R6.7
# (`_read_sky` answers from the staged manifest, never from `<map>.sky`).
#
# **Lights (R5.6, `seam_map_map_lighting.md` -> "## Import" -> "Lights final").** One actor per
# lump-15 `worldLights[]` row, from the staged `lights` table (`UE_map_sidecars.light_rows`, the
# `.lights` producer's own rows), with every value `UElysiumLightRig::ApplyToSource` used to derive
# at load written here once by `derive_light` from the `UElysiumLightingSettings` page, plus the
# MegaLights policy. The runtime rig on a converted map snapshots the actor (`AdoptBaked`) and
# applies only the R4.3 calibration asset and the lightstyle animation. The two lanes are selected per map by
# `elysium_pipeline.map_transport.is_map_on_v2_models`, the tracked list in
# `Config/DefaultElysium.ini`.
#
# **Materials (R5.4, `seam_map_map.md` -> "## Import -- materials").** A V2 surface binds the
# imported `MI_` the material lane already made for its `vtmb:material:*` unit -- a PAKFILE-patched
# face by its `maps/<map>/...` id -- resolved offline into the staged manifest's `materials` table
# and loaded here by asset path. No per-map WORLD material package is authored for a V2 map: the
# `/ElysiumBaked/<map>/Materials` set is pruned. The one per-map material set that survives is the
# decal lane's `Materials/Decals` (legacy `M_Decal` MICs): a `UDecalComponent` renders only an
# `MD_DeferredDecal`-domain material and every V2 master is `MD_Surface`, so the decal rebind is
# R7.6's, not this task's.
#
# **This half reads a staged pair, not the GLB.** Decoding the unit needs `numpy` -- the sky-area BSP
# walk and the accessor decode -- and Unreal's embedded CPython does not carry it, so the read runs
# offline (`elysium_pipeline.importers.map_geometry.stage_map`, called from `unreal.bake_maps` just
# before this commandlet launches) and lands one manifest plus one packed vertex file under
# `$ELYSIUM_WORK_ROOT/import/map_geometry/<map>/`. Everything below reads that pair with `json` and
# `array` alone -- the same offline-stage / editor-import split the model, material and texture
# lanes use.
#
# **Detail props (R6.3, `seam_map_map.md` -> "Detail props (R6.3)").** Every `dprp` record of the
# staged `details` table becomes one instance of an `AElysiumDetailPropActor`'s instanced component
# -- one actor per model (and per 3D-skybox half) per map, on the R1 corpus mesh, instances in lump
# order, no collision, no shadow, culled at VtMB's `cl_detaildist`/`cl_detailfade` off the Models
# page, the record's `swayAmount / 255` as per-instance custom data float 0, and the material lane's
# `MI_` re-bound through a `MI_DetailSway_*` child that switches the master's `UseDetailSway` term
# on (`seam_map_material.md` -> "Detail sway on the model masters (R6.3)").
#
# **Sprites (R6.1, `seam_map_map.md` -> "Sprites (R6.1)").** Every staged `sprites[]` row (one per
# `env_sprite` block, lump order) becomes one `AElysiumSpriteActor`: the imported `MI_` of the
# sprite VMT re-parented once per `(MI_, blend)` through an `MI_Sprite_*` child that overrides the
# blend the entity's `rendermode` selects and switches the master's vertex colour/alpha on, the
# world size `scale x texture` in Source units, `rendercolor`/`renderamt`, `parallel_upright`, the
# `CSprite::Spawn` hidden rule, and the entity index as a tag so the leaf's inputs find it. The
# glow rule (screen-constant size, `19000 / dist^2`, the occlusion-query fade) is the runtime
# proxy's, off the Sprites settings page; the bake writes only the facts.
#
# **Effects (R7.3, `seam_map_map.md` -> "Import -- effects (R7.3)").** Every staged `effects[]` row
# (one per `env_particle` / `func_particle`, lump order, whose root resolved) becomes one
# `AElysiumEffectActor` carrying the row's fields under the same names and its root's
# `particleTrees{}` entry as the actor's `Tree`; every `dustmotes[]` / `steam[]` / `beams[]` row one
# `AElysiumDustActor` / `AElysiumSteamActor` / `AElysiumBeamActor` carrying its row. The actor is
# tagged `elysium.effect` + `elysium.ent=<index>` (+ `elysium.sky` inside the miniature, with the
# miniature transform a sprite takes), so the leaf's inputs reach it by entity index. The bake writes
# facts only: the Niagara user parameters are the actor's own writer at adopt (`effects-architecture.md`
# section 5.3). A property the actor class does not expose is a loud failure naming it
# (`EFFECT_ROW_FIELDS` / `EFFECT_NODE_FIELDS` are the one mapping both halves build to).
#
# `bake_map.py` is an editor *script* (`-run=pythonscript`), so it calls `main()` at module scope.
# Importing it from here would run a second whole bake, so the dependency goes the other way:
# `bake_map` calls `bind(sys.modules[__name__])` and then `bake_class()`, and this module reaches
# its host through `HOST`.
from array import array
import json
import math
import os
import sys
import time

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402
from elysium_pipeline import mounts  # noqa: E402
from elysium_pipeline import paths  # noqa: E402
from elysium_pipeline import placed_models as PM  # noqa: E402

#: The staged pair's names and schema. Restated rather than imported: `importers.map_geometry`
#: reaches numpy through the R3.2 producer, which this interpreter cannot load.
FAMILY = "map_geometry"
MANIFEST_NAME = "manifest.json"
MANIFEST_SCHEMA = "elysium.map-geometry"
#: 2 (R5.4): the manifest carries the `materials` table this lane binds from.
#: 3 (R5.5): and the `cubemaps` table this lane stands reflection captures at.
#: 4 (R5.6): and the `lights` table this lane derives every light actor from.
#: 5 (R6.3): and the `details` table this lane instances.
#: 6 (R6.1): and the `sprites` table this lane places billboards from.
#: 7 (R7.3): and the `effects` / `particleTrees` / `dustmotes` / `steam` / `beams` tables this
#: lane places effect actors from.
MANIFEST_VERSION = 7

#: The VtMB light types that place an actor (`type` 0 texlight, 1 point, 2 spot, 3 sun); type 5
#: skyambient tints the SkyLight through `_place_sky`'s R5.2 join and places none.
LIGHT_KIND_LABELS = {0: "tex", 1: "point", 2: "spot", 3: "sun"}

#: The key space the staged `MI_` instances live under in `Bake.materials` -- beside the legacy
#: `(package, key)` pairs, never colliding with one.
V2_MATERIAL_SCOPE = "v2"


#: The R1 model corpus (`docs/architecture/seam_map_model.md` -> "Import" -> "Identity and
#: naming"), `importers.models.PACKAGE_ROOT`'s value restated for the same reason. Its C++ twin
#: after this task's flip is `FElysiumContentPaths::BakedMeshes`.
V2_MESH_PACKAGE = "/ElysiumBaked/Meshes"
V2_SKIN_ASSET = "%s/DA_ElysiumPropSkins" % V2_MESH_PACKAGE
#: R6.3: the sway children of the imported `MI_` a detail model binds -- map-independent like the
#: mesh they dress, so they live beside the corpus meshes and no map's prune scope reaches them.
V2_DETAIL_MATERIAL_PACKAGE = "%s/Detail" % V2_MESH_PACKAGE
#: The one static switch a sway child sets (`ElysiumSurfaceParams{Lit,Unlit}::Switches`).
DETAIL_SWAY_SWITCH = "UseDetailSway"
#: The tags the runtime buckets a detail actor by (`ElysiumBakedTags::Detail` / `DetailModel`).
TAG_DETAIL = "elysium.detail"
#: R6.7: the miniature's scope marker (`bake_map.TAG_SKY` / `ElysiumBakedTags::Sky` restated so the
#: pure placement functions can carry it). A sky chunk or sky prop carries it *instead of* a class
#: tag; a detail component actor or a sprite actor carries it *beside* its class tag
#: (`seam_map_map.md` -> "3D-skybox composition (R6.7)").
TAG_SKY = "elysium.sky"
#: The shape `_place_details` writes a group as -- bumped when the writer changes what it puts on
#: the actor for the same staged rows (2: the R6.7 scope marker), so the level re-authors.
DETAIL_ACTOR_SHAPE = 2
#: `swayAmount` is a byte; the custom data float is its unit fraction.
DETAIL_SWAY_FULL = 255.0
#: R6.1: the per-blend children of the imported sprite `MI_` -- map-independent like the detail
#: sway children, so they live beside the corpus and no map's prune scope reaches them.
V2_SPRITE_MATERIAL_PACKAGE = "%s/Sprites" % mounts.BAKED
#: The two static switches a sprite child sets (`SpriteParams.Switches`): the tint and the glow
#: blend ride the quad's vertex colour, so no material is touched at runtime.
SPRITE_SWITCHES = ("UseVertexColor", "UseVertexAlpha")
#: The tags the runtime buckets a sprite actor by (`ElysiumBakedTags::Sprite` / `EntityIndex`).
TAG_SPRITE = "elysium.sprite"
TAG_ENTITY_PREFIX = "elysium.ent="
#: The shape `_place_sprites` writes a row as -- bumped when the writer changes what it puts on
#: the actor for the same staged row (2: the BGRA colour fix; 3: the R6.7 scope marker), so the
#: level re-authors.
SPRITE_ACTOR_SHAPE = 3
#: `BlendMode` member per staged blend name (`import_materials.BLEND_MODE_MEMBERS`, restated).
SPRITE_BLEND_MEMBERS = {
    "Opaque": "BLEND_OPAQUE", "Masked": "BLEND_MASKED", "Translucent": "BLEND_TRANSLUCENT",
    "Additive": "BLEND_ADDITIVE", "Modulate": "BLEND_MODULATE",
}

#: R7.3: the tag the runtime buckets an effect actor by (`ElysiumBakedTags::Effect`).
TAG_EFFECT = "elysium.effect"
#: The shape `_place_effects` writes a row as -- bumped when the writer changes what it puts on
#: the actor for the same staged row, so the level re-authors.
EFFECT_ACTOR_SHAPE = 1
#: The actor class per staged table (`effects-architecture.md` section 5.2 / 5.6).
EFFECT_ACTOR_CLASSES = {
    "effects": "ElysiumEffectActor", "dustmotes": "ElysiumDustActor",
    "steam": "ElysiumSteamActor", "beams": "ElysiumBeamActor",
}
#: `effects[]` row field -> actor property (Unreal's Python spelling of the UPROPERTY the contract
#: names: `EntityIndex` -> `entity_index`, `bActiveAtSpawn` -> `active_at_spawn`, ...). A row
#: field absent here is placement data the writer consumes itself (`origin_cm`, `rotation`,
#: `sky`, `unresolved`).
EFFECT_ROW_FIELDS = (
    ("index", "entity_index"), ("classname", "classname"), ("particle", "root"),
    ("particle_definition", "root_name"), ("attach_type", "attach_type"),
    ("parentname", "parent_name"), ("bone", "attach_bone"), ("attach_point", "attach_point"),
    ("active", "active_at_spawn"), ("start_hidden", "start_hidden"),
    ("spawnbounds_cm", "spawn_bounds_cm"), ("ramp_scale", "ramp_scale"),
    ("ramp_time", "ramp_time"), ("bounds_cm", "bounds_cm"), ("volume_scale", "volume_scale"),
    ("angles_deg", "angles_deg"), ("targetname", "target_name"), ("spawnflags", "spawn_flags"),
)
#: `dustmotes[]` / `steam[]` / `beams[]` row field -> actor property: the row's own names, in
#: Unreal's Python spelling (a `_cm` / `_s` suffix kept as written). `color` + `alpha` fold into
#: one `LinearColor` property `color`.
FAMILY_ROW_SKIP = frozenset({"index", "sky", "origin_cm", "rotation", "alpha", "targetname"})
#: `particleTrees{}.nodes[]` field -> `FElysiumParticleNode` property. Ramps (`[[t, lo, hi], ...]`)
#: become arrays of `FElysiumRampKey {T, Lo, Hi}`; the spawn block's fields are prefixed `spawn_`;
#: `sprite` / `normal` carry `texture` (path) and `aspect` (Vector2D); `collide` flattens to
#: `collide` (bool) + `collide_*`.
EFFECT_NODE_FIELDS = (
    ("index", "index"), ("id", "id"), ("name", "name"), ("kind", "kind"), ("draws", "draws"),
    ("spawns", "spawns"), ("parent", "parent"), ("via", "via"), ("blockIndex", "block_index"),
    ("depth", "depth"), ("resolved", "resolved"), ("fps", "fps"),
    ("lifetime_s", "lifetime"), ("lifetime_min_s", "lifetime_min"),
    ("lifetime_max_s", "lifetime_max"), ("loop", "loop"),
    ("size_cm", "size"), ("width", "width"), ("height", "height"), ("rotation_deg", "rotation"),
    ("red", "red"), ("green", "green"), ("blue", "blue"), ("color", "color"), ("mask", "mask"),
    ("refract", "refract"), ("radius_speed_cm_s", "radius_speed"),
    ("theta_speed_deg_s", "theta_speed"), ("phi_speed_deg_s", "phi_speed"),
    ("x_speed_cm_s", "x_speed"), ("y_speed_cm_s", "y_speed"), ("z_speed_cm_s", "z_speed"),
    ("elevation_speed_cm_s", "elevation_speed"), ("parent_speed", "parent_speed"),
    ("movealign", "move_align"), ("flat", "flat"), ("sortfront", "sort_front"),
    ("no_z_test", "no_z_test"), ("lighting", "lighting"), ("precipitation", "precipitation"),
    ("depth_offset_cm", "depth_offset_cm"), ("surface_color_optout", "surface_color_optout"),
)
EFFECT_SPAWN_FIELDS = (
    ("rate", "spawn_rate"), ("burst", "spawn_burst"), ("distance", "spawn_distance"),
    ("radius_cm", "spawn_radius"), ("theta_deg", "spawn_theta"), ("phi_deg", "spawn_phi"),
    ("x_cm", "spawn_x"), ("y_cm", "spawn_y"), ("z_cm", "spawn_z"),
    ("elevation_cm", "spawn_elevation"), ("rotation_deg", "spawn_rotation"),
    ("width", "spawn_width"), ("height", "spawn_height"), ("size", "spawn_size"),
    ("red", "spawn_red"), ("green", "spawn_green"), ("blue", "spawn_blue"),
    ("color", "spawn_color"), ("mask", "spawn_mask"), ("refract", "spawn_refract"),
    ("timescale", "spawn_timescale"),
)
EFFECT_COLLIDE_FIELDS = (
    ("bounce", "collide_bounce"), ("friction", "collide_friction"),
    ("gravity", "collide_gravity"), ("drag", "collide_drag"), ("self", "collide_self"),
    ("nested", "collide_nested"), ("spawn", "collide_spawn"),
)

#: The host script's namespace (`bake_map`'s `globals()`), bound once by it at import time. Wrapped
#: so this module reads `HOST.Bake` rather than a dict subscript, and read lazily so binding does not
#: have to wait for the last name the host defines.
HOST = None
_CLASS = [None]


class _Host(object):
    """Attribute access over the host script's namespace."""

    def __init__(self, namespace):
        self._namespace = namespace

    def __getattr__(self, name):
        try:
            return self._namespace[name]
        except KeyError:
            raise AttributeError(
                "bake_map defines no %r for the V2 lane to use" % name)


def bind(namespace):
    """Give this lane its host namespace. Called by `bake_map` right after importing this module."""

    global HOST
    HOST = _Host(namespace)


def bake_class():
    """The `Bake` subclass this lane runs, built on the host's own class."""

    if _CLASS[0] is None:
        _CLASS[0] = _build_class()
    return _CLASS[0]


def _build_class():
    Bake = HOST.Bake
    log = HOST.log
    fail = HOST.fail

    class MapBakeV2(Bake):
        """`bake_map.Bake` with its geometry and placement inputs taken from the map root unit."""

        #: Named in every receipt this lane writes, so a legacy asset and a V2 asset can never be
        #: mistaken for one another even at the same package path.
        lane = "v2"

        def __init__(self, map_name, tracker, digest_cache):
            Bake.__init__(self, map_name, tracker, digest_cache)
            self.geometry = None       # _StagedGeometry
            self.sky_model = None      # bl.ObjModel for the miniature
            self.sky_blend = []
            self.v2_materials = {}     # face group key -> _V2Material (R5.4)
            self.decal_mats = {}       # the legacy `.mtl` rows the decal lane still authors from
            self.v2_skins = {}         # stem -> (family count, [ {slot: material path} ])
            self.placed_index = {}     # catalogue stem -> npc_index row
            self.placed_index_version = 0
            self.rest_labels = {}      # placement index -> the rest clip it was dealt
            self.detail_materials = {}  # imported MI_ path -> its MI_DetailSway_* child path
            self.sprite_materials = {}  # (imported MI_ path, blend) -> its MI_Sprite_* child path

        # -------------------------------------------------------------------------- inputs

        def load_sources(self):
            """The map root unit's geometry, placements and materials, plus the map-scoped inputs
            the shared stages still own (`.env`, `.decals`, `.weather`, and the `.mtl` rows the
            decal lane alone still reads).

            Lights are the staged `lights` table's (R5.6). Every surface's material is the staged
            `materials` table's (R5.4); the `.mtl` is consulted for nothing but the decal lane's
            legacy `M_Decal` instances.
            """

            staged = _staging_dir(self.map)
            manifest_file = os.path.join(staged, MANIFEST_NAME)
            if not os.path.isfile(manifest_file):
                fail("no staged geometry for %s at %s (the offline stage runs from "
                     "`uv run elysium export map %s`)" % (self.map, manifest_file, self.map))
                return False
            if not self.load_corpus():
                return False
            self.env = self._read_env()

            start = time.time()
            self.geometry = _StagedGeometry(manifest_file)
            if self.geometry.error:
                fail(self.geometry.error)
                return False
            self.world_obj, self.blend = self.geometry.scene("world")
            self.sky_model, self.sky_blend = self.geometry.scene("sky")
            for stem in self.geometry.brush_stems().values():
                model, blend = self.geometry.scene(stem)
                self.brush_models[stem] = model
                self.brush_blend[stem] = blend

            # R5.4: the surfaces' materials are the staged table's; `world_mats` is what the shared
            # chunking/Nanite predicate reads (`.opaque`), and it is the same objects.
            self.v2_materials = {
                key: _V2Material(key, row) for key, row in self.geometry.materials.items()}
            self.world_mats = self.v2_materials
            legacy_rows = bl.read_mtl(
                os.path.join(self.dir, "%s.mtl" % self.map),
                corpus=self.corpus_materials, local=self.local_materials)
            self.decal_mats = {key: mat for key, mat in legacy_rows.items() if mat.decal}
            self.decals = bl.read_decals(os.path.join(self.dir, "%s.decals" % self.map))
            weather_path = os.path.join(self.dir, "%s.weather.json" % self.map)
            if os.path.isfile(weather_path):
                with open(weather_path, "r", encoding="utf-8") as handle:
                    self.weather = json.load(handle)
                if (self.weather.get("schema") != "elysium.map-weather"
                        or self.weather.get("version") != 1):
                    fail("unsupported weather sidecar: %s" % weather_path)
                    return False

            counts = self.geometry.counts
            log("v2 unit: %s (%s)" % (
                os.path.basename(self.geometry.unit), self.geometry.unit_sha256[:12]))
            log("v2 world: %d verts / %d tris / %d groups / %d decals (%.1fs)" % (
                len(self.world_obj.positions), self.world_obj.tri_count,
                len(self.world_obj.groups), len(self.decals), time.time() - start))
            log("v2 sky: %d tris / %d groups; brushes: %d models / %d tris; props: %d" % (
                counts["skyTriangles"], len(self.sky_model.groups),
                counts["brushModels"], counts["brushTriangles"],
                len(self.geometry.placements)))

            # Every material group the unit's faces resolved has to be in the staged table, or a
            # surface would bind the shared error material without anyone saying why. The offline
            # stage already failed the map if a unit was not staged; this is the cheap re-check
            # that the pair on disk is the pair that stage wrote.
            unknown = sorted(
                set(self.world_obj.groups) | set(self.sky_model.groups)
                | {key for model in self.brush_models.values() for key in model.groups}
            )
            unknown = [key for key in unknown if key not in self.v2_materials]
            if unknown:
                fail("%d unit material group(s) absent from the staged materials table: %s"
                     % (len(unknown), ", ".join(unknown[:8])))
                return False
            log("v2 materials: %d MI_ bound (%d PAKFILE-patched; %s)" % (
                len(self.v2_materials),
                sum(1 for mat in self.v2_materials.values() if mat.patched),
                ", ".join("%s %d" % item for item in sorted(
                    _count_by_master(self.v2_materials).items()))))
            self._load_v2_skins()
            self._load_placed_index()
            return True

        def _load_placed_index(self):
            """`npc/npc_index.json`, the catalogue that says which placed models have no static
            equivalent and must stand on an authored rest pose instead of a bind-pose mesh.

            Unchanged from the legacy lane: the catalogue, the rest-clip deal and the
            `AElysiumPlacedModelActor` it feeds are the placed-model bake's, not this task's. Only
            the placement records driving it come from the unit now.
            """

            path = os.path.join(HOST.OUT_ROOT, "npc", "npc_index.json")
            if not os.path.isfile(path):
                return
            with open(path, "r", encoding="utf-8") as handle:
                document = json.load(handle)
            self.placed_index_version = int(document.get("manifest_version", 0))
            self.placed_index = document.get("placed_models", {})

        def _load_v2_skins(self):
            """The R1 corpus skin table, read once per map.

            The V2 lane binds a placement's alternate skin from the same asset the running game
            binds it from (`/ElysiumBaked/Meshes/DA_ElysiumPropSkins`), so a baked placement and a
            runtime `skin` write can never disagree about what family 2 repaints.
            """

            asset = unreal.EditorAssetLibrary.load_asset(V2_SKIN_ASSET)
            if asset is None:
                log("v2 skins: %s absent, placements keep their authored set" % V2_SKIN_ASSET)
                return
            for model in asset.get_editor_property("models"):
                stem = str(model.get_editor_property("stem"))
                families = []
                for family in model.get_editor_property("families"):
                    overrides = {}
                    for override in family.get_editor_property("overrides"):
                        material = override.get_editor_property("material")
                        if material is not None:
                            overrides[str(override.get_editor_property("slot_name"))] = material
                    families.append(overrides)
                self.v2_skins[stem] = (
                    int(model.get_editor_property("family_count")), families)
            log("v2 skins: %d model(s) with alternate families" % len(self.v2_skins))

        # ----------------------------------------------------------------------- materials

        def _stage_wet_cubemaps(self):
            """No per-map wet instance exists to stamp a `SourceCube` into (R5.3/R5.4): wetness
            rides `MPC_ElysiumEnvironment` and the shared `MI_`. The legacy lane's cube package is
            pruned rather than authored."""
            pruned = bl.prune_package(self.cube_pkg, set(), self.prune_scope)
            self.tracker.pruned("textures", pruned)

        def _material_sets(self):
            """The world set is EMPTY on this lane, so `stage_materials` prunes the per-map
            `Materials` package instead of authoring it; the decal set is the legacy lane's,
            unchanged, until R7.6 gives decals a deferred-decal-domain V2 master."""
            return (({}, self.mat_pkg, self.shared_tex_pkg),
                    (self.decal_mats, self.decal_mat_pkg, self.shared_tex_pkg))

        def _shared_material_keys(self):
            """Nothing is resolved out of the legacy shared corpus package on this lane."""
            return {}

        def resolve_materials(self):
            """The decal MICs (legacy, `Bake.resolve_materials`) plus every staged `MI_`, loaded by
            asset path. A missing instance is a named failure, not a grey surface: the material
            lane imports map-scoped, and the map it did not import is exactly the map this would
            silently unbind."""
            Bake.resolve_materials(self)
            loaded = {}
            missing = []
            for key, mat in sorted(self.v2_materials.items()):
                asset = loaded.get(mat.asset)
                if asset is None and mat.asset not in loaded:
                    asset = unreal.EditorAssetLibrary.load_asset(mat.asset)
                    loaded[mat.asset] = asset
                if asset is None:
                    missing.append(mat.asset)
                    continue
                self.materials[(V2_MATERIAL_SCOPE, key)] = asset
            if missing:
                fail("%d staged material instance(s) are not imported "
                     "(run: uv run elysium import materials): %s"
                     % (len(missing), ", ".join(sorted(set(missing))[:8])))
                raise SystemExit(1)
            log("v2 materials: %d instance(s) loaded" % len(loaded))

        def material_for(self, key):
            """The imported `MI_` one surface binds, by its face group key."""
            if key not in self.v2_materials:
                return self.error_bind(key, self.map)
            return self.materials.get((V2_MATERIAL_SCOPE, key))

        # -------------------------------------------------------------------------- stages

        def stage_sky(self):
            """The 3D-skybox miniature, chunked exactly as the legacy `_sky.obj` stage chunked it.

            Authoring the sky *dome* (the six-face cube behind the miniature) is R5.2's; this stage
            still authors only the miniature's own geometry.
            """

            cell = HOST.CELL_CM * 4
            if not self.sky_model or not self.sky_model.groups:
                pruned = bl.prune_package_prefix(
                    self.mesh_pkg, "SM_Sky_", set(), self.prune_scope)
                self.tracker.pruned("sky", pruned)
                log("sky: the unit meshes no miniature, %d stale pruned" % pruned)
                return
            start = time.time()
            model = self.sky_model
            normals = bl.vertex_normals(model.positions, model.groups.values())
            # The legacy stage read no `_sky.blend` at all, so a miniature displacement's
            # `WorldVertexTransition` weight was dropped. The unit carries it per vertex like any
            # other, so it is passed through here; on the three converted maps every miniature
            # blend weight is 0, so the two lanes still author identical sky meshes.
            buckets = self._chunk_world(model, self.world_mats, self.sky_blend, cell)
            bl.ensure_dir(self.mesh_pkg)
            built = tris = dropped = 0
            wanted = set()
            for key in sorted(buckets.keys()):
                cx, cy, cz, opaque = key
                pivot = ((cx + 0.5) * cell, (cy + 0.5) * cell, (cz + 0.5) * cell)
                sections, names = self._sections(
                    model, normals, self.sky_blend, buckets[key], pivot)
                asset_path = "%s/SM_Sky_%s%d_%d_%d" % (
                    self.mesh_pkg, "" if opaque else "T_", cx, cy, cz)
                materials = [self.material_for(name) for name in names]
                kept, lost, _ = self._emit(
                    "sky", asset_path, sections, names, materials, nanite=opaque)
                tris += kept
                dropped += lost
                built += 1 if kept else 0
                if kept:
                    wanted.add(asset_path.rsplit("/", 1)[-1])
            pruned = bl.prune_package_prefix(self.mesh_pkg, "SM_Sky_", wanted, self.prune_scope)
            self.tracker.pruned("sky", pruned)
            log("sky: %d meshes / %d tris / %d dropped / %d stale pruned (%.1fs)" % (
                built, tris, dropped, pruned, time.time() - start))
            log("sky assets: %s" % self.tracker.summary("sky"))

        # -------------------------------------------------------------------------- level

        def _level_recipe(self):
            """The host's recipe plus this lane's identity and its one input file.

            Naming the lane is what makes a legacy-to-V2 switch re-author the level rather than
            reuse it: the assets have the same paths, so only the recipe can tell the two apart.
            """

            recipe = Bake._level_recipe(self)
            recipe["lane"] = self.lane
            recipe["unit_sha256"] = self.geometry.unit_sha256
            recipe["v2_materials"] = sorted({mat.asset for mat in self.v2_materials.values()})
            recipe["props"] = sorted(
                "%s/SM_%s" % (V2_MESH_PACKAGE, placement.stem)
                for placement in self.geometry.placements
            )
            recipe["prop_skins"] = sorted(
                "%s:%d" % (placement.stem, placement.skin)
                for placement in self.geometry.placements if placement.skin
            )
            recipe["placements"] = len(self.geometry.placements)
            # R6.3: every detail record and the two page distances are inputs to the level's
            # instanced components; the sway children are named so a re-authored child (a master
            # graph bump) re-authors the level that binds it.
            recipe["details"] = [row.as_row() for row in self.geometry.details]
            recipe["detail_models"] = sorted(
                "%s/SM_%s" % (V2_MESH_PACKAGE, model["stem"])
                for model in self.geometry.detail_models)
            recipe["detail_cull_cm"] = list(detail_cull())
            recipe["detail_actor_shape"] = DETAIL_ACTOR_SHAPE
            # R6.7: the miniature transform every sky-flagged row is placed through is the
            # manifest's own, so a moved `sky_camera` re-authors the level (the legacy `.sky`
            # digest in the host's recipe no longer describes what this lane reads).
            recipe["sky"] = {
                "scale": self.geometry.sky_scale,
                "origin": list(self.geometry.sky_origin),
                "ok": self.geometry.sky_ok,
            }
            # R6.1: every sprite row is an input to its actor; the children are named so a
            # re-authored child (a master graph bump) re-authors the level that binds it.
            recipe["sprites"] = [row.as_dict() for row in self.geometry.sprites]
            recipe["sprite_actor_shape"] = SPRITE_ACTOR_SHAPE
            # R7.3: every effects row and every tree it binds is an input to its actor; the
            # material children are bound by the runtime writer, so they are not level inputs.
            recipe["effects"] = self.geometry.effects
            recipe["particle_trees"] = self.geometry.particle_trees
            recipe["dustmotes"] = self.geometry.dustmotes
            recipe["steam"] = self.geometry.steam
            recipe["beams"] = self.geometry.beams
            recipe["effect_actor_shape"] = EFFECT_ACTOR_SHAPE
            # R5.5: a moved sample or an edited `CaptureRadius` re-authors the level, because the
            # capture's contents live in the level's own MapBuildData and nowhere else.
            recipe["capture_radius"] = HOST.capture_radius()
            recipe["captures"] = [
                [sample.index, list(sample.position), sample.sky]
                for sample in self.geometry.cubemaps
            ]
            # R5.6: the light actors carry FINAL values, so both their inputs -- every staged
            # row and every calibration field of the lighting page -- are in the recipe; an
            # edited page or a re-exported unit re-authors the level rather than reusing it
            # (the runtime no longer re-derives on a converted map).
            recipe["lights"] = [row.as_dict() for row in self.geometry.lights]
            recipe["lighting"] = lighting_calibration()
            # The rest clip a skeletal-rest placement is dealt is a function of its model path and
            # its lump index, so it belongs in the recipe: a re-deal has to re-author the level.
            recipe["rest_poses"] = dict(sorted(self.rest_labels.items()))
            return recipe

        def _read_sky(self):
            """R6.7: the miniature transform is the staged manifest's own `sky` block (the unit's
            `sky_camera` join), never the legacy `<map>.sky` sidecar -- `seam_map_map.md` ->
            "3D-skybox composition (R6.7)". Every sky-flagged row of every lane, and the sky
            chunks the host's `stage_level` places, go through this one answer."""

            return self.geometry.sky_scale, self.geometry.sky_origin

        def _place_props(self, actors, sky_scale=16.0, sky_origin=(0.0, 0.0, 0.0),
                         world_fog=None, sky_fog=None):
            """Every `staticProps[]` record as one `AStaticMeshActor` on the R1 corpus mesh.

            The placement record is the collision authority, not the model
            (`seam_map_model.md` -> "Import" -> "Collision"): `solid` decides whether this
            placement blocks, `skin` which family it is painted in, and the FADES flag whether it
            culls with distance. All three are placement facts the bake writes once, because a
            GAME_LUMP prop is not an entity and never changes any of them at runtime.
            """

            placed = skinned = sky_placed = faded = 0
            solid_count = skeletal_placed = 0
            cache = {}
            missing = {}
            for placement in self.geometry.placements:
                mesh = cache.get(placement.stem)
                if mesh is None and placement.stem not in cache:
                    mesh = unreal.EditorAssetLibrary.load_asset(
                        "%s/SM_%s" % (V2_MESH_PACKAGE, placement.stem))
                    cache[placement.stem] = mesh
                if not mesh:
                    missing.setdefault(placement.stem, 0)
                    missing[placement.stem] += 1
                    continue
                position = placement.position
                if placement.sky:
                    position = tuple(
                        sky_scale * (position[i] - sky_origin[i]) for i in range(3))
                # A miniature prop is never solid whatever its record says: it is scenery the
                # player can never reach, and at 16x it would wall the map off.
                solid = placement.solid_blocks and not placement.sky
                catalogue = PM.model_stem(placement.model_path)
                record = self.placed_index.get(catalogue)
                if self.placed_index_version >= 7 and record is None:
                    fail("placed model %s (%s) is absent from npc_index v%d"
                         % (placement.stem, catalogue, self.placed_index_version))
                    raise SystemExit(1)
                # A model with no static equivalent stands on an authored rest pose instead of its
                # bind-pose twin -- the placed-model lane's rule, unchanged; only the placement
                # record driving it comes from the unit now.
                use_skeletal = bool(record and not record.get("static_equivalent", False))
                actor = actors.spawn_actor_from_class(
                    unreal.ElysiumPlacedModelActor if use_skeletal else unreal.StaticMeshActor,
                    unreal.Vector(*position), unreal.Quat(*placement.rotation).rotator())
                if not actor:
                    continue
                if use_skeletal:
                    component = self._configure_rest(actor, placement, record, mesh, solid)
                    if component is None:
                        raise SystemExit(1)
                    skeletal_placed += 1
                else:
                    component = actor.static_mesh_component
                    component.set_static_mesh(mesh)
                    component.set_collision_profile_name(
                        HOST.PROFILE_PROP_SOLID if solid else HOST.PROFILE_PICK_ONLY)
                solid_count += 1 if solid else 0
                if placement.skin:
                    skinned += self._apply_v2_skin(component, mesh, placement)
                if placement.fades:
                    component.set_editor_property(
                        "ld_max_draw_distance", placement.fade_max_cm)
                    faded += 1
                fog = sky_fog if placement.sky else world_fog
                if fog:
                    HOST.set_fog(component, fog)
                if placement.sky:
                    actor.set_actor_scale3d(unreal.Vector(sky_scale, sky_scale, sky_scale))
                    component.set_editor_property("visible_in_ray_tracing", False)
                    component.set_cast_shadow(False)
                    sky_placed += 1
                actor.set_actor_label("Prop_%d_%s" % (placement.index, placement.stem))
                actor.tags = [HOST.TAG_SKY if placement.sky else HOST.TAG_PROP]
                actor.set_folder_path("Sky/Props" if placement.sky else "Props")
                placed += 1
            if missing:
                # A placement with no mesh is a hole in the map, and the R1 lane is map-scoped:
                # the map it did not stage is exactly the map this would silently empty.
                fail("%d placement(s) over %d model(s) have no %s asset "
                     "(run: uv run elysium import models --maps %s): %s"
                     % (sum(missing.values()), len(missing), V2_MESH_PACKAGE, self.map,
                        ", ".join(sorted(missing)[:8])))
                raise SystemExit(1)
            log("level: %d solid / %d skinned / %d distance-faded placements" % (
                solid_count, skinned, faded))
            if skeletal_placed:
                log("level: %d placements held on authored skeletal rest poses" % skeletal_placed)
            self._place_details(actors, sky_scale, sky_origin, world_fog, sky_fog)
            self._place_sprites(actors, sky_scale, sky_origin)
            self._place_effects(actors, sky_scale, sky_origin, world_fog, sky_fog)
            return placed, sky_placed

        # ------------------------------------------------------------- detail props (R6.3)

        def _place_details(self, actors, sky_scale, sky_origin, world_fog, sky_fog):
            """Every staged `details.records[]` row as one instance of an
            `AElysiumDetailPropActor`'s instanced component -- one actor per `(model, sky)` group,
            instances in lump order (`seam_map_map.md` -> "Detail props (R6.3)").

            The record is the whole authority: transform from the placements-scene node, no
            collision (the lump is client-only in VtMB), no shadow (VRAD never lit by one), the
            page's `cl_detaildist`/`cl_detailfade` as the instance cull range, `swayAmount / 255`
            as custom data float 0, and the scene-fog stamp every prop takes. Returns
            `(instances, components)`.
            """

            groups = detail_instance_rows(self.geometry.details, sky_scale, sky_origin)
            if not groups:
                log("details: the unit places no detail props")
                return 0, 0
            start_cm, end_cm = detail_cull()
            cache = {}
            missing = {}
            instances = components = swaying = sky_components = 0
            for (stem, sky), rows in groups.items():
                mesh = cache.get(stem)
                if mesh is None and stem not in cache:
                    mesh = unreal.EditorAssetLibrary.load_asset(
                        "%s/SM_%s" % (V2_MESH_PACKAGE, stem))
                    cache[stem] = mesh
                if not mesh:
                    missing[stem] = missing.get(stem, 0) + len(rows)
                    continue
                actor = actors.spawn_actor_from_class(
                    unreal.ElysiumDetailPropActor, unreal.Vector(0.0, 0.0, 0.0))
                if not actor:
                    fail("details: spawn failed for %s" % stem)
                    raise SystemExit(1)
                component = actor.instances
                component.set_static_mesh(mesh)
                for slot, child in self._detail_sway_materials(mesh):
                    component.set_material(slot, child)
                component.set_num_custom_data_floats(1)
                component.set_cull_distances(int(round(start_cm)), int(round(end_cm)))
                transforms = []
                for position, rotation, scale, _sway in rows:
                    transform = unreal.Transform()
                    transform.translation = unreal.Vector(*position)
                    transform.rotation = unreal.Quat(*rotation)
                    transform.scale3d = unreal.Vector(scale, scale, scale)
                    transforms.append(transform)
                component.add_instances(transforms, False, True)
                for index, (_position, _rotation, _scale, sway) in enumerate(rows):
                    if sway:
                        component.set_custom_data_value(index, 0, sway, False)
                        swaying += 1
                fog = sky_fog if sky else world_fog
                if fog:
                    HOST.set_fog(component, fog)
                if sky:
                    component.set_editor_property("visible_in_ray_tracing", False)
                    sky_components += 1
                actor.set_editor_property("model_stem", stem)
                actor.set_actor_label("Detail_%s%s" % (stem, "_sky" if sky else ""))
                actor.tags = list(detail_actor_tags(stem, sky))
                actor.set_folder_path("Sky/Details" if sky else "Details")
                instances += len(rows)
                components += 1
            if missing:
                fail("%d detail record(s) over %d model(s) have no %s asset "
                     "(run: uv run elysium import models --maps %s): %s"
                     % (sum(missing.values()), len(missing), V2_MESH_PACKAGE, self.map,
                        ", ".join(sorted(missing)[:8])))
                raise SystemExit(1)
            log("details: %d instances over %d component(s) (%d in the 3D skybox, %d swaying); "
                "cull %.0f..%.0f cm; %d sway material(s)" % (
                    instances, components, sky_components, swaying, start_cm, end_cm,
                    len(self.detail_materials)))
            return instances, components

        def _detail_sway_materials(self, mesh):
            """`[(slot index, MI_DetailSway_* child)]` for every slot of a detail model's mesh:
            the imported `MI_` the slot already binds, re-parented once through a child whose only
            own value is `UseDetailSway = true`, authored under `V2_DETAIL_MATERIAL_PACKAGE` and
            recipe-stamped on the parent path and the master's graph version. A slot whose master
            has no such switch is a named failure -- the weed would stand still with nobody
            saying why."""

            out = []
            for slot, entry in enumerate(mesh.get_editor_property("static_materials")):
                parent = entry.get_editor_property("material_interface")
                if parent is None:
                    continue
                parent_path = parent.get_path_name().split(".", 1)[0]
                child_path = self.detail_materials.get(parent_path)
                if child_path is None:
                    child_path = self._author_detail_sway_material(parent, parent_path)
                    self.detail_materials[parent_path] = child_path
                child = unreal.EditorAssetLibrary.load_asset(child_path)
                if not child:
                    fail("details: sway material %s did not load" % child_path)
                    raise SystemExit(1)
                out.append((slot, child))
            return out

        def _author_detail_sway_material(self, parent, parent_path):
            master = parent.get_base_material()
            master_path = master.get_path_name().split(".", 1)[0] if master else ""
            switches = []
            if master:
                switches = [str(name) for name in
                            unreal.MaterialEditingLibrary.get_static_switch_parameter_names(master)]
            if DETAIL_SWAY_SWITCH not in switches:
                fail("details: %s is on %s, which exposes no %s switch (the detail sway term "
                     "lives on M_V2_Lit / M_V2_LitTranslucent / M_V2_Unlit)"
                     % (parent_path, master_path or "no master", DETAIL_SWAY_SWITCH))
                raise SystemExit(1)
            name = "MI_DetailSway_" + bl.safe_name(
                parent_path.replace(mounts.BAKED + "/Materials/", "").replace("/MI_", "/"))
            child_path = "%s/%s" % (V2_DETAIL_MATERIAL_PACKAGE, name)
            recipe = {
                "parent": parent_path,
                "master": master_path,
                "master_recipe": bl.stored_recipe(master_path) if master_path else "",
                "switch": DETAIL_SWAY_SWITCH,
            }
            if self.tracker.register("materials", child_path, recipe,
                                     expected_class="MaterialInstanceConstant"):
                child = bl.make_material_instance(name, V2_DETAIL_MATERIAL_PACKAGE, parent)
                if not child:
                    fail("details: could not author %s" % child_path)
                    raise SystemExit(1)
                bl.set_static_switch_param(child, DETAIL_SWAY_SWITCH, True)
                unreal.MaterialEditingLibrary.update_material_instance(child)
                self.tracker.stamp(child, child_path)
                if not bl.save(child_path):
                    fail("details: save failed: %s" % child_path)
                    raise SystemExit(1)
                self.tracker.built("materials")
            return child_path

        # ------------------------------------------------------------------ sprites (R6.1)

        def _place_sprites(self, actors, sky_scale, sky_origin):
            """Every staged `sprites[]` row as one `AElysiumSpriteActor` (`seam_map_map.md` ->
            "Sprites (R6.1)"): the imported `MI_` through its per-blend child, the size in Source
            units, the colour, the mode, the orientation, the spawn-hidden rule and the entity
            index tag. Returns `(placed, glow, hidden)`."""

            rows = self.geometry.sprites
            if not rows:
                log("sprites: the map places no env_sprite")
                return 0, 0, 0
            placed = glow = hidden = sky_placed = 0
            for row in rows:
                values = sprite_actor_values(row, sky_scale, sky_origin)
                material = self._sprite_material(row.asset, row.blend)
                actor = actors.spawn_actor_from_class(
                    unreal.ElysiumSpriteActor, unreal.Vector(*values["position"]))
                if not actor:
                    fail("sprites: spawn failed for env_sprite %d" % row.index)
                    raise SystemExit(1)
                component = actor.sprite
                component.set_editor_property("material", material)
                component.set_editor_property(
                    "size_inches", unreal.Vector2D(*values["size_inches"]))
                component.set_editor_property("render_mode", row.mode)
                component.set_editor_property("render_fx", row.fx)
                # `unreal.Color` is FColor's own BGRA layout: positional arguments would swap
                # red and blue, so the channels are named.
                component.set_editor_property(
                    "color", unreal.Color(b=row.color[2], g=row.color[1], r=row.color[0],
                                          a=row.alpha))
                component.set_editor_property("upright", row.upright)
                actor.set_editor_property("entity_index", row.index)
                if values["scale"] != 1.0:
                    actor.set_actor_scale3d(unreal.Vector(*([values["scale"]] * 3)))
                if row.hidden:
                    actor.set_actor_hidden_in_game(True)
                    hidden += 1
                actor.set_actor_label(values["label"])
                actor.tags = list(values["tags"])
                actor.set_folder_path(values["folder"])
                placed += 1
                glow += 1 if row.glow else 0
                sky_placed += 1 if row.sky else 0
            log("sprites: %d placed (%d glow, %d hidden at spawn, %d in the 3D skybox); "
                "%d material child(ren)" % (
                    placed, glow, hidden, sky_placed, len(self.sprite_materials)))
            return placed, glow, hidden

        def _sprite_material(self, parent_path, blend):
            """The `MI_Sprite_<material>_<blend>` child of one imported sprite `MI_`: the blend
            override the entity's mode selects and the master's two vertex switches on, authored
            once per `(parent, blend)` under `V2_SPRITE_MATERIAL_PACKAGE`, recipe-stamped on the
            parent path, the parent's own recipe, the blend and the switches. A parent whose
            master exposes neither switch is a named failure -- the tint would silently drop."""

            key = (parent_path, blend)
            child_path = self.sprite_materials.get(key)
            if child_path is None:
                parent = unreal.EditorAssetLibrary.load_asset(parent_path)
                if not parent:
                    fail("sprites: %s is not imported (run: uv run elysium import materials)"
                         % parent_path)
                    raise SystemExit(1)
                child_path = self._author_sprite_material(parent, parent_path, blend)
                self.sprite_materials[key] = child_path
            child = unreal.EditorAssetLibrary.load_asset(child_path)
            if not child:
                fail("sprites: material %s did not load" % child_path)
                raise SystemExit(1)
            return child

        def _author_sprite_material(self, parent, parent_path, blend):
            master = parent.get_base_material()
            master_path = master.get_path_name().split(".", 1)[0] if master else ""
            switches = []
            if master:
                switches = [str(name) for name in
                            unreal.MaterialEditingLibrary.get_static_switch_parameter_names(master)]
            missing = [name for name in SPRITE_SWITCHES if name not in switches]
            if missing:
                fail("sprites: %s is on %s, which exposes no %s switch (a sprite draws through "
                     "M_V2_Sprite's vertex colour)" % (
                         parent_path, master_path or "no master", "/".join(missing)))
                raise SystemExit(1)
            member = SPRITE_BLEND_MEMBERS.get(blend)
            if member is None:
                fail("sprites: blend %r names no BlendMode" % (blend,))
                raise SystemExit(1)
            name = sprite_child_name(parent_path, blend)
            child_path = "%s/%s" % (V2_SPRITE_MATERIAL_PACKAGE, name)
            recipe = {
                "parent": parent_path,
                "master": master_path,
                "master_recipe": bl.stored_recipe(master_path) if master_path else "",
                "parent_recipe": bl.stored_recipe(parent_path),
                "blend": blend,
                "switches": list(SPRITE_SWITCHES),
            }
            if self.tracker.register("materials", child_path, recipe,
                                     expected_class="MaterialInstanceConstant"):
                child = bl.make_material_instance(name, V2_SPRITE_MATERIAL_PACKAGE, parent)
                if not child:
                    fail("sprites: could not author %s" % child_path)
                    raise SystemExit(1)
                for switch in SPRITE_SWITCHES:
                    bl.set_static_switch_param(child, switch, True)
                overrides = child.get_editor_property("base_property_overrides")
                overrides.set_editor_property("override_blend_mode", True)
                overrides.set_editor_property("blend_mode", getattr(unreal.BlendMode, member))
                child.set_editor_property("base_property_overrides", overrides)
                unreal.MaterialEditingLibrary.update_material_instance(child)
                self.tracker.stamp(child, child_path)
                if not bl.save(child_path):
                    fail("sprites: save failed: %s" % child_path)
                    raise SystemExit(1)
                self.tracker.built("materials")
            return child_path

        # ------------------------------------------------------------------ effects (R7.3)

        def _place_effects(self, actors, sky_scale, sky_origin, world_fog, sky_fog):
            """Every staged `effects[]` row whose root resolved as one `AElysiumEffectActor`, and
            every `dustmotes[]` / `steam[]` / `beams[]` row as its family actor
            (`seam_map_map.md` -> "Import -- effects (R7.3)"). Returns the placed count."""

            geometry = self.geometry
            tables = (
                ("effects", [row for row in geometry.effects if row.get("particle")]),
                ("dustmotes", geometry.dustmotes), ("steam", geometry.steam),
                ("beams", geometry.beams),
            )
            if not any(rows for _name, rows in tables):
                log("effects: the map places no effects entity")
                return 0
            skipped = [row["index"] for row in geometry.effects if not row.get("particle")]
            placed = sky_placed = 0
            per_table = {}
            for table, rows in tables:
                class_name = EFFECT_ACTOR_CLASSES[table]
                actor_class = getattr(unreal, class_name, None)
                if rows and actor_class is None:
                    fail("effects: unreal.%s is not registered (build Source/ElysiumUE first)"
                         % class_name)
                    raise SystemExit(1)
                for row in rows:
                    values = effect_actor_values(row, table, sky_scale, sky_origin)
                    actor = actors.spawn_actor_from_class(
                        actor_class, unreal.Vector(*values["position"]),
                        unreal.Quat(*values["rotation"]).rotator())
                    if not actor:
                        fail("effects: spawn failed for %s %d" % (table, row["index"]))
                        raise SystemExit(1)
                    if table == "effects":
                        self._write_effect_row(actor, row)
                        self._write_tree(actor, geometry.particle_trees.get(row["particle"]))
                    else:
                        self._write_family_row(actor, row)
                    component = actor.get_editor_property("root_component")
                    fog = sky_fog if row.get("sky") else world_fog
                    if fog and component is not None:
                        try:
                            HOST.set_fog(component, fog)
                        except Exception:  # noqa: BLE001 -- a non-primitive root has no slots
                            pass
                    if values["scale"] != 1.0:
                        actor.set_actor_scale3d(unreal.Vector(*([values["scale"]] * 3)))
                    if row.get("start_hidden"):
                        actor.set_actor_hidden_in_game(True)
                    actor.set_actor_label(values["label"])
                    actor.tags = list(values["tags"])
                    actor.set_folder_path(values["folder"])
                    placed += 1
                    sky_placed += 1 if row.get("sky") else 0
                    per_table[table] = per_table.get(table, 0) + 1
            log("effects: %d placed (%s; %d in the 3D skybox); %d unresolved root(s) placed no "
                "actor%s" % (placed, ", ".join("%d %s" % (n, t) for t, n in sorted(per_table.items())),
                             sky_placed, len(skipped),
                             (": entities %s" % skipped[:8]) if skipped else ""))
            return placed

        def _set(self, target, name, value, what):
            """One property write that fails naming the property the class does not expose."""

            try:
                target.set_editor_property(name, value)
            except Exception as error:  # noqa: BLE001 -- the mapping table is the contract
                fail("effects: %s exposes no property %r for %s (%s)" % (
                    target.get_class().get_name() if hasattr(target, "get_class") else target,
                    name, what, error))
                raise SystemExit(1)

        def _write_effect_row(self, actor, row):
            for field, prop in EFFECT_ROW_FIELDS:
                value = row.get(field)
                if field == "bounds_cm":
                    box = unreal.Box()
                    if value:
                        box = unreal.Box(min=unreal.Vector(*value["min"]),
                                         max=unreal.Vector(*value["max"]), is_valid=1)
                    value = box
                elif field == "angles_deg":
                    value = unreal.Vector(*value)
                elif field == "particle":
                    value = str(value or "")
                elif value is None:
                    value = "" if field in ("parentname", "bone", "targetname") else 0
                self._set(actor, prop, value, "effects[%d].%s" % (row["index"], field))

        def _write_family_row(self, actor, row):
            self._set(actor, "entity_index", int(row["index"]), "row.index")
            for field, value in row.items():
                if field in FAMILY_ROW_SKIP:
                    continue
                if field == "color":
                    value = unreal.LinearColor(value[0], value[1], value[2],
                                               float(row.get("alpha", 1.0)))
                elif field == "bounds_cm":
                    box = unreal.Box()
                    if value:
                        box = unreal.Box(min=unreal.Vector(*value["min"]),
                                         max=unreal.Vector(*value["max"]), is_valid=1)
                    value = box
                elif value is None:
                    value = ""
                self._set(actor, field, value, "%s[%d].%s" % (
                    row.get("classname", "row"), row["index"], field))

        def _write_tree(self, actor, tree):
            if not tree:
                return
            nodes = []
            for node in tree["nodes"]:
                struct = unreal.ElysiumParticleNode()
                for field, prop in EFFECT_NODE_FIELDS:
                    value = node.get(field)
                    if isinstance(value, list) and value and isinstance(value[0], list):
                        value = [unreal.ElysiumRampKey(t=k[0], lo=k[1], hi=k[2]) for k in value]
                    elif value is None:
                        value = -1 if field == "parent" else ""
                    self._set(struct, prop, value, "node[%d].%s" % (node["index"], field))
                spawn = node.get("spawn") or {}
                self._set(struct, "has_spawn", bool(node.get("spawn")), "node.spawn")
                for field, prop in EFFECT_SPAWN_FIELDS:
                    if field not in spawn:
                        continue
                    value = spawn[field]
                    if isinstance(value, list) and value and isinstance(value[0], list):
                        value = [unreal.ElysiumRampKey(t=k[0], lo=k[1], hi=k[2]) for k in value]
                    self._set(struct, prop, value, "node[%d].spawn.%s" % (node["index"], field))
                for key in ("sprite", "normal"):
                    image = node.get(key) or {}
                    self._set(struct, key, str(image.get("texture") or ""), "node." + key)
                    aspect = image.get("aspect") or [0.5, 0.5]
                    self._set(struct, key + "_aspect", unreal.Vector2D(aspect[0], aspect[1]),
                              "node." + key + ".aspect")
                collide = node.get("collide")
                self._set(struct, "collide", bool(collide), "node.collide")
                if collide:
                    for field, prop in EFFECT_COLLIDE_FIELDS:
                        self._set(struct, prop, collide[field], "node.collide." + field)
                nodes.append(struct)
            tree_struct = unreal.ElysiumParticleTree()
            self._set(tree_struct, "root", tree["root"], "tree.root")
            self._set(tree_struct, "name", tree["name"], "tree.name")
            self._set(tree_struct, "nodes", nodes, "tree.nodes")
            self._set(tree_struct, "leaf_count", int(tree["stats"]["leafCount"]), "tree.leafCount")
            self._set(tree_struct, "max_depth", int(tree["stats"]["depth"]), "tree.depth")
            self._set(actor, "tree", tree_struct, "actor.tree")

        # ------------------------------------------------------------------ lights (R5.6)

        def _place_lights(self, actors, sky_scale=16.0, sky_origin=(0.0, 0.0, 0.0)):
            """One light actor per staged `lights[]` row, carrying its FINAL values
            (`seam_map_map_lighting.md` -> "## Import" -> "Lights final (R5.6)").

            `derive_light` is `UElysiumLightRig::ApplyToSource` restated, fed from the
            `UElysiumLightingSettings` page and the surfaces page's `LightSpecularScale`; the
            runtime rig on this map snapshots the actor and derives nothing. A styled source is
            placed at its unanimated base intensity (the rig animates it per frame off the
            `elysium.style` tag). A row inside the 3D-skybox miniature takes the sky transform
            for position and reach, the owner call of 2026-07-26. Returns
            `(placed, sky_placed, sky_ambient)` in the host's shape: `sky_ambient` is the first
            type-5 row's `(colour, magnitude)` (first wins, by lump order -- VRAD resolves the
            sky ambient once, globally, and the engine's multi-`light_environment` rule is
            first-wins too, RE-A3/RE-A5), which `_place_sky` joins with the baked cube (R5.2)."""

            calibration = lighting_calibration()
            placed = sky_placed = 0
            sky_ambient = None
            for row in self.geometry.lights:
                mag = max(row.rgb)
                if mag <= 0.0:
                    continue
                if row.type == 5:
                    if sky_ambient is None:
                        sky_ambient = (unreal.LinearColor(
                            row.rgb[0] / mag, row.rgb[1] / mag, row.rgb[2] / mag, 1.0), mag)
                    continue
                if row.type not in LIGHT_KIND_LABELS:
                    continue
                final = derive_light(row.as_dict(), calibration, sky_scale, sky_origin)
                origin = unreal.Vector(*final["position"])
                direction = unreal.Vector(*row.direction)
                if row.type in (0, 1):
                    actor = actors.spawn_actor_from_class(unreal.PointLight, origin)
                    component = actor.point_light_component if actor else None
                elif row.type == 2:
                    actor = actors.spawn_actor_from_class(
                        unreal.SpotLight, origin, HOST._dir_rotator(direction))
                    component = actor.spot_light_component if actor else None
                else:
                    actor = actors.spawn_actor_from_class(
                        unreal.DirectionalLight, origin, HOST._dir_rotator(direction))
                    # ADirectionalLight exposes only ALight's generic component property.
                    component = actor.light_component if actor else None
                if not actor or not component:
                    fail("light %d: spawn failed at %s" % (row.index, final["position"]))
                    raise SystemExit(1)
                # Movable FIRST: radius and cone writes are dropped in silence on a Stationary
                # light (`bake_map._make_movable`).
                HOST._make_movable(component)
                component.set_light_color(unreal.LinearColor(*final["color"], 1.0))
                component.set_intensity(final["intensity"])
                component.set_cast_shadows(final["cast_shadows"])
                component.set_editor_property("specular_scale", final["specular_scale"])
                component.set_editor_property(
                    "indirect_lighting_intensity", final["indirect_lighting_intensity"])
                component.set_editor_property(
                    "volumetric_scattering_intensity", final["volumetric_scattering_intensity"])
                if row.type in (0, 1, 2):
                    component.set_attenuation_radius(final["reach_cm"])
                    # VtMB light is ~flat within its authored radius, so gentle-exponent
                    # falloff, not inverse-square (docs/architecture/rendering-perf.md).
                    component.set_editor_property("use_inverse_squared_falloff", False)
                    component.set_editor_property(
                        "light_falloff_exponent", final["falloff_exponent"])
                    # Elysium's hundreds of movable local lights depend on fixed-cost RT
                    # MegaLights: a renderer contract the rig used to restate every load.
                    component.set_editor_property("allow_mega_lights", True)
                    component.set_editor_property(
                        "mega_lights_shadow_method",
                        unreal.MegaLightsShadowMethod.RAY_TRACING)
                if row.type == 2:
                    component.set_outer_cone_angle(final["outer_cone_deg"])
                    component.set_inner_cone_angle(final["inner_cone_deg"])
                if row.type == 3:
                    component.set_editor_property(
                        "light_source_angle", final["sun_source_angle_deg"])
                    component.set_editor_property(
                        "light_source_soft_angle", final["sun_soft_source_angle_deg"])
                actor.set_actor_label("Light_%d_%s%s" % (
                    row.index, LIGHT_KIND_LABELS[row.type], "_sky" if row.sky else ""))
                # The lump-15 ordinal is the R4.3 calibration asset's key and the type/style
                # tags are the two facts the slim rig still needs (`ElysiumBakedTags`).
                actor.tags = [HOST.TAG_LIGHT, "elysium.src=%d" % row.index,
                              "elysium.type=%d" % row.type, "elysium.style=%d" % row.style]
                actor.set_folder_path("Sky/Lights" if row.sky else "Lights")
                placed += 1
                sky_placed += 1 if row.sky else 0
            log("lights: %d placed (%d in the 3D skybox) with final values; ceiling %.1f%s" % (
                placed, sky_placed,
                calibration["ExtendedMaxBrightness"]
                if calibration["bUseExtendedBrightnessCeiling"]
                else calibration["MaxBrightness"],
                " (extended)" if calibration["bUseExtendedBrightnessCeiling"] else ""))
            return placed, sky_placed, sky_ambient

        # ---------------------------------------------------------------- captures (R5.5)

        def _place_captures(self, actors, sky_scale=16.0, sky_origin=(0.0, 0.0, 0.0)):
            """One `ASphereReflectionCapture` per `cubemaps[]` sample, at the sample's own
            position and at the settings page's `CaptureRadius` (`seam_map_map.md` -> "Import --
            reflection captures (R5.5)"). A sample inside the 3D-skybox miniature takes the sky
            transform like a miniature light: position scaled about the sky origin, radius scaled
            by the same factor. The VtMB probe the sample names is provenance; the capture renders
            the baked scene."""

            radius = HOST.capture_radius()
            placed = sky_placed = 0
            for sample in self.geometry.cubemaps:
                position, influence = capture_placement(
                    sample.position, sample.sky, sky_scale, sky_origin, radius)
                actor = actors.spawn_actor_from_class(
                    unreal.SphereReflectionCapture, unreal.Vector(*position))
                if not actor:
                    fail("capture %d: spawn failed at %s" % (sample.index, position))
                    raise SystemExit(1)
                component = actor.get_editor_property("capture_component")
                component.set_editor_property("influence_radius", influence)
                actor.set_actor_label("Capture_%d%s" % (
                    sample.index, "_sky" if sample.sky else ""))
                actor.tags = [HOST.TAG_CAPTURE, "elysium.src=%d" % sample.index]
                actor.set_folder_path("Sky/Captures" if sample.sky else "Captures")
                placed += 1
                sky_placed += sample.sky
            log("captures: %d placed (%d in the 3D skybox) at %.0f cm" % (
                placed, sky_placed, radius))
            return placed

        def _build_captures(self, world, placed):
            """`UElysiumMapBakeLibrary.build_reflection_captures`: the editor's own Build ->
            Reflection Captures over this world, under the commandlet's
            `-AllowCommandletRendering`. Returns the built count and fails the map when it is short
            of `placed`: a capture that placed but never rendered is a black probe the game would
            read as "no reflection here" with nobody saying why."""

            start = time.time()
            built = int(unreal.ElysiumMapBakeLibrary.build_reflection_captures(world))
            if built < 0:
                fail("captures: the build could not run (no editor, or no world)")
                raise SystemExit(1)
            if built != placed:
                fail("captures: %d placed but %d carry MapBuildData after the build"
                     % (placed, built))
                raise SystemExit(1)
            log("captures: %d built into MapBuildData (%.1fs)" % (built, time.time() - start))
            return built

        def _configure_rest(self, actor, placement, record, mesh, solid):
            """One placement whose model has no static equivalent: the skeletal body, the rest clip
            the catalogue deals it, and the static mesh as its collision proxy.

            Returns the visual component, or None after a logged failure -- a missing rest asset
            means the placed-model bake has not run, and continuing would stand a bind-posed body in
            the level with nobody saying why.
            """

            model_path = record.get("model", placement.model_path)
            catalogue = PM.model_stem(model_path)
            rest = PM.select_rest_label(model_path, record, placement.index)
            skeletal = unreal.EditorAssetLibrary.load_asset(
                "%s/Props/%s/SK_%s" % (mounts.BAKED, catalogue, catalogue))
            animation = unreal.EditorAssetLibrary.load_asset(
                "%s/Props/%s/A_%s" % (mounts.BAKED, catalogue, bl.safe_name(rest)))
            if not skeletal or not animation or not rest:
                fail("placed model %s has no baked rest asset '%s' "
                     "(run: uv run elysium export placed-model %s)" % (catalogue, rest, catalogue))
                return None
            if not actor.configure_rest(skeletal, animation, mesh, solid):
                fail("placed model %s refused rest configuration" % catalogue)
                return None
            self.rest_labels[placement.index] = rest
            actor.collision_proxy.set_collision_profile_name(
                HOST.PROFILE_PROP_SOLID if solid else HOST.PROFILE_PICK_ONLY)
            return actor.skeletal_visual

        def _apply_v2_skin(self, component, mesh, placement):
            """Repaint the slots this placement's skin family changes.

            Mirrors `UElysiumPropSkinSet::Find`: an out-of-range family clamps to the model's last
            family (VtMB's `skin` is an unclamped int write and the engine clamps rather than
            falling back to 0), family 0 and any family that repaints nothing resolve to no row.
            """

            row = self.v2_skins.get(placement.stem)
            if row is None:
                return 0
            family_count, families = row
            family = placement.skin
            if family_count > 0 and family >= family_count:
                family = family_count - 1
            if family <= 0 or family >= len(families) or not families[family]:
                return 0
            slots = [str(entry.get_editor_property("material_slot_name"))
                     for entry in mesh.get_editor_property("static_materials")]
            applied = 0
            for slot, material in sorted(families[family].items()):
                if slot not in slots:
                    continue
                component.set_material(slots.index(slot), material)
                applied += 1
            return 1 if applied else 0

    return MapBakeV2


def capture_placement(position, sky, sky_scale, sky_origin, radius):
    """`(position, influence_radius)` for one capture: a world sample is placed as-is at
    `radius`; a 3D-skybox sample takes the miniature's transform -- `scale * (p - origin)` and
    `radius * scale` -- the same rule a miniature light's position and reach take."""

    if not sky:
        return tuple(float(v) for v in position), float(radius)
    scaled = tuple(float(sky_scale) * (float(position[i]) - float(sky_origin[i]))
                   for i in range(3))
    return scaled, float(radius) * float(sky_scale)


def detail_instance_rows(details, sky_scale=16.0, sky_origin=(0.0, 0.0, 0.0)):
    """The staged `details.records[]` rows grouped into instanced components (R6.3): an ordered
    `{(stem, sky): [(position, rotation, scale, sway), ...]}`, groups in first-record order and
    rows in lump order inside each group, so instance `k` of a component is the model's `k`-th
    record. `sway` is `swayAmount / 255` (0.0 exactly for an unswayed record); a 3D-skybox record
    takes the miniature transform a static prop takes -- `scale * (p - origin)` and uniform scale
    `scale`. Pure, no `unreal`, so a pytest pins it."""

    groups = {}
    for detail in details:
        position = tuple(float(v) for v in detail.position)
        scale = 1.0
        if detail.sky:
            position = tuple(
                float(sky_scale) * (position[i] - float(sky_origin[i])) for i in range(3))
            scale = float(sky_scale)
        sway = float(detail.sway) / DETAIL_SWAY_FULL if detail.sway else 0.0
        groups.setdefault((detail.stem, bool(detail.sky)), []).append(
            (position, tuple(float(v) for v in detail.rotation), scale, sway))
    return groups


def sprite_child_name(parent_path, blend):
    """`MI_Sprite_<material path>_<blend>`: the child's asset name for one imported sprite `MI_`
    and one blend, the same fold the detail sway children use."""

    stem = parent_path.replace(mounts.BAKED + "/Materials/", "").replace("/MI_", "/")
    return "MI_Sprite_" + bl.safe_name(stem) + "_" + blend


def sprite_actor_values(row, sky_scale=16.0, sky_origin=(0.0, 0.0, 0.0)):
    """The placement facts `_place_sprites` writes for one staged row (R6.1), pure so a pytest pins
    them: the position (a 3D-skybox row takes the miniature transform a prop takes), the actor's
    uniform scale (the miniature's, so a fixed-size card scales with the miniature), the size in
    Source units (`scale x texture`), the label, the folder and the tags."""

    position = tuple(float(v) for v in row.position)
    scale = 1.0
    if row.sky:
        position = tuple(
            float(sky_scale) * (position[i] - float(sky_origin[i])) for i in range(3))
        scale = float(sky_scale)
    stem = row.material.rsplit("/", 1)[-1]
    return {
        "position": position,
        "scale": scale,
        "size_inches": (row.scale * row.width, row.scale * row.height),
        "label": "Sprite_%d_%s%s" % (row.index, stem, "_sky" if row.sky else ""),
        "folder": "Sky/Sprites" if row.sky else "Sprites",
        "tags": (TAG_SPRITE, "%s%d" % (TAG_ENTITY_PREFIX, row.index))
                + ((TAG_SKY,) if row.sky else ()),
    }


def effect_actor_values(row, table, sky_scale=16.0, sky_origin=(0.0, 0.0, 0.0)):
    """The placement facts `_place_effects` writes for one staged row (R7.3), pure so a pytest pins
    them: the position and rotation (a 3D-skybox row takes the miniature transform a sprite
    takes), the actor's uniform scale, the label, the folder and the tags."""

    position = tuple(float(v) for v in (row.get("origin_cm") or (0.0, 0.0, 0.0)))
    rotation = tuple(float(v) for v in (row.get("rotation") or (0.0, 0.0, 0.0, 1.0)))
    scale = 1.0
    sky = bool(row.get("sky"))
    if sky:
        position = tuple(
            float(sky_scale) * (position[i] - float(sky_origin[i])) for i in range(3))
        scale = float(sky_scale)
    if table == "effects":
        stem = str(row.get("particle") or "").rsplit(":", 1)[-1].replace(" ", "_")
        label = "Effect_%d_%s" % (row["index"], stem)
    else:
        label = "%s_%d" % ({"dustmotes": "Dust", "steam": "Steam", "beams": "Beam"}[table],
                           row["index"])
    return {
        "position": position,
        "rotation": rotation,
        "scale": scale,
        "label": label + ("_sky" if sky else ""),
        "folder": "Sky/Effects" if sky else "Effects",
        "tags": (TAG_EFFECT, "%s%d" % (TAG_ENTITY_PREFIX, row["index"]))
                + ((TAG_SKY,) if sky else ()),
    }


def detail_actor_tags(stem, sky):
    """The tags `_place_details` writes on one component actor (R6.3 + R6.7): the class tag and
    the model tag the runtime buckets by, plus the miniature's scope marker for a sky group --
    so `ApplySceneFog` stamps it with the `sky_camera`'s set and `ToggleSkybox` hides it with
    the miniature. Pure, so a pytest pins it."""

    tags = (TAG_DETAIL, "elysium.model=%s" % stem)
    return tags + ((TAG_SKY,) if sky else ())


def miniature_transform(sky_block):
    """`(scale, origin)` from the staged manifest's `sky` block (R6.7): the one transform every
    sky-flagged row is placed through, `world(v) = scale * (v - origin)`. The block is always
    written (`map_geometry.stage_map`); a map with no `sky_camera` carries Source's default
    scale 16 and the world origin, and flags nothing sky, so the numbers are never applied."""

    return (float(sky_block["scale"]),
            tuple(float(value) for value in sky_block["origin"]))


#: The `UElysiumModelSettings` fields the detail cull range reads (R6.3), C++ name -> Python name.
DETAIL_SETTINGS_FIELDS = (
    ("DetailDrawDistanceCm", "detail_draw_distance_cm"),
    ("DetailFadeRangeCm", "detail_fade_range_cm"),
)


def detail_cull():
    """`(start_cm, end_cm)` for every detail component: `end` is the Models page's
    `DetailDrawDistanceCm` (VtMB `cl_detaildist` 600 in), `start` is `end - DetailFadeRangeCm`
    (`cl_detailfade` 300 in), floored at 0. Read off the CDO -- the tracked ini -- never a
    literal."""

    page = unreal.get_default_object(unreal.ElysiumModelSettings)
    values = {cpp: float(page.get_editor_property(py)) for cpp, py in DETAIL_SETTINGS_FIELDS}
    end = max(0.0, values["DetailDrawDistanceCm"])
    start = max(0.0, end - max(0.0, values["DetailFadeRangeCm"]))
    return start, end


#: The `UElysiumLightingSettings` fields `derive_light` reads, by their C++ name, with the Python
#: reflection name each is read through (`b` prefix dropped, snake_case). Restated here so a
#: renamed page field fails the bake loudly instead of silently deriving from a default.
LIGHTING_SETTINGS_FIELDS = (
    ("PointSpotScale", "point_spot_scale"),
    ("MaxBrightness", "max_brightness"),
    ("ExtendedMaxBrightness", "extended_max_brightness"),
    ("bUseExtendedBrightnessCeiling", "use_extended_brightness_ceiling"),
    ("FalloffExponent", "falloff_exponent"),
    ("RadiusScale", "radius_scale"),
    ("FallbackRadiusCm", "fallback_radius_cm"),
    ("IndirectLightingScale", "indirect_lighting_scale"),
    ("VolumetricScatteringScale", "volumetric_scattering_scale"),
    ("SunScaleLux", "sun_scale_lux"),
    ("SunSourceAngleDegrees", "sun_source_angle_degrees"),
    ("SunSoftSourceAngleDegrees", "sun_soft_source_angle_degrees"),
    ("MinSkyReachCm", "min_sky_reach_cm"),
    ("bPointShadows", "point_shadows"),
    ("bSpotShadows", "spot_shadows"),
    ("bSunShadows", "sun_shadows"),
)


def lighting_calibration():
    """The lighting page (`UElysiumLightingSettings`, Project Settings -> Elysium -> Lighting,
    the tracked ini) plus the surfaces page's `LightSpecularScale` (R5.5), as one plain dict --
    never a literal (`seam_map_map_lighting.md` -> "Import"). Read off the CDOs so the level in
    the editor and the values an owner sees on the page are the same numbers, and returned as
    plain Python so `derive_light` needs no `unreal` and the recipe can carry it."""

    page = unreal.get_default_object(unreal.ElysiumLightingSettings)
    calibration = {}
    for cpp_name, python_name in LIGHTING_SETTINGS_FIELDS:
        value = page.get_editor_property(python_name)
        calibration[cpp_name] = bool(value) if cpp_name.startswith("b") else float(value)
    calibration["LightSpecularScale"] = HOST.light_specular_scale()
    return calibration


def _cone_degrees(cosine):
    """`ConeDegrees` in `ElysiumLightRig.cpp`: the cone half-angle a stopdot cosine names,
    clamped to the 1..80 degrees a spot light accepts."""

    return max(1.0, min(80.0, math.degrees(math.acos(max(-1.0, min(1.0, float(cosine)))))))


def derive_light(row, calibration, sky_scale=16.0, sky_origin=(0.0, 0.0, 0.0)):
    """`UElysiumLightRig::ApplyToSource`, once, at bake (R5.6): the final actor values for one
    staged `lights[]` row under one lighting-page calibration. Pure -- no `unreal` -- so a pytest
    can pin it against the rig's formulas. `row` is `UE_map_sidecars.light_rows`' dict; the
    caller has already dropped rows with `max(rgb) <= 0` and the type-5 skyambient.

    The table in `seam_map_map_lighting.md` -> "Lights final (R5.6)" is this function."""

    kind = int(row["type"])
    rgb = [float(v) for v in row["rgb"]]
    mag = max(rgb)
    sky = bool(row["sky"])
    position = [float(v) for v in row["position"]]
    if sky:
        position = [float(sky_scale) * (position[i] - float(sky_origin[i])) for i in range(3)]

    if kind == 3:
        intensity = max(mag * calibration["SunScaleLux"], 0.01)
        reach = 0.0
    else:
        ceiling = (calibration["ExtendedMaxBrightness"]
                   if calibration["bUseExtendedBrightnessCeiling"]
                   else calibration["MaxBrightness"])
        intensity = min(mag * calibration["PointSpotScale"], ceiling)
        radius_cm = float(row["radiusCm"])
        reach = (radius_cm if radius_cm > 1.0 else calibration["FallbackRadiusCm"]) \
            * calibration["RadiusScale"]
        # A miniature light's reach is authored in miniature units, so it scales with the
        # geometry it lights; the floor keeps a degenerate authored radius from collapsing.
        if sky:
            reach = max(reach * float(sky_scale), calibration["MinSkyReachCm"])

    outer = _cone_degrees(row["stopdot2"])
    inner = min(_cone_degrees(row["stopdot"]), outer)
    cast_shadows = (False if kind == 0
                    else calibration["bSpotShadows"] if kind == 2
                    else calibration["bSunShadows"] if kind == 3
                    else calibration["bPointShadows"])
    return {
        "kind": kind,
        "position": position,
        "color": [rgb[0] / mag, rgb[1] / mag, rgb[2] / mag],
        "intensity": intensity,
        "reach_cm": reach,
        "falloff_exponent": calibration["FalloffExponent"],
        "outer_cone_deg": outer,
        "inner_cone_deg": inner,
        "cast_shadows": bool(cast_shadows),
        "specular_scale": calibration["LightSpecularScale"],
        "indirect_lighting_intensity": max(0.0, min(6.0, calibration["IndirectLightingScale"])),
        "volumetric_scattering_intensity":
            max(0.0, min(4.0, calibration["VolumetricScatteringScale"])),
        "sun_source_angle_deg": max(0.0, min(5.0, calibration["SunSourceAngleDegrees"])),
        "sun_soft_source_angle_deg": max(0.0, min(5.0, calibration["SunSoftSourceAngleDegrees"])),
        "allow_mega_lights": kind != 3,
    }


def _staging_dir(map_name):
    """`$ELYSIUM_WORK_ROOT/import/map_geometry/<map>` -- `map_geometry.staging_dir`'s twin."""

    return os.path.join(os.fspath(paths.work_root()), "import", FAMILY, map_name)


class _V2Material(object):
    """One staged `materials` row (`map_geometry.MaterialBinding.as_row`): the imported `MI_` a
    face group binds, and the root master/blend it renders through. `opaque` is the one predicate
    the shared chunking stage reads (a Nanite chunk is opaque/masked only), exactly the question
    the legacy `MatDef.opaque` answered from the corpus flags."""

    __slots__ = ("key", "unit", "asset", "master", "blend_mode", "opaque", "patched")

    #: The legacy per-map wetness path (`Bake.resolve_textures`'s sm_hub_1 `SourceCube` join)
    #: reads this off every world row; no V2 surface takes it -- wetness is the shared
    #: instance's own lane (R5.3), never a per-map cube stamped at bake.
    wet = False

    def __init__(self, key, row):
        self.key = key
        self.unit = row["unit"]
        self.asset = row["asset"]
        self.master = row["master"]
        self.blend_mode = row["blendMode"]
        self.opaque = bool(row["opaque"])
        self.patched = bool(row.get("patched"))


def _count_by_master(materials):
    counts = {}
    for mat in materials.values():
        counts[mat.master] = counts.get(mat.master, 0) + 1
    return counts


class _Placement(object):
    """One staged `staticProps[]` row, in the shape `map_geometry.Placement` publishes."""

    __slots__ = ("index", "stem", "model_path", "position", "rotation", "solid", "flags",
                 "skin", "fade_min_cm", "fade_max_cm", "sky")

    def __init__(self, row):
        self.index = int(row["index"])
        self.stem = row["stem"]
        self.model_path = row["modelPath"]
        self.position = tuple(float(value) for value in row["position"])
        self.rotation = tuple(float(value) for value in row["rotation"])
        self.solid = int(row["solid"])
        self.flags = int(row["flags"])
        self.skin = int(row["skin"])
        self.fade_min_cm = float(row["fadeMinCm"])
        self.fade_max_cm = float(row["fadeMaxCm"])
        self.sky = bool(row["sky"])

    @property
    def solid_blocks(self):
        return self.solid != 0

    @property
    def fades(self):
        return bool(self.flags & 0x1) and self.fade_max_cm > 0.0


class _DetailPlacement(object):
    """One staged `details.records[]` row (R6.3), in `map_geometry.DETAIL_RECORD_FIELDS` order:
    `[index, model, px, py, pz, qx, qy, qz, qw, sway, sky]`; `stem` is joined from
    `details.models[]` by the row's `model`."""

    __slots__ = ("index", "model", "stem", "position", "rotation", "sway", "sky")

    def __init__(self, row, stems):
        self.index = int(row[0])
        self.model = int(row[1])
        self.stem = stems[self.model]
        self.position = tuple(float(v) for v in row[2:5])
        self.rotation = tuple(float(v) for v in row[5:9])
        self.sway = int(row[9])
        self.sky = bool(row[10])

    def as_row(self):
        return [self.index, self.model, list(self.position), list(self.rotation), self.sway,
                int(self.sky)]


class _SpriteRow(object):
    """One staged `sprites[]` row (R6.1), in the shape `map_geometry.sprite_row` publishes."""

    __slots__ = ("index", "name", "material", "asset", "texture", "width", "height", "position",
                 "scale", "mode", "blend", "glow", "color", "alpha", "fx", "upright", "hidden",
                 "sky")

    def __init__(self, row):
        self.index = int(row["index"])
        self.name = str(row.get("name") or "")
        self.material = str(row["material"])
        self.asset = str(row["asset"])
        self.texture = str(row.get("texture") or "")
        self.width = int(row["width"])
        self.height = int(row["height"])
        self.position = tuple(float(v) for v in row["position"])
        self.scale = float(row["scale"])
        self.mode = int(row["mode"])
        self.blend = str(row["blend"])
        self.glow = bool(row["glow"])
        self.color = tuple(int(v) for v in row["color"])
        self.alpha = int(row["alpha"])
        self.fx = int(row.get("fx") or 0)
        self.upright = bool(row["upright"])
        self.hidden = bool(row["hidden"])
        self.sky = bool(row["sky"])

    def as_dict(self):
        return {
            "index": self.index, "name": self.name, "material": self.material,
            "asset": self.asset, "texture": self.texture, "width": self.width,
            "height": self.height, "position": list(self.position), "scale": self.scale,
            "mode": self.mode, "blend": self.blend, "glow": self.glow,
            "color": list(self.color), "alpha": self.alpha, "fx": self.fx,
            "upright": self.upright, "hidden": self.hidden, "sky": self.sky,
        }


class _CubemapSample(object):
    """One staged `cubemaps[]` row (R5.5): where a reflection capture stands."""

    __slots__ = ("index", "origin", "position", "sky")

    def __init__(self, row):
        self.index = int(row["index"])
        self.origin = tuple(int(v) for v in row["origin"])
        self.position = tuple(float(v) for v in row["position"])
        self.sky = bool(row["sky"])


class _LightRow(object):
    """One staged `lights[]` row (R5.6): `UE_map_sidecars.light_rows`' dict, the `.lights`
    producer's own row, in Unreal space with the engine's load-time fixups applied."""

    __slots__ = ("index", "type", "position", "direction", "rgb", "radius_cm", "stopdot",
                 "stopdot2", "exponent", "style", "sky")

    def __init__(self, row):
        self.index = int(row["index"])
        self.type = int(row["type"])
        self.position = tuple(float(v) for v in row["position"])
        self.direction = tuple(float(v) for v in row["direction"])
        self.rgb = tuple(float(v) for v in row["rgb"])
        self.radius_cm = float(row["radiusCm"])
        self.stopdot = float(row["stopdot"])
        self.stopdot2 = float(row["stopdot2"])
        self.exponent = float(row["exponent"])
        self.style = int(row["style"])
        self.sky = bool(row["sky"])

    def as_dict(self):
        """The row back in the staged shape -- what `derive_light` reads and the recipe carries."""
        return {
            "index": self.index, "type": self.type, "position": list(self.position),
            "direction": list(self.direction), "rgb": list(self.rgb),
            "radiusCm": self.radius_cm, "stopdot": self.stopdot, "stopdot2": self.stopdot2,
            "exponent": self.exponent, "style": self.style, "sky": int(self.sky),
        }


class _StagedGeometry(object):
    """The staged manifest plus its packed vertex file, read with no third-party module.

    Each scene comes back as a `bake_lib.ObjModel` -- the bake's own vertex-soup carrier, flat
    position/UV lists plus per-material index triples -- because that is exactly what the offline
    reader produces, in Unreal centimetres and Unreal winding. Adapting rather than reimplementing
    is the point: `_chunk_world`, `_sections`, `vertex_normals` and `_emit` are shared by both lanes,
    so the only difference between a legacy mesh and a V2 mesh is where its triangles came from.
    """

    def __init__(self, manifest_file):
        self.error = ""
        with open(manifest_file, "r", encoding="utf-8") as handle:
            self.manifest = json.load(handle)
        if (self.manifest.get("schema") != MANIFEST_SCHEMA
                or int(self.manifest.get("version", 0)) != MANIFEST_VERSION):
            self.error = "unsupported staged geometry manifest: %s" % manifest_file
            return
        vertex_file = os.path.join(
            os.path.dirname(manifest_file), self.manifest["vertexFile"])
        with open(vertex_file, "rb") as handle:
            self.bytes = handle.read()
        if len(self.bytes) != int(self.manifest["vertexBytes"]):
            self.error = ("staged vertex file %s is %d bytes, the manifest says %d"
                          % (vertex_file, len(self.bytes), self.manifest["vertexBytes"]))
            return
        self.unit = self.manifest["unit"]
        self.unit_sha256 = self.manifest["unitSha256"]
        self.counts = self.manifest["counts"]
        self.sky_scale, self.sky_origin = miniature_transform(self.manifest["sky"])
        self.sky_ok = bool(self.manifest["sky"]["ok"])
        self.placements = [_Placement(row) for row in self.manifest["placements"]]
        self.materials = dict(self.manifest["materials"])
        self.cubemaps = [_CubemapSample(row) for row in self.manifest.get("cubemaps") or []]
        self.lights = [_LightRow(row) for row in self.manifest.get("lights") or []]
        details = self.manifest.get("details") or {}
        self.detail_models = list(details.get("models") or [])
        stems = {int(model["model"]): str(model["stem"]) for model in self.detail_models}
        self.details = [_DetailPlacement(row, stems) for row in details.get("records") or []]
        self.sprites = [_SpriteRow(row) for row in self.manifest.get("sprites") or []]
        # R7.3: the effects tables, read as the stage wrote them (dicts; the writer maps fields).
        self.effects = list(self.manifest.get("effects") or [])
        self.particle_trees = dict(self.manifest.get("particleTrees") or {})
        self.dustmotes = list(self.manifest.get("dustmotes") or [])
        self.steam = list(self.manifest.get("steam") or [])
        self.beams = list(self.manifest.get("beams") or [])
        self.effect_stats = dict(self.manifest.get("effectStats") or {})

    def brush_stems(self):
        return {int(index): stem
                for index, stem in sorted(self.manifest["brushStems"].items(),
                                          key=lambda item: int(item[0]))}

    def _span(self, span, typecode):
        offset, count = span
        block = array(typecode)
        block.frombytes(self.bytes[offset:offset + count * block.itemsize])
        if sys.byteorder != "little":
            block.byteswap()
        return block

    def scene(self, name):
        """`(ObjModel, blend)` for one staged scene."""

        block = self.manifest["scenes"][name]
        positions = self._span(block["positions"], "f")
        uvs = self._span(block["uvs"], "f")
        blend = self._span(block["blend"], "f")
        indices = self._span(block["indices"], "I")

        model = bl.ObjModel()
        model.path = self.unit
        model.positions = [tuple(positions[base:base + 3])
                           for base in range(0, len(positions), 3)]
        model.uvs = [tuple(uvs[base:base + 2]) for base in range(0, len(uvs), 2)]
        model.groups = {
            key: indices[offset:offset + count].tolist()
            for key, (offset, count) in block["groups"].items()
        }
        return model, blend.tolist()
