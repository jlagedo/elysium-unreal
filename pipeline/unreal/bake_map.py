# Bakes one exported map into real Unreal assets under the /ElysiumBaked mount.
#
# Where the shipping runtime builds every engine object in code at map-load time, this pass
# runs once in a headless editor and writes the same world out as Texture2D / MaterialInstance
# / StaticMesh assets plus a .umap, so the map gets the parts of the engine that only exist
# behind an offline build: Nanite, DDC-fitted Lumen surface cards, real LODs and BC7/BC5
# compression. It deliberately builds no mesh distance fields: the project renders with
# hardware-ray-traced Lumen (Config/DefaultEngine.ini, "Rendering") and nothing ever reads one,
# so the flag GeometryScript's asset options set on every mesh is cleared again in
# bake_lib.create_static_mesh.
#
# The output is derived from the user's own VtMB install, so it is gitignored and regenerable
# exactly like $ELYSIUM_EXPORT_ROOT -- only the .uplugin mount descriptor is committed.
#
# Internal editor worker coordinated by `uv run elysium export map`:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="pipeline/unreal/bake_map.py"
#       -BakeMap=sp_tutorial_1 -unattended -nosplash -nopause
#
# Optional -BakeForce=1 re-authors every asset whether or not its stamped recipe matches.
import gc
import hashlib
import math
import json
import os
from pathlib import Path
import re
import struct
import time

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402
from pipeline.unreal import bake_map_v2 as v2  # noqa: E402
from elysium_pipeline import map_bake_stages  # noqa: E402
from elysium_pipeline import mounts  # noqa: E402
from elysium_pipeline.asset_names import (  # noqa: E402
    LIGHTSTYLE_KEY_SUFFIX, brush_slot_style)
from elysium_pipeline.paths import export_root  # noqa: E402
from elysium_pipeline.tasking import ContentDigestCache, DIGEST_CACHE_FILE  # noqa: E402

from pipeline.unreal import sky_composites as sky_assets

MOUNT = mounts.BAKED
OUT_ROOT = os.fspath(export_root())

#: Where this run's map sidecars stand. `-BakeMapSidecars=<root>` names the parent, one directory
#: per map below it, and the host points it at the producer's own `exports_v2/_sidecars`
#: (0018 story 21-4). Empty on the legacy lane, which keeps reading beside the rest of the map's
#: export -- `_sidecar_dir` resolves the two.
SIDECAR_ROOT_FLAG = "BakeMapSidecars"


def _sidecar_dir(map_name):
    """The directory holding one map's eight producer sidecars, for this run."""

    root = cmdline_arg(SIDECAR_ROOT_FLAG, "")
    return os.path.join(root or OUT_ROOT, map_name)

MASTERS = {
    "opaque": "%s/M_World_Opaque.M_World_Opaque" % mounts.MATERIALS,
    "masked": "%s/M_World_Masked.M_World_Masked" % mounts.MATERIALS,
    "translucent": "%s/M_World_Translucent.M_World_Translucent" % mounts.MATERIALS,
    "glass": "%s/M_World_Glass.M_World_Glass" % mounts.MATERIALS,
    "refract": "%s/M_Refract.M_Refract" % mounts.MATERIALS,
    "additive": "%s/M_Additive.M_Additive" % mounts.MATERIALS,
}
# R7.2: there is no `decal` master
# here any more. `M_Decal` is deleted along with its generator; the one decal material in the
# project is `M_V2_Decal`, and the one instance a decal binds is the shared `MI_<unit>_Decal` the
# material lane stages per `$decal` / `decalmodulate` unit -- never a per-map copy.

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
# The one `AElysiumWaterVolumes` actor a converted map's `water.volumes[]` rows fold into (R7.1,
# V2 lane only; the legacy lane places nothing, like TAG_CAPTURE).
TAG_WATER = "elysium.water"
# One reflection capture at a `cubemaps[]` sample (R5.5, V2 lane only). Carries a second
# `elysium.src=<index>` tag naming its lump-42 row, like a light names its `.lights` line.
TAG_CAPTURE = "elysium.capture"
# The baked 2D-sky backdrop dome (R5.2). Distinct from TAG_SKY -- the
# 3D-skybox miniature's own tag -- so the miniature's sky-fog stamping pass never walks the dome.
TAG_SKYDOME = "elysium.skydome"
# One overhead cable segment (0018 story 21-3, V2 lane only; `bake_ropes.TAG_ROPE`). Carries a
# second `elysium.src=<index>` tag naming its row in the staged `ropes` block.
TAG_ROPE = "elysium.rope"
# R7.4 (water-complete contract 3): the lightstyle a world/sky chunk carries, restating
# `ElysiumBakedTags::LightStyle`'s own format (`"elysium.style=%d"`) so `stage_level` can write it
# without a C++ import. Beside TAG_WORLD/TAG_SKY, never instead of them.
LIGHT_STYLE_TAG_PREFIX = "elysium.style="


def chunk_style_suffix(style):
    """The filename fragment a styled world/sky chunk's asset name carries beyond
    `SM_World_<T_>?<cx>_<cy>_<cz>` / `SM_Sky_<T_>?<cx>_<cy>_<cz>` (contract 3: `_chunk_world`
    keeps a styled material group out of every unstyled bucket, so its chunk needs a name of its
    own too, or two different buckets in the same cell would collide on one asset path). Empty
    for style 0 -- the overwhelming majority, every map before this lane existed -- so an unstyled
    bake names its chunks exactly as it always has."""
    return "_S%d" % style if style else ""


def chunk_actor_tags(name):
    """The baked tags a world/sky chunk actor carries, from its asset name alone.

    Its lane tag (`TAG_SKY` for a 3D-skybox miniature chunk, `TAG_WORLD` otherwise), plus
    `elysium.style=<n>` when `chunk_style_suffix` named a lightstyle on it (R7.4 contract 3).
    The runtime's style clock (`UElysiumMapVisuals::AdoptBakedLevel` -> `UElysiumLightRig::
    AdoptStyledPrimitives`) finds a chunk by exactly this tag and overwrites CustomPrimitiveData
    slot `LIGHT_STYLE_CPD_SLOT` on it every tick; every other primitive keeps the
    `LIGHT_STYLE_CPD_DEFAULT` that `set_fog` stamped. Pulled out of `stage_level`'s placement loop
    so the one decision the lane added is assertable without an editor world to spawn into.
    """
    _base, style = parse_chunk_style(name)
    tags = [TAG_SKY if name.startswith("SM_Sky") else TAG_WORLD]
    if style:
        tags.append("%s%d" % (LIGHT_STYLE_TAG_PREFIX, style))
    return tags


def parse_chunk_style(name):
    """`(base name, style)` -- the inverse of `chunk_style_suffix`, read where a chunk is
    rediscovered by listing built assets rather than carried through in memory (`stage_level`).
    Unambiguous: a chunk's whole name after its `SM_World_`/`SM_Sky_` prefix is digits, `-` and
    `_` only, so `_S<digits>` cannot occur except as this suffix."""
    base, sep, tail = name.rpartition("_S")
    if sep and tail.isdigit():
        return base, int(tail)
    return name, 0


# D1: the texture lane owns cubes; maps bind them and author per-sky material instances.
SKY_TEX_PKG = "%s/Textures/skybox" % MOUNT
SKY_MESH_PKG = sky_assets.SKY_DOME_PACKAGE
SKY_MAT_PKG = "%s/Materials/skybox" % MOUNT
# `ElysiumMapVisuals.cpp`'s own `SkyDomeHalfExtentCm` -- the baked dome has to be the identical
# box the runtime built at load, or the two would silently draw different backdrops.
SKY_DOME_HALF_EXTENT_CM = 500000.0
# UE's own KINDA_SMALL_NUMBER -- the black-cube guard applied before
# dividing `emit_skyambient`'s magnitude by the cube's upper-hemisphere mean.
KINDA_SMALL_NUMBER = 1e-4

# A deferred decal's projection box reaches this far (cm) either way along its projection
# axis. Kept shallow so a decal catches its host wall and not the geometry behind it.
DECAL_HALF_DEPTH = 16.0

# R7.2 ruling 2: `importers.materials.decal_asset_path_for` restated. That module imports numpy
# transitively (through `importers.textures`) and the editor's embedded Python has none, so the
# fold is restated here exactly as `bake_verify.verify_ropes` restates `asset_path_for`:
# `asset_names.safe_name` per path part, `MI_` on the stem, `_Decal` on the whole name.
DECAL_MATERIALS_ROOT = "/ElysiumBaked/Materials"
DECAL_INSTANCE_SUFFIX = "_Decal"


def decal_instance_path(material_key):
    """`<material key>` -> `/ElysiumBaked/Materials/<dir>/MI_<safe stem>_Decal`, the shared
    projector instance every decal -- baked line, mesh-decal face group or runtime `Lay()` --
    binds for that unit."""
    parts = str(material_key).split("/")
    folded = "/".join(bl.safe_name(part) for part in parts[:-1])
    name = "MI_" + bl.safe_name(parts[-1]) + DECAL_INSTANCE_SUFFIX
    return ("%s/%s/%s" % (DECAL_MATERIALS_ROOT, folded, name) if folded
            else "%s/%s" % (DECAL_MATERIALS_ROOT, name))

