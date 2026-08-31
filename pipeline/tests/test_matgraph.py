"""`pipeline/unreal/matgraph.py`, the V2-master graph-authoring helper layer (SF-4.3a), exercised
against a faked `unreal.MaterialEditingLibrary` the way `test_import_textures_editor.py` fakes the
texture-import editor surface. `matgraph` is import-safe (no module-scope editor work), so the
fake only needs to answer `create_material_expression` / `connect_material_expressions` /
`connect_material_property` and the handful of enums the helpers read.
"""
from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]


class FakeNode:
    def __init__(self, cls, x, y):
        self.cls = cls
        self.x, self.y = x, y
        self.props = {}

    def set_editor_property(self, name, value):
        self.props[name] = value

    def get_class(self):
        return SimpleNamespace(get_name=lambda: getattr(self.cls, "__name__", str(self.cls)))


class FakeMaterialEditingLibrary:
    def __init__(self):
        self.connections = []          # (src, out, dst, inp)
        self.property_connections = []  # (src, out, prop)
        self.refuse_pins = set()       # {(dst_input,)} to refuse
        self.refuse_properties = set()

    def create_material_expression(self, mat, cls, x, y):
        return FakeNode(cls, x, y)

    def connect_material_expressions(self, src, src_out, dst, dst_in):
        self.connections.append((src, src_out, dst, dst_in))
        return dst_in not in self.refuse_pins

    def connect_material_property(self, mat, src, src_out, prop):
        self.property_connections.append((src, src_out, prop))
        return prop not in self.refuse_properties


def _classes():
    names = [
        "MaterialExpressionTextureSampleParameter2D",
        "MaterialExpressionTextureSampleParameterCube",
        "MaterialExpressionTextureObjectParameter",
        "MaterialExpressionTextureSample",
        "MaterialExpressionScalarParameter",
        "MaterialExpressionVectorParameter",
        "MaterialExpressionComponentMask",
        "MaterialExpressionStaticSwitchParameter",
        "MaterialExpressionCollectionParameter",
        "MaterialExpressionConstant",
        "MaterialExpressionConstant3Vector",
        "MaterialExpressionMultiply",
        "MaterialExpressionAdd",
        "MaterialExpressionSubtract",
        "MaterialExpressionDivide",
        "MaterialExpressionDotProduct",
        "MaterialExpressionPower",
        "MaterialExpressionLinearInterpolate",
        "MaterialExpressionSaturate",
        "MaterialExpressionOneMinus",
        "MaterialExpressionClamp",
        "MaterialExpressionAppendVector",
        "MaterialExpressionTime",
        "MaterialExpressionSine",
        "MaterialExpressionPanner",
        "MaterialExpressionVertexColor",
        "MaterialExpressionFresnel",
        "MaterialExpressionReflectionVectorWS",
    ]
    return {name: type(name, (), {}) for name in names}


def _fake_unreal(mel, textures=None):
    textures = textures or {}

    class LinearColor:
        def __init__(self, r, g, b, a):
            self.r, self.g, self.b, self.a = r, g, b, a

    def _enum(prefix, *names):
        return SimpleNamespace(**{n: "%s.%s" % (prefix, n) for n in names})

    ns = SimpleNamespace(
        MaterialEditingLibrary=mel,
        load_asset=lambda path: textures.get(path),
        LinearColor=LinearColor,
        MaterialSamplerType=_enum(
            "SAMPLERTYPE", "SAMPLERTYPE_COLOR", "SAMPLERTYPE_MASKS",
            "SAMPLERTYPE_NORMAL", "SAMPLERTYPE_LINEAR_COLOR"),
        TextureMipValueMode=_enum("TMVM", "TMVM_MIP_LEVEL"),
    )
    for name, cls in _classes().items():
        setattr(ns, name, cls)
    return ns


