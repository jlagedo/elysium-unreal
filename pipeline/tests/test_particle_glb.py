"""Synthetic contract tests for the isolated particle-definition GLB exporter."""

from __future__ import annotations

import copy

import pytest

from elysium_pipeline.exporters import particle_glb as exporter
from elysium_pipeline.formats.particle_glb import (
    PARTICLE_EXTENSION,
    decode_particle,
    load_source_closure,
    parse_value,
)
from elysium_pipeline.formats.particle_glb.model import asset_id
from elysium_pipeline.formats.particle_glb.source import ParticleSourceError
from elysium_pipeline.formats.unit_contract import ByteLedger, ByteLedgerError, ranges_sha256, read_glb
from elysium_pipeline.validation import particle_glb as validation

WELL_FORMED = (
    b'Particle\n'
    b'{\n'
    b'\tsprite "DropletFast"\n'
    b'\tframes "15"\n'
    b'\tloop "1"\n'
    b'\tunknown_key "42"\n'
    b'\tsize "1,10"\n'
    b'\ttheta_speed "0"\n'
    b'\n'
    b'\tspawn\n'
    b'\t{\n'
    b'\t\tparticle "child_particle"\n'
    b'\t\trate "5~10"\n'
    b'\t\tburst "1"\n'
    b'\t}\n'
    b'\n'
    b'\tcollide\n'
    b'\t{\n'
    b'\t\tspawn\n'
    b'\t\t{\n'
    b'\t\t\tparticle "impact_particle"\n'
    b'\t\t\tfriction "0"\n'
    b'\t\t}\n'
    b'\t\tdecal\n'
    b'\t\t{\n'
    b'\t\t\tparticle "impact_decal"\n'
    b'\t\t}\n'
    b'\t}\n'
    b'}\n'
)

ORPHAN_OPEN = (
    b'Particle\n'
    b'{\n'
    b'\tloop "1"\n'
    b'\tspawn\n'
    b'\t{\n'
    b'\t\tparticle "live_child"\n'
    b'\t}\n'
    b'//\tspawn\n'
    b'\t{\n'
    b'\t\tparticle "orphaned_child"\n'
    b'\t}\n'
    b'}\n'
)

NO_WRAPPER = b'"bumpscale" "0.3"\n'

EMPTY_DEFINITION = b'Particle\n{\n}\n'

REPEATED_KEY = b'Particle\n{\n\tloop "1"\n\tloop "0"\n}\n'

WRONG_ROOT = b'Particlex\n{\n\tloop "1"\n}\n'

FRAMES_ZERO_WRAPPER = (
    b'Particle\n{\n\tframes "0"\n\tspawn\n\t{\n\t\tparticle "child"\n\t}\n}\n'
)

MATERIAL_SPRITE = b'Particle\n{\n\tsprite "materials/fx/spark"\n}\n'

TWO_DECALS_IN_ONE_COLLIDE = (
    b'Particle\n{\n\tcollide\n\t{\n\t\tdecal\n\t\t{\n\t\t\tparticle "impact_decal_a"\n\t\t}\n'
    b'\t\tdecal\n\t\t{\n\t\t\tparticle "impact_decal_b"\n\t\t}\n\t}\n}\n'
)

ORPHAN_OPEN_TRAILING_COMMENT = (
    b'Particle\n{\n\tloop "1"\n\tspawn\n\t{\n\t\tparticle "live_child"\n\t}\n'
    b'\t{\n\t\tparticle "orphaned_child"\n\t}\n\t// trailing note\n}\n'
)

WHOLE_MEMBER_UNPARSED = b"{" + b"a" * 100 + b"}"

EMPTY_KEY = b'Particle\n{\n\t"" "1"\n}\n'

TWO_ROOT_KEYS_SAME_LENGTH = b'Particle\n{\n\tloop "1"\n\tflat "1"\n}\n'


def _index(key: str = "particles/demo_emitter.txt", data: bytes = WELL_FORMED) -> tuple[dict, callable]:
    index = {key: ("loose", f"C:/game/Vampire/{key}")}
    return index, lambda idx, requested: data if requested == key else None


