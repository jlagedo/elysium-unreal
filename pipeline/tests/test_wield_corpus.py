"""The wield corpus's decisions: the item join, mount detection, binding modes and the three
checks the socket collapse rests on.

Every definition and every skeleton here is synthesised in-code, so nothing depends on the user's
game install. The container reading itself belongs to `mdl_skel` and is covered there; what is
exercised here is the decision layer over an already-decoded skeleton.
"""

from __future__ import annotations

import math
import struct
import zlib
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import pytest

from elysium_pipeline import wield_corpus as W
from elysium_pipeline.formats import install, mdl, mdl_skel
from elysium_pipeline.formats.tex_to_png import FMT_BGR888, FMT_DXT1

IDENTITY_Q = (0.0, 0.0, 0.0, 1.0)


def bone(index, name, parent, pos=(0.0, 0.0, 0.0), quat=IDENTITY_Q):
    """One `mdl_skel.Bone` with the fields the decision layer reads.

    `pose_to_bone` defaults to a garbage identity; the tests that assert on it set it themselves.
    """
    return mdl_skel.Bone(index=index, name=name, parent=parent, pos=pos, quat=quat,
                         posscale=(1.0, 1.0, 1.0), rotscale=(1.0, 1.0, 1.0, 1.0),
                         pose_to_bone=(1.0, 0.0, 0.0, 0.0,
                                       0.0, 1.0, 0.0, 0.0,
                                       0.0, 0.0, 1.0, 0.0),
                         flags=0)


def arm(hand="Bip01 R Hand"):
    """The shared prefix every wield model carries, ending at one hand."""
    chain = ["Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1", "Bip01 Spine2",
             "Bip01 Neck", "Bip01 R Clavicle", "Bip01 R UpperArm", "Bip01 R Forearm", hand]
    return [bone(i, name, i - 1) for i, name in enumerate(chain)]


def with_subrig(*specs, hand="Bip01 R Hand"):
    """The arm chain plus weapon bones. Each spec is ``(name, parent_offset_or_None)`` where a
    ``None`` parent hangs the bone off the hand."""
    bones = arm(hand)
    base = len(bones)
    hand_index = base - 1
    for offset, (name, parent) in enumerate(specs):
        bones.append(bone(base + offset, name,
                          hand_index if parent is None else base + parent))
    return bones


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


def _tth(width, height, fmt, mips=1):
    """A minimal `.tth` buffer carrying just the embedded VTF header fields
    `tex_to_png.parse_tth` reads: width/height, the high-res format, and the mip count."""
    buf = bytearray(80)
    buf[0:4] = b"TTH\0"
    v = 8
    buf[v:v + 4] = b"VTF\0"
    struct.pack_into("<HH", buf, v + 16, width, height)
    struct.pack_into("<I", buf, v + 52, fmt)
    buf[v + 56] = mips
    return bytes(buf)


def test_repeated_key_resolves_last_wins() -> None:
    with tempfile.TemporaryDirectory() as root:
        install = _Install(Path(root))
        install.item("item_w_x", '\t"anim_prefix"\t"first"\n\t"anim_prefix"\t"last"\n'
                                 '\t"wieldmodel_f"\t"models/weapons/w_f_x.mdl"\n'
                                 '\t"wieldmodel_m"\t"models/weapons/w_m_x.mdl"\n')

        rows = W.wield_rows(install.index)

        assert [row.anim_prefix for row in rows] == ["last", "last"]


def test_shows_view_model_defaults_to_one_and_zero_is_explicit() -> None:
    with tempfile.TemporaryDirectory() as root:
        install = _Install(Path(root))
        install.item("item_w_default", '\t"wieldmodel_f"\t"models/weapons/w_f_a.mdl"\n')
        install.item("item_a_armor", '\t"shows_view_model"\t"0"\n'
                                     '\t"wieldmodel_f"\t"models/weapons/w_null.mdl"\n')

        gates = {row.classname: row.shows_view_model for row in W.wield_rows(install.index)}

        assert gates["item_w_default"] == 1
        assert gates["item_a_armor"] == 0


