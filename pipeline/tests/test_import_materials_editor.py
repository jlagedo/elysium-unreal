"""`pipeline/unreal/import_materials.py`, the editor half of `uv run elysium import materials`,
exercised against a faked editor.

The script is an editor entry point, so the module is loaded with a fake `unreal` whose asset
registry, `MaterialEditingLibrary`, asset tools and provenance library record what the script asked
of them. The fake keeps one dict of assets keyed by package path; stamping through
`set_metadata_tag` and reading back through `bake_lib.stored_recipe` both go through it, so reuse
is proved end to end rather than by patching the decision.
"""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]
ROOT = "/ElysiumBaked/Materials"
MASTER_ROOT = "/Game/ElysiumGenerated/Materials/V2"
M_LIT = MASTER_ROOT + "/M_V2_Lit"
M_UNLIT = MASTER_ROOT + "/M_V2_Unlit"


# --- the fake editor ----------------------------------------------------------------------------


class FakeBasePropertyOverrides:
    def __init__(self):
        self.props = {
            "override_blend_mode": False, "blend_mode": None,
            "override_two_sided": False, "two_sided": False,
            "override_opacity_mask_clip_value": False, "opacity_mask_clip_value": 0.3333,
        }

    def get_editor_property(self, name):
        return self.props[name]

    def set_editor_property(self, name, value):
        self.props[name] = value


class FakeLinearColor:
    def __init__(self, r=0.0, g=0.0, b=0.0, a=0.0):
        self.value = (r, g, b, a)

    def __eq__(self, other):
        return isinstance(other, FakeLinearColor) and self.value == other.value

    def __repr__(self):
        return "FakeLinearColor%r" % (self.value,)


class FakeStruct:
    """A generated USTRUCT wrapper: named properties, and nothing else."""

    def __init__(self, **props):
        self.props = dict(props)

    def get_editor_property(self, name):
        return self.props[name]

    def set_editor_property(self, name, value):
        self.props[name] = value


def _parameter_value(name, value):
    """One `FScalarParameterValue`/`FVectorParameterValue`/`FTextureParameterValue` row, with the
    `FMaterialParameterInfo` a global parameter carries (`Index` = `INDEX_NONE`)."""
    return FakeStruct(parameter_info=FakeStruct(name=name, index=-1), parameter_value=value)


#: The three `UMaterialInstance` parameter-value arrays `bake_lib` merges its writes into.
PARAMETER_ARRAYS = (
    "texture_parameter_values", "scalar_parameter_values", "vector_parameter_values")


class FakeAsset:
    """One loaded asset. A `MaterialInstanceConstant` additionally carries the parameter state
    `MaterialEditingLibrary` mutates; any other class (a master, a texture, a physical material)
    is just an identity `unreal.load_asset` can hand back.

    The scalar/vector/texture parameters live where the real ones do -- in the instance's
    `*_parameter_values` arrays, reached through `get_editor_property`/`set_editor_property` --
    because that is the seam `bake_lib.set_tex_param` and its siblings write through now: one
    value write each, then one `update_material_instance` for the instance."""

    def __init__(self, path, class_name):
        self.path = path
        self.class_name = class_name
        self.metadata = {}
        self.sidecar = None
        self.parent = None
        self.phys_material = None
        self.base_property_overrides = FakeBasePropertyOverrides()
        self.parameter_values = {name: [] for name in PARAMETER_ARRAYS}
        self.switches = {}
        self.pending_switches = {}
        self.update_calls = 0

    # -- the parameter arrays, resolved by name the way a reader wants to assert on them --
    def resolved(self, array_property):
        return {str(row.get_editor_property("parameter_info").get_editor_property("name")):
                row.get_editor_property("parameter_value")
                for row in self.parameter_values[array_property]}

    @property
    def textures(self):
        return self.resolved("texture_parameter_values")

    @property
    def scalars(self):
        return self.resolved("scalar_parameter_values")

    @property
    def vectors(self):
        return self.resolved("vector_parameter_values")

    def set_parameter(self, array_property, name, value):
        """`UMaterialInstance::Set*ParameterValueInternal`: the row for this name, or a new one."""
        for row in self.parameter_values[array_property]:
            if str(row.get_editor_property("parameter_info").get_editor_property("name")) == name:
                row.set_editor_property("parameter_value", value)
                return
        self.parameter_values[array_property].append(_parameter_value(name, value))

    def get_class(self):
        return SimpleNamespace(get_name=lambda: self.class_name)

    def get_path_name(self):
        return "%s.%s" % (self.path, self.path.rsplit("/", 1)[-1])

    def get_editor_property(self, name):
        if name in self.parameter_values:
            return list(self.parameter_values[name])
        if name == "parent":
            return self.parent
        if name == "phys_material":
            return self.phys_material
        if name == "base_property_overrides":
            return self.base_property_overrides
        raise KeyError(name)

    def set_editor_property(self, name, value, notify_mode=None):
        if name in self.parameter_values:
            self.parameter_values[name] = list(value)
        elif name == "parent":
            self.parent = value
        elif name == "phys_material":
            self.phys_material = value
        elif name == "base_property_overrides":
            self.base_property_overrides = value
        else:
            raise KeyError(name)