def _closure(name: str = "demo_emitter", data: bytes = WELL_FORMED):
    index, read_bytes = _index(f"particles/{name}.txt", data)
    return load_source_closure(index, name, read_bytes=read_bytes)


def _decode(name: str = "demo_emitter", data: bytes = WELL_FORMED, **exists):
    closure = _closure(name, data)
    return decode_particle(closure, **exists)


def _publish(name: str = "demo_emitter", data: bytes = WELL_FORMED, **exists):
    model = _decode(name, data, **exists)
    document, binary = exporter.build_document(model)
    return model, document, binary


# --- identity and key folding ------------------------------------------------------------------


def test_the_unit_key_is_the_lower_cased_stem_with_spaces_preserved():
    index, read_bytes = _index("particles/tz_ bloodtrickle_emitter.txt", WELL_FORMED)
    closure = load_source_closure(
        index, "Particles/TZ_ BloodTrickle_Emitter.TXT", read_bytes=read_bytes
    )
    assert closure.key == "tz_ bloodtrickle_emitter"
    assert closure.asset_id == "vtmb:particle:tz_ bloodtrickle_emitter"


def test_a_bare_directory_or_extension_spelling_all_resolve_one_key():
    index, read_bytes = _index("particles/foo_emitter.txt", WELL_FORMED)
    for spelling in ("foo_emitter", "particles/foo_emitter", "particles/foo_emitter.txt", "FOO_EMITTER"):
        closure = load_source_closure(index, spelling, read_bytes=read_bytes)
        assert closure.key == "foo_emitter"


def test_asset_id_is_vtmb_particle_prefixed():
    assert asset_id("foo_emitter") == "vtmb:particle:foo_emitter"


def test_a_missing_member_refuses_the_closure():
    index, read_bytes = _index()
    with pytest.raises(ParticleSourceError):
        load_source_closure(index, "does_not_exist", read_bytes=read_bytes)


# --- byte ledger: gapless, every byte claimed exactly once -------------------------------------


def test_every_byte_of_a_well_formed_definition_is_claimed_exactly_once():
    model = _decode()
    row = model.byte_ledger[0]
    assert row["byteLength"] == len(WELL_FORMED)
    assert row["accountedBytes"] == len(WELL_FORMED)
    assert row["coveragePercent"] == 100.0
    ranges = sorted(row["ranges"], key=lambda r: r["offset"])
    cursor = 0
    for entry in ranges:
        assert entry["offset"] == cursor
        cursor += entry["length"]
    assert cursor == len(WELL_FORMED)


def test_text_ranges_are_graded_mapped_text_not_mapped():
    """The ledger state `mapped` is reserved for a binary record or
    payload; `particles/*.txt` is a plain-text KeyValues file, so `root`, every `keys[i].key`/
    `keys[i].value` and every `blocks[i].name`/`blocks[i].braces` this seam claims must be graded
    `mapped-text`. This grammar's own `bom` token folds onto `whitespace` (`omitted-proven`), so
    unlike its sibling text seams it publishes no `mapped` range at all."""

    model = _decode()
    row = model.byte_ledger[0]
    assert "mapped" not in row["stateBytes"]
    assert row["stateBytes"]["mapped-text"] > 0
    owners_by_state: dict[str, set[str]] = {}
    for entry in row["ranges"]:
        owners_by_state.setdefault(entry["state"], set()).add(entry["owner"])
    assert "root" in owners_by_state["mapped-text"]
    assert any(owner.startswith("keys[") for owner in owners_by_state["mapped-text"])


def test_an_empty_member_is_a_zero_length_gapless_ledger_with_an_omission():
    model = _decode(data=b"")
    row = model.byte_ledger[0]
    assert row["byteLength"] == 0
    assert row["ranges"] == []
    assert row["coveragePercent"] == 100.0
    assert model.omissions == [{"role": "empty-member", "reason": "zero-byte-source"}]
    assert model.role == "neither"


