"""Wield-model exporter contract: the join `UE_extract_wield.main` writes, not the decisions it
consumes -- those are `wield_corpus`'s and are exercised by `test_wield_corpus.py`.

Every install this test touches is synthesised in-code. The decision layer (`wield_corpus.classify`
and its checks, `bake_pose`, `model_materials`, `skin_families`, `bone_motion`) and the geometry
writer (`UE_mdl_skeletal.write_model`) are mocked -- what is exercised here is the orchestration:
row classification into the four `kind`s, the census assertion, the reference-pose readback, and
the texture-channel union. The `.tth`/`.ttz` texture decode is real (it is cheap and already
covered end to end by `test_wield_corpus.py`'s material tests), so the texture-inventory tests
prove the actual `tex/` files land, not just that a dict entry was written.
"""
from __future__ import annotations

import json
import os
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest import mock

import pytest

from elysium_pipeline import wield_corpus as W
from elysium_pipeline.exporters import UE_extract_wield as export
from elysium_pipeline.formats import bsp, eskm, mdl, mdl_skel
from elysium_pipeline.formats.tex_to_png import FMT_BGR888
IDENTITY_Q = (0.0, 0.0, 0.0, 1.0)


def _bone(index, name, parent, pos=(0.0, 0.0, 0.0), quat=IDENTITY_Q):
    return mdl_skel.Bone(index=index, name=name, parent=parent, pos=pos, quat=quat,
                        posscale=(1.0, 1.0, 1.0), rotscale=(1.0, 1.0, 1.0, 1.0),
                        pose_to_bone=(1.0, 0.0, 0.0, 0.0,
                                      0.0, 1.0, 0.0, 0.0,
                                      0.0, 0.0, 1.0, 0.0),
                        flags=0)


def _tth(width=1, height=1, fmt=FMT_BGR888, mips=1):
    """The subset of a `.tth` header `tex_to_png.parse_tth` reads (`test_wield_corpus.py`'s
    fixture, reused so the envmask decode this module actually performs is real)."""
    buf = bytearray(80)
    buf[0:4] = b"TTH\0"
    v = 8
    buf[v:v + 4] = b"VTF\0"
    struct.pack_into("<HH", buf, v + 16, width, height)
    struct.pack_into("<I", buf, v + 52, fmt)
    buf[v + 56] = mips
    return bytes(buf)


class _Install:
    """A synthetic patch-first index: install key -> file on disk, read through install.read."""

    def __init__(self, root: Path):
        self.root = root
        self.index: dict[str, tuple[str, str]] = {}

    def add(self, key: str, text: str = "") -> None:
        path = self.root / key.replace("/", "_")
        path.write_text(text, encoding="ascii")
        self.index[key] = ("loose", str(path))

    def add_bytes(self, key: str, data: bytes) -> None:
        path = self.root / key.replace("/", "_")
        path.write_bytes(data)
        self.index[key] = ("loose", str(path))

    def item(self, classname: str, body: str) -> None:
        self.add(f"vdata/items/{classname}.txt", "WeaponData\n{\n" + body + "}\n")


#: The one real model every happy-path test shares: two bones, `socket_hand`, one material and one
#: skin-family override whose albedo/envmask a real model_materials call would never reach on its
#: own -- so the texture-inventory tests prove the skin-family branch, not the material branch.
REAL_MODEL = "models/weapons/real/w_real.mdl"
NULL_F, NULL_M = "models/w_null.mdl", "models/weapons/w_null.mdl"
ABSENT_MODEL = "models/weapons/ghostgun/w_f_ghostgun.mdl"

#: Matches `_bones()` below; used to build both the mocked `bake_pose` answer and the eskm
#: readback fixture from the same source-space numbers, through the real coordinate conversion.
_POSE_LOCALS = (((0.0, 0.0, 0.0), IDENTITY_Q), ((4.0, 1.0, 0.0), IDENTITY_Q))

