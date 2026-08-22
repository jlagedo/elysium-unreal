import tempfile
import unittest
from pathlib import Path

from elysium_pipeline import shared_corpus as SC
from elysium_pipeline.formats import mdl


class KeyRuleTests(unittest.TestCase):
    def test_texture_key_accepts_every_authored_spelling(self):
        for spelling in (
            "metal/metalox",
            "Metal\\MetalOx",
            "/metal//metalox",
            "materials/metal/metalox",
            "materials/metal/metalox.tth",
            "metal/metalox.vtf",
        ):
            self.assertEqual(SC.texture_key(spelling), "metal/metalox", spelling)

    def test_material_key_drops_the_prefix_and_the_extension(self):
        self.assertEqual(
            SC.material_key("materials\\Models/Scenery/spike.vmt"),
            "models/scenery/spike",
        )

    def test_static_stem_folds_the_whole_path_including_models(self):
        self.assertEqual(
            SC.static_stem("models/items/Rings/Ground/Ring03.mdl"),
            "models_items_rings_ground_ring03",
        )

    def test_static_stem_keeps_the_characters_the_c_twin_keeps(self):
        # FElysiumContentPaths::PropModelStem replaces one-for-one and keeps '.', '_' and '-'.
        self.assertEqual(
            SC.static_stem("models/scenery/a-b/c.d e.mdl"),
            "models_scenery_a-b_c.d_e",
        )


class PropMaterialKeyNormalizationTests(unittest.TestCase):
    """One material key, one fold, on both sides of the join.

    A model header states its texture search paths and its material names in the install's own
    mixed case; `shared/materials.json` is keyed by `material_key`, which is lower case. Every
    lookup against that document -- `bake_lib.read_mtl`'s above all -- is case-sensitive, so a
    `.mtl` naming the header's spelling joins nothing and the mesh bakes with a null material.
    """

    SEARCH = ["models/scenery/furniture/MilkCrate/"]
    KEY = "models/scenery/furniture/milkcrate/milkcrate"
    VMT = b'"VertexLitGeneric"\n{\n"$basetexture" "models/scenery/milkcrate"\n}\n'

    def _read(self, key):
        # The install index is case-folded (`install.read` lowers), so a mixed-case candidate
        # reads the same file a lower-case one would.
        return self.VMT if key.lower() == "materials/%s.vmt" % self.KEY else None

    def test_resolution_keeps_the_installs_own_spelling(self):
        path, info = mdl.resolve_vmt("MilkCrate", self.SEARCH, self._read)
        self.assertIsNotNone(info)
        self.assertEqual(path, "models/scenery/furniture/MilkCrate/MilkCrate")

    def test_the_recorded_key_is_the_one_the_corpus_document_carries(self):
        channels = mdl.material_channels("MilkCrate", self.SEARCH, self._read)
        self.assertEqual(channels["vmt"], self.KEY)
        self.assertEqual(channels["vmt"], SC.material_key(channels["vmt"]))

    def test_a_prop_mtl_names_that_key(self):
        mesh = mdl.Mesh("MilkCrate")
        mesh.verts = [(1.0, 2.0, 3.0, 0.0, 0.0),
                      (2.0, 2.0, 3.0, 1.0, 0.0),
                      (1.0, 3.0, 3.0, 0.0, 1.0)]
        mesh.tris = [(0, 1, 2)]
        with tempfile.TemporaryDirectory() as out:
            mdl.write_obj_scene([mesh], "crate", out, self.SEARCH, self._read, {})
            mtl = (Path(out) / "crate.mtl").read_text(encoding="utf-8")
        self.assertIn("mat %s\n" % self.KEY, mtl)


