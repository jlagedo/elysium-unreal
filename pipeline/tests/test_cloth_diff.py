from elysium_pipeline.validation.cloth_diff import read_legacy_counts, compare_counts


def test_log_comparison_distinguishes_multiple_garments_and_real_counter_loss():
    log = """[make_cloth_assets] coat -> /old/CLOTH_coat_0 [wool] (3 sim vertices, 1 faces, 1 pinned, 2 collision bodies, 46 config properties)
[make_cloth_assets] coat   built: 3 sim vertices, 1 kinematic, 2 tethers
[make_cloth_assets] coat   render: 2 driven, 1 skinned, 0 orphaned, 0 root-bound particles, 0 particles with no normal
[make_cloth_assets] coat -> /old/CLOTH_coat_1 [wool] (4 sim vertices, 2 faces, 2 pinned, 2 collision bodies, 46 config properties)
[make_cloth_assets] coat   built: 4 sim vertices, 2 kinematic, 2 tethers
[make_cloth_assets] coat   render: 2 driven, 2 skinned, 0 orphaned, 0 root-bound particles, 0 particles with no normal
"""
    before = read_legacy_counts(log)
    native = {"vtmb:model:coat": [dict(row) for row in before["coat"]]}
    native["vtmb:model:coat"][0]["assetPath"] = "/new/CLOTH_coat_0"
    native["vtmb:model:coat"][1]["built_kinematic_vertices"] = 1
    result = compare_counts(before, native, {"coat": "vtmb:model:coat"})
    assert result["compared"] == 2
    assert not result["missing"]
    assert result["differences"] == [{"assetId": "vtmb:model:coat", "garment": 1,
                                      "field": "built_kinematic_vertices", "before": 2, "after": 1}]