#: The fixture's expected corpus shape -- 4 definitions, 8 rows, one real model -- which is what
#: `EXPECTED_CENSUS`/`EXPECTED_BINDINGS` are patched to for every happy-path test, since the real
#: constants describe the full 244-definition install this test never touches.
_CENSUS = {
    "definitions": 4, "definitions_naming_a_wield_model": 3, "rows": 8,
    "null_rows": 2, "empty_rows": 2, "absent_rows": 2, "absent_paths": 1,
    "real_rows": 2, "real_models": 1, "texture_union": 3,
}
_BINDINGS = {"socket_hand": 1}


def _bones():
    return [_bone(0, "Bip01", -1), _bone(1, "handle", 0, pos=(4.0, 1.0, 0.0))]


def _materials():
    return [W.MaterialRow(name="claws_mat", albedo="claws", flags=frozenset(),
                          failure="", envmask="", bump="")]


def _skins():
    # The one shipped case (`w_{m,f}_fire_axe` family 1) is a slot override whose albedo/envmask
    # a plain `model_materials` walk never names -- exactly what this fixture stands in for.
    return [W.SkinOverride(family=1, slot="claws_mat", material="ghost_mat", albedo="ghost",
                           flags=frozenset({"translucent"}), failure="",
                           envmask="ghost_mask", bump="")]


def _classification():
    return W.Classification(binding="socket_hand", mount_bone="handle", hand_bone="Bip01",
                            collapse_bone="handle", grip="right",
                            mount_bind=((4.0, 1.0, 0.0), IDENTITY_Q),
                            bone_count=2, skinned_bone_count=1, anomalies=())


def _pose():
    return W.Pose(locals=_POSE_LOCALS, source="clip", label="idle", bind_offset_pos=0.0,
                 bind_offset_rot=0.0)


def _passing_readback():
    """The eskm rows a correct write would read back: `_POSE_LOCALS` through the same
    `bsp.source_to_unreal`/`source_quat_to_unreal` the real writer and the real assertion use."""
    rows = []
    for index, (pos, quat) in enumerate(_POSE_LOCALS):
        name = ("Bip01", "handle")[index]
        rows.append((name, -1 if index == 0 else 0,
                    bsp.source_to_unreal(*pos), bsp.source_quat_to_unreal(*quat)))
    return rows


def _fake_write_model(_idx, _model_path, out_dir, stem=None, ref_pose=None, **_kw):
    """Stands in for `UE_mdl_skeletal.write_model`: the geometry writer is frozen and covered by
    its own tests, so this only reproduces the one side effect this module's own logic depends on
    -- the albedo files `_register_albedo` expects to already be on disk."""
    tex = os.path.join(out_dir, "tex")
    os.makedirs(tex, exist_ok=True)
    for key in ("claws", "ghost"):
        (Path(tex) / (mdl.sanitize(key) + ".png")).write_bytes(b"\x89PNG\r\n")
    return {"stem": stem, "bones": len(ref_pose or ())}


# WieldExportTests
# Shared fixture: one real model plus a null/empty/absent row apiece, patched onto the
# exporter's decision-layer calls. `_run` mocks the census constants to the fixture's own shape
# so `main` does not fail its own drift assertion against the 244-definition install.

def _install(root: Path) -> _Install:
    inst = _Install(root)
    inst.item("item_a_real", f'\t"wieldmodel_f"\t"{REAL_MODEL}"\n'
                             f'\t"wieldmodel_m"\t"{REAL_MODEL}"\n')
    inst.item("item_b_null", f'\t"wieldmodel_f"\t"{NULL_F}"\n\t"wieldmodel_m"\t"{NULL_M}"\n')
    inst.item("item_c_empty", '\t"wieldmodel_f"\t""\n\t"wieldmodel_m"\t""\n')
    inst.item("item_d_absent", f'\t"wieldmodel_f"\t"{ABSENT_MODEL}"\n'
                               f'\t"wieldmodel_m"\t"{ABSENT_MODEL}"\n')
    # `mdl.load` needs both companions present for the one model meant to resolve as real.
    inst.add_bytes(REAL_MODEL, b"IDST-fake-mdl")
    inst.add_bytes(REAL_MODEL[:-4] + ".dx80.vtx", b"fake-vtx")
    # The skin-family-only envmask this module must decode for real.
    inst.add(
        "materials/ghost_mask.vmt",
        '"VertexLitGeneric"\n{\n\t"$basetexture" "ghost_mask"\n}\n',
    )
    inst.add_bytes("materials/ghost_mask.tth", _tth())
    inst.add_bytes("materials/ghost_mask.ttz", zlib.compress(bytes((1, 2, 3))))
    return inst