class FakeEditor:
    """One registry of assets plus the calls the script makes against it."""

    def __init__(self):
        self.assets = {}                 # package path -> FakeAsset
        self.created = []                # (name, package, class name)
        self.deleted = []
        self.saved = []
        self.made_dirs = []
        self.logs, self.warnings, self.errors = [], [], []
        self.scanned = []
        self.command_line = ""
        #: What `create_asset` yields for a name; the class it builds, or None to refuse.
        self.creates_class = {}
        #: Names whose `apply_json` refuses, with the reason.
        self.apply_refuses = {}
        self.save_refuses = set()
        #: assetPath -> pixel-shader-instruction count `get_statistics` reports; default 128.
        self.compile_instructions = {}
        #: assetPath -> `list_shaders` row names; default `_DEFAULT_SHADER_TYPE_NAMES`.
        self.shader_type_names = {}
        self.compile_calls = []          # asset paths probed, in order
        self.parent_calls = []           # (mic path, parent path)
        self.readback_calls = []         # (mic path, parameter)

    # -- asset tools --
    def create_asset(self, name, package, asset_class, factory):
        built = self.creates_class.get(name, asset_class.name)
        self.created.append((name, package, built))
        if built is None:
            return None
        asset = FakeAsset("%s/%s" % (package, name), built)
        self.assets[asset.path] = asset
        return asset

    # -- EditorAssetLibrary --
    def does_directory_exist(self, target):
        return True

    def make_directory(self, target):
        self.made_dirs.append(target)
        return True

    def does_asset_exist(self, target):
        return target in self.assets

    def load_asset(self, target):
        return self.assets.get(target)

    def list_assets(self, package, recursive=True, include_folder=False):
        prefix = package.rstrip("/") + "/"
        return [asset.get_path_name() for path, asset in self.assets.items()
                if path.startswith(prefix)]

    def get_metadata_tag(self, asset, tag):
        return asset.metadata.get(tag, "")

    def set_metadata_tag(self, asset, tag, value):
        asset.metadata[tag] = value

    def save_asset(self, target, only_if_is_dirty=True):
        if target in self.save_refuses:
            return False
        self.saved.append(target)
        return True

    def delete_asset(self, target):
        self.deleted.append(target)
        return self.assets.pop(target, None) is not None

    def delete_loaded_assets(self, loaded):
        for asset in loaded:
            self.deleted.append(asset.path)
            self.assets.pop(asset.path, None)
        return True

    def delete_directory(self, target):
        return True

    def find_asset_data(self, target):
        asset = self.assets.get(target)
        name = asset.class_name if asset else ""
        return SimpleNamespace(asset_class_path=SimpleNamespace(asset_name=name))

    # -- registry --
    def get_asset_by_object_path(self, object_path):
        asset = self.assets.get(object_path.split(".", 1)[0])
        if asset is None:
            return None
        return SimpleNamespace(
            is_valid=lambda: True,
            get_tag_value=lambda tag, a=asset: a.metadata.get(tag, ""))

    def scan_paths_synchronous(self, paths, force_rescan=False):
        self.scanned = list(paths)

    # -- MaterialEditingLibrary --
    def clear_all_material_instance_parameters(self, mic):
        # Matches the real `clear_all_material_instance_parameters`, which the whole review pass
        # (finding 2) is about: it clears non-static parameters only. A static switch a previous
        # run set survives this call untouched -- `mic.switches` is deliberately NOT cleared here,
        # so a test that reuses `mic` across two `module.run` calls can prove the entry's own
        # full-state `switches` dict is what actually clears a stale value, not this call.
        for rows in mic.parameter_values.values():
            rows.clear()

    def set_material_instance_parent(self, mic, parent):
        self.parent_calls.append((mic.path, getattr(parent, "path", parent)))
        mic.parent = parent

    def set_material_instance_texture_parameter_value(self, mic, name, value):
        mic.set_parameter("texture_parameter_values", name, value)

    def get_material_instance_texture_parameter_value(self, mic, name):
        self.readback_calls.append((mic.path, name))
        return mic.textures.get(name)

    def set_material_instance_scalar_parameter_value(self, mic, name, value):
        mic.set_parameter("scalar_parameter_values", name, value)

    def set_material_instance_vector_parameter_value(self, mic, name, value):
        mic.set_parameter("vector_parameter_values", name, value)

    def get_material_instance_static_switch_parameter_value(self, mic, name):
        # The effective value: a pending (not-yet-applied) write wins, else whatever the last
        # `update_material_instance` landed, else the fake's own "nothing set yet" default
        # (`False`, matching every master's own switches in this test file).
        if name in mic.pending_switches:
            return mic.pending_switches[name]
        return mic.switches.get(name, False)

    def set_material_instance_static_switch_parameter_value(self, mic, name, value,
                                                             update_material_instance=True):
        mic.pending_switches[name] = value
        if update_material_instance:
            self.update_material_instance(mic)

    def update_material_instance(self, mic):
        mic.switches.update(mic.pending_switches)
        mic.pending_switches.clear()
        mic.update_calls += 1

    def get_statistics(self, mic):
        self.compile_calls.append(mic.path)
        instructions = self.compile_instructions.get(mic.path, 128)
        return SimpleNamespace(num_pixel_shader_instructions=instructions)

    #: assetPath -> `list_shaders` row names, default a realistic base-pass/depth/hit-proxy set
    #: (review finding 3: the probe now requires all three to be present).
    _DEFAULT_SHADER_TYPE_NAMES = (
        "TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy",
        "TBasePassPSFPrecomputedVolumetricLightmapLightingPolicy",
        "FDepthOnlyVS", "FDepthOnlyPS", "FHitProxyVS", "FHitProxyPS",
    )

    def get_num_shader_types(self, mic):
        return len(self.shader_type_names.get(mic.path, self._DEFAULT_SHADER_TYPE_NAMES))

    def list_shaders(self, mic):
        names = self.shader_type_names.get(mic.path, self._DEFAULT_SHADER_TYPE_NAMES)
        return [SimpleNamespace(vertex_factory_name="FLocalVertexFactory", shader_type_name=name)
                for name in names]


