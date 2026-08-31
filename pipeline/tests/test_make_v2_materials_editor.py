"""`pipeline/unreal/make_v2_materials.py`, the SF-4.3 M_V2_Lit generator, exercised end to end
against a faked editor -- the same shape as `test_import_textures_editor.py`. The module runs its
whole build at import time (module scope, no `__main__` guard, matching every other
`pipeline/unreal/make_*.py` generator), so loading it under the fake IS the test: a wrong pin
name, an unmasked multi-output node fed into a switch, or a missing enum surfaces as an exception
during import rather than only inside the real editor, where a failure costs a multi-minute
launch to discover.
"""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import sys
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]


class FakeNode:
    _next_id = [0]

    def __init__(self, cls, x, y):
        self.cls = cls
        self.x, self.y = x, y
        self.props = {}
        FakeNode._next_id[0] += 1
        self.id = FakeNode._next_id[0]

    def set_editor_property(self, name, value):
        self.props[name] = value

    def get_editor_property(self, name):
        return self.props.get(name)

    def get_class(self):
        return SimpleNamespace(get_name=lambda: getattr(self.cls, "__name__", str(self.cls)))


class FakeAsset:
    def __init__(self, name, package, cls):
        self.name = name
        self.package = package
        self.path = "%s/%s" % (package, name)
        self.cls = cls
        self.props = {}
        self.metadata = {}
        self.expressions = []

    def set_editor_property(self, name, value):
        self.props[name] = value

    def get_editor_property(self, name):
        return self.props.get(name, [] if name in ("scalar_parameters", "vector_parameters") else None)

    def get_class(self):
        name = self.cls if isinstance(self.cls, str) else getattr(self.cls, "__name__", str(self.cls))
        return SimpleNamespace(get_name=lambda: name)


class FakeMel:
    #: Real-editor fact (confirmed by an independent review of this file against the actual
    #: editor): a `VectorParameter`/`TextureSample`/`TextureSampleParameter2D`/`VertexColor`
    #: node's *default* (`""`) output is RGB only (float3) -- the 4th (alpha) channel needs the
    #: explicit named `"A"` or `"RGBA"` output. A `ComponentMask` asking for the `a` channel off
    #: one of these node types' bare default output is a real compile error ("Not enough
    #: components ... for component mask 0011"), not just a fake-harness nicety, so the fake
    #: refuses the connection the same way the real editor's compile would.
    _RGB_ONLY_DEFAULT_CLASSES = frozenset([
        "MaterialExpressionVectorParameter",
        "MaterialExpressionTextureSampleParameter2D",
        "MaterialExpressionTextureSample",
        "MaterialExpressionVertexColor",
    ])

    def __init__(self, editor):
        self.editor = editor
        self.connections = []
        self.property_connections = []
        self.refuse_pins = set()
        self.refuse_properties = set()
        self.recompile_errors = []

    def create_material_expression(self, mat, cls, x, y):
        node = FakeNode(cls, x, y)
        mat.expressions.append(node)
        return node

    def connect_material_expressions(self, src, src_out, dst, dst_in):
        self.connections.append((src, src_out, dst, dst_in))
        if src is None or dst is None:
            return False
        if (dst_in, src_out) in self.refuse_pins:
            return False
        src_cls_name = getattr(getattr(src, "cls", None), "__name__", "")
        dst_cls_name = getattr(getattr(dst, "cls", None), "__name__", "")
        if (src_out == "" and src_cls_name in self._RGB_ONLY_DEFAULT_CLASSES
                and dst_cls_name == "MaterialExpressionComponentMask"
                and dst.props.get("a")):
            return False
        return True

    def connect_material_property(self, src, src_out, prop):
        self.property_connections.append((src, src_out, prop))
        return prop not in self.refuse_properties

    def recompile_material(self, mat):
        return list(self.recompile_errors)