def _run(root: Path, out: Path, *, bone_locals=None, census=None, bindings=None,
         classification=None, trail_tip=None, capture_attachments=None):
    """One `main()` call with the decision layer and the geometry writer replaced, against a
    fresh install rooted at `root` and an export root at `out`.

    `capture_attachments`, when given a list, receives `write_model`'s `extra_attachments`
    kwarg from every call -- how the socket_prop trail-tip tests observe what would have
    been baked into the `.eskm` without a real geometry writer.
    """
    inst = _install(root)

    def _write_model(idx, model_path, out_dir_, stem=None, ref_pose=None,
                     extra_attachments=None, **kw):
        if capture_attachments is not None:
            capture_attachments.append(extra_attachments)
        return _fake_write_model(idx, model_path, out_dir_, stem=stem, ref_pose=ref_pose, **kw)

    patches = [
        mock.patch.object(export, "export_root", return_value=out),
        mock.patch.object(export, "write_model", side_effect=_write_model),
        mock.patch.object(export, "EXPECTED_CENSUS", census or _CENSUS),
        mock.patch.object(export, "EXPECTED_BINDINGS", bindings or _BINDINGS),
        mock.patch.object(mdl_skel, "read_bones", return_value=_bones()),
        # main() decodes each model's geometry once and hands it to the decision layer; the
        # decision layer is mocked here, so the shared decode is stubbed the same way
        # read_bones is.
        mock.patch.object(mdl_skel, "decode_skinned", return_value={}),
        mock.patch.object(W, "skinned_bones", return_value={1}),
        mock.patch.object(W, "classify", return_value=classification or _classification()),
        mock.patch.object(W, "check_subtree",
                          return_value=W.Check("subtree", True, ())),
        mock.patch.object(W, "check_collapse",
                          return_value=W.Check("collapse", True, ())),
        mock.patch.object(W, "check_motion",
                          return_value=W.Check("motion", True, ())),
        mock.patch.object(W, "trail_tip", return_value=trail_tip),
        mock.patch.object(W, "bake_pose", return_value=_pose()),
        mock.patch.object(W, "model_materials", return_value=_materials()),
        mock.patch.object(W, "skin_families", return_value=_skins()),
        mock.patch.object(W, "bone_motion", return_value=[
            W.BoneMotion(name="Bip01", max_pos=0.0, max_rot=0.0, skinned=False),
            W.BoneMotion(name="handle", max_pos=0.02, max_rot=1.4, skinned=True),
        ]),
        mock.patch.object(mdl_skel, "local_sequences", return_value=[
            mdl_skel.Seq(label="idle", base=0, frames=30, fps=30.0,
                        activity="ACT_IDLE", actweight=1, flags=0)]),
        mock.patch.object(eskm, "read", return_value=b""),
        mock.patch.object(eskm, "bone_locals",
                          return_value=bone_locals if bone_locals is not None
                          else _passing_readback()),
    ]
    with mock.patch.object(W, "npc_carried", return_value={"item_a_real"}):
        for patch in patches:
            patch.start()
        try:
            export.main(index=inst.index)
        finally:
            for patch in patches:
                patch.stop()
    return json.loads((out / "items" / "wield_models.json").read_text(encoding="utf-8"))


def test_the_four_kinds_land_on_their_rows() -> None:
    with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as out:
        manifest = _run(Path(root), Path(out))

        rows = manifest["rows"]
        assert rows["item_a_real"]["f"]["kind"] == "real"
        assert rows["item_a_real"]["f"]["stem"] == "w_real"
        assert rows["item_b_null"]["f"]["kind"] == "null"
        assert rows["item_c_empty"]["f"]["kind"] == "empty"
        assert rows["item_c_empty"]["f"]["source"] == ""
        assert rows["item_d_absent"]["f"]["kind"] == "absent"
        assert rows["item_a_real"]["npc_carried"]
        assert not rows["item_b_null"]["npc_carried"]