def test_the_shared_ledger_rejects_a_reserved_zero_claim_over_a_non_zero_byte():
    ledger = ByteLedger("particles/demo.txt", b"\x01\x00")
    with pytest.raises(ByteLedgerError):
        ledger.claim(0, 1, "reserved-zero", "test.owner")


# --- value grammar ---------------------------------------------------------------------------


def test_a_scalar_value_parses_to_one_number():
    parsed = parse_value("3")
    assert parsed.kind == "scalar"
    assert parsed.values == [3.0]


def test_a_tilde_value_parses_to_a_range():
    parsed = parse_value("0~360")
    assert parsed.kind == "range"
    assert parsed.values == [0.0, 360.0]


def test_a_comma_value_parses_to_a_ramp_of_numbers():
    parsed = parse_value("1,10")
    assert parsed.kind == "ramp"
    assert parsed.values == [1.0, 10.0]


def test_a_ramp_element_may_itself_be_a_range():
    parsed = parse_value("15,5~50,20,5~50,15")
    assert parsed.kind == "ramp"
    assert parsed.values[1] == {"kind": "range", "values": [5.0, 50.0]}
    assert parsed.values[3] == {"kind": "range", "values": [5.0, 50.0]}


def test_a_ramp_keyframe_may_carry_an_explicit_position():
    parsed = parse_value("0,80(10)")
    assert parsed.kind == "ramp"
    assert parsed.values == [0.0, 80.0]
    assert parsed.positions == [None, 10.0]


def test_a_value_with_no_number_is_text():
    parsed = parse_value("DropletFast")
    assert parsed.kind == "text"
    assert parsed.values == ["DropletFast"]


# --- dependencies: one role per reference kind ------------------------------------------------


def test_a_sprite_key_produces_an_image_dependency():
    model = _decode(sprite_exists=lambda key: key == "dropletfast")
    row = next(row for row in model.dependencies if row["role"] == "image")
    assert row["asset"] == "vtmb:image:particles/dropletfast.tga"
    assert row["sourcePath"] == "particles/dropletfast.tga"
    assert row["resolved"] is True


def test_a_sprite_the_install_lacks_still_publishes_unresolved_not_failed():
    model = _decode(sprite_exists=lambda key: False)
    row = next(row for row in model.dependencies if row["role"] == "image")
    assert row["resolved"] is False
    assert model.unresolved == []              # the definition's own decode does not depend on it


def test_a_materials_path_sprite_produces_a_material_dependency_instead_of_an_image():
    model = _decode(data=MATERIAL_SPRITE, material_exists=lambda key: key == "fx/spark")
    row = next(row for row in model.dependencies if row["role"] == "material")
    assert row["asset"] == "vtmb:material:fx/spark"
    assert row["resolved"] is True
    assert not any(row["role"] == "image" for row in model.dependencies)


def test_spawn_and_decal_particle_keys_each_produce_one_particle_dependency():
    model = _decode(particle_exists=lambda key: key in ("child_particle", "impact_particle"))
    particle_rows = {row["sourcePath"]: row["resolved"] for row in model.dependencies if row["role"] == "particle"}
    assert particle_rows == {
        "particles/child_particle.txt": True,
        "particles/impact_particle.txt": True,
        "particles/impact_decal.txt": False,
    }


def test_a_repeated_reference_to_one_particle_publishes_one_dependency_row():
    data = WELL_FORMED.replace(b'"impact_particle"', b'"child_particle"')
    model = _decode(data=data)
    rows = [row for row in model.dependencies if row["asset"] == asset_id("child_particle")]
    assert len(rows) == 1


def test_a_root_normal_key_produces_both_an_image_and_a_particle_dependency():
    # `Deform_Normal` may name a sprite bitmap (`particles/deform_normal.tga`) and/or a
    # same-named rootless bumpscale particle definition (`particles/deform_normal.txt`); both
    # are declared, whichever the install actually resolves.
    data = b'Particle\n{\n\tnormal "Deform_Normal"\n}\n'
    model = _decode(
        data=data,
        sprite_exists=lambda key: key == "deform_normal",
        particle_exists=lambda key: key == "deform_normal",
    )
    image_row = next(row for row in model.dependencies if row["role"] == "image")
    particle_row = next(row for row in model.dependencies if row["role"] == "particle")
    assert image_row["asset"] == "vtmb:image:particles/deform_normal.tga"
    assert image_row["resolved"] is True
    assert particle_row["asset"] == "vtmb:particle:deform_normal"
    assert particle_row["resolved"] is True


