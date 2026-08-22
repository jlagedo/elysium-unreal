# Bakes one exported map into real Unreal assets under the /ElysiumBaked mount (the
# .uasset-bake spike -- docs/architecture/uasset-bake-spike.md).
#
# Where the shipping runtime builds every engine object in code at map-load time, this pass
# runs once in a headless editor and writes the same world out as Texture2D / MaterialInstance
# / StaticMesh assets plus a .umap, so the map gets the parts of the engine that only exist
# behind an offline build: Nanite, DDC-fitted Lumen surface cards, distance fields, real LODs
# and BC7/BC5 compression.
#
# The output is derived from the user's own VtMB install, so it is gitignored and regenerable
# exactly like $ELYSIUM_EXPORT_ROOT -- only the .uplugin mount descriptor is committed.
#
# Internal editor worker coordinated by `uv run elysium export map`:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="pipeline/unreal/bake_map.py"
#       -BakeMap=sp_tutorial_1 -unattended -nosplash -nopause
#
# Optional -BakeStages=<csv> restricts the run to a subset of:
#   textures, materials, world, sky, particles, level
import math
import json
import os
from pathlib import Path
import struct
import time

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402
from elysium_pipeline import bake_cache, placed_models as PM, shared_corpus as SC  # noqa: E402
from elysium_pipeline.paths import export_root  # noqa: E402
from elysium_pipeline.tasking import ContentDigestCache  # noqa: E402

MOUNT = "/ElysiumBaked"
OUT_ROOT = os.fspath(export_root())

MASTERS = {
    "opaque": "/Game/VtMB/Materials/M_World_Opaque.M_World_Opaque",
    "masked": "/Game/VtMB/Materials/M_World_Masked.M_World_Masked",
    "translucent": "/Game/VtMB/Materials/M_World_Translucent.M_World_Translucent",
    "glass": "/Game/VtMB/Materials/M_World_Glass.M_World_Glass",
    "refract": "/Game/VtMB/Materials/M_Refract.M_Refract",
    "additive": "/Game/VtMB/Materials/M_Additive.M_Additive",
    "decal": "/Game/VtMB/Materials/M_Decal.M_Decal",
}

# World chunk edge, centimetres. Triangles are binned by centroid cell; each cell yields one
# Nanite mesh (opaque + masked surfaces) and, where present, one non-Nanite sibling for the
# translucent/additive surfaces Nanite cannot carry.
CELL_CM = 2048.0

# Mirrors the runtime cvar defaults (FElysiumMaterialFactory) so a baked material instance
# lands on the same values the runtime MID would have bound.
EMISSIVE_SCALE = 1.5
BUMP_AMOUNT = 1.0
ENV_STRENGTH = 1.0

# The $envmap reflection channel (roadmap 7.5). Mirrors make_world_materials.py's parameter
# defaults, and bound only onto materials that actually carry $envmap -- a non-reflective
# surface keeps the master's own Lambert base (RoughBase 1.0 / SpecBase 0.0) untouched.
ROUGH_BASE = 1.0
ROUGH_REFLECT = 0.15
SPEC_BASE = 0.0
SPEC_REFLECT = 0.5

# UElysiumLightRig's calibrated constants, so a baked light matches the runtime rig's.
POINT_SPOT_SCALE = 0.003
MAX_BRIGHTNESS = 8.0
FALLOFF_EXPONENT = 1.0
RADIUS_SCALE = 1.0
SPECULAR_SCALE = 0.0
SUN_SCALE_LUX = 8.0
FALLBACK_RADIUS_CM = 2500.0
# Floor on a 3D-skybox light's reach after the miniature's uniform scale. A miniature light's
# authored radius is in miniature units and scales with the geometry it lights, but a source
# authored with a degenerate radius would otherwise scale to a reach that lights nothing at all.
MIN_SKY_REACH_CM = 5000.0
# The sky light's hue where a map authors no type-5 row. Its INTENSITY in that case is 0 (D2),
# so this only shows through if something later gives such a map a level.
SKYLIGHT_FALLBACK_COLOR = unreal.LinearColor(0.12, 0.13, 0.18, 1.0)

# The actor tags AElysiumMapActor::AdoptBakedLevel buckets the level by. Keep in sync with
# Source/ElysiumUE/Public/ElysiumBakedTags.h -- tags rather than Outliner folders because
# folder paths are editor-only metadata and do not survive into a -game build.
TAG_WORLD = "elysium.world"
TAG_SKY = "elysium.sky"
TAG_PROP = "elysium.prop"
TAG_LIGHT = "elysium.light"
TAG_SKYLIGHT = "elysium.skylight"
TAG_FOG = "elysium.fog"
TAG_DECAL = "elysium.decal"
TAG_PPV = "elysium.ppv"

# A deferred decal's projection box reaches this far (cm) either way along its projection
# axis. Kept shallow so a decal catches its host wall and not the geometry behind it.
DECAL_HALF_DEPTH = 16.0

# Source's distance fog is a PER-PRIMITIVE material term, not the height fog actor: the world and
# the 3D-skybox miniature carry two different fogs and share screen depth, so no engine-side fog
# mechanism can separate them (the reasoning and the measurement are in pipeline/unreal/mat_fog.py). These
# are the Custom Primitive Data slots that term reads. Keep in sync with mat_fog.CPD_* and
# Source/ElysiumUE/Public/ElysiumFog.h.
FOG_CPD_COLOR = 0        # float4: linear RGB, then an unused A
FOG_CPD_START = 4
FOG_CPD_INV_RANGE = 5
FOG_CPD_FLOATS = 6

# A map bakes only what carries its own inputs. Prop meshes and every surface texture and
# material belong to the corpus scope, which runs `shared_corpus.STAGES` instead.
ALL_STAGES = ("textures", "materials", "world", "sky", "particles", "level")

# Static models per prop-stage checkpoint. The corpus builds thousands of meshes in one editor
# process, so the stage saves, reports and releases each batch instead of holding the whole set
# until the end -- a run that dies keeps every batch that landed.
PROP_BATCH = 250

# The shared, map-independent scope: every texture, material and static model in the install.
# It is a scope name rather than a map name -- `$ELYSIUM_EXPORT_ROOT/shared` in,
# `/ElysiumBaked/Shared` out -- and only the three stages a bodiless corpus has inputs for apply
# to it. Its names are `elysium_pipeline.shared_corpus`'s; keep the C++ twin in
# FElysiumContentPaths in sync with that module.


# Both are named profiles from Config/DefaultEngine.ini rather than per-channel edits, because
# only the profile name survives the .umap save/load round-trip: loading re-applies the profile
# and discards custom responses set alongside it.
PROFILE_PICK_ONLY = "ElysiumPickOnly"    # drawn, but touched by nothing except the debug pick
PROFILE_PROP_SOLID = "ElysiumPropSolid"  # blocks the pawn, occludes +use, pickable


def log(msg):
    unreal.log("[bake] %s" % msg)


def _make_movable(component):
    """Make a spawned light dynamic BEFORE anything sets a property that only a movable light takes.

    `APointLight`/`ASpotLight`/`ADirectionalLight` all construct Stationary, and
    `SetAttenuationRadius`, `SetInnerConeAngle` and `SetOuterConeAngle` guard on
    `AreDynamicDataChangesAllowed(false)` -- which rejects Stationary and returns **silently**, no
    log and no return value. Setting mobility afterwards is therefore too late: the light keeps
    ULocalLightComponent's constructed 1000 cm radius and 44 degree cone instead of the authored
    ones. `SetIntensity` and `SetLightColor` pass the default `bIgnoreStationary=true` and do
    apply, which is what made the loss look like a tuning problem rather than a dropped write.
    """
    component.set_mobility(unreal.ComponentMobility.MOVABLE)


def _dir_rotator(direction):
    """Unreal spot/directional lights emit along +X, so aim that axis down the beam."""
    if direction.length() < 1e-6:
        return unreal.Rotator(0.0, 0.0, 0.0)
    return unreal.MathLibrary.conv_vector_to_rotator(direction)


def fail(msg):
    unreal.log_error("[bake] %s" % msg)


def png_coarse_mip(path):
    """Mip level whose larger axis is approximately eight texels."""
    try:
        with open(path, "rb") as handle:
            header = handle.read(24)
        if header[:8] != b"\x89PNG\r\n\x1a\n":
            return 5.0
        width, height = struct.unpack(">II", header[16:24])
        return max(0.0, math.log(max(width, height), 2.0) - 3.0)
    except (OSError, ValueError, struct.error):
        return 5.0


def fog_color(env, prefix=""):
    """One `.env` fog colour, decoded to linear. `.env` transports the authored value verbatim
    (/255); VtMB's colours are gamma-encoded and its own math decodes them with a plain 2.2
    (RE-A5), which is also what an albedo texture gets on import. The magnitudes agree: the maps'
    own sky radiance is 0.0034-0.0066, so an undecoded 17/255 = 0.067 would put the fog well
    above the world it hangs in. Full reasoning: Source/ElysiumUE/Public/ElysiumFog.h."""
    rgb = env.get(prefix + "fogcolor", ["0", "0", "0"])
    return [max(0.0, float(c)) ** 2.2 for c in rgb[:3]]


def fog_data(env, prefix=""):
    """The FOG_CPD_FLOATS custom-primitive floats for one `.env` fog set: "" is `worldspawn`'s
    (the world's own) and "sky" is the `sky_camera`'s (the miniature's, its distances already
    x scale into world units by the exporter).

    A set that is off -- or degenerate -- yields an inverse range of 0, which is precisely what an
    unwritten slot reads as, so "no fog" and "never written" are the same thing everywhere."""
    on = env.get(prefix + "fog", ["0"])[0] == "1"
    start = float(env.get(prefix + "fogstart", ["0"])[0])
    end = float(env.get(prefix + "fogend", ["0"])[0])
    return fog_color(env, prefix) + [1.0,
            start, 1.0 / (end - start) if on and end > start else 0.0]