def _fake_unreal(editor):
    def apply_json(mic, text):
        name = mic.path.rsplit("/", 1)[-1]
        if name in editor.apply_refuses:
            return None, editor.apply_refuses[name]
        try:
            mic.sidecar = json.loads(text)
        except ValueError as exc:
            return None, "bad json: %s" % exc
        return mic.sidecar, ""

    def stamp_registry_tags(mic):
        if mic.sidecar is None:
            return False, "material carries no ElysiumMaterialProvenance"
        mic.metadata["ElysiumAssetId"] = mic.sidecar.get("assetId", "")
        mic.metadata["ElysiumMaster"] = mic.sidecar.get("master", "") or ""
        mic.metadata["ElysiumShaderProgram"] = mic.sidecar.get("shader", "") or ""
        return True, ""

    def _enum(prefix, *names):
        return SimpleNamespace(**{name: "%s.%s" % (prefix, name) for name in names})

    library = SimpleNamespace(
        does_directory_exist=editor.does_directory_exist,
        make_directory=editor.make_directory,
        does_asset_exist=editor.does_asset_exist,
        load_asset=editor.load_asset,
        list_assets=editor.list_assets,
        get_metadata_tag=editor.get_metadata_tag,
        set_metadata_tag=editor.set_metadata_tag,
        save_asset=editor.save_asset,
        delete_asset=editor.delete_asset,
        delete_loaded_assets=editor.delete_loaded_assets,
        delete_directory=editor.delete_directory,
        find_asset_data=editor.find_asset_data,
    )
    registry = SimpleNamespace(
        get_asset_by_object_path=editor.get_asset_by_object_path,
        scan_paths_synchronous=editor.scan_paths_synchronous,
    )
    material_editing = SimpleNamespace(
        clear_all_material_instance_parameters=editor.clear_all_material_instance_parameters,
        set_material_instance_parent=editor.set_material_instance_parent,
        set_material_instance_texture_parameter_value=(
            editor.set_material_instance_texture_parameter_value),
        get_material_instance_texture_parameter_value=(
            editor.get_material_instance_texture_parameter_value),
        set_material_instance_scalar_parameter_value=(
            editor.set_material_instance_scalar_parameter_value),
        set_material_instance_vector_parameter_value=(
            editor.set_material_instance_vector_parameter_value),
        get_material_instance_static_switch_parameter_value=(
            editor.get_material_instance_static_switch_parameter_value),
        set_material_instance_static_switch_parameter_value=(
            editor.set_material_instance_static_switch_parameter_value),
        update_material_instance=editor.update_material_instance,
        get_statistics=editor.get_statistics,
        get_num_shader_types=editor.get_num_shader_types,
        list_shaders=editor.list_shaders,
    )
    return SimpleNamespace(
        AssetToolsHelpers=SimpleNamespace(
            get_asset_tools=lambda: SimpleNamespace(create_asset=editor.create_asset)),
        MaterialEditingLibrary=material_editing,
        GeometryScript_Collision=object(),
        EditorAssetLibrary=library,
        AssetRegistryHelpers=SimpleNamespace(get_asset_registry=lambda: registry),
        SystemLibrary=SimpleNamespace(
            get_command_line=lambda: editor.command_line, collect_garbage=lambda: None),
        MaterialInstanceConstant=SimpleNamespace(name="MaterialInstanceConstant"),
        MaterialInstanceConstantFactoryNew=lambda: SimpleNamespace(),
        # The parameter-value structs `bake_lib` appends to an instance's arrays, and the notify
        # mode it writes those arrays under. `MaterialParameterInfo` carries the engine struct's
        # own defaults for a global parameter, which is what `bake_lib` relies on.
        MaterialParameterInfo=lambda: FakeStruct(name="", index=-1),
        ScalarParameterValue=FakeStruct,
        VectorParameterValue=FakeStruct,
        TextureParameterValue=FakeStruct,
        PropertyAccessChangeNotifyMode=_enum(
            "PropertyAccessChangeNotifyMode", "DEFAULT", "NEVER", "ALWAYS"),
        BlendMode=_enum("BLEND", "BLEND_OPAQUE", "BLEND_MASKED", "BLEND_TRANSLUCENT",
                        "BLEND_ADDITIVE", "BLEND_MODULATE"),
        LinearColor=FakeLinearColor,
        ElysiumMaterialProvenance=SimpleNamespace(
            apply_json=apply_json, stamp_registry_tags=stamp_registry_tags),
        load_asset=editor.load_asset,
        # `PyCore.cpp`'s module-level sweep, the one `import_materials._collect_garbage` calls.
        collect_garbage=lambda: None,
        log=editor.logs.append,
        log_warning=editor.warnings.append,
        log_error=editor.errors.append,
    )