def test_a_refract_key_naming_a_materials_path_produces_only_a_material_dependency():
    data = b'Particle\n{\n\trefract "materials/fx/normalmap"\n}\n'
    model = _decode(data=data, material_exists=lambda key: key == "fx/normalmap")
    row = next(row for row in model.dependencies if row["role"] == "material")
    assert row["asset"] == "vtmb:material:fx/normalmap"
    assert not any(row["role"] in ("image", "particle") for row in model.dependencies)


def test_decal_vdecal_keys_each_produce_a_material_dependency():
    data = (
        b'Particle\n{\n\tcollide\n\t{\n\t\tdecal\n\t\t{\n\t\t\tparticle "impact_decal"\n'
        b'\t\t\tvdecal_first "decals/stains/blooda"\n'
        b'\t\t\tvdecal_last "materials/decals/stains/bloodb"\n'
        b'\t\t}\n\t}\n}\n'
    )
    model = _decode(
        data=data,
        material_exists=lambda key: key in ("decals/stains/blooda", "decals/stains/bloodb"),
    )
    material_rows = {row["asset"]: row["resolved"] for row in model.dependencies if row["role"] == "material"}
    assert material_rows == {
        "vtmb:material:decals/stains/blooda": True,
        "vtmb:material:decals/stains/bloodb": True,
    }


# --- vocabulary: meaning or typedUnidentified ---------------------------------------------------


def test_a_vocabulary_key_carries_its_meaning_in_projection():
    model = _decode()
    assert model.projection["definition"]["loop"]["meaning"] == "lifetime and looping"
    assert model.projection["particle"]["sprite"]["meaning"] == "sprite binding and orientation"
    spawn_entry = model.projection["spawns"][0]
    assert spawn_entry["keys"]["particle"]["meaning"] == "child emission"


def test_a_key_outside_the_vocabulary_is_typed_unidentified_with_meaning_null():
    model = _decode()
    assert model.projection["particle"]["unknown_key"]["meaning"] is None
    typed = next(row for row in model.typed_unidentified if row["key"] == "unknown_key")
    assert typed["block"] is None
    assert isinstance(typed["offset"], int)


def test_a_decal_key_outside_its_narrow_vocabulary_is_typed_unidentified():
    # `decal` documents only `particle`; a key like `theta` inside one (real shipped files carry
    # it) has no established decal-scope meaning.
    data = WELL_FORMED.replace(b'particle "impact_decal"', b'particle "impact_decal"\n\t\t\ttheta "0"')
    model = _decode(data=data)
    assert any(row["key"] == "theta" for row in model.typed_unidentified)


# --- role derivation -----------------------------------------------------------------------


def test_role_is_drawing_when_only_a_sprite_is_present():
    data = b'Particle\n{\n\tsprite "x"\n}\n'
    assert _decode(data=data).role == "drawing"


def test_role_is_emitter_when_only_a_top_level_spawn_is_present():
    data = b'Particle\n{\n\tspawn\n\t{\n\t\tparticle "x"\n\t}\n}\n'
    assert _decode(data=data).role == "emitter"


def test_role_is_both_when_sprite_and_spawn_are_both_present():
    assert _decode().role == "both"


def test_role_is_neither_when_no_sprite_and_no_spawn_are_present():
    data = b'Particle\n{\n\tloop "1"\n}\n'
    assert _decode(data=data).role == "neither"


# --- anomalies named by the seam spec ----------------------------------------------------------


def test_a_root_token_other_than_particle_is_an_anomaly():
    model = _decode(data=WRONG_ROOT)
    assert model.root == "Particlex"
    assert any(row["role"] == "root-not-particle" for row in model.anomalies)