def test_an_absent_model_is_recorded_not_fatal() -> None:
    # The whole run must complete -- an absent model is an authored possibility, never fatal
    # (`docs/vtmb/wielded_weapons.md`) -- and the census must count it rather than drop it.
    with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as out:
        manifest = _run(Path(root), Path(out))

        assert manifest["census"]["absent_rows"] == 2
        assert manifest["census"]["absent_paths"] == 1
        assert ABSENT_MODEL not in manifest["models"]


def test_motion_rows_exclude_bip01_bones() -> None:
    with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as out:
        manifest = _run(Path(root), Path(out))

        motion = manifest["models"]["w_real"]["motion"]
        assert [row["name"] for row in motion] == ["handle"]


def test_texture_inventory_includes_a_skin_family_only_key() -> None:
    with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as out:
        manifest = _run(Path(root), Path(out))

        # One flat key namespace across all three channels (`_register_texture`'s docstring):
        # a key used by two different roles is still one install file and one manifest entry.
        textures = manifest["textures"]
        assert "claws" in textures
        assert "ghost" in textures
        assert "ghost_mask" in textures
        assert textures["ghost_mask"] == "tex/ghost_mask.png"
        # The decode is real -- the file this key names actually exists.
        assert (Path(out) / "items" / "wield" / "tex" / "ghost_mask.png").is_file()
        assert manifest["census"]["texture_union"] == 3


def test_ref_pose_readback_mismatch_fails_loudly() -> None:
    wrong = list(_passing_readback())
    name, parent, _pos, quat = wrong[1]
    wrong[1] = (name, parent, (99.0, 0.0, 0.0), quat)          # "handle" moved 99cm
    with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as out:
        with pytest.raises(AssertionError) as ctx:
            _run(Path(root), Path(out), bone_locals=wrong)

    message = str(ctx.value)
    assert REAL_MODEL in message
    assert "handle" in message


def test_socket_hand_model_carries_no_trail_tip() -> None:
    # The fixture's REAL_MODEL classifies socket_hand (a firearm shape); a melee-only
    # attachment must not appear on it.
    with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as out:
        manifest = _run(Path(root), Path(out))

        assert manifest["models"]["w_real"]["trail_tip"] is None


def test_socket_prop_model_carries_a_trail_tip_attachment() -> None:
    prop_cls = W.Classification(binding="socket_prop", mount_bone="handle", hand_bone="Bip01",
                                collapse_bone="handle", grip="right",
                                mount_bind=((4.0, 1.0, 0.0), IDENTITY_Q),
                                bone_count=2, skinned_bone_count=1, anomalies=())
    tip = ((6.0, 1.0, 0.0), IDENTITY_Q)
    captured: list = []
    with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as out:
        manifest = _run(Path(root), Path(out), bindings={"socket_prop": 1},
                             classification=prop_cls, trail_tip=tip,
                             capture_attachments=captured)

    trail_tip_out = manifest["models"]["w_real"]["trail_tip"]
    assert trail_tip_out["bone"] == "handle"
    assert trail_tip_out["pos"] == [6.0, 1.0, 0.0]
    assert trail_tip_out["quat"] == list(IDENTITY_Q)

    # write_model must have received the synthetic attachment record too -- the manifest field
    # and the baked socket are the same fact stated twice, and both have to agree.
    [attachments] = captured
    assert len(attachments) == 1
    assert attachments[0].name == "TrailTip"
    assert attachments[0].bone == 1   # "handle" is StudioBone index 1 in `_bones()`
    assert attachments[0].pos == (6.0, 1.0, 0.0)
    assert attachments[0].quat == IDENTITY_Q


def test_manifest_is_byte_identical_across_two_runs() -> None:
    with tempfile.TemporaryDirectory() as root:
        with tempfile.TemporaryDirectory() as out_a, tempfile.TemporaryDirectory() as out_b:
            _run(Path(root), Path(out_a))
            _run(Path(root), Path(out_b))

            text_a = (Path(out_a) / "items" / "wield_models.json").read_text(encoding="utf-8")
            text_b = (Path(out_b) / "items" / "wield_models.json").read_text(encoding="utf-8")

    assert text_a == text_b