def _load(editor):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_import_materials", REPO / "pipeline/unreal/import_materials.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    fake = _fake_unreal(editor)
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        try:
            spec.loader.exec_module(module)   # main() exits at once: no -ImportMaterials=
        except SystemExit:
            pass
    module.unreal = fake
    return module


def _add_master(editor, path):
    editor.assets[path] = FakeAsset(path, "Material")
    return editor.assets[path]


def _add_texture(editor, path):
    editor.assets[path] = FakeAsset(path, "Texture2D")
    return editor.assets[path]


def _add_phys(editor, path):
    editor.assets[path] = FakeAsset(path, "ElysiumPhysicalMaterial")
    return editor.assets[path]


# --- staging fixtures ---------------------------------------------------------------------------


def _entry(key, **over):
    entry = {
        "assetPath": "%s/art/MI_%s" % (ROOT, key),
        "unit": "vtmb:material:art/%s" % key,
        "unitGlb": "materials/art/%s.glb" % key,
        "unitSha256": "sha-" + key,
        "parent": M_LIT,
        "patched": False,
        "provenanceOnly": False,
        "textures": {"BaseTexture": "/ElysiumBaked/Textures/art/T_%s" % key},
        "scalars": {"SurfaceClassIndex": 0},
        "vectors": {"EnvMapTint": [1.0, 1.0, 1.0, 1.0]},
        "switches": {},
        "basePropertyOverrides": {"blendMode": "Opaque"},
        "physMaterial": "/ElysiumBaked/SurfaceProperties/PM_default",
        "surfaceClass": "default",
        "surfaceClassIndex": 0,
        "provenance": "art/%s.provenance.json" % key,
        "recipe": {
            "unitSha256": "sha-" + key, "parent": M_LIT,
            "settingsVersion": "elysium-material-import-v1",
            "params": {"textures": {"BaseTexture": "/ElysiumBaked/Textures/art/T_%s" % key},
                       "scalars": {"SurfaceClassIndex": 0},
                       "vectors": {"EnvMapTint": [1.0, 1.0, 1.0, 1.0]},
                       "switches": {}, "basePropertyOverrides": {"blendMode": "Opaque"}},
        },
    }
    entry.update(over)
    return entry


def _stage(tmp_path, entries, keep=(), select=None, master_root=MASTER_ROOT):
    """Write a manifest plus the sidecar each entry names."""
    for entry in entries:
        (tmp_path / entry["provenance"]).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / entry["provenance"]).write_text(json.dumps({
            "assetId": entry["unit"],
            "master": entry["parent"] if not entry["patched"] else None,
            "shader": "vertexlitgeneric",
        }), encoding="utf-8")
    manifest = {"schemaVersion": "1.0.0", "settingsVersion": "elysium-material-import-v1",
                "packageRoot": ROOT, "masterRoot": master_root, "select": select,
                "pruneScope": ROOT + "/", "keep": list(keep), "stageFailures": [],
                "provenanceOnly": [], "assets": entries}
    path = tmp_path / "manifest.json"
    path.write_text(json.dumps(manifest), encoding="utf-8")
    return str(path)


def _report(tmp_path):
    return json.loads((tmp_path / "import_report.json").read_text(encoding="utf-8"))


def _base_editor():
    editor = FakeEditor()
    _add_master(editor, M_LIT)
    _add_master(editor, M_UNLIT)
    _add_phys(editor, "/ElysiumBaked/SurfaceProperties/PM_default")
    return editor


# --- the run -------------------------------------------------------------------------------------