def set_fog(component, data):
    """Stamp a fog set onto one primitive. The default (serialized) slot, not the transient one:
    the level has to look right when it is opened in the editor, before any game runs."""
    component.set_default_custom_primitive_data_float_array(FOG_CPD_COLOR, data)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line."""
    needle = "-%s=" % key
    for token in unreal.SystemLibrary.get_command_line().split():
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def _asset_path(asset):
    if asset is None:
        return ""
    path = asset.get_path_name() if hasattr(asset, "get_path_name") else str(asset)
    return path.split(".", 1)[0]


class AssetTracker(object):
    """Decide and report per-package work for one map bake."""

    def __init__(self, map_name, plan, digest_cache):
        self.map = map_name
        self.plan = plan
        self.run_id = str(plan["run_id"])
        self.force = bool(plan.get("force"))
        self.map_plan = plan["maps"][map_name]
        self.digest_cache = digest_cache
        self.selected = tuple(self.map_plan["stages"])
        self.receipts = bake_cache.AssetReceiptStore(Path(OUT_ROOT), map_name)
        self.repo_root = Path(unreal.Paths.project_dir()).resolve()
        self.stages = {}
        for stage in self.selected:
            self.stages[stage] = {
                "policy": self.map_plan["policies"][stage],
                "assets": {},
                "built": 0,
                "reused": 0,
                "pruned": 0,
            }

    def selected_stage(self, stage):
        return stage in self.stages

    def file_sha256(self, path):
        return self.digest_cache.digest(Path(path))

    def register(self, stage, object_path, recipe, expected_class="", fresh=True):
        if stage not in self.stages:
            raise RuntimeError("asset registered for an unselected stage: %s" % stage)
        policy = self.stages[stage]["policy"]
        fingerprint = bake_cache.asset_recipe_fingerprint(
            stage, object_path, policy, recipe)
        output = bake_cache.unreal_output_path(self.repo_root, object_path, stage)
        exists = unreal.EditorAssetLibrary.does_asset_exist(object_path)
        class_ok = True
        if exists and expected_class:
            class_ok = bl.asset_class_name(object_path) == expected_class
            if not class_ok:
                bl.delete_owned_asset(object_path)
                exists = False
        dirty = (self.force or not fresh or not exists or not class_ok
                 or not self.receipts.matches(stage, object_path, fingerprint))
        self.stages[stage]["assets"][object_path] = {
            "object_path": object_path,
            "fingerprint": fingerprint,
            "output": str(output),
        }
        if dirty:
            return True
        self.stages[stage]["reused"] += 1
        return False

    def built(self, stage, count=1):
        self.stages[stage]["built"] += count

    def pruned(self, stage, count):
        self.stages[stage]["pruned"] += int(count)

    def summary(self, stage):
        data = self.stages[stage]
        return "%d built / %d reused / %d pruned" % (
            data["built"], data["reused"], data["pruned"])

    def write_report(self):
        report = {
            "schema": bake_cache.ASSET_RUN_SCHEMA,
            "version": bake_cache.ASSET_SCHEMA_VERSION,
            "run_id": self.run_id,
            "map": self.map,
            "stages": self.stages,
        }
        bake_cache.write_asset_run_report(Path(OUT_ROOT), report)

    def write_receipts(self):
        """Persist the per-asset receipts of every stage that has flushed work.

        `write_report` publishes what the orchestrator validates and promotes once the commandlet
        exits. This writes that promoted document directly, so the work survives a kill of the
        whole process tree -- the orchestrator no longer has to be alive for it to be kept.
        """
        return bake_cache.checkpoint_receipts(Path(OUT_ROOT), self.map, self.stages)


class Bake(object):
    #: The scope this bake may prune. Empty is a map: it authors only its own packages, so
    #: `bake_lib` refuses it the shared corpus, whose wanted-set no map knows.
    prune_scope = ""

    def __init__(self, map_name, tracker, digest_cache):
        self.map = map_name
        self.tracker = tracker
        self.digest_cache = digest_cache
        self.dir = os.path.join(OUT_ROOT, map_name)
        # The shared corpus: one decode per source, so one asset per source. A map resolves these
        # and never authors them -- the corpus scope owns their receipts and their pruning.
        self.corpus_dir = os.fspath(SC.corpus_dir(OUT_ROOT))
        self.shared_tex_pkg = SC.BAKED_TEXTURES
        self.shared_mat_pkg = SC.BAKED_MATERIALS
        self.shared_mesh_pkg = SC.BAKED_MESHES
        self.pkg = "%s/%s" % (MOUNT, map_name)
        # A map's own packages hold only what carries a map-specific input: the baked env cubemaps,
        # the rain height field, the material instances that stamp this map's fog or weather, and
        # its geometry.
        self.cube_pkg = "%s/Textures/Cubes" % self.pkg
        self.mat_pkg = "%s/Materials" % self.pkg
        self.decal_mat_pkg = "%s/Materials/Decals" % self.pkg
        self.mesh_pkg = "%s/Meshes" % self.pkg
        self.brush_pkg = "%s/Brushes" % self.pkg
        self.prop_pkg = "%s/Props" % self.pkg
        self.weather_pkg = "%s/Weather" % self.pkg
        self.corpus_materials = {}   # material key -> definition (the whole install)
        self.local_materials = {}    # material key -> definition only this map carries
        self.corpus_manifest = {}    # the whole census
        self.corpus_textures = {}    # texture key -> {"files": {...}, "size": [...]}

        self.world_obj = None            # ObjModel
        self.world_mats = {}             # name -> MatDef
        self.blend = []                  # per-vertex WVT weight
        self.decals = []                 # DecalDef, in sidecar order
        self.textures = {}               # (package, asset name) -> Texture2D
        self.cubemaps = {}               # exact MTL envmap id -> TextureCube
        self.materials = {}              # (package, material key) -> MaterialInstanceConstant
        self.prop_models = {}            # stem -> ObjModel
        self.brush_models = {}           # brush_<model> -> local-space ObjModel
        self.brush_blend = {}            # stem -> per-vertex WVT weight
        self.prop_mats = {}              # stem -> {name: MatDef}
        self.prop_skins = {}             # stem -> {family: {authored material: family material}}
        self.prop_phys = {}              # stem -> {"mass": kg, "hulls": [(verts, tris)]}
        self.masters = {}
        self.error_material_asset = None  # the shared checkerboard, authored on the first miss
        self.error_keys = {}             # material name that resolved no VMT -> slots bound to it
        self.env = {}                    # <map>.env, key -> [tokens]
        self.weather = None              # <map>.weather.json
        self.rain_height = None
        self.saved = []                  # asset paths pending save

    def _file_sha256(self, path):
        return self.digest_cache.digest(Path(path))

    # ------------------------------------------------------------------ inputs

    def load_masters(self):
        for key, path in MASTERS.items():
            asset = unreal.load_asset(path)
            if not asset:
                fail("master material missing: %s" % path)
                return False
            self.masters[key] = asset
        return True

    def load_corpus(self):
        """Load the shared corpus's two documents plus this map's own local material overrides."""
        materials_file = os.fspath(SC.materials_path(OUT_ROOT))
        manifest_file = os.fspath(SC.manifest_path(OUT_ROOT))
        for path in (materials_file, manifest_file):
            if not os.path.isfile(path):
                fail("no shared corpus at %s (run: uv run elysium export bundle corpus)" % path)
                return False
        with open(materials_file, "r", encoding="utf-8") as handle:
            self.corpus_materials = SC.check_materials(json.load(handle))["materials"]
        with open(manifest_file, "r", encoding="utf-8") as handle:
            self.corpus_manifest = SC.check_manifest(json.load(handle))
        self.corpus_textures = self.corpus_manifest["textures"]
        local_file = os.path.join(self.dir, "%s.materials.json" % self.map)
        if os.path.isfile(local_file):
            with open(local_file, "r", encoding="utf-8") as handle:
                self.local_materials = SC.check_materials(json.load(handle))["materials"]
        return True

    def load_sources(self):
        obj_path = os.path.join(self.dir, "%s.obj" % self.map)
        if not os.path.isfile(obj_path):
            fail("no export at %s" % obj_path)
            return False
        if not self.load_corpus():
            return False
        self.env = self._read_env()
        start = time.time()
        self.world_obj = bl.read_obj(obj_path)
        self.world_mats = bl.read_mtl(
            os.path.join(self.dir, "%s.mtl" % self.map),
            corpus=self.corpus_materials, local=self.local_materials)
        self.blend = bl.read_floats(os.path.join(self.dir, "%s.blend" % self.map))
        self.decals = bl.read_decals(os.path.join(self.dir, "%s.decals" % self.map))
        weather_path = os.path.join(self.dir, "%s.weather.json" % self.map)
        if os.path.isfile(weather_path):
            with open(weather_path, "r", encoding="utf-8") as handle:
                self.weather = json.load(handle)
            if (self.weather.get("schema") != "elysium.map-weather"
                    or self.weather.get("version") != 1):
                fail("unsupported weather sidecar: %s" % weather_path)
                return False
        log("world: %d verts / %d tris / %d groups / %d materials / %d decals (%.1fs)" % (
            len(self.world_obj.positions), self.world_obj.tri_count,
            len(self.world_obj.groups), len(self.world_mats), len(self.decals),
            time.time() - start))

        self._load_placed_prop_materials()

        brush_dir = os.path.join(self.dir, "brushes")
        if os.path.isdir(brush_dir):
            for entry in sorted(os.listdir(brush_dir)):
                if not entry.endswith(".obj"):
                    continue
                stem = entry[:-4]
                self.brush_models[stem] = bl.read_obj(os.path.join(brush_dir, entry))
                self.brush_blend[stem] = bl.read_floats(
                    os.path.join(brush_dir, stem + ".blend"))
            log("brushes: %d local-space models / %d tris" % (
                len(self.brush_models),
                sum(m.tri_count for m in self.brush_models.values())))
        return True

    def _load_placed_prop_materials(self):
        """The material and skin tables for the models THIS map places.

        A map builds no prop geometry -- the corpus owns every mesh -- but its level stage still
        bakes a placement's alternate skin in as material overrides, because a GAME_LUMP prop is
        not an entity and never changes skin at runtime. That needs two small joins and no mesh:
        the manifest's own slot -> material key table, and the model's `.skins` sidecar.
        """
        stems = set()
        props = os.path.join(self.dir, "%s.props" % self.map)
        if os.path.isfile(props):
            with open(props, "r", encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    tok = line.split()
                    if tok:
                        stems.add(tok[0])
        ents = os.path.join(self.dir, "%s.ents" % self.map)
        if os.path.isfile(ents):
            with open(ents, "r", encoding="utf-8") as handle:
                for entity in json.load(handle).get("entities", []):
                    stem = entity.get("model_mesh", "")
                    if stem:
                        stems.add(stem)
        models = self.corpus_manifest.get("models", {})
        prop_dir = os.path.join(self.corpus_dir, SC.PROPS)
        missing = []
        for stem in sorted(stems):
            row = models.get(stem)
            if row is None:
                missing.append(stem)
                continue
            # The manifest already states each slot under the fold the OBJ's `usemtl`, the
            # `.skins` sidecar and the corpus `.mtl` all use, so the join is verbatim. Applying a
            # second fold here would silently miss the models whose names the two folds treat
            # differently -- `bl.safe_name` collapses runs and drops `.` and `-`.
            self.prop_mats[stem] = {
                slot: bl.mat_from_record(slot, self.corpus_materials[key], key)
                for slot, key in row.get("materials", {}).items()
                if key in self.corpus_materials
            }
            skins = bl.read_skins(os.path.join(prop_dir, stem + ".skins"))
            if skins:
                self.prop_skins[stem] = skins
        if missing:
            fail("%d placed model(s) are absent from the shared corpus: %s"
                 % (len(missing), ", ".join(missing[:8])))
        log("placed props: %d model(s) joined to the corpus, %d with alternate skins"
            % (len(stems) - len(missing), len(self.prop_skins)))

    def _load_prop_sources(self):
        """Read the corpus's `props/` -- one OBJ + MTL per model, plus its skin and physics
        sidecars. One model, one decode, whichever maps place it."""
        prop_dir = os.path.join(self.corpus_dir, SC.PROPS)
        if os.path.isdir(prop_dir):
            start = time.time()
            for entry in sorted(os.listdir(prop_dir)):
                if not entry.endswith(".obj"):
                    continue
                stem = entry[:-4]
                model = bl.read_obj(os.path.join(prop_dir, entry))
                self.prop_models[stem] = model
                self.prop_mats[stem] = bl.read_mtl(
                    os.path.join(prop_dir, model.mtl_name or (stem + ".mtl")),
                    corpus=self.corpus_materials)
                skins = bl.read_skins(os.path.join(prop_dir, stem + ".skins"))
                if skins:
                    self.prop_skins[stem] = skins
                # A `.phys` marks a prop_physics model and carries VtMB's own convex
                # collision + authored mass. Its absence is meaningful, not a gap: the
                # model then has no collision model at all, and the runtime leaves the
                # prop visible but inert, as CPhysicsProp::CreateVPhysics does.
                phys = bl.read_phys(os.path.join(prop_dir, stem + ".phys"))
                if phys and phys["hulls"]:
                    self.prop_phys[stem] = phys
            tris = sum(m.tri_count for m in self.prop_models.values())
            hulls = sum(len(p["hulls"]) for p in self.prop_phys.values())
            log("props: %d models / %d tris / %d with alternate skins / "
                "%d physics (%d convex hulls) (%.1fs)" % (
                    len(self.prop_models), tris, len(self.prop_skins),
                    len(self.prop_phys), hulls, time.time() - start))

    # ---------------------------------------------------------------- textures

    def _texture_jobs(self, mats, base_dir, package):
        """Collect (source file, asset name, role) for every texture the material set uses."""
        roles = {}
        for mat in mats.values():
            for path, role in ((mat.albedo, "albedo"), (mat.emissive, "albedo"),
                               (mat.base_tex2, "albedo"), (mat.bump, "normal"),
                               (mat.refract_map, "normal"),
                               (mat.env_mask, "mask")):
                if not path:
                    continue
                src = os.path.join(base_dir, path.replace("/", os.sep))
                name = SC.texture_asset(path)
                # First use wins the role; a texture bound as both colour and mask is rare
                # and the colour reading is the safe default.
                roles.setdefault(name, (src, role))
        return [(name, src, role) for name, (src, role) in sorted(roles.items())]

    def _import_texture_jobs(self, jobs, package, expected_class="Texture2D", prune_prefix=""):
        wanted = set()
        dirty = []
        result = {}
        for name, source, role in jobs:
            if not os.path.isfile(source):
                raise SystemExit("[bake] referenced texture source is missing: %s" % source)
            object_path = "%s/%s" % (package, name)
            wanted.add(name)
            recipe = {
                "source": os.path.relpath(source, self.dir).replace(os.sep, "/"),
                "sha256": self._file_sha256(source),
                "role": role,
                "class": expected_class,
                "settings": "elysium-texture-role-v1",
            }
            if self.tracker.register(
                    "textures", object_path, recipe,
                    expected_class=expected_class):
                dirty.append((source, name, role, object_path))
            else:
                asset = unreal.EditorAssetLibrary.load_asset(object_path)
                if not asset:
                    raise SystemExit("[bake] cached texture could not be loaded: %s" % object_path)
                result[name] = asset

        imported = bl.import_textures(
            [(source, name) for source, name, _, _ in dirty], package)
        for source, name, role, object_path in dirty:
            texture = imported.get(name)
            if not texture or texture.get_class().get_name() != expected_class:
                raise SystemExit("[bake] texture import failed: %s" % object_path)
            bl.configure_texture(texture, role)
            result[name] = texture
            self.saved.append(object_path)
            self.tracker.built("textures")
        pruned = (bl.prune_package_prefix(package, prune_prefix, wanted, self.prune_scope)
                  if prune_prefix else bl.prune_package(package, wanted, self.prune_scope))
        self.tracker.pruned("textures", pruned)
        return result

    def _drop_superseded_packages(self):
        """Delete the packages a map no longer owns.

        Prop meshes, prop materials and every surface texture moved to the shared corpus. A map
        baked before that still carries them, and nothing authors those packages any more -- so
        nothing would ever prune them. Left in place they are the duplication this scope split
        removed, still on disk and still in the asset registry.
        """
        dropped = 0
        # Whole directories, not asset by asset: a map's retired prop tree runs to thousands of
        # packages and `prune_package` force-deletes each one with its own garbage collection,
        # which costs minutes per map. Nothing references these, so the directory goes at once.
        for package in ("%s/Props" % self.pkg,):
            if unreal.EditorAssetLibrary.does_directory_exist(package):
                dropped += len(unreal.EditorAssetLibrary.list_assets(package, recursive=True))
                unreal.EditorAssetLibrary.delete_directory(package)
        # The map's own texture package keeps its `Cubes` subdirectory, so this one is pruned
        # rather than deleted -- the surface textures directly in it are the retired set.
        if unreal.EditorAssetLibrary.does_directory_exist("%s/Textures" % self.pkg):
            dropped += bl.prune_package("%s/Textures" % self.pkg, set(), self.prune_scope)
        if dropped:
            self.tracker.pruned("textures", dropped)
            log("superseded: %d asset(s) removed from this map's retired packages" % dropped)

    def stage_textures(self):
        """A map imports only the textures that are its own: the baked env cubemaps and the rain
        height field. Every surface texture is the corpus's, imported once by its own scope."""
        self._drop_superseded_packages()
        wet_materials = [mat for mat in self.world_mats.values() if mat.wet]
        if self.map == "sm_hub_1":
            if len(wet_materials) != 14:
                raise SystemExit(
                    "[bake] sm_hub_1 requires 14 GlobalWetness materials, found %d"
                    % len(wet_materials))
            cube_ids = {mat.env_cube for mat in wet_materials}
            if cube_ids != {"cubemapdefault"}:
                raise SystemExit(
                    "[bake] sm_hub_1 wet materials require cubemapdefault, found %r"
                    % sorted(cube_ids))
            jobs = []
            for cube_id in sorted(cube_ids):
                source = os.path.join(self.dir, "tex", "cube", cube_id + ".dds")
                if not os.path.isfile(source):
                    raise SystemExit("[bake] wet cubemap missing: %s" % source)
                jobs.append(("TC_" + bl.safe_name(cube_id), source, "cube"))
            imported = self._import_texture_jobs(jobs, self.cube_pkg, "TextureCube")
            for cube_id in sorted(cube_ids):
                name = "TC_" + bl.safe_name(cube_id)
                texture = imported.get(name)
                if not texture or texture.get_class().get_name() != "TextureCube":
                    raise SystemExit(
                        "[bake] %s/%s did not import as TextureCube" % (self.cube_pkg, name))
                self.cubemaps[cube_id] = texture
            log("wet cubemaps: %d into %s" % (len(imported), self.cube_pkg))
        else:
            pruned = bl.prune_package(self.cube_pkg, set(), self.prune_scope)
            self.tracker.pruned("textures", pruned)
        if self.weather:
            relative = self.weather["height_texture"]["path"]
            source = os.path.join(self.dir, relative.replace("/", os.sep))
            if not os.path.isfile(source):
                fail("weather height texture missing: %s" % source)
            else:
                imported = self._import_texture_jobs(
                    [("T_RainHeight", source, "height")], self.weather_pkg,
                    prune_prefix="T_")
                self.rain_height = imported.get("T_RainHeight")
                if not self.rain_height:
                    raise SystemExit("[bake] weather height texture import failed")
        else:
            path = "%s/T_RainHeight" % self.weather_pkg
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                bl.delete_owned_asset(path)
                self.tracker.pruned("textures", 1)
        log("textures: %s" % self.tracker.summary("textures"))

    def _all_prop_mats(self):
        """Every prop material, by its corpus key. A model names a material; the key it resolved
        to is its identity, so two models drawing one material draw one instance."""
        merged = {}
        for mats in self.prop_mats.values():
            for mat in mats.values():
                merged[mat.material_key] = mat
        return merged

    def _material_sets(self):
        """(materials, material package, texture package) for every set this scope AUTHORS.

        A map authors only the materials that stamp something of its own into the instance: the
        ones VBSP patched to a baked cubemap, the deferred decals that carry its fog, and the
        wetness-driven surfaces that carry its weather. Everything else is the corpus's, and a map
        that authored a second copy would be the duplication this whole scope split removes.
        """
        world, decals = {}, {}
        for key, mat in self.world_mats.items():
            if not SC.is_map_scoped_material(
                    key, decal=mat.decal, wetness_driven=mat.wetness_driven, local=mat.local):
                continue
            (decals if mat.decal else world)[key] = mat
        return ((world, self.mat_pkg, self.shared_tex_pkg),
                (decals, self.decal_mat_pkg, self.shared_tex_pkg))

    def error_material(self):
        """The material a slot binds when its own name resolved no `.vmt`. Authored on first use,
        so a run without a miss adds nothing to the mount."""
        if self.error_material_asset is None:
            self.error_material_asset = bl.ensure_error_material()
            if self.error_material_asset is None:
                raise SystemExit("[bake] the error material could not be authored: %s"
                                 % bl.ERROR_MATERIAL_PATH)
        return self.error_material_asset

    def error_bind(self, key, where):
        """Bind the error material for one slot whose material name resolves no `.vmt`, warning
        once per distinct name.

        VtMB substitutes its own `___error` material for exactly this case and says nothing about
        it at the shipped developer level, deduping its dev-only message per name; 1,152 model
        slots across 100 names miss install-wide (research case `material-resolution`). So a
        missed slot is a surface to reproduce, not a failure: the model is built and receipted
        with this material bound, and because the recipe names it, a `.vmt` appearing for the key
        later rewrites the `.mtl`, the recipe and the mesh.
        """
        if key not in self.error_keys:
            unreal.log_warning(
                "[bake] material %r resolves no VMT -- binding %s (first seen on %s)"
                % (key, bl.ERROR_MATERIAL_PATH, where))
            self.error_keys[key] = 0
        self.error_keys[key] += 1
        return self.error_material()

    def material_for(self, key):
        """The material instance one world surface binds.

        Which package holds it follows the same predicate the material stage authored it under: a
        surface whose material stamps this map's cubemap, fog or weather into the instance binds
        the map's own copy; every other surface binds the corpus's single instance. Looking in one
        package only would leave the other set bound to nothing, which draws the master's own
        placeholder rather than failing.
        """
        mat = self.world_mats.get(key)
        if mat is None:
            # The `.mtl` carries this surface's `newmtl` with no `mat` line, so its material name
            # resolved no `.vmt` -- the engine's own miss, answered the engine's own way.
            return self.error_bind(key, self.map)
        if SC.is_map_scoped_material(key, decal=mat.decal, wetness_driven=mat.wetness_driven,
                                     local=mat.local):
            package = self.decal_mat_pkg if mat.decal else self.mat_pkg
            return self.materials.get((package, key))
        return self.materials.get((self.shared_mat_pkg, mat.material_key))

    def _shared_material_keys(self):
        """Every corpus material this scope only RESOLVES, so a mesh stage can bind it."""
        keys = {}
        for key, mat in self.world_mats.items():
            if not SC.is_map_scoped_material(
                    key, decal=mat.decal, wetness_driven=mat.wetness_driven, local=mat.local):
                keys[mat.material_key] = mat
        return keys

    def _master_for(self, mat):
        if mat.decal:
            return self.masters["decal"]
        if mat.additive:
            return self.masters["additive"]
        if mat.refract:
            return self.masters["refract"]
        if mat.glass:
            return self.masters["glass"]
        # Before `blend`, because the exporter writes `water 1` INSTEAD of `blend 1` -- a water
        # surface never carries the translucent flag, so testing `blend` alone bakes every canal
        # and sewer as an opaque Nanite card. Most water authors no $basetexture either, so that
        # card resolves no albedo and draws the master's own placeholder.
        if mat.water:
            return self.masters["translucent"]
        if mat.blend:
            return self.masters["translucent"]
        if mat.scissor:
            return self.masters["masked"]
        return self.masters["opaque"]

    def _material_recipe(self, mat, tex_pkg):
        values = {name: getattr(mat, name) for name in mat.__slots__}
        values.update({
            "opaque": mat.opaque,
            "chromatic": mat.chromatic,
            "tint_luma": mat.tint_luma,
            "master": _asset_path(self._master_for(mat)),
            "textures": {
                key: ("%s/%s" % (tex_pkg, SC.texture_asset(path))) if path else ""
                for key, path in {
                    "albedo": mat.albedo,
                    "emissive": mat.emissive,
                    "base_tex2": mat.base_tex2,
                    "bump": mat.bump,
                    "refract_map": mat.refract_map,
                    "env_mask": mat.env_mask,
                }.items()
            },
            "constants": {
                "emissive": EMISSIVE_SCALE,
                "bump": BUMP_AMOUNT,
                "env": ENV_STRENGTH,
                "rough_base": ROUGH_BASE,
                "rough_reflect": ROUGH_REFLECT,
                "spec_base": SPEC_BASE,
                "spec_reflect": SPEC_REFLECT,
            },
        })
        if mat.env_mask:
            # `_bind` stamps EnvMaskCoarseMip out of this file's own PNG header, so the mask's
            # bytes are part of the instance rather than only of the texture asset it binds.
            source = os.path.join(self.corpus_dir, mat.env_mask.replace("/", os.sep))
            if os.path.isfile(source):
                values["env_mask_sha256"] = self._file_sha256(source)
            else:
                fail("material %s names a missing env mask: %s" % (mat.name, source))
                values["env_mask_sha256"] = "missing"
        if mat.decal:
            values["fog"] = fog_data(self.env)
        if mat.wet:
            values["weather"] = self.weather
        return values

    def _bind(self, mic, mat, tex_pkg):
        """Bind the same named parameters FElysiumMaterialFactory::Build binds."""
        def tex(path):
            if not path:
                return None
            return self.textures.get((tex_pkg, SC.texture_asset(path)))

        if mat.refract:
            normal = tex(mat.refract_map)
            if normal:
                bl.set_tex_param(mic, "RefractMap", normal)
            bl.set_scalar_param(mic, "SourceRefractAmount", mat.refract_amount)
            return

        albedo = tex(mat.albedo)
        if albedo:
            bl.set_tex_param(mic, "Albedo", albedo)
        # The additive master is unlit and carries none of the lit feature parameters.
        if mat.additive:
            return
        emissive = tex(mat.emissive)
        if emissive:
            bl.set_tex_param(mic, "Emissive", emissive)
            bl.set_scalar_param(mic, "EmissiveScale", EMISSIVE_SCALE)
        # M_Decal carries only the albedo (RGB -> BaseColor, A -> Opacity), that same
        # alpha-masked self-illum path, and the world's fog; the surface it projects onto owns
        # the rest. The fog is bound here rather than as custom primitive data because a
        # UDecalComponent is a USceneComponent and carries none -- and needs none, since a decal
        # is only ever a world surface. Without it the decal would blend an unfogged patch into
        # the GBuffer its wall already fogged.
        if mat.decal:
            fog = fog_data(self.env)
            bl.set_vector_param(mic, "FogColor",
                                unreal.LinearColor(fog[0], fog[1], fog[2], fog[3]))
            bl.set_scalar_param(mic, "FogStart", fog[4])
            bl.set_scalar_param(mic, "FogInvRange", fog[5])
            return
        bump = tex(mat.bump)
        if bump:
            bl.set_tex_param(mic, "BumpMap", bump)
            bl.set_scalar_param(mic, "BumpAmount", BUMP_AMOUNT)
        if mat.env_cube:
            mask = tex(mat.env_mask)
            if mask:
                bl.set_tex_param(mic, "EnvMask", mask)
                mask_source = os.path.join(self.corpus_dir, mat.env_mask.replace("/", os.sep))
                bl.set_scalar_param(mic, "EnvMaskCoarseMip", png_coarse_mip(mask_source))
            # An unmasked reflective surface reflects uniformly; the master's own white
            # default stands in for the runtime's 1x1 white texture.
            bl.set_scalar_param(mic, "EnvStrength", ENV_STRENGTH)
            bl.set_scalar_param(mic, "RoughBase", ROUGH_BASE)
            bl.set_scalar_param(mic, "RoughReflect", ROUGH_REFLECT)
            bl.set_scalar_param(mic, "SpecBase", SPEC_BASE)
            # $envmaptint splits two ways (docs/vtmb/reflections.md). Grey -- 362 of the game's
            # 2,610 reflective materials -- is a reflection-strength dim-down, so its luma
            # scales the specular level. Chromatic (102) names a metal, and a metal's
            # reflection colour lives in BaseColor, which is what MetalMask + EnvTint drive;
            # specular is ignored once Metallic is up, so the two paths do not overlap.
            if mat.chromatic:
                bl.set_scalar_param(mic, "MetalMask", 1.0)
                bl.set_vector_param(mic, "EnvTint", unreal.LinearColor(
                    mat.env_tint[0], mat.env_tint[1], mat.env_tint[2], 1.0))
            else:
                bl.set_scalar_param(mic, "SpecReflect", SPEC_REFLECT * mat.tint_luma)
        if mat.wet:
            bl.set_scalar_param(mic, "WetnessDriven", 1.0)
            bl.set_scalar_param(mic, "WetnessScale", mat.wetness_scale)
            if self.map == "sm_hub_1":
                source_cube = self.cubemaps.get(mat.env_cube)
                if not source_cube:
                    raise SystemExit(
                        "[bake] wet material %s has no imported SourceCube %r"
                        % (mat.name, mat.env_cube))
                bl.set_tex_param(mic, "SourceCube", source_cube)
                bl.set_static_switch_param(mic, "WetnessUsesSourceCube", True)
            if self.weather and self.rain_height:
                bounds = self.weather["world_bounds_cm"]
                minimum, maximum = bounds["min"], bounds["max"]
                height = self.weather["height_texture"]
                bl.set_tex_param(mic, "RainHeightTexture", self.rain_height)
                bl.set_scalar_param(mic, "RainBoundsMinX", minimum[0])
                bl.set_scalar_param(mic, "RainBoundsMinY", minimum[1])
                bl.set_scalar_param(mic, "RainBoundsSizeX", maximum[0] - minimum[0])
                bl.set_scalar_param(mic, "RainBoundsSizeY", maximum[1] - minimum[1])
                bl.set_scalar_param(mic, "RainHeightMinZ", height["min_z_cm"])
                bl.set_scalar_param(mic, "RainHeightZScale", height["z_scale_cm"])
        tex2 = tex(mat.base_tex2)
        if tex2:
            bl.set_tex_param(mic, "BaseTex2", tex2)
            bl.set_scalar_param(mic, "BlendAmount", 1.0)

    def stage_materials(self):
        for mats, mat_pkg, tex_pkg in self._material_sets():
            if not mats:
                pruned = bl.prune_package(mat_pkg, set(), self.prune_scope)
                self.tracker.pruned("materials", pruned)
                continue
            start = time.time()
            made = 0
            wanted = set()
            for key, mat in sorted(mats.items()):
                name = "MI_" + bl.safe_name(key)
                object_path = "%s/%s" % (mat_pkg, name)
                if self.tracker.register(
                        "materials", object_path, self._material_recipe(mat, tex_pkg),
                        expected_class="MaterialInstanceConstant"):
                    mic = bl.make_material_instance(name, mat_pkg, self._master_for(mat))
                    if not mic:
                        raise SystemExit("[bake] material instance failed: %s" % object_path)
                    self._bind(mic, mat, tex_pkg)
                    self.saved.append(object_path)
                    self.tracker.built("materials")
                else:
                    mic = unreal.EditorAssetLibrary.load_asset(object_path)
                    if not mic:
                        raise SystemExit("[bake] cached material could not be loaded: %s" % object_path)
                self.materials[(mat_pkg, key)] = mic
                wanted.add(name)
                made += 1
            # This stage authors a package's whole material set in one pass, so anything else
            # left in it is from an earlier bake of a different export -- an unreferenced asset
            # the level would never load but the registry still carries.
            pruned = bl.prune_package(mat_pkg, wanted, self.prune_scope)
            self.tracker.pruned("materials", pruned)
            log("materials: %d into %s%s (%.1fs)" % (
                made, mat_pkg, ", %d stale pruned" % pruned if pruned else "",
                time.time() - start))
        if self.weather:
            # The rain master and system are tracked authored assets
            # (Content/ElysiumAuthored/VFX); the bake instances the master per map and never
            # writes either asset -- the runtime binds this map's instances through the system's
            # material user parameters (ElysiumMapActorWeather.cpp).
            master = unreal.load_asset(
                "/Game/ElysiumAuthored/VFX/M_ElysiumRain.M_ElysiumRain")
            if not master or not self.rain_height:
                fail("the authored rain master or the map height texture is missing")
                return
            bounds = self.weather["world_bounds_cm"]
            minimum, maximum = bounds["min"], bounds["max"]
            height = self.weather["height_texture"]
            rain_path = "%s/MI_ElysiumRain" % self.weather_pkg
            mist_path = "%s/MI_ElysiumRainMist" % self.weather_pkg
            rain_recipe = {
                "master": _asset_path(master),
                "height": "%s/T_RainHeight" % self.weather_pkg,
                "weather": self.weather,
                "layers": ("streaks", "mist"),
            }

            def _make_rain_mic(name, path, layer):
                if self.tracker.register(
                        "materials", path, {**rain_recipe, "layer": layer},
                        expected_class="MaterialInstanceConstant"):
                    mic = bl.make_material_instance(name, self.weather_pkg, master)
                    if not mic:
                        raise SystemExit("[bake] weather rain material instance failed: %s" % path)
                    bl.set_tex_param(mic, "RainHeightTexture", self.rain_height)
                    bl.set_scalar_param(mic, "RainBoundsMinX", minimum[0])
                    bl.set_scalar_param(mic, "RainBoundsMinY", minimum[1])
                    bl.set_scalar_param(mic, "RainBoundsSizeX", maximum[0] - minimum[0])
                    bl.set_scalar_param(mic, "RainBoundsSizeY", maximum[1] - minimum[1])
                    bl.set_scalar_param(mic, "RainHeightMinZ", height["min_z_cm"])
                    bl.set_scalar_param(mic, "RainHeightZScale", height["z_scale_cm"])
                    bl.set_scalar_param(mic, "RainLayer", layer)
                    self.saved.append(path)
                    self.tracker.built("materials")
                    return mic
                mic = unreal.EditorAssetLibrary.load_asset(path)
                if not mic:
                    raise SystemExit("[bake] cached rain material could not be loaded: %s" % path)
                return mic

            _make_rain_mic("MI_ElysiumRain", rain_path, 0.0)
            _make_rain_mic("MI_ElysiumRainMist", mist_path, 1.0)
        else:
            rain_path = "%s/MI_ElysiumRain" % self.weather_pkg
            mist_path = "%s/MI_ElysiumRainMist" % self.weather_pkg
            pruned = 0
            for path in (rain_path, mist_path):
                if unreal.EditorAssetLibrary.does_asset_exist(path):
                    bl.delete_owned_asset(path)
                    pruned += 1
            if pruned:
                self.tracker.pruned("materials", pruned)
        log("materials: %s" % self.tracker.summary("materials"))

    def resolve_textures(self):
        """Load the textures this scope's own material instances bind.

        Only the sets it AUTHORS: a map instances the materials that stamp its cubemap, fog or
        weather, and those need their textures in hand. Every other material it draws is already a
        finished instance in the corpus package, so pulling that material's textures into memory
        would load most of the install's 8,000 texture assets to bind nothing.
        """
        for mats, _mat_pkg, _tex_pkg in self._material_sets():
            for name, _, _ in self._texture_jobs(mats, self.corpus_dir, self.shared_tex_pkg):
                if (self.shared_tex_pkg, name) in self.textures:
                    continue
                path = "%s/%s" % (self.shared_tex_pkg, name)
                if unreal.EditorAssetLibrary.does_asset_exist(path):
                    self.textures[(self.shared_tex_pkg, name)] = (
                        unreal.EditorAssetLibrary.load_asset(path))
        if self.map == "sm_hub_1":
            for mat in self.world_mats.values():
                if not mat.wet or mat.env_cube in self.cubemaps:
                    continue
                name = "TC_" + bl.safe_name(mat.env_cube)
                path = "%s/%s" % (self.cube_pkg, name)
                if unreal.EditorAssetLibrary.does_asset_exist(path):
                    self.cubemaps[mat.env_cube] = unreal.EditorAssetLibrary.load_asset(path)
        if self.weather and not self.rain_height:
            path = "%s/T_RainHeight" % self.weather_pkg
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                self.rain_height = unreal.EditorAssetLibrary.load_asset(path)

    def resolve_materials(self):
        """Load already-baked material instances into the lookup, so a mesh or level stage can run
        without re-authoring the materials it binds.

        Two sources: the sets this scope authors, and every corpus material it merely draws --
        the shared world surfaces and every prop material. A shared instance the corpus has not
        baked yet is a named failure at bind time, not a silently grey surface.
        """
        for mats, mat_pkg, _ in self._material_sets():
            for key in mats:
                if (mat_pkg, key) in self.materials:
                    continue
                path = "%s/MI_%s" % (mat_pkg, bl.safe_name(key))
                if unreal.EditorAssetLibrary.does_asset_exist(path):
                    self.materials[(mat_pkg, key)] = unreal.EditorAssetLibrary.load_asset(path)
        shared = set(self._shared_material_keys()) | set(self._all_prop_mats())
        missing = []
        for key in sorted(shared):
            if (self.shared_mat_pkg, key) in self.materials:
                continue
            path = "%s/%s" % (self.shared_mat_pkg, SC.material_asset(key))
            asset = (unreal.EditorAssetLibrary.load_asset(path)
                     if unreal.EditorAssetLibrary.does_asset_exist(path) else None)
            if asset is None:
                missing.append(key)
            else:
                self.materials[(self.shared_mat_pkg, key)] = asset
        if missing:
            # Continuing would author meshes and a level whose slots bind nothing, and the
            # per-asset receipts would then freeze that unbound state as current.
            fail("%d shared material(s) are not baked (run: uv run elysium export bundle corpus): %s"
                 % (len(missing), ", ".join(missing[:8])))
            raise SystemExit(1)

    def _emit(self, stage, asset_path, sections, names, materials, nanite, phys=None,
              collision=True):
        """Build one StaticMesh from prepared sections.
        Returns (triangles kept, dropped, simple collision shapes)."""
        want = sum(len(s[4]) for s in sections) // 3
        recipe = {
            "sections": sections,
            "slot_names": [bl.safe_name(name) for name in names],
            "materials": [_asset_path(material) for material in materials],
            "nanite": bool(nanite),
            "collision": bool(collision),
            "physics": phys,
        }
        if not self.tracker.register(
                stage, asset_path, recipe, expected_class="StaticMesh"):
            return want, 0, len(phys["hulls"]) if phys else 0
        mesh = bl.build_dynamic_mesh(sections)
        got = bl.mesh_triangle_count(mesh)
        static_mesh = bl.create_static_mesh(
            mesh, asset_path, materials, [bl.safe_name(n) for n in names], nanite=nanite,
            collision=collision or phys is not None)
        if not static_mesh:
            fail("mesh build failed: %s" % asset_path)
            return 0, want, 0
        # A physics prop simulates, so it needs real simple collision -- VtMB's own convex
        # hulls. Everything else makes its render triangles the collision.
        shapes = bl.set_phy_collision(static_mesh, phys) if phys else 0
        if not phys and collision:
            bl.set_complex_collision(static_mesh)
        self.saved.append(asset_path)
        self.tracker.built(stage)
        return got, want - got, shapes

    # ------------------------------------------------------------------- world

    def _chunk_world(self, model, mats, blend, cell_cm):
        """Bin triangles into (cell, nanite-able) buckets. Returns
        {(cx, cy, cz, opaque): {material name: [tri indices]}}."""
        buckets = {}
        positions = model.positions
        for name, indices in model.groups.items():
            mat = mats.get(name)
            opaque = mat.opaque if mat else True
            for base in range(0, len(indices), 3):
                i0, i1, i2 = indices[base], indices[base + 1], indices[base + 2]
                ax, ay, az = positions[i0]
                bx, by, bz = positions[i1]
                cx, cy, cz = positions[i2]
                key = (int((ax + bx + cx) / 3.0 // cell_cm),
                       int((ay + by + cy) / 3.0 // cell_cm),
                       int((az + bz + cz) / 3.0 // cell_cm),
                       opaque)
                buckets.setdefault(key, {}).setdefault(name, []).extend((i0, i1, i2))
        return buckets

    def _sections(self, model, normals, blend, groups, pivot):
        """Turn {material: [indices]} into the per-slot buffers build_dynamic_mesh wants,
        re-based on `pivot`.

        Every triangle gets its own three vertices. FDynamicMesh3 rejects any triangle that
        would make an edge non-manifold, and VtMB geometry is soup -- a prop model shares
        vertices freely, so an indexed append silently loses faces. An unshared soup can never
        be non-manifold. Shading is unaffected because `normals` was already accumulated over
        the model's original shared indices."""
        sections = []
        names = []
        px, py, pz = pivot
        for name in sorted(groups.keys()):
            indices = groups[name]
            positions, norms, uvs, colors, tris = [], [], [], [], []
            for local, idx in enumerate(indices):
                x, y, z = model.positions[idx]
                positions.append((x - px, y - py, z - pz))
                norms.append(normals[idx])
                uvs.append(model.uvs[idx])
                if blend:
                    colors.append(blend[idx] if idx < len(blend) else 0.0)
                tris.append(local)
            sections.append((positions, norms, uvs, colors, tris))
            names.append(name)
        return sections, names

    def stage_world(self):
        model = self.world_obj
        start = time.time()
        normals = bl.vertex_normals(model.positions, model.groups.values())
        log("world normals: %.1fs" % (time.time() - start))

        buckets = self._chunk_world(model, self.world_mats, self.blend, CELL_CM)
        bl.ensure_dir(self.mesh_pkg)
        built = 0
        tris = 0
        dropped = 0
        wanted = set()
        start = time.time()
        for key in sorted(buckets.keys()):
            cx, cy, cz, opaque = key
            pivot = ((cx + 0.5) * CELL_CM, (cy + 0.5) * CELL_CM, (cz + 0.5) * CELL_CM)
            sections, names = self._sections(model, normals, self.blend, buckets[key], pivot)
            asset_path = "%s/SM_World_%s%d_%d_%d" % (
                self.mesh_pkg, "" if opaque else "T_", cx, cy, cz)
            materials = [self.material_for(name) for name in names]
            kept, lost, _ = self._emit(
                "world", asset_path, sections, names, materials, nanite=opaque)
            tris += kept
            dropped += lost
            built += 1 if kept else 0
            if kept:
                wanted.add(asset_path.rsplit("/", 1)[-1])
        pruned = bl.prune_package_prefix(self.mesh_pkg, "SM_World_", wanted, self.prune_scope)
        self.tracker.pruned("world", pruned)
        log("world: %d chunk meshes / %d tris / %d dropped / %d stale pruned (%.1fs)" % (
            built, tris, dropped, pruned, time.time() - start))

        # Brush entities are one local-pivot mesh each. They are never placed in the baked
        # level: the runtime attaches them to the convex entity body that owns movement,
        # collision, hiding and teardown.
        bl.ensure_dir(self.brush_pkg)
        wanted = set()
        brush_tris = 0
        brush_dropped = 0
        for stem, brush in sorted(self.brush_models.items()):
            normals = bl.vertex_normals(brush.positions, brush.groups.values())
            blend = self.brush_blend.get(stem, [])
            sections, names = self._sections(
                brush, normals, blend, brush.groups, (0.0, 0.0, 0.0))
            asset_path = "%s/SM_%s" % (self.brush_pkg, stem)
            materials = [self.material_for(name) for name in names]
            nanite = all(self.world_mats.get(name).opaque
                         if self.world_mats.get(name) else True for name in names)
            kept, lost, _ = self._emit(
                "world", asset_path, sections, names, materials, nanite=nanite,
                collision=False)
            if kept:
                wanted.add("SM_%s" % stem)
            brush_tris += kept
            brush_dropped += lost
        pruned = bl.prune_package(self.brush_pkg, wanted, self.prune_scope)
        self.tracker.pruned("world", pruned)
        log("brushes: %d meshes / %d tris / %d dropped / %d stale pruned" % (
            len(wanted), brush_tris, brush_dropped, pruned))
        log("world assets: %s" % self.tracker.summary("world"))

    # --------------------------------------------------------------------- sky

    def stage_sky(self):
        sky_path = os.path.join(self.dir, "%s_sky.obj" % self.map)
        if not os.path.isfile(sky_path):
            pruned = bl.prune_package_prefix(self.mesh_pkg, "SM_Sky_", set(), self.prune_scope)
            self.tracker.pruned("sky", pruned)
            log("sky: no _sky.obj, %d stale pruned" % pruned)
            return
        start = time.time()
        model = bl.read_obj(sky_path)
        normals = bl.vertex_normals(model.positions, model.groups.values())
        buckets = self._chunk_world(model, self.world_mats, [], CELL_CM * 4)
        bl.ensure_dir(self.mesh_pkg)
        built = 0
        tris = 0
        dropped = 0
        wanted = set()
        cell = CELL_CM * 4
        for key in sorted(buckets.keys()):
            cx, cy, cz, opaque = key
            pivot = ((cx + 0.5) * cell, (cy + 0.5) * cell, (cz + 0.5) * cell)
            sections, names = self._sections(model, normals, [], buckets[key], pivot)
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

    # ------------------------------------------------------------------- props

    def stage_props(self):
        bl.ensure_dir(self.shared_mesh_pkg)
        start = time.time()
        built = 0
        tris = 0
        dropped = 0
        unbound = 0
        error_slots = 0
        error_props = 0
        phys_meshes = 0
        phys_shapes = 0
        wanted = set()
        stems = sorted(self.prop_models.keys())
        for offset in range(0, len(stems), PROP_BATCH):
            batch = stems[offset:offset + PROP_BATCH]
            for stem in batch:
                model = self.prop_models[stem]
                mats = self.prop_mats.get(stem, {})
                if not model.groups:
                    continue
                normals = bl.vertex_normals(model.positions, model.groups.values())
                sections, names = self._sections(
                    model, normals, [], model.groups, (0.0, 0.0, 0.0))
                asset_path = "%s/%s" % (self.shared_mesh_pkg, SC.mesh_asset(stem))
                materials = []
                missed = 0
                for name in names:
                    mat = mats.get(name)
                    if mat is None:
                        # No `mat` line for this slot: the exporter resolved no `.vmt` for its
                        # material name, which is the engine's own routine miss.
                        materials.append(self.error_bind(name, stem))
                        missed += 1
                        continue
                    material = self.materials.get((self.shared_mat_pkg, mat.material_key))
                    if material is None:
                        # The material is defined but its instance is not on the mount, so the
                        # corpus is unbaked or stale. A null slot saves a mesh that renders
                        # Unreal's own default, and its receipt would then freeze that unbound
                        # state as current. The prop is left unbuilt and unreceipted so the next
                        # run retries it.
                        fail("%s: slot %r resolves no loaded material for key %r"
                             % (stem, name, mat.material_key))
                        materials = None
                        break
                    materials.append(material)
                if materials is None:
                    unbound += 1
                    # Keep the prop's already-baked package on the mount even though this run
                    # will not receipt it -- the prune below deletes every SM_ name not in
                    # `wanted`, and an unresolved slot must retry next run, not go missing now.
                    wanted.add(asset_path.rsplit("/", 1)[-1])
                    continue
                if missed:
                    error_slots += missed
                    error_props += 1
                # A prop model whose materials are all opaque/masked can be Nanite; a mixed model
                # cannot (Nanite is a whole-mesh setting), so it falls back wholesale. The test
                # spans the alternate skin families too: a skin swaps whole material instances, so
                # a family that brings in a translucent/additive surface would leave one on a
                # Nanite mesh -- and outside the editor no permutation can be compiled, which
                # renders default grey.
                skinned = set(names) | {rep for remap in self.prop_skins.get(stem, {}).values()
                                        for rep in remap.values()}
                nanite = all((mats[n].opaque if n in mats else True) for n in skinned)
                phys = self.prop_phys.get(stem)
                kept, lost, shapes = self._emit("props", asset_path, sections, names, materials,
                                                nanite=nanite, phys=phys)
                tris += kept
                dropped += lost
                built += 1 if kept else 0
                if kept:
                    wanted.add(asset_path.rsplit("/", 1)[-1])
                if phys:
                    phys_meshes += 1
                    phys_shapes += shapes
                    if shapes != len(phys["hulls"]):
                        fail("%s: %d hulls in the sidecar but %d collision shapes"
                             % (stem, len(phys["hulls"]), shapes))
            # The batch's source geometry is spent once its meshes are built, so the collector can
            # reclaim it. Textures and material instances stay resident: every later model binds
            # them, and so do the skin set and the level stage.
            for stem in batch:
                del self.prop_models[stem]
            if offset + PROP_BATCH < len(stems):
                if self.checkpoint("props %d/%d" % (offset + len(batch), len(stems))):
                    raise SystemExit("[bake] prop checkpoint could not save every mesh")
        pruned = bl.prune_package_prefix(self.shared_mesh_pkg, "SM_", wanted, self.prune_scope)
        self.tracker.pruned("props", pruned)
        log("props: %d meshes / %d tris / %d dropped / %d physics (%d convex shapes) / "
            "%d stale pruned%s%s (%.1fs)"
            % (built, tris, dropped, phys_meshes, phys_shapes, pruned,
               " / %d slots error-bound across %d props" % (error_slots, error_props)
               if error_slots else "",
               " / %d unbuilt for an unbound material" % unbound if unbound else "",
               time.time() - start))
        self._author_skin_set()
        log("prop assets: %s" % self.tracker.summary("props"))

    def _author_skin_set(self):
        """Author the map's UElysiumPropSkinSet from the `.skins` sidecars, resolving each family
        material to the instance the material stage already made. The runtime looks an override up
        by mesh material *slot* name, which is the authored material's name -- the same key the
        sidecar uses -- so no naming rule has to be reproduced on either side."""
        object_path = "%s/%s" % (self.shared_mesh_pkg, SC.PROP_SKINS_ASSET)
        if not self.prop_skins:
            if unreal.EditorAssetLibrary.does_asset_exist(object_path):
                bl.delete_owned_asset(object_path)
                self.tracker.pruned("props", 1)
            return
        models, overrides, unresolved = [], 0, 0
        for stem in sorted(self.prop_skins):
            families = self.prop_skins[stem]
            mats = self.prop_mats.get(stem, {})
            rows = [{} for _ in range(max(families) + 1)]   # index = VtMB's skin number
            for family, remap in families.items():
                for slot, rep in remap.items():
                    mat = mats.get(rep)
                    # A family repaint resolves like any other slot: no `.vmt` for the name is the
                    # engine's miss and takes the error material; a defined material with no
                    # instance on the mount is a stale corpus and is named instead.
                    mic = (self.materials.get((self.shared_mat_pkg, mat.material_key)) if mat
                           else self.error_bind(rep, stem))
                    if mic is None:
                        unresolved += 1
                        log("  ! %s skin %d: no material instance for %r" % (stem, family, rep))
                        continue
                    # Keyed by the mesh's material *slot* name, which is safe_name of the authored
                    # material (create_static_mesh names the slots that way) -- so the runtime
                    # looks up what the asset actually carries and reproduces no naming rule.
                    rows[family][bl.safe_name(slot)] = mic
                    overrides += 1
            models.append((stem, rows))
        recipe_models = []
        for stem, rows in models:
            recipe_models.append((stem, [
                {slot: _asset_path(material) for slot, material in sorted(row.items())}
                for row in rows
            ]))
        if not self.tracker.register(
                "props", object_path, {"models": recipe_models},
                expected_class="ElysiumPropSkinSet"):
            log("prop skins: cached %d models / %d overrides" % (len(models), overrides))
            return
        asset = bl.make_skin_set(SC.PROP_SKINS_ASSET, self.shared_mesh_pkg, models)
        if asset is None:
            fail("prop skin set failed: %s" % self.shared_mesh_pkg)
            return
        self.saved.append(object_path)
        self.tracker.built("props")
        log("prop skins: %d models / %d overrides%s" % (
            len(models), overrides, ", %d unresolved" % unresolved if unresolved else ""))

    # ------------------------------------------------------------------ lights

    def _place_lights(self, actors, sky_scale=16.0, sky_origin=(0.0, 0.0, 0.0)):
        """One light actor per WORLDLIGHTS `.lights` line, with UElysiumLightRig's calibration
        applied verbatim: 16 fields `type x y z dx dy dz r g b radius stopdot stopdot2 _ style sky`,
        soft non-inverse-square falloff, specular killed, everything Movable.

        Lightstyle animation (field 15) has no baked equivalent -- a styled source is placed at
        its unanimated base intensity.

        Field 16 marks a source inside the 3D-skybox miniature. Those never lit the playable
        world, but they were not dead either -- VtMB's light cache read exactly those lump-15
        rows to light the miniature's props -- so they are carried into the sky transform
        (position scaled, reach scaled by the same factor) rather than deleted, per the owner
        call of 2026-07-26. Deleting them would un-light authored content."""
        path = os.path.join(self.dir, "%s.lights" % self.map)
        if not os.path.isfile(path):
            return 0, 0, None
        placed = sky_placed = 0
        sky_ambient = None
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for index, line in enumerate(handle):
                tok = line.split()
                if len(tok) < 15:
                    continue
                kind = int(tok[0])
                is_sky = len(tok) >= 16 and int(tok[15]) != 0
                pos = [float(tok[1]), float(tok[2]), float(tok[3])]
                if is_sky:
                    pos = [sky_scale * (pos[i] - sky_origin[i]) for i in range(3)]
                origin = unreal.Vector(*pos)
                direction = unreal.Vector(float(tok[4]), float(tok[5]), float(tok[6]))
                rgb = (float(tok[7]), float(tok[8]), float(tok[9]))
                radius_cm = float(tok[10])
                stopdot = float(tok[11])
                stopdot2 = float(tok[12])

                mag = max(rgb)
                if mag <= 0.0:
                    continue
                color = unreal.LinearColor(rgb[0] / mag, rgb[1] / mag, rgb[2] / mag, 1.0)

                # Type 5 skyambient is not a light; it tints the SkyLight. FIRST wins, by
                # lump-15 order: VRAD resolves the sky ambient once, globally, first-entity-wins
                # and stamps that value on every type-5 row, and the engine's own multi-
                # light_environment rule is first-wins too (RE-A3/RE-A5). Overwriting in the loop
                # -- last-wins -- read the wrong row on the 5 maps with several.
                if kind == 5:
                    if sky_ambient is None:
                        sky_ambient = (color, mag)
                    continue

                reach = (radius_cm if radius_cm > 1.0 else FALLBACK_RADIUS_CM) * RADIUS_SCALE
                # A miniature light's reach is authored in miniature units, so it scales with the
                # geometry it lights or it lights a 16th of what it did. The floor keeps a
                # degenerate authored radius from collapsing to nothing after the scale.
                if is_sky:
                    reach = max(reach * sky_scale, MIN_SKY_REACH_CM)
                soft = min(mag * POINT_SPOT_SCALE, MAX_BRIGHTNESS)

                if kind in (0, 1):
                    actor = actors.spawn_actor_from_class(unreal.PointLight, origin)
                    component = actor.point_light_component if actor else None
                    if component:
                        _make_movable(component)
                        component.set_attenuation_radius(reach)
                        component.set_intensity(soft)
                        # Texlights stay shadowless, as the rig has them.
                        component.set_cast_shadows(kind != 0)
                elif kind == 2:
                    inner = math.degrees(math.acos(max(-1.0, min(1.0, stopdot))))
                    outer = math.degrees(math.acos(max(-1.0, min(1.0, stopdot2))))
                    inner = max(1.0, min(80.0, inner))
                    outer = max(1.0, min(80.0, outer))
                    actor = actors.spawn_actor_from_class(
                        unreal.SpotLight, origin, _dir_rotator(direction))
                    component = actor.spot_light_component if actor else None
                    if component:
                        _make_movable(component)
                        component.set_attenuation_radius(reach)
                        component.set_intensity(soft)
                        component.set_outer_cone_angle(outer)
                        component.set_inner_cone_angle(min(inner, outer))
                        component.set_cast_shadows(True)
                elif kind == 3:
                    actor = actors.spawn_actor_from_class(
                        unreal.DirectionalLight, origin, _dir_rotator(direction))
                    # ADirectionalLight exposes only ALight's generic component property.
                    component = actor.light_component if actor else None
                    if component:
                        _make_movable(component)
                        component.set_intensity(max(mag * SUN_SCALE_LUX, 0.01))
                        component.set_cast_shadows(True)
                else:
                    continue
                if not actor or not component:
                    continue
                if kind in (0, 1, 2):
                    # VtMB light is ~flat within its authored radius, so gentle-exponent
                    # falloff, not inverse-square (docs/architecture/rendering-perf.md calibration).
                    component.set_editor_property("use_inverse_squared_falloff", False)
                    component.set_editor_property("light_falloff_exponent", FALLOFF_EXPONENT)
                component.set_light_color(color)
                # The VtMB world is pure Lambert -- kill specular so lights do not glare.
                component.set_editor_property("specular_scale", SPECULAR_SCALE)
                actor.set_actor_label("Light_%d_%s%s" % (
                    index, {0: "tex", 1: "point", 2: "spot", 3: "sun"}[kind],
                    "_sky" if is_sky else ""))
                # The line index is the binding UElysiumLightRig::Adopt needs: it re-derives
                # every intensity and reach from this row at load, so the values written above
                # are only what the level looks like in the editor before the game runs.
                actor.tags = [TAG_LIGHT, "elysium.src=%d" % index]
                actor.set_folder_path("Sky/Lights" if is_sky else "Lights")
                placed += 1
                sky_placed += is_sky
        return placed, sky_placed, sky_ambient

    def _place_sky(self, actors, sky_ambient):
        """The sky light and the map's height fog.

        The sky light is placed empty here and handed its real cubemap at load
        (AElysiumMapActor::ApplyEnvironment), because the cube is assembled from the six
        exported sky face images rather than being an asset. What matters is that it is a
        cubemap sky light at all: that is what gives Lumen sky occlusion, so an interior goes
        dark because it cannot see the sky instead of being washed by a constant fill through
        solid walls. Lower hemisphere black, or the sky would light the world's undersides and
        defeat the occlusion.

        Its INTENSITY is likewise the runtime's to set (C1/C2): the level comes from the map's
        type-5 `emit_skyambient` magnitude divided by the cube's own mean radiance, and the cube
        does not exist until load. What is written here is that magnitude alone, so the actor
        carries the map's real data in the editor rather than a placeholder constant -- 0 on the
        83 maps with no sky pair, which is the policy, not an absence."""
        actor = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 0.0))
        if actor:
            color, mag = sky_ambient if sky_ambient else (None, 0.0)
            component = actor.light_component
            component.set_mobility(unreal.ComponentMobility.MOVABLE)
            component.set_editor_property("source_type",
                                          unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
            component.set_editor_property("cubemap", None)
            component.set_editor_property("lower_hemisphere_is_black", True)
            component.set_editor_property("intensity", mag)
            component.set_light_color(color or SKYLIGHT_FALLBACK_COLOR)
            actor.set_actor_label("SkyLight")
            actor.tags = [TAG_SKYLIGHT]
            actor.set_folder_path("Environment")

        # The height fog is NOT the map's distance fog -- that is a per-primitive material term
        # now (B8b), because the world and the miniature carry two different fogs and share
        # screen depth. What is left here is the VOLUMETRIC layer: participating media the map's
        # hundreds of dynamic lights shaft through, which is the Presentation-layer modernization
        # D4 sanctioned and which no per-surface term can produce. Its analytic contribution is
        # a residue rather than a design: the engine divides both FogDensity and FogHeightFalloff
        # by 1000, so at 3/end this integrates to under 0.2% across a whole map, and the backdrop
        # is cut off before it anyway (AElysiumMapActor::FogCutoffCm). Calibrating the volumetric
        # layer for its own sake is open -- at this density it, too, is near-invisible.
        env = self.env
        if env.get("fog", ["0"])[0] == "1":
            fog = actors.spawn_actor_from_class(unreal.ExponentialHeightFog,
                                                unreal.Vector(0.0, 0.0, 0.0))
            if fog:
                component = fog.component
                start_cm = float(env.get("fogstart", [0.0])[0])
                end_cm = max(float(env.get("fogend", [1.0])[0]), start_cm + 1.0)
                # Same decode as the per-primitive term, so the two layers agree on what a
                # colour means rather than sitting 30x apart.
                color = fog_color(env)
                component.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(
                    color[0], color[1], color[2], 1.0))
                component.set_editor_property("start_distance", start_cm)
                component.set_editor_property("fog_height_falloff", 0.02)
                component.set_editor_property(
                    "fog_density", max(0.0001, min(0.05, 3.0 / end_cm)))
                # Volumetric fog turns the map's hundreds of dynamic lights into real shafts and
                # haze rather than a flat depth tint (a Presentation-layer call).
                component.set_editor_property("enable_volumetric_fog", True)
                component.set_editor_property("volumetric_fog_scattering_distribution", 0.2)
                component.set_editor_property("volumetric_fog_extinction_scale", 1.0)
                fog.set_actor_label("HeightFog")
                fog.tags = [TAG_FOG]
                fog.set_folder_path("Environment")

        # The map's Lumen art-direction volume (D3, sky-ambience.md). It ships
        # NEUTRAL -- unbound, and with not a single bOverride_ set -- so it changes no pixel.
        # What it provides is the place a per-map value goes when C4/C5 measure a deficit that
        # justifies one: Skylight Leaking (+ its full-leaking distance) as the sanctioned
        # replacement for load-bearing author fill where Lumen has nothing to bounce off, and
        # Lumen Diffuse Color Boost as the bounce-strength A/B. Landing the mechanism now
        # means such a decision is one number in one place, not new plumbing under time
        # pressure. Ambient Cubemap is deliberately NOT among them: a flat occlusion-ignoring
        # term is the contrast-killer both Epic and the direction charter warn against.
        ppv = actors.spawn_actor_from_class(unreal.PostProcessVolume,
                                            unreal.Vector(0.0, 0.0, 0.0))
        if ppv:
            ppv.set_editor_property("unbound", True)
            ppv.set_editor_property("priority", 0.0)
            ppv.set_actor_label("PostProcess")
            ppv.tags = [TAG_PPV]
            ppv.set_folder_path("Environment")

    # ------------------------------------------------------------------- level

    def _level_recipe(self):
        def assets(package, prefixes=()):
            values = unreal.EditorAssetLibrary.list_assets(
                package, recursive=False, include_folder=False)
            paths = [value.split(".", 1)[0] for value in values]
            if prefixes:
                paths = [path for path in paths
                         if path.rsplit("/", 1)[-1].startswith(prefixes)]
            return sorted(paths)

        return {
            "placement": bake_cache.level_sidecar_recipe(Path(self.dir), self.map),
            "world_sky_meshes": assets(self.mesh_pkg, ("SM_World_", "SM_Sky_")),
            # Only the models this map places. Enumerating the whole shared package would make
            # every level stale whenever any of the corpus's 3,000 meshes changed, which is the
            # opposite of what one asset per source is for.
            "props": sorted(
                "%s/%s" % (self.shared_mesh_pkg, SC.mesh_asset(stem))
                for stem in self.prop_mats),
            "brushes": assets(self.brush_pkg, ("SM_",)),
            "materials": sorted(
                _asset_path(material) for material in self.materials.values() if material),
            "prop_skins": self.prop_skins,
            "cell_cm": CELL_CM,
            "profiles": [PROFILE_PICK_ONLY, PROFILE_PROP_SOLID],
        }

    def stage_level(self):
        start = time.time()
        map_path = "%s/%s" % (self.pkg, self.map)
        if not self.tracker.register(
                "level", map_path, self._level_recipe(), expected_class="World"):
            log("level: reused %s" % map_path)
            return True
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        if not world:
            fail("new_blank_map returned null")
            return False
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

        # The 3D skybox is a miniature authored at 1/scale in a corner of the map, with the
        # sky_camera origin standing for world (0,0,0): world(v) = scale * (v - origin). A sky
        # mesh's verts are already re-based on its cell pivot, so the actor takes uniform scale
        # and sits at scale * (pivot - origin).
        sky_scale, sky_origin = self._read_sky()

        # The world's fog and the miniature's, from their two owners (`worldspawn` and
        # `sky_camera`, RE-A8). Each primitive carries its own set as custom primitive data,
        # which is the only way to fog two things differently when they share screen depth.
        world_fog = fog_data(self.env)
        sky_fog = fog_data(self.env, "sky")

        placed = 0
        for asset_path in sorted(unreal.EditorAssetLibrary.list_assets(
                self.mesh_pkg, recursive=False, include_folder=False)):
            static_mesh = unreal.EditorAssetLibrary.load_asset(asset_path)
            if not static_mesh:
                continue
            name = asset_path.rsplit("/", 1)[-1].split(".")[0]
            parts = name.split("_")
            is_sky = name.startswith("SM_Sky")
            cell = CELL_CM * (4 if is_sky else 1)
            cx, cy, cz = (int(v) for v in parts[-3:])
            pivot = ((cx + 0.5) * cell, (cy + 0.5) * cell, (cz + 0.5) * cell)
            if is_sky:
                location = unreal.Vector(*[sky_scale * (pivot[i] - sky_origin[i])
                                           for i in range(3)])
            else:
                location = unreal.Vector(*pivot)
            actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, location)
            if not actor:
                continue
            actor.set_actor_label(name)
            component = actor.static_mesh_component
            component.set_static_mesh(static_mesh)
            component.set_collision_profile_name(PROFILE_PICK_ONLY)
            set_fog(component, sky_fog if is_sky else world_fog)
            actor.tags = [TAG_SKY if is_sky else TAG_WORLD]
            if is_sky:
                actor.set_actor_scale3d(unreal.Vector(sky_scale, sky_scale, sky_scale))
                # Blown up 16x the 3D skybox encloses the playable space, and a mesh that
                # overlaps the whole scene is the canonical hardware-ray-tracing cost -- Epic
                # names skyboxes explicitly. It is backdrop, so it casts nothing either.
                component.set_editor_property("visible_in_ray_tracing", False)
                component.set_cast_shadow(False)
            actor.set_folder_path("Sky" if is_sky else "World")
            placed += 1
        log("level: %d world/sky actors" % placed)

        props, sky_props = self._place_props(actors, sky_scale, sky_origin, world_fog, sky_fog)
        log("level: %d prop actors (%d in the 3D skybox)" % (props, sky_props))
        log("level: %d decal actors" % self._place_decals(actors))
        log("fog: world %s, 3D skybox %s" % (
            "%.0f->%.0fcm" % (world_fog[4], world_fog[4] + 1.0 / world_fog[5])
            if world_fog[5] else "off",
            "%.0f->%.0fcm" % (sky_fog[4], sky_fog[4] + 1.0 / sky_fog[5]) if sky_fog[5] else "off"))
        lights, sky_lights, sky_ambient = self._place_lights(actors, sky_scale, sky_origin)
        log("level: %d light actors (%d in the 3D skybox)" % (lights, sky_lights))
        self._place_sky(actors, sky_ambient)
        self._place_player_start(actors)

        if unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
            self.tracker.built("level")
            log("level: saved %s (%.1fs)" % (map_path, time.time() - start))
            return True
        else:
            fail("level save failed: %s" % map_path)
            return False

    def _read_env(self):
        """<map>.env as key -> [tokens]. Absent on a map with no environment sidecar at all."""
        env = {}
        path = os.path.join(self.dir, "%s.env" % self.map)
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    tok = line.split()
                    if len(tok) >= 2:
                        env[tok[0]] = tok[1:]
        return env

    def _read_sky(self):
        """(scale, origin) from the .sky sidecar. 16 / world origin is the Source default."""
        scale = 16.0
        origin = (0.0, 0.0, 0.0)
        path = os.path.join(self.dir, "%s.sky" % self.map)
        if not os.path.isfile(path):
            return scale, origin
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                tok = line.split()
                if len(tok) == 4 and tok[0] == "origin":
                    origin = (float(tok[1]), float(tok[2]), float(tok[3]))
                elif len(tok) == 2 and tok[0] == "scale":
                    scale = float(tok[1])
        return scale, origin

    def _place_props(self, actors, sky_scale=16.0, sky_origin=(0.0, 0.0, 0.0),
                     world_fog=None, sky_fog=None):
        """Place GAME_LUMP props in storage-equivalent static or authored-rest form."""
        path = os.path.join(self.dir, "%s.props" % self.map)
        if not os.path.isfile(path):
            return 0, 0
        placed = skinned = sky_placed = skeletal_placed = 0
        cache = {}
        index_path = os.path.join(OUT_ROOT, "npc", "npc_index.json")
        placed_index = {}
        index_version = 0
        if os.path.isfile(index_path):
            with open(index_path, "r", encoding="utf-8") as index_handle:
                index_doc = json.load(index_handle)
            index_version = int(index_doc.get("manifest_version", 0))
            placed_index = index_doc.get("placed_models", {})
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for placement_token, line in enumerate(handle):
                tok = line.split()
                if len(tok) < 9:
                    continue
                stem = tok[0]
                mesh = cache.get(stem)
                if mesh is None:
                    mesh = unreal.EditorAssetLibrary.load_asset(
                        "%s/%s" % (self.shared_mesh_pkg, SC.mesh_asset(stem)))
                    cache[stem] = mesh
                if not mesh:
                    if index_version >= 7:
                        raise RuntimeError("GAME_LUMP model %s has no baked static material/collision mesh" % stem)
                    continue
                # Field 11 marks a prop inside the 3D-skybox miniature: it is placed under the
                # same transform the sky world meshes take, `world(v) = scale * (v - origin)`,
                # rather than at its raw 1/scale coordinates in the middle of the playable map.
                is_sky = len(tok) >= 11 and int(tok[10]) != 0
                pos = [float(tok[1]), float(tok[2]), float(tok[3])]
                if is_sky:
                    pos = [sky_scale * (pos[i] - sky_origin[i]) for i in range(3)]
                location = unreal.Vector(*pos)
                rotation = unreal.Quat(float(tok[4]), float(tok[5]),
                                       float(tok[6]), float(tok[7])).rotator()
                # Field 0 is the map export's legacy OBJ/static-mesh stem.  The v7 catalogue is
                # intentionally keyed by the normalized full model-path stem, which field 11
                # carries without loss.  Keep the two identities separate: the legacy stem owns
                # this map's material/collision mesh; the path stem owns the shared skeletal body.
                model_path = tok[11] if len(tok) >= 12 else ""
                catalogue_stem = PM.model_stem(model_path) if model_path else stem
                record = placed_index.get(catalogue_stem) if index_version >= 7 else None
                if index_version >= 7 and record is None:
                    raise RuntimeError("GAME_LUMP model %s (%s) is absent from npc_index v7" %
                                       (stem, catalogue_stem))
                use_skeletal = bool(record and not record.get("static_equivalent", False))
                actor_class = unreal.ElysiumPlacedModelActor if use_skeletal else unreal.StaticMeshActor
                actor = actors.spawn_actor_from_class(actor_class, location, rotation)
                if not actor:
                    continue
                if use_skeletal:
                    model_path = record.get("model", model_path)
                    rest = PM.select_rest_label(model_path, record, placement_token)
                    skel = unreal.EditorAssetLibrary.load_asset(
                        "/ElysiumBaked/Props/%s/SK_%s" %
                        (catalogue_stem, catalogue_stem))
                    anim = unreal.EditorAssetLibrary.load_asset(
                        "/ElysiumBaked/Props/%s/A_%s" %
                        (catalogue_stem, bl.safe_name(rest)))
                    if not skel or not anim or not rest:
                        raise RuntimeError("GAME_LUMP model %s has no baked rest asset '%s'" %
                                           (stem, rest))
                    solid = int(tok[8]) != 0 and not is_sky
                    if not actor.configure_rest(skel, anim, mesh, solid):
                        raise RuntimeError("GAME_LUMP model %s refused rest configuration" % stem)
                    component = actor.skeletal_visual
                    proxy = actor.collision_proxy
                    proxy.set_collision_profile_name(PROFILE_PROP_SOLID if solid else PROFILE_PICK_ONLY)
                    skeletal_placed += 1
                else:
                    component = actor.static_mesh_component
                    component.set_static_mesh(mesh)
                # Field 9 is Source's own `solid` byte: a solid prop blocks, the rest is dressing.
                # A miniature prop is never solid whatever it says -- it is scenery the player can
                # never reach, and at 16x it would wall off the map.
                if not use_skeletal:
                    component.set_collision_profile_name(
                        PROFILE_PROP_SOLID if (int(tok[8]) != 0 and not is_sky) else PROFILE_PICK_ONLY)
                # Field 10 (DStaticPropV4.skin) names an alternate skin family. A GAME_LUMP prop is
                # not an entity and never changes skin, so the remap is baked into the placement as
                # material overrides rather than costing anything at runtime. Older 9-field exports
                # simply have no skin.
                if len(tok) >= 10 and int(tok[9]) != 0:
                    skinned += self._apply_prop_skin(component, mesh, stem, int(tok[9]))
                fog = sky_fog if is_sky else world_fog
                if fog:
                    set_fog(component, fog)
                if is_sky:
                    actor.set_actor_scale3d(unreal.Vector(sky_scale, sky_scale, sky_scale))
                    # Same reasoning as the sky world meshes: blown up 16x the miniature encloses
                    # the playable space, which is the canonical hardware-ray-tracing overlap cost,
                    # and it is backdrop, so it casts nothing.
                    component.set_editor_property("visible_in_ray_tracing", False)
                    component.set_cast_shadow(False)
                    sky_placed += 1
                actor.tags = [TAG_SKY if is_sky else TAG_PROP]
                actor.set_folder_path("Sky/Props" if is_sky else "Props")
                placed += 1
        if skinned:
            log("level: %d static props on an alternate skin" % skinned)
        if skeletal_placed:
            log("level: %d GAME_LUMP props held on authored skeletal rest poses" % skeletal_placed)
        return placed, sky_placed

    def _apply_prop_skin(self, component, mesh, stem, family):
        """Override the material of every slot this model's skin family repaints. Returns 1 when
        anything was applied. An out-of-range family is not an error -- Source's `skin` is an
        unclamped int, and a map can name a family the model does not have."""
        remap = self.prop_skins.get(stem, {}).get(family)
        if not remap:
            return 0
        slots = [str(s.get_editor_property("material_slot_name"))
                 for s in mesh.get_editor_property("static_materials")]
        applied = 0
        for slot, rep in sorted(remap.items()):
            name = bl.safe_name(slot)
            if name not in slots:
                continue
            mat = self.prop_mats.get(stem, {}).get(rep)
            mic = (self.materials.get((self.shared_mat_pkg, mat.material_key)) if mat
                   else self.error_bind(rep, stem))
            if mic is not None:
                component.set_material(slots.index(name), mic)
                applied += 1
        return 1 if applied else 0

    def _place_decals(self, actors):
        """One ADecalActor per `.decals` line -- VtMB's `infodecal` layer (blood, bullet holes,
        graffiti, posters, stains).

        A deferred decal maps its texture U to the component's local Z and V to local Y, not the
        intuitive Y=U/Z=V, so the surface's horizontal axis (SDir, the U/s texture axis) goes on
        local Z and the vertical (TDir) falls out as the derived Y. MakeRotFromXZ builds a valid
        right-handed rotation from Normal + SDir; a 3-axis matrix would be reflected, because the
        exporter's s/t frame is left-handed with respect to the normal. Local +X is the
        room-facing normal, so the component projects along its -X into the wall, and DecalSize
        is the box HALF-size (X = projection reach, Y = vertical, Z = horizontal).

        Sort order is the sidecar's own line order, so two decals on the same wall layer the way
        the map author stacked them instead of in undefined order."""
        if not self.decals:
            return 0
        placed = 0
        missing = set()
        for index, decal in enumerate(self.decals):
            mic = self.materials.get((self.decal_mat_pkg, decal.mat))
            if not mic:
                missing.add(decal.mat)
                continue
            rotation = unreal.MathLibrary.make_rot_from_xz(decal.normal, decal.s_dir)
            actor = actors.spawn_actor_from_class(unreal.DecalActor, decal.loc, rotation)
            if not actor:
                continue
            component = actor.decal
            component.set_decal_material(mic)
            component.set_editor_property("decal_size", unreal.Vector(
                DECAL_HALF_DEPTH, decal.half_h, decal.half_w))
            # VtMB decals persist at any distance -- no screen-size fade-out.
            component.set_fade_screen_size(0.0)
            component.set_sort_order(index)
            actor.set_actor_label("Decal_%d_%s" % (index, bl.safe_name(decal.mat)))
            actor.tags = [TAG_DECAL]
            actor.set_folder_path("Decals")
            placed += 1
        for name in sorted(missing):
            # The one bind that does not fall back to the error material: a UDecalComponent only
            # accepts a Deferred Decal domain material, and the shared error material is a surface
            # one, so binding it here would draw nothing and complain. A decal is a whole actor
            # rather than a slot, so the wall it projected onto still renders correctly without it.
            fail("decal material has no baked instance: %s" % name)
        return placed

    def _place_player_start(self, actors):
        path = os.path.join(self.dir, "%s.spawn" % self.map)
        if not os.path.isfile(path):
            return
        origin = None
        yaw = 0.0
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                tok = line.split()
                if not tok:
                    continue
                if tok[0] == "origin" and len(tok) >= 4:
                    origin = unreal.Vector(float(tok[1]), float(tok[2]), float(tok[3]))
                elif tok[0] == "yaw" and len(tok) >= 2:
                    yaw = float(tok[1])
        if origin:
            actors.spawn_actor_from_class(unreal.PlayerStart, origin,
                                          unreal.Rotator(0.0, 0.0, yaw))

    # ------------------------------------------------------------------- drive

    def flush(self):
        start = time.time()
        failed = 0
        for asset_path in self.saved:
            if not bl.save(asset_path):
                failed += 1
        log("saved %d assets, %d failed (%.1fs)" % (
            len(self.saved) - failed, failed, time.time() - start))
        # The queue is what is still unwritten. Emptying it keeps a later flush from re-saving
        # everything an earlier checkpoint already wrote.
        self.saved = []
        return failed

    def checkpoint(self, label):
        """Save the queued packages, publish the run report and the receipts, reclaim memory.

        Both documents are written AFTER the save, so every asset either one lists exists on disk.
        That is what makes the report promotable -- `load_asset_run_reports` validates each
        receipt's output file -- and what makes the receipts themselves a safe crash floor.
        Returns the number of packages that could not be saved.
        """
        failed = self.flush()
        if failed:
            fail("checkpoint %s: %d asset(s) could not be saved, nothing recorded"
                 % (label, failed))
            return failed
        self.tracker.write_report()
        self.tracker.write_receipts()
        _collect_garbage()
        log("corpus checkpoint: %s -- report and receipts written, %s" % (label, "; ".join(
            "%s %s" % (stage, self.tracker.summary(stage)) for stage in self.tracker.stages)))
        return 0