def test_a_file_with_no_wrapper_brace_at_all_is_root_not_particle():
    model = _decode(data=NO_WRAPPER)
    assert model.root == ""
    row = next(row for row in model.anomalies if row["role"] == "root-not-particle")
    assert row["reason"] == "no-root-block"
    # the flat pair still gets decoded and its bytes still fully claimed
    assert model.byte_ledger[0]["coveragePercent"] == 100.0
    assert any(entry.key == "bumpscale" for entry in model.keys)


def test_an_empty_root_block_is_an_empty_definition():
    model = _decode(data=EMPTY_DEFINITION)
    assert any(row["role"] == "empty-definition" for row in model.anomalies)


def test_a_repeated_scalar_key_is_an_anomaly_and_keeps_both_occurrences():
    model = _decode(data=REPEATED_KEY)
    assert sum(1 for entry in model.keys if entry.key == "loop") == 2
    assert any(row["role"] == "repeated-scalar-key" for row in model.anomalies)


def test_frames_zero_on_an_emitter_only_wrapper_is_an_anomaly():
    model = _decode(data=FRAMES_ZERO_WRAPPER)
    assert model.role == "emitter"
    assert any(row["role"] == "frames-zero-on-wrapper" for row in model.anomalies)


def test_an_orphan_open_brace_is_unbalanced_braces_with_an_unparsed_tail():
    model = _decode(data=ORPHAN_OPEN)
    assert any(row["role"] == "unbalanced-braces" for row in model.anomalies)
    omission = next(row for row in model.omissions if row["role"] == "unparsed-region")
    assert omission["offset"] > 0
    # the well-formed prefix (the live spawn block) is still a structured part of the tree
    assert any(
        entry.value == "live_child" for entry in model.keys if entry.key == "particle"
    )
    row = model.byte_ledger[0]
    assert row["coveragePercent"] == 100.0
    assert row["accountedBytes"] == len(ORPHAN_OPEN)


def test_the_unparsed_region_carries_its_own_text_as_raw():
    model = _decode(data=ORPHAN_OPEN)
    omission = next(row for row in model.omissions if row["role"] == "unparsed-region")
    end = omission["offset"] + omission["length"]
    assert omission["raw"] == ORPHAN_OPEN[omission["offset"]:end].decode("latin-1")


def test_an_unterminated_quoted_string_is_a_named_anomaly():
    data = b'Particle\n{\n\tloop "1\n}\n'
    model = _decode(data=data)
    assert any(row["role"] == "unterminated-quoted-string" for row in model.anomalies)
    # the mis-paired value still absorbs every trailing byte, so the ledger stays gapless
    assert model.byte_ledger[0]["coveragePercent"] == 100.0


# --- whitespace and root-scope booleans ---------------------------------------------------------


def test_insignificant_whitespace_is_named_in_omissions():
    model = _decode()
    assert any(
        row["role"] == "keyvalues-insignificant-whitespace" for row in model.omissions
    )


def test_precipitation_is_published_as_a_boolean_beside_role():
    data = b'Particle\n{\n\tprecipitation "1"\n}\n'
    model, document, _ = _publish(data=data)
    assert model.precipitation is True
    root = document["extensions"][PARTICLE_EXTENSION]
    assert root["precipitation"] is True


def test_precipitation_defaults_to_false_when_the_key_is_absent():
    model = _decode()
    assert model.precipitation is False


# --- document shape --------------------------------------------------------------------------


def test_the_document_declares_the_extension_used_and_required_and_is_scene_less():
    _, document, binary = _publish()
    assert document["extensionsUsed"] == [PARTICLE_EXTENSION]
    assert document["extensionsRequired"] == [PARTICLE_EXTENSION]
    assert document["asset"]["generator"] == "Elysium Particle GLB Exporter"
    assert binary == b""
    for forbidden in ("scenes", "nodes", "meshes", "images", "textures", "samplers"):
        assert forbidden not in document


def test_the_extension_root_opens_with_the_contract_key_order():
    _, document, _ = _publish()
    root = document["extensions"][PARTICLE_EXTENSION]
    assert list(root)[:5] == ["schemaVersion", "identity", "sourceResolution", "dependencies", "coverage"]