class MaterialResolutionTests(unittest.TestCase):
    """`resolve_vmt` is the engine's own walk and only that walk (research case
    `material-resolution`): the model header's search paths, in header order, each composed into
    `materials/<path><name>.vmt`, with nothing after the last one."""

    HIT = b'"VertexLitGeneric"\n{\n"$basetexture" "models/spike"\n}\n'

    def test_the_header_search_paths_are_tried_in_header_order(self):
        tried = []

        def read(key):
            tried.append(key)
            return self.HIT if key == "materials/models/props/second/spike.vmt" else None

        path, info = mdl.resolve_vmt(
            "spike", ["models/props/first/", "models/props/second/"], read)
        self.assertIsNotNone(info)
        self.assertEqual(path, "models/props/second/spike")
        self.assertEqual(tried, ["materials/models/props/first/spike.vmt",
                                 "materials/models/props/second/spike.vmt"])

    def test_a_flat_material_is_not_a_last_resort(self):
        # The engine composes one path per search path and stops; there is no global
        # `materials/<name>.vmt` step, so a name that only exists flat misses.
        tried = []

        def read(key):
            tried.append(key)
            return self.HIT if key == "materials/spike.vmt" else None

        self.assertEqual(mdl.resolve_vmt("spike", ["models/props/"], read), (None, None))
        self.assertEqual(tried, ["materials/models/props/spike.vmt"])

    def test_a_model_with_no_search_path_resolves_nothing(self):
        self.assertEqual(mdl.resolve_vmt("spike", [], lambda key: self.HIT), (None, None))

    def test_a_world_name_resolves_against_the_materials_root(self):
        # A world or decal material's authored name is already its path, and the engine's brush
        # path composes exactly `materials/<name>.vmt` for it.
        tried = []

        def read(key):
            tried.append(key)
            return self.HIT if key == "materials/brick/brickwall001a.vmt" else None

        path, info = mdl.resolve_vmt("brick/brickwall001a", mdl.WORLD_SEARCH, read)
        self.assertEqual(path, "brick/brickwall001a")
        self.assertEqual(tried, ["materials/brick/brickwall001a.vmt"])

    def test_a_total_miss_answers_no_material(self):
        self.assertIsNone(mdl.material_channels("spike", ["models/props/"], lambda key: None))

    def test_a_missed_slot_writes_newmtl_with_no_mat_line(self):
        """The bake's signal for a miss is the absence of the `mat` line, which is what lets it
        bind the error material on exactly that slot."""
        mesh = mdl.Mesh("spike")
        mesh.verts = [(1.0, 2.0, 3.0, 0.0, 0.0),
                      (2.0, 2.0, 3.0, 1.0, 0.0),
                      (1.0, 3.0, 3.0, 0.0, 1.0)]
        mesh.tris = [(0, 1, 2)]
        with tempfile.TemporaryDirectory() as out:
            mdl.write_obj_scene([mesh], "spike", out, ["models/props/"], lambda key: None, {})
            mtl = (Path(out) / "spike.mtl").read_text(encoding="utf-8")
        self.assertIn("newmtl spike\n", mtl)
        self.assertNotIn("mat ", mtl)


class WorldMaterialKeyTests(unittest.TestCase):
    def test_an_unpatched_face_keys_by_its_authored_material(self):
        self.assertEqual(SC.world_material_key("art/bdiorama1", "sm_hub_1"), "art/bdiorama1")

    def test_a_map_wide_cubemap_face_carries_the_default_tag(self):
        self.assertEqual(
            SC.world_material_key("maps/sm_hub_1/asphalt/asphaltasan", "sm_hub_1"),
            "asphalt/asphaltasan@cubemapdefault",
        )

    def test_a_positioned_cubemap_face_carries_its_own_cube(self):
        self.assertEqual(
            SC.world_material_key("maps/sm_hub_1/glass/glass01_660_671_73", "sm_hub_1"),
            "glass/glass01@c660_671_73",
        )

    def test_another_maps_patch_is_not_read_as_this_maps_cubemap(self):
        # base_material strips any map's prefix, but only the owning map's patch names a cube.
        self.assertIsNone(SC.cubemap_of("maps/la_hub_1/glass/glass01", "sm_hub_1"))

    def test_two_maps_naming_one_material_resolve_to_one_key(self):
        # The regression this corpus exists for: the same authored surface in two maps was two
        # assets, free to disagree.
        first = SC.world_material_key("brick/brickwall001a", "sm_hub_1")
        second = SC.world_material_key("brick/brickwall001a", "sm_junkyard_1")
        self.assertEqual(first, second)


