"""The model provenance sidecar's key set, pinned on both sides of the seam (R1.4).

`UElysiumModelProvenance::FromJson` reads the sidecar `importers/models.py::stage_unit` writes.
Nothing but a test makes those two agree: the material lane shipped a reader composed to the
contract's prose rather than to the stage's output, and ~60% of its fields read back empty on real
assets before anyone noticed. So the key set lives in one tracked fixture,
`fixtures/model_provenance_keys.json`, this module pins the stage against it, and the C++ reader's
own automation fixture (`Source/ElysiumUE/Private/Tests/ElysiumModelProvenanceTests.cpp`) is
composed from the same file and cites it. A key added on either side without the other fails here.
"""

from __future__ import annotations

import json
from pathlib import Path

from elysium_pipeline.importers import models as importer

from test_importers_models import _material_index, _model_document

FIXTURE = Path(__file__).with_name("fixtures") / "model_provenance_keys.json"


def _stage(key: str, **kwargs) -> dict:
    document = _model_document(key, **kwargs)
    _entry, provenance = importer.stage_unit(
        key, document, "c" * 64,
        material_index=_material_index(
            [row["material"] for row in
             document["extensions"][importer.MODEL_EXTENSION]["materialBindings"]["slots"]]),
        model_settings=(1.0, 0.001, 0.9),
    )
    return provenance


def test_the_sidecar_key_set_is_exactly_the_pinned_one():
    """Every key the stage writes, at every level the C++ reader descends to.

    Two units between them cover every branch: one with a `.phy` (mass, surface property, a second
    skin family) and one without (the `hullBounds` box and this lane's own omission row).
    """
    pinned = json.loads(FIXTURE.read_text(encoding="utf-8"))

    with_phy = _stage(
        "scenery/props/crate/crate",
        slots=[{"slot": 0, "sourceName": "cratewood",
                "sourcePath": "materials/models/x/cratewood.vmt",
                "material": "vtmb:material:models/x/cratewood", "resolved": True,
                "surfaceProperty": "wood"}],
        skin_families=[["vtmb:material:models/x/cratewood"],
                       ["vtmb:material:models/x/cratewood"]],
    )
    without_phy = _stage("scenery/props/sign/sign", physics=None)

    assert sorted(with_phy) == sorted(pinned["topLevel"])
    assert sorted(without_phy) == sorted(pinned["topLevel"])
    # The editor import adds exactly one key of its own before `apply_json`; it must not already
    # be a sidecar key, or the two would silently fight over it.
    assert not set(pinned["editorAdded"]) & set(with_phy)

    assert with_phy["slots"] and len(with_phy["skinFamilies"]) == 2
    for row in with_phy["slots"]:
        assert sorted(row) == sorted(pinned["slot"])
    for row in with_phy["skinFamilies"]:
        assert sorted(row) == sorted(pinned["skinFamily"])
    for row in with_phy["lods"]:
        assert sorted(row) == sorted(pinned["lod"])
    for row in with_phy["sourceSha256"]:
        assert sorted(row) == sorted(pinned["sourceSha256Row"])
    assert sorted(without_phy["hullBounds"]) == sorted(pinned["hullBounds"])

    # The stage merges its own rows (`kind`) with the unit's own export rows (`row`/`reason`)
    # verbatim, so the reader takes the first spelling present. A third spelling would read back
    # as an empty `Kind`/`Reason` and roll up under "" in the report.
    labels = pinned["rowLabelKeys"]
    assert without_phy["omissions"], "a unit with no .phy records the box-fallback omission"
    for name, keys in (("anomalies", labels["anomalies"]), ("omissions", labels["omissions"])):
        for provenance in (with_phy, without_phy):
            for row in provenance[name]:
                assert set(row) & set(keys), (name, row)
