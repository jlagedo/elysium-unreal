"""Bounded publication/retention gates; no animation sample evaluation or Unreal."""
import hashlib
import json
import struct

import pytest

from elysium_pipeline import animation_publications as publications, cook_roots as roots
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.formats import eskm


ID = "vtmb:model:character/test/bank"


def package(label):
    return baked_unit(ID, "A", label=label)


def _record(name, base, flags):
    def string(value):
        raw = value.encode()
        return struct.pack("<I", len(raw)) + raw
    return (string(name) + string(base) + struct.pack("<IfIiI", 1, 30., flags, -1, 1)
            + struct.pack("<IBB3f4f", 0, 1, 1, 0., 0., 0., 0., 0., 0., 1.))


def fixture(root, *, referenced=False, derived_flags=4, metadata=True):
    def write(name, value):
        raw = value if isinstance(value, bytes) else json.dumps(value).encode()
        (root / name).write_bytes(raw)
        return hashlib.sha256(raw).hexdigest()
    records = _record("delta", "", 4) + _record("host", "", 0) + _record("delta@host", "host", derived_flags)
    anim = struct.pack("<I", 3) + records
    payload = struct.pack("<4sIII4sQQ", b"ESKM", eskm.VERSION, 1, 0, b"ANIM", 36, len(anim)) + anim
    desc = [{"label": "delta", "flags": 4, "events": [{"event": 123, "options": "retained"}]},
            {"label": "host", "flags": 0, "autolayers": ["delta"]}]
    source = {"assetId": ID, "sequences": desc, "sourceSemantics": {"mdl": {"sequences": desc}}}
    meta = {"assetId": ID, "ownerRoot": "", "clips": {"delta": {
        "sourceLabel": "delta", "slice": {"clips": {"delta": [[0, 0, 1, 4]]}},
        "timelines": {"events": {"delta": [{"event": 123, "options": "retained"}]}}}}}
    if not metadata:
        meta["clips"] = {}
    body = {"assetId": ID, "ownerRoot": "", "nativeSequences": {"delta": roots.object_path(package("delta"))},
            "sequences": [{"label": "host", "layers": [{"sequence": roots.object_path(
                package("delta@host" if referenced else "delta"))}]}]}
    entry = {"assetId": ID, "payload": "bank.skel", "unitGlb": "models/character/test/bank.glb",
             "body": "body.json", "clipData": "clips.json", "bodyData": "vocabulary.json",
             "animationAssets": [package(name) for name in ("delta", "host", "delta@host")],
             "recipe": {"payloadSha256": write("bank.skel", payload), "bodySha256": write("body.json", source),
                        "unitSha256": "0" * 64},
             "clipDataSha256": write("clips.json", meta), "bodyDataSha256": write("vocabulary.json", body)}
    sha = write("manifest.json", {"assets": [entry]})
    return root / "manifest.json", sha


def test_only_derived_additive_is_nonproduct_and_complete_metadata_is_retained(tmp_path):
    path, sha = fixture(tmp_path)
    candidates = [package(name) for name in ("delta", "host", "delta@host", "unknown")]
    report = publications.audit_missing(path, sha, candidates)
    assert [r["packagePath"] for r in report["nonProducts"]] == [package("delta@host")]
    assert report["requiredPackages"] == [package("delta")]
    assert set(report["unresolvedPackages"]) == set(candidates) - {package("delta@host")}
    owner = report["owners"][0]
    assert (owner["stagedRecords"], owner["nativeRecords"]) == (3, 2)
    assert owner["sourceDescriptors"]["delta"]["events"][0]["options"] == "retained"
    assert owner["nativeMetadata"]["delta"]["timelines"]["events"]["delta"][0]["event"] == 123


@pytest.mark.parametrize("settings", [{"referenced": True}, {"derived_flags": 0}, {"metadata": False}])
def test_required_reference_nondelta_overlay_or_missing_metadata_cannot_be_excluded(tmp_path, settings):
    path, sha = fixture(tmp_path, **settings)
    report = publications.audit_missing(path, sha, [package("delta@host")])
    assert not report["nonProducts"]
    assert report["unresolvedPackages"] == [package("delta@host")]


@pytest.mark.parametrize("name", ["manifest.json", "bank.skel", "body.json", "clips.json", "vocabulary.json"])
def test_changed_retention_evidence_fails_closed(tmp_path, name):
    path, sha = fixture(tmp_path)
    with (tmp_path / name).open("ab") as stream:
        stream.write(b" ")
    with pytest.raises(ValueError, match="stale animation evidence"):
        publications.audit_missing(path, sha, [package("delta@host")])


def test_cook_reconciliation_keeps_raw_required_and_already_published_intermediates(tmp_path):
    path, sha = fixture(tmp_path)
    source = {"expectedPackages": [*roots.GLOBALS, package("delta@host")],
              "inputs": [{"kind": "characters", "path": str(path), "sha256": sha}],
              "sourceDecisions": {}, "catalogueManifestSupplied": True}
    published = [{"packagePath": p, "objectPath": roots.object_path(p), "className": kind,
                  "producer": "test", "recipe": "test"} for p, kind in roots.GLOBALS.items()]
    consumer = dict(source, referencePackages=[package("delta@host")])
    assert roots.reconcile_declarations(consumer, published) == consumer
    assert package("delta@host") in roots.plan_roots(consumer, published)["missingPackages"]
    cooked = roots.reconcile_declarations(source, published)
    assert package("delta@host") not in cooked["expectedPackages"]
    assert package("delta") in cooked["expectedPackages"]
    assert roots.plan_roots(cooked, published)["missingPackages"] == [package("delta")]
    published.append({"packagePath": package("delta"), "objectPath": roots.object_path(package("delta")),
                      "className": "AnimSequence", "producer": "characters", "recipe": "test"})
    assert roots.plan_roots(cooked, published)["readyToPublish"]
    published.append({"packagePath": package("delta@host"), "objectPath": roots.object_path(package("delta@host")),
                      "className": "AnimSequence", "producer": "characters", "recipe": "test"})
    assert package("delta@host") in roots.plan_roots(cooked, published)["extraPublishedPackages"]
    roots.verify_inputs(cooked)
    (tmp_path / "bank.skel").write_bytes(b"changed")
    with pytest.raises(roots.CookRootError, match="stale"):
        roots.verify_inputs(cooked)
