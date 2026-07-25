# Bakes one exported map into real Unreal assets under the /ElysiumBaked mount (the
# .uasset-bake spike -- docs/uasset-bake-spike.md).
#
# Where the shipping runtime builds every engine object in code at map-load time, this pass
# runs once in a headless editor and writes the same world out as Texture2D / MaterialInstance
# / StaticMesh assets plus a .umap, so the map gets the parts of the engine that only exist
# behind an offline build: Nanite, DDC-fitted Lumen surface cards, distance fields, real LODs
# and BC7/BC5 compression.
#
# The output is derived from the user's own VtMB install, so it is gitignored and regenerable
# exactly like tools/out -- only the .uplugin mount descriptor is committed.
#
# Run headless (normally via bake.bat):
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="tools/bake_map.py"
#       -BakeMap=sp_tutorial_1 -unattended -nosplash -nopause
#
# Optional -BakeStages=<csv> restricts the run to a subset of:
#   textures, materials, world, sky, props, level
import math
import os
import sys
import time

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import bake_lib as bl   # noqa: E402

MOUNT = "/ElysiumBaked"
OUT_ROOT = os.path.join(HERE, "out")

MASTERS = {
    "opaque": "/Game/VtMB/Materials/M_World_Opaque.M_World_Opaque",
    "masked": "/Game/VtMB/Materials/M_World_Masked.M_World_Masked",
    "translucent": "/Game/VtMB/Materials/M_World_Translucent.M_World_Translucent",
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

# UElysiumLightRig's calibrated constants, so a baked light matches the runtime rig's.
POINT_SPOT_SCALE = 0.003
MAX_BRIGHTNESS = 8.0
FALLOFF_EXPONENT = 1.0
RADIUS_SCALE = 1.0
SPECULAR_SCALE = 0.0
SUN_SCALE_LUX = 8.0
FALLBACK_RADIUS_CM = 2500.0
SKYLIGHT_INTENSITY = 1.0
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

# A deferred decal's projection box reaches this far (cm) either way along its projection
# axis. Kept shallow so a decal catches its host wall and not the geometry behind it.
DECAL_HALF_DEPTH = 16.0

ALL_STAGES = ("textures", "materials", "world", "sky", "props", "level")


# Both are named profiles from Config/DefaultEngine.ini rather than per-channel edits, because
# only the profile name survives the .umap save/load round-trip: loading re-applies the profile
# and discards custom responses set alongside it.
PROFILE_PICK_ONLY = "ElysiumPickOnly"    # drawn, but touched by nothing except the debug pick
PROFILE_PROP_SOLID = "ElysiumPropSolid"  # blocks the pawn, occludes +use, pickable


def log(msg):
    unreal.log("[bake] %s" % msg)


def _dir_rotator(direction):
    """Unreal spot/directional lights emit along +X, so aim that axis down the beam."""
    if direction.length() < 1e-6:
        return unreal.Rotator(0.0, 0.0, 0.0)
    return unreal.MathLibrary.conv_vector_to_rotator(direction)


def fail(msg):
    unreal.log_error("[bake] %s" % msg)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line."""
    needle = "-%s=" % key
    for token in unreal.SystemLibrary.get_command_line().split():
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


class Bake(object):
    def __init__(self, map_name):
        self.map = map_name
        self.dir = os.path.join(OUT_ROOT, map_name)
        self.pkg = "%s/%s" % (MOUNT, map_name)
        self.tex_pkg = "%s/Textures" % self.pkg
        self.mat_pkg = "%s/Materials" % self.pkg
        self.decal_mat_pkg = "%s/Materials/Decals" % self.pkg
        self.mesh_pkg = "%s/Meshes" % self.pkg
        self.prop_pkg = "%s/Props" % self.pkg
        self.prop_tex_pkg = "%s/Props/Textures" % self.pkg
        self.prop_mat_pkg = "%s/Props/Materials" % self.pkg

        self.world_obj = None            # ObjModel
        self.world_mats = {}             # name -> MatDef
        self.blend = []                  # per-vertex WVT weight
        self.decals = []                 # DecalDef, in sidecar order
        self.textures = {}               # (package, asset name) -> Texture2D
        self.materials = {}              # (package, material key) -> MaterialInstanceConstant
        self.prop_models = {}            # stem -> ObjModel
        self.prop_mats = {}              # stem -> {name: MatDef}
        self.masters = {}
        self.saved = []                  # asset paths pending save

    # ------------------------------------------------------------------ inputs

    def load_masters(self):
        for key, path in MASTERS.items():
            asset = unreal.load_asset(path)
            if not asset:
                fail("master material missing: %s" % path)
                return False
            self.masters[key] = asset
        return True

    def load_sources(self):
        obj_path = os.path.join(self.dir, "%s.obj" % self.map)
        if not os.path.isfile(obj_path):
            fail("no export at %s" % obj_path)
            return False
        start = time.time()
        self.world_obj = bl.read_obj(obj_path)
        self.world_mats = bl.read_mtl(os.path.join(self.dir, "%s.mtl" % self.map))
        self.blend = bl.read_floats(os.path.join(self.dir, "%s.blend" % self.map))
        self.decals = bl.read_decals(os.path.join(self.dir, "%s.decals" % self.map))
        log("world: %d verts / %d tris / %d groups / %d materials / %d decals (%.1fs)" % (
            len(self.world_obj.positions), self.world_obj.tri_count,
            len(self.world_obj.groups), len(self.world_mats), len(self.decals),
            time.time() - start))

        prop_dir = os.path.join(self.dir, "props")
        if os.path.isdir(prop_dir):
            start = time.time()
            for entry in sorted(os.listdir(prop_dir)):
                if not entry.endswith(".obj"):
                    continue
                stem = entry[:-4]
                model = bl.read_obj(os.path.join(prop_dir, entry))
                self.prop_models[stem] = model
                self.prop_mats[stem] = bl.read_mtl(
                    os.path.join(prop_dir, model.mtl_name or (stem + ".mtl")))
            tris = sum(m.tri_count for m in self.prop_models.values())
            log("props: %d models / %d tris (%.1fs)" % (
                len(self.prop_models), tris, time.time() - start))
        return True

    # ---------------------------------------------------------------- textures

    def _texture_jobs(self, mats, base_dir, package):
        """Collect (source file, asset name, role) for every texture the material set uses."""
        roles = {}
        for mat in mats.values():
            for path, role in ((mat.albedo, "albedo"), (mat.emissive, "albedo"),
                               (mat.base_tex2, "albedo"), (mat.bump, "normal"),
                               (mat.env_mask, "mask")):
                if not path:
                    continue
                src = os.path.join(base_dir, path.replace("/", os.sep))
                if not os.path.isfile(src):
                    continue
                name = "T_" + bl.safe_name(os.path.splitext(path)[0])
                # First use wins the role; a texture bound as both colour and mask is rare
                # and the colour reading is the safe default.
                roles.setdefault(name, (src, role))
        return [(name, src, role) for name, (src, role) in sorted(roles.items())]

    def stage_textures(self):
        for mats, base_dir, package in (
                (self.world_mats, self.dir, self.tex_pkg),
                (self._all_prop_mats(), os.path.join(self.dir, "props"), self.prop_tex_pkg)):
            jobs = self._texture_jobs(mats, base_dir, package)
            if not jobs:
                continue
            start = time.time()
            imported = bl.import_textures([(src, name) for name, src, _ in jobs], package)
            for name, _, role in jobs:
                texture = imported.get(name)
                if not texture:
                    fail("texture import failed: %s/%s" % (package, name))
                    continue
                bl.configure_texture(texture, role)
                self.textures[(package, name)] = texture
                self.saved.append("%s/%s" % (package, name))
            log("textures: %d into %s (%.1fs)" % (len(imported), package, time.time() - start))

    def _all_prop_mats(self):
        """Every prop material, keyed uniquely so identical names in different models do not
        collide. The key is what the material asset gets named."""
        merged = {}
        for stem, mats in self.prop_mats.items():
            for name, mat in mats.items():
                merged[self._prop_mat_key(stem, name, mat)] = mat
        return merged

    @staticmethod
    def _prop_mat_key(stem, name, mat):
        """Prop materials dedupe across models on name + albedo: two models naming the same
        material with the same texture really are the same surface."""
        return "%s__%s" % (bl.safe_name(name), bl.safe_name(os.path.splitext(mat.albedo)[0]))

    # --------------------------------------------------------------- materials

    def _material_sets(self):
        """(materials, material package, texture package) for every set the bake authors.

        Decal-flagged world materials are split off into their own package: an `infodecal`
        surface is a projector, not geometry, so it instances the deferred-decal master. They
        share the world's texture package, since they ride the same `<map>.mtl`."""
        world, decals = {}, {}
        for key, mat in self.world_mats.items():
            (decals if mat.decal else world)[key] = mat
        return ((world, self.mat_pkg, self.tex_pkg),
                (decals, self.decal_mat_pkg, self.tex_pkg),
                (self._all_prop_mats(), self.prop_mat_pkg, self.prop_tex_pkg))

    def _master_for(self, mat):
        if mat.decal:
            return self.masters["decal"]
        if mat.additive:
            return self.masters["additive"]
        if mat.blend:
            return self.masters["translucent"]
        if mat.scissor:
            return self.masters["masked"]
        return self.masters["opaque"]

    def _bind(self, mic, mat, tex_pkg):
        """Bind the same named parameters FElysiumMaterialFactory::Build binds."""
        def tex(path):
            if not path:
                return None
            return self.textures.get((tex_pkg, "T_" + bl.safe_name(os.path.splitext(path)[0])))

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
        # M_Decal carries only the albedo (RGB -> BaseColor, A -> Opacity) and that same
        # alpha-masked self-illum path; the surface it projects onto owns the rest.
        if mat.decal:
            return
        bump = tex(mat.bump)
        if bump:
            bl.set_tex_param(mic, "BumpMap", bump)
            bl.set_scalar_param(mic, "BumpAmount", BUMP_AMOUNT)
        if mat.envmap:
            mask = tex(mat.env_mask)
            if mask:
                bl.set_tex_param(mic, "EnvMask", mask)
            # An unmasked reflective surface reflects uniformly; the master's own white
            # default stands in for the runtime's 1x1 white texture.
            bl.set_scalar_param(mic, "EnvStrength", ENV_STRENGTH)
        tex2 = tex(mat.base_tex2)
        if tex2:
            bl.set_tex_param(mic, "BaseTex2", tex2)
            bl.set_scalar_param(mic, "BlendAmount", 1.0)

    def stage_materials(self):
        for mats, mat_pkg, tex_pkg in self._material_sets():
            if not mats:
                continue
            start = time.time()
            made = 0
            wanted = set()
            for key, mat in sorted(mats.items()):
                name = "MI_" + bl.safe_name(key)
                mic = bl.make_material_instance(name, mat_pkg, self._master_for(mat))
                if not mic:
                    fail("material instance failed: %s/%s" % (mat_pkg, name))
                    continue
                self._bind(mic, mat, tex_pkg)
                self.materials[(mat_pkg, key)] = mic
                self.saved.append("%s/%s" % (mat_pkg, name))
                wanted.add(name)
                made += 1
            # This stage authors a package's whole material set in one pass, so anything else
            # left in it is from an earlier bake of a different export -- an unreferenced asset
            # the level would never load but the registry still carries.
            pruned = bl.prune_package(mat_pkg, wanted)
            log("materials: %d into %s%s (%.1fs)" % (
                made, mat_pkg, ", %d stale pruned" % pruned if pruned else "",
                time.time() - start))

    def resolve_textures(self):
        """Load already-imported textures into the lookup, so the material stage can bind
        them without re-importing."""
        for mats, base_dir, package in (
                (self.world_mats, self.dir, self.tex_pkg),
                (self._all_prop_mats(), os.path.join(self.dir, "props"), self.prop_tex_pkg)):
            for name, _, _ in self._texture_jobs(mats, base_dir, package):
                if (package, name) in self.textures:
                    continue
                path = "%s/%s" % (package, name)
                if unreal.EditorAssetLibrary.does_asset_exist(path):
                    self.textures[(package, name)] = unreal.EditorAssetLibrary.load_asset(path)

    def resolve_materials(self):
        """Load already-baked material instances into the lookup, so a mesh or level stage can
        run without re-authoring the materials it binds."""
        for mats, mat_pkg, _ in self._material_sets():
            for key in mats:
                if (mat_pkg, key) in self.materials:
                    continue
                path = "%s/MI_%s" % (mat_pkg, bl.safe_name(key))
                if unreal.EditorAssetLibrary.does_asset_exist(path):
                    self.materials[(mat_pkg, key)] = unreal.EditorAssetLibrary.load_asset(path)

    def _emit(self, asset_path, sections, names, materials, nanite):
        """Build one StaticMesh from prepared sections. Returns (triangles kept, dropped)."""
        want = sum(len(s[4]) for s in sections) // 3
        mesh = bl.build_dynamic_mesh(sections)
        got = bl.mesh_triangle_count(mesh)
        static_mesh = bl.create_static_mesh(
            mesh, asset_path, materials, [bl.safe_name(n) for n in names], nanite=nanite)
        if not static_mesh:
            fail("mesh build failed: %s" % asset_path)
            return 0, want
        bl.set_complex_collision(static_mesh)
        self.saved.append(asset_path)
        return got, want - got

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
        start = time.time()
        for key in sorted(buckets.keys()):
            cx, cy, cz, opaque = key
            pivot = ((cx + 0.5) * CELL_CM, (cy + 0.5) * CELL_CM, (cz + 0.5) * CELL_CM)
            sections, names = self._sections(model, normals, self.blend, buckets[key], pivot)
            asset_path = "%s/SM_World_%s%d_%d_%d" % (
                self.mesh_pkg, "" if opaque else "T_", cx, cy, cz)
            materials = [self.materials.get((self.mat_pkg, name)) for name in names]
            kept, lost = self._emit(asset_path, sections, names, materials, nanite=opaque)
            tris += kept
            dropped += lost
            built += 1 if kept else 0
        log("world: %d chunk meshes / %d tris / %d dropped (%.1fs)" % (
            built, tris, dropped, time.time() - start))

    # --------------------------------------------------------------------- sky

    def stage_sky(self):
        sky_path = os.path.join(self.dir, "%s_sky.obj" % self.map)
        if not os.path.isfile(sky_path):
            log("sky: no _sky.obj, skipped")
            return
        start = time.time()
        model = bl.read_obj(sky_path)
        normals = bl.vertex_normals(model.positions, model.groups.values())
        buckets = self._chunk_world(model, self.world_mats, [], CELL_CM * 4)
        bl.ensure_dir(self.mesh_pkg)
        built = 0
        tris = 0
        dropped = 0
        cell = CELL_CM * 4
        for key in sorted(buckets.keys()):
            cx, cy, cz, opaque = key
            pivot = ((cx + 0.5) * cell, (cy + 0.5) * cell, (cz + 0.5) * cell)
            sections, names = self._sections(model, normals, [], buckets[key], pivot)
            asset_path = "%s/SM_Sky_%s%d_%d_%d" % (
                self.mesh_pkg, "" if opaque else "T_", cx, cy, cz)
            materials = [self.materials.get((self.mat_pkg, name)) for name in names]
            kept, lost = self._emit(asset_path, sections, names, materials, nanite=opaque)
            tris += kept
            dropped += lost
            built += 1 if kept else 0
        log("sky: %d meshes / %d tris / %d dropped (%.1fs)" % (
            built, tris, dropped, time.time() - start))

    # ------------------------------------------------------------------- props

    def stage_props(self):
        bl.ensure_dir(self.prop_pkg)
        start = time.time()
        built = 0
        tris = 0
        dropped = 0
        for stem in sorted(self.prop_models.keys()):
            model = self.prop_models[stem]
            mats = self.prop_mats.get(stem, {})
            if not model.groups:
                continue
            normals = bl.vertex_normals(model.positions, model.groups.values())
            sections, names = self._sections(model, normals, [], model.groups, (0.0, 0.0, 0.0))
            asset_path = "%s/SM_%s" % (self.prop_pkg, bl.safe_name(stem))
            materials = []
            for name in names:
                mat = mats.get(name)
                key = self._prop_mat_key(stem, name, mat) if mat else name
                materials.append(self.materials.get((self.prop_mat_pkg, key)))
            # A prop model whose materials are all opaque/masked can be Nanite; a mixed model
            # cannot (Nanite is a whole-mesh setting), so it falls back wholesale.
            nanite = all((mats[n].opaque if n in mats else True) for n in names)
            kept, lost = self._emit(asset_path, sections, names, materials, nanite=nanite)
            tris += kept
            dropped += lost
            built += 1 if kept else 0
        log("props: %d meshes / %d tris / %d dropped (%.1fs)" % (
            built, tris, dropped, time.time() - start))

    # ------------------------------------------------------------------ lights

    def _place_lights(self, actors):
        """One light actor per WORLDLIGHTS `.lights` line, with UElysiumLightRig's calibration
        applied verbatim: 15 fields `type x y z dx dy dz r g b radius _ stopdot2 _ style`,
        soft non-inverse-square falloff, specular killed, everything Movable.

        Lightstyle animation (field 15) has no baked equivalent -- a styled source is placed at
        its unanimated base intensity."""
        path = os.path.join(self.dir, "%s.lights" % self.map)
        if not os.path.isfile(path):
            return 0, None
        placed = 0
        sky_ambient = None
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for index, line in enumerate(handle):
                tok = line.split()
                if len(tok) < 15:
                    continue
                kind = int(tok[0])
                origin = unreal.Vector(float(tok[1]), float(tok[2]), float(tok[3]))
                direction = unreal.Vector(float(tok[4]), float(tok[5]), float(tok[6]))
                rgb = (float(tok[7]), float(tok[8]), float(tok[9]))
                radius_cm = float(tok[10])
                stopdot2 = float(tok[12])

                mag = max(rgb)
                if mag <= 0.0:
                    continue
                color = unreal.LinearColor(rgb[0] / mag, rgb[1] / mag, rgb[2] / mag, 1.0)

                # Type 5 skyambient is not a light; it tints the SkyLight.
                if kind == 5:
                    sky_ambient = color
                    continue

                reach = (radius_cm if radius_cm > 1.0 else FALLBACK_RADIUS_CM) * RADIUS_SCALE
                soft = min(mag * POINT_SPOT_SCALE, MAX_BRIGHTNESS)

                if kind in (0, 1):
                    actor = actors.spawn_actor_from_class(unreal.PointLight, origin)
                    component = actor.point_light_component if actor else None
                    if component:
                        component.set_attenuation_radius(reach)
                        component.set_intensity(soft)
                        # Texlights stay shadowless, as the rig has them.
                        component.set_cast_shadows(kind != 0)
                elif kind == 2:
                    outer = math.degrees(math.acos(max(-1.0, min(1.0, stopdot2))))
                    outer = max(1.0, min(80.0, outer))
                    actor = actors.spawn_actor_from_class(
                        unreal.SpotLight, origin, _dir_rotator(direction))
                    component = actor.spot_light_component if actor else None
                    if component:
                        component.set_attenuation_radius(reach)
                        component.set_intensity(soft)
                        component.set_outer_cone_angle(outer)
                        component.set_inner_cone_angle(max(1.0, outer * 0.6))
                        component.set_cast_shadows(True)
                elif kind == 3:
                    actor = actors.spawn_actor_from_class(
                        unreal.DirectionalLight, origin, _dir_rotator(direction))
                    # ADirectionalLight exposes only ALight's generic component property.
                    component = actor.light_component if actor else None
                    if component:
                        component.set_intensity(max(mag * SUN_SCALE_LUX, 0.01))
                        component.set_cast_shadows(True)
                else:
                    continue
                if not actor or not component:
                    continue
                if kind in (0, 1, 2):
                    # VtMB light is ~flat within its authored radius, so gentle-exponent
                    # falloff, not inverse-square (docs/rendering-perf.md calibration).
                    component.set_editor_property("use_inverse_squared_falloff", False)
                    component.set_editor_property("light_falloff_exponent", FALLOFF_EXPONENT)
                component.set_light_color(color)
                # The VtMB world is pure Lambert -- kill specular so lights do not glare.
                component.set_editor_property("specular_scale", SPECULAR_SCALE)
                component.set_mobility(unreal.ComponentMobility.MOVABLE)
                actor.set_actor_label("Light_%d_%s" % (
                    index, {0: "tex", 1: "point", 2: "spot", 3: "sun"}[kind]))
                # The line index is the binding UElysiumLightRig::Adopt needs: it re-derives
                # every intensity and reach from this row at load, so the values written above
                # are only what the level looks like in the editor before the game runs.
                actor.tags = [TAG_LIGHT, "elysium.src=%d" % index]
                actor.set_folder_path("Lights")
                placed += 1
        return placed, sky_ambient

    def _place_sky(self, actors, sky_ambient):
        """The sky light and the map's height fog.

        The sky light is placed empty here and handed its real cubemap at load
        (AElysiumMapActor::ApplyEnvironment), because the cube is assembled from the six
        exported sky face images rather than being an asset. What matters is that it is a
        cubemap sky light at all: that is what gives Lumen sky occlusion, so an interior goes
        dark because it cannot see the sky instead of being washed by a constant fill through
        solid walls. Lower hemisphere black, or the sky would light the world's undersides and
        defeat the occlusion."""
        actor = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 0.0))
        if actor:
            component = actor.light_component
            component.set_mobility(unreal.ComponentMobility.MOVABLE)
            component.set_editor_property("source_type",
                                          unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
            component.set_editor_property("cubemap", None)
            component.set_editor_property("lower_hemisphere_is_black", True)
            component.set_editor_property("intensity", SKYLIGHT_INTENSITY)
            component.set_light_color(sky_ambient or SKYLIGHT_FALLBACK_COLOR)
            actor.set_actor_label("SkyLight")
            actor.tags = [TAG_SKYLIGHT]
            actor.set_folder_path("Environment")

        env = {}
        env_path = os.path.join(self.dir, "%s.env" % self.map)
        if os.path.isfile(env_path):
            with open(env_path, "r", encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    tok = line.split()
                    if len(tok) >= 2:
                        env[tok[0]] = tok[1:]
        if env.get("fog", ["0"])[0] == "1":
            fog = actors.spawn_actor_from_class(unreal.ExponentialHeightFog,
                                                unreal.Vector(0.0, 0.0, 0.0))
            if fog:
                component = fog.component
                start_cm = float(env.get("fogstart", [0.0])[0])
                end_cm = max(float(env.get("fogend", [1.0])[0]), start_cm + 1.0)
                color = env.get("fogcolor", ["1", "1", "1"])
                component.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(
                    float(color[0]), float(color[1]), float(color[2]), 1.0))
                component.set_editor_property("start_distance", start_cm)
                component.set_editor_property("fog_height_falloff", 0.02)
                component.set_editor_property(
                    "fog_density", max(0.0001, min(0.05, 3.0 / end_cm)))
                # Volumetric fog turns the map's hundreds of dynamic lights into real shafts and
                # haze rather than a flat depth tint (decisions.md -- a Presentation-layer call).
                component.set_editor_property("volumetric_fog", True)
                component.set_editor_property("volumetric_fog_scattering_distribution", 0.2)
                component.set_editor_property("volumetric_fog_extinction_scale", 1.0)
                fog.set_actor_label("HeightFog")
                fog.tags = [TAG_FOG]
                fog.set_folder_path("Environment")

    # ------------------------------------------------------------------- level

    def stage_level(self):
        start = time.time()
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        if not world:
            fail("new_blank_map returned null")
            return
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

        # The 3D skybox is a miniature authored at 1/scale in a corner of the map, with the
        # sky_camera origin standing for world (0,0,0): world(v) = scale * (v - origin). A sky
        # mesh's verts are already re-based on its cell pivot, so the actor takes uniform scale
        # and sits at scale * (pivot - origin).
        sky_scale, sky_origin = self._read_sky()

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

        log("level: %d prop actors" % self._place_props(actors))
        log("level: %d decal actors" % self._place_decals(actors))
        lights, sky_ambient = self._place_lights(actors)
        log("level: %d light actors" % lights)
        self._place_sky(actors, sky_ambient)
        self._place_player_start(actors)

        map_path = "%s/%s" % (self.pkg, self.map)
        if unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
            log("level: saved %s (%.1fs)" % (map_path, time.time() - start))
        else:
            fail("level save failed: %s" % map_path)

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

    def _place_props(self, actors):
        """One StaticMeshActor per .props line: `stem x y z qx qy qz qw solid`."""
        path = os.path.join(self.dir, "%s.props" % self.map)
        if not os.path.isfile(path):
            return 0
        placed = 0
        cache = {}
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                tok = line.split()
                if len(tok) < 9:
                    continue
                stem = tok[0]
                mesh = cache.get(stem)
                if mesh is None:
                    mesh = unreal.EditorAssetLibrary.load_asset(
                        "%s/SM_%s" % (self.prop_pkg, bl.safe_name(stem)))
                    cache[stem] = mesh
                if not mesh:
                    continue
                location = unreal.Vector(float(tok[1]), float(tok[2]), float(tok[3]))
                rotation = unreal.Quat(float(tok[4]), float(tok[5]),
                                       float(tok[6]), float(tok[7])).rotator()
                actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, location, rotation)
                if not actor:
                    continue
                component = actor.static_mesh_component
                component.set_static_mesh(mesh)
                # Field 9 is Source's own `solid` byte: a solid prop blocks, the rest is dressing.
                component.set_collision_profile_name(
                    PROFILE_PROP_SOLID if int(tok[8]) != 0 else PROFILE_PICK_ONLY)
                actor.tags = [TAG_PROP]
                actor.set_folder_path("Props")
                placed += 1
        return placed

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
        return failed


def main():
    map_name = cmdline_arg("BakeMap", "sp_tutorial_1")
    stages = [s.strip() for s in cmdline_arg("BakeStages", ",".join(ALL_STAGES)).split(",")
              if s.strip()]
    log("map=%s stages=%s" % (map_name, ",".join(stages)))

    # A fresh commandlet has not indexed the mount, so does_asset_exist reports False for
    # assets already on disk and every create_asset call then trips the unattended
    # overwrite guard. Scan it up front so re-runs reuse what is there.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [MOUNT], force_rescan=True)

    bake = Bake(map_name)
    if not bake.load_masters() or not bake.load_sources():
        raise SystemExit(1)

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
    if "props" in stages:
        bake.stage_props()
    if bake.flush():
        raise SystemExit(1)
    if "level" in stages:
        bake.stage_level()
    log("done")


main()