def _load(mel, textures=None):
    fake = _fake_unreal(mel, textures)
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        sys.modules.pop("pipeline.unreal.mat_fog", None)
        sys.modules.pop("pipeline.unreal.matgraph", None)
        spec = importlib.util.spec_from_file_location(
            "elysium_test_matgraph", REPO / "pipeline/unreal/matgraph.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    module.unreal = fake
    return module


# --- basic node construction ----------------------------------------------------------------


def test_tex_defaults_to_color_sampler_and_the_engine_default_texture():
    mel = FakeMaterialEditingLibrary()
    default_tex = object()
    module = _load(mel, {"/Engine/EngineResources/DefaultTexture.DefaultTexture": default_tex})
    g = module.Graph(object())

    n = g.tex("BaseTexture", 0, 0)

    assert n.props["parameter_name"] == "BaseTexture"
    assert n.props["sampler_type"] == "SAMPLERTYPE.SAMPLERTYPE_COLOR"
    assert n.props["texture"] is default_tex


def test_tex_mask_kind_has_no_universal_default_but_accepts_an_explicit_one():
    mel = FakeMaterialEditingLibrary()
    white = object()
    module = _load(mel, {"/Game/X/T_White": white})
    g = module.Graph(object())

    unbound = g.tex("EnvMapMask", 0, 0, kind="mask")
    bound = g.tex("EnvMapMask", 0, 0, kind="mask", default="/Game/X/T_White")

    assert "texture" not in unbound.props
    assert bound.props["texture"] is white
    assert bound.props["sampler_type"] == "SAMPLERTYPE.SAMPLERTYPE_MASKS"


def test_vec3_masks_the_vector_parameter_to_rgb():
    mel = FakeMaterialEditingLibrary()
    module = _load(mel)
    g = module.Graph(object())

    masked = g.vec3("Color", (1.0, 0.5, 0.25, 1.0), 0, 0)

    assert masked.cls.__name__ == "MaterialExpressionComponentMask"
    assert masked.props["r"] and masked.props["g"] and masked.props["b"]
    assert masked.props["a"] is False
    # the parameter feeds the mask, not the other way around
    assert mel.connections[-1][2] is masked


def test_switch_wires_both_branches_and_carries_the_default():
    mel = FakeMaterialEditingLibrary()
    module = _load(mel)
    g = module.Graph(object())
    true_n, false_n = FakeNode(None, 0, 0), FakeNode(None, 0, 0)

    n = g.switch("UseEnvMap", true_n, false_n, 0, 0, default=True)

    assert n.props["parameter_name"] == "UseEnvMap"
    assert n.props["default_value"] is True
    assert (true_n, "", n, "True") in mel.connections
    assert (false_n, "", n, "False") in mel.connections


def test_mpc_raises_without_a_bound_collection():
    mel = FakeMaterialEditingLibrary()
    module = _load(mel)
    g = module.Graph(object(), collection=None)

    with pytest.raises(SystemExit):
        g.mpc("DefaultSpecular", 0, 0)


def test_mpc_reads_the_bound_collection():
    mel = FakeMaterialEditingLibrary()
    module = _load(mel)
    collection = object()
    g = module.Graph(object(), collection=collection)

    n = g.mpc("DefaultSpecular", 0, 0)

    assert n.props["collection"] is collection
    assert n.props["parameter_name"] == "DefaultSpecular"


# --- connect / to raise on a refused pin, exactly like mat_fog.connect -----------------------


def test_connect_raises_on_a_refused_pin():
    mel = FakeMaterialEditingLibrary()
    mel.refuse_pins.add("NoSuchPin")
    module = _load(mel)
    a, b = FakeNode(None, 0, 0), FakeNode(None, 0, 0)

    with pytest.raises(SystemExit):
        module.connect(a, "", b, "NoSuchPin")


def test_to_raises_on_a_refused_material_property():
    mel = FakeMaterialEditingLibrary()
    mel.refuse_properties.add("MP_BASE_COLOR")
    module = _load(mel)
    g = module.Graph(object())
    n = FakeNode(None, 0, 0)

    with pytest.raises(SystemExit):
        g.to(n, "", "MP_BASE_COLOR")


def test_to_succeeds_and_records_the_property_connection():
    mel = FakeMaterialEditingLibrary()
    module = _load(mel)
    g = module.Graph(object())
    n = FakeNode(None, 0, 0)

    g.to(n, "RGB", "MP_BASE_COLOR")

    assert (n, "RGB", "MP_BASE_COLOR") in mel.property_connections


# --- class LUT sampling (SF-4.1 knobs, mechanics doc 1b) --------------------------------------


def test_class_lut_uv_is_the_texel_centre_of_a_64_row_lut():
    mel = FakeMaterialEditingLibrary()
    module = _load(mel)
    g = module.Graph(object())
    index = FakeNode(None, 0, 0)

    uv = module.class_lut_uv(g, index, 0, 0)

    assert uv.cls.__name__ == "MaterialExpressionAppendVector"
    # walk back: uv <- (u, v); u <- Divide(Add(index, 0.5), 64)
    u_conn = next(c for c in mel.connections if c[2] is uv and c[3] == "A")
    divide = u_conn[0]
    assert divide.cls.__name__ == "MaterialExpressionDivide"
    add_conn = next(c for c in mel.connections if c[2] is divide and c[3] == "A")
    add = add_conn[0]
    assert add.cls.__name__ == "MaterialExpressionAdd"
    assert any(c[0] is index for c in mel.connections if c[2] is add)
    rows_conn = next(c for c in mel.connections if c[2] is divide and c[3] == "B")
    assert rows_conn[0].props["r"] == 64.0
    half_conn = next(c for c in mel.connections if c[2] is add and c[3] == "B")
    assert half_conn[0].props["r"] == 0.5


def test_read_class_lut_samples_at_a_hard_pinned_mip_zero():
    mel = FakeMaterialEditingLibrary()
    module = _load(mel)
    g = module.Graph(object())
    lut = FakeNode(None, 0, 0)
    uv = FakeNode(None, 0, 0)

    sampled = module.read_class_lut(g, lut, uv, 0, 0)

    assert sampled.cls.__name__ == "MaterialExpressionTextureSample"
    assert sampled.props["mip_value_mode"] == "TMVM.TMVM_MIP_LEVEL"
    assert sampled.props["const_mip_value"] == 0.0
    assert (lut, "", sampled, "Tex") in mel.connections
    assert (uv, "", sampled, "UVs") in mel.connections