def test_a_complete_unit_has_zero_unresolved_and_zero_unsupported():
    _, document, _ = _publish()
    coverage = document["extensions"][PARTICLE_EXTENSION]["coverage"]
    assert coverage["unresolved"] == []
    assert coverage["unsupported"] == []


def test_identity_carries_particle_path_beside_source_path_and_key():
    _, document, _ = _publish()
    identity = document["extensions"][PARTICLE_EXTENSION]["identity"]
    assert identity["particlePath"] == "particles/demo_emitter.txt"
    assert identity["key"] == "demo_emitter"
    assert identity["sourcePath"] == "particles/demo_emitter.txt"


# --- validation --------------------------------------------------------------------------------


def test_validate_document_accepts_a_freshly_built_document():
    model, document, binary = _publish()
    closure = _closure()
    summary = validation.validate_document(document, binary, source_members=closure.members())
    assert summary["asset"] == "vtmb:particle:demo_emitter"
    assert summary["role"] == "both"


def test_validate_document_rejects_a_tampered_byte_ledger():
    _, document, binary = _publish()
    tampered = copy.deepcopy(document)
    ranges = tampered["extensions"][PARTICLE_EXTENSION]["coverage"]["byteLedger"][0]["ranges"]
    ranges[0]["length"] += 1
    with pytest.raises(Exception):
        validation.validate_document(tampered, binary)


def test_validate_document_rejects_an_unknown_anomaly_role():
    _, document, binary = _publish()
    tampered = copy.deepcopy(document)
    tampered["extensions"][PARTICLE_EXTENSION]["anomalies"].append({"role": "made-up", "offset": 0})
    with pytest.raises(validation.ParticleGlbValidationError):
        validation.validate_document(tampered, binary)


def test_export_time_validation_re_decodes_independently_and_catches_a_forged_key_table():
    _, document, binary = _publish()
    closure = _closure()
    tampered = copy.deepcopy(document)
    tampered["extensions"][PARTICLE_EXTENSION]["keys"][0]["value"] = "forged"
    with pytest.raises(validation.ParticleGlbValidationError):
        validation.validate_document(tampered, binary, source_members=closure.members())


def test_export_writes_a_unit_that_round_trips_through_standalone_validation(tmp_path):
    index, read_bytes = _index()
    destination = exporter.export(index, "demo_emitter", tmp_path, read_bytes=read_bytes)
    assert destination.exists()
    summary = validation.validate(destination)
    assert summary["asset"] == "vtmb:particle:demo_emitter"
    assert summary["byteCoveragePercent"] == 100.0
    # the fake index carries only the definition itself, so every reference is unresolved
    assert summary["unresolvedDependencies"] == sorted([
        "particles/dropletfast.tga",
        "particles/child_particle.txt",
        "particles/impact_particle.txt",
        "particles/impact_decal.txt",
    ])


# --- F2: `waterbigsplash_emitter` is read from the retail VPK member, not the UP stub -----------


def test_waterbigsplash_emitter_is_read_from_the_retail_vpk_member_not_the_up_stub(tmp_path):
    # Shapes of the real UP-first stub (authored-empty, spawn block commented out) and the real
    # retail `pack001.vpk` member (`water-complete.md` Phase 0 verdict F2), trimmed to what the
    # decoder needs to tell them apart: the stub has no live spawn block, the retail member does.
    up_stub = (
        b'Particle\r\n{\r\n\tloop "0"\t\r\n\tframes "10"\r\n\r\n// removed by wesp\tspawn\r\n'
        b'\t{\r\n\t\tparticle "WaterBigSplash"\r\n\t}\r\n}\r\n'
    )
    retail_data = (
        b'Particle\n{\n\tloop "0"\t\n\tframes "10"\n\n\tspawn\n\t{\n\t\tparticle "WaterBigSplash"'
        b'\n\t\tburst "4"\n\t}\n}\n'
    )
    path = "particles/waterbigsplash_emitter.txt"
    index = {path: ("loose", f"C:/game/Unofficial_Patch/{path}")}
    retail_index = {path: ("pack001.vpk", 70407456, len(retail_data))}

    def read_bytes(idx, requested):
        if requested != path:
            return None
        kind, _ = idx[requested]
        return retail_data if kind == "vpk" else up_stub

    destination = exporter.export(
        index, "waterbigsplash_emitter", tmp_path,
        read_bytes=read_bytes, retail_index=retail_index,
    )
    summary = validation.validate(destination)
    # The retail spawn block resolved, not the UP stub's dead one: the unit draws something.
    assert summary["role"] in ("emitter", "both")
    assert "unbalanced-braces" not in summary["anomalies"]

    document, _binary = read_glb(destination)
    root = document["extensions"][PARTICLE_EXTENSION]
    member = root["sourceResolution"]["members"][0]
    assert member["origin"]["kind"] == "vpk"
    assert member["origin"]["container"] == "pack001.vpk"