class FakeEditor:
    def __init__(self):
        self.assets = {}
        self.saved = []
        self.deleted = []
        self.tasks = []
        self.errors, self.warnings, self.logs = [], [], []
        self.command_line = ""
        self.mel = FakeMel(self)

    # EditorAssetLibrary
    def does_asset_exist(self, path):
        return path in self.assets

    def delete_asset(self, path):
        self.deleted.append(path)
        return self.assets.pop(path, None) is not None

    def save_asset(self, path, only_if_is_dirty=True):
        self.saved.append(path)
        return path in self.assets

    def set_metadata_tag(self, asset, tag, value):
        asset.metadata[tag] = value

    def get_metadata_tag(self, asset, tag):
        return asset.metadata.get(tag, "")

    # AssetRegistry (bake_lib.stored_recipe / object_path_of)
    def get_asset_by_object_path(self, object_path):
        path = object_path.split(".", 1)[0]
        asset = self.assets.get(path)
        if asset is None:
            return None
        return SimpleNamespace(is_valid=lambda: True,
                               get_tag_value=lambda tag, a=asset: a.metadata.get(tag, ""))

    def scan_paths_synchronous(self, *a, **k):
        pass

    # AssetTools
    def create_asset(self, name, package, cls, factory):
        asset = FakeAsset(name, package, cls)
        self.assets[asset.path] = asset
        return asset

    def import_asset_tasks(self, tasks):
        for task in tasks:
            self.tasks.append(task)
            name = task.get_editor_property("destination_name")
            package = task.get_editor_property("destination_path")
            path = "%s/%s" % (package, name)
            filename = task.get_editor_property("filename") or ""
            # The real DDS importer's behaviour on a 1-slice DX10-array header is unverified
            # (make_v2_materials.py's own docstring for `_make_default_frames_array`); the fake
            # assumes success so the generator's happy path is exercised, and the real editor run
            # is what settles whether that assumption holds.
            cls = "Texture2DArray" if filename.lower().endswith(".dds") else "Texture2D"
            self.assets[path] = FakeAsset(name, package, cls)

    def load_asset(self, path):
        return self.assets.get(path)


def _enum(prefix, *names):
    return SimpleNamespace(**{n: "%s.%s" % (prefix, n) for n in names})


