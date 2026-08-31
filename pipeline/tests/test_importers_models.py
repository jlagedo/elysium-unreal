"""Contract tests for `uv run elysium import models` (the offline stage phase, R1.3).

Most fixtures are synthetic model units built directly as an `ELYSIUM_vtmb_model` extension body
plus the minimal outer glTF core it needs (`meshes`, `accessors`, `nodes`) and encoded with
`encode_glb` -- never a real staging tree -- mirroring `test_materials_stage.py`'s own pattern.
One test (`test_map_scoped_selection_matches_real_corpus`) walks the real export corpus, gated the
way `test_unit_divergences_key_every_real_unit_and_are_actually_used` gates itself: skipped
outright when `ELYSIUM_EXPORT_V2_ROOT` is not configured or the corpus is not exported locally.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from elysium_pipeline import paths
from elysium_pipeline.formats.unit_contract.container import encode_glb
from elysium_pipeline.importers import models as importer

MODEL_EXTENSION = importer.MODEL_EXTENSION


def _slot(slot: int, source_name: str, material: str, *, surface_property: str = "") -> dict:
    return {
        "slot": slot, "sourceName": source_name,
        "sourcePath": f"materials/models/x/{source_name}.vmt",
        "material": material, "resolved": True, "surfaceProperty": surface_property,
    }


def _primitive(*, indices: int, model: int, mesh: int, skin_reference: int, body_part: int = 0) -> dict:
    return {
        "attributes": {"POSITION": 0},
        "indices": indices,
        "extensions": {MODEL_EXTENSION: {
            "bodyPart": body_part, "model": model, "mesh": mesh, "skinReference": skin_reference,
        }},
    }


def _model_document(
    key: str, *,
    shape: str = "static",
    slots: list[dict] | None = None,
    skin_families: list[list[str]] | None = None,
    body_parts: list[dict] | None = None,
    physics: dict | None = "default",
    node0: dict | None = None,
) -> dict:
    """One synthetic unit: a single LOD, a single submodel-0 primitive per skin-table slot."""

    if slots is None:
        slots = [_slot(0, "brick", "vtmb:material:models/x/brick")]
    if skin_families is None:
        skin_families = [[row["material"] for row in slots]]
    if body_parts is None:
        body_parts = [{"index": 0, "models": [{"index": 0}]}]
    if physics == "default":
        physics = {
            "solids": [{
                "properties": {"mass": 12.0, "surfaceprop": "wood"},
                "hulls": [{"sourceOffset": 0}],
            }],
        }

    primitives = [
        _primitive(indices=index, model=0, mesh=0, skin_reference=row["slot"])
        for index, row in enumerate(slots)
    ]
    extension = {
        "schemaVersion": "2.0.0",
        "identity": {
            "asset": f"vtmb:model:{key}", "modelPath": f"models/{key}.mdl",
            "family": key.split("/")[0], "shape": shape, "roles": ["placed-prop"],
        },
        "sourceResolution": {"members": [
            {"role": "mdl", "sha256": "a" * 64}, {"role": "vtx-dx80", "sha256": "b" * 64},
        ]},
        "mdl": {
            "header": {
                "hullMin": [-1.0, -1.0, -1.0], "hullMax": [1.0, 1.0, 1.0],
                "surfaceProperty": "", "flags": {"staticProp": shape == "static"},
            },
            "bodyParts": body_parts,
        },
        "vtx": {"lods": [{"index": 0, "mesh": 0, "switchPoints": [0.0], "primitiveCount": len(primitives)}]},
        "physics": physics,
        "materialBindings": {"slots": slots, "skinFamilies": skin_families},
        "dependencies": [], "anomalies": [], "omissions": [],
    }
    if node0 is None:
        node0 = {"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0]}
    return {
        "asset": {"version": "2.0"},
        "extensionsUsed": [MODEL_EXTENSION],
        "extensions": {MODEL_EXTENSION: extension},
        "nodes": [node0, {"mesh": 0}],
        "meshes": [{"primitives": primitives}],
        "accessors": [{"count": 300} for _ in primitives],
    }


def _material_index(material_ids: list[str], *, blend_mode: str = "Opaque",
                     master: str = "/Game/ElysiumGenerated/Materials/V2/M_V2_Lit") -> dict[str, dict]:
    return {
        material_id: {
            "assetPath": "/ElysiumBaked/Materials/" + material_id[len("vtmb:material:"):],
            "blendMode": blend_mode, "master": master,
        }
        for material_id in material_ids
    }


# --- duplicate slot-name folding ------------------------------------------------------------------


def test_duplicate_slot_name_folds_by_table_order_not_geometry_use():
    """Two skin-table columns author the same studio texture name (`null` twice); only the first
    is ever drawn by a primitive. Occurrence 0 keeps the name verbatim, occurrence 1 gets the
    `_<slot index>` suffix -- in the skin-table's own column order, not the Unreal slot list's."""

    slots = [
        _slot(0, "bed_col1", "vtmb:material:models/x/bed_col1"),
        _slot(1, "null", "vtmb:material:models/x/null"),
        _slot(2, "null", "vtmb:material:models/x/null"),
    ]
    # Only columns 0 and 1 are ever drawn; column 2 ("null" again) is skin-table-only, exactly the
    # `fancybed`/`fancybed2` corpus shape.
    document = _model_document(
        "scenery/fancybed", slots=slots[:2],
        skin_families=[[row["material"] for row in slots]],
    )
    document["extensions"][MODEL_EXTENSION]["materialBindings"]["slots"] = slots

    material_index = _material_index([row["material"] for row in slots])
    entry, provenance = importer.stage_unit(
        "scenery/fancybed", document, "0" * 64, material_index=material_index,
    )

    drawn_names = {row["skinReference"]: row["slotName"] for row in entry["slots"]}
    assert drawn_names == {0: "bed_col1", 1: "null"}

    full_table_names = entry["skinFamilies"][0]["slots"]
    assert full_table_names == ["bed_col1", "null", "null_2"]

    kinds = [row["kind"] for row in provenance["anomalies"]]
    assert "duplicateSlotName" in kinds
    duplicate_row = next(r for r in provenance["anomalies"] if r["kind"] == "duplicateSlotName")
    assert duplicate_row["names"] == ["null"]


# --- sentinel slot ---------------------------------------------------------------------------------


def test_sentinel_slot_binds_placeholder_and_records_anomaly():
    slots = [_slot(0, "missingtex", "vtmb:missing-material:0:missingtex")]
    document = _model_document("scenery/sentinel", slots=slots, skin_families=[[slots[0]["material"]]])

    entry, provenance = importer.stage_unit(
        "scenery/sentinel", document, "0" * 64, material_index={},
    )

    assert entry["slots"][0]["isSentinel"] is True
    assert entry["slots"][0]["materialAsset"] == importer.MI_V2_MISSING
    assert entry["skinFamilies"][0]["materials"] == [importer.MI_V2_MISSING]
    assert any(row["kind"] == "missingMaterialSentinel" for row in provenance["anomalies"])
    # A sentinel is never a stage failure, and it never vetoes Nanite.
    assert entry["nanite"] is True


# --- skin families / FamilyCount -------------------------------------------------------------------


def test_skin_families_carry_family_count_for_the_runtime_clamp():
    slots = [_slot(0, "tankwht", "vtmb:material:models/x/tankwht_a")]
    families = [
        ["vtmb:material:models/x/tankwht_a"],
        ["vtmb:material:models/x/tankwht_b"],
        ["vtmb:material:models/x/tankwht_c"],
    ]
    document = _model_document("scenery/skinned", slots=slots, skin_families=families)
    material_index = _material_index(
        ["vtmb:material:models/x/tankwht_a", "vtmb:material:models/x/tankwht_b",
         "vtmb:material:models/x/tankwht_c"]
    )

    entry, provenance = importer.stage_unit(
        "scenery/skinned", document, "0" * 64, material_index=material_index,
    )

    assert entry["familyCount"] == 3
    assert provenance["familyCount"] == 3
    assert [row["family"] for row in entry["skinFamilies"]] == [0, 1, 2]
    assert entry["skinFamilies"][1]["materials"][0].endswith("tankwht_b")


# --- loud failures -----------------------------------------------------------------------------


def test_slot_with_no_material_id_and_no_sentinel_fails_loudly():
    """"A slot with no material id *and* no sentinel is a stage failure, because that combination
    means the export itself is incomplete." """

    slots = [{"slot": 0, "sourceName": "brick", "sourcePath": "materials/x.vmt",
              "material": None, "resolved": False, "surfaceProperty": ""}]
    document = _model_document("scenery/broken", slots=slots, skin_families=[[None]])

    with pytest.raises(importer.ModelImportError):
        importer.stage_unit("scenery/broken", document, "0" * 64, material_index={})