class CorpusBake(Bake):
    """The shared corpus scope: `$ELYSIUM_EXPORT_ROOT/shared` -> /ElysiumBaked/Shared.

    Every texture, every material and every static model in the user's install, baked once. A map
    resolves these and authors none of them, so the doorknob 42 maps hang on a door is one
    `StaticMesh`, its brick is one `Texture2D`, and changing either is one rebake rather than 42.

    It runs no world, sky, particle or level stage, because it has none of those inputs. It is
    also the only scope that prunes these packages: it authors them in full, so anything else left
    in them is from an earlier corpus.
    """

    prune_scope = SC.SCOPE

    def __init__(self, tracker, digest_cache):
        super(CorpusBake, self).__init__(SC.SCOPE, tracker, digest_cache)
        self.dir = self.corpus_dir

    def load_sources(self):
        if not self.load_corpus():
            return False
        if not os.path.isdir(os.path.join(self.corpus_dir, SC.PROPS)):
            fail("no corpus models at %s (run: uv run elysium export bundle corpus)"
                 % os.path.join(self.corpus_dir, SC.PROPS))
            return False
        # The world half of the corpus: every material no map has to stamp anything into. Their
        # `MatDef`s come straight off the one definition, so nothing re-parses a VMT here.
        self.world_mats = {
            key: bl.mat_from_record(key, record, key)
            for key, record in self.corpus_materials.items()
            if not SC.is_map_scoped_material(
                key, decal=record.get("decal"), wetness_driven=record.get("wetness") is not None)
        }
        self._load_prop_sources()
        return True

    def _material_sets(self):
        """One set: every shared material, in the one package that holds them."""
        merged = dict(self.world_mats)
        merged.update(self._all_prop_mats())
        return ((merged, self.shared_mat_pkg, self.shared_tex_pkg),)

    def _shared_material_keys(self):
        return {}

    def stage_textures(self):
        """Import every texture the manifest names, and prune the package to exactly that set.

        The manifest is the wanted-set rather than what the material graph happens to reach: a sky
        face belongs to no material, and a texture left from an earlier corpus is one nothing
        references but the registry still carries.
        """
        start = time.time()
        wanted = SC.texture_files({"textures": self.corpus_textures})
        jobs = []
        for name, role in sorted(wanted.items()):
            source = os.path.join(self.corpus_dir, SC.TEX, name)
            if not os.path.isfile(source):
                raise SystemExit("[bake] corpus texture is missing: %s" % source)
            jobs.append((SC.texture_asset(name), source, role))
        imported = self._import_texture_jobs(jobs, self.shared_tex_pkg)
        for asset_name, _source, _role in jobs:
            texture = imported.get(asset_name)
            if not texture:
                raise SystemExit(
                    "[bake] texture resolution failed: %s/%s" % (self.shared_tex_pkg, asset_name))
            self.textures[(self.shared_tex_pkg, asset_name)] = texture
        log("corpus textures: %d desired in %s (%.1fs)" % (
            len(imported), self.shared_tex_pkg, time.time() - start))
        log("textures: %s" % self.tracker.summary("textures"))

    def resolve_textures(self):
        """Load whatever the texture stage did not build this run, so the material stage binds a
        complete set even on a cached run."""
        for name in sorted(SC.texture_files({"textures": self.corpus_textures})):
            asset_name = SC.texture_asset(name)
            if (self.shared_tex_pkg, asset_name) in self.textures:
                continue
            path = "%s/%s" % (self.shared_tex_pkg, asset_name)
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                self.textures[(self.shared_tex_pkg, asset_name)] = (
                    unreal.EditorAssetLibrary.load_asset(path))

    def resolve_materials(self):
        for mats, mat_pkg, _ in self._material_sets():
            for key in mats:
                if (mat_pkg, key) in self.materials:
                    continue
                path = "%s/%s" % (mat_pkg, SC.material_asset(key))
                if unreal.EditorAssetLibrary.does_asset_exist(path):
                    self.materials[(mat_pkg, key)] = unreal.EditorAssetLibrary.load_asset(path)


