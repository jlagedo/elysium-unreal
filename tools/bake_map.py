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
SKYLIGHT_INTENSITY = 0.4          # ambient fill with no skyambient in the .lights sidecar
SKYAMBIENT_INTENSITY = 0.6        # ambient fill when the map names one
SKYLIGHT_FALLBACK_COLOR = unreal.LinearColor(0.12, 0.13, 0.18, 1.0)

ALL_STAGES = ("textures", "materials", "world", "sky", "props", "level")


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
        self.mesh_pkg = "%s/Meshes" % self.pkg
        self.prop_pkg = "%s/Props" % self.pkg
        self.prop_tex_pkg = "%s/Props/Textures" % self.pkg
        self.prop_mat_pkg = "%s/Props/Materials" % self.pkg

        self.world_obj = None            # ObjModel
        self.world_mats = {}             # name -> MatDef
        self.blend = []                  # per-vertex WVT weight
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
        log("world: %d verts / %d tris / %d groups / %d materials (%.1fs)" % (
            len(self.world_obj.positions), self.world_obj.tri_count,
            len(self.world_obj.groups), len(self.world_mats), time.time() - start))

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

    def _master_for(self, mat):
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
        for mats, mat_pkg, tex_pkg in (
                (self.world_mats, self.mat_pkg, self.tex_pkg),
                (self._all_prop_mats(), self.prop_mat_pkg, self.prop_tex_pkg)):
            start = time.time()
            made = 0
            for key, mat in sorted(mats.items()):
                name = "MI_" + bl.safe_name(key)
                mic = bl.make_material_instance(name, mat_pkg, self._master_for(mat))
                if not mic:
                    fail("material instance failed: %s/%s" % (mat_pkg, name))
                    continue
                self._bind(mic, mat, tex_pkg)
                self.materials[(mat_pkg, key)] = mic
                self.saved.append("%s/%s" % (mat_pkg, name))
                made += 1
            log("materials: %d into %s (%.1fs)" % (made, mat_pkg, time.time() - start))

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
        """Load already-baked material instances into the lookup, so a mesh stage can run
        without re-authoring the materials it binds."""
        for mats, mat_pkg in ((self.world_mats, self.mat_pkg),
                              (self._all_prop_mats(), self.prop_mat_pkg)):
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
                actor.set_folder_path("Lights")
                placed += 1
        return placed, sky_ambient

    def _place_sky(self, actors, sky_ambient):
        """The dim ambient fill plus the map's height fog, as AElysiumMapActor sets them.

        SLS_SpecifiedCubemap with no cubemap resolves to a flat constant ambient of the light
        colour -- a fixed fill independent of any scene capture, with the lower hemisphere lit
        so undersides and floor-facing faces read. A captured-scene skylight would sample the
        near-black 2D sky and leave the map unlit."""
        actor = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0.0, 0.0, 0.0))
        if actor:
            component = actor.light_component
            component.set_mobility(unreal.ComponentMobility.MOVABLE)
            component.set_editor_property("source_type",
                                          unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
            component.set_editor_property("cubemap", None)
            component.set_editor_property("lower_hemisphere_is_black", False)
            # The per-source rig owns the scene, so the fill only keeps deep shadows off
            # pure black: 0.6 when the map names a skyambient, 0.4 when it does not.
            component.set_editor_property(
                "intensity", SKYAMBIENT_INTENSITY if sky_ambient else SKYLIGHT_INTENSITY)
            component.set_light_color(sky_ambient or SKYLIGHT_FALLBACK_COLOR)
            actor.set_actor_label("SkyLight")
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
                fog.set_actor_label("HeightFog")
                fog.set_folder_path("Environment")

    # ------------------------------------------------------------------- level

    def stage_level(self):
        start = time.time()
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        if not world:
            fail("new_blank_map returned null")
            return
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

        placed = 0
        for asset_path in sorted(unreal.EditorAssetLibrary.list_assets(
                self.mesh_pkg, recursive=False, include_folder=False)):
            static_mesh = unreal.EditorAssetLibrary.load_asset(asset_path)
            if not static_mesh:
                continue
            name = asset_path.rsplit("/", 1)[-1].split(".")[0]
            parts = name.split("_")
            cell = CELL_CM * (4 if name.startswith("SM_Sky") else 1)
            cx, cy, cz = (int(v) for v in parts[-3:])
            location = unreal.Vector((cx + 0.5) * cell, (cy + 0.5) * cell, (cz + 0.5) * cell)
            actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, location)
            if not actor:
                continue
            actor.set_actor_label(name)
            actor.static_mesh_component.set_static_mesh(static_mesh)
            actor.set_folder_path("Sky" if name.startswith("SM_Sky") else "World")
            placed += 1
        log("level: %d world/sky actors" % placed)

        log("level: %d prop actors" % self._place_props(actors))
        lights, sky_ambient = self._place_lights(actors)
        log("level: %d light actors" % lights)
        self._place_sky(actors, sky_ambient)
        self._place_player_start(actors)

        map_path = "%s/%s" % (self.pkg, self.map)
        if unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
            log("level: saved %s (%.1fs)" % (map_path, time.time() - start))
        else:
            fail("level save failed: %s" % map_path)

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
                if len(tok) < 8:
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
                actor.static_mesh_component.set_static_mesh(mesh)
                actor.set_folder_path("Props")
                placed += 1
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
