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


#: `MaterialExpressionFresnel`'s only two *static* (`set_editor_property`-reachable) properties --
#: `ExponentIn`/`BaseReflectFractionIn` are connectable `FExpressionInput` pins instead
#: (`connect(..., "ExponentIn")`/`"BaseReflectFractionIn"`, real-editor fact, `matgraph.Graph.
#: fresnel`'s own docstring). Regression guard for the `d698166d` fixes (review finding 12): a
#: caller that reached for `set_editor_property("exponent_in", ...)` -- the wrong, pin-shaped name
#: -- used to succeed silently against this fake and fail only in the real editor.
_FRESNEL_STATIC_PROPERTIES = frozenset({"exponent", "base_reflect_fraction"})


class FakeNode:
    _next_id = [0]

    def __init__(self, cls, x, y):
        self.cls = cls
        self.x, self.y = x, y
        self.props = {}
        FakeNode._next_id[0] += 1
        self.id = FakeNode._next_id[0]

    def set_editor_property(self, name, value):
        if (getattr(self.cls, "__name__", "") == "MaterialExpressionFresnel"
                and name not in _FRESNEL_STATIC_PROPERTIES):
            raise AttributeError(
                "MaterialExpressionFresnel has no static property %r -- ExponentIn/"
                "BaseReflectFractionIn are connectable pins, not set_editor_property names" % name)
        self.props[name] = value

    def get_editor_property(self, name):
        return self.props.get(name)

    def get_class(self):
        return SimpleNamespace(get_name=lambda: getattr(self.cls, "__name__", str(self.cls)))


class FakeStruct:
    """A minimal editor-property struct standing in for a value type such as
    `MaterialInstanceBasePropertyOverrides` -- `get_editor_property`/`set_editor_property`
    round-trip through a plain dict, nothing more."""

    def __init__(self):
        self.props = {}

    def set_editor_property(self, name, value):
        self.props[name] = value

    def get_editor_property(self, name):
        return self.props.get(name)


def _material_parameter_info():
    """`unreal.MaterialParameterInfo()`: the struct's own C++ defaults, which is all
    `bake_lib._set_param` relies on beyond the name it writes."""
    info = FakeStruct()
    info.set_editor_property("name", "")
    info.set_editor_property("index", -1)
    return info