def bake_corpus(stages, asset_plan, digest_cache):
    """Bake the shared corpus in the current editor process.

    The corpus is thousands of packages in one process, so each stage -- and each batch of prop
    meshes inside the props stage -- checkpoints when it completes: the queued packages are saved,
    an interim run report is published, and the editor collects what the batch no longer holds. A
    run that dies later still leaves a report the orchestrator can promote, so the next run reuses
    the work that landed instead of building it a second time.
    """
    tracker = AssetTracker(SC.SCOPE, asset_plan, digest_cache)
    bake = CorpusBake(tracker, digest_cache)
    if not bake.load_masters() or not bake.load_sources():
        return False
    if "textures" in stages:
        bake.stage_textures()
        if bake.checkpoint("textures"):
            return False
    bake.resolve_textures()
    if "materials" in stages:
        bake.stage_materials()
        if bake.checkpoint("materials"):
            return False
    bake.resolve_materials()
    if "props" in stages:
        bake.stage_props()
    if bake.flush():
        return False
    tracker.write_report()
    log("%s done" % SC.SCOPE)
    return True


def bake_one(map_name, stages, asset_plan, digest_cache):
    """Bake one map in the current editor process."""
    tracker = AssetTracker(map_name, asset_plan, digest_cache)
    bake = Bake(map_name, tracker, digest_cache)
    if not bake.load_masters() or not bake.load_sources():
        return False

    # Textures and materials are prerequisites for every mesh stage, so they always run --
    # a restricted -BakeStages skips their asset *creation*, not the lookup.
    if "textures" in stages:
        bake.stage_textures()
    bake.resolve_textures()
    if "materials" in stages:
        bake.stage_materials()
    bake.resolve_materials()
    if "world" in stages:
        bake.stage_world()
    if "sky" in stages:
        bake.stage_sky()
    if "particles" in stages:
        # One Niagara system per env_particle definition the map places. Independent of the mesh
        # stages -- it reads the offline particle sidecar, not the OBJ/material graph.
        from pipeline.unreal import make_particle_systems
        make_particle_systems.build(
            map_name, Path(OUT_ROOT), bake.pkg, tracker=bake.tracker)
    if bake.flush():
        return False
    if "level" in stages and not bake.stage_level():
        return False
    tracker.write_report()
    log("%s done" % map_name)
    return True