def test_a_key_outside_the_divergence_list_still_resolves_up_first(tmp_path):
    # The override touches only its one named key; a plain loose-shadowed unit stages the loose
    # answer exactly as it always did.
    path = "particles/watersplash_emitter.txt"
    index = {path: ("loose", f"C:/game/Unofficial_Patch/{path}")}
    destination = exporter.export(
        index, "watersplash_emitter", tmp_path,
        read_bytes=lambda idx, requested: WELL_FORMED if requested == path else None,
    )
    document, _binary = read_glb(destination)
    member = document["extensions"][PARTICLE_EXTENSION]["sourceResolution"]["members"][0]
    assert member["origin"]["kind"] == "loose"


def test_export_of_a_malformed_file_still_publishes_and_warns(tmp_path):
    index, read_bytes = _index("particles/broken_emitter.txt", ORPHAN_OPEN)
    destination = exporter.export(index, "broken_emitter", tmp_path, read_bytes=read_bytes)
    summary = validation.validate(destination)
    assert "unbalanced-braces" in summary["anomalies"]
    warnings = validation.warnings_for(summary)
    assert any("anomaly" in warning for warning in warnings)


# --- a collide block with more than one decal (review finding: only the first was published) ----


def test_a_collide_with_two_decals_publishes_projection_for_both():
    model = _decode(data=TWO_DECALS_IN_ONE_COLLIDE)
    collides = model.projection["collide"]
    assert len(collides) == 1
    decals = collides[0]["decals"]
    assert len(decals) == 2
    assert all(entry["keys"]["particle"]["meaning"] == "the decal definition" for entry in decals)
    particle_paths = {
        row["sourcePath"] for row in model.dependencies if row["role"] == "particle"
    }
    assert particle_paths == {"particles/impact_decal_a.txt", "particles/impact_decal_b.txt"}


def test_no_collide_publishes_an_empty_collide_list_not_null():
    model = _decode(data=b'Particle\n{\n\tloop "1"\n}\n')
    assert model.projection["collide"] == []


# --- normal/refract only names a dependency when the value is actually a name --------------------


def test_a_refract_key_holding_a_numeric_ramp_produces_no_phantom_dependency():
    # Every shipped `refract` is a numeric ramp, never a texture name; a dependency row must not
    # be fabricated out of compiler-dead cruft.
    data = b'Particle\n{\n\trefract "3~-3,0"\n}\n'
    model = _decode(data=data)
    assert not any(row["role"] in ("image", "particle") for row in model.dependencies)
    assert model.projection["particle"]["refract"]["meaning"] == (
        "a second texture used as a refraction card"
    )


def test_a_normal_key_holding_a_bare_scalar_produces_no_phantom_dependency():
    data = b'Particle\n{\n\tnormal "1.0"\n}\n'
    model = _decode(data=data)
    assert model.dependencies == []


# --- a comment inside the unparsed tail is still published in comments[] -------------------------


def test_a_comment_inside_the_unparsed_region_still_appears_in_comments():
    model = _decode(data=ORPHAN_OPEN_TRAILING_COMMENT)
    assert any("trailing note" in entry["text"] for entry in model.comments)
    # its bytes stay claimed once, under the `unparsed[0]` owner, not doubly under `comments[i]`
    row = model.byte_ledger[0]
    assert row["coveragePercent"] == 100.0