def test_null_empty_and_named_are_three_distinct_values() -> None:
    with tempfile.TemporaryDirectory() as root:
        install = _Install(Path(root))
        install.item("item_null", '\t"wieldmodel_f"\t"models/weapons/w_null.mdl"\n'
                                  '\t"wieldmodel_m"\t"models/weapons/w_null.mdl"\n')
        install.item("item_empty", '\t"wieldmodel_f"\t""\n\t"wieldmodel_m"\t""\n')
        install.item("item_real", '\t"wieldmodel_f"\t"models/weapons/katana/w_f_katana"\n'
                                  '\t"wieldmodel_m"\t"models/weapons/katana/w_m_katana.mdl"\n')

        rows = {(r.classname, r.sex): r.model for r in W.wield_rows(install.index)}

        assert W.is_null(rows[("item_null", "f")])
        assert rows[("item_empty", "f")] == ""
        assert not W.is_null(rows[("item_empty", "f")])
        # A definition may omit the extension; the engine resolves it as a `.mdl` all the same.
        assert rows[("item_real", "f")] == "models/weapons/katana/w_f_katana.mdl"


def test_bit_flags_are_read_off_their_own_keys() -> None:
    with tempfile.TemporaryDirectory() as root:
        install = _Install(Path(root))
        install.item("item_w_y", '\t"BitFlag_CantBeLast"\t"1"\n'
                                 '\t"reload_single"\t"1"\n'
                                 '\t"wieldmodel_f"\t"models/weapons/w_f_y.mdl"\n')

        row = W.wield_rows(install.index)[0]

        assert row.cant_be_last
        assert row.reload_single
        assert not row.discipline_tgt


def test_stem_is_the_basename_and_survives_backslashes() -> None:
    assert W.stem("models\\weapons\\Katana\\wield\\W_M_Katana.mdl") == "w_m_katana"
    assert W.stem("") == ""


def test_prop_bone_mount_is_socket_prop() -> None:
    bones = with_subrig(("handle", None))
    cls = W.classify_bones(bones, {10})

    assert cls.binding == "socket_prop"
    assert cls.mount_bone == "handle"
    assert cls.collapse_bone == "handle"
    assert cls.grip == "right"


def test_unmatched_mount_collapses_onto_the_hand() -> None:
    bones = with_subrig(("body", None), ("slide", 0), ("mag", 0))
    cls = W.classify_bones(bones, {10, 11, 12})

    assert cls.binding == "socket_hand"
    assert cls.mount_bone == "body"
    assert cls.collapse_bone == "Bip01 R Hand"


def test_left_hand_chain_reports_left_grip() -> None:
    bones = with_subrig(("bush hook", None), hand="Bip01 L Hand")
    bones[6] = bone(6, "Bip01 L Clavicle", 5)
    bones[7] = bone(7, "Bip01 L UpperArm", 6)
    bones[8] = bone(8, "Bip01 L Forearm", 7)

    assert W.classify_bones(bones, {10}).grip == "left"


def test_unskinned_leftover_beside_the_mount_is_ignored() -> None:
    # Two bones hang off the hand; only one carries weight, so only one is a skinned root.
    bones = with_subrig(("hands box", None), ("body", None), ("slide", 1))
    cls = W.classify_bones(bones, {11, 12})

    assert cls.binding == "socket_hand"
    assert cls.mount_bone == "body"


def test_leftover_on_a_second_root_outside_the_biped_chain_is_ignored() -> None:
    bones = with_subrig(("body", None))
    bones.append(bone(11, "hands box", -1))          # its own root, no Biped parent
    cls = W.classify_bones(bones, {10})

    assert cls.binding == "socket_hand"
    assert cls.mount_bone == "body"


def test_mount_one_unskinned_bone_below_the_hand_still_resolves() -> None:
    # The general "topmost skinned in the hand's subtree" rule, not "direct child of the hand".
    bones = with_subrig(("filler", None), ("body", 0))
    cls = W.classify_bones(bones, {11})

    assert cls.binding == "socket_hand"
    assert cls.mount_bone == "body"


def test_degenerate_identity_bind_is_recorded_not_corrected() -> None:
    bones = with_subrig(("bush hook", None))
    bones[10] = bone(10, "bush hook", 9, pos=(0.0, 0.0, 1e-7), quat=IDENTITY_Q)
    cls = W.classify_bones(bones, {10})

    assert "degenerate_bind" in cls.anomalies
    assert cls.binding == "socket_prop"
    assert cls.mount_bind[0] == (0.0, 0.0, 1e-7)