def test_no_admitted_vtx_topology_skips_rather_than_fails():
    """Body parts declared, no `meshes` published -- "skip the unit loudly and name it", never a
    hard stage failure."""

    document = _model_document("weapons/w_null")
    document["meshes"] = []
    document["extensions"][MODEL_EXTENSION]["vtx"]["lods"] = []

    with pytest.raises(importer.ModelSkip):
        importer.stage_unit("weapons/w_null", document, "0" * 64, material_index={})


def test_joint0_not_identity_fails_static_shape_unit():
    document = _model_document(
        "scenery/tilted", node0={"translation": [10.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0]},
    )
    material_index = _material_index(["vtmb:material:models/x/brick"])

    with pytest.raises(importer.ModelImportError, match="joint 0"):
        importer.stage_unit("scenery/tilted", document, "0" * 64, material_index=material_index)


# --- map-scoped selection ---------------------------------------------------------------------------


def test_map_scoped_selection_matches_real_corpus():
    """`--maps sp_tutorial_1 sm_pawnshop_1 sm_hub_1` resolves the exact referenced set the design
    pins (`seam_map_model.md` -> "Import" -> "Scope and selection"): 242 / 95 / 178, 414 union."""

    try:
        export_v2_root = paths.export_v2_root()
    except Exception:
        pytest.skip("no export_v2 root configured")
    if not (Path(export_v2_root) / "maps").is_dir():
        pytest.skip("map corpus not exported locally")

    selection = importer.select_for_maps(
        export_v2_root, ["sp_tutorial_1", "sm_pawnshop_1", "sm_hub_1"],
    )
    assert len(selection["perMap"]["sp_tutorial_1"]) == 242
    assert len(selection["perMap"]["sm_pawnshop_1"]) == 95
    assert len(selection["perMap"]["sm_hub_1"]) == 178
    assert len(selection["keys"]) == 414


def test_stage_refuses_to_run_unscoped(tmp_path):
    with pytest.raises(importer.ModelImportError, match="unscoped"):
        importer.stage_models(tmp_path / "export", tmp_path / "stage")