# --- a definition unparseable from byte zero still publishes its raw text whole -------------------


def test_a_definition_unparseable_from_byte_zero_keeps_its_raw_text_whole(tmp_path):
    # The capsule doctrine already carries the exact source bytes -- `omissions[].raw` truncating
    # under the old opaque-source rule was pure fidelity loss with no reader left to protect.
    model, document, binary = _publish(data=WHOLE_MEMBER_UNPARSED)
    omission = next(row for row in model.omissions if row["role"] == "unparsed-region")
    assert omission["offset"] == 0
    assert omission["length"] == len(WHOLE_MEMBER_UNPARSED)
    assert omission["raw"] == WHOLE_MEMBER_UNPARSED.decode("latin-1")
    assert model.byte_ledger[0]["coveragePercent"] == 100.0
    closure = _closure(data=WHOLE_MEMBER_UNPARSED)
    summary = validation.validate_document(document, binary, source_members=closure.members())
    assert "unparsed-region" in summary["omissions"]


# --- an empty quoted key is named, not rejected ---------------------------------------------------


def test_an_empty_quoted_key_is_named_as_an_anomaly_not_rejected():
    model = _decode(data=EMPTY_KEY)
    assert any(entry.key == "" for entry in model.keys)
    assert any(row["role"] == "empty-key" for row in model.anomalies)
    assert model.byte_ledger[0]["coveragePercent"] == 100.0


def test_export_of_an_empty_quoted_key_round_trips_through_validation(tmp_path):
    index, read_bytes = _index("particles/empty_key_emitter.txt", EMPTY_KEY)
    destination = exporter.export(index, "empty_key_emitter", tmp_path, read_bytes=read_bytes)
    summary = validation.validate(destination)
    assert "empty-key" in summary["anomalies"]


# --- quoting is published per token, not borrowed from the other one of the pair -----------------


def test_keys_publish_quoting_for_the_key_and_value_tokens_separately():
    bare = b'Particle\n{\n\tsprite DropletFast\n}\n'
    _, bare_document, _ = _publish(data=bare)
    bare_row = bare_document["extensions"][PARTICLE_EXTENSION]["keys"][0]
    assert bare_row["quotedKey"] is False
    assert bare_row["quotedValue"] is False

    quoted = b'Particle\n{\n\t"sprite" "DropletFast"\n}\n'
    _, quoted_document, _ = _publish(data=quoted)
    quoted_row = quoted_document["extensions"][PARTICLE_EXTENSION]["keys"][0]
    assert quoted_row["quotedKey"] is True
    assert quoted_row["quotedValue"] is True


# --- export-time validation proves an owner's bytes independently of the writer's own offsets ----


def test_export_time_validation_independently_checks_that_an_owner_names_its_own_bytes():
    _, document, binary = _publish(data=TWO_ROOT_KEYS_SAME_LENGTH)
    closure = _closure(data=TWO_ROOT_KEYS_SAME_LENGTH)
    tampered = copy.deepcopy(document)
    ledger_row = tampered["extensions"][PARTICLE_EXTENSION]["coverage"]["byteLedger"][0]
    ranges = ledger_row["ranges"]
    key0 = next(r for r in ranges if r["owner"] == "keys[0].key")
    key1 = next(r for r in ranges if r["owner"] == "keys[1].key")
    assert key0["length"] == key1["length"]
    # Swap which offset each owner names -- the ledger stays gapless and its own digest is
    # recomputed to match, so only a check that reads the actual bytes back can catch this.
    key0["offset"], key1["offset"] = key1["offset"], key0["offset"]
    ranges.sort(key=lambda entry: entry["offset"])
    ledger_row["ranges"] = ranges
    ledger_row["rangesSha256"] = ranges_sha256(
        ledger_row["sourcePath"], ledger_row["byteLength"], ranges
    )
    with pytest.raises(validation.ParticleGlbValidationError):
        validation.validate_document(tampered, binary, source_members=closure.members())