def test_ordinary_bind_carries_no_anomaly() -> None:
    bones = with_subrig(("handle", None))
    bones[10] = bone(10, "handle", 9, pos=(4.0, 0.5, 0.0))

    assert W.classify_bones(bones, {10}).anomalies == ()


# TrailTipTests
# `trail_tip_from_geometry`: the melee weapon-trail VFX's synthetic `TrailTip` attachment,
# the model's own geometry's farthest point from the mount along its own long axis.

# TrailTipTests
# `trail_tip_from_geometry`: the melee weapon-trail VFX's synthetic `TrailTip` attachment,
# the model's own geometry's farthest point from the mount along its own long axis.

def _rig():
    # Every arm bone is at the origin with an identity bind (the `bone()` default), so the
    # mount's bind-space WORLD transform equals its own local: a pure translation to (4,0,0).
    bones = with_subrig(("handle", None))
    bones[10] = bone(10, "handle", 9, pos=(4.0, 0.0, 0.0))
    return bones, W.classify_bones(bones, {10})


def test_tip_sits_on_the_long_axis_at_the_far_extreme() -> None:
    bones, cls = _rig()
    # In the mount's local frame (subtract the mount's world translation, (4,0,0)): the grip
    # end at the origin, the blade's far corners 6 units out on X, 2 wide on Y and Z.
    positions = [(4.0, 0.0, 0.0), (10.0, 2.0, -1.0), (10.0, 0.0, 1.0)]

    pos, quat = W.trail_tip_from_geometry(bones, cls, positions)

    # X is the long axis (extent 6 against 2 on Y/Z) and the far end is the +X extreme.
    assert pos[0] == pytest.approx(6.0, abs=1e-7)
    # The other two axes sit at the geometry's own midpoint, not the origin -- Y's bbox is
    # [0,2], not [-1,1], so a wrong "midpoint is always zero" implementation would fail this.
    assert pos[1] == pytest.approx(1.0, abs=1e-7)
    assert pos[2] == pytest.approx(0.0, abs=1e-7)
    assert quat == (0.0, 0.0, 0.0, 1.0)


def test_long_axis_is_read_per_model_not_assumed() -> None:
    bones, cls = _rig()
    # The same rig, but this time the geometry's long axis is Z, not X -- the function must
    # not hardcode which axis is "the blade direction" (the corpus itself disagrees by sex on
    # `bushhook`: local-Y in the male file, local-Z in the female).
    positions = [(4.0, 0.0, 0.0), (4.0, 1.0, 8.0), (4.0, -1.0, 8.0)]

    pos, _ = W.trail_tip_from_geometry(bones, cls, positions)

    assert pos[2] == pytest.approx(8.0, abs=1e-7)
    assert pos[0] == pytest.approx(4.0 - 4.0, abs=1e-7)  # X sits at its own (degenerate) midpoint, 0
    assert pos[1] == pytest.approx(0.0, abs=1e-7)


def test_degenerate_mount_bind_still_produces_a_sane_offset() -> None:
    # Mirrors `test_degenerate_identity_bind_is_recorded_not_corrected`: a literal identity
    # local bind on the mount is a retail authoring defect (`w_f_bushhook.mdl`), reproduced
    # rather than corrected. The geometry bbox math must not raise or blow up over it -- only
    # the vertex positions matter, not whether the bind itself is well-formed.
    bones = with_subrig(("bush hook", None))
    bones[10] = bone(10, "bush hook", 9, pos=(0.0, 0.0, 1e-7), quat=IDENTITY_Q)
    cls = W.classify_bones(bones, {10})
    positions = [(0.0, 0.0, 0.0), (0.0, 0.0, 5.0)]

    pos, _ = W.trail_tip_from_geometry(bones, cls, positions)

    assert pos[2] == pytest.approx(5.0, abs=1e-3)


def test_no_positions_returns_the_origin() -> None:
    bones, cls = _rig()

    pos, quat = W.trail_tip_from_geometry(bones, cls, [])

    assert pos == (0.0, 0.0, 0.0)
    assert quat == (0.0, 0.0, 0.0, 1.0)