class PredicateTests(unittest.TestCase):
    def test_a_plain_material_is_shared(self):
        self.assertFalse(SC.is_map_scoped_material("brick/brickwall001a"))

    def test_a_cubemap_patched_material_stays_with_its_map(self):
        self.assertTrue(SC.is_map_scoped_material("asphalt/asphaltasan@cubemapdefault"))

    def test_a_decal_stays_with_its_map(self):
        self.assertTrue(SC.is_map_scoped_material("decals/stains/blooda", decal=True))

    def test_a_wetness_driven_surface_stays_with_its_map(self):
        self.assertTrue(SC.is_map_scoped_material("concrete/wet", wetness_driven=True))

    def test_a_material_only_this_maps_pakfile_carries_stays_with_its_map(self):
        """The corpus reads the install, so it never holds a VBSP-written map-local material.

        Called shared, no package would author it and the surface would bind the master's own
        placeholder rather than failing.
        """
        self.assertTrue(SC.is_map_scoped_material(
            "maps/sm_pier_1/water/invisible_water_depth_33", local=True))


class RigBoneNameTests(unittest.TestCase):
    """`rig_bone_name` must be a fixed point of Control Rig's own sanitizer: every character it
    emits is one `URigHierarchy::SanitizeName` keeps, or the track and the bone diverge again."""

    def test_control_rig_illegal_characters_fold_to_underscores(self):
        from elysium_pipeline.asset_names import rig_bone_name
        self.assertEqual(rig_bone_name("[2]upper_teeth"), "_2_upper_teeth")
        self.assertEqual(rig_bone_name("[2]GeoSphere02"), "_2_GeoSphere02")

    def test_legal_names_pass_verbatim(self):
        from elysium_pipeline.asset_names import rig_bone_name
        for name in ("Bip01 L Finger0", "lower_teeth", "bush hook", "a-b.c|d"):
            self.assertEqual(rig_bone_name(name), name)

    def test_a_leading_space_folds_and_a_later_space_does_not(self):
        from elysium_pipeline.asset_names import rig_bone_name
        self.assertEqual(rig_bone_name(" tail bone"), "_tail bone")


class FileNameTests(unittest.TestCase):
    def test_every_product_of_one_key_is_a_distinct_file(self):
        key = "models/scenery/structural/doorknoba/doorknob1"
        names = {SC.texture_file(key, suffix) for suffix in SC.ROLES}
        self.assertEqual(len(names), len(SC.ROLES))
        self.assertIn("models_scenery_structural_doorknoba_doorknob1.png", names)
        self.assertIn("models_scenery_structural_doorknoba_doorknob1_ke.png", names)

    def test_the_corpus_relative_path_is_what_materials_json_records(self):
        self.assertEqual(
            SC.texture_rel("metal/metalox", SC.NORMAL), "tex/metal_metalox_n.png")

    def test_a_sky_face_is_addressed_by_the_sky_not_by_the_map(self):
        # The map export writes these as the map-local alias `sky_<face>`, which is why two maps
        # sharing one sky held two copies under one name.
        self.assertEqual(SC.sky_face_file("nightsky1", "bk"), "skybox_nightsky1bk.png")
        self.assertEqual(
            SC.sky_face_file("nightsky1", "bk"), SC.sky_face_file("NightSky1", "bk"))

    def test_the_sky_prefix_is_what_the_runtime_appends_its_faces_to(self):
        prefix = SC.sky_face_prefix("nightsky1")
        self.assertEqual(prefix, "skybox_nightsky1")
        for face in SC.SKY_FACES:
            self.assertEqual(prefix + face + ".png", SC.sky_face_file("nightsky1", face))

    def test_asset_names_follow_the_established_folds(self):
        self.assertEqual(SC.texture_asset("metal_metalox_n.png"), "T_metal_metalox_n")
        self.assertEqual(SC.material_asset("models/scenery/spike"), "MI_models_scenery_spike")
        self.assertEqual(SC.mesh_asset("models_scenery_doorknoba"), "SM_models_scenery_doorknoba")

    def test_baked_paths_sit_on_the_shared_mount(self):
        self.assertEqual(
            SC.baked_mesh("models_scenery_doorknoba"),
            "/ElysiumBaked/Shared/Meshes/SM_models_scenery_doorknoba",
        )