class FakeAsset:
    def __init__(self, name, package, cls):
        self.name = name
        self.package = package
        self.path = "%s/%s" % (package, name)
        self.cls = cls
        self.props = {}
        self.metadata = {}
        self.expressions = []

    def set_editor_property(self, name, value, notify_mode=None):
        self.props[name] = value

    def get_editor_property(self, name):
        if name == "base_property_overrides":
            # Struct property: the real editor hands back a live, mutable value the caller
            # edits in place before writing it back with `set_editor_property` -- lazily
            # materialize one rather than returning `None`, matching that shape.
            return self.props.setdefault(name, FakeStruct())
        if name.endswith("_parameter_values"):
            # `UMaterialInstance`'s own scalar/vector/texture override arrays: an instance that
            # has never been written carries an empty one, not `None` -- `bake_lib.set_*_param`
            # reads this array, merges its row and writes the whole array back.
            return list(self.props.get(name, []))
        return self.props.get(name, [] if name in ("scalar_parameters", "vector_parameters") else None)

    def resolved_parameters(self, array_property):
        """{parameter name: value} over one of the `*_parameter_values` arrays."""
        return {str(row.get_editor_property("parameter_info").get_editor_property("name")):
                row.get_editor_property("parameter_value")
                for row in self.props.get(array_property, [])}

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

    #: H2 review fix: output-width model for the node classes `_sine_lane`'s `Lerp` actually feeds.
    _FIXED_WIDTH_CLASSES = {
        "MaterialExpressionConstant": 1,
        "MaterialExpressionScalarParameter": 1,
        "MaterialExpressionConstant3Vector": 3,
        "MaterialExpressionConstant4Vector": 4,
    }
    #: These four broadcast their (up to two) operands' width the ordinary HLSL way -- the widest
    #: known operand, a scalar operand broadcasting into a wider one -- so a chain of them
    #: propagates a known width through instead of going opaque at the first arithmetic node.
    _BROADCAST_CLASSES = frozenset([
        "MaterialExpressionMultiply", "MaterialExpressionAdd", "MaterialExpressionSubtract",
        "MaterialExpressionDivide",
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

    def _output_width(self, node, out_name, _seen=None):
        """Best-effort output width of `node`'s `out_name` output, `None` if not modeled. Direct
        classes resolve immediately; `_BROADCAST_CLASSES` recurse into their own `A`/`B`/`""` input
        connections (already recorded in `self.connections` by the time this runs, since a node's
        inputs connect before it is handed downstream) so a width propagates through an ordinary
        arithmetic chain instead of going opaque at the first `Multiply`/`Add`."""
        _seen = _seen or frozenset()
        if id(node) in _seen:
            return None  # cycle guard; never expected in a DAG
        cls_name = getattr(getattr(node, "cls", None), "__name__", "")
        if cls_name in self._FIXED_WIDTH_CLASSES:
            return self._FIXED_WIDTH_CLASSES[cls_name]
        if cls_name in self._RGB_ONLY_DEFAULT_CLASSES:
            if out_name == "RGBA":
                return 4
            if out_name in ("A", "a"):
                return 1
            return 3  # bare default output is RGB-only, real-editor fact (see the class above)
        if cls_name == "MaterialExpressionComponentMask":
            return sum(1 for ch in "rgba" if node.props.get(ch))
        if cls_name in self._BROADCAST_CLASSES:
            seen_with_self = _seen | {id(node)}
            widths = [
                self._output_width(src, src_out, seen_with_self)
                for src, src_out, dst, dst_in in self.connections
                if dst is node and dst_in in ("A", "B", "")
            ]
            widths = [w for w in widths if w is not None]
            return max(widths) if widths else None
        return None

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
        # H2 review fix: `MaterialExpressionLinearInterpolate`'s `Alpha` must agree in width with
        # its own `A` result side -- a modeled, non-scalar (>1-wide) `Alpha` that disagrees with a
        # modeled, non-scalar `A` is exactly the `SineChannelMask`-truncated-to-`.r` defect the
        # review found (`Constant4Vector` result / float3 alpha coerces the alpha to float1 instead
        # of masking per-channel; `HLSLMaterialTranslator`'s lerp-alpha coercion, per the review). A
        # true scalar (1-wide) `Alpha`/`A` is always legal (the ordinary "lerp two colours by one
        # scalar" shape), so only a >1-wide vs >1-wide mismatch is refused.
        if dst_cls_name == "MaterialExpressionLinearInterpolate" and dst_in in ("A", "Alpha"):
            width = self._output_width(src, src_out)
            widths = dst.props.setdefault("_lerp_widths", {})
            if width is not None:
                widths[dst_in] = width
            a_width, alpha_width = widths.get("A"), widths.get("Alpha")
            if (a_width is not None and alpha_width is not None
                    and a_width > 1 and alpha_width > 1 and a_width != alpha_width):
                return False
        return True

    def connect_material_property(self, src, src_out, prop):
        self.property_connections.append((src, src_out, prop))
        return prop not in self.refuse_properties

    def recompile_material(self, mat):
        return list(self.recompile_errors)

    # -- MaterialInstanceConstant surface (`_probe_all_switches_true`) --------------------------
    def set_material_instance_parent(self, mic, parent):
        mic.props["parent"] = parent

    def clear_all_material_instance_parameters(self, mic):
        mic.props["switches"] = {}

    def set_material_instance_static_switch_parameter_value(
        self, mic, name, value, update_material_instance=True):
        mic.props.setdefault("switches", {})[name] = value

    def set_material_instance_texture_parameter_value(self, mic, name, texture):
        mic.props.setdefault("textures", {})[name] = texture

    def update_material_instance(self, mic):
        mic.props["updated"] = True

    def get_statistics(self, mic):
        # A real compile counts real pixel-shader instructions; the fake reports a positive,
        # deterministic count so `_probe_all_switches_true`'s own `instructions <= 0` check has
        # something real to pass, and a test can still monkeypatch `mel.get_statistics` to force
        # the zero-instruction failure path.
        return SimpleNamespace(num_pixel_shader_instructions=42)


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

    def does_directory_exist(self, path):
        return True

    def make_directory(self, path):
        pass

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
            cls = "Texture2D"
            if filename.lower().endswith(".dds"):
                # `_make_default_frames_array` writes a DX10-array DDS -- confirmed live against
                # the real editor (SF-4.3-part-3 cross-cutting ruling (c)) that this imports as a
                # real Texture2DArray *only* when the DX10 header's own `arraySize` exceeds one; a
                # 1-slice array header collapses to a plain Texture2D (review fix, finding 12: the
                # fake used to always report Texture2DArray for any `.dds`, which could not have
                # caught that collapse if a future edit reintroduced it). `arraySize` is the DX10
                # header's 4th little-endian uint32, starting right after the 4-byte "DDS " magic
                # and the fixed 124-byte legacy header (`_dds_header`/`_dds_dx10_header`'s own
                # layout in `make_v2_materials.py`).
                try:
                    data = Path(filename).read_bytes()
                    array_size = int.from_bytes(data[4 + 124 + 12:4 + 124 + 16], "little")
                except (OSError, IndexError):
                    array_size = 0
                if array_size > 1:
                    cls = "Texture2DArray"
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
        "MaterialExpressionConstant2Vector",
        "MaterialExpressionConstant3Vector", "MaterialExpressionConstant4Vector",
        "MaterialExpressionMultiply", "MaterialExpressionAdd",
        "MaterialExpressionSubtract", "MaterialExpressionDivide", "MaterialExpressionDotProduct",
        "MaterialExpressionPower", "MaterialExpressionLinearInterpolate",
        "MaterialExpressionSaturate", "MaterialExpressionOneMinus", "MaterialExpressionClamp",
        "MaterialExpressionFloor", "MaterialExpressionFrac",
        "MaterialExpressionAppendVector", "MaterialExpressionTime", "MaterialExpressionSine",
        "MaterialExpressionPanner", "MaterialExpressionVertexColor", "MaterialExpressionFresnel",
        "MaterialExpressionParticleColor", "MaterialExpressionDynamicParameter",
        "MaterialExpressionReflectionVectorWS", "MaterialExpressionTextureCoordinate",
        "MaterialExpressionRayTracingQualitySwitch", "MaterialExpressionIf",
        "MaterialExpressionPixelDepth",
        # R6.3: the detail-sway World Position Offset lane (`_detail_sway`).
        "MaterialExpressionPerInstanceCustomData", "MaterialExpressionWorldPosition",
        "MaterialExpressionTransformPosition", "MaterialExpressionTransform", "MaterialExpressionObjectLocalBounds",
        # R7.1: the Single Layer Water output node (`_build_water`) and the underwater
        # post-process's scene reads (`_build_underwater`).
        "MaterialExpressionSingleLayerWaterMaterialOutput", "MaterialExpressionSceneTexture",
        # R7.5 look pass: the water master's cheap-overlay distance blend (`_build_water`).
        "MaterialExpressionCameraPositionWS", "MaterialExpressionDistance",
        # R7.5 G3: the water master renormalises its tangent normal after folding the DUDV
        # offset field into it (`matgraph.Graph.normalize`).
        "MaterialExpressionNormalize",
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
        MaterialInstanceConstant=type("MaterialInstanceConstant", (), {}),
        MaterialInstanceConstantFactoryNew=type("MaterialInstanceConstantFactoryNew", (), {}),
        # The parameter-value structs `bake_lib` appends to an instance's arrays, and the notify
        # mode it writes those arrays under. A fresh `FMaterialParameterInfo` is a *global*
        # parameter -- `Index` = `INDEX_NONE` -- which is what `bake_lib` leaves it at.
        MaterialParameterInfo=_material_parameter_info,
        ScalarParameterValue=FakeStruct,
        VectorParameterValue=FakeStruct,
        TextureParameterValue=FakeStruct,
        PropertyAccessChangeNotifyMode=_enum(
            "PropertyAccessChangeNotifyMode", "DEFAULT", "NEVER", "ALWAYS"),
        MaterialSamplerType=_enum(
            "SAMPLERTYPE", "SAMPLERTYPE_COLOR", "SAMPLERTYPE_MASKS", "SAMPLERTYPE_NORMAL",
            "SAMPLERTYPE_LINEAR_COLOR"),
        TextureMipValueMode=_enum("TMVM", "TMVM_MIP_LEVEL"),
        TextureCompressionSettings=_enum(
            "TC", "TC_MASKS", "TC_VECTOR_DISPLACEMENTMAP", "TC_DEFAULT", "TC_NORMALMAP"),
        TextureMipGenSettings=_enum("TMGS", "TMGS_NO_MIPMAPS"),
        TextureFilter=_enum("TF", "TF_NEAREST"),
        TextureAddress=_enum("TA", "TA_CLAMP"),
        TextureLossyCompressionAmount=_enum("TLCA", "TLCA_NONE"),
        MaterialDomain=_enum("MD", "MD_SURFACE", "MD_DEFERRED_DECAL", "MD_POST_PROCESS"),
        BlendableLocation=_enum("BL", "BL_SCENE_COLOR_BEFORE_DOF"),
        SceneTextureId=_enum("PPI", "PPI_POST_PROCESS_INPUT0", "PPI_SCENE_DEPTH"),
        BlendMode=_enum("BLEND", "BLEND_OPAQUE", "BLEND_MASKED", "BLEND_TRANSLUCENT", "BLEND_MODULATE",
                        "BLEND_ALPHA_COMPOSITE"),
        TranslucencyLightingMode=_enum("TLM", "TLM_SURFACE_PER_PIXEL_LIGHTING",
                                       "TLM_VOLUMETRIC_PER_VERTEX_NON_DIRECTIONAL"),
        MaterialShadingModel=_enum("MSM", "MSM_UNLIT", "MSM_DEFAULT_LIT", "MSM_SINGLE_LAYER_WATER"),
        RefractionMode=_enum("RM", "RM_PIXEL_NORMAL_OFFSET", "RM_2D_OFFSET"),
        MaterialPositionTransformSource=_enum(
            "TRANSFORMPOSSOURCE", "TRANSFORMPOSSOURCE_WORLD", "TRANSFORMPOSSOURCE_INSTANCE", "TRANSFORMPOSSOURCE_LOCAL"),
        MaterialVectorCoordTransformSource=_enum("TRANSFORMSOURCE", "TRANSFORMSOURCE_LOCAL"),
        MaterialVectorCoordTransform=_enum("TRANSFORM", "TRANSFORM_WORLD"),
        SamplerSourceMode=_enum("SSM", "SSM_CLAMP_WORLD_GROUP_SETTINGS"),
        # `unreal.MaterialProperty` in this 5.8 build exposes no `MP_PIXEL_DEPTH_OFFSET`
        # (phase4_mechanics.md section 0, confirmed against the real editor's own
        # `dir(unreal.MaterialProperty)`) -- the fake used to carry it, which let a
        # `connect_material_property(..., MP_PIXEL_DEPTH_OFFSET)` call succeed here and fail only
        # in the real editor. Nothing in this module calls it any more, and the fake now matches
        # reality instead of papering over the gap.
        MaterialProperty=_enum(
            "MP", "MP_BASE_COLOR", "MP_NORMAL", "MP_EMISSIVE_COLOR", "MP_SPECULAR",
            "MP_ROUGHNESS", "MP_METALLIC", "MP_OPACITY", "MP_OPACITY_MASK", "MP_REFRACTION",
            "MP_WORLD_POSITION_OFFSET"),
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
    "DetailSwayAmplitude", "WaterFogScale", "WaterWarpScale", "WaterReflectWarpScale",
]


#: R5.3's own collection (`make_world_materials.py::make_environment_collection`,
#: `AElysiumMapActor::ApplyWeatherTuning` its sole writer) -- a *different* asset from
#: `MPC_ElysiumSurfaces` above, at the legacy materials package root, not under `V2`.
ENVIRONMENT_PKG = "/Game/ElysiumGenerated/Materials"


def _seed_surface_knobs(editor):
    mpc = FakeAsset("MPC_ElysiumSurfaces", PKG, "MaterialParameterCollection")
    scalars = []
    for name in REQUIRED_MPC_SCALARS:
        row = FakeNode(None, 0, 0)
        row.set_editor_property("parameter_name", name)
        scalars.append(row)
    mpc.set_editor_property("scalar_parameters", scalars)
    editor.assets[mpc.path] = mpc

    environment = FakeAsset("MPC_ElysiumEnvironment", ENVIRONMENT_PKG, "MaterialParameterCollection")
    environment_scalars = []
    for name in ("GlobalWetness", "WetnessOutputScale"):
        row = FakeNode(None, 0, 0)
        row.set_editor_property("parameter_name", name)
        environment_scalars.append(row)
    environment.set_editor_property("scalar_parameters", environment_scalars)
    editor.assets[environment.path] = environment

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
            package, _, child = mod.rpartition(".")
            if package in sys.modules:
                monkeypatch.delattr(sys.modules[package], child, raising=False)
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
#: `domain` defaults `MD.MD_SURFACE` for every master except `M_V2_Decal` (R7.2: re-cut to
#: `MD_DEFERRED_DECAL` -- the one deferred-decal domain a `UDecalComponent` actually draws).
MASTERS = [
    ("M_V2_Lit", "Params", "BLEND.BLEND_OPAQUE", "MD.MD_SURFACE"),
    ("M_V2_LitTranslucent", "LitParams", "BLEND.BLEND_TRANSLUCENT", "MD.MD_SURFACE"),
    ("M_V2_LitSkinned", "LitSkinnedParams", "BLEND.BLEND_MASKED", "MD.MD_SURFACE"),
    ("M_V2_LitSkinnedTranslucent", "LitSkinnedParams", "BLEND.BLEND_TRANSLUCENT", "MD.MD_SURFACE"),
    ("M_V2_Unlit", "UnlitParams", "BLEND.BLEND_OPAQUE", "MD.MD_SURFACE"),
    ("M_V2_TwoTexture", "TwoTextureParams", "BLEND.BLEND_OPAQUE", "MD.MD_SURFACE"),
    ("M_V2_Eyes", "EyesParams", "BLEND.BLEND_MASKED", "MD.MD_SURFACE"),
    # R7.1: Single Layer Water is opaque by the shading model's own rule.
    ("M_V2_Water", "WaterParams", "BLEND.BLEND_OPAQUE", "MD.MD_SURFACE"),
    ("M_V2_Sprite", "SpriteParams", "BLEND.BLEND_TRANSLUCENT", "MD.MD_SURFACE"),
    ("M_V2_Refract", "RefractParams", "BLEND.BLEND_TRANSLUCENT", "MD.MD_SURFACE"),
    ("M_V2_Decal", "DecalParams", "BLEND.BLEND_TRANSLUCENT", "MD.MD_DEFERRED_DECAL"),
]


def test_a_fresh_run_authors_all_compiling_masters(tmp_path, monkeypatch):
    editor = FakeEditor()
    module = _load(editor, tmp_path, monkeypatch)

    for name, _params_attr, blend, domain in MASTERS:
        asset = editor.assets.get("%s/%s" % (PKG, name))
        assert asset is not None, "%s was not authored" % name
        assert asset.props.get("material_domain") == domain
        assert asset.props.get("blend_mode") == blend
        assert "ElysiumRecipe" in asset.metadata
        assert "%s/%s" % (PKG, name) in editor.saved
    assert editor.mel.recompile_errors == []


def test_water_master_is_single_layer_water_with_the_volume_pins_fed(tmp_path, monkeypatch):
    """R7.1 ruling A (`water-architecture.md` section 4): `M_V2_Water` is `MSM_SingleLayerWater`,
    opaque, one-sided, never Nanite, and its `SingleLayerWaterMaterialOutput` node is fed on all
    four pins -- the VMT fog keys as extinction, `RefractTint` as Color Scale Behind Water. The
    underwater post-process master lands beside it in the post-process domain."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)

    water = editor.assets["%s/M_V2_Water" % PKG]
    assert water.props.get("shading_model") == "MSM.MSM_SINGLE_LAYER_WATER"
    assert water.props.get("blend_mode") == "BLEND.BLEND_OPAQUE"


    assert water.props.get("two_sided") is False
    assert not water.props.get("used_with_nanite")
    # R7.5 look pass: the Refraction pin is `Water_Old`'s screen-space DUDV warp of the refracted
    # scene, an explicit 2D offset (`RM_2D_OFFSET`), which SLW's base pass applies.
    assert water.props.get("refraction_method") == "RM.RM_2D_OFFSET"
    outputs = [n for n in water.expressions
               if n.cls.__name__ == "MaterialExpressionSingleLayerWaterMaterialOutput"]
    assert len(outputs) == 1
    fed = {dst_in for _src, _out, dst, dst_in in editor.mel.connections if dst is outputs[0]}
    assert fed == {"ScatteringCoefficients", "AbsorptionCoefficients", "PhaseG",
                   "ColorScaleBehindWater"}
    switches = {n.props.get("parameter_name") for n in water.expressions
                if n.cls.__name__ == "MaterialExpressionStaticSwitchParameter"}
    assert {"Underside", "CheapWater", "UseFogEnable"} <= switches
    mpc = {n.props.get("parameter_name") for n in water.expressions
           if n.cls.__name__ == "MaterialExpressionCollectionParameter"}
    assert "WaterFogScale" in mpc

    underwater = editor.assets["%s/M_ElysiumUnderwater" % PKG]
    assert underwater.props.get("material_domain") == "MD.MD_POST_PROCESS"
    assert underwater.props.get("blendable_location") == "BL.BL_SCENE_COLOR_BEFORE_DOF"
    names = {n.props.get("parameter_name") for n in underwater.expressions
             if n.props.get("parameter_name")}
    assert {"FogColor", "FogStart", "FogInvRange"} <= names
    # The depth read is a `SceneTexture` fetch, never the bare `SceneDepth` node: in the
    # post-process pass that node resolves its own screen UV and the view under the plane renders
    # unfogged (witnessed on `sm_hub_1`; see `_build_underwater`).
    reads = {n.props.get("scene_texture_id") for n in underwater.expressions
             if n.cls.__name__ == "MaterialExpressionSceneTexture"}
    assert reads == {"PPI.PPI_POST_PROCESS_INPUT0", "PPI.PPI_SCENE_DEPTH"}
    assert not [n for n in underwater.expressions
                if n.cls.__name__ == "MaterialExpressionSceneDepth"]


def test_mask_width_bug_is_caught_offline():
    """Regression guard for the review that found `M_V2_Lit` did not actually compile: masking the
    alpha channel off a bare `VectorParameter`/`TextureSample`(`Parameter`)/`VertexColor` node's
    default output, and lerping a wide (`>1`) result against a differently-wide, non-scalar `Alpha`
    (the H2 `SineChannelMask`-truncated-to-`.r` defect), are both refused directly by `FakeMel` --
    not just exercised indirectly through `test_a_fresh_run_authors_all_compiling_masters` (which
    would raise `SystemExit` from `matgraph.connect`'s refused-pin contract if either pattern were
    reintroduced, since neither is present in the graph any master actually builds today)."""
    editor = FakeEditor()
    mel = editor.mel

    vector_param = FakeNode(type("MaterialExpressionVectorParameter", (), {}), 0, 0)
    full_mask = FakeNode(type("MaterialExpressionComponentMask", (), {}), 0, 0)
    full_mask.props.update(r=True, g=True, b=True, a=True)
    assert mel.connect_material_expressions(vector_param, "", full_mask, "") is False
    rgb_mask = FakeNode(type("MaterialExpressionComponentMask", (), {}), 0, 0)
    rgb_mask.props.update(r=True, g=True, b=True, a=False)
    assert mel.connect_material_expressions(vector_param, "", rgb_mask, "") is True

    result4 = FakeNode(type("MaterialExpressionConstant4Vector", (), {}), 0, 0)
    alpha3 = FakeNode(type("MaterialExpressionConstant3Vector", (), {}), 0, 0)
    lerp_mismatched = FakeNode(type("MaterialExpressionLinearInterpolate", (), {}), 0, 0)
    assert mel.connect_material_expressions(result4, "", lerp_mismatched, "A") is True
    assert mel.connect_material_expressions(alpha3, "", lerp_mismatched, "Alpha") is False

    result3 = FakeNode(type("MaterialExpressionConstant3Vector", (), {}), 0, 0)
    lerp_matched = FakeNode(type("MaterialExpressionLinearInterpolate", (), {}), 0, 0)
    assert mel.connect_material_expressions(result3, "", lerp_matched, "A") is True
    assert mel.connect_material_expressions(alpha3, "", lerp_matched, "Alpha") is True

    scalar_alpha = FakeNode(type("MaterialExpressionScalarParameter", (), {}), 0, 0)
    lerp_scalar_alpha = FakeNode(type("MaterialExpressionLinearInterpolate", (), {}), 0, 0)
    assert mel.connect_material_expressions(result4, "", lerp_scalar_alpha, "A") is True
    assert mel.connect_material_expressions(scalar_alpha, "", lerp_scalar_alpha, "Alpha") is True


def test_sine_lane_lerp_widths_agree_for_every_apply_call_site(tmp_path, monkeypatch):
    """H2 regression: loading every master under `FakeMel`'s lerp-width guard (above) already
    proves no `_sine_lane.apply` call site reintroduces the `SineChannelMask`-truncated-to-`.r`
    shape -- a mismatched `Lerp` would refuse the connection, `matgraph.connect` would raise
    `SystemExit`, and `_load` below would propagate it instead of returning a module. This test
    names that guarantee explicitly rather than leaving it implicit in
    `test_a_fresh_run_authors_all_compiling_masters`."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)  # raises if any sine-lane Lerp width mismatches


def test_param_tables_are_pinned_against_the_stages_exposed_params(tmp_path, monkeypatch):
    """M7 review fix: this generator's own `*_PARAM_TABLE`s (the binding contract's Python-side
    half) were never checked against `EXPOSED_PARAMS` (`elysium_pipeline.importers.materials`, the
    stage's own half) -- only `ElysiumV2MaterialTests.cpp`'s `Elysium.Policy.V2MasterParams` checks
    the *compiled* graph against `ElysiumSurfaceParams.h`, and nothing offline checked the two
    Python sides agree with each other before it. A name or kind drifting between them is exactly
    the silent-stage-failure `importers/materials.py`'s own "No silent drop" module-docstring rule
    exists to prevent -- this pins it so a drift fails a fast, editor-free pytest run instead."""
    editor = FakeEditor()
    module = _load(editor, tmp_path, monkeypatch)
    from elysium_pipeline.importers import materials as importer

    kind_by_category = {"textures": "T", "scalars": "S", "vectors": "V", "switches": "#"}
    table_by_master = {
        "M_V2_Lit": module.LIT_PARAM_TABLE,
        "M_V2_LitTranslucent": module.LIT_PARAM_TABLE,
        "M_V2_LitSkinned": module.LIT_SKINNED_PARAM_TABLE,
        "M_V2_LitSkinnedTranslucent": module.LIT_SKINNED_PARAM_TABLE,
        "M_V2_Unlit": module.UNLIT_PARAM_TABLE,
        "M_V2_TwoTexture": module.TWOTEXTURE_PARAM_TABLE,
        "M_V2_Eyes": module.EYES_PARAM_TABLE,
        "M_V2_Water": module.WATER_PARAM_TABLE,
        "M_V2_Sprite": module.SPRITE_PARAM_TABLE,
        "M_V2_Refract": module.REFRACT_PARAM_TABLE,
        "M_V2_Decal": module.DECAL_PARAM_TABLE,
    }
    assert set(table_by_master) == set(importer.EXPOSED_PARAMS)
    for master, table in table_by_master.items():
        generator_kinds = {
            name: kind
            for category, kind in kind_by_category.items()
            for name in table[category]
        }
        assert generator_kinds == importer.EXPOSED_PARAMS[master], master


def test_declared_parameters_all_land_on_the_graph(tmp_path, monkeypatch):
    editor = FakeEditor()
    module = _load(editor, tmp_path, monkeypatch)

    for name, params_attr, _blend, _domain in MASTERS:
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
    for name, _params_attr, _blend, _domain in MASTERS:
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
    for name, _params_attr, _blend, _domain in MASTERS:
        assert "%s/%s" % (PKG, name) in editor2.saved


def test_wrongly_classed_default_frames_array_is_rebuilt_not_reused(tmp_path, monkeypatch):
    """Review fix (finding 5): a prior run's `T_V2_DefaultFrames` that ended up a plain `Texture2D`
    (the `arraySize = 1` collapse the module docstring names, or any other stale-asset defect) is
    re-verified and rebuilt the next time any master's own build actually runs (not skipped by the
    recipe-stamp early return), not returned unchecked because an asset already sits at that name.
    `M_V2_Lit`'s own asset is dropped here so its build genuinely re-runs without `-PolicyForce`,
    isolating the re-verify behaviour from the force-always-rebuilds path."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    frames_path = "%s/T_V2_DefaultFrames" % PKG
    assert editor.assets[frames_path].cls == "Texture2DArray"
    editor.assets[frames_path].cls = "Texture2D"  # simulate the sticky wrong-class defect
    del editor.assets["%s/M_V2_Lit" % PKG]  # force M_V2_Lit's own build to actually re-run

    editor2 = FakeEditor()
    editor2.assets = editor.assets
    editor2.command_line = ""
    _load(editor2, tmp_path, monkeypatch, seed=False)

    assert editor2.assets[frames_path].cls == "Texture2DArray"
    assert any("not Texture2DArray" in message for message in editor2.warnings)


def test_wrongly_classed_linear_white_mask_is_rebuilt_not_reused(tmp_path, monkeypatch):
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    mask_path = "%s/T_LinearWhiteMask" % PKG
    assert editor.assets[mask_path].cls == "Texture2D"
    editor.assets[mask_path].cls = "TextureCube"  # simulate a stray wrongly-classed asset
    del editor.assets["%s/M_V2_Lit" % PKG]  # force M_V2_Lit's own build to actually re-run

    editor2 = FakeEditor()
    editor2.assets = editor.assets
    editor2.command_line = ""
    _load(editor2, tmp_path, monkeypatch, seed=False)

    assert editor2.assets[mask_path].cls == "Texture2D"
    assert any("not Texture2D" in message for message in editor2.warnings)


def test_policy_force_rebuilds_the_default_frames_array_regardless_of_its_class(tmp_path, monkeypatch):
    """`-PolicyForce` always deletes and recreates the shared helper textures too, independent of
    the recipe-stamp skip -- distinct from the re-verify path above, which only fires when a
    master's own build actually runs."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    frames_path = "%s/T_V2_DefaultFrames" % PKG

    editor2 = FakeEditor()
    editor2.assets = editor.assets
    editor2.command_line = "-PolicyForce=1"
    _load(editor2, tmp_path, monkeypatch, seed=False)

    assert editor2.assets[frames_path].cls == "Texture2DArray"
    assert frames_path in editor2.deleted


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


def test_make_missing_binds_the_checker_on_m_v2_unlit(tmp_path, monkeypatch):
    """`make_missing()` (`pipeline/unreal/make_v2_materials.py`) is what `5416ba8f` broke and
    `ab550d12` only taught the doubles to tolerate -- nothing asserted MI_V2_Missing's own shape.
    Pins the parent master, the bound checker texture, every switch stated explicitly (only
    `UseBaseTexture` on, matching `MISSING_SWITCHES`), and the blend/two-sided overrides that keep
    a later master change from moving what a missing slot looks like."""
    editor = FakeEditor()
    module = _load(editor, tmp_path, monkeypatch)

    missing_path = "%s/MI_V2_Missing" % PKG
    checker_path = "%s/T_V2_MissingChecker" % PKG
    mic = editor.assets.get(missing_path)
    assert mic is not None, "MI_V2_Missing was not authored"
    assert mic.get_class().get_name() == "MaterialInstanceConstant"
    assert mic.props.get("parent") is editor.assets["%s/M_V2_Unlit" % PKG]
    assert "ElysiumRecipe" in mic.metadata
    assert missing_path in editor.saved

    checker = editor.assets.get(checker_path)
    assert checker is not None, "T_V2_MissingChecker was not authored"
    assert mic.resolved_parameters("texture_parameter_values").get(
        module.UnlitParams.Textures.BaseTexture) is checker

    # Every switch M_V2_Unlit exposes is stated explicitly, and only UseBaseTexture is on.
    switches = mic.props.get("switches", {})
    assert switches == module.MISSING_SWITCHES
    assert switches[module.UnlitParams.Switches.UseBaseTexture] is True
    assert all(value is False for name, value in switches.items()
               if name != module.UnlitParams.Switches.UseBaseTexture)

    bpo = mic.props["base_property_overrides"]
    assert bpo.get_editor_property("override_blend_mode") is True
    assert bpo.get_editor_property("blend_mode") == "BLEND.BLEND_OPAQUE"
    assert bpo.get_editor_property("override_two_sided") is True
    assert bpo.get_editor_property("two_sided") is False


def test_make_missing_is_reused_when_current_and_force_rebuilt(tmp_path, monkeypatch):
    """Matches the `test_wrongly_classed_*_is_rebuilt_not_reused` / `test_policy_force_rebuilds_
    even_when_current` style for the masters: a no-op rerun reuses MI_V2_Missing (recipe stamp
    unchanged, not re-saved), and `-PolicyForce` rebuilds it even though nothing changed."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    missing_path = "%s/MI_V2_Missing" % PKG
    stamped = editor.assets[missing_path].metadata["ElysiumRecipe"]

    editor2 = FakeEditor()
    editor2.assets = editor.assets
    editor2.command_line = ""
    _load(editor2, tmp_path, monkeypatch, seed=False)
    assert missing_path not in editor2.saved
    assert editor2.assets[missing_path].metadata["ElysiumRecipe"] == stamped

    editor3 = FakeEditor()
    editor3.assets = editor.assets
    editor3.command_line = "-PolicyForce=1"
    _load(editor3, tmp_path, monkeypatch, seed=False)
    assert missing_path in editor3.saved
    assert editor3.assets[missing_path].metadata["ElysiumRecipe"] == stamped


# --- R5.4: the scene-fog term reads the slots ApplySceneFog stamps ---------------------------------

_FOG_HEADER = REPO / "Source/ElysiumUE/Public/ElysiumFog.h"
_SCENE_FOG_MASTERS = ("M_V2_Lit", "M_V2_LitTranslucent", "M_V2_Unlit", "M_V2_TwoTexture",
                      "M_V2_Refract")


def _fog_slots_from_header():
    """`{SlotColor, SlotStart, SlotInvRange}` off `ElysiumFog.h`, parsed rather than restated --
    the header is the one contract between the bake's stamp, `ApplySceneFog` and the graph."""
    import re
    text = _FOG_HEADER.read_text(encoding="utf-8")
    return {name: int(value) for name, value in re.findall(
        r"inline constexpr int32 (Slot\w+) = (\d+);", text)}


def test_scene_fog_masters_read_the_custom_primitive_data_slots_apply_scene_fog_stamps(
        tmp_path, monkeypatch):
    """`UElysiumMapVisuals::ApplySceneFog` writes `ElysiumFog::Pack`'s six floats at
    `SetCustomPrimitiveDataFloatArray(ElysiumFog::SlotColor, ...)`; the bake stamps the same
    block as default primitive data. A master reads them only if its three fog parameters are
    CPD-driven at exactly those indices -- a name-only match would compile and fog nothing."""
    editor = FakeEditor()
    module = _load(editor, tmp_path, monkeypatch)
    slots = _fog_slots_from_header()
    assert slots == {"SlotColor": 0, "SlotStart": 4, "SlotInvRange": 5}
    mat_fog = module.mat_fog
    assert (mat_fog.CPD_COLOR, mat_fog.CPD_START, mat_fog.CPD_INV_RANGE) == (
        slots["SlotColor"], slots["SlotStart"], slots["SlotInvRange"])

    expected = {
        mat_fog.P_COLOR: slots["SlotColor"],
        mat_fog.P_START: slots["SlotStart"],
        mat_fog.P_INV_RANGE: slots["SlotInvRange"],
    }
    for name in _SCENE_FOG_MASTERS:
        asset = editor.assets["%s/%s" % (PKG, name)]
        by_name = {n.props.get("parameter_name"): n for n in asset.expressions
                   if "parameter_name" in n.props}
        for param, index in expected.items():
            node = by_name.get(param)
            assert node is not None, "%s: %s not on the graph" % (name, param)
            assert node.props.get("use_custom_primitive_data") is True, (name, param)
            assert node.props.get("primitive_data_index") == index, (name, param)
        gate = by_name.get(mat_fog.P_INSCATTER)
        assert gate is not None and gate.props.get("default_value") == 1.0, name
        assert not gate.props.get("use_custom_primitive_data"), name

    # The masters that do NOT carry the lane carry none of the SCENE-FOG primitive-driven names --
    # M_V2_Water's `FogColor`/`FogStart` are the VMT's own water-fog keys, instance-driven. The
    # one primitive-driven scalar a non-scene-fog master may carry is R7.5 G6's lightstyle
    # brightness, which is a different lane in a slot of its own (6) and is asserted separately by
    # `test_light_style_brightness_reads_custom_primitive_data_slot_six`.
    fog_names = set(expected) | {mat_fog.P_INSCATTER}
    for name in ("M_V2_Water", "M_V2_Sprite", "M_V2_Eyes", "M_V2_Decal"):
        asset = editor.assets["%s/%s" % (PKG, name)]
        cpd = sorted(n.props.get("parameter_name") for n in asset.expressions
                     if n.props.get("use_custom_primitive_data"))
        assert not (set(cpd) & fog_names), (name, cpd)
        assert set(cpd) <= {"LightStyleBrightness"}, (name, cpd)


#: The three masters an `env_sprite` billboard or a Niagara sprite renderer ever draws through.
_SPRITE_MASTERS = ("M_V2_Sprite", "M_V2_SpriteZ", "M_V2_SpriteZLit")


#: R7.5 G6 (owner decision 4): the masters a lightstyle-bearing face can bind -- the Lit pair for
#: the pier's 34 `objects/surf` foam cards (styles 0+1, 21 also 32), Water for a lightstyle-bearing
#: water face, and TwoTexture for the eight styled blend sections (`sm_pier_1`'s
#: `blends/blend_pier*`, `sp_soc_3`'s `blends/seablend` / `searock_tunnel` / `twrconwllab`). The
#: first three carry the brightness on BOTH the base colour and the emissive; TwoTexture has no
#: emissive term of its own, so its lane is base colour alone.
_LIGHT_STYLE_MASTERS = ("M_V2_Lit", "M_V2_LitTranslucent", "M_V2_Water")
_LIGHT_STYLE_BASE_COLOR_ONLY_MASTERS = ("M_V2_TwoTexture",)


def _property_source(mel, asset, prop):
    """The one expression wired to a `MaterialProperty` pin on `asset`, or `None`."""
    sources = [src for src, _out, wired in mel.property_connections
               if wired == prop and src in asset.expressions]
    return sources[-1] if sources else None


def _feeding(mel, node, expressions):
    """`node` and everything reachable backwards from it -- `_sources_of` excludes the node
    itself, and the pin a term ends on is very often the switch that selects it (the water
    master's `Underside` specular/roughness gates, its `UseAnimatedDuDvFrames` normal gate)."""
    return [node] + _sources_of(mel, node, expressions)


def test_light_style_brightness_reads_custom_primitive_data_slot_six(tmp_path, monkeypatch):
    """R7.5 G6 / contract 3. VtMB animates a face's lighting by swapping which lightmap page it
    samples (`Mod_LoadFaces` flags the face `0x2000`, `CWorld::vfunc104` registers the pattern);
    owner decision 4 is that Lumen replaces lightmaps project-wide, so the port keeps the authored
    *motion* -- the style clock the runtime already runs -- and spends it as a brightness on the
    lit result. One CPD slot (6, `ElysiumLightStyle::SlotBrightness`), default 1 so an untagged
    primitive is untouched, reaching BOTH the base colour and the emissive on all three masters
    that can carry a lightstyle-bearing face."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    lanes = [(name, ("MP.MP_BASE_COLOR", "MP.MP_EMISSIVE_COLOR"))
             for name in _LIGHT_STYLE_MASTERS]
    lanes += [(name, ("MP.MP_BASE_COLOR",))
              for name in _LIGHT_STYLE_BASE_COLOR_ONLY_MASTERS]
    for name, properties in lanes:
        asset = editor.assets["%s/%s" % (PKG, name)]
        by_name = {n.props.get("parameter_name"): n for n in asset.expressions
                   if "parameter_name" in n.props}
        node = by_name.get("LightStyleBrightness")
        assert node is not None, name
        assert node.props.get("use_custom_primitive_data") is True, name
        assert node.props.get("primitive_data_index") == 6, name
        assert node.props.get("default_value") == 1.0, name
        for prop in properties:
            sink = _property_source(editor.mel, asset, prop)
            assert sink is not None, (name, prop)
            reached = {n.props.get("parameter_name")
                       for n in _sources_of(editor.mel, sink, asset.expressions)}
            assert "LightStyleBrightness" in reached, (name, prop)


def test_water_normal_folds_the_dudv_flipbook_into_one_normal(tmp_path, monkeypatch):
    """R7.5 look pass (`water_audit/LOOK_SPEC.md`). `Water_Old` perturbed the UVs of BOTH render
    targets by `dev/water_dudv`'s signed offset field, scaled per pass by `$refractamount` /
    `$reflectamount` (`texbem`, `water_dx80.cpp`). The two warps land on two pins: the REFRACTION
    warp is the Refraction pin's explicit 2D screen offset (`RefractAmount x WaterWarpScale`), the
    REFLECTION warp tilts `MP_NORMAL` (`ReflectAmount x WaterReflectWarpScale`), renormalised.
    Walked backwards from each pin, because a `DuDvMapFrames` parameter authored on the graph and
    wired to nothing is exactly the state R7.1 left `DuDvMap` in."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    water = editor.assets["%s/M_V2_Water" % PKG]
    normal = _property_source(editor.mel, water, "MP.MP_NORMAL")
    assert normal is not None
    reached = _feeding(editor.mel, normal, water.expressions)
    names = {n.props.get("parameter_name") for n in reached}
    assert {"DuDvMapFrames", "DuDvFrameRate", "DuDvFrameCount", "UseAnimatedDuDvFrames",
            "ReflectAmount", "WaterReflectWarpScale", "NormalMapFrames"} <= names
    assert "RefractAmount" not in names
    refraction = _property_source(editor.mel, water, "MP.MP_REFRACTION")
    assert refraction is not None
    refraction_names = {n.props.get("parameter_name")
                        for n in _feeding(editor.mel, refraction, water.expressions)}
    assert {"DuDvMapFrames", "UseAnimatedDuDvFrames", "RefractAmount",
            "WaterWarpScale"} <= refraction_names
    assert "ReflectAmount" not in refraction_names
    # The fold renormalises: an un-normalised tangent normal with an offset added into XY is not a
    # unit vector, and 5.8's base pass does not renormalise MP_NORMAL for you.
    assert any(getattr(n.cls, "__name__", "") == "MaterialExpressionNormalize" for n in reached)
    # The DUDV array is sampled as LINEAR data, not as a normal map: `TA_water_dudv` stages
    # `TC_VECTOR_DISPLACEMENTMAP`, and the `x * 2 - 1` bias is Source's own DUDV convention
    # applied explicitly (a `SAMPLERTYPE_NORMAL` object against that texture is a compile error).
    dudv_object = next(n for n in reached if n.props.get("parameter_name") == "DuDvMapFrames")
    assert dudv_object.props.get("sampler_type") == "SAMPLERTYPE.SAMPLERTYPE_LINEAR_COLOR"


def test_water_underside_drops_the_reflection_and_keeps_the_volume(tmp_path, monkeypatch):
    """R7.5 contract 1 (verdict B2). VtMB's response to a down-facing water face is
    `$reflecttexture->SetUndefined()` on that face's material -- it removes the REFLECTION and
    touches nothing else. On a deferred renderer that takes two pins: specular 0 kills the direct
    highlight, roughness 1 kills the mirror Lumen would still resolve off a smooth surface. The
    volume coefficients are the surface's, so `Underside` must NOT reach the SLW output node any
    more (R7.1 zeroed them there)."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    water = editor.assets["%s/M_V2_Water" % PKG]
    for prop in ("MP.MP_SPECULAR", "MP.MP_ROUGHNESS"):
        sink = _property_source(editor.mel, water, prop)
        assert sink is not None, prop
        reached = {n.props.get("parameter_name")
                   for n in _feeding(editor.mel, sink, water.expressions)}
        assert "Underside" in reached, prop
    output = next(n for n in water.expressions
                  if n.cls.__name__ == "MaterialExpressionSingleLayerWaterMaterialOutput")
    reached = {n.props.get("parameter_name")
               for n in _sources_of(editor.mel, output, water.expressions)}
    assert "Underside" not in reached
    assert {"CheapWater", "UseFogEnable", "FogColor", "FogStart", "FogEnd"} <= reached


def test_cheap_water_emits_the_fresnel_lerp_between_the_cube_and_the_fog_colour(
        tmp_path, monkeypatch):
    """R7.5 look pass. The cheap pass `Water_Old` draws on EVERY water unit
    (`water.cpp::SHADER_DRAW`, `WaterCheap_ps20.fxc`): `$fogcolor + cube(reflect(eye, N)) x
    fresnel`, alpha-blended by distance (`$cheapwaterstart/enddistance`) over the expensive result,
    alone with alpha 1 under `$forcecheap`. Emissive is the only pin that survives an unlit face
    (`SURF 0x408` = `WARP|NOLIGHT`), so the colour is emitted and the blend is Opacity (coverage).
    `BaseReflectFract` is the Fresnel's own base fraction, which is that parameter's VtMB meaning."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    water = editor.assets["%s/M_V2_Water" % PKG]
    emissive = _property_source(editor.mel, water, "MP.MP_EMISSIVE_COLOR")
    reached = _feeding(editor.mel, emissive, water.expressions)
    names = {n.props.get("parameter_name") for n in reached}
    assert {"CheapWater", "FogColor", "EnvMapTint", "EnvMap", "UseFixedCube",
            "BaseReflectFract", "CheapWaterStartDistance", "CheapWaterEndDistance"} <= names
    # The distance blend is coverage too: past `$cheapwaterenddistance` nothing refracts through.
    opacity = _property_source(editor.mel, water, "MP.MP_OPACITY")
    opacity_names = {n.props.get("parameter_name")
                     for n in _feeding(editor.mel, opacity, water.expressions)}
    assert {"CheapWater", "CheapWaterStartDistance", "CheapWaterEndDistance"} <= opacity_names
    assert any(getattr(n.cls, "__name__", "") == "MaterialExpressionCameraPositionWS"
               for n in reached)
    fresnel = [n for n in reached if getattr(n.cls, "__name__", "") == "MaterialExpressionFresnel"]
    assert len(fresnel) == 1
    # VtMB's own water Fresnel is `(1 - N.V)^5` (PS `c3 = (1,0,0,0)`, R0 = 0).
    assert fresnel[0].props.get("exponent") == 5.0
    fed = {dst_in for _src, _out, dst, dst_in in editor.mel.connections if dst is fresnel[0]}
    assert "BaseReflectFractionIn" in fed


def test_envmapcontrast_reaches_the_lit_fixed_cube_and_nothing_else(tmp_path, monkeypatch):
    """R7.5 G5 -- a NAMED MODERNIZATION, not a reproduction: `docs/vtmb/reflections.md` measured
    that no shipped VtMB `.psh` carries a term for `$envmapcontrast` at all, over every
    `lightmappedgeneric*envmap*` and `vertexlitgeneric*envmap*` program. 19 units author it, the
    pier's ocean card at `0.85`. It applies where a cube is actually sampled -- the authored fixed
    cube -- and defaults to 0, so every instance that does not author it is unchanged."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    for name in ("M_V2_Lit", "M_V2_LitTranslucent"):
        asset = editor.assets["%s/%s" % (PKG, name)]
        by_name = {n.props.get("parameter_name"): n for n in asset.expressions
                   if "parameter_name" in n.props}
        contrast = by_name.get("EnvMapContrast")
        assert contrast is not None, name
        assert contrast.props.get("default_value") == 0.0, name
        emissive = _property_source(editor.mel, asset, "MP.MP_EMISSIVE_COLOR")
        reached = {n.props.get("parameter_name")
                   for n in _sources_of(editor.mel, emissive, asset.expressions)}
        assert {"EnvMapContrast", "UseFixedCube", "EnvMap"} <= reached, name
        # The Lumen path (Specular/Roughness) has no cube sample to contrast, so the knob must not
        # leak into it -- otherwise every `env_cubemap` unit would silently change.
        specular = _property_source(editor.mel, asset, "MP.MP_SPECULAR")
        spec_reached = {n.props.get("parameter_name")
                        for n in _sources_of(editor.mel, specular, asset.expressions)}
        assert "EnvMapContrast" not in spec_reached, name


def test_the_dudv_default_frames_array_is_its_own_linear_asset(tmp_path, monkeypatch):
    """The DUDV lane and the normal lane cannot share a default array: `T_V2_DefaultNormalFrames`
    is `TC_NORMALMAP` and this lane samples `SAMPLERTYPE_LINEAR_COLOR` (because `TA_water_dudv`
    stages `TC_VECTOR_DISPLACEMENTMAP`), which Unreal refuses as a sampler-type mismatch in both
    directions. Two slices, for the same `arraySize = 1` collapse-to-`Texture2D` reason the other
    two default arrays carry."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    asset = editor.assets["%s/T_V2_DefaultDuDvFrames" % PKG]
    assert asset.get_class().get_name() == "Texture2DArray"
    assert asset.props.get("srgb") is False
    assert asset.props.get("compression_settings") == "TC.TC_VECTOR_DISPLACEMENTMAP"
    assert asset.props.get("mip_gen_settings") == "TMGS.TMGS_NO_MIPMAPS"


def _sources_of(mel, node, expressions):
    """Every expression reachable *backwards* from `node` through the recorded connections,
    restricted to `expressions` (one material's own nodes)."""
    own = {id(n) for n in expressions}
    seen, stack, out = {id(node)}, [node], []
    while stack:
        current = stack.pop()
        for src, _src_out, dst, _dst_in in mel.connections:
            if dst is not current or src is None or id(src) not in own or id(src) in seen:
                continue
            seen.add(id(src))
            out.append(src)
            stack.append(src)
    return out


def test_sprite_tint_lanes_read_both_the_vertex_and_the_particle_colour(tmp_path, monkeypatch):
    """One master, two vertex factories, each of which compiles the *other* colour term in as
    white: `NiagaraSpriteVertexFactory.ush` sets `VertexColor = 1` and fills `Particle.Color`,
    while the `env_sprite` billboard (`ElysiumSpriteComponent`'s `FDynamicMeshBuilder` on the local
    vertex factory) has no particle data and gets `Particle.Color = (1,1,1,1)`. VtMB writes
    `rendercolor`/`renderamt` as per-corner vertex colour (`CMeshBuilder::Color4ubv`, `1008232b`)
    with no material colour modulation, so the `UseVertexColor`/`UseVertexAlpha` lanes must reach
    *both* nodes -- reading only one silently drops a whole family's tint (`ParticleColor` alone
    drew every `env_sprite` untinted and fully opaque; `VertexColor` alone drew the particle
    sprites as black cards)."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    for name in _SPRITE_MASTERS:
        asset = editor.assets["%s/%s" % (PKG, name)]
        switches = {n.props.get("parameter_name"): n for n in asset.expressions
                    if getattr(n.cls, "__name__", "")
                    == "MaterialExpressionStaticSwitchParameter"}
        for switch_name in ("UseVertexColor", "UseVertexAlpha"):
            switch = switches.get(switch_name)
            assert switch is not None, "%s: no %s switch" % (name, switch_name)
            true_lane = [src for src, _out, dst, dst_in in editor.mel.connections
                         if dst is switch and dst_in == "True"]
            assert len(true_lane) == 1, (name, switch_name)
            reached = {getattr(n.cls, "__name__", "")
                       for n in _sources_of(editor.mel, true_lane[0], asset.expressions)}
            where = (name, switch_name, sorted(reached))
            assert "MaterialExpressionVertexColor" in reached, where
            assert "MaterialExpressionParticleColor" in reached, where


#: The two masters that carry R7.1 ruling J's UV slide -- the same graph, built twice.
_SINE_UV_MASTERS = ("M_V2_Lit", "M_V2_LitTranslucent")


def test_sine_uv_translate_reaches_the_base_lane_only(tmp_path, monkeypatch):
    """R7.1 ruling J (`water-architecture.md` -> "Surf sine UV translate"): `SineUVTranslate` moves
    the BASE texture's coordinate by `amp x wave + off`, and nothing else -- Source's
    `$baseTextureTransform` translates the base map and leaves `$bumpTransform` alone, so the
    normal lane must still read the untranslated, un-slid coordinate. Walked backwards from each
    sample, the way the two colour lanes are checked below: a name-only assertion would pass on a
    parameter authored on the graph and wired to nothing."""
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    for name in _SINE_UV_MASTERS:
        asset = editor.assets["%s/%s" % (PKG, name)]
        by_name = {n.props.get("parameter_name"): n for n in asset.expressions
                   if "parameter_name" in n.props}
        assert "SineUVTranslate" in by_name, name
        for slot, expected in (("BaseTexture", True), ("NormalMap", False)):
            sample = by_name[slot]
            reached = {n.props.get("parameter_name")
                       for n in _sources_of(editor.mel, sample, asset.expressions)}
            assert ("SineUVTranslate" in reached) is expected, (name, slot, sorted(reached))
            # Both lanes still ride the shared scale/offset transform, so the check above is a
            # real difference between the two coordinates and not a disconnected normal lane.
            assert "TexScaleOffset" in reached, (name, slot)


def test_eye_material_has_dynamic_gaze_normal_and_fade_inputs(tmp_path, monkeypatch):
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    material = editor.assets[f"{PKG}/M_V2_Eyes"]
    assert material.props["tangent_space_normal"] is False
    assert material.props["two_sided"] and material.props["dither_opacity_mask"]
    iris = next(n for n in material.expressions if n.props.get("parameter_name") == "Iris")
    assert iris.props["sampler_source"] == "SSM.SSM_CLAMP_WORLD_GROUP_SETTINGS"
    reached = _sources_of(editor.mel, iris, material.expressions)
    assert {"IrisOrigin", "IrisU", "IrisV"} <= {n.props.get("parameter_name") for n in reached}
    normal = _property_source(editor.mel, material, "MP.MP_NORMAL")
    assert {"NormalOrigin", "EyeUpN", "Flatten"} <= {
        n.props.get("parameter_name") for n in _feeding(editor.mel, normal, material.expressions)}
    vampire = next(n for n in material.expressions if n.props.get("parameter_name") == "Vampire")
    assert vampire.cls.__name__ == "MaterialExpressionScalarParameter"
    for prop in ("MP.MP_BASE_COLOR", "MP.MP_EMISSIVE_COLOR"):
        assert vampire in _feeding(editor.mel, _property_source(editor.mel, material, prop), material.expressions)
    assert any(n.props.get("parameter_name") == "ModelAlpha" for n in
               _feeding(editor.mel, _property_source(editor.mel, material, "MP.MP_OPACITY_MASK"), material.expressions))


def test_skinned_masters_compile_clothing_and_feed_camera_fade_into_both_alpha_pins(tmp_path, monkeypatch):
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    for name in ("M_V2_LitSkinned", "M_V2_LitSkinnedTranslucent"):
        material = editor.assets[f"{PKG}/{name}"]
        assert material.props["used_with_skeletal_mesh"]
        assert material.props["used_with_morph_targets"]
        assert material.props["used_with_clothing"]
        assert not material.props.get("used_with_nanite")
        assert not material.props.get("used_with_instanced_static_meshes")
        assert material.props["dither_opacity_mask"]
        assert material.props["opacity_mask_clip_value"] == .333
        for prop in ("MP.MP_OPACITY", "MP.MP_OPACITY_MASK"):
            node = _property_source(editor.mel, material, prop)
            assert node is not None
            upstream = _feeding(editor.mel, node, material.expressions)
            assert any(n.props.get("parameter_name") == "ModelAlpha" for n in upstream)
        mask = _property_source(editor.mel, material, "MP.MP_OPACITY_MASK")
        assert any(n.props.get("parameter_name") == "UseAlphaTest"
                   for n in _feeding(editor.mel, mask, material.expressions))