def test_wrapper_is_none_off_socket_prop() -> None:
    bones = with_subrig(("body", None), ("slide", 0))
    cls = W.classify_bones(bones, {10, 11})
    assert cls.binding == "socket_hand"

    assert W.trail_tip(b"", b"", bones, cls) is None


# NonSocketBindingTests
# The three non-socket modes need the character corpus: whether a rig is worn at all is a
# fact about the cast, not about the file.

# NonSocketBindingTests
# The three non-socket modes need the character corpus: whether a rig is worn at all is a
# fact about the cast, not about the file.

def _claws():
    # Skinned across both hands, so there is no single skinned root.
    bones = arm()
    bones.append(bone(10, "Bip01 L Hand", 5))
    bones.append(bone(11, "Bip01 R Finger1", 9))
    bones.append(bone(12, "Bip01 L Finger1", 10))
    return bones, {11, 12}


def test_unresolved_without_a_body_index() -> None:
    bones, skinned = _claws()

    assert W.classify_bones(bones, skinned).binding is None


def test_worn_and_static_is_leader_pose() -> None:
    bones, skinned = _claws()
    bodies = {"a": {"bip01 r finger1": "bip01 r hand", "bip01 l finger1": "bip01 l hand"}}

    cls = W.classify_bones(bones, skinned, bodies=bodies, animated=False)

    assert cls.binding == "leader_pose"


def test_worn_and_animated_is_copy_pose() -> None:
    bones, skinned = _claws()
    bodies = {"a": {"bip01 r finger1": "bip01 r hand", "bip01 l finger1": "bip01 l hand"}}

    cls = W.classify_bones(bones, skinned, bodies=bodies, animated=True)

    assert cls.binding == "copy_pose"


def test_no_body_can_wear_it_is_a_projectile() -> None:
    bones = [bone(0, "polySurface49", -1), bone(1, "polySurface50", 0)]
    bodies = {"a": {"bip01": "", "bip01 r hand": "bip01 r forearm"}}

    cls = W.classify_bones(bones, {1}, bodies=bodies)

    assert cls.binding == "projectile"
    assert "no_single_skinned_root" in cls.anomalies


def test_skin_confined_to_the_collapse_subtree_passes() -> None:
    bones = with_subrig(("body", None), ("slide", 0))
    cls = W.classify_bones(bones, {10, 11})

    assert W.check_subtree(bones, {10, 11}, cls).ok


def test_skin_on_a_second_matched_bone_fails_and_names_it() -> None:
    # A vertex weighted to the forearm as well as the weapon deforms between two independently
    # matched bones, so one socket cannot place it.
    bones = with_subrig(("handle", None))
    cls = W.classify_bones(bones, {10})

    result = W.check_subtree(bones, {8, 10}, cls)

    assert not result.ok
    assert result.detail == ("Bip01 R Forearm",)


def _consistent():
    """A two-bone chain whose stored inverse bind agrees with the composed one."""
    import numpy as np

    bones = [bone(0, "Bip01 R Hand", -1, pos=(1.0, 2.0, 3.0)),
             bone(1, "body", 0, pos=(4.0, 0.0, 0.0))]
    world = np.eye(4)
    for item in bones:
        local = np.eye(4)
        local[:3, :3] = mdl_skel.rot_matrix(item.quat)
        local[:3, 3] = item.pos
        world = world @ local if item.parent >= 0 else local
        item.pose_to_bone = tuple(np.linalg.inv(world)[:3, :4].reshape(-1))
    return bones


def test_self_consistent_file_passes() -> None:
    bones = _consistent()
    cls = W.classify_bones(bones, {1})

    assert W.check_collapse(bones, {1}, cls).ok


def test_inconsistent_stored_inverse_bind_fails_and_names_the_bone() -> None:
    bones = _consistent()
    bones[1].pose_to_bone = (1.0, 0.0, 0.0, 99.0,
                             0.0, 1.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 0.0)
    cls = W.classify_bones(bones, {1})

    result = W.check_collapse(bones, {1}, cls)

    assert not result.ok
    assert result.detail[0][0] == "body"