class ManifestTests(unittest.TestCase):
    def _textures(self):
        return {
            "metal/metalox": {
                "files": {"metal_metalox.png": "albedo"}, "alpha": False},
            "glass/glass01": {
                "files": {"glass_glass01.png": "albedo",
                          "glass_glass01_n.png": "normal"}, "alpha": True},
        }

    def test_a_manifest_round_trips_its_own_check(self):
        document = SC.build_manifest(
            textures=self._textures(),
            materials={"brick/brickwall001a": {"map_scoped": False}},
            models={"models_scenery_doorknoba": {
                "model": "models/scenery/doorknoba.mdl", "materials": ["models/scenery/spike"]}},
            fingerprint="abc",
        )
        self.assertIs(SC.check_manifest(document), document)
        self.assertEqual(
            SC.model_record(document, "models_scenery_doorknoba")["model"],
            "models/scenery/doorknoba.mdl",
        )
        self.assertEqual(SC.shared_material_keys(document), ["brick/brickwall001a"])

    def test_texture_files_is_the_whole_wanted_set(self):
        document = SC.build_manifest(textures=self._textures(), materials={}, models={})
        self.assertEqual(
            SC.texture_files(document),
            {"metal_metalox.png": "albedo",
             "glass_glass01.png": "albedo",
             "glass_glass01_n.png": "normal"},
        )

    def test_two_keys_folding_to_one_file_name_is_a_named_failure(self):
        with self.assertRaises(ValueError) as caught:
            SC.build_manifest(
                textures={"a/b c": {"files": {"a_b_c.png": "albedo"}},
                          "a/b_c": {"files": {"a_b_c.png": "albedo"}}},
                materials={}, models={})
        self.assertIn("collision", str(caught.exception))

    def test_the_fingerprint_ignores_discovery_order(self):
        first = SC.corpus_fingerprint(["b", "a"], ["y", "x"], ["q", "p"])
        second = SC.corpus_fingerprint(["a", "b"], ["x", "y"], ["p", "q"])
        self.assertEqual(first, second)
        self.assertNotEqual(first, SC.corpus_fingerprint(["a"], ["x", "y"], ["p", "q"]))

    def test_a_stale_version_names_the_command_that_regenerates_it(self):
        document = SC.build_manifest(textures={}, materials={}, models={})
        document["version"] = SC.VERSION + 1
        with self.assertRaises(ValueError) as caught:
            SC.check_manifest(document)
        self.assertIn("export bundle corpus", str(caught.exception))

    def test_a_placed_stem_the_corpus_never_decoded_is_named(self):
        document = SC.build_manifest(
            textures={}, materials={},
            models={"models_scenery_doorknoba": {"model": "models/scenery/doorknoba.mdl"}})
        placed = ["models_scenery_doorknoba", "models_scenery_ashtray",
                  "models_scenery_ashtray", ""]
        self.assertEqual(SC.missing_models(document["models"], placed),
                         ["models_scenery_ashtray"])

    def test_every_texture_bearing_field_is_declared_a_channel(self):
        """`CHANNEL_FIELDS` is how a texture is traced back to the materials that draw it.

        A field that states a decoded file but is left out of the list makes that texture
        untraceable, so `export texture` would re-decode nothing and report success.
        """
        record = SC.material_record({
            "albedo": "metal/metalox", "selfillum": True, "bump": "metal/metalox_bump",
            "refract_map": "glass/warp", "envmap": "env_cubemap", "envmask": "metal/mask",
            "base_tex2": "ground/dirt", "water": True, "water_normal": "water/ripples",
        })
        stated = {name for name, value in record.items()
                  if isinstance(value, str) and value.startswith(SC.TEX + "/")}
        self.assertEqual(set(SC.CHANNEL_FIELDS), stated)

    def test_a_materials_document_round_trips_its_own_check(self):
        document = SC.build_materials(
            {"models/scenery/spike": {"albedo": "tex/models_scenery_spike.png",
                                      "scissor": True}},
            fingerprint="abc")
        self.assertIs(SC.check_materials(document), document)
        self.assertTrue(SC.material_definition(document, "models/scenery/spike")["scissor"])
        self.assertIsNone(SC.material_definition(document, "absent"))


