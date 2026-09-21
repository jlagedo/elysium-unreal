"""The decal stage (0018 story 21-4): one staged projector row per baked `ADecalActor`.

The synthetic cases pin the two pure pieces of the projector search -- the convex distance test
that decides whether a decal's origin lands on a face, and the material reader that sizes its
quad from the published units -- plus the staging lane's refusals. The corpus cases (skipped
without a V2 export root) pin the six 21-4 maps' placed counts and re-derive the whole `.decals`
byte stream for each, which is the comparison the story's "diff before delete" rule is built on:
the port matches the decoder, and where it cannot the difference is named.

Two contracts are checked together on purpose, as `test_map_ropes.py` checks them: `decal_rows`
is the producer's derivation and `decal_line` is the `.decals` sidecar's byte format. The sidecar
is an offline intermediate now, so the only thing keeping the two honest is that both are written
from the same rows.

The editor half -- `_staged_decal` and `_place_decals` -- is `test_bake_map_decals.py`, which
needs a fake `unreal`.
"""
from __future__ import annotations

import numpy as np
import pytest

from elysium_pipeline import paths
from elysium_pipeline.exporters import UE_map_sidecars as producer
from elysium_pipeline.importers import map_decals as decals


def _row(**overrides):
    row = {
        "index": 0,
        "materialId": "vtmb:material:decals/stains/rusta",
        "face": 7,
        "locCm": [1.0, 2.0, 3.0],
        "normal": [1.0, 0.0, 0.0],
        "sDir": [0.0, 1.0, 0.0],
        "tDir": [0.0, 0.0, 1.0],
        "halfWCm": 20.32,
        "halfHCm": 81.28,
    }
    row.update(overrides)
    return row


# --------------------------------------------------------------------------- the face test


def _square(size=10.0):
    return np.asarray([[0.0, 0.0], [size, 0.0], [size, size], [0.0, size]])


def test_a_point_inside_a_face_is_at_distance_zero():
    assert producer._inplane(_square(), np.asarray([5.0, 5.0])) == 0.0


def test_either_winding_counts_as_inside():
    """A BSP face's winding is authored, not normalised -- the legacy test accepts both, because a
    decal on a clockwise face must bind exactly as one on a counter-clockwise face does."""

    reversed_square = _square()[::-1]
    assert producer._inplane(reversed_square, np.asarray([5.0, 5.0])) == 0.0


def test_a_point_outside_reports_its_distance_to_the_nearest_edge():
    assert producer._inplane(_square(), np.asarray([15.0, 5.0])) == pytest.approx(5.0)
    assert producer._inplane(_square(), np.asarray([-3.0, 5.0])) == pytest.approx(3.0)


def test_the_half_unit_seam_tolerance_keeps_an_edge_decal_bound():
    """The legacy test calls a point inside when every cross product clears -0.5, so a decal on a
    shared edge binds one of the two faces rather than neither."""

    assert producer._inplane(_square(), np.asarray([10.4, 5.0])) == 0.0


# --------------------------------------------------------------------------- the material read


def test_decalscale_defaults_to_one_and_a_literal_zero_does_too():
    """`vmt.parse` reads `$decalscale` as `_find_f(...) or 1.0`, so an absent key and an authored
    `0` both size at 1.0. The quirk is reproduced, not corrected."""

    assert producer._material_decal_scale({"parameters": []}) == 1.0
    assert producer._material_decal_scale(
        {"parameters": [{"key": "$decalscale", "value": "0"}]}) == 1.0
    assert producer._material_decal_scale(
        {"parameters": [{"key": "$DecalScale", "value": "0.5"}]}) == 0.5
    assert producer._material_decal_scale(
        {"parameters": [{"key": "$decalscale", "value": "junk"}]}) == 1.0


def test_the_albedo_is_read_off_the_resolved_texture_binding():
    unit = {"textureBindings": [
        {"parameter": "$bumpmap", "asset": "vtmb:texture:decals/n"},
        {"parameter": "$basetexture", "asset": "vtmb:texture:decals/stains/rusta"},
    ]}
    assert producer._material_albedo(unit) == "decals/stains/rusta"
    assert producer._material_albedo({"textureBindings": []}) is None


def test_render_flags_are_vmt_parse_s_own_three_tests():
    units = producer.MaterialUnits.__new__(producer.MaterialUnits)
    units._flags = {}
    units._units = {
        "a": {"shader": "water", "parameters": []},
        "b": {"shader": "lightmappedgeneric", "parameters": [{"key": "%compilewater",
                                                              "value": "1"}]},
        "c": {"shader": "refract", "parameters": []},
        "d": {"shader": "unlitgeneric", "parameters": [{"key": "$additive", "value": "1"}]},
        "e": {"shader": "lightmappedgeneric", "parameters": []},
    }
    assert units.render_flags("a")["water"] is True
    assert units.render_flags("b")["water"] is True          # vbsp reads the compile key
    assert units.render_flags("c")["refract"] is True
    assert units.render_flags("d")["additive"] is True
    assert units.render_flags("e") == {"water": False, "refract": False, "additive": False}
    # A key with no published unit answers all three false, which is what an unresolved corpus
    # material answered before.
    units._units["missing"] = None
    assert units.render_flags("missing") == {
        "water": False, "refract": False, "additive": False}