# MotionTests
# Measured across frames, not against the bind pose. A clip sitting at a constant offset from
# bind is still a rigid weapon -- `bake_pose` carries that offset -- so only frame-to-frame
# variation makes a socket wrong.

# MotionTests
# Measured across frames, not against the bind pose. A clip sitting at a constant offset from
# bind is still a rigid weapon -- `bake_pose` carries that offset -- so only frame-to-frame
# variation makes a socket wrong.

def _pose(frames, mover=None):
    out = []
    for index in range(frames):
        row = [((0.0, 0.0, 0.0), IDENTITY_Q), ((5.0, 0.0, 0.0), IDENTITY_Q)]
        if mover is not None:
            row[1] = (mover(index), IDENTITY_Q)
        out.append(row)
    return out


# BoneMotionTests
# `bone_motion`'s envelope across every local sequence, at full precision. The four
# ground-truth wield magnitudes it mirrors -- `w_m_flamethrower`'s unskinned `trigger` at
# 0.3203in/1.2535deg and `w_m_lockpick`'s skinned `lockpick` at 0.0313in/6.3565deg -- are
# measured against the real install and are not reproduced here; what is exercised is the same
# decision shape at representative magnitudes.

def _bones():
    return [bone(0, "Bip01 R Hand", -1), bone(1, "slide", 0)]


def test_sixty_one_static_frames_pass() -> None:
    assert W.frame_variance(_bones(), _pose(61), [1]) == []


def test_a_moving_sub_rig_bone_is_caught_and_named() -> None:
    pose = _pose(61, mover=lambda f: (5.0 + f * 0.1, 0.0, 0.0))

    found = W.frame_variance(_bones(), pose, [1])

    assert len(found) == 1
    assert found[0][0] == "slide"
    assert found[0][1] > 5.0


def test_rotation_alone_is_caught() -> None:
    half = math.sin(math.radians(20.0) / 2.0)
    pose = _pose(2)
    pose[1][1] = ((5.0, 0.0, 0.0), (0.0, 0.0, half, math.cos(math.radians(20.0) / 2.0)))

    found = W.frame_variance(_bones(), pose, [1])

    assert len(found) == 1
    assert found[0][2] > 19.0


def test_a_prop_bone_under_a_hand_reports_its_parent() -> None:
    bodies = {
        "a": {"handle": "bip01 r hand", "bip01 r hand": "bip01 r forearm"},
        "b": {"handle": "bip01 r hand", "bip01 r hand": "bip01 r forearm"},
    }

    scope = W.on_body_scope({}, "handle", bodies=bodies)

    assert scope["bodies"] == 2
    assert scope["parents"] == {"bip01 r hand": 2}
    assert scope["under_hand"]


def test_a_name_hanging_off_the_pelvis_is_not_under_a_hand() -> None:
    # `Box01` matches four NPC bodies but hangs from the pelvis; a boolean scope would route
    # its item families to a hip socket.
    bodies = {"swat": {"box01": "bip01 pelvis"}, "bomb_guy": {"box02": "bip01 r finger1"}}

    scope = W.on_body_scope({}, "Box01", bodies=bodies)

    assert scope["bodies"] == 1
    assert scope["parents"] == {"bip01 pelvis": 1}
    assert not scope["under_hand"]


def test_an_unknown_name_reports_no_bodies() -> None:
    scope = W.on_body_scope({}, "flamethrower", bodies={"a": {"handle": "bip01 r hand"}})

    assert scope["bodies"] == 0
    assert not scope["under_hand"]


# MaterialResolutionTests
# `_resolve_material_row`'s three outcomes: a real decode, a VMT that never resolves, and a
# texture whose bytes do not decode -- the corpus's one real case, `handleclaws`'s ``null``
# material and its 20-byte empty `.ttz`.

# MaterialResolutionTests
# `_resolve_material_row`'s three outcomes: a real decode, a VMT that never resolves, and a
# texture whose bytes do not decode -- the corpus's one real case, `handleclaws`'s ``null``
# material and its 20-byte empty `.ttz`.

def _bgr_texture(pixel=(10, 20, 30)):
    return _tth(1, 1, FMT_BGR888), zlib.compress(bytes(pixel))