class BakeScopeContractTests(unittest.TestCase):
    """The bake's two material packages, asserted at the source level.

    `bake_lib` imports `unreal`, so the split cannot be exercised directly here. What can be
    asserted is that no mesh stage reaches into one package by hand: a world surface's material
    lives in the map's package or the corpus's depending on `is_map_scoped_material`, and a bare
    lookup in either one silently binds nothing for the other half.
    """

    @staticmethod
    def _bake_map() -> str:
        from pathlib import Path

        repo = Path(__file__).resolve().parents[2]
        return (repo / "pipeline" / "unreal" / "bake_map.py").read_text(encoding="utf-8")

    def test_world_surfaces_bind_through_the_scope_resolver(self):
        source = self._bake_map()
        self.assertIn("def material_for(self, key):", source)
        self.assertNotIn("self.materials.get((self.mat_pkg, name))", source)
        self.assertEqual(source.count("materials = [self.material_for(name) for name in names]"), 3)

    def test_the_resolver_asks_the_shared_predicate(self):
        self.assertIn("SC.is_map_scoped_material(key, decal=mat.decal", self._bake_map())

    def test_a_prop_slot_that_binds_nothing_is_a_named_failure(self):
        """Appending an unresolved lookup builds a mesh with a null material slot, which renders
        the engine's default checker; the receipt then reports that mesh as current."""
        source = self._bake_map()
        self.assertNotIn("materials.append(self.materials.get((self.shared_mat_pkg, key)))",
                         source)
        self.assertIn('fail("%s: slot %r resolves no loaded material for key %r"', source)

    def test_an_unbound_prop_keeps_its_existing_package_off_the_prune_list(self):
        """The unresolved-material `continue` leaves the prop unreceipted so the next run
        retries it, but the trailing `prune_package_prefix` deletes every SM_ name not in
        `wanted` -- so the same branch must still add the prop's name to `wanted`, or an
        abandoned prop's still-good package is deleted from the mount instead of just retried.
        """
        source = self._bake_map()
        marker = "if materials is None:\n                    unbound += 1\n"
        self.assertIn(marker, source)
        skip = source.split(marker, 1)[1]
        guard, _, _rest = skip.partition("continue")
        self.assertIn('wanted.add(asset_path.rsplit("/", 1)[-1])', guard)

    @staticmethod
    def _bake_lib() -> str:
        from pathlib import Path

        repo = Path(__file__).resolve().parents[2]
        return (repo / "pipeline" / "unreal" / "bake_lib.py").read_text(encoding="utf-8")

    def test_only_the_corpus_scope_can_prune_the_corpus(self):
        """The sharpest risk of one shared package: a map's wanted set is not the corpus's.

        A map, or a single-unit run, knows the handful of assets it wanted. Pruning a shared
        package against that would delete every other map's textures, materials and meshes.
        """
        library = self._bake_lib()
        self.assertIn("def _assert_prunable(package, scope):", library)
        self.assertIn("package.startswith(shared_corpus.BAKED_ROOT) and scope != shared_corpus.SCOPE",
                      library)
        source = self._bake_map()
        self.assertEqual(source.count("prune_scope = SC.SCOPE"), 1)
        self.assertEqual(source.count('prune_scope = ""'), 1)
        # Every prune states its scope; a bare call would take the default and pass the guard.
        self.assertEqual(source.count("bl.prune_package"), source.count("self.prune_scope"))

    def test_no_wetness_site_reads_the_authored_flag_directly(self):
        """`wetness_driven` is the material's authored fact; `wet` is what the surface runs.

        A decal carries neither wetness parameter nor a source cube -- it bakes onto M_Decal and
        the wall it projects onto owns the wetness. A site that counted, fingerprinted or bound
        `wetness_driven` would pull a decal into the wet set, which is how sm_hub_1's closure
        guard came to see 16 surfaces for 14 wet materials.
        """
        source = self._bake_map()
        self.assertIn("wet_materials = [mat for mat in self.world_mats.values() if mat.wet]",
                      source)
        self.assertIn("if mat.wet:\n            values[\"weather\"]", source)
        self.assertIn("if mat.wet:\n            bl.set_scalar_param(mic, \"WetnessDriven\"", source)
        self.assertIn("if not mat.wet or mat.env_cube in self.cubemaps:", source)

    @staticmethod
    def _exporter() -> str:
        from pathlib import Path

        repo = Path(__file__).resolve().parents[2]
        return (repo / "pipeline" / "src" / "elysium_pipeline" / "exporters"
                / "UE_bsp_to_scene.py").read_text(encoding="utf-8")

    def test_a_sidecar_states_the_hop_out_to_the_corpus(self):
        """A map sidecar's texture path is joined onto the MAP directory at load.

        `../tex/...` resolves beside the map directory, where nothing lives; the corpus is
        `../shared/tex/...`, which is what `map_relative` writes. Hand-built hops are how the
        water normal came to point at a file that does not exist.
        """
        source = self._exporter()
        self.assertNotIn('f"normalmap ../', source)
        self.assertIn("shared_corpus.map_relative(os.path.basename(w[\"water_normal\"]))", source)

    def test_the_pakfile_local_fallback_checks_the_decoded_set(self):
        source = self._exporter()
        self.assertIn("shared_corpus.material_record(channels, corpus_files)", source)

    def test_a_map_bakes_no_prop_stage(self):
        source = self._bake_map()
        self.assertIn('ALL_STAGES = ("textures", "materials", "world", "sky", "particles", "level")',
                      source)
        self.assertNotIn("ITEMS_SCOPE", source)