# --------------------------------------------------------------------------- the staging lane


def test_a_malformed_row_fails_the_bake():
    with pytest.raises(decals.MapDecalsError, match="vtmb:material:"):
        decals._validate("synthetic", _row(materialId="decals/stains/rusta"))
    with pytest.raises(decals.MapDecalsError, match="malformed locCm"):
        decals._validate("synthetic", _row(locCm=[1.0, 2.0]))
    with pytest.raises(decals.MapDecalsError, match="length"):
        decals._validate("synthetic", _row(normal=[1.0, 1.0, 0.0]))
    with pytest.raises(decals.MapDecalsError, match="halfWCm"):
        decals._validate("synthetic", _row(halfWCm=0.0))
    with pytest.raises(decals.MapDecalsError, match="missing"):
        decals._validate("synthetic", {"index": 0})
    decals._validate("synthetic", _row())                      # the good row raises nothing


def test_the_payload_carries_the_counts_and_a_digest_over_its_rows():
    result = {"rows": [_row()], "placed": 1, "unresolved": 0, "unbound": 2, "materials": 1,
              "projectorFaces": 99, "dispFacesSkipped": 3}
    payload = decals.stage_result("synthetic", result)
    assert payload["counts"] == {
        "placed": 1, "unresolved": 0, "unbound": 2, "unmatched": 2, "materials": 1,
        "projectorFaces": 99, "dispFacesSkipped": 3}
    assert len(payload["sha256"]) == 64
    moved = decals.stage_result(
        "synthetic", {**result, "rows": [_row(locCm=[1.0, 2.0, 4.0])]})
    assert moved["sha256"] != payload["sha256"]


def test_the_row_and_the_sidecar_line_are_the_same_fifteen_tokens():
    tokens = producer.decal_line(_row()).split()
    assert len(tokens) == 15
    # The line names the bare key; the row names the same material by its placement-lane id.
    assert "vtmb:material:" + tokens[0] == _row()["materialId"]
    assert [float(value) for value in tokens[1:4]] == pytest.approx(_row()["locCm"])
    assert [float(value) for value in tokens[4:7]] == pytest.approx(_row()["normal"])
    assert [float(value) for value in tokens[7:10]] == pytest.approx(_row()["sDir"])
    assert [float(value) for value in tokens[10:13]] == pytest.approx(_row()["tDir"])
    assert float(tokens[13]) == pytest.approx(_row()["halfWCm"], abs=1e-4)
    assert float(tokens[14]) == pytest.approx(_row()["halfHCm"], abs=1e-4)


# --------------------------------------------------------------------------- the corpus


def _export_root():
    try:
        root = paths.export_v2_root()
    except RuntimeError:
        pytest.skip("V2 export root not configured")
    if not (root / "maps").is_dir():
        pytest.skip("V2 map units unavailable")
    return root


#: Placed-decal counts for the six maps of 0018 story 21-4, each equal to that map's legacy
#: `.decals` line count, and each the number `bake map --verify` counts `ADecalActor`s against.
#: `sp_soc_3` and `sp_genesisdevice_1` author no `infodecal` at all and the decoder wrote no file.
PLACED_PINS = {
    "sp_tutorial_1": 123,
    "sm_hub_1": 219,
    "sm_pawnshop_1": 38,
    "sp_theatre": 29,
    "sp_soc_3": 0,
    "sp_genesisdevice_1": 0,
}


@pytest.mark.parametrize("map_name", sorted(PLACED_PINS))
def test_each_map_places_the_decals_the_decoder_placed(map_name):
    _export_root()
    payload = decals.stage_map(map_name)
    assert payload["counts"]["placed"] == PLACED_PINS[map_name]
    # Not one decal on these six is lost to a material the units cannot size: every published
    # decal material resolves an albedo with dimensions, so `unmatched` can only be a decal that
    # binds no face, and on these six it is none.
    assert payload["counts"]["unmatched"] == 0


@pytest.mark.parametrize("map_name", sorted(PLACED_PINS))
def test_every_row_names_an_importable_material_and_a_real_quad(map_name):
    root = _export_root()
    for row in decals.stage_map(map_name)["rows"]:
        key = row["materialId"][len("vtmb:material:"):]
        assert (root / "materials" / f"{key}.glb").is_file()
        assert row["halfWCm"] > 0.0 and row["halfHCm"] > 0.0
