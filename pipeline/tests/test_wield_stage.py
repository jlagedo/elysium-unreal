from types import SimpleNamespace
import math

import numpy as np
import pytest

from elysium_pipeline.formats import mdl_skel
from elysium_pipeline.skeletal_stage import payload, wield


def test_glb_wield_uses_clip_reference_pose_and_preserves_rigid_trail_decision():
    bones = []
    for i, (name, parent, local_x, world_x) in enumerate([
            ("Bip01", -1, 0., 0.), ("Bip01 R Hand", 0, 10., 10.), ("Bat", 1, 2., 12.)]):
        bones.append(mdl_skel.Bone(index=i, name=name, parent=parent, flags=0,
            pos=(local_x, 0., 0.), quat=(0., 0., 0., 1.),
            pose_to_bone=(1., 0., 0., -world_x, 0., 1., 0., 0., 0., 0., 1., 0.)))
    clip = mdl_skel.Seq("idle", 0, 2, 30., "", 0, 0)
    pose = [(b.pos, b.quat) for b in bones]
    pose[2] = ((3., 0., 0.), (0., 0., math.sqrt(.5), math.sqrt(.5)))
    attributes = {1: np.array([[2, 0, 0, 0]] * 3), 2: np.array([[1., 0., 0., 0.]] * 3),
                  3: np.array([[0], [1], [2]])}
    primitive = {"extensions": {"ELYSIUM_vtmb_model": {"sourceVertices": [0, 1, 2], "sourcePositions": {}}},
                 "attributes": {"JOINTS_0": 1, "WEIGHTS_0": 2}, "indices": 3}
    unit = SimpleNamespace(id="vtmb:model:weapons/test", bones=bones, sequences=[clip],
        extension={"vtx": {"lods": [{"index": 0, "mesh": 0}]}},
        document={"meshes": [{"primitives": [primitive]}]},
        accessor=lambda i: attributes[i],
        precise=lambda record, shape: np.array([[12., 0., 0.], [16., 0., 0.], [12., 1., 0.]]),
        animation_frames=lambda base, frames: [pose] * frames)
    rows, bone_map, reparented = payload.unreal_bones(bones)
    result = wield.describe(unit, {"body": {"bat": "bip01 r hand"}}, rows, bone_map, reparented)
    assert result["binding"] == "socket_prop"
    assert result["referencePoseSource"] == "clip" and result["referenceClip"] == "idle"
    assert result["referencePose"][2]["position"] == [3. * 2.54, 0., 0.]
    assert result["trailTipSource"]["position"] == [4., .5, 0.]
    assert result["onBody"] == {"bodies": 1, "parents": {"bip01 r hand": 1}, "under_hand": True}
    assert all(check["ok"] for check in result["checks"].values())


@pytest.mark.parametrize("resolved", [False, True])
def test_authored_absence_is_distinct_from_a_missing_export(tmp_path, resolved):
    from elysium_pipeline.formats.unit_contract.container import encode_glb
    fields = {key: {"value": value} for key, value in (
        ("animPrefix", ""), ("showsViewModel", True), ("cameraClass", ""),
        ("bitFlagCantBeLast", False), ("bitFlagDisciplineTgt", False), ("reloadSingle", False))}
    fields["models"] = {"wieldmodel_" + sex: {"raw": "models/error.mdl", "asset": "vtmb:model:error", "resolved": resolved}
                        for sex in ("f", "m")}
    root = {"identity": {"asset": "vtmb:vdata:items/weapon"},
            "projection": {"rootKey": "WeaponData", "fields": fields}}
    path = tmp_path / "vdata/items/weapon.glb"
    path.parent.mkdir(parents=True)
    path.write_bytes(encode_glb({"extensions": {"ELYSIUM_vtmb_vdata": root}}))
    result = wield.discover(tmp_path, {})
    assert result["items"][0]["models"]["f"]["kind"] == "absent"
    assert len(result["failures"]) == (2 if resolved else 0)
    assert len(result["sourceGaps"]) == (0 if resolved else 2)