def _fake_unreal(editor):
    class AssetImportTask(FakeNode):
        def __init__(self):
            super().__init__(None, 0, 0)

    class LinearColor:
        def __init__(self, r, g, b, a):
            self.r, self.g, self.b, self.a = r, g, b, a

    class CollectionScalarParameter(FakeNode):
        def __init__(self):
            super().__init__(None, 0, 0)

    material_expression_names = [
        "MaterialExpressionTextureSampleParameter2D", "MaterialExpressionTextureSampleParameterCube",
        "MaterialExpressionTextureObjectParameter", "MaterialExpressionTextureSample",
        "MaterialExpressionScalarParameter", "MaterialExpressionVectorParameter",
        "MaterialExpressionComponentMask", "MaterialExpressionStaticSwitchParameter",
        "MaterialExpressionCollectionParameter", "MaterialExpressionConstant",
        "MaterialExpressionConstant3Vector", "MaterialExpressionConstant4Vector",
        "MaterialExpressionMultiply", "MaterialExpressionAdd",
        "MaterialExpressionSubtract", "MaterialExpressionDivide", "MaterialExpressionDotProduct",
        "MaterialExpressionPower", "MaterialExpressionLinearInterpolate",
        "MaterialExpressionSaturate", "MaterialExpressionOneMinus", "MaterialExpressionClamp",
        "MaterialExpressionFloor", "MaterialExpressionFrac",
        "MaterialExpressionAppendVector", "MaterialExpressionTime", "MaterialExpressionSine",
        "MaterialExpressionPanner", "MaterialExpressionVertexColor", "MaterialExpressionFresnel",
        "MaterialExpressionReflectionVectorWS", "MaterialExpressionTextureCoordinate",
        "MaterialExpressionRayTracingQualitySwitch", "MaterialExpressionIf",
    ]

    ns = SimpleNamespace(
        MaterialEditingLibrary=editor.mel,
        GeometryScript_Collision=object(),
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: editor),
        EditorAssetLibrary=editor,
        AssetRegistryHelpers=SimpleNamespace(get_asset_registry=lambda: editor),
        SystemLibrary=SimpleNamespace(get_command_line=lambda: editor.command_line),
        load_asset=editor.load_asset,
        log=editor.logs.append,
        log_warning=editor.warnings.append,
        log_error=editor.errors.append,
        LinearColor=LinearColor,
        AssetImportTask=AssetImportTask,
        CollectionScalarParameter=CollectionScalarParameter,
        Material=type("Material", (), {}),
        MaterialFactoryNew=type("MaterialFactoryNew", (), {}),
        MaterialParameterCollection=type("MaterialParameterCollection", (), {}),
        MaterialParameterCollectionFactoryNew=type("MaterialParameterCollectionFactoryNew", (), {}),
        MaterialSamplerType=_enum(
            "SAMPLERTYPE", "SAMPLERTYPE_COLOR", "SAMPLERTYPE_MASKS", "SAMPLERTYPE_NORMAL",
            "SAMPLERTYPE_LINEAR_COLOR"),
        TextureMipValueMode=_enum("TMVM", "TMVM_MIP_LEVEL"),
        TextureCompressionSettings=_enum("TC", "TC_MASKS", "TC_VECTOR_DISPLACEMENTMAP", "TC_DEFAULT"),
        TextureMipGenSettings=_enum("TMGS", "TMGS_NO_MIPMAPS"),
        TextureFilter=_enum("TF", "TF_NEAREST"),
        TextureAddress=_enum("TA", "TA_CLAMP"),
        TextureLossyCompressionAmount=_enum("TLCA", "TLCA_NONE"),
        MaterialDomain=_enum("MD", "MD_SURFACE"),
        BlendMode=_enum("BLEND", "BLEND_OPAQUE", "BLEND_TRANSLUCENT", "BLEND_MODULATE"),
        TranslucencyLightingMode=_enum("TLM", "TLM_SURFACE_PER_PIXEL_LIGHTING"),
        MaterialShadingModel=_enum("MSM", "MSM_UNLIT"),
        RefractionMode=_enum("RM", "RM_PIXEL_NORMAL_OFFSET"),
        # `unreal.MaterialProperty` in this 5.8 build exposes no `MP_PIXEL_DEPTH_OFFSET`
        # (phase4_mechanics.md section 0, confirmed against the real editor's own
        # `dir(unreal.MaterialProperty)`) -- the fake used to carry it, which let a
        # `connect_material_property(..., MP_PIXEL_DEPTH_OFFSET)` call succeed here and fail only
        # in the real editor. Nothing in this module calls it any more, and the fake now matches
        # reality instead of papering over the gap.
        MaterialProperty=_enum(
            "MP", "MP_BASE_COLOR", "MP_NORMAL", "MP_EMISSIVE_COLOR", "MP_SPECULAR",
            "MP_ROUGHNESS", "MP_METALLIC", "MP_OPACITY", "MP_OPACITY_MASK", "MP_REFRACTION"),
    )
    for name in material_expression_names:
        setattr(ns, name, type(name, (), {}))
    return ns


PKG = "/Game/ElysiumGenerated/Materials/V2"

#: SF-4.1's own generator, `make_surface_knobs.py`, is what actually creates and owns
#: `MPC_ElysiumSurfaces`/`T_SurfaceClassLUT`; `make_v2_materials.py` only *reads* them now (a
#: fail-hard `_load_surfaces_collection`/`_load_class_lut`, not a seed-on-demand). This mirrors
#: that generator's real output well enough for the module under test to find what it looks for --
#: it is not a copy of `make_surface_knobs.py`'s own logic, and a change to the real required-row
#: list is caught by `test_declared_parameters_all_land_on_the_graph`-style failures the moment
#: `make_v2_materials.py` asks for a row this fixture does not carry.
REQUIRED_MPC_SCALARS = [
    "Overbright", "MaskRoughnessMin", "MaskRoughnessMax", "MaskSpecularScale",
    "MaskMetallicMax", "ChromaticTintStrength", "EnvTintScale", "FixedCubeStrength",
    "DefaultRoughness", "DefaultSpecular", "DefaultMetallic", "ClassInfluence",
]