def test_a_resolved_material_decodes_and_carries_its_flags() -> None:
    with tempfile.TemporaryDirectory() as root:
        install_ = _Install(Path(root))
        install_.add(
            "materials/models/weapons/claws/claws.vmt",
            '"VertexLitGeneric"\n{\n\t"$basetexture" "models/weapons/claws/claws"\n'
            '\t"$alphatest" "1"\n\t"$envmap" "env_cubemap"\n}\n',
        )
        tth, ttz = _bgr_texture()
        install_.add_bytes("materials/models/weapons/claws/claws.tth", tth)
        install_.add_bytes("materials/models/weapons/claws/claws.ttz", ttz)
        read_bytes = lambda key: install.read(install_.index, key)

        albedo, flags, failure, envmask, bump = W._resolve_material_row(
            "models/weapons/claws/claws", [""], read_bytes)

        assert albedo == "models/weapons/claws/claws"
        assert flags == frozenset({"alphatest", "envmap"})
        assert failure == ""
        # This VMT names neither $envmapmask nor $bumpmap -- the normal case.
        assert envmask == ""
        assert bump == ""


def test_a_texture_that_fails_to_decode_is_recorded_not_fatal() -> None:
    # The handleclaws real case: the VMT resolves and names a basetexture, but the .ttz is a
    # 20-byte empty file and the DXT mip it should hold is not there.
    with tempfile.TemporaryDirectory() as root:
        install_ = _Install(Path(root))
        install_.add(
            "materials/weapons/null.vmt",
            '"VertexLitGeneric"\n{\n\t"$basetexture" "weapons/null"\n\t"$alphatest" "1"\n}\n',
        )
        install_.add_bytes("materials/weapons/null.tth", _tth(4, 4, FMT_DXT1))
        install_.add_bytes("materials/weapons/null.ttz", zlib.compress(b""))
        read_bytes = lambda key: install.read(install_.index, key)

        albedo, flags, failure, _envmask, _bump = W._resolve_material_row(
            "weapons/null", [""], read_bytes)

        assert albedo == ""
        assert flags == frozenset({"alphatest"})
        assert failure != ""


def test_a_vmt_with_no_drawable_texture_is_not_a_failure() -> None:
    with tempfile.TemporaryDirectory() as root:
        install_ = _Install(Path(root))
        install_.add("materials/empty.vmt", '"UnlitGeneric"\n{\n}\n')
        read_bytes = lambda key: install.read(install_.index, key)

        assert W._resolve_material_row("empty", [""], read_bytes) == ("", frozenset(), "", "", "")


# ModelMaterialsTests
# `model_materials` over an already-loaded model: header material order, one row per
# material, and a per-row failure that never raises.

# ModelMaterialsTests
# `model_materials` over an already-loaded model: header material order, one row per
# material, and a per-row failure that never raises.

def test_materials_are_reported_in_header_order_with_per_row_failures() -> None:
    with tempfile.TemporaryDirectory() as root:
        install_ = _Install(Path(root))
        install_.add(
            "materials/claws.vmt",
            '"VertexLitGeneric"\n{\n\t"$basetexture" "claws"\n\t"$bumpmap" "claws_n"\n}\n')
        install_.add_bytes("materials/claws.tth", _tth(1, 1, FMT_BGR888))
        install_.add_bytes("materials/claws.ttz", zlib.compress(bytes((1, 2, 3))))

        with (mock.patch.object(mdl, "search_paths", return_value=[""]),
              mock.patch.object(mdl_skel, "decode_skinned",
                                return_value={"claws": {}, "handle": {}})):
            rows = W.model_materials(b"mdl", b"vtx", install_.index)

        assert [row.name for row in rows] == ["claws", "handle"]
        assert rows[0].albedo == "claws"
        assert rows[0].failure == ""
        assert rows[0].bump == "claws_n"
        assert rows[0].envmask == ""
        assert rows[1].albedo == ""
        assert rows[1].failure != ""


# SkinFamilyOverrideTests
# `skin_families` diffs every extra family against family 0 and resolves the override the
# same way a drawn material resolves -- the fire_axe ghost reskin is the one real case.

# SkinFamilyOverrideTests
# `skin_families` diffs every extra family against family 0 and resolves the override the
# same way a drawn material resolves -- the fire_axe ghost reskin is the one real case.

