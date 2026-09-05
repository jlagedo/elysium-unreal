from collections import OrderedDict
import struct
import hashlib

import pytest

from elysium_pipeline.validation.skeletal_projection import assemble, compare_legacy_projection
from elysium_pipeline.validation.skeletal_diff import SkeletalDiffError


def text(value):
    raw = value.encode()
    return struct.pack("<I", len(raw)) + raw


def clip(name, mask, position=1.):
    return (text(name) + text("") + struct.pack("<IfIiI", 1, 30., 0, mask, 1)
            + struct.pack("<IBB7f", 0, 1, 1, position, 0., 0., 0., 0., 0., 1.))


def payload(clips, masks):
    parts = OrderedDict()
    if masks:
        parts["MASK"] = struct.pack("<I", len(masks)) + b"".join(struct.pack("<I", len(m)) + m for m in masks)
    parts["ANIM"] = struct.pack("<I", len(clips)) + b"".join(clips)
    return assemble(parts)


def test_expanded_clips_preserve_every_legacy_clip_and_mask():
    before = payload([clip("idle", 0), clip("use", 1)], [b"\x01\x00", b"\x00\x01"])
    after = payload([clip("extra", 0), clip("idle", 1), clip("use", 2)],
                    [b"\x01\x01", b"\x01\x00", b"\x00\x01"])
    result = compare_legacy_projection(before, after, clip_mode="required")
    assert result.passed
    assert result.divergences == [{"class": "additional-source-clips", "legacyMode": "required", "labels": ["extra"]}]
    changed = payload([clip("extra", 0), clip("idle", 1, position=5.), clip("use", 2)],
                      [b"\x01\x01", b"\x01\x00", b"\x00\x01"])
    assert not compare_legacy_projection(before, changed, clip_mode="required").passed


@pytest.mark.parametrize("new", [[clip("idle", -1)], [clip("use", -1), clip("idle", -1)]])
def test_extra_clip_policy_cannot_hide_missing_or_reordered_legacy_clips(new):
    before = payload([clip("idle", -1), clip("use", -1)], [])
    with pytest.raises(SkeletalDiffError, match="missing or reordered"):
        compare_legacy_projection(before, payload(new, []), clip_mode="rest")


def test_full_clip_mode_does_not_accept_unexplained_additions():
    before = payload([clip("idle", -1)], [])
    after = payload([clip("idle", -1), clip("extra", -1)], [])
    assert not compare_legacy_projection(before, after).passed


def source_fixture(tmp_path, *, morph=False, compact=False):
    from pipeline.tests.test_model_glb import _unit
    from elysium_pipeline.exporters.model_glb import build_document
    from elysium_pipeline.formats.unit_contract.container import write_glb
    _, unit = _unit()
    primitive = unit.lods[0]["primitives"][0]
    primitive["positions"] = [(0., 0., 0.), (1., 0., 0.), (0., 1., 0.)]
    primitive["sourceNormals"] = primitive["normals"] = [(1., 0., 0.)] * 3
    if compact:
        unit.body_parts[0]["models"][0]["vertexListType"] = 1
    if morph:
        unit.facial["morphTargets"] = [{"index": 0, "name": "smile", "flexDescription": 0, "targets": [0., 1., 1., 2.]}]
        primitive["morphRecords"] = [{"target": 0, "vertex": 1, "sourceVertex": 1,
                                      "position": [1., 2., 3.], "normal": [0., 0., 0.]}]
    path = tmp_path / "source.glb"
    document, binary = build_document(unit)
    write_glb(document, binary, path)
    return path, hashlib.sha256(path.read_bytes()).hexdigest(), unit.textures[0]["name"]


def mesh(name, normal, *, uv=0.):
    data = struct.pack("<III", 3, 1, 1) + text(name) + struct.pack("<II", 0, 1)
    for position in [(0., 0., 0.), (2.54, 0., 0.), (0., -2.54, 0.)]:
        data += struct.pack("<8f4H4f", *position, *normal, uv, 0., 0, 0, 0, 0, 1., 0., 0., 0.)
    return data + struct.pack("<3I", 0, 2, 1)


def test_packed_normal_restoration_is_source_checked_and_other_mesh_fields_still_fail(tmp_path):
    source, digest, name = source_fixture(tmp_path, compact=True)
    before = assemble({"MESH": mesh(name, (0., 0., -1.))})
    after = assemble({"MESH": mesh(name, (1., 0., 0.))})
    result = compare_legacy_projection(before, after, source_path=source, source_sha=digest)
    assert result.passed
    assert result.divergences[0]["class"] == "restored-authored-compact-normals"
    bad_uv = assemble({"MESH": mesh(name, (1., 0., 0.), uv=1.)})
    assert not compare_legacy_projection(before, bad_uv, source_path=source, source_sha=digest).passed
    bad_normal = assemble({"MESH": mesh(name, (0., 1., 0.))})
    assert not compare_legacy_projection(before, bad_normal, source_path=source, source_sha=digest).passed


def test_added_morphs_must_reproduce_the_glb_source_records(tmp_path):
    source, digest, name = source_fixture(tmp_path, morph=True)
    geometry = mesh(name, (1., 0., 0.))
    before = assemble({"MESH": geometry})
    morph = struct.pack("<I", 1) + text("smile") + struct.pack("<II6f", 1, 1, 2.54, -5.08, 7.62, 0., 0., 0.)
    result = compare_legacy_projection(before, assemble({"MESH": geometry, "MORF": morph}), source_path=source, source_sha=digest)
    assert result.passed
    assert result.divergences[0]["class"] == "restored-source-morphs"
    bad = morph[:-4] + struct.pack("<f", 1.)
    assert not compare_legacy_projection(before, assemble({"MESH": geometry, "MORF": bad}), source_path=source, source_sha=digest).passed
