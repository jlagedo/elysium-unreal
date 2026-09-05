"""Independent comparison catches topology, mask, timeline and ordered JSON loss."""
import struct

import pytest

from elysium_pipeline.validation.skeletal_diff import compare_payloads, compare_json, sections, SkeletalDiffError


def container(tag, data):
    return struct.pack("<4sIII4sQQ", b"ESKM", 8, 1, 0, tag, 36, len(data)) + data


def string(value):
    raw = value.encode()
    return struct.pack("<I", len(raw)) + raw


def skeleton(position=1.):
    return container(b"SKEL", struct.pack("<I", 1) + string("root") +
                     struct.pack("<i7f", -1, position, 0., 0., 0., 0., 0., 1.))


def test_bounded_float_round_trip_is_named_but_large_change_fails():
    result = compare_payloads(skeleton(), skeleton(1.000001))
    assert result.passed
    assert result.divergences[0]["class"] == "float32-basis-roundtrip"
    assert not compare_payloads(skeleton(), skeleton(1.01)).passed


def test_nan_is_never_accepted_as_a_round_trip():
    assert not compare_payloads(skeleton(), skeleton(float("nan"))).passed


def test_masks_cannot_change_ownership():
    a = container(b"MASK", struct.pack("<II", 1, 2) + b"\x01\x00")
    b = container(b"MASK", struct.pack("<II", 1, 2) + b"\x00\x01")
    assert not compare_payloads(a, b).passed


def test_directory_rejects_truncation_and_unclaimed_bytes():
    for data in (skeleton()[:-1], skeleton() + b"lost", b"bad"):
        with pytest.raises(SkeletalDiffError):
            sections(data)


def test_json_reports_order_and_does_not_ignore_absent_fields():
    assert compare_json({"clips": ["a", "b"]}, {"clips": ["b", "a"]})
    assert compare_json({"movement": []}, {})
    assert compare_json({"flags": 1}, {"flags": True})
    assert not compare_json({"a": 1, "b": 2}, {"b": 2, "a": 1})