def test_a_fresh_run_creates_binds_probes_stamps_and_saves(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    entries = [_entry("brick")]
    manifest = _stage(tmp_path, entries)

    report = module.run(manifest, force=False)

    assert report.failures == []
    assert (report.built, report.reused, report.pruned) == (1, 0, 0)
    assert editor.scanned == [ROOT, MASTER_ROOT]
    assert [(name, built) for name, _, built in editor.created] == [
        ("MI_brick", "MaterialInstanceConstant")]

    mic = editor.assets[ROOT + "/art/MI_brick"]
    assert mic.parent is editor.assets[M_LIT]
    assert mic.textures["BaseTexture"] is editor.assets["/ElysiumBaked/Textures/art/T_brick"]
    assert mic.scalars["SurfaceClassIndex"] == 0
    assert mic.vectors["EnvMapTint"] == FakeLinearColor(1.0, 1.0, 1.0, 1.0)
    assert mic.phys_material is editor.assets["/ElysiumBaked/SurfaceProperties/PM_default"]
    assert mic.base_property_overrides.props["override_blend_mode"] is True
    assert mic.base_property_overrides.props["blend_mode"] == "BLEND.BLEND_OPAQUE"
    assert mic.sidecar["assetId"] == "vtmb:material:art/brick"
    assert mic.metadata["ElysiumRecipe"] == module.bl.recipe_fingerprint(
        "materials", entries[0]["assetPath"], entries[0]["recipe"])
    assert editor.saved == [entries[0]["assetPath"]]
    # Exactly one compile probe for the sole (parent, switches, blendMode) permutation realised.
    assert editor.compile_calls == [mic.path]

    written = _report(tmp_path)
    assert written["imported"] == 1 and written["failed"] == []
    assert len(written["compiledPermutations"]) == 1
    assert written["compiledPermutations"][0]["numPixelShaderInstructions"] == 128


def test_source_members_sha256_merges_into_the_sidecar_under_its_new_name(tmp_path):
    """Review finding 9: the manifest entry's `sourceMembersSha256` (renamed from `sourceSha256`)
    merges into the sidecar object under the same name `UElysiumMaterialProvenance::FromJson`
    reads (`ElysiumMaterialProvenance.cpp`'s `Str(O, TEXT("sourceMembersSha256"))`)."""
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    entry = _entry("brick", sourceMembersSha256="deadbeef")
    manifest = _stage(tmp_path, [entry])

    report = module.run(manifest)

    assert report.failures == []
    mic = editor.assets[ROOT + "/art/MI_brick"]
    assert mic.sidecar["sourceMembersSha256"] == "deadbeef"
    assert "sourceSha256" not in mic.sidecar


def test_a_second_run_reuses_every_stamped_asset(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    manifest = _stage(tmp_path, [_entry("brick")])
    module.run(manifest)
    editor.created.clear()
    editor.compile_calls.clear()

    second = module.run(manifest)

    assert second.failures == []
    assert (second.built, second.reused) == (0, 1)
    assert editor.created == []
    # A reused instance is not re-probed.
    assert editor.compile_calls == []


def test_force_re_authors_a_current_asset_in_place(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    manifest = _stage(tmp_path, [_entry("brick")])
    module.run(manifest)
    editor.created.clear()

    forced = module.run(manifest, force=True)

    assert (forced.built, forced.reused) == (1, 0)
    assert editor.created == []
    assert editor.assets[ROOT + "/art/MI_brick"].sidecar is not None


# --- parent ordering ------------------------------------------------------------------------------


def test_a_patched_instance_is_authored_after_its_base(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_base")
    module = _load(editor)
    base = _entry("base")
    patched = _entry(
        "base_1_2_3", parent=ROOT + "/art/MI_base", patched=True,
        textures={}, scalars={}, vectors={}, switches={}, basePropertyOverrides={},
        physMaterial=None, surfaceClass=None, surfaceClassIndex=None,
        recipe={"unitSha256": "sha-patch", "parent": ROOT + "/art/MI_base",
               "settingsVersion": "elysium-material-import-v1",
               "params": {"textures": {}, "scalars": {}, "vectors": {}, "switches": {},
                          "basePropertyOverrides": {}}},
    )
    # Listed child-before-parent in the manifest -- the script must still order the base first.
    manifest = _stage(tmp_path, [patched, base])

    report = module.run(manifest)

    assert report.failures == []
    assert (report.built, report.reused) == (2, 0)
    order = [name for name, _, _ in editor.created]
    assert order.index("MI_base") < order.index("MI_base_1_2_3")
    patched_mic = editor.assets[ROOT + "/art/MI_base_1_2_3"]
    assert patched_mic.parent is editor.assets[ROOT + "/art/MI_base"]
    # No overrides on the patched instance: base-property overrides are inherited, not re-set.
    assert patched_mic.base_property_overrides.props["override_blend_mode"] is False


def test_a_child_fails_when_its_base_fails_without_touching_the_editor(tmp_path):
    editor = _base_editor()
    # No T_base texture staged: the base entry fails on the missing texture.
    module = _load(editor)
    base = _entry("base")
    patched = _entry(
        "base_1_2_3", parent=ROOT + "/art/MI_base", patched=True,
        textures={}, scalars={}, vectors={}, switches={}, basePropertyOverrides={},
        physMaterial=None, surfaceClass=None, surfaceClassIndex=None,
        recipe={"unitSha256": "sha-patch", "parent": ROOT + "/art/MI_base",
               "settingsVersion": "elysium-material-import-v1",
               "params": {}},
    )
    manifest = _stage(tmp_path, [base, patched])

    report = module.run(manifest)

    reasons = {row["assetPath"]: row["reason"] for row in report.failures}
    assert "texture not found" in reasons[ROOT + "/art/MI_base"]
    assert "base instance failed" in reasons[ROOT + "/art/MI_base_1_2_3"]
    # The patched entry never reached the editor at all.
    assert all(created_name != "MI_base_1_2_3" for created_name, _, _ in editor.created)


# --- failure isolation ----------------------------------------------------------------------------


def test_a_missing_texture_fails_only_its_entry(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_wood")
    module = _load(editor)
    manifest = _stage(tmp_path, [_entry("brick"), _entry("wood")])

    report = module.run(manifest)

    assert report.built == 1
    assert len(report.failures) == 1
    assert "texture not found" in report.failures[0]["reason"]
    assert report.failures[0]["assetPath"] == ROOT + "/art/MI_brick"


def test_an_unknown_parameter_name_fails_the_entry_on_read_back(tmp_path):
    """A texture parameter the master does not expose: the write still lands in the instance's
    `TextureParameterValues` array (`Set*ParameterValueInternal` appends unconditionally), but
    `GetTextureParameterValue` resolves against the *master's* parameter set and reports nothing
    for a name it does not carry. The read-back is what must catch that, so the refusal is
    simulated where the engine puts it -- on the read."""
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)

    # A master that silently ignores an unknown parameter reports None on read-back.
    real_getter = editor.get_material_instance_texture_parameter_value

    def refusing_getter(mic, name):
        if name == "EnvMapMask":
            return None
        return real_getter(mic, name)

    editor.get_material_instance_texture_parameter_value = refusing_getter
    module.unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value = refusing_getter

    entry = _entry("brick", textures={
        "BaseTexture": "/ElysiumBaked/Textures/art/T_brick",
        "EnvMapMask": "/ElysiumBaked/Textures/art/T_brick",
    })
    manifest = _stage(tmp_path, [entry])

    report = module.run(manifest)

    assert report.built == 0
    assert "unknown or unbound texture parameter" in report.failures[0]["reason"]


# --- switch batching -------------------------------------------------------------------------------


def test_switches_are_set_unbatched_then_applied_in_one_update_call(tmp_path):
    """`UseNormalMap: False` never gets a `set_...` call at all: a fresh instance's own effective
    value is already `False` (this fake's default, matching a fresh `MaterialInstanceConstant`'s
    inherited-from-master resolution), so the perf optimization -- read the effective value first,
    only write when it disagrees -- skips it. `UseEnvMap: True` genuinely differs, so it is
    written. Both still resolve correctly either way (`get_material_instance_static_switch_
    parameter_value` reads `False` for a name mic.switches never carried)."""
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    entry = _entry("brick", switches={"UseEnvMap": True, "UseNormalMap": False})
    manifest = _stage(tmp_path, [entry])

    report = module.run(manifest)

    assert report.failures == []
    mic = editor.assets[ROOT + "/art/MI_brick"]
    assert mic.switches == {"UseEnvMap": True}
    assert editor.get_material_instance_static_switch_parameter_value(mic, "UseNormalMap") is False
    assert mic.update_calls == 1


def test_no_switches_still_gets_the_one_post_override_update_call(tmp_path):
    # `update_material_instance` is called exactly once per entry regardless of switch count --
    # after base-property overrides land too, not per-switch -- so the static-permutation
    # resource is rebuilt against the entry's final state before the compile probe touches it
    # (see the comment in `_finish_entry`: a `TwoSided`-only trigger for the editor's hit-proxy
    # shader permutation crashes UE 5.8 if probed against a stale snapshot).
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    manifest = _stage(tmp_path, [_entry("brick", switches={})])

    module.run(manifest)

    assert editor.assets[ROOT + "/art/MI_brick"].update_calls == 1


# --- blend override mapping -------------------------------------------------------------------------


@pytest.mark.parametrize("blend, member", [
    ("Opaque", "BLEND.BLEND_OPAQUE"),
    ("Masked", "BLEND.BLEND_MASKED"),
    ("Translucent", "BLEND.BLEND_TRANSLUCENT"),
    ("Additive", "BLEND.BLEND_ADDITIVE"),
    ("Modulate", "BLEND.BLEND_MODULATE"),
])
def test_blend_mode_overrides_map_to_the_named_enum_member(tmp_path, blend, member):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    entry = _entry("brick", basePropertyOverrides={"blendMode": blend})
    manifest = _stage(tmp_path, [entry])

    report = module.run(manifest)

    assert report.failures == []
    mic = editor.assets[ROOT + "/art/MI_brick"]
    assert mic.base_property_overrides.props["blend_mode"] == member
    assert mic.base_property_overrides.props["override_blend_mode"] is True


def test_two_sided_and_opacity_clip_overrides_are_applied_only_when_present(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    entry = _entry("brick", basePropertyOverrides={
        "blendMode": "Masked", "opacityMaskClipValue": 0.5, "twoSided": True})
    manifest = _stage(tmp_path, [entry])

    module.run(manifest)

    mic = editor.assets[ROOT + "/art/MI_brick"]
    props = mic.base_property_overrides.props
    assert props["override_opacity_mask_clip_value"] is True
    assert props["opacity_mask_clip_value"] == 0.5
    assert props["override_two_sided"] is True
    assert props["two_sided"] is True


def test_a_stale_two_sided_and_clip_override_is_explicitly_cleared_on_reimport(tmp_path):
    """Review finding 2: `override_blend_mode`/`override_two_sided`/`override_opacity_mask_clip_value`
    are set explicitly every entry, true-with-a-value or false -- an entry re-imported (`--force`)
    with a plainer state than before must actually clear a stale `True` override, not merely leave
    it untouched because this entry's own overrides no longer mention it."""
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    stale = _entry("brick", basePropertyOverrides={
        "blendMode": "Masked", "opacityMaskClipValue": 0.75, "twoSided": True})
    module.run(_stage(tmp_path, [stale]))
    mic = editor.assets[ROOT + "/art/MI_brick"]
    assert mic.base_property_overrides.props["override_two_sided"] is True

    plain = _entry("brick", basePropertyOverrides={
        "blendMode": "Opaque", "opacityMaskClipValue": None, "twoSided": False})
    module.run(_stage(tmp_path, [plain]), force=True)

    props = mic.base_property_overrides.props
    assert props["override_two_sided"] is True and props["two_sided"] is False
    assert props["override_opacity_mask_clip_value"] is False


def test_a_stale_blend_override_is_cleared_before_the_parent_moves(tmp_path):
    """R7.5: re-parenting compiles the instance, so the overrides must land FIRST on a re-import
    that moves a unit to another master. The `%compilewater` reroute is the case that makes it
    visible -- `water/invisible_water` and `water/cheap_water` author `$translucent 1`, sat on
    `M_V2_Unlit`/`M_V2_LitTranslucent`, and move to the Single Layer Water master, which refuses
    to compile anything but opaque or masked. Asserted at the moment the parent is written, which
    is the only moment the illegal pair could exist."""
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    translucent = _entry("brick", basePropertyOverrides={
        "blendMode": "Translucent", "twoSided": False})
    module.run(_stage(tmp_path, [translucent]))
    mic = editor.assets[ROOT + "/art/MI_brick"]
    assert mic.base_property_overrides.props["blend_mode"] == "BLEND.BLEND_TRANSLUCENT"

    seen = []
    original = editor.set_material_instance_parent

    def watched(instance, parent):
        seen.append((getattr(parent, "path", parent),
                     instance.base_property_overrides.props["blend_mode"]))
        return original(instance, parent)

    editor.set_material_instance_parent = watched
    module = _load(editor)
    rerouted = _entry("brick", parent=M_UNLIT, basePropertyOverrides={
        "blendMode": "Opaque", "twoSided": False})
    module.run(_stage(tmp_path, [rerouted]), force=True)

    assert seen == [(M_UNLIT, "BLEND.BLEND_OPAQUE")]
    assert mic.parent is editor.assets[M_UNLIT]
    assert mic.base_property_overrides.props["blend_mode"] == "BLEND.BLEND_OPAQUE"


def test_a_stale_static_switch_is_explicitly_cleared_on_reimport(tmp_path):
    """The 12 known-stale flipbook/water assets: `clear_all_material_instance_parameters` never
    touches a static switch, so a switch that was `True` in an earlier import and is `False` in
    this one must land as an explicit `set_material_instance_static_switch_parameter_value` call,
    not silently persist."""
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    was_animated = _entry("brick", switches={"UseAnimatedFrames": True})
    module.run(_stage(tmp_path, [was_animated]))
    mic = editor.assets[ROOT + "/art/MI_brick"]
    assert mic.switches["UseAnimatedFrames"] is True

    no_longer_animated = _entry("brick", switches={"UseAnimatedFrames": False})
    module.run(_stage(tmp_path, [no_longer_animated]), force=True)

    assert mic.switches["UseAnimatedFrames"] is False


def test_a_stale_phys_material_is_cleared_when_the_entry_no_longer_wants_one(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    with_phys = _entry("brick", physMaterial="/ElysiumBaked/SurfaceProperties/PM_default")
    module.run(_stage(tmp_path, [with_phys]))
    mic = editor.assets[ROOT + "/art/MI_brick"]
    assert mic.phys_material is not None

    without_phys = _entry("brick", physMaterial=None)
    module.run(_stage(tmp_path, [without_phys]), force=True)

    assert mic.phys_material is None


# --- compile probe --------------------------------------------------------------------------------


def test_the_compile_probe_runs_once_per_realised_permutation(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_a")
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_b")
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_c")
    module = _load(editor)
    same_switches = {"UseEnvMap": True}
    entries = [
        _entry("a", textures={"BaseTexture": "/ElysiumBaked/Textures/art/T_a"},
              switches=same_switches),
        _entry("b", textures={"BaseTexture": "/ElysiumBaked/Textures/art/T_b"},
              switches=same_switches),
        _entry("c", textures={"BaseTexture": "/ElysiumBaked/Textures/art/T_c"},
              switches={}),
    ]
    manifest = _stage(tmp_path, entries)

    report = module.run(manifest)

    assert report.failures == []
    # a and b share (parent, switches, blendMode); c is a distinct permutation.
    assert editor.compile_calls == [ROOT + "/art/MI_a", ROOT + "/art/MI_c"]
    assert len(report.compiled_permutations) == 2


def test_zero_pixel_shader_instructions_fails_the_probed_entry(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    editor.compile_instructions[ROOT + "/art/MI_brick"] = 0
    manifest = _stage(tmp_path, [_entry("brick")])

    report = module.run(manifest)

    assert report.built == 0
    assert "0 pixel-shader instructions" in report.failures[0]["reason"]


def test_the_permutation_key_distinguishes_two_sided_and_clip_value(tmp_path):
    """Review finding 3: a `twoSided`/`opacityMaskClipValue` difference alone used to share a
    probe with an entry that had neither -- 963 measured keys against 1,000 real permutations.
    Two entries that agree on parent/switches/blendMode but differ on `twoSided` must each get
    their own compile probe."""
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_a")
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_b")
    module = _load(editor)
    entries = [
        _entry("a", textures={"BaseTexture": "/ElysiumBaked/Textures/art/T_a"},
              basePropertyOverrides={"blendMode": "Opaque", "twoSided": False}),
        _entry("b", textures={"BaseTexture": "/ElysiumBaked/Textures/art/T_b"},
              basePropertyOverrides={"blendMode": "Opaque", "twoSided": True}),
    ]
    manifest = _stage(tmp_path, entries)

    report = module.run(manifest)

    assert report.failures == []
    assert editor.compile_calls == [ROOT + "/art/MI_a", ROOT + "/art/MI_b"]
    assert len(report.compiled_permutations) == 2


def test_a_missing_hit_proxy_or_depth_shader_is_recorded_not_fatal(tmp_path):
    """Production evidence (a real full-corpus run, 2026-08-31): a headless commandlet import
    never compiles a hit-proxy or depth-only shader through this path -- ~90 real entries,
    including `cable/MI_cable` itself, failed hard on an earlier version of this check that
    required them. They are recorded in `missingShaderTypes` and never fail the entry."""
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    editor.shader_type_names[ROOT + "/art/MI_brick"] = ("TBasePassPS",)  # no hit-proxy/depth
    module = _load(editor)
    manifest = _stage(tmp_path, [_entry("brick")])

    report = module.run(manifest)

    assert report.failures == []
    row = report.compiled_permutations[0]
    assert sorted(row["missingShaderTypes"]) == ["depthonly", "hitproxy"]


def test_compile_probe_fails_without_a_base_pass_shader(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    editor.shader_type_names[ROOT + "/art/MI_brick"] = ("FHitProxyPS", "FDepthOnlyPS")  # no base pass
    module = _load(editor)
    manifest = _stage(tmp_path, [_entry("brick")])

    report = module.run(manifest)

    assert report.built == 0
    assert "basepass" in report.failures[0]["reason"]


def test_compiled_permutations_record_probed_shader_types(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    manifest = _stage(tmp_path, [_entry("brick")])

    report = module.run(manifest)

    assert report.failures == []
    row = report.compiled_permutations[0]
    assert row["probedShaderTypes"]
    assert row["numShaderTypes"] == len(row["probedShaderTypes"])
    assert row["missingShaderTypes"] == []  # the default fixture carries hit-proxy/depth/base-pass


# --- pruning ---------------------------------------------------------------------------------------


def test_pruning_removes_what_the_manifest_neither_names_nor_protects(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    editor.assets[ROOT + "/art/MI_retired"] = FakeAsset(
        ROOT + "/art/MI_retired", "MaterialInstanceConstant")
    editor.assets[ROOT + "/art/MI_retired"].metadata["ElysiumProducer"] = 'materials'
    editor.assets[ROOT + "/art/MI_unreadable"] = FakeAsset(
        ROOT + "/art/MI_unreadable", "MaterialInstanceConstant")
    editor.assets[ROOT + "/art/MI_unreadable"].metadata["ElysiumProducer"] = 'materials'
    module = _load(editor)
    manifest = _stage(tmp_path, [_entry("brick")], keep=[ROOT + "/art/MI_unreadable"])

    report = module.run(manifest)

    assert report.pruned == 1
    assert editor.deleted == [ROOT + "/art/MI_retired"]
    assert ROOT + "/art/MI_unreadable" in editor.assets
    # The generated masters are never in the prune scope.
    assert M_LIT in editor.assets and M_UNLIT in editor.assets


def test_prune_never_touches_the_master_root(tmp_path):
    editor = _base_editor()
    _add_texture(editor, "/ElysiumBaked/Textures/art/T_brick")
    module = _load(editor)
    manifest = _stage(tmp_path, [_entry("brick")])

    module.run(manifest)

    assert M_LIT in editor.assets and M_UNLIT in editor.assets


# --- the manifest contract ---------------------------------------------------------------------------


def test_a_manifest_the_script_cannot_execute_is_refused_whole(tmp_path):
    module = _load(FakeEditor())

    def refused(**over):
        body = {"schemaVersion": "1.0.0", "packageRoot": ROOT, "masterRoot": MASTER_ROOT,
                "pruneScope": ROOT + "/", "keep": [], "assets": [_entry("brick")]}
        body.update(over)
        path = tmp_path / "bad.json"
        path.write_text(json.dumps(body), encoding="utf-8")
        return str(path)

    for over in (
        {"schemaVersion": "9.9.9"},
        {"packageRoot": "ElysiumBaked/Materials"},
        {"masterRoot": "Game/ElysiumGenerated/Materials/V2"},
        {"assets": {}},
        {"pruneScope": "/Game/Elsewhere/"},
        {"keep": ["/Game/Elsewhere/MI_x"]},
        {"assets": [_entry("brick", assetPath="/Game/Elsewhere/MI_brick")]},
        {"assets": [_entry("brick", parent="/Game/SomewhereElse/M_Rogue")]},
        {"assets": [_entry("brick"), _entry("brick")]},
    ):
        with pytest.raises(module.ManifestError):
            module.load_manifest(refused(**over))

    incomplete = dict(_entry("brick"))
    incomplete.pop("recipe")
    with pytest.raises(module.ManifestError):
        module.load_manifest(refused(assets=[incomplete]))


def test_a_patched_parent_below_the_package_root_is_accepted(tmp_path):
    body = {"schemaVersion": "1.0.0", "packageRoot": ROOT, "masterRoot": MASTER_ROOT,
            "pruneScope": ROOT + "/", "keep": [],
            "assets": [_entry("brick", parent=ROOT + "/art/MI_base", patched=True)]}
    path = tmp_path / "ok.json"
    path.write_text(json.dumps(body), encoding="utf-8")
    module = _load(FakeEditor())

    manifest = module.load_manifest(str(path))

    assert manifest["assets"][0]["parent"] == ROOT + "/art/MI_base"


def test_the_command_line_reader_takes_a_quoted_path_and_a_zero_flag(tmp_path):
    editor = FakeEditor()
    module = _load(editor)
    editor.command_line = (
        'UnrealEditor-Cmd.exe -run=pythonscript '
        '"-ImportMaterials=C:/work root/import/manifest.json" -ImportForce=0')

    assert module.cmdline_arg("ImportMaterials") == "C:/work root/import/manifest.json"
    assert module.flag(module.cmdline_arg("ImportForce")) is False
    assert module.flag("1") and module.flag("true") and module.flag("YES")
    assert not module.flag("") and not module.flag("no")