def _collect_garbage():
    collect = getattr(unreal.SystemLibrary, "collect_garbage", None)
    if collect:
        collect()


def _manual_plan(scopes, stages):
    """The forced run plan a direct invocation uses. Never promoted by the outer orchestrator."""
    return {
        "schema": bake_cache.ASSET_RUN_SCHEMA,
        "version": bake_cache.ASSET_SCHEMA_VERSION,
        "run_id": "manual-%d" % int(time.time()),
        "force": True,
        "maps": {scope: {
            "stages": list(stages),
            "fingerprints": {stage: "manual" for stage in stages},
            "policies": {stage: "manual" for stage in stages},
        } for scope in scopes},
    }


def _load_asset_plan(path):
    """Read one frozen asset run plan off disk, or exit when it is not one this bake can read."""
    try:
        with open(path, "r", encoding="utf-8") as handle:
            plan = json.load(handle)
    except (OSError, ValueError) as exc:
        fail("invalid asset run plan: %s" % exc)
        raise SystemExit(1)
    if (plan.get("schema") != bake_cache.ASSET_RUN_SCHEMA
            or plan.get("version") != bake_cache.ASSET_SCHEMA_VERSION):
        fail("unsupported asset run plan schema")
        raise SystemExit(1)
    return plan


def _run_corpus():
    """-BakeCorpus=1: the shared corpus, gated upstream by its own manifest task."""
    asset_plan_path = cmdline_arg("BakeAssetPlan", "")
    if asset_plan_path:
        asset_plan = _load_asset_plan(asset_plan_path)
        planned = asset_plan.get("maps", {}).get(SC.SCOPE)
        if not planned or set(planned.get("stages", [])) != set(SC.STAGES):
            fail("asset run plan does not match %s stages" % SC.SCOPE)
            raise SystemExit(1)
    else:
        # Direct developer invocation remains a recovery surface.
        asset_plan = _manual_plan([SC.SCOPE], SC.STAGES)
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [MOUNT], force_rescan=True)
    digest_cache = ContentDigestCache(Path(OUT_ROOT) / bake_cache.DIGEST_CACHE_FILE)
    try:
        ok = bake_corpus(SC.STAGES, asset_plan, digest_cache)
    finally:
        digest_cache.write()
        _collect_garbage()
    if not ok:
        fail("shared corpus bake failed")
        raise SystemExit(1)


