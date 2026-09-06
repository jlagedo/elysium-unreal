"""Bounded publication/retention gates; no animation sample evaluation or Unreal."""
import hashlib
import json
import os
from pathlib import Path
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


@pytest.mark.skipif(not os.environ.get("ELYSIUM_ANIMATION_PUBLICATION_AUDIT"), reason="explicit two-bank read-only retention audit")
def test_current_224_declarations_are_intermediates_with_source_and_native_coverage():
    from elysium_pipeline.formats.unit_contract.container import read_document
    lane = Path(r"E:\elysium-work\_r8_explore\agents\physics")
    manifest_path = Path(r"E:\elysium-work\import\characters\manifest.json")
    raw = manifest_path.read_bytes()
    sha = hashlib.sha256(raw).hexdigest()
    manifest = json.loads(raw)
    baseline = json.loads((lane / "animation_publication_missing_packages.json").read_bytes())
    # This frozen inventory records the reported 224; never replace it with this audit.
    candidates = [p for p in baseline["missingPackages"] if p.rsplit("/", 1)[-1].startswith("A_")]
    assert len(candidates) == 224
    report = publications.audit_missing(manifest_path, sha, candidates)
    assert len(report["nonProducts"]) == 224 and not report["unresolvedPackages"]
    assert len(report["owners"]) == 2 and len(report["requiredPackages"]) == 110
    for package_path in report["requiredPackages"]:
        file = Path(r"E:\dev\elysium-unreal\Plugins\ElysiumBaked\Content") / (package_path.removeprefix("/ElysiumBaked/") + ".uasset")
        assert file.is_file(), package_path
    verification = json.loads((manifest_path.parent / "native_verify_report.json").read_bytes())
    assert verification["manifestSha256"] == sha and not verification["failed"]
    for owner in report["owners"]:
        assert owner["assetId"] in manifest["selectedUnits"]
        glb = Path(r"E:\elysium-work\exports_v2") / owner["unitGlb"]
        with glb.open("rb") as stream:
            assert hashlib.file_digest(stream, "sha256").hexdigest() == owner["unitSha256"]
        mdl = read_document(glb)["extensions"]["ELYSIUM_vtmb_model"]["mdl"]
        source = json.loads((manifest_path.parent / owner["body"]).read_bytes())
        assert mdl == source["sourceSemantics"]["mdl"]
    report["nativeVerification"] = {"manifestSha256": sha, "failed": [], "selectedBodyDataCount": verification["bodyData"],
                                    "scope": "two affected owners included in fresh selected BodyData verification"}
    report["sourceVerification"] = "both complete GLB MDL tables equal hash-checked staged sourceSemantics.mdl"
    (lane / "animation_publication_audit.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"224 false declarations; 110 raw assets present; 2 complete source tables retained; manifest {sha}")
