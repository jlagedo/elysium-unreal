# The V2 map bake lane: world, 3D-sky and brush-model geometry and static-prop placements authored
# from the published map root unit (`vtmb:map:<map>`) instead of the legacy `<map>.obj`,
# `<map>_sky.obj`, `brushes/*.obj` and `<map>.props` sidecars.
#
# Roadmap R5.1 (`docs/project/seam_migration.md` -> "Roadmap -- one pipeline"); the ruling is
# `docs/architecture/seam_map_map.md` -> "## Import -- geometry and placements (R5.1)".
#
# **Beside the legacy bake, not over it.** This module holds only the inputs and the stages that
# differ; everything else -- lights, decals, fog stamping, the 3D-skybox transform, the player
# start, pruning, recipes and receipts -- is `bake_map.Bake`'s, unchanged, and a map that is not on
# the R5.1 flag never reaches a line of this file. The two lanes are selected per map by
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
# `bake_map.py` is an editor *script* (`-run=pythonscript`), so it calls `main()` at module scope.
# Importing it from here would run a second whole bake, so the dependency goes the other way:
# `bake_map` calls `bind(sys.modules[__name__])` and then `bake_class()`, and this module reaches
# its host through `HOST`.
from array import array
import json
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
MANIFEST_VERSION = 3

#: The key space the staged `MI_` instances live under in `Bake.materials` -- beside the legacy
#: `(package, key)` pairs, never colliding with one.
V2_MATERIAL_SCOPE = "v2"


#: The R1 model corpus (`docs/architecture/seam_map_model.md` -> "Import" -> "Identity and
#: naming"), `importers.models.PACKAGE_ROOT`'s value restated for the same reason. Its C++ twin
#: after this task's flip is `FElysiumContentPaths::BakedMeshes`.
V2_MESH_PACKAGE = "/ElysiumBaked/Meshes"
V2_SKIN_ASSET = "%s/DA_ElysiumPropSkins" % V2_MESH_PACKAGE

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

        # -------------------------------------------------------------------------- inputs

        def load_sources(self):
            """The map root unit's geometry, placements and materials, plus the map-scoped inputs
            the shared stages still own (`.env`, `.decals`, `.weather`, and the `.mtl` rows the
            decal lane alone still reads).

            Lights are R5.6's. Every surface's material is the staged `materials` table's (R5.4);
            the `.mtl` is consulted for nothing but the decal lane's legacy `M_Decal` instances.
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
            # R5.5: a moved sample or an edited `CaptureRadius` re-authors the level, because the
            # capture's contents live in the level's own MapBuildData and nowhere else.
            recipe["capture_radius"] = HOST.capture_radius()
            recipe["captures"] = [
                [sample.index, list(sample.position), sample.sky]
                for sample in self.geometry.cubemaps
            ]
            # The rest clip a skeletal-rest placement is dealt is a function of its model path and
            # its lump index, so it belongs in the recipe: a re-deal has to re-author the level.
            recipe["rest_poses"] = dict(sorted(self.rest_labels.items()))
            return recipe

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
            return placed, sky_placed

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


class _CubemapSample(object):
    """One staged `cubemaps[]` row (R5.5): where a reflection capture stands."""

    __slots__ = ("index", "origin", "position", "sky")

    def __init__(self, row):
        self.index = int(row["index"])
        self.origin = tuple(int(v) for v in row["origin"])
        self.position = tuple(float(v) for v in row["position"])
        self.sky = bool(row["sky"])


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
        self.sky_scale = float(self.manifest["sky"]["scale"])
        self.sky_origin = tuple(float(v) for v in self.manifest["sky"]["origin"])
        self.sky_ok = bool(self.manifest["sky"]["ok"])
        self.placements = [_Placement(row) for row in self.manifest["placements"]]
        self.materials = dict(self.manifest["materials"])
        self.cubemaps = [_CubemapSample(row) for row in self.manifest.get("cubemaps") or []]

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