def main():
    if cmdline_arg("BakeCorpus", ""):
        _run_corpus()
        return
    raw_maps = cmdline_arg("BakeMaps", "")
    map_names = [item.strip() for item in raw_maps.split(",") if item.strip()]
    if not map_names:
        map_names = [cmdline_arg("BakeMap", "sp_tutorial_1")]
    stages = [s.strip() for s in cmdline_arg("BakeStages", ",".join(ALL_STAGES)).split(",")
              if s.strip()]
    unknown = sorted(set(stages) - set(ALL_STAGES))
    if unknown:
        fail("unknown stage(s): %s" % ", ".join(unknown))
        raise SystemExit(1)
    asset_plan_path = cmdline_arg("BakeAssetPlan", "")
    if asset_plan_path:
        asset_plan = _load_asset_plan(asset_plan_path)
    else:
        # Direct developer invocation remains a recovery surface.
        asset_plan = _manual_plan(map_names, stages)
    for map_name in map_names:
        planned = asset_plan.get("maps", {}).get(map_name)
        if not planned or set(planned.get("stages", [])) != set(stages):
            fail("asset run plan does not match %s stages" % map_name)
            raise SystemExit(1)
    log("maps=%s stages=%s" % (",".join(map_names), ",".join(stages)))

    # A fresh commandlet has not indexed the mount, so does_asset_exist reports False for
    # assets already on disk and every create_asset call then trips the unattended
    # overwrite guard. Scan it up front so re-runs reuse what is there.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [MOUNT], force_rescan=True)

    failed = []
    digest_cache = ContentDigestCache(Path(OUT_ROOT) / bake_cache.DIGEST_CACHE_FILE)
    for position, map_name in enumerate(map_names, 1):
        log("--- [%d/%d] %s ---" % (position, len(map_names), map_name))
        try:
            if not bake_one(map_name, stages, asset_plan, digest_cache):
                failed.append(map_name)
        except (Exception, SystemExit) as exc:
            fail("%s raised: %s" % (map_name, exc))
            failed.append(map_name)
        finally:
            _collect_garbage()
    digest_cache.write()

    if failed:
        fail("%d of %d map bake(s) failed: %s" % (
            len(failed), len(map_names), ", ".join(failed)))
        raise SystemExit(1)
    log("all %d map bake(s) completed" % len(map_names))


main()