def test_no_extra_family_reports_nothing() -> None:
    with (mock.patch.object(mdl, "skin_families", return_value=[["a", "b"]]),
          mock.patch.object(mdl, "search_paths", return_value=[""])):
        assert W.skin_families(b"mdl", {}) == []


def test_an_extra_family_reports_the_repainted_slot_resolved() -> None:
    with tempfile.TemporaryDirectory() as root:
        install_ = _Install(Path(root))
        install_.add(
            "materials/transparent.vmt",
            '"VertexLitGeneric"\n{\n\t"$basetexture" "ghost"\n\t"$translucent" "1"\n'
            '\t"$envmapmask" "ghost_mask"\n}\n')
        install_.add_bytes("materials/ghost.tth", _tth(1, 1, FMT_BGR888))
        install_.add_bytes("materials/ghost.ttz", zlib.compress(bytes((4, 5, 6))))

        with (mock.patch.object(mdl, "search_paths", return_value=[""]),
              mock.patch.object(mdl, "skin_families",
                                return_value=[["axe", "wood"], ["transparent", "wood"]])):
            overrides = W.skin_families(b"mdl", install_.index)

        assert len(overrides) == 1
        override = overrides[0]
        assert override.family == 1
        assert override.slot == "axe"
        assert override.material == "transparent"
        assert override.albedo == "ghost"
        assert "translucent" in override.flags
        assert override.failure == ""
        assert override.envmask == "ghost_mask"
        assert override.bump == ""


def _bone_motion_bones():
    return [bone(0, "mount", -1), bone(1, "trigger", 0)]


def _quat(degrees):
    half = math.radians(degrees) / 2.0
    return (0.0, 0.0, math.sin(half), math.cos(half))


def test_motion_is_measured_from_each_sequences_own_frame_zero() -> None:
    bones = _bone_motion_bones()
    rest = ((0.0, 0.0, 0.0), IDENTITY_Q)
    moved = ((0.3203, 0.0, 0.0), _quat(1.2535))
    seq = mdl_skel.Seq(label="fire", base=0, frames=2, fps=30.0, activity="", actweight=0,
                       flags=0)

    with (mock.patch.object(mdl_skel, "local_sequences", return_value=[seq]),
          mock.patch.object(mdl_skel, "read_anim",
                            return_value=[[rest, rest], [rest, moved]]),
          mock.patch.object(mdl_skel, "decode_skinned",
                            return_value={"m": {"joints": [[0, 0, 0, 0]],
                                                 "weights": [[1.0, 0.0, 0.0, 0.0]]}})):
        rows = {row.name: row for row in W.bone_motion(b"mdl", b"vtx", bones)}

    assert rows["mount"].max_pos == 0.0
    assert rows["mount"].max_rot == 0.0
    assert rows["mount"].skinned

    assert rows["trigger"].max_pos == pytest.approx(0.3203, abs=1e-4)
    assert rows["trigger"].max_rot == pytest.approx(1.2535, abs=1e-3)
    assert not rows["trigger"].skinned


def test_the_envelope_is_the_max_across_every_sequence() -> None:
    bones = _bone_motion_bones()
    rest = ((0.0, 0.0, 0.0), IDENTITY_Q)
    small = ((0.01, 0.0, 0.0), IDENTITY_Q)
    big = ((0.5, 0.0, 0.0), IDENTITY_Q)
    seq_a = mdl_skel.Seq(label="a", base=0, frames=2, fps=30.0, activity="", actweight=0,
                         flags=0)
    seq_b = mdl_skel.Seq(label="b", base=100, frames=2, fps=30.0, activity="", actweight=0,
                         flags=0)

    def read_anim(_d, _bones, base, _frames):
        return [[rest, rest], [rest, small]] if base == 0 else [[rest, rest], [rest, big]]

    with (mock.patch.object(mdl_skel, "local_sequences", return_value=[seq_a, seq_b]),
          mock.patch.object(mdl_skel, "read_anim", side_effect=read_anim),
          mock.patch.object(mdl_skel, "decode_skinned", return_value={})):
        rows = {row.name: row for row in W.bone_motion(b"mdl", b"vtx", bones)}

    assert rows["trigger"].max_pos == 0.5
    assert not rows["mount"].skinned