# Source's distance fog is a PER-PRIMITIVE material term, not the height fog actor: the world and
# the 3D-skybox miniature carry two different fogs and share screen depth, so no engine-side fog
# mechanism can separate them (the reasoning and the measurement are in pipeline/unreal/mat_fog.py). These
# are the Custom Primitive Data slots that term reads. Keep in sync with mat_fog.CPD_* and
# Source/ElysiumUE/Public/ElysiumFog.h.
FOG_CPD_COLOR = 0        # float4: linear RGB, then an unused A
FOG_CPD_START = 4
FOG_CPD_INV_RANGE = 5
FOG_CPD_FLOATS = 6

# R7.4 (water-complete contract 3, `ElysiumLightStyle::SlotBrightness` beside `ElysiumFog.h`'s
# slots 0-5): the term the Lit/LitTranslucent/Water masters multiply base colour and emissive by.
# Unwritten reads as 1.0 -- "no dimming" -- the opposite convention from fog's "unwritten = off":
# almost every primitive in the world carries no lightstyle at all and must stay lit at its
# authored brightness rather than default to black. `UElysiumLightRig`'s style clock overwrites
# the slot every tick, but only on the components a bake tagged `ElysiumBakedTags::LightStyle`
# (`elysium.style=<n>`) -- everything else keeps this baked default forever.
LIGHT_STYLE_CPD_SLOT = 6
LIGHT_STYLE_CPD_DEFAULT = 1.0

# Commandlet frame cadence. A `-run=pythonscript` process never reaches `FEngineLoop::Tick`, so
# every D3D12 free stays queued behind the frame fence until a frame is simulated
# (`UElysiumMapBakeLibrary::TickCommandletFrames`), and the fast allocator retires at most one
# page per frame. The mesh factory ticks one frame per this many meshes built -- the engine's own
# commandlets pump on the same 256-item cadence -- and each release point ticks a short run.
MESH_TICK_INTERVAL = 256
RELEASE_TICK_FRAMES = 4

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


#: Every `fail` call counts here; the launch exits non-zero when any fired, so a map or
#: corpus that finished with a dropped mesh, a missing decal instance or a pruned prop cannot
#: exit 0 and be receipted as complete by the launch gate that decides whether to boot again.
FAILURES = [0]


def fail(msg):
    FAILURES[0] += 1
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
    """Stamp a fog set onto one primitive, plus the R7.4 lightstyle-brightness default riding the
    same array right behind it (index `LIGHT_STYLE_CPD_SLOT`, one call). The default (serialized)
    slot, not the transient one: the level has to look right when it is opened in the editor,
    before any game runs, and a component the bake never tagged `elysium.style=` must still read
    full brightness rather than fog's "unwritten = off" convention."""
    component.set_default_custom_primitive_data_float_array(
        FOG_CPD_COLOR, list(data) + [LIGHT_STYLE_CPD_DEFAULT])


def sky_join_intensity(mag, upper_mean, sky_name=""):
    """The C1/C2 sky-ambience policy, computed once at bake
    (R5.2) instead of every load: no pair or a pair reading zero -> 0 (an authored zero is a
    reading, not a missing one); a cube whose upper hemisphere is at or below UE's own
    `KINDA_SMALL_NUMBER` -> 0, logged, rather than a divide that would ship an infinity;
    otherwise the factor that makes the cube deliver VtMB's stated sky radiance, `mag /
    upper_mean`."""
    if mag <= 0.0:
        return 0.0
    if upper_mean <= KINDA_SMALL_NUMBER:
        log("sky '%s': type-5 magnitude %.5f but the cube's upper hemisphere is black -- no IBL "
            "level" % (sky_name, mag))
        return 0.0
    return mag / upper_mean


def sky_dome_geometry():
    """`(positions, normals, uvs, tris)` for the baked 2D-sky backdrop (R5.2):
    The inward box the runtime used to build at load (retired by 0018 story 21-1), reproduced
    vertex-for-vertex (same 8 corners, same six-quad/twelve-triangle winding, same all-up
    normals and all-zero UVs — the runtime's box has neither, since M_Sky samples the cube by
    view direction, not by surface attribute) so the mesh the bake authors is the identical box
    the runtime built at load on every map before this one converted. Plain Python, no `unreal`
    import, so it is unit-testable without a fake editor module.
    """
    h = SKY_DOME_HALF_EXTENT_CM
    positions = [
        (-h, -h, -h), (h, -h, -h), (h, h, -h), (-h, h, -h),
        (-h, -h, h), (h, -h, h), (h, h, h), (-h, h, h),
    ]
    quads = ((0, 1, 2, 3), (7, 6, 5, 4), (4, 5, 1, 0), (3, 2, 6, 7), (1, 5, 6, 2), (4, 0, 3, 7))
    tris = []
    for a, b, c, d in quads:
        tris.extend((a, b, c, a, c, d))
    normals = [(0.0, 0.0, 1.0)] * 8
    uvs = [(0.0, 0.0)] * 8
    return positions, normals, uvs, tris


def _build_sky_dome_dynamic_mesh():
    """The `UDynamicMesh` build of `sky_dome_geometry()` -- one shared mesh, every sky samples
    the same box through its own `MI_Sky_<name>`."""
    positions, normals, uvs, tris = sky_dome_geometry()
    return bl.build_dynamic_mesh([(positions, normals, uvs, None, tris)])


def light_specular_scale():
    """`UElysiumSurfaceSettings.LightSpecularScale`, the one global light-specular knob (R5.5):
    `LightSpecularScale` rides along -- the bake stamps the same value the rig re-applies at
    adopt, so the level in the editor
    and the adopted level in the game agree. Read from the settings CDO -- the ini -- never a
    literal; the legacy `SPECULAR_SCALE = 0.0` was the third of the repudiated three zeroes."""
    settings = unreal.get_default_object(unreal.ElysiumSurfaceSettings)
    return float(settings.get_editor_property("light_specular_scale"))