class MaterialRecordTests(unittest.TestCase):
    def _channels(self, **overrides):
        base = {
            "material": "spike", "vmt": "models/scenery/spike", "albedo": "models/scenery/spike",
            "needs_alpha": False, "selfillum": False, "additive": False, "translucent": False,
            "alphatest": False, "glass": False, "envmap": "", "envmap_path": "", "envmask": "",
            "envmask_from_alpha": False, "envtint": None, "globalwetness": None, "bump": "",
            "base_tex2": "", "refract": False, "refract_amount": 0.0, "refract_map": "",
            "refract_is_dudv": False, "iris": "", "vampire": False, "water": False,
            "water_normal": "", "water_fog_color": None, "water_fog_start": None,
            "water_fog_end": None, "water_reflect_tint": None, "decal_scale": 0.0, "unlit": False,
        }
        base.update(overrides)
        return base

    def test_a_channel_whose_file_the_corpus_lacks_is_stated_empty(self):
        # A record that pointed at a file the decode never wrote would bake as the master's own
        # placeholder, which is exactly the silent grey surface the guard exists to prevent.
        record = SC.material_record(self._channels(), files=set())
        self.assertEqual(record["albedo"], "")
        self.assertEqual(record["albedo_key"], "models/scenery/spike")

    def test_a_present_channel_is_stated_corpus_relative(self):
        record = SC.material_record(
            self._channels(), files={"models_scenery_spike.png"})
        self.assertEqual(record["albedo"], "tex/models_scenery_spike.png")

    def test_without_a_file_set_every_named_channel_is_stated(self):
        # `files=None` is "state what the VMT names": the caller has no decoded set to check
        # against. Every caller that does have one passes it, including the map exporter's
        # PAKFILE-local fallback -- a local material can name a texture the corpus never decoded.
        record = SC.material_record(self._channels())
        self.assertEqual(record["albedo"], "tex/models_scenery_spike.png")

    def test_an_authored_bumpmap_outranks_the_derived_glass_normal(self):
        record = SC.material_record(
            self._channels(glass=True, bump="models/scenery/authored"))
        self.assertEqual(record["bump"], "tex/models_scenery_authored_n.png")

    def test_semantic_glass_falls_back_to_the_normal_derived_from_its_albedo(self):
        record = SC.material_record(self._channels(glass=True))
        self.assertEqual(record["bump"], "tex/models_scenery_spike_glass_n.png")

    def test_a_water_material_carries_its_own_normal_and_fog(self):
        # The Water shader names no $basetexture, so a record built only from albedo would drop
        # every canal and sewer in the game.
        record = SC.material_record(self._channels(
            albedo="", water=True, water_normal="dev/water_normal",
            water_fog_end=1024.0, water_reflect_tint=[0.5, 0.6, 0.7]))
        self.assertTrue(record["water"])
        self.assertEqual(record["water_normal"], "tex/dev_water_normal_n.png")
        self.assertEqual(record["water_fog_end"], 1024.0)
        self.assertEqual(record["water_reflect_tint"], [0.5, 0.6, 0.7])

    def test_the_base_alpha_env_mask_reads_the_albedo_key(self):
        record = SC.material_record(
            self._channels(envmap="env_cubemap", envmask_from_alpha=True))
        self.assertEqual(record["env_mask"], "tex/models_scenery_spike_envmask.png")


if __name__ == "__main__":
    unittest.main()