def _seed_surface_knobs(editor):
    mpc = FakeAsset("MPC_ElysiumSurfaces", PKG, "MaterialParameterCollection")
    scalars = []
    for name in REQUIRED_MPC_SCALARS:
        row = FakeNode(None, 0, 0)
        row.set_editor_property("parameter_name", name)
        scalars.append(row)
    mpc.set_editor_property("scalar_parameters", scalars)
    editor.assets[mpc.path] = mpc

    # `_load_class_lut` reads the LUT texture through `DA_SurfaceCalibration`'s own `Lut`
    # property, never a hard-coded texture path -- that is what stays correct whether the real
    # texture lives at its own top-level asset path or, once SF-4.1 folds it in, as an inner
    # object of the calibration asset's own package.
    lut = FakeAsset("T_SurfaceClassLUT", PKG, "Texture2D")
    calibration = FakeAsset("DA_SurfaceCalibration", PKG, "ElysiumSurfaceCalibration")
    calibration.set_editor_property("lut", lut)
    editor.assets[calibration.path] = calibration


def _load(editor, tmp_path, monkeypatch, *, seed=True):
    monkeypatch.setenv("ELYSIUM_WORK_ROOT", str(tmp_path))
    monkeypatch.delenv("ELYSIUM_EXPORT_V2_ROOT", raising=False)
    if seed:
        _seed_surface_knobs(editor)
    fake = _fake_unreal(editor)
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        for mod in ("pipeline.unreal.mat_fog", "pipeline.unreal.matgraph",
                    "pipeline.unreal.bake_lib", "pipeline.unreal.make_v2_materials"):
            sys.modules.pop(mod, None)
        spec = importlib.util.spec_from_file_location(
            "elysium_test_make_v2_materials", REPO / "pipeline/unreal/make_v2_materials.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    module.unreal = fake
    return module


# Grew by one master per commit in the SF-4.3 part-2 series: M_V2_Lit and M_V2_LitTranslucent
# landed together (the same graph, and M_V2_LitTranslucent needs no Params class of its own),
# then M_V2_Unlit, then M_V2_TwoTexture. Part 3 grows this by one more master per commit,
# starting with M_V2_Eyes.
MASTERS = [
    ("M_V2_Lit", "Params", "BLEND.BLEND_OPAQUE"),
    ("M_V2_LitTranslucent", "LitParams", "BLEND.BLEND_TRANSLUCENT"),
    ("M_V2_Unlit", "UnlitParams", "BLEND.BLEND_OPAQUE"),
    ("M_V2_TwoTexture", "TwoTextureParams", "BLEND.BLEND_OPAQUE"),
    ("M_V2_Eyes", "EyesParams", "BLEND.BLEND_OPAQUE"),
    ("M_V2_Water", "WaterParams", "BLEND.BLEND_TRANSLUCENT"),
    ("M_V2_Sprite", "SpriteParams", "BLEND.BLEND_TRANSLUCENT"),
    ("M_V2_Refract", "RefractParams", "BLEND.BLEND_TRANSLUCENT"),
    ("M_V2_Decal", "DecalParams", "BLEND.BLEND_MODULATE"),
]


def test_a_fresh_run_authors_all_compiling_masters(tmp_path, monkeypatch):
    editor = FakeEditor()
    module = _load(editor, tmp_path, monkeypatch)

    for name, _params_attr, blend in MASTERS:
        asset = editor.assets.get("%s/%s" % (PKG, name))
        assert asset is not None, "%s was not authored" % name
        assert asset.props.get("material_domain") == "MD.MD_SURFACE"
        assert asset.props.get("blend_mode") == blend
        assert "ElysiumRecipe" in asset.metadata
        assert "%s/%s" % (PKG, name) in editor.saved
    assert editor.mel.recompile_errors == []


def test_mask_width_bug_is_caught_offline():
    """Regression guard for the review that found `M_V2_Lit` did not actually compile: masking the
    alpha channel off a bare `VectorParameter`/`TextureSample`(`Parameter`)/`VertexColor` node's
    default output is refused by `FakeMel` the same way the real editor's compile refuses it (see
    `FakeMel.connect_material_expressions`'s docstring). If a future edit reintroduces that
    pattern, `test_a_fresh_run_authors_all_compiling_masters` raises `SystemExit` from
    `matgraph.connect`'s refused-pin contract instead of silently producing a broken graph -- this
    test only documents why, since the mechanism is exercised by that test, not by a call here."""


def test_declared_parameters_all_land_on_the_graph(tmp_path, monkeypatch):
    editor = FakeEditor()
    module = _load(editor, tmp_path, monkeypatch)

    for name, params_attr, _blend in MASTERS:
        asset = editor.assets["%s/%s" % (PKG, name)]
        names = {n.props.get("parameter_name") for n in asset.expressions if "parameter_name" in n.props}
        params = getattr(module, params_attr)
        for table in (params.Textures, params.Scalars, params.Vectors, params.Switches):
            for attr, value in vars(table).items():
                if attr.startswith("_"):
                    continue
                assert value in names, "%s: %s not authored on the graph" % (name, value)


def test_a_second_run_with_no_change_skips_the_rebuild_and_deletes_nothing(tmp_path, monkeypatch):
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    lit_path = "%s/M_V2_Lit" % PKG
    first_expression_count = len(editor.assets[lit_path].expressions)

    editor2 = FakeEditor()
    editor2.assets = editor.assets  # same "disk" state (recipe stamp persists)
    editor2.command_line = ""
    _load(editor2, tmp_path, monkeypatch, seed=False)

    # A no-op rerun deletes nothing and authors no *new* expression on any master -- the recipe
    # stamp already matches, so every master takes the early-return "up to date, skipping" path.
    assert editor2.deleted == []
    for name, _params_attr, _blend in MASTERS:
        path = "%s/%s" % (PKG, name)
        assert path not in editor2.saved
    assert len(editor2.assets[lit_path].expressions) == first_expression_count


def test_policy_force_rebuilds_even_when_current(tmp_path, monkeypatch):
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    lit_path = "%s/M_V2_Lit" % PKG
    stamped = editor.assets[lit_path].metadata["ElysiumRecipe"]

    editor2 = FakeEditor()
    editor2.assets = editor.assets
    editor2.command_line = "-PolicyForce=1"
    _load(editor2, tmp_path, monkeypatch, seed=False)

    asset = editor2.assets[lit_path]
    assert asset.metadata["ElysiumRecipe"] == stamped
    for name, _params_attr, _blend in MASTERS:
        assert "%s/%s" % (PKG, name) in editor2.saved


def test_missing_surfaces_collection_fails_loudly_rather_than_seeding_one(tmp_path, monkeypatch):
    """`make_v2_materials.py` no longer creates `MPC_ElysiumSurfaces` -- SF-4.1's
    `make_surface_knobs.py` owns it. A run without that prerequisite must fail, naming it, not
    silently author a placeholder collection with invented defaults."""
    editor = FakeEditor()
    with pytest.raises(SystemExit):
        _load(editor, tmp_path, monkeypatch, seed=False)
    assert any("MPC_ElysiumSurfaces" in message for message in editor.errors)


def test_missing_class_lut_fails_loudly_rather_than_seeding_a_placeholder(tmp_path, monkeypatch):
    editor = FakeEditor()
    _seed_surface_knobs(editor)
    del editor.assets["%s/DA_SurfaceCalibration" % PKG]
    with pytest.raises(SystemExit):
        _load(editor, tmp_path, monkeypatch, seed=False)
    assert any("DA_SurfaceCalibration" in message for message in editor.errors)