def capture_radius():
    """`UElysiumSurfaceSettings.CaptureRadius`: every reflection capture's influence radius."""
    settings = unreal.get_default_object(unreal.ElysiumSurfaceSettings)
    return float(settings.get_editor_property("capture_radius"))


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line (a quoted path is one token)."""
    needle = "-%s=" % key
    for token in re.findall(r'"[^"]*"|\S+', unreal.SystemLibrary.get_command_line()):
        token = token.strip('"')
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def _asset_path(asset):
    if asset is None:
        return ""
    path = asset.get_path_name() if hasattr(asset, "get_path_name") else str(asset)
    return path.split(".", 1)[0]


def level_sidecar_recipe(map_root: Path, map_name: str, digest=None) -> dict:
    """Parse only the values that affect authored level actors.

    Formatting, comments, and ignored Source fields do not dirty the level package.  Ordered
    rows stay ordered because decal sort order and light source indices are authored output.

    `.ents`, `.hulls`, `.dispcol` and `.ropes` are runtime-only -- nothing here parses their
    fields, because no baked actor is authored from them (MP-1.3/R2.3). Their whole-file digest
    still has to be part of the recipe: they are read at map load, not at bake time, so nothing
    else notices a touched one, and the tracker would keep serving a level stamped against an
    input that no longer matches -- a stale level that reads as a runtime bug. `digest` lets a
    caller route the hash through a cache (`Bake._file_sha256`); the default hashes the file
    directly, which is what a standalone call (a test, a differ) needs.

    **`.props` and `.decals` are not parsed here any more** (0018 story 21-4). Both were only
    ever read for the V2 lane's benefit, and both are staged now: `MapBakeV2._level_recipe`
    already overwrote `props`/`prop_skins` from `geometry.placements`, and it stamps the staged
    projector rows' own digest in place of this function's `decals` parse. Nothing on either lane
    authors an actor from the files.
    """

    def _digest(path: Path) -> str:
        if digest is not None:
            return digest(path)
        hasher = hashlib.sha256()
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                hasher.update(chunk)
        return hasher.hexdigest()

    def sidecar_digest(suffix: str):
        path = map_root / f"{map_name}.{suffix}"
        return _digest(path) if path.is_file() else None

    def lines(suffix: str):
        path = map_root / f"{map_name}.{suffix}"
        if not path.is_file():
            return []
        return [line.split() for line in path.read_text(
            encoding="utf-8", errors="replace"
        ).splitlines()]

    def number(value: str):
        try:
            return float(value)
        except ValueError:
            return value

    lights = []
    for index, tokens in enumerate(lines("lights")):
        if len(tokens) < 15:
            continue
        kind = int(tokens[0])
        rgb = [float(value) for value in tokens[7:10]]
        if max(rgb) <= 0.0 or kind not in (0, 1, 2, 3, 5):
            continue
        row = {"index": index, "kind": kind, "rgb": rgb}
        if kind != 5:
            row.update({
                "position": [float(value) for value in tokens[1:4]],
                "direction": [float(value) for value in tokens[4:7]],
                "radius": float(tokens[10]),
                "stopdot": float(tokens[11]),
                "stopdot2": float(tokens[12]),
                "sky": int(tokens[15]) if len(tokens) >= 16 else 0,
            })
        lights.append(row)

    env = {}
    relevant_env = {
        "fog", "fogcolor", "fogstart", "fogend",
        "skyfog", "skyfogcolor", "skyfogstart", "skyfogend",
    }
    for tokens in lines("env"):
        if len(tokens) >= 2 and tokens[0] in relevant_env:
            if tokens[0] in {"fog", "skyfog"}:
                env[tokens[0]] = tokens[1] == "1"
            else:
                env[tokens[0]] = [number(value) for value in tokens[1:]]

    sky = {"origin": [0.0, 0.0, 0.0], "scale": 16.0}
    for tokens in lines("sky"):
        if len(tokens) == 4 and tokens[0] == "origin":
            sky["origin"] = [float(value) for value in tokens[1:4]]
        elif len(tokens) == 2 and tokens[0] == "scale":
            sky["scale"] = float(tokens[1])

    spawn = {"origin": None, "yaw": 0.0}
    for tokens in lines("spawn"):
        if len(tokens) >= 4 and tokens[0] == "origin":
            spawn["origin"] = [float(value) for value in tokens[1:4]]
        elif len(tokens) >= 2 and tokens[0] == "yaw":
            spawn["yaw"] = float(tokens[1])

    return {
        "lights": lights,
        "environment": env,
        "sky": sky,
        "spawn": spawn,
        "runtime_sidecars": {
            "ents_sha256": sidecar_digest("ents"),
            "hulls_sha256": sidecar_digest("hulls"),
            "dispcol_sha256": sidecar_digest("dispcol"),
            "ropes_sha256": sidecar_digest("ropes"),
        },
    }


class AssetTracker(object):
    """Decide per-asset work by comparing each recipe against the hash stamped on the asset.

    The mount is the record: every baked asset carries its recipe fingerprint as package
    metadata (`bake_lib.RECIPE_TAG`), surfaced as an asset registry tag, so a fresh process
    reads what is current without loading a package, a crash loses only the packages that were
    not saved, and no state exists outside the assets themselves. Unreal owns everything
    downstream -- references resolve by path and derived data re-keys off content -- so the one
    question decided here is the one the engine cannot answer: does this asset still match the
    intermediate it was authored from?

    `stage` names group the run's counters for reporting, and since 0018 story 21-2 they also
    name what `--from <stage>` forces: a stage in `force_stages` re-authors whatever its recipe
    says, exactly as `--force` does for all of them (`--force` is `--from textures`).
    """

    def __init__(self, scope, digest_cache, force=False, force_stages=()):
        self.scope = scope
        self.force = bool(force)
        self.force_stages = frozenset(force_stages)
        self.digest_cache = digest_cache
        self.stages = {}
        self.recipes = {}

    def forced(self, stage):
        """Whether this run must re-author `stage` whatever the mount already carries."""
        return self.force or stage in self.force_stages

    def _counters(self, stage):
        return self.stages.setdefault(stage, {"built": 0, "reused": 0, "pruned": 0})

    def file_sha256(self, path):
        return self.digest_cache.digest(Path(path))

    def register(self, stage, object_path, recipe, expected_class="", fresh=True):
        counters = self._counters(stage)
        fingerprint = bl.recipe_fingerprint(stage, object_path, recipe)
        self.recipes[object_path] = fingerprint
        stored = bl.stored_recipe(object_path, producer="maps-legacy")
        exists = unreal.EditorAssetLibrary.does_asset_exist(object_path)
        if exists and expected_class:
            if bl.asset_class_name(object_path) != expected_class:
                bl.delete_owned_asset(object_path)
                exists = False
        if (self.forced(stage) or not fresh or not exists
                or stored != fingerprint):
            return True
        counters["reused"] += 1
        return False

    def stamp(self, asset, object_path):
        """Stamp the recipe this run computed for the asset; called before its save."""
        fingerprint = self.recipes.get(object_path)
        if not fingerprint:
            fail("no recipe was registered for %s; left unstamped, it re-authors next run"
                 % object_path)
            return
        bl.stamp_recipe(asset, fingerprint, producer='maps-legacy')

    def built(self, stage, count=1):
        self._counters(stage)["built"] += count

    def pruned(self, stage, count):
        self._counters(stage)["pruned"] += int(count)

    def summary(self, stage):
        data = self._counters(stage)
        return "%d built / %d reused / %d pruned" % (
            data["built"], data["reused"], data["pruned"])


class Bake(object):
    def __init__(self, map_name, tracker, digest_cache):
        self.map = map_name
        self.tracker = tracker
        self.digest_cache = digest_cache
        self.dir = os.path.join(OUT_ROOT, map_name)
        # 0018 story 21-4: where this map's SIDECARS stand, which is no longer where the rest of
        # its legacy export stands. The producer writes the eight it owns into
        # `exports_v2/_sidecars/<map>/` and the host names that root on the command line; with no
        # flag (the legacy lane) it is `self.dir`, exactly as before. Only the sidecar readers
        # take this -- `.obj`, `.mtl`, `.blend`, `brushes/` and `tex/cube/` are the decoder's and
        # stay under `self.dir` until 21-5 deletes them.
        self.sidecar_dir = _sidecar_dir(map_name)
        # Where `_stage_weather_texture` finds the rain raster. The legacy lane keeps it beside
        # the rest of the map's export; `MapBakeV2` re-points it at the geometry staging tree
        # once it has read the staged weather payload.
        self.weather_dir = self.dir
        from elysium_pipeline.asset_paths import map_package
        self.pkg = map_package(map_name)
        # A map's own packages hold only what carries a map-specific input: the baked env cubemaps,
        # the rain height field, the material instances that stamp this map's fog or weather, and
        # its geometry.
        self.cube_pkg = "%s/Textures/Cubes" % self.pkg
        self.mat_pkg = "%s/Materials" % self.pkg
        self.decal_mat_pkg = "%s/Materials/Decals" % self.pkg
        self.mesh_pkg = "%s/Meshes" % self.pkg
        self.brush_pkg = "%s/Brushes" % self.pkg
        self.weather_pkg = "%s/Weather" % self.pkg

        self.world_obj = None            # ObjModel
        self.world_mats = {}             # name -> MatDef
        self.blend = []                  # per-vertex WVT weight
        self.decals = []                 # DecalDef, in sidecar order
        self.materials = {}              # (package, material key) -> MaterialInstanceConstant
        self.brush_models = {}           # brush_<model> -> local-space ObjModel
        self.brush_blend = {}            # stem -> per-vertex WVT weight
        self.masters = {}
        self.error_material_asset = None  # the shared checkerboard, authored on the first miss
        self.error_keys = {}             # material name that resolved no VMT -> slots bound to it
        self.env = {}                    # <map>.env, key -> [tokens]
        self.weather = None              # <map>.weather.json
        self.rain_height = None
        self.saved = []                  # (asset path, asset) pending save
        # The world and sky chunk meshes this run BUILT, by asset path. `stage_level` places every
        # chunk in `mesh_pkg`, and re-loading one it was just handed is a package lookup per chunk
        # for nothing. A reused chunk is absent here (`_emit` never loads one) and `stage_level`
        # loads it by path. Held on the instance, which dies with `bake_one`, so the wrappers go
        # before `_release_map_packages` runs -- see its own comment.
        self.world_meshes = {}
        # The three per-map asset lanes' staged entries for THIS map, handed in by `bake_one` from
        # the manifests the host staged before the editor launched (0018 story 21-2). None is a
        # lane the run was not given a manifest for, which `stage_*` refuses rather than skips.
        self.entities_entry = None
        self.environment_entry = None
        self.collision_entry = None
        # The cooked payload and its fingerprint, set by `stage_collision` and read by
        # `stage_level`: the actor it places points at these bodies, and the level's own recipe
        # carries this hash.
        self.collision_payload = None
        self.collision_fingerprint = ""

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
            self.saved.append((object_path, texture))
            self.tracker.built("textures")
        pruned = (bl.prune_package_prefix(package, prune_prefix, wanted)
                  if prune_prefix else bl.prune_package(package, wanted))
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
        tex_pkg = "%s/Textures" % self.pkg
        if unreal.EditorAssetLibrary.does_directory_exist(tex_pkg):
            dropped += bl.prune_package(tex_pkg, set())
            # An emptied directory goes with its retired set: only `Cubes` can still hold
            # assets, and a directory that no longer exists ends this sweep at the
            # existence check on every later run.
            if not unreal.EditorAssetLibrary.list_assets(
                    tex_pkg, recursive=True, include_folder=False):
                if not unreal.EditorAssetLibrary.delete_directory(tex_pkg):
                    unreal.log_warning(
                        "[bake] could not delete the emptied retired directory %s" % tex_pkg)
        if dropped:
            self.tracker.pruned("textures", dropped)
            log("superseded: %d asset(s) removed from this map's retired packages" % dropped)

    def stage_textures(self):
        """A map imports only the textures that are its own: the baked env cubemaps and the rain
        height field. Every surface texture is the corpus's, imported once by its own scope."""
        self._drop_superseded_packages()
        self._stage_wet_cubemaps()
        self._stage_weather_texture()
        log("textures: %s" % self.tracker.summary("textures"))

    def _stage_weather_texture(self):
        """The rain height field, the one map-owned texture both lanes import.

        `weather_dir` is where the raster stands: the map's export directory on the legacy lane,
        the geometry stage's own staging directory since 0018 story 21-4 (`importers.map_weather`
        writes it beside the manifest, because the editor's embedded Python carries no Pillow).
        """
        if self.weather:
            relative = self.weather["height_texture"]["path"]
            source = os.path.join(self.weather_dir, relative.replace("/", os.sep))
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

    #: The per-map material packages this stage owns. Both are PRUNED and never authored:
    #: R7.2 ruling 2 retired the last per-map material a map wrote -- a surface and a decal
    #: both bind the shared `MI_<unit>` the material lane staged -- and 0018 story 21-5 deleted
    #: the legacy authoring body that was the only thing still writing into them.
    MATERIAL_PACKAGE_ATTRS = ("mat_pkg", "decal_mat_pkg")

    def stage_materials(self):
        for attr in self.MATERIAL_PACKAGE_ATTRS:
            pruned = bl.prune_package(getattr(self, attr), set())
            self.tracker.pruned("materials", pruned)
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
                    bl.finish_material_instance(mic)
                    self.saved.append((path, mic))
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
        """Re-adopt the rain height field a warm run did not rebuild.

        The only texture a map still resolves by hand. Every material it draws is a finished
        instance the material lane staged, so its textures are reached through that asset rather
        than loaded here -- pulling them in would load most of the install's 8,000 texture assets
        to bind nothing. 0018 story 21-5 deleted the legacy corpus-texture and `sm_hub_1`
        wet-cubemap walks that were the rest of this method.
        """
        if self.weather and not self.rain_height:
            path = "%s/T_RainHeight" % self.weather_pkg
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                self.rain_height = unreal.EditorAssetLibrary.load_asset(path)

    def _emit(self, stage, asset_path, sections, names, materials, nanite, phys=None,
              collision=True):
        """Build one StaticMesh from prepared sections.

        Returns (triangles kept, dropped, simple collision shapes, the built asset). The asset is
        None whenever this call did not build one -- a reused mesh is not loaded here, because not
        loading it is the whole point of the reuse."""
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
            return want, 0, (len(phys["hulls"]) if phys else 0), None
        mesh = bl.build_dynamic_mesh(sections)
        got = bl.mesh_triangle_count(mesh)
        static_mesh = bl.create_static_mesh(
            mesh, asset_path, materials, [bl.safe_name(n) for n in names], nanite=nanite,
            collision=collision or phys is not None)
        if not static_mesh:
            fail("mesh build failed: %s" % asset_path)
            return 0, want, 0, None
        # A physics prop simulates, so it needs real simple collision -- VtMB's own convex
        # hulls. Everything else makes its render triangles the collision.
        shapes = bl.set_phy_collision(static_mesh, phys) if phys else 0
        if not phys and collision:
            bl.set_complex_collision(static_mesh)
        self.saved.append((asset_path, static_mesh))
        self.tracker.built(stage)
        # Every mesh built commits GPU and heap pages that only a simulated frame gives back, and
        # this is the one place any lane creates a StaticMesh -- world chunks, brushes, sky and
        # props on both lanes come through here, so the cadence lives here rather than in each of
        # their loops.
        _tick_built_mesh()
        return got, want - got, shapes, static_mesh

    # ------------------------------------------------------------------- world

    def _chunk_world(self, model, mats, blend, cell_cm):
        """Bin triangles into (cell, nanite-able, lightstyle) buckets. Returns
        {(cx, cy, cz, opaque, style): {material name: [tri indices]}}.

        R7.4 (water-complete contract 3): a material group whose staged row names a lightstyle
        (`_V2Material.light_style`, the pier's `objects/surf`) is kept out of every unstyled
        bucket its cell would otherwise put it in, so it becomes its own chunk regardless of what
        else shares the cell -- `stage_level` tags exactly that chunk `elysium.style=<n>` and
        nothing else needs it. A `mats` entry with no `light_style` attribute (every legacy
        `bake_lib.MatDef`, and every V2 unit before this lane) reads 0, so this bucket key is a
        strict refinement: identical output whenever nothing carries a style."""
        buckets = {}
        positions = model.positions
        for name, indices in model.groups.items():
            mat = mats.get(name)
            opaque = mat.opaque if mat else True
            style = int(getattr(mat, "light_style", 0) or 0) if mat else 0
            for base in range(0, len(indices), 3):
                i0, i1, i2 = indices[base], indices[base + 1], indices[base + 2]
                ax, ay, az = positions[i0]
                bx, by, bz = positions[i1]
                cx, cy, cz = positions[i2]
                key = (int((ax + bx + cx) / 3.0 // cell_cm),
                       int((ay + by + cy) / 3.0 // cell_cm),
                       int((az + bz + cz) / 3.0 // cell_cm),
                       opaque, style)
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
            cx, cy, cz, opaque, style = key
            pivot = ((cx + 0.5) * CELL_CM, (cy + 0.5) * CELL_CM, (cz + 0.5) * CELL_CM)
            sections, names = self._sections(model, normals, self.blend, buckets[key], pivot)
            asset_path = "%s/SM_World_%s%d_%d_%d%s" % (
                self.mesh_pkg, "" if opaque else "T_", cx, cy, cz, chunk_style_suffix(style))
            materials = [self.material_for(name) for name in names]
            kept, lost, _, static_mesh = self._emit(
                "world", asset_path, sections, names, materials, nanite=opaque)
            tris += kept
            dropped += lost
            built += 1 if kept else 0
            if kept:
                wanted.add(asset_path.rsplit("/", 1)[-1])
            if static_mesh:
                self.world_meshes[asset_path] = static_mesh
        pruned = bl.prune_package_prefix(self.mesh_pkg, "SM_World_", wanted)
        self.tracker.pruned("world", pruned)
        log("world: %d chunk meshes / %d tris / %d dropped / %d stale pruned (%.1fs)" % (
            built, tris, dropped, pruned, time.time() - start))

        # Brush entities are one local-pivot mesh each. They are never placed in the baked
        # level: the runtime attaches them to the convex entity body that owns movement,
        # collision, hiding and teardown. R7.4 contract 3 therefore reaches them through the mesh's
        # own slot names rather than through a chunk actor's tag -- `brush_slot_style` states the
        # rule, `RegisterRuntimeBrush` reads it back.
        bl.ensure_dir(self.brush_pkg)
        wanted = set()
        brush_tris = 0
        brush_dropped = 0
        styled_brushes = 0
        for stem, brush in sorted(self.brush_models.items()):
            normals = bl.vertex_normals(brush.positions, brush.groups.values())
            blend = self.brush_blend.get(stem, [])
            sections, names = self._sections(
                brush, normals, blend, brush.groups, (0.0, 0.0, 0.0))
            asset_path = "%s/SM_%s" % (self.brush_pkg, stem)
            materials = [self.material_for(name) for name in names]
            nanite = all(self.world_mats.get(name).opaque
                         if self.world_mats.get(name) else True for name in names)
            # The style the runtime will read back off this mesh, and the one way the slot-name
            # carrier can lie: an UNSTYLED group key whose fold happens to end in `_style<n>`
            # would animate a body nothing styled. Say so and stop rather than ship it.
            slot_names = [bl.safe_name(name) for name in names]
            for name, slot in zip(names, slot_names):
                if LIGHTSTYLE_KEY_SUFFIX not in name and brush_slot_style([slot]):
                    raise SystemExit(
                        "[map] brush '%s': material group %r carries no lightstyle but its slot "
                        "name %r folds to one -- `brush_slot_style` would animate the body "
                        "(rename the unit or extend the carrier)" % (stem, name, slot))
            style = brush_slot_style(slot_names)
            styled_brushes += 1 if style else 0
            kept, lost, _, _ = self._emit(
                "world", asset_path, sections, names, materials, nanite=nanite,
                collision=False)
            if kept:
                wanted.add("SM_%s" % stem)
            brush_tris += kept
            brush_dropped += lost
        pruned = bl.prune_package(self.brush_pkg, wanted)
        self.tracker.pruned("world", pruned)
        log("brushes: %d meshes / %d tris / %d dropped / %d styled / %d stale pruned" % (
            len(wanted), brush_tris, brush_dropped, styled_brushes, pruned))
        log("world assets: %s" % self.tracker.summary("world"))

    # --------------------------------------------------------------------- sky

    def _assert_saved_captures(self, map_path, placed):
        """Re-count the built captures off the SAVED level, and fail the map when they are absent.

        `_build_captures` asserted `built == placed` in memory. Nothing asserted the same of the
        file, and the cubes are not in the `.umap`: `ULevel::CreateMapBuildDataPackage` gives the
        registry a package of its own, `<map>_BuiltData`, which is saved as a second act and can
        therefore be lost on its own -- which is what `Elysium.Content.MapBake.
        ReflectionCapturesBuilt` was reporting, 0 rendered cubes on maps whose bake had said every
        capture was built.

        `UElysiumMapBakeLibrary::CountBuiltReflectionCapturesInPackage` drops both packages and
        loads the level again, so the answer is the disk's rather than the copy already in memory;
        -1 is "no package, or no world in it".

        A level that fails here is deleted rather than only reported (`_discard_stamped_level`):
        it is on disk carrying this run's recipe stamp, so leaving it would have the next run
        REUSE a level whose captures are black with nothing saying so.
        """
        built = int(unreal.ElysiumMapBakeLibrary.count_built_reflection_captures_in_package(
            map_path))
        if built == placed:
            log("level: %d capture(s) re-counted on the saved level" % built)
            return True
        if built < 0:
            fail("level: %s did not re-open after the save" % map_path)
        else:
            fail("level: %s reloaded with %d of %d capture(s) carrying MapBuildData"
                 % (map_path, built, placed))
        self._discard_stamped_level(map_path)
        return False

    def _discard_stamped_level(self, map_path):
        """Delete a level that reached disk carrying this run's recipe stamp.

        EVERY exit from `save_map` onwards comes through here. `tracker.stamp` runs before the
        save, so a run that writes the `.umap` and then fails on the second capture build, on
        `<map>_BuiltData` or on the re-count leaves a level the next non-forced bake reads as
        current: `AssetTracker.register` finds it present with a matching recipe, logs "level:
        reused", skips the builds and the re-count entirely, and the map exits 0 with black probes
        and nothing said. Deleting is the only way to make a failed bake un-reusable, because the
        stamp is already written and the file is already there.

        The editor is still standing on this world on the paths that fail before the re-count, and
        `delete_asset` cannot take the level out from under it -- so it is moved off first, the
        same `NewBlankMap` release `release_baked_packages` does for the same reason. The re-count
        path arrives here already released (`UPackageTools::UnloadPackages` opens a fresh map when
        it unloads the editor's own), where the second open is a no-op sweep.
        """
        if not unreal.EditorLoadingAndSavingUtils.new_blank_map(False):
            fail("level: no blank map to release %s onto before deleting it" % map_path)
        # A save that answered False may have written nothing at all, and there is then nothing
        # to make un-reusable -- only a delete that was ASKED for and refused is an error.
        if not unreal.EditorAssetLibrary.does_asset_exist(map_path):
            log("level: nothing to discard -- %s never reached the mount" % map_path)
            return
        bl.delete_owned_asset(map_path)
        log("level: deleted the stamped %s -- a saved level with unbuilt captures would be "
            "REUSED by the next bake" % map_path)

    def _bake_sky_cube(self, sky_name):
        """Resolve the texture-lane composite and its conserved mean; missing is a bake failure."""
        return sky_assets.load_sky_cube(sky_name)

    def _place_sky_dome(self, actors, sky_name, cube):
        """The baked 2D-sky backdrop (R5.2): the shared `SM_SkyDome` box
        (`_build_sky_dome_dynamic_mesh`) through a per-sky
        `MI_Sky_<SkyName>` bound to `cube`. Tagged `elysium.skydome`, not `elysium.sky` -- see
        `TAG_SKYDOME`'s own comment."""
        bl.ensure_dir(SKY_MAT_PKG)
        master = unreal.EditorAssetLibrary.load_asset("%s/M_Sky" % mounts.MATERIALS)
        if master is None:
            log("sky dome: M_Sky master not found -- run make_sky_material.py")
            return
        mi_package, mi_name = sky_assets.material_address(sky_name)
        mi = bl.make_material_instance(mi_name, mi_package, master)
        if mi is None:
            log("sky dome: %s could not be created" % mi_name)
            return
        bl.set_tex_param(mi, "SkyCube", cube)
        # The faithful default: VtMB's own sky transfer is the identity (D7), matching
        # the identity the retired `elysium.SkyBrightness` knob defaulted to.
        bl.set_scalar_param(mi, "Brightness", 1.0)
        bl.finish_material_instance(mi)
        unreal.EditorAssetLibrary.save_asset("%s/%s" % (mi_package, mi_name))

        bl.ensure_dir(SKY_MESH_PKG)
        mesh_path = "%s/SM_SkyDome" % SKY_MESH_PKG
        dome_mesh = bl.create_static_mesh(
            _build_sky_dome_dynamic_mesh(), mesh_path, [master], ["Sky"], nanite=False,
            collision=False)
        if dome_mesh is None:
            log("sky dome: mesh build failed")
            return
        unreal.EditorAssetLibrary.save_asset(mesh_path)

        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0))
        if not actor:
            return
        component = actor.static_mesh_component
        component.set_static_mesh(dome_mesh)
        component.set_material(0, mi)
        # Non-solid backdrop: no collision, no shadow, and
        # excluded from ray tracing (a box that encloses the whole scene is the canonical
        # hardware-ray-tracing overlap cost).
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        component.set_cast_shadow(False)
        component.set_editor_property("visible_in_ray_tracing", False)
        actor.set_actor_label("SkyDome")
        actor.tags = [TAG_SKYDOME]
        actor.set_folder_path("Environment")

    def _place_sky(self, actors, sky_ambient):
        """The sky light and the map's height fog.

        On a map still on the legacy transport, the sky light is placed empty here and handed
        its real cubemap at load (AElysiumMapActor::ApplyEnvironment), because the cube is
        assembled from the six exported sky face images rather than being an asset. What matters
        is that it is a cubemap sky light at all: that is what gives Lumen sky occlusion, so an
        interior goes dark because it cannot see the sky instead of being washed by a constant
        fill through solid walls. Lower hemisphere black, or the sky would light the world's
        undersides and defeat the occlusion. Its INTENSITY is likewise the runtime's to set
        (C1/C2): the level comes from the map's type-5 `emit_skyambient` magnitude divided by the
        cube's own mean radiance, and the cube does not exist until load. What is written here is
        that magnitude alone, so the actor carries the map's real data in the editor rather than
        a placeholder constant -- 0 on the 83 maps with no sky pair, which is the policy, not an
        absence.

        This stage finishes the job rather than deferring it (R5.2): it bakes the real cube,
        joins it with the same magnitude the runtime's own policy used to apply, and authors the
        backdrop dome -- the runtime assembles no sky at all."""
        color, mag = sky_ambient if sky_ambient else (None, 0.0)
        skybox = self.env.get("skybox", ["0"])[0] == "1"
        sky_name = self.env.get("skyname", [""])[0]

        cube = None
        intensity = 0.0
        if skybox and sky_name:
            cube, upper_mean = self._bake_sky_cube(sky_name)
            if cube is not None:
                intensity = sky_join_intensity(mag, upper_mean, sky_name)

        actor = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 0.0))
        if actor:
            component = actor.light_component
            component.set_mobility(unreal.ComponentMobility.MOVABLE)
            component.set_editor_property("source_type",
                                          unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
            component.set_editor_property("cubemap", cube)
            component.set_editor_property("lower_hemisphere_is_black", True)
            component.set_editor_property("intensity", intensity)
            component.set_light_color(color or SKYLIGHT_FALLBACK_COLOR)
            actor.set_actor_label("SkyLight")
            actor.tags = [TAG_SKYLIGHT]
            actor.set_folder_path("Environment")

        if cube is not None:
            self._place_sky_dome(actors, sky_name, cube)

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

    # ------------------------------------- the three per-map asset lanes (0018 story 21-2)

    def _lane_entry(self, entry, lane, flag):
        if entry is None:
            raise RuntimeError(
                "%s: no staged %s entry; the bake was launched without -%s, or the stage did not "
                "reach this map" % (self.map, lane, flag))
        return entry

    def stage_entities(self):
        """`DA_<map>_Entities`, the entity table the runtime reads and the only one it reads."""
        from pipeline.unreal import bake_map_entities
        entry = self._lane_entry(self.entities_entry, "entity table", "BakeMapEntities")
        bake_map_entities.author(entry, force=self.tracker.forced("entities"))

    def stage_environment(self):
        """`DA_<map>_Environment`: the fog, the sky, the 3D-skybox transform and the spawn."""
        from pipeline.unreal import bake_map_environment
        entry = self._lane_entry(self.environment_entry, "environment", "BakeMapEnvironment")
        bake_map_environment.author(entry, force=self.tracker.forced("environment"))

    def stage_collision(self):
        """Cook `DA_<map>_Collision` and hold it for the level.

        Before `stage_level`, not inside it, because the level's own recipe carries this payload's
        fingerprint: `reset_authoring` replaces every body setup, so a re-cooked payload leaves a
        saved level pointing at objects that no longer exist, and nothing else the recipe digests
        can say so. The decision to re-cook therefore has to be made before the level is asked
        whether it may be reused.
        """
        from pipeline.unreal import bake_map_collision
        entry = self._lane_entry(self.collision_entry, "collision", "BakeMapCollision")
        self.collision_fingerprint = bake_map_collision.payload_fingerprint(entry)
        self.collision_payload, _outcome = bake_map_collision.author_payload(
            entry, force=self.tracker.forced("collision"))

    def _place_collision(self, actors):
        """The world-collision actor and the nav-area marks, in that order.

        Both before the meshes are cut: the bodies ARE the geometry Recast rasterises, and an area
        mark only reaches tiles rasterised after it.
        """
        from pipeline.unreal import bake_map_collision
        entry = self._lane_entry(self.collision_entry, "collision", "BakeMapCollision")
        bodies = bake_map_collision.place_world_collision(entry, self.collision_payload, actors)
        marks = bake_map_collision.place_nav_areas(entry, actors)
        return bodies, marks

    def _build_navigation(self, world, actors):
        """Cut this map's navigation meshes from the collision that now stands in the level, and
        drop the ones the engine spawned for agents the map never asked for."""
        from pipeline.unreal import bake_map_collision
        agents = bake_map_collision.build_navigation(self.map, world, self._used_hull_bits())
        bake_map_collision.prune_unwanted_navmeshes(self.map, actors, agents)
        return agents

    def _assert_saved_navigation(self, map_path):
        """Whether the saved level carries a mesh with tiles for every agent the map asked for."""
        from pipeline.unreal import bake_map_collision
        return bake_map_collision.saved_navigation_is_complete(
            self.map, map_path, self._used_hull_bits())

    # ------------------------------------------------------------------- level

    def _level_recipe(self):
        """Everything the level is stamped against that is not this lane's own identity.

        `MapBakeV2` extends this (its `_level_recipe`) with the staged digests and the placement
        set. 0018 story 21-5 removed the `props` and `prop_skins` entries the legacy prop lane
        filled from `<map>.props`: the V2 override already restated both from the staged
        placements, so the host half never contributed either on a live bake.
        """
        from elysium_pipeline.asset_paths import corpus_path

        # One recursive listing over the map's own package, bucketed into the two
        # directly-owned mesh sets the recipe names.
        world_sky = []
        brushes = []
        for value in unreal.EditorAssetLibrary.list_assets(
                self.pkg, recursive=True, include_folder=False):
            path = value.split(".", 1)[0]
            directory, _, name = path.rpartition("/")
            if directory == self.mesh_pkg and name.startswith(("SM_World_", "SM_Sky_")):
                world_sky.append(path)
            elif directory == self.brush_pkg and name.startswith("SM_"):
                brushes.append(path)

        return {
            "placement": level_sidecar_recipe(
                Path(self.sidecar_dir), self.map, digest=self._file_sha256),
            "world_sky_meshes": sorted(world_sky),
            "brushes": sorted(brushes),
            "materials": sorted(
                _asset_path(material) for material in self.materials.values() if material),
            # R7.2 ruling 2: the shared projector instances this level's decal rows bind.
            # Named here rather than left to the sidecar's digest alone, because the binding is a
            # resolved asset path like every other entry in this recipe -- and a level baked
            # against the retired per-map `M_Decal` MICs must re-author, not be reused.
            "decal_materials": sorted({decal_instance_path(decal.mat) for decal in self.decals}),
            "model_catalogues": {
                corpus_path("model", "DA", name): bl.stored_recipe(
                    corpus_path("model", "DA", name), producer="model-catalogues")
                for name in ("PlacedModels", "PropSkins")},
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
        # The empty world `bake_one` opened before any of this map's stages ran. This stage is
        # the only thing in the process that spawns an actor, so the world it was handed is
        # still blank and authoring into it is the same act as opening a second one -- and
        # `NewBlankMap` is a full `CollectGarbage` sweep, which is not worth paying twice a map.
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        if not world:
            fail("no editor world to author the level into")
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
        # The listing is the enumeration, not the load: `stage_world`/`stage_sky` left every chunk
        # they built in `world_meshes`, so only the chunks this run REUSED are loaded by path.
        for object_path in sorted(unreal.EditorAssetLibrary.list_assets(
                self.mesh_pkg, recursive=False, include_folder=False)):
            asset_path = object_path.split(".", 1)[0]
            static_mesh = self.world_meshes.get(asset_path)
            if not static_mesh:
                static_mesh = unreal.EditorAssetLibrary.load_asset(object_path)
            if not static_mesh:
                continue
            name = asset_path.rsplit("/", 1)[-1]
            # R7.4 contract 3: a styled chunk's name carries `chunk_style_suffix` beyond its
            # cell coordinates -- peel it off before parsing the coordinates back out.
            base_name, _style = parse_chunk_style(name)
            parts = base_name.split("_")
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
            # `ElysiumBakedTags::LightStyle` rides beside the lane tag when the chunk is styled;
            # `set_fog` above already left CPD slot 6 at its 1.0 default for everyone else.
            actor.tags = chunk_actor_tags(name)
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
        self._place_navigation(actors)
        self._place_ai_infra(actors)
        self._place_ropes(actors)
        # The world collision and the marks, before the captures only because they are authoring
        # and the capture pass is a render. Neither renders -- `UElysiumWorldCollisionComponent`
        # creates no scene proxy and a nav-area component is a plain `USceneComponent` -- so
        # standing them here cannot reach a probe.
        bodies, marks = self._place_collision(actors)
        log("level: %d world collision body(ies), %d nav-area convex(es)" % (bodies, marks))
        # R5.5: captures last, so every surface, prop, light and the sky are in the render.
        captures = self._place_captures(actors, sky_scale, sky_origin)
        # **Built once before the save and once after it, because the two halves of a reflection
        # capture are lost in opposite orders.** Measured on `sm_pawnshop_1` (2026-09-04), each
        # half on its own:
        #
        # * Built only BEFORE `save_map`: the level saves with its `MapBuildData` link, and the
        #   registry reaches disk EMPTY -- 14 of 14 in memory before the save, 0 of 14 on a
        #   reload, same registry object (`#67486`), every `MapBuildDataId` unchanged. The save
        #   drains `UMapBuildDataRegistry::ReflectionCaptureBuildData`; it does not re-key it.
        # * Built only AFTER `save_map`: the entries survive to disk, and the level reloads with
        #   **no registry at all** (`registry ABSENT on the level`, read by
        #   `Elysium.Content.MapBake.ReflectionCapturesBuilt` on a fresh editor) -- the registry
        #   is created by the first build, so a level saved before it has nothing to link.
        #
        # So the first build exists to give the level something to point at when it saves, and
        # the second to put the cubes back into the registry it now points at. Only the registry
        # package is written after the second build: another `save_map` would drain it again, and
        # the level needs no second write, because a capture's id is made when its component is
        # constructed rather than by the render -- the `.umap` already carries every id this build
        # keys by. That is the whole of why `ReflectionCapturesBuilt` read 0 rendered cubes on
        # every converted map while the bake's own in-memory count said all of them were built.
        if captures:
            built = self._build_captures(world, captures)
            log("level: %d reflection capture actors, %d built" % (captures, built))

        # Navigation last, after the capture RENDER. Three reasons, none of them ordering against
        # the captures themselves -- the nav meshes go into the `.umap` and the capture cubes into
        # the `_BuiltData` sibling, so the two products never meet. A `RecastNavMesh` carries a
        # debug rendering component, and a commandlet with rendering allowed is not the place to
        # find out whether its proxy is created; `_build_captures` raises `SystemExit` on a short
        # count, and a capture failure should not first pay for a Recast build; and this is the
        # longest step in the stage, so it belongs on the side of the stamp where failing is free.
        self._build_navigation(world, actors)

        self.tracker.stamp(world, map_path)
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
            fail("level save failed: %s" % map_path)
            # A refused save can still have left a partial `.umap` behind, and it would carry
            # this run's stamp: discarded like every other post-stamp exit.
            world = None
            self._discard_stamped_level(map_path)
            return False
        if captures:
            # The level is on the mount from here on, stamped with this run's recipe, so every
            # way out of this block deletes it -- including the `SystemExit` `_build_captures`
            # raises, which would otherwise end the process leaving the next run a level it
            # reuses without ever rendering a cube into it.
            try:
                rebuilt = self._build_captures(world, captures)
                log("level: %d capture(s) re-rendered into the saved level's registry" % rebuilt)
                # `<map>_BuiltData` is a sibling package no level save carries, so it is written
                # on its own -- and the library fully loads it first, because the mount's previous
                # copy arrives here partially loaded and `UPackage::Save` calls `appError` on that.
                saved_build_data = unreal.ElysiumMapBakeLibrary.save_map_build_data(world)
            except BaseException:
                world = None
                self._discard_stamped_level(map_path)
                raise
            if not saved_build_data:
                fail("level: %s saved with its %d built capture(s) left in memory -- no _BuiltData "
                     "package on disk" % (map_path, captures))
                world = None
                self._discard_stamped_level(map_path)
                return False
        # Dropped before the re-count, and load-bearing: the count unloads the level and its
        # `_BuiltData` sibling to force the read off disk, and a live `unreal` wrapper is a root
        # for the editor's collector -- the unload would find this world still reachable, the
        # load would hand back the copy already in memory, and the check would prove nothing.
        world = None
        if captures and not self._assert_saved_captures(map_path, captures):
            return False
        # Unconditional, unlike the capture re-count: every map owes navigation. This is the check
        # the fold had to replace rather than inherit -- `import map-collision` used to read the
        # tile counts off a RELOADED level, which was the only thing in the project that ever
        # asked whether baked navigation reached disk, and it asked on the next run rather than
        # this one. The level is already stamped and on the mount, so a failure discards it.
        if not self._assert_saved_navigation(map_path):
            self._discard_stamped_level(map_path)
            return False
        self.tracker.built("level")
        log("level: saved %s (%.1fs)" % (map_path, time.time() - start))
        return True

    def _read_env(self):
        """<map>.env as key -> [tokens]. Absent on a map with no environment sidecar at all."""
        env = {}
        path = os.path.join(self.sidecar_dir, "%s.env" % self.map)
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    tok = line.split()
                    if len(tok) >= 2:
                        env[tok[0]] = tok[1:]
        return env

    def _place_decals(self, actors):
        """One ADecalActor per `.decals` line -- VtMB's `infodecal` layer (blood, bullet holes,
        graffiti, posters, stains).

        R7.2 ruling 2: the component binds `MI_<unit>_Decal`, the shared projector instance the
        material lane stages beside the surface one for every `$decal` / `decalmodulate` unit,
        resolved from the line's own material id -- the same name the runtime `Lay()` resolves.
        No per-map decal material is authored or read, on either lane.

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
        loaded = {}
        for index, decal in enumerate(self.decals):
            path = decal_instance_path(decal.mat)
            if path not in loaded:
                loaded[path] = unreal.EditorAssetLibrary.load_asset(path)
            mic = loaded[path]
            if not mic:
                missing.add("%s -> %s" % (decal.mat, path))
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
            # one, so binding it here would draw nothing and complain. Named, never skipped: an
            # absent projector instance means the material lane did not stage this unit as a decal
            # (run: uv run elysium import materials).
            fail("decal material has no projector instance: %s" % name)
        return placed

    def _place_player_start(self, actors):
        path = os.path.join(self.sidecar_dir, "%s.spawn" % self.map)
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
        """Save every queued package, stamped with its recipe, and let its source geometry go.

        The queue carries the asset beside its path because the stage that queued it had it in
        hand: a `load_asset` here re-resolved an object this process already holds, once per
        authored package. It is only loaded when the queue could not carry one.
        """
        start = time.time()
        failed = 0
        for asset_path, queued in self.saved:
            asset = queued if queued else unreal.EditorAssetLibrary.load_asset(asset_path)
            if asset:
                self.tracker.stamp(asset, asset_path)
            if not bl.save(asset_path):
                failed += 1
                continue
            if isinstance(asset, unreal.StaticMesh):
                # Saved, so the source description is in the package's bulk data and nothing in
                # the rest of the run reads it back. `UStaticMesh::Build` -- the path this bake
                # authors through -- is the one that does not drop it the way `PostLoad` does, so
                # without this every mesh a run creates keeps its full source geometry resident.
                unreal.ElysiumMapBakeLibrary.release_mesh_source_data(asset)
        log("saved %d assets, %d failed (%.1fs)" % (
            len(self.saved) - failed, failed, time.time() - start))
        # The queue is what is still unwritten. Emptying it keeps a later flush from re-saving
        # everything an earlier checkpoint already wrote.
        self.saved = []
        return failed

    def checkpoint(self, label):
        """Save the queued packages and reclaim memory.

        A saved asset carries its recipe stamp, so whatever lands before a crash is already
        current for the next run; there is nothing else to record. Returns the number of
        packages that could not be saved.
        """
        failed = self.flush()
        if failed:
            fail("checkpoint %s: %d asset(s) could not be saved" % (label, failed))
            return failed
        _settle_and_collect()
        log("corpus checkpoint: %s -- %s" % (label, "; ".join(
            "%s %s" % (stage, self.tracker.summary(stage)) for stage in self.tracker.stages)))
        return 0


def bake_one(map_name, digest_cache, lanes=None, force=False, force_stages=()):
    """Bake one map in the current editor process.

    Geometry and placements are authored from the map's published root unit (R5.1,
    `bake_map_v2`); 0018 story 21-1 retired the `.obj`/`.props` lane and the flag that chose it.
    Since 21-2 one call is the whole of a loadable level: the entity table, the environment and
    the cooked collision payload are stages here, and the world-collision actor, the nav-area
    marks and the Recast meshes go into the level before its one save. `lanes` is
    `{lane: {map: entry}}` from the three manifests the host staged before this process started.

    The previous map's world goes first, before anything of this map's is read, and
    unconditionally. It used to go inside `stage_level`, which is both too late and conditional:
    too late, because every material-instance write in between runs an `FMaterialUpdateContext`,
    which walks the scene and rebuilds a static draw list for every primitive still registered in
    it -- sm_hub_2's material stage measured 0.057 s per instance against an empty scene and
    1.31 s per instance with the previous map's level still standing; conditional, because a map
    whose level recipe already matches returns before ever opening a new one, so the old scene
    survived the whole of the next map's bake. `NewBlankMap` -> `GEditor->NewMap` tears the world
    down through `EditorDestroyWorld`, and `stage_level` authors into the empty world left here.

    The collect that follows is what frees the previous map's assets: the sweep inside
    `EditorDestroyWorld` keeps `GARBAGE_COLLECTION_KEEPFLAGS`, which takes the actors but leaves
    every RF_Standalone texture, material instance and mesh the map loaded.
    """
    if not unreal.EditorLoadingAndSavingUtils.new_blank_map(False):
        fail("%s: new_blank_map returned null" % map_name)
        return False
    _collect_garbage()
    tracker = AssetTracker(map_name, digest_cache, force=force, force_stages=force_stages)
    bake = v2.bake_class()(map_name, tracker, digest_cache)
    lanes = lanes or {}
    bake.entities_entry = lanes.get("entities", {}).get(map_name)
    bake.environment_entry = lanes.get("environment", {}).get(map_name)
    bake.collision_entry = lanes.get("collision", {}).get(map_name)
    if not bake.load_masters() or not bake.load_sources():
        return False
    bake.stage_textures()
    bake.resolve_textures()
    bake.stage_materials()
    bake.resolve_materials()
    bake.stage_world()
    bake.stage_sky()
    # The two data-asset lanes: no world, no level, no ordering against anything here. The
    # collision cook DOES have one -- the level recipe carries its fingerprint, so the decision to
    # re-cook is made before the level is asked whether it may be reused.
    bake.stage_entities()
    bake.stage_environment()
    bake.stage_collision()
    if bake.flush():
        return False
    if not bake.stage_level():
        return False
    log("%s done" % map_name)
    return True


# The V2 lane builds its `Bake` subclass on this module's own classes and constants, so it is handed
# this namespace rather than importing it -- `bake_map` is an editor script that calls `main()` at
# module scope, and importing it a second time would run a second bake. It is `globals()` rather
# than `sys.modules[__name__]` because `-run=pythonscript` execs the script into a namespace that is
# not the interpreter's `__main__` module.
v2.bind(globals())


def _collect_garbage():
    """Free what the finished map no longer holds, synchronously and now.

    This used to call `unreal.SystemLibrary.collect_garbage`, which is
    `UKismetSystemLibrary::CollectGarbage` and does nothing but raise
    `GEngine->ForceGarbageCollection(true)`. That flag is consumed in `UWorld::Tick`, and a
    `-run=pythonscript` commandlet never ticks a world -- so the release between maps was a
    no-op for the whole life of this script, and the resident set grew map over map until D3D12
    refused an upload heap.

    The module-level `unreal.collect_garbage` (PythonScriptPlugin, `PyCore.cpp`) instead calls
    `::CollectGarbage` on the spot, and in a commandlet it passes `RF_NoFlags` where the editor
    would pass `GARBAGE_COLLECTION_KEEPFLAGS` -- so the previous map's textures, material
    instances and meshes, which are all RF_Standalone, are collectable rather than kept.

    Python's own cycle pass runs first: every `unreal` wrapper is a root for the collector while
    it lives, and a wrapper caught in a reference cycle is only dropped by `gc.collect()`.
    """
    gc.collect()
    unreal.collect_garbage()


def _settle_and_collect():
    """A release point: finish the async work, free what it was holding, then hand the pages back.

    The three halves are one act and none of them works alone. Async texture and static-mesh
    compilation is on by default in a commandlet and nothing there pumps it, so a collect that
    runs first finds every in-flight build's source data still owned
    (`FAssetCompilingManager::FinishAllCompilation`, plus the shader manager behind it). The
    collect then frees the objects. What it frees is only *queued* for release inside D3D12, which
    drains its deferred-deletion queue in `RHIEndFrame` -- reached only from a simulated frame, so
    the ticks come last (`UElysiumMapBakeLibrary`).
    """
    unreal.ElysiumMapBakeLibrary.finish_asset_compilation()
    _collect_garbage()
    unreal.ElysiumMapBakeLibrary.tick_commandlet_frames(RELEASE_TICK_FRAMES)


def _peak_working_set_mb():
    """This process's peak working set in MB, or None where it cannot be asked.

    Through `psapi` directly, because the editor's embedded Python carries no `psutil`. The
    `restype`/`argtypes` are not decoration: `GetCurrentProcess` returns a pseudo-handle of -1,
    which a default `c_int` return truncates on a 64-bit process, and the call then fails.
    """
    try:
        import ctypes
        from ctypes import wintypes

        class _Counters(ctypes.Structure):
            _fields_ = [
                ("cb", wintypes.DWORD),
                ("PageFaultCount", wintypes.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        kernel32, psapi = ctypes.windll.kernel32, ctypes.windll.psapi
        kernel32.GetCurrentProcess.restype = wintypes.HANDLE
        psapi.GetProcessMemoryInfo.argtypes = [
            wintypes.HANDLE, ctypes.POINTER(_Counters), wintypes.DWORD]
        psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
        counters = _Counters()
        counters.cb = ctypes.sizeof(_Counters)
        if not psapi.GetProcessMemoryInfo(
                kernel32.GetCurrentProcess(), ctypes.byref(counters), counters.cb):
            return None
        return counters.PeakWorkingSetSize / (1024.0 * 1024.0)
    except (AttributeError, ImportError, OSError):
        return None


def _release_map_packages(map_name):
    """Drop everything the finished map loaded, so the next one starts from a clean heap.

    A collect alone never took any of it. Every package on the bake mount holds `RF_Standalone`
    assets, which is exactly what `GARBAGE_COLLECTION_KEEPFLAGS` keeps while `GIsEditor`, so
    across a `MAP_BAKE_BATCH` run the resident set was the union of every map the process had
    touched -- the map's own textures, materials and meshes plus the whole slice of the shared
    corpus it bound. The unload clears the flag and takes the linkers with it
    (`UElysiumMapBakeLibrary::UnloadBakedPackages` -> `UPackageTools::UnloadPackages`).

    The whole `/ElysiumBaked` mount is the scope, not the map's own package: a map binds far more
    of the shared corpus than it authors, and everything on the mount is saved output that the
    next map re-loads by path from disk when it needs it. What is deliberately NOT in scope is
    `/Game/ElysiumGenerated/Materials` -- the six master materials every map's instances parent
    to. `load_masters` would reload them, but they are six packages whose shader maps every map in
    the batch shares, so unloading them would trade the batch's one master load for one per map
    and buy nothing.

    It runs from `main`'s `finally` rather than at the end of `bake_one`, because the `Bake`
    instance is a local of `bake_one` and its `textures` / `materials` / `masters` maps hold
    `unreal` wrappers -- and a live wrapper is a root for the editor's collector. Only after
    `bake_one` has returned is that set unreachable, and only Python's own cycle pass drops a
    wrapper caught in a reference cycle, so `gc.collect()` leads.

    The world is torn down first, and here rather than left to `UnloadPackages`. It only opens a
    fresh empty map itself (`GEditor->CreateNewMapForEditing`) when the world it is unloading is
    the editor's own, and that holds on the success path -- `stage_level`'s `save_map` renames the
    editor world's package to `<map>/<map>`, which is on the mount -- but not on the failure one:
    a map that dies inside `stage_level` after its actors are spawned is still standing in the
    blank `/Temp/Untitled_N` world `bake_one` opened, which no `/ElysiumBaked` scope can name. Its
    actors then hold every mesh, material and texture reachable, `UnloadPackages` restores
    `RF_Standalone` on all of them and frees nothing -- while still running them through
    `ResetLoaders`, which by its own comment forces attached bulk-data payloads to load into
    memory, so the release would end the map slightly heavier than it found it. `NewBlankMap`
    ahead of the unload hands the collector an empty scene on both paths -- and on the third, a
    converted map whose captures were re-counted off disk, where the editor is already standing in
    the blank map that count's own unload opened and the level is a package like any other on the
    mount. The last map in a batch is released the same way as every other, so no process ends
    still holding a level.
    """
    gc.collect()
    if not unreal.EditorLoadingAndSavingUtils.new_blank_map(False):
        fail("%s: new_blank_map returned null before the package release" % map_name)
    released = unreal.ElysiumMapBakeLibrary.unload_baked_packages(MOUNT)
    log("%s: released %d package(s) under %s" % (map_name, released, MOUNT))
    _settle_and_collect()


#: Meshes built since the last frame tick. A list because `_emit` is a method and this is module
#: state shared by every bake instance in the process.
_MESHES_SINCE_TICK = [0]


def _tick_built_mesh():
    """Count one built mesh and simulate a frame every `MESH_TICK_INTERVAL` of them."""
    _MESHES_SINCE_TICK[0] += 1
    if _MESHES_SINCE_TICK[0] >= MESH_TICK_INTERVAL:
        _MESHES_SINCE_TICK[0] = 0
        unreal.ElysiumMapBakeLibrary.tick_commandlet_frames(1)


def _scan_packages(paths):
    """Index exactly the mount packages this scope reads, before anything reads them.

    A fresh commandlet has not indexed the mount, so `does_asset_exist` reports False for assets
    already on disk and every create_asset call then trips the unattended overwrite guard. The
    scan also loads the recipe tags every reuse decision reads.

    Forced, because the registry's own start-up scan runs in the background and a plain scan of a
    path that gatherer already owns returns at once -- the tags then read as absent until it
    finishes, and every asset on the mount rebuilds. That reason is about the path being scanned,
    not about how much of the mount is scanned, so `force_rescan` stays while the scope narrows.

    The scope is the packages the run actually reads rather than the whole `/ElysiumBaked` mount
    (~89k files across every baked map). The saving is real but small -- 4.0 s against 4.5 s,
    measured twice each on `export map sm_hub_1` -- and it is worth knowing why it is not larger:
    the mount's own start-up scan is already running over everything, and
    `FAssetRegistryImpl::ScanPathsSynchronous` ends in `TickGatherer`, which ingests every result
    the background gather has produced whatever paths were asked for. Narrowing the request
    narrows what this call *waits* for, not what the process gathers.
    """
    start = time.time()
    scope = sorted(set(paths))
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        scope, force_rescan=True)
    log("registry: %d package path(s) scanned (%.1fs)" % (len(scope), time.time() - start))


#: The mount packages any map bake resolves against: the R1 model corpus with its skin table and
#: the detail-sway children beside it, the shared unit material instances a surface or a decal
#: binds, the per-blend sprite children, the placed-model units, and the shared sky package.
#: Everything else a map touches lives under its own `/ElysiumBaked/<map>`. 0018 story 21-5
#: removed `/ElysiumBaked/Shared`, which the legacy corpus bake authored and nothing resolves.
MAP_SCAN_PACKAGES = (
    v2.V2_MESH_PACKAGE,
    DECAL_MATERIALS_ROOT,
    v2.V2_SPRITE_MATERIAL_PACKAGE,
    "%s/Props" % MOUNT,
    SKY_TEX_PKG,
    SKY_MAT_PKG,
    SKY_MESH_PKG,
)


#: The three per-map asset lanes the bake absorbed in 0018 story 21-2: the stage label, the flag
#: naming its staged manifest, and the module that reads it. Each manifest covers the whole run,
#: so a batch of three maps reads each of them once and indexes by map name.
LANE_MANIFESTS = (
    ("entities", "BakeMapEntities", "bake_map_entities"),
    ("environment", "BakeMapEnvironment", "bake_map_environment"),
    ("collision", "BakeMapCollision", "bake_map_collision"),
)


def _load_lane_manifests():
    """`{lane: {map: entry}}` for the three lanes, validated before a single asset is touched.

    A lane whose flag is absent contributes nothing and `Bake.stage_*` refuses the map by name --
    an entity table, an environment and a collision payload are what make a level loadable, so a
    run that cannot author one has nothing to offer and must not write a level instead.
    """
    import importlib

    lanes = {}
    for lane, flag, module_name in LANE_MANIFESTS:
        path = cmdline_arg(flag, "")
        if not path:
            continue
        module = importlib.import_module("pipeline.unreal.%s" % module_name)
        manifest = module.load_manifest(path)
        lanes[lane] = {entry["map"]: entry for entry in manifest["maps"]}
        log("%s: %d staged map(s) from %s" % (lane, len(lanes[lane]), path))
    return lanes


def main():
    raw_maps = cmdline_arg("BakeMaps", "")
    map_names = [item.strip() for item in raw_maps.split(",") if item.strip()]
    if not map_names:
        map_names = [cmdline_arg("BakeMap", "sp_tutorial_1")]
    force = bool(cmdline_arg("BakeForce", ""))
    try:
        force_stages = map_bake_stages.stages_from(cmdline_arg("BakeFrom", "").strip())
    except ValueError as exc:
        raise SystemExit("[bake] -BakeFrom: %s" % exc)
    log("maps=%s%s%s" % (
        ",".join(map_names),
        " (forced)" if force else "",
        " (from %s)" % cmdline_arg("BakeFrom", "") if force_stages and not force else ""))

    lanes = _load_lane_manifests()

    from elysium_pipeline.asset_paths import map_package
    _scan_packages(list(MAP_SCAN_PACKAGES) + [map_package(name) for name in map_names])

    failed = []
    digest_cache = ContentDigestCache(Path(OUT_ROOT) / DIGEST_CACHE_FILE)
    for position, map_name in enumerate(map_names, 1):
        log("--- [%d/%d] %s ---" % (position, len(map_names), map_name))
        try:
            if not bake_one(map_name, digest_cache, lanes=lanes, force=force,
                            force_stages=force_stages):
                failed.append(map_name)
        except (Exception, SystemExit) as exc:
            fail("%s raised: %s" % (map_name, exc))
            failed.append(map_name)
        finally:
            # Both paths: a map that failed halfway has loaded just as much as one that finished,
            # and the next map in the batch must not inherit it. A failed map is released by the
            # blank map the call opens first -- its own level package is unsaved and therefore
            # skipped, but tearing the scene down is what makes the rest of the mount collectable.
            _release_map_packages(map_name)
            peak = _peak_working_set_mb()
            if peak is not None:
                # The evidence `unreal.MAP_BAKE_BATCH` asks for. The peak is the PROCESS's, so in
                # a batch it only ever rises: read the first map's line as that map's cost and
                # each later one as what the release between maps failed to give back.
                log("%s: process peak working set %.0f MB" % (map_name, peak))
    digest_cache.write()

    if failed:
        fail("%d of %d map bake(s) failed: %s" % (
            len(failed), len(map_names), ", ".join(failed)))
        raise SystemExit(1)
    if FAILURES[0]:
        fail("%d map bake(s) finished with %d failure(s) above" % (len(map_names), FAILURES[0]))
        raise SystemExit(1)
    log("all %d map bake(s) completed" % len(map_names))


main()
