"""The Model GLB seam, exercised on synthetic bytes and a fake install index.

Nothing here reads the real installation: every source is built in the test, so a failure names a
rule of the seam rather than a property of one machine's copy of the game.
"""

from __future__ import annotations

from pathlib import Path
import struct
import tempfile

import pytest

from elysium_pipeline.exporters import model_glb
from elysium_pipeline.formats.model_glb import (
    ModelSourceError,
    coverage,
    decode_model,
    expressions,
    load_source_closure,
    physics,
    source_keys,
    surface_property_names,
    vtx,
)
from elysium_pipeline.formats.model_glb.model import (
    SCHEMA_VERSION,
    asset_id,
    family_of,
    normalize_model_key,
    output_relative_path,
    shape_of,
)
from elysium_pipeline.formats.unit_contract import ByteLedgerError, SourceMember, origin_of
from elysium_pipeline.validation import model_glb as validation

MODEL_KEY = "scenery/synthetic/prop"
MODEL_PATH = f"models/{MODEL_KEY}.mdl"
CHECKSUM = 0x13572468
EXTENSION = "ELYSIUM_vtmb_model"


def test_morph_projection_preserves_zero_undrawn_and_repeated_contributions(monkeypatch):
    from elysium_pipeline.formats.model_glb import decode
    monkeypatch.setattr(decode.mdl_skel, "flex_descs", lambda data: ["smile"])
    flexes = [{"flexdesc": 0, "targets": [0., 1., 1., 2.], "index": i,
               "numverts": 3 if i == 0 else 1} for i in range(2)]
    monkeypatch.setattr(decode.mdl_skel, "mesh_flexes", lambda *args: flexes)
    def deltas(data, flex, normals):
        if flex["index"] == 0:
            return [(0, (0., 0., 0.), (0., 0., 0.)),
                    (1, (1., 0., 0.), (0., 0., 0.)),
                    (2, (9., 0., 0.), (0., 0., 0.))]
        return [(1, (2., 0., 0.), (0., 1., 0.))]
    monkeypatch.setattr(decode.mdl_skel, "vert_anims", deltas)
    primitive = {"sourceVertices": [10, 11], "positions": [(0., 0., 0.)] * 2,
                 "modelBase": 0, "mesh": 0, "vertexOffset": 10}
    targets = decode._morph_targets(b"", [{"primitives": [primitive]}], object())
    assert len(targets) == 1
    source = primitive["morphRecords"]
    assert [row["sourceVertex"] for row in source] == [10, 11, 12, 11]
    assert [row["vertex"] for row in source] == [0, 1, None, 1]
    assert [row["flex"] for row in source] == [0, 0, 0, 1]
    assert source[0]["position"] == (0., 0., 0.)
    assert source[2]["position"] == (9., 0., 0.)
    assert primitive["morphTargets"][0]["position"][1] == (3., 0., 0.)
    mesh = {"index": 0, "vertexOffset": 10, "flexes": flexes}
    parts = [{"index": 0, "models": [{"index": 0, "vertexCount": 20, "meshes": [mesh]}]}]
    decode._unrendered_morph_records(b"", parts, [], object())
    assert [r["sourceVertex"] for r in mesh["unrenderedMorphRecords"]] == [10, 11, 12, 11]
    root = {"vtx": {"lods": []}, "facial": {"morphTargets": []}, "mdl": {"bodyParts": parts}}
    validation._check_morph_records({}, root)
    mesh["unrenderedMorphRecords"].pop()
    with pytest.raises(validation.ModelGlbValidationError, match="unrenderedMorphRecords"):
        validation._check_morph_records({}, root)


def _minimal_mdl_vtx(
    *, static_prop: bool = True, checksum: int = CHECKSUM, textures: int = 1
) -> tuple[bytes, bytes]:
    """One bone, one mesh, one triangle and two independently addressed VTX LODs.

    `textures` moves the material table to the image tail so that two slots can name one VMT.
    """

    mdl = bytearray(1124)
    mdl[:4] = b"IDST"
    struct.pack_into("<iI", mdl, 4, 2531, checksum)
    internal = b"synthetic/prop.mdl\0"
    mdl[12:12 + len(internal)] = internal
    struct.pack_into("<i", mdl, 140, len(mdl))
    struct.pack_into("<i", mdl, 228, 0x10 if static_prop else 0)
    struct.pack_into("<2f", mdl, 232, 0.065, 0.1)
    struct.pack_into("<2i", mdl, 240, 1, 424)          # one bone at 424
    struct.pack_into("<2i", mdl, 292, 1, 600)          # one texture at 600
    struct.pack_into("<2i", mdl, 300, 1, 632)          # one search path at 632
    struct.pack_into("<3i", mdl, 308, 1, 1, 640)       # one skin family of one reference
    struct.pack_into("<2i", mdl, 320, 1, 644)          # one body part at 644
    struct.pack_into("<i", mdl, 420, 1)                # CONTENTS_SOLID

    bone = 424
    struct.pack_into("<2i", mdl, bone, 160, -1)
    struct.pack_into("<3f", mdl, bone + 32, 0.0, 0.0, 0.0)
    struct.pack_into("<4f", mdl, bone + 44, 0.0, 0.0, 0.0, 1.0)
    struct.pack_into("<3f", mdl, bone + 60, 1.0, 1.0, 1.0)
    struct.pack_into("<4f", mdl, bone + 72, 1.0, 1.0, 1.0, 1.0)
    struct.pack_into(
        "<12f", mdl, bone + 88,
        1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
    )
    struct.pack_into("<i", mdl, bone + 156, 1)
    mdl[584:589] = b"root\0"

    struct.pack_into("<i", mdl, 600, 20)
    mdl[620:625] = b"body\0"
    struct.pack_into("<i", mdl, 632, 636)
    mdl[636] = 0
    struct.pack_into("<h", mdl, 640, 0)
    struct.pack_into("<i", mdl, 648, 1)                # body part: one model
    struct.pack_into("<i", mdl, 656, 16)               # model offset, body-part relative

    model = 660
    mdl[model:model + 10] = b"synthetic\0"
    struct.pack_into("<3i", mdl, model + 136, 1, 224, 3)
    struct.pack_into("<i", mdl, model + 148, 284)
    struct.pack_into("<i", mdl, model + 152, 416)
    struct.pack_into("<i", mdl, model + 156, 0)
    mesh = model + 224
    struct.pack_into("<4i", mdl, mesh, 0, 0, 3, 0)
    vertices = model + 284
    for index, (position, uv) in enumerate(
        (((0.0, 0.0, 0.0), (0.0, 0.0)), ((1.0, 0.0, 0.0), (1.0, 0.0)), ((0.0, 1.0, 0.0), (0.0, 1.0)))
    ):
        vertex = vertices + index * 44
        struct.pack_into("<4B4h", mdl, vertex, 255, 0, 0, 1, 0, 0, 0, 0)
        struct.pack_into("<3f", mdl, vertex + 12, *position)
        struct.pack_into("<3f", mdl, vertex + 24, 0.0, 0.0, 1.0)
        struct.pack_into("<2f", mdl, vertex + 36, *uv)
        struct.pack_into("<4f", mdl, 1076 + index * 16, 1.0, 0.0, 0.0, 1.0)

    if textures != 1:
        table = len(mdl)
        mdl[600:625] = bytes(25)                       # the one-record table it replaces
        mdl.extend(bytes(textures * 20))
        for slot in range(textures):
            name = len(mdl)
            mdl.extend(b"body\0")
            struct.pack_into("<i", mdl, table + slot * 20, name - (table + slot * 20))
        mdl.extend(bytes(-len(mdl) % 4))
        struct.pack_into("<2i", mdl, 292, textures, table)
        struct.pack_into("<i", mdl, 140, len(mdl))

    topology = bytearray(204)
    struct.pack_into("<i", topology, 0, 107)
    struct.pack_into("<I", topology, 16, checksum)
    struct.pack_into("<2i", topology, 20, 2, 188)
    struct.pack_into("<2i", topology, 28, 1, 36)
    struct.pack_into("<2i", topology, 36, 1, 8)
    struct.pack_into("<2i", topology, 44, 2, 8)
    struct.pack_into("<2if", topology, 52, 1, 24, 0.0)
    struct.pack_into("<2if", topology, 64, 1, 20, 10.0)
    struct.pack_into("<H", topology, 76, 1)
    struct.pack_into("<i", topology, 80, 16)
    struct.pack_into("<H", topology, 84, 1)
    struct.pack_into("<i", topology, 88, 56)
    for group in (92, 140):
        struct.pack_into("<H", topology, group, 3)
        struct.pack_into("<H", topology, group + 2, 3)
        struct.pack_into("<H", topology, group + 4, 1)
        topology[group + 6] = 0x10
        struct.pack_into("<3i", topology, group + 8, 20, 26, 32)
        struct.pack_into("<3H", topology, group + 20, 0, 1, 2)
        struct.pack_into("<3H", topology, group + 26, 0, 1, 2)
        struct.pack_into("<4H", topology, group + 32, 3, 0, 3, 0)
    return bytes(mdl), bytes(topology)


def _stub_images(*, checksum: int = CHECKSUM) -> tuple[bytes, bytes]:
    """A header-only MDL/VTX pair, for the source-closure rules that never decode."""

    mdl = bytearray(424)
    mdl[:4] = b"IDST"
    struct.pack_into("<iI", mdl, 4, 2531, checksum)
    topology = bytearray(36)
    struct.pack_into("<I", topology, 16, checksum)
    return bytes(mdl), bytes(topology)


def _install(extra: dict[str, bytes] | None = None, *, static_prop: bool = True):
    mdl, topology = _minimal_mdl_vtx(static_prop=static_prop)
    files = {
        MODEL_PATH: mdl,
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        "materials/body.vmt": b'VertexLitGeneric\n{\n"$basetexture" "synthetic/body"\n'
                              b'"$surfaceprop" "metal"\n}\n',
        "scripts/surfaceproperties.txt": b'"default"\n{\n"friction" "0.8"\n}\n'
                                         b'"metal"\n{\n"friction" "0.6"\n}\n',
    }
    files.update(extra or {})
    index = {key: ("loose", f"C:/VTMB/Unofficial_Patch/{key}") for key in files}
    return index, files, (lambda _index, key: files.get(key))


def _export(tmp: Path, key: str = MODEL_KEY, **kwargs) -> Path:
    index, _files, read_bytes = _install(kwargs.pop("extra", None), **kwargs)
    return model_glb.export(index, key, tmp, read_bytes=read_bytes, anorms=[])


def _unit(**kwargs):
    index, _files, read_bytes = _install(kwargs.pop("extra", None), **kwargs)
    closure = load_source_closure(index, MODEL_KEY, read_bytes=read_bytes)
    return closure, decode_model(
        closure,
        index,
        read_bytes=read_bytes,
        anorms=[],
        surface_properties=surface_property_names(index, read_bytes=read_bytes),
    )


# --- identity -------------------------------------------------------------------------------


def test_the_key_is_the_normalized_path_below_models_without_the_extension() -> None:
    for spelling in (MODEL_PATH, f"{MODEL_KEY}.mdl", MODEL_KEY, f"\\Models\\{MODEL_KEY.upper()}"):
        assert normalize_model_key(spelling) == MODEL_KEY
    assert asset_id(MODEL_PATH) == f"vtmb:model:{MODEL_KEY}"
    assert output_relative_path(MODEL_PATH).as_posix() == f"{MODEL_KEY}.glb"


def test_a_model_at_the_root_of_models_publishes_an_empty_family() -> None:
    assert family_of(MODEL_KEY) == "scenery"
    assert family_of("models/null.mdl") == ""


def test_shape_is_classified_from_the_bytes_and_not_from_the_path() -> None:
    assert shape_of(body_part_count=0, bone_count=60, local_animation_count=0, flags=0) == "bank"
    assert shape_of(body_part_count=1, bone_count=9, local_animation_count=4, flags=0x10) == "static"
    assert shape_of(body_part_count=1, bone_count=1, local_animation_count=0, flags=0) == "static"
    assert shape_of(body_part_count=1, bone_count=9, local_animation_count=4, flags=0) == "skeletal"


def test_only_members_below_models_are_model_units() -> None:
    index = {
        MODEL_PATH: ("loose", "x"),
        "models/scenery/other/prop.MDL".lower(): ("loose", "x"),
        "unpacked 0.74/shovelhead/shovelhead_short.mdl": ("loose", "x"),
        "models/scenery/synthetic/prop.dx80.vtx": ("loose", "x"),
    }
    assert source_keys(index) == ["scenery/other/prop", MODEL_KEY]


# --- source closure -------------------------------------------------------------------------


def test_the_dx80_variant_is_primary_and_the_dx7_pair_is_its_alternate() -> None:
    mdl, topology = _stub_images()
    files = {
        MODEL_PATH: mdl,
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        f"models/{MODEL_KEY}.dx7_2bone.vtx": topology,
    }
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in files}
    closure = load_source_closure(index, MODEL_PATH, read_bytes=lambda _i, key: files.get(key))
    assert closure.primary_vtx.role == "vtx-dx80"
    assert closure.alternate_vtx.role == "vtx-dx7-2bone"
    assert [member.role for member in closure.members()] == ["mdl", "vtx-dx80", "vtx-dx7-2bone"]


def test_an_absent_variant_is_an_omission_and_not_a_member() -> None:
    _closure, unit = _unit()
    rows = [row for row in unit.omissions if row["row"] == "missing-vtx-variant"]
    assert [row["variant"] for row in rows] == ["vtx-dx7-2bone"]
    assert "vtx-dx7-2bone" not in {member.role for member in unit.members}


def test_a_model_with_body_parts_and_no_topology_at_all_is_unresolved() -> None:
    mdl, _topology = _minimal_mdl_vtx()
    files = {MODEL_PATH: mdl, "materials/body.vmt": b"VertexLitGeneric\n{\n}\n"}
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in files}
    closure = load_source_closure(index, MODEL_KEY, read_bytes=lambda _i, key: files.get(key))
    unit = decode_model(
        closure, index, read_bytes=lambda _i, key: files.get(key), anorms=[],
        surface_properties=frozenset(),
    )
    assert closure.primary_vtx is None
    assert [row["reason"] for row in unit.unresolved] == ["no-admitted-vtx-variant"]
    assert unit.lods == []


def test_a_variant_whose_checksum_disagrees_is_admitted_only_when_it_indexes_this_mdl() -> None:
    mdl, topology = _minimal_mdl_vtx()
    stale = bytearray(topology)
    struct.pack_into("<I", stale, 16, CHECKSUM ^ 0xFFFF)
    coherent = {MODEL_PATH: mdl, f"models/{MODEL_KEY}.dx80.vtx": bytes(stale)}
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in coherent}
    closure = load_source_closure(index, MODEL_KEY, read_bytes=lambda _i, key: coherent.get(key))
    assert closure.primary_vtx is not None
    assert closure.disagreeing_variants[0].admitted is True

    struct.pack_into("<H", stale, 20, 9)               # a vertex the MDL pool does not hold
    struct.pack_into("<H", stale, 68, 9)
    incoherent = {MODEL_PATH: mdl, f"models/{MODEL_KEY}.dx80.vtx": bytes(stale)}
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in incoherent}
    closure = load_source_closure(index, MODEL_KEY, read_bytes=lambda _i, key: incoherent.get(key))
    assert closure.primary_vtx is None
    assert closure.disagreeing_variants[0].reason == "checksum-and-topology-disagree-with-mdl"


def test_a_missing_mdl_is_refused_rather_than_published_empty() -> None:
    with pytest.raises(ModelSourceError, match="missing required mdl"):
        load_source_closure({}, MODEL_KEY, read_bytes=lambda _i, _k: None)


# --- byte ledger ----------------------------------------------------------------------------


def test_every_mdl_byte_is_claimed_exactly_once() -> None:
    mdl, _topology = _minimal_mdl_vtx()
    row = coverage.cover_mdl(MODEL_PATH, mdl)
    assert row["coveragePercent"] == 100.0
    assert row["accountedBytes"] == row["byteLength"] == len(mdl)
    assert sum(row["stateBytes"].values()) == len(mdl)
    cursor = 0
    for entry in row["ranges"]:
        assert entry["offset"] == cursor
        cursor += entry["length"]
    assert cursor == len(mdl)


def test_every_vtx_byte_is_claimed_exactly_once() -> None:
    _mdl, topology = _minimal_mdl_vtx()
    row = coverage.cover_vtx(f"models/{MODEL_KEY}.dx80.vtx", topology, variant="vtx-dx80")
    assert row["accountedBytes"] == len(topology)
    assert row["coveragePercent"] == 100.0


def test_a_zero_gap_is_verified_and_accounted_as_padding() -> None:
    ledger = coverage.ModelLedger("source.bin", b"A\0\0", padding_owner="mdl.padding")
    ledger.claim(0, 1, "mapped", "value")
    row = ledger.finish()
    assert row["accountedBytes"] == 3
    assert row["stateBytes"]["padding-zero"] == 2
    assert row["ranges"][-1]["owner"] == "mdl.padding"


def test_an_unclaimed_non_zero_byte_aborts_publication() -> None:
    ledger = coverage.ModelLedger("source.bin", b"\0\x7f")
    ledger.claim(0, 1, "mapped", "header")
    with pytest.raises(coverage.ModelByteCoverageError, match="unclaimed non-zero byte"):
        ledger.finish()


def test_a_zero_state_claim_over_a_non_zero_byte_is_refused() -> None:
    ledger = coverage.ModelLedger("source.bin", b"\x01\x02")
    with pytest.raises(coverage.ModelByteCoverageError, match="non-zero bytes as reserved-zero"):
        ledger.claim(0, 2, "reserved-zero", "reservedField")


def test_two_lods_may_share_one_declared_range_and_nothing_else_may() -> None:
    ledger = coverage.ModelLedger("mesh.vtx", bytes(44))
    ledger.array(4, 1, 20, "lod[0].mesh[0].stripGroups")
    ledger.array(4, 1, 20, "lod[1].mesh[0].stripGroups", allow_existing=True)
    assert len(ledger.ranges) == 1
    with pytest.raises(coverage.ModelByteCoverageError, match="overlaps"):
        ledger.claim(8, 4, "mapped", "somethingElse")


def test_a_record_that_reads_inside_another_claimed_record_shares_its_range() -> None:
    """One retail strip's bone-state array falls inside the material-replacement list."""

    ledger = coverage.ModelLedger("mesh.vtx", bytes(16))
    ledger.claim(0, 8, "mapped", "vtx.materialReplacements")
    ledger.claim(0, 4, "mapped", "vtx.strips[0].boneStateChanges", allow_existing=True)
    assert [entry["owner"] for entry in ledger.ranges] == ["vtx.materialReplacements"]
    with pytest.raises(coverage.ModelByteCoverageError, match="overlaps"):
        ledger.claim(4, 8, "mapped", "vtx.strips[1].boneStateChanges", allow_existing=True)


def test_an_alignment_zero_does_not_end_the_unreferenced_string_walk() -> None:
    payload = b"AAAA\0\0TRFHandtop\0TRFHandbottom\0"
    ledger = coverage.ModelLedger("tail.bin", payload)
    ledger.claim(0, 5, "mapped-string", "mdl.skinTable")
    coverage._cover_unreferenced_strings(ledger, payload)
    row = ledger.finish()
    owners = [entry["owner"] for entry in row["ranges"]]
    assert sum(1 for owner in owners if owner.startswith("mdl.unreferencedString@")) == 2
    assert row["stateBytes"]["padding-zero"] == 1


def test_the_header_name_is_its_own_ledger_owner() -> None:
    mdl, _topology = _minimal_mdl_vtx()
    row = coverage.cover_mdl(MODEL_PATH, mdl)
    owners = {entry["owner"] for entry in row["ranges"]}
    assert "mdl.header" in owners and "mdl.header.name" in owners
    name = next(entry for entry in row["ranges"] if entry["owner"] == "mdl.header.name")
    assert (name["offset"], name["state"]) == (12, "mapped-string")


def test_the_compiler_eof_trailer_is_a_mapped_record_and_not_a_gap() -> None:
    payload = b"skin\0" + bytes.fromhex("64001100516e4462546d")
    ledger = coverage.ModelLedger("tail.bin", payload)
    ledger.claim(0, 5, "mapped-string", "mdl.textures[9].name")
    coverage._cover_retained_mdl_payloads(ledger, payload, bone_count=1)
    row = ledger.finish()
    assert row["ranges"][-1]["owner"] == "mdl.compilerTrailerQnDbTm"
    assert coverage.compiler_trailer(payload)["pathOffset"] == 0x00110064


def _two_ledge_solid() -> bytes:
    """One solid whose two leaf ledges reach one point array at different extents."""

    body = bytearray(48)                                    # solid header
    struct.pack_into("<f", body, 24, 1.0)                   # upper limit radius
    struct.pack_into("<i", body, 32, 48)                    # ledge-tree root, body relative
    body[44:48] = b"IVPS"
    root, left, right = 48, 76, 104
    ledge_a, ledge_b, points = 132, 180, 212
    body.extend(bytes(points + 4 * 16 - len(body)))
    struct.pack_into("<2i", body, root, right - root, 0)    # inner node: two children
    struct.pack_into("<2i", body, left, 0, ledge_a - left)
    struct.pack_into("<2i", body, right, 0, ledge_b - right)
    struct.pack_into("<iiIhh", body, ledge_a, points - ledge_a, 0, 0, 2, 0)
    for triangle, corners in enumerate(((0, 1, 2), (0, 2, 3))):
        record = ledge_a + 16 + triangle * 16
        struct.pack_into("<I", body, record, 0)
        for edge, corner in enumerate(corners):
            struct.pack_into("<I", body, record + 4 + edge * 4, corner)
    struct.pack_into("<iiIhh", body, ledge_b, points - ledge_b, 0, 0, 1, 0)
    struct.pack_into("<I", body, ledge_b + 16, 0)
    for edge, corner in enumerate((0, 1, 2)):
        struct.pack_into("<I", body, ledge_b + 20 + edge * 4, corner)
    for index in range(4):
        struct.pack_into("<4f", body, points + index * 16, float(index), 0.0, 0.0, 1.0)
    struct.pack_into("<I", body, 28, len(body) << 8)

    data = bytearray(16)
    struct.pack_into("<4i", data, 0, 16, 0, 1, 7)
    data += struct.pack("<i", len(body)) + body
    data += b'solid { "index" "0" "surfaceprop" "metal" }\n'
    return bytes(data)


def test_two_phy_ledges_that_share_one_point_cloud_claim_it_once() -> None:
    """A prop solid reaches one point array from several ledges, at different extents."""

    row = coverage.cover_phy(f"models/{MODEL_KEY}.phy", _two_ledge_solid())
    assert row["coveragePercent"] == 100.0
    owners = [entry["owner"] for entry in row["ranges"]]
    assert sum(1 for owner in owners if ".points@" in owner) == 1
    claimed = next(entry for entry in row["ranges"] if ".points@" in entry["owner"])
    assert claimed["length"] == 4 * 16, "the shared array is claimed to its widest extent"


# --- decode ---------------------------------------------------------------------------------


def test_the_minimal_closure_decodes_end_to_end() -> None:
    _closure, unit = _unit()
    assert unit.asset == f"vtmb:model:{MODEL_KEY}"
    assert (unit.family, unit.shape) == ("scenery", "static")
    assert unit.roles == []
    assert len(unit.bones) == 1 and len(unit.lods) == 2
    assert unit.materials[0]["material"] == "vtmb:material:body"
    assert unit.header["flags"] == {"value": 0x10, "staticProp": True}


def test_v2531_declares_no_header_keyvalues_region_and_says_so() -> None:
    _closure, unit = _unit()
    assert unit.key_values is None
    reasons = {row["reason"] for row in unit.omissions}
    assert "v2531-has-no-header-keyvalues-region" in reasons


def test_a_texture_name_that_resolves_to_no_vmt_keeps_a_sentinel_with_evidence() -> None:
    mdl, topology = _minimal_mdl_vtx()
    files = {MODEL_PATH: mdl, f"models/{MODEL_KEY}.dx80.vtx": topology}
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in files}
    closure = load_source_closure(index, MODEL_KEY, read_bytes=lambda _i, key: files.get(key))
    unit = decode_model(
        closure, index, read_bytes=lambda _i, key: files.get(key), anorms=[],
        surface_properties=frozenset(),
    )
    assert unit.materials[0]["material"] == "vtmb:missing-material:0:body"
    proven = [row for row in unit.omitted_proven if row["path"] == "materialBindings.slots[0]"]
    assert proven[0]["reason"] == "studio-texture-name-has-no-vmt"
    assert proven[0]["candidates"] == ["materials/body.vmt"]
    assert not [row for row in unit.dependencies if row["role"] == "material"]


def test_one_vmt_reached_from_two_slots_is_declared_once() -> None:
    mdl, topology = _minimal_mdl_vtx(textures=2)
    files = {
        MODEL_PATH: mdl,
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        "materials/body.vmt": b"VertexLitGeneric\n{\n}\n",
    }
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in files}
    closure = load_source_closure(index, MODEL_KEY, read_bytes=lambda _i, key: files.get(key))
    unit = decode_model(
        closure, index, read_bytes=lambda _i, key: files.get(key), anorms=[],
        surface_properties=frozenset(),
    )
    assert [row["material"] for row in unit.materials] == ["vtmb:material:body"] * 2
    assert [row["asset"] for row in unit.dependencies if row["role"] == "material"] == [
        "vtmb:material:body"
    ]


def test_the_material_dependency_carries_the_bytes_the_decode_depended_on() -> None:
    _closure, unit = _unit()
    row = next(row for row in unit.dependencies if row["role"] == "material")
    assert row["asset"] == "vtmb:material:body"
    assert row["sourcePath"] == "materials/body.vmt" and row["resolved"] is True
    assert row["byteLength"] > 0 and len(row["sha256"]) == 64


def test_a_material_surfaceprop_becomes_a_surface_property_dependency() -> None:
    _closure, unit = _unit()
    surfaces = [row for row in unit.dependencies if row["role"] == "surface-property"]
    assert [row["asset"] for row in surfaces] == ["vtmb:surface-property:metal"]
    assert surfaces[0]["sourcePath"] == "scripts/surfaceproperties.txt#metal"


def test_an_include_model_the_install_lacks_is_unresolved_under_its_own_identity() -> None:
    mdl, topology = _minimal_mdl_vtx()
    mdl = bytearray(mdl)
    include = len(mdl)
    mdl.extend(bytes(116))
    struct.pack_into("<2i", mdl, 404, 1, include)
    struct.pack_into("<i", mdl, include, 116)               # filename, record relative
    struct.pack_into("<i", mdl, include + 16, 172)          # bone remap, record relative
    mdl.extend(b"shared/male/npc_allsequences.mdl\0")
    mdl.extend(bytes(-len(mdl) % 4))
    remap = include + 172
    mdl.extend(bytes(max(0, remap + 56 - len(mdl))))
    struct.pack_into("<h2B2h", mdl, remap, -1, 0, 0, -1, -1)
    struct.pack_into("<i", mdl, 140, len(mdl))
    files = {
        MODEL_PATH: bytes(mdl),
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        "materials/body.vmt": b"VertexLitGeneric\n{\n}\n",
    }
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in files}
    closure = load_source_closure(index, MODEL_KEY, read_bytes=lambda _i, key: files.get(key))
    unit = decode_model(
        closure, index, read_bytes=lambda _i, key: files.get(key), anorms=[],
        surface_properties=frozenset(),
    )
    # An include is the unit's own structure, so an install that lacks it produces an unresolved
    # row under the include's real identity -- not a sentinel, which would claim the model kind's
    # own rules make it unreachable and would carry no dependency row at all.
    assert unit.include_models[0]["asset"] == "vtmb:model:shared/male/npc_allsequences"
    assert unit.include_models[0]["resolved"] is False
    assert [row["row"] for row in unit.anomalies] == ["stale-include-path"]
    assert [row["reason"] for row in unit.unresolved] == ["include-model-is-not-installed"]
    rows = [row for row in unit.dependencies if row["role"] == "model"]
    assert [(row["asset"], row["resolved"]) for row in rows] == [
        ("vtmb:model:shared/male/npc_allsequences", False)
    ]


def test_an_expression_table_is_a_reference_and_never_inlined() -> None:
    rows = expressions.selected_tables(
        "npc/unique/downtown/lacroix/lacroix",
        {"expressions/lacroix_phonemes.vfe": ("vpk", ("p", 0, 1)),
         "expressions/phonemes.vfe": ("vpk", ("p", 0, 1))},
    )
    by_class = {row["class"]: row for row in rows}
    assert by_class["phonemes"]["asset"] == "vtmb:expression-table:lacroix_phonemes"
    assert by_class["phonemes"]["resolved"] is True
    assert by_class["expressions"]["asset"] == (
        "vtmb:missing-expression-table:lacroix_expressions"
    )
    assert by_class["expressions"]["fallbacks"][0]["resolved"] is True
    dependencies = expressions.table_dependencies(rows)
    assert {row["asset"] for row in dependencies} == {
        "vtmb:expression-table:lacroix_phonemes", "vtmb:expression-table:phonemes"
    }
    assert expressions.omitted_selections(rows)[0]["reason"] == (
        "model-stem-has-no-expression-table"
    )


def test_a_degenerate_authored_normal_is_replaced_and_the_vertex_is_named() -> None:
    mdl, topology = _minimal_mdl_vtx()
    mdl = bytearray(mdl)
    struct.pack_into("<3f", mdl, 660 + 284 + 24, 0.0, 0.0, 0.0)
    lods = vtx.decode_lods(bytes(mdl), topology, variant="vtx-dx80", anorms=[])
    from elysium_pipeline.formats.model_glb.decode import _repair_degenerate_normals

    anomalies = _repair_degenerate_normals(lods)
    assert [row["row"] for row in anomalies][:1] == ["degenerate-normal"]
    assert lods[0]["primitives"][0]["normals"][0] != (0.0, 0.0, 0.0)


def test_a_sound_event_names_a_dependency_and_an_absent_one_stays_unresolved() -> None:
    from elysium_pipeline.formats.model_glb.decode import _event_dependencies

    sequences = [
        {"events": [{"event": 1004, "options": "player\\Footsteps\\Step1"},
                    {"event": 5111, "options": "particles/blood_spurt.txt"},
                    {"event": 4020, "options": "missing/line"}]}
    ]
    index = {"sound/player/footsteps/step1.wav": 1, "particles/blood_spurt.txt": 1}
    rows = _event_dependencies(sequences, index)
    assert [(row["role"], row["asset"], row["resolved"]) for row in rows] == [
        ("sound", "vtmb:sound:player/footsteps/step1.wav", True),
        ("particle", "vtmb:particle:blood_spurt", True),
        ("sound", "vtmb:sound:missing/line.wav", False),
    ]


def test_the_secondary_motion_preset_is_carried_as_typed_but_unidentified() -> None:
    mdl, topology = _minimal_mdl_vtx()
    mdl = bytearray(mdl)
    record = len(mdl)
    mdl.extend(bytes(28))
    struct.pack_into("<2i", mdl, 396, 1, record)
    struct.pack_into("<2i", mdl, record, 0, -1)
    struct.pack_into("<5f", mdl, record + 8, 1.0, 2.0, 3.0, 4.0, 5.0)
    struct.pack_into("<i", mdl, 140, len(mdl))
    files = {
        MODEL_PATH: bytes(mdl),
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        "materials/body.vmt": b"VertexLitGeneric\n{\n}\n",
    }
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in files}
    closure = load_source_closure(index, MODEL_KEY, read_bytes=lambda _i, key: files.get(key))
    unit = decode_model(
        closure, index, read_bytes=lambda _i, key: files.get(key), anorms=[],
        surface_properties=frozenset(),
    )
    assert [row["path"] for row in unit.typed_unidentified] == [
        "secondaryMotion[].unusedAuthoredPreset"
    ]
    assert unit.secondary_motion[0]["unusedAuthoredPreset"] == 1.0


def test_the_ragdoll_keyvalues_tail_is_typed_rather_than_kept_as_text() -> None:
    blocks, anomalies = physics._blocks(
        'ragdollconstraint {\n"parent" "0"\n"child" "3"\n"xmin" "-25.0"\n}\n\0'
    )
    assert anomalies == []
    assert physics._typed_values("ragdollconstraint", blocks[0]["values"]) == {
        "parent": 0, "child": 3, "xmin": -25.0
    }


# --- document -------------------------------------------------------------------------------


def test_the_document_states_the_contract_key_order_and_the_seam_generator() -> None:
    _closure, unit = _unit()
    document, _binary = model_glb.build_document(unit)
    assert document["asset"]["generator"] == "Elysium Model GLB Exporter"
    root = document["extensions"][EXTENSION]
    assert list(root)[:5] == [
        "schemaVersion", "identity", "sourceResolution", "dependencies", "coverage"
    ]
    assert root["schemaVersion"] == SCHEMA_VERSION == "2.2.0"
    assert EXTENSION in document["extensionsUsed"]
    assert EXTENSION in document["extensionsRequired"]


def test_a_unit_that_includes_no_bank_does_not_declare_the_model_reference() -> None:
    _closure, unit = _unit()
    document, _binary = model_glb.build_document(unit)
    assert "ELYSIUM_model_reference" not in document["extensionsUsed"]
    assert "ELYSIUM_material_reference" in document["extensionsUsed"]


def test_one_mesh_per_lod_and_the_scene_instances_lod_zero() -> None:
    _closure, unit = _unit()
    document, _binary = model_glb.build_document(unit)
    names = [mesh["name"] for mesh in document["meshes"]]
    assert names == [f"{MODEL_KEY}:lod0", f"{MODEL_KEY}:lod1"]
    mesh_nodes = [node for node in document["nodes"] if "mesh" in node]
    assert [node["mesh"] for node in mesh_nodes] == [0]
    assert document["scenes"][0]["nodes"][-1] == document["nodes"].index(mesh_nodes[0])


def test_the_joint_nodes_are_the_mdl_bone_order_and_carry_the_skin() -> None:
    _closure, unit = _unit()
    document, _binary = model_glb.build_document(unit)
    assert document["nodes"][0]["name"] == "root"
    assert document["skins"][0]["joints"] == [0]
    assert document["skins"][0]["skeleton"] == 0


def test_the_export_is_byte_identical_for_one_source_closure() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        first = _export(Path(temporary)).read_bytes()
        second = _export(Path(temporary)).read_bytes()
    assert first == second


def test_a_unit_the_corpus_index_back_filled_still_validates() -> None:
    """`identity.roles` is empty at export and written in place by the corpus index, in the
    model seam's own closed vocabulary; validation reads a published unit in both states."""

    from elysium_pipeline.formats.corpus_index_glb import backfill
    from elysium_pipeline.formats.model_glb.model import ROLES

    with tempfile.TemporaryDirectory() as temporary:
        destination = _export(Path(temporary))
        asset = "vtmb:model:" + MODEL_KEY
        roles = ["placed-prop", "static-prop"]
        assert [role for role in ROLES if role in roles] == roles
        assert backfill.rewrite(destination, asset, roles) is not None
        validation.validate(destination)
        backfill.rewrite(destination, asset, [{"from": "vtmb:map:tutorial", "role": "model"}])
        with pytest.raises(validation.ModelGlbValidationError):
            validation.validate(destination)


def test_the_public_exporter_writes_the_family_relative_path() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        destination = _export(Path(temporary))
        relative = destination.relative_to(temporary).as_posix()
        summary = validation.validate(destination)
    assert relative == f"{MODEL_KEY}.glb"
    assert summary["lods"] == 2 and summary["shape"] == "static"
    assert summary["byteCoveragePercent"] == 100.0
    assert summary["unresolved"] == summary["unsupported"] == 0


# --- validation -----------------------------------------------------------------------------


def test_a_model_with_no_bone_and_no_triangle_publishes_scene_less() -> None:
    """`models/null.mdl` declares a body part the runtime draws nothing from."""

    mdl, topology = _minimal_mdl_vtx()
    mdl = bytearray(mdl)
    struct.pack_into("<2i", mdl, 240, 0, 424)               # no bones
    mdl[424:600] = bytes(176)                               # the bone record and its name
    struct.pack_into("<3i", mdl, 660 + 136, 0, 224, 0)      # no meshes and no vertices
    struct.pack_into("<i", mdl, 660 + 152, 0)               # no tangents
    mdl[884:1124] = bytes(240)                              # the mesh, vertex and tangent payload
    topology = bytearray(topology)
    struct.pack_into("<2i", topology, 44, 0, 8)             # the model declares no LOD
    topology[52:188] = bytes(136)                           # the LOD, mesh and strip payload
    files = {
        MODEL_PATH: bytes(mdl),
        f"models/{MODEL_KEY}.dx80.vtx": bytes(topology),
    }
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in files}
    closure = load_source_closure(index, MODEL_KEY, read_bytes=lambda _i, key: files.get(key))
    unit = decode_model(
        closure, index, read_bytes=lambda _i, key: files.get(key), anorms=[],
        surface_properties=frozenset(),
    )
    document, binary = model_glb.build_document(unit)
    assert not any(
        key in document for key in ("scene", "scenes", "nodes", "skins", "meshes", "animations")
    )
    assert binary == b""
    summary = validation.validate_document(document, binary)
    assert (summary["bones"], summary["lods"], summary["meshes"]) == (0, 0, 0)


def test_a_scene_less_unit_that_declares_a_node_is_refused() -> None:
    _closure, unit = _unit()
    document, binary = model_glb.build_document(unit)
    root = document["extensions"][EXTENSION]
    root["mdl"]["bones"] = []
    root["vtx"]["lods"] = []
    with pytest.raises(validation.ModelGlbValidationError, match="scene-less unit declares no"):
        validation.validate_document(document, binary)


def test_the_validator_refuses_a_tampered_byte_ledger() -> None:
    _closure, unit = _unit()
    document, binary = model_glb.build_document(unit)
    ledger = document["extensions"][EXTENSION]["coverage"]["byteLedger"][0]
    ledger["ranges"][0]["length"] -= 1
    with pytest.raises(validation.ModelGlbValidationError, match="byte range"):
        validation.validate_document(document, binary)


def test_the_validator_refuses_a_ledger_whose_range_digest_was_rewritten() -> None:
    _closure, unit = _unit()
    document, binary = model_glb.build_document(unit)
    ledger = document["extensions"][EXTENSION]["coverage"]["byteLedger"][0]
    ledger["rangesSha256"] = "0" * 64
    with pytest.raises(validation.ModelGlbValidationError, match="digest disagrees"):
        validation.validate_document(document, binary)


def test_export_time_validation_rechecks_the_source_members() -> None:
    closure, unit = _unit()
    document, binary = model_glb.build_document(unit)
    changed = SourceMember(
        role="mdl",
        path=closure.mdl.path,
        data=b"changed-source",
        origin=origin_of(("loose", "C:/VTMB/Vampire/" + closure.mdl.path)),
    )
    with pytest.raises(validation.ModelGlbValidationError):
        validation.validate_document(
            document, binary, source_members=(changed, *closure.members()[1:])
        )


def test_the_validator_refuses_a_shape_that_contradicts_the_header() -> None:
    _closure, unit = _unit()
    document, binary = model_glb.build_document(unit)
    document["extensions"][EXTENSION]["identity"]["shape"] = "bank"
    with pytest.raises(validation.ModelGlbValidationError, match="contradicts the bytes"):
        validation.validate_document(document, binary)


def test_the_validator_refuses_a_reference_with_no_dependency_row() -> None:
    _closure, unit = _unit()
    document, binary = model_glb.build_document(unit)
    root = document["extensions"][EXTENSION]
    root["dependencies"] = [row for row in root["dependencies"] if row["role"] != "material"]
    with pytest.raises(validation.ModelGlbValidationError, match="carry no dependency row"):
        validation.validate_document(document, binary)


def test_the_validator_refuses_a_sentinel_that_produced_a_dependency_row() -> None:
    _closure, unit = _unit()
    document, binary = model_glb.build_document(unit)
    root = document["extensions"][EXTENSION]
    root["dependencies"].append(
        {"role": "material", "asset": "vtmb:missing-material:0:body",
         "sourcePath": "materials/body.vmt", "resolved": False}
    )
    with pytest.raises(validation.ModelGlbValidationError, match="sentinel identity"):
        validation.validate_document(document, binary)


def test_the_validator_refuses_a_split_rotation_list_that_disagrees_with_the_flags() -> None:
    mdl, topology = _minimal_mdl_vtx()
    mdl = bytearray(mdl)
    struct.pack_into("<i", mdl, 424 + 136, 0x2)             # StudioBone.flags@136
    files = {
        MODEL_PATH: bytes(mdl),
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        "materials/body.vmt": b"VertexLitGeneric\n{\n}\n",
    }
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in files}
    closure = load_source_closure(index, MODEL_KEY, read_bytes=lambda _i, key: files.get(key))
    unit = decode_model(
        closure, index, read_bytes=lambda _i, key: files.get(key), anorms=[],
        surface_properties=frozenset(),
    )
    document, binary = model_glb.build_document(unit)
    assert document["extensions"][EXTENSION]["mdl"]["splitRotationBones"] == [
        {"bone": 0, "name": "root", "rotation": "model-space",
         "translation": "parent-attached"}
    ]
    document["extensions"][EXTENSION]["mdl"]["splitRotationBones"] = []
    with pytest.raises(validation.ModelGlbValidationError, match="splitRotationBones"):
        validation.validate_document(document, binary)


def test_the_validator_refuses_a_generator_another_seam_wrote() -> None:
    _closure, unit = _unit()
    document, binary = model_glb.build_document(unit)
    document["asset"]["generator"] = "Elysium Character GLB Exporter"
    with pytest.raises(validation.ModelGlbValidationError, match="asset.generator"):
        validation.validate_document(document, binary)


def test_a_written_unit_round_trips_through_standalone_validation() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        destination = _export(Path(temporary))
        summary = validation.validate(destination)
        document, binary = validation.read_glb(destination)
    assert summary["asset"] == f"vtmb:model:{MODEL_KEY}"
    assert summary["sourceBytes"] == summary["accountedBytes"]
    assert summary["family"] == "scenery"
    assert validation.warnings_for(summary) == []
    assert document["extensions"][EXTENSION]["identity"]["roles"] == []
    assert binary, "a unit with geometry carries a BIN chunk"


# --- evidence, offsets and references ---------------------------------------------------------


def _mdl_with_unreferenced_string() -> tuple[bytes, bytes]:
    """The minimal pair with an ASCII string inside the image no declared offset addresses."""

    mdl, topology = _minimal_mdl_vtx()
    mdl = bytearray(mdl)
    mdl.extend(b"models/retained/donor.mdl\0")
    struct.pack_into("<i", mdl, 140, len(mdl))
    return bytes(mdl), topology


def _install_with(files: dict[str, bytes]):
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in files}
    return index, (lambda _index, key: files.get(key))


def test_every_omitted_proven_range_carries_the_reason_it_stands_for() -> None:
    mdl, _topology = _mdl_with_unreferenced_string()
    row = coverage.cover_mdl(MODEL_PATH, mdl)
    omitted = [entry for entry in row["ranges"] if entry["state"] == "omitted-proven"]
    assert [entry["owner"] for entry in omitted] == ["mdl.unreferencedString@1124"]
    assert coverage.omitted_proven_rows([row]) == [
        {
            "path": "mdl.unreferencedString@1124",
            "sourcePath": MODEL_PATH,
            "reason": "ascii-string-no-declared-offset-addresses",
        }
    ]


def test_an_omitted_proven_owner_the_seam_never_classified_aborts_publication() -> None:
    with pytest.raises(coverage.ModelByteCoverageError, match="no recorded reason"):
        coverage.omission_reason("mdl.somethingNobodyClassified@12")


def test_the_validator_refuses_an_omitted_proven_range_with_no_reason_row() -> None:
    mdl, topology = _mdl_with_unreferenced_string()
    index, read_bytes = _install_with({
        MODEL_PATH: mdl,
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        "materials/body.vmt": b"VertexLitGeneric\n{\n}\n",
    })
    closure = load_source_closure(index, MODEL_KEY, read_bytes=read_bytes)
    unit = decode_model(
        closure, index, read_bytes=read_bytes, anorms=[], surface_properties=frozenset()
    )
    document, binary = model_glb.build_document(unit)
    coverage_block = document["extensions"][EXTENSION]["coverage"]
    assert {row["path"] for row in coverage_block["omittedProven"]} == {
        "mdl.unreferencedString@1124"
    }
    validation.validate_document(document, binary)
    coverage_block["omittedProven"] = []
    with pytest.raises(validation.ModelGlbValidationError, match="carries no reason row"):
        validation.validate_document(document, binary)


def test_the_ledger_names_one_owner_per_bone_hitbox_sequence_and_mesh_record() -> None:
    mdl, _topology = _minimal_mdl_vtx()
    owners = {entry["owner"] for entry in coverage.cover_mdl(MODEL_PATH, mdl)["ranges"]}
    assert "mdl.bones[0]" in owners and "mdl.bones" not in owners
    assert "mdl.bodyParts[0].models[0].meshes[0]" in owners


def test_a_record_read_from_an_offset_keeps_that_offset() -> None:
    mdl, topology = _minimal_mdl_vtx()
    mdl = bytearray(mdl)
    attachment, pose = len(mdl), len(mdl) + 60
    controller, mouth, motion = pose + 20, pose + 40, pose + 60
    mdl.extend(bytes(60 + 20 + 20 + 20 + 28))
    struct.pack_into("<2i", mdl, 328, 1, attachment)        # one attachment
    struct.pack_into("<2i", mdl, 384, 1, pose)              # one pose parameter
    struct.pack_into("<2i", mdl, 352, 1, controller)        # one flex controller
    struct.pack_into("<2i", mdl, 376, 1, mouth)             # one mouth
    struct.pack_into("<2i", mdl, 396, 1, motion)            # one secondary-motion chain
    for record, relative in ((attachment, 60), (pose, 20), (controller, 20)):
        struct.pack_into("<i", mdl, record, relative)
    struct.pack_into("<i", mdl, controller + 4, 16 + 20)
    struct.pack_into("<2i", mdl, motion, 0, -1)
    struct.pack_into("<5f", mdl, motion + 8, 1.0, 2.0, 3.0, 4.0, 5.0)
    names = len(mdl)
    mdl.extend(b"attach\0")
    struct.pack_into("<i", mdl, attachment, names - attachment)
    struct.pack_into("<i", mdl, pose, len(mdl) - pose)
    mdl.extend(b"body_yaw\0")
    struct.pack_into("<i", mdl, controller, len(mdl) - controller)
    mdl.extend(b"blink\0")
    struct.pack_into("<i", mdl, controller + 4, len(mdl) - controller)
    mdl.extend(b"upper\0")
    struct.pack_into("<i", mdl, 140, len(mdl))
    index, read_bytes = _install_with({
        MODEL_PATH: bytes(mdl),
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        "materials/body.vmt": b"VertexLitGeneric\n{\n}\n",
    })
    closure = load_source_closure(index, MODEL_KEY, read_bytes=read_bytes)
    unit = decode_model(
        closure, index, read_bytes=read_bytes, anorms=[], surface_properties=frozenset()
    )
    assert unit.attachments[0]["sourceOffset"] == attachment
    assert unit.pose_parameters[0]["sourceOffset"] == pose
    assert unit.facial["controllers"][0]["sourceOffset"] == controller
    assert unit.facial["mouths"][0]["sourceOffset"] == mouth
    assert unit.secondary_motion[0]["sourceOffset"] == motion
    document, binary = model_glb.build_document(unit)
    validation.validate_document(document, binary, source_members=closure.members())
    del document["extensions"][EXTENSION]["mdl"]["attachments"][0]["sourceOffset"]
    with pytest.raises(validation.ModelGlbValidationError, match="does not keep the offset"):
        validation.validate_document(document, binary)


def test_a_physics_ledge_keeps_the_offset_its_ledger_range_paid_for() -> None:
    """`phy.solids[i].ledges[j].header` and the decoded ledge name one place."""

    ledge = 64
    points = ledge + 16 + 4 * 16
    data = bytearray(points + 4 * 16)
    struct.pack_into("<iiIhh", data, ledge, points - ledge, 0, 0, 4, 0)
    for triangle, corners in enumerate(((0, 1, 2), (0, 1, 3), (0, 2, 3), (1, 2, 3))):
        record = ledge + 16 + triangle * 16
        for edge, corner in enumerate(corners):
            struct.pack_into("<I", data, record + 4 + edge * 4, corner)
    for index in range(4):
        struct.pack_into("<4f", data, points + index * 16, float(index), 1.0, 2.0, 3.0)
    assert physics._ledge(bytes(data), ledge)["sourceOffset"] == ledge
    owners = {
        entry["owner"]
        for entry in coverage.cover_phy("models/synthetic.phy", _two_ledge_solid())["ranges"]
        if entry["owner"].endswith(".header")
    }
    assert owners >= {"phy.solids[0].ledges[0].header", "phy.solids[0].ledges[1].header"}


def test_a_surface_a_bone_names_carries_its_own_dependency_row() -> None:
    mdl, topology = _minimal_mdl_vtx()
    mdl = bytearray(mdl)
    name = len(mdl)
    mdl.extend(b"flesh\0")
    struct.pack_into("<i", mdl, 424 + 152, name - 424)      # StudioBone.surfaceProperty@152
    struct.pack_into("<i", mdl, 140, len(mdl))
    index, read_bytes = _install_with({
        MODEL_PATH: bytes(mdl),
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        "materials/body.vmt": b"VertexLitGeneric\n{\n}\n",
        "scripts/surfaceproperties.txt": b'"flesh"\n{\n"friction" "0.8"\n}\n',
    })
    closure = load_source_closure(index, MODEL_KEY, read_bytes=read_bytes)
    unit = decode_model(
        closure, index, read_bytes=read_bytes, anorms=[],
        surface_properties=surface_property_names(index, read_bytes=read_bytes),
    )
    document, binary = model_glb.build_document(unit)
    root = document["extensions"][EXTENSION]
    assert [row["asset"] for row in root["dependencies"] if row["role"] == "surface-property"] == [
        "vtmb:surface-property:flesh"
    ]
    root["dependencies"] = [
        row for row in root["dependencies"] if row["role"] != "surface-property"
    ]
    with pytest.raises(validation.ModelGlbValidationError, match="carry no dependency row"):
        validation.validate_document(document, binary)


def test_a_sound_or_particle_an_event_names_carries_a_dependency_row() -> None:
    root = {
        "mdl": {
            "sequences": [
                {"events": [{"event": 1004, "options": "player\\Footsteps\\Step1"},
                            {"event": 5111, "options": "particles/blood_spurt.txt"}]}
            ]
        },
        "dependencies": [
            {"role": "sound", "asset": "vtmb:sound:player/footsteps/step1.mp3",
             "sourcePath": "sound/player/footsteps/step1.mp3", "resolved": True},
            {"role": "particle", "asset": "vtmb:particle:blood_spurt",
             "sourcePath": "particles/blood_spurt.txt", "resolved": True},
        ],
    }
    validation._check_dependencies(root)
    root["dependencies"] = [row for row in root["dependencies"] if row["role"] != "particle"]
    with pytest.raises(
        validation.ModelGlbValidationError, match="carries no particle dependency row"
    ):
        validation._check_dependencies(root)


def test_coverage_mapped_names_only_the_sections_the_unit_carries() -> None:
    _closure, unit = _unit()
    document, _binary = model_glb.build_document(unit)
    mapped = document["extensions"][EXTENSION]["coverage"]["mapped"]
    assert document["extensions"][EXTENSION]["physics"] is None
    assert "physics" not in mapped and "cloth" not in mapped
    assert mapped[:4] == ["identity", "sourceResolution", "coordinateTransform", "mdl"]


def test_the_validator_refuses_a_ledger_that_renames_a_declared_table() -> None:
    from elysium_pipeline.formats.unit_contract import ranges_sha256

    closure, unit = _unit()
    document, binary = model_glb.build_document(unit)
    ledger = document["extensions"][EXTENSION]["coverage"]["byteLedger"][0]
    entry = next(row for row in ledger["ranges"] if row["owner"] == "mdl.bones[0]")
    entry["owner"] = "mdl.somethingElse"
    ledger["rangesSha256"] = ranges_sha256(
        ledger["sourcePath"], ledger["byteLength"], ledger["ranges"]
    )
    with pytest.raises(validation.ModelGlbValidationError, match="not by 'mdl.bones\\['"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_contract_ledger_error_type_is_the_one_the_seam_raises() -> None:
    assert issubclass(coverage.ModelByteCoverageError, ByteLedgerError)

# --- the review's second round ---------------------------------------------------------------


def _twin_install():
    """A closure that ships both topology variants, agreeing LOD for LOD."""

    mdl, topology = _minimal_mdl_vtx()
    return _install_with({
        MODEL_PATH: mdl,
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        f"models/{MODEL_KEY}.dx7_2bone.vtx": topology,
        "materials/body.vmt": b"VertexLitGeneric\n{\n}\n",
    })


def _twin_unit():
    index, read_bytes = _twin_install()
    closure = load_source_closure(index, MODEL_KEY, read_bytes=read_bytes)
    return closure, decode_model(
        closure, index, read_bytes=read_bytes, anorms=[], surface_properties=frozenset()
    )


def test_the_legacy_twin_claims_mapped_only_for_the_bytes_it_publishes() -> None:
    _closure, unit = _twin_unit()
    legacy = next(row for row in unit.byte_ledger if row["sourcePath"].endswith(".dx7_2bone.vtx"))
    primary = next(row for row in unit.byte_ledger if row["sourcePath"].endswith(".dx80.vtx"))
    assert legacy["coveragePercent"] == 100.0
    mapped = {entry["owner"] for entry in legacy["ranges"] if entry["state"] == "mapped"}
    assert mapped == {
        "vtx.vtx-dx7-2bone.header", "vtx.vtx-dx7-2bone.materialReplacements"
    }, "only the header and the replacement list of the superseded twin reach the product"
    topology = [
        entry for entry in legacy["ranges"]
        if entry["owner"].startswith("vtx.vtx-dx7-2bone.bodyParts")
    ]
    assert topology and all(entry["state"] == "omitted-proven" for entry in topology)
    # The primary variant publishes every LOD it carries, so none of its topology is omitted.
    assert not [entry for entry in primary["ranges"] if entry["state"] == "omitted-proven"]
    assert legacy["stateBytes"]["omitted-proven"] > legacy["stateBytes"]["mapped"]


def test_the_superseded_legacy_topology_carries_the_reason_it_stands_for() -> None:
    _closure, unit = _twin_unit()
    rows = [
        row for row in unit.omitted_proven
        if str(row.get("sourcePath", "")).endswith(".dx7_2bone.vtx")
    ]
    assert rows and {row["reason"] for row in rows} == {
        "legacy-vtx-equivalent-lod-the-primary-variant-publishes"
    }
    assert coverage.omission_reason("vtx.vtx-dx7-2bone.bodyParts[0].models[0].lods[1]") == (
        "legacy-vtx-equivalent-lod-the-primary-variant-publishes"
    )


def test_a_lod_only_the_legacy_twin_carries_is_published_and_stays_mapped() -> None:
    """The alternate keeps mapped for a LOD the primary variant does not carry."""

    _mdl, topology = _minimal_mdl_vtx()
    row = coverage.cover_vtx(
        f"models/{MODEL_KEY}.dx7_2bone.vtx", topology,
        variant="vtx-dx7-2bone", published_lods=frozenset({1}),
    )
    by_state = {entry["owner"]: entry["state"] for entry in row["ranges"]}
    assert by_state["vtx.vtx-dx7-2bone.bodyParts[0].models[0].lods[1]"] == "mapped"
    assert by_state["vtx.vtx-dx7-2bone.bodyParts[0].models[0].lods[0]"] == "omitted-proven"
    assert by_state["vtx.vtx-dx7-2bone.bodyParts[0]"] == "mapped"
    assert row["coveragePercent"] == 100.0


def test_a_superseded_record_leaves_the_bytes_a_published_one_already_paid_for() -> None:
    """Two LODs of the legacy twin share a table; the LOD that publishes it keeps the grading."""

    ledger = coverage.ModelLedger("models/synthetic.dx7_2bone.vtx", bytes(16))
    ledger.claim(4, 4, "mapped", "vtx.vtx-dx7-2bone.published")
    ledger.claim_free(0, 12, "omitted-proven", "vtx.vtx-dx7-2bone.superseded")
    assert ledger.free_spans(0, 16) == [(12, 16)]
    assert [
        (entry["offset"], entry["length"], entry["state"], entry["owner"])
        for entry in ledger.ranges
    ] == [
        (0, 4, "omitted-proven", "vtx.vtx-dx7-2bone.superseded"),
        (4, 4, "mapped", "vtx.vtx-dx7-2bone.published"),
        (8, 4, "omitted-proven", "vtx.vtx-dx7-2bone.superseded"),
    ]


def test_the_validator_refuses_a_comparison_that_renames_the_published_lods() -> None:
    closure, unit = _twin_unit()
    document, binary = model_glb.build_document(unit)
    validation.validate_document(document, binary, source_members=closure.members())
    document["extensions"][EXTENSION]["vtx"]["comparison"]["alternateOnlyLods"] = [1]
    with pytest.raises(validation.ModelGlbValidationError, match="alternateOnlyLods"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_bind_transform_the_joint_node_carries_is_not_stated_again() -> None:
    _closure, unit = _unit()
    document, _binary = model_glb.build_document(unit)
    bone = document["extensions"][EXTENSION]["mdl"]["bones"][0]
    assert "position" not in bone and "rotation" not in bone
    assert "storedPosition" not in bone and "storedRotation" not in bone
    assert document["nodes"][0]["translation"] == [0.0, 0.0, 0.0]
    assert document["nodes"][0]["rotation"] == [0.0, 0.0, 0.0, 1.0]
    assert bone["sourceOffset"] == 424 and bone["name"] == "root"


def test_a_stored_bind_the_node_cannot_recover_stays_in_the_record() -> None:
    mdl, topology = _minimal_mdl_vtx()
    patched = bytearray(mdl)
    struct.pack_into("<4f", patched, 424 + 44, 0.0, 0.0, 0.0, 0.5)   # a quaternion of length 0.5
    index, read_bytes = _install_with({
        MODEL_PATH: bytes(patched),
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        "materials/body.vmt": b"VertexLitGeneric\n{\n}\n",
    })
    closure = load_source_closure(index, MODEL_KEY, read_bytes=read_bytes)
    unit = decode_model(
        closure, index, read_bytes=read_bytes, anorms=[], surface_properties=frozenset()
    )
    document, binary = model_glb.build_document(unit)
    bone = document["extensions"][EXTENSION]["mdl"]["bones"][0]
    assert bone["storedRotation"] == [0.0, 0.0, 0.0, 0.5]
    assert bone["storedTransformNote"] == model_glb.STORED_TRANSFORM_NOTE
    assert document["nodes"][0]["rotation"] == [0.0, 0.0, 0.0, 1.0]
    validation.validate_document(document, binary, source_members=closure.members())


def test_the_bytes_past_the_declared_image_are_stated_once_and_graded_mapped() -> None:
    mdl, topology = _minimal_mdl_vtx()
    patched = mdl + b"\n"
    row = coverage.cover_mdl(MODEL_PATH, patched)
    entry = row["ranges"][-1]
    assert (entry["owner"], entry["state"], entry["length"]) == (
        "mdl.header.trailingPatchWhitespace", "mapped", 1
    )
    index, read_bytes = _install_with({
        MODEL_PATH: patched,
        f"models/{MODEL_KEY}.dx80.vtx": topology,
        "materials/body.vmt": b"VertexLitGeneric\n{\n}\n",
    })
    closure = load_source_closure(index, MODEL_KEY, read_bytes=read_bytes)
    unit = decode_model(
        closure, index, read_bytes=read_bytes, anorms=[], surface_properties=frozenset()
    )
    anomaly = next(row for row in unit.anomalies if row["row"] == "header-length-mismatch")
    assert "trailingBytes" not in anomaly
    assert anomaly["field"] == "mdl.header.trailingPatchWhitespace"
    assert unit.header["trailingPatchWhitespace"] == "0a"
    document, binary = model_glb.build_document(unit)
    validation.validate_document(document, binary, source_members=closure.members())


def _tetra_phy() -> bytes:
    """One solid whose single leaf ledge is a tetrahedron, with a KeyValues tail."""

    body = bytearray(48)
    struct.pack_into("<f", body, 24, 1.0)                   # upper limit radius
    struct.pack_into("<i", body, 32, 48)                    # ledge-tree root, body relative
    body[44:48] = b"IVPS"
    root, ledge, points = 48, 76, 156
    body.extend(bytes(points + 4 * 16 - len(body)))
    struct.pack_into("<2i", body, root, 0, ledge - root)    # a leaf node names its ledge
    struct.pack_into("<iiIhh", body, ledge, points - ledge, 0, 0, 4, 0)
    for triangle, corners in enumerate(((0, 1, 2), (0, 2, 3), (0, 3, 1), (1, 3, 2))):
        record = ledge + 16 + triangle * 16
        struct.pack_into("<I", body, record, triangle)
        for edge, corner in enumerate(corners):
            struct.pack_into("<I", body, record + 4 + edge * 4, corner)
    for index, position in enumerate(
        ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    ):
        struct.pack_into("<4f", body, points + index * 16, *position, 1.0)
    struct.pack_into("<I", body, 28, len(body) << 8)

    data = bytearray(16)
    struct.pack_into("<4i", data, 0, 16, 0, 1, 7)
    data += struct.pack("<i", len(body)) + body
    data += b'solid { "index" "0" "surfaceprop" "metal" }\n'
    return bytes(data)


def test_the_phy_key_values_tail_is_projected_once() -> None:
    assert coverage.cover_phy("models/tetra.phy", _tetra_phy())["coveragePercent"] == 100.0
    result = physics.decode(_tetra_phy())
    assert result["solids"][0]["properties"] == {"index": 0, "surfaceprop": "metal"}
    assert result["keyValues"] == [
        {
            "type": "solid",
            "pairs": [{"key": "index", "value": "0"}, {"key": "surfaceprop", "value": "metal"}],
        }
    ]


def test_a_prop_with_no_facial_rig_does_not_claim_to_have_mapped_one() -> None:
    _closure, unit = _unit()
    document, _binary = model_glb.build_document(unit)
    root = document["extensions"][EXTENSION]
    assert "phonemeFilter" not in root["facial"]
    assert root["mdl"]["header"]["phonemeFilter"] == pytest.approx([0.065, 0.1], rel=1e-6)
    assert "facial" not in root["coverage"]["mapped"]


def test_the_validator_refuses_a_ledger_that_renames_a_model_vertex_pool() -> None:
    from elysium_pipeline.formats.unit_contract import ranges_sha256

    closure, unit = _unit()
    document, binary = model_glb.build_document(unit)
    ledger = document["extensions"][EXTENSION]["coverage"]["byteLedger"][0]
    entry = next(
        row for row in ledger["ranges"]
        if row["owner"] == "mdl.bodyParts[0].models[0].vertices"
    )
    entry["owner"] = "mdl.bodyParts[0].models[0].somethingElse"
    ledger["rangesSha256"] = ranges_sha256(
        ledger["sourcePath"], ledger["byteLength"], ledger["ranges"]
    )
    with pytest.raises(validation.ModelGlbValidationError, match="vertices"):
        validation.validate_document(document, binary, source_members=closure.members())


# --- retail bytes the declared tables do not index --------------------------------------------


def _donor_mesh(vertex_count: int, model_relative: int = -224) -> bytes:
    """One `mstudiomesh_t` a superseded compile pass left behind."""

    record = bytearray(60)
    struct.pack_into("<4i", record, 0, 0, model_relative, vertex_count, 0)
    return bytes(record)


def test_a_retained_donor_pool_is_split_at_the_donor_stride_and_its_zero_lead() -> None:
    """`scenery/vehicles/yugo` keeps a mesh whose pool is compact, not this model's 44 bytes."""

    total, lead, stride = 3, 12, 12
    payload = (
        b"anchor\0"
        + _donor_mesh(total)
        + bytes(lead)
        + bytes(range(1, total * stride + 1))
        + b"\x7f" * (total * 16)
        + b"pool\0"
    )
    ledger = coverage.ModelLedger("donor.mdl", payload, padding_owner="mdl.padding")
    ledger.claim(0, 7, "mapped-string", "mdl.textures[0].name")
    ledger.claim(len(payload) - 5, 5, "mapped-string", "mdl.searchPaths[0].value")
    coverage._cover_unindexed_meshes(ledger, payload)
    row = ledger.finish()
    owners = {entry["owner"]: entry for entry in row["ranges"]}
    assert owners["mdl.unindexedMesh@7"]["length"] == 60
    assert (owners["mdl.unindexedMesh@7.vertices"]["offset"],
            owners["mdl.unindexedMesh@7.vertices"]["length"]) == (79, total * stride)
    assert owners["mdl.unindexedMesh@7.tangents"]["length"] == total * 16
    assert owners["mdl.padding"]["offset"] == 67 and owners["mdl.padding"]["length"] == lead
    assert row["coveragePercent"] == 100.0


def test_a_retained_material_table_copy_is_claimed_table_by_table() -> None:
    """The texture, search-path and skin tables the compiler wrote beside its geometry."""

    pool = b"body\0models\\scenery\\prop\\\0"
    tables = bytearray(20 * 2 + 4 + 4)
    base = len(b"anchor\0")
    for slot, name in enumerate((0, 5)):
        record = base + slot * 20
        struct.pack_into("<i", tables, slot * 20, base + len(tables) + name - record)
        struct.pack_into("<f", tables, slot * 20 + 16, 0.25)
    struct.pack_into("<i", tables, 40, base + len(tables) + 5)
    struct.pack_into("<2H", tables, 44, 0, 1)
    payload = b"anchor\0" + bytes(tables) + pool
    ledger = coverage.ModelLedger("tables.mdl", payload, padding_owner="mdl.padding")
    ledger.claim(0, 7, "mapped-string", "mdl.bones[0].name")
    ledger.claim(7 + len(tables), len(pool), "omitted-proven", "mdl.unreferencedString@0")
    coverage._cover_retained_material_tables(ledger, payload)
    row = ledger.finish()
    owners = {entry["owner"]: entry["length"] for entry in row["ranges"]}
    assert owners["mdl.unindexedTextureDuplicate"] == 40
    assert owners["mdl.unindexedSearchPathDuplicate"] == 4
    assert owners["mdl.unindexedSkinTableDuplicate"] == 4
    assert row["coveragePercent"] == 100.0
    for owner in owners:
        if owner.startswith("mdl.unindexed"):
            coverage.omission_reason(owner)


def test_a_compiler_trailer_inside_the_image_is_named_rather_than_left_unclaimed() -> None:
    """`scenery/structural/malkavian/malkmazedrd` carries a second `QnDbTm` mid-image."""

    payload = b"path\0\0\0" + bytes.fromhex("00000000") + b"QnDbTm" + b"tail\0"
    ledger = coverage.ModelLedger("trailer.mdl", payload, padding_owner="mdl.padding")
    ledger.claim(0, 5, "mapped-string", "mdl.searchPaths[0].value")
    ledger.claim(len(payload) - 5, 5, "mapped-string", "mdl.textures[0].name")
    coverage._cover_retained_compiler_trailers(ledger)
    row = ledger.finish()
    entry = next(
        entry for entry in row["ranges"]
        if entry["owner"].startswith("mdl.retainedCompilerTrailerQnDbTm@")
    )
    assert (entry["offset"], entry["length"], entry["state"]) == (7, 10, "omitted-proven")
    assert coverage.omission_reason(entry["owner"]).startswith("compiler-trailer")
    assert row["coveragePercent"] == 100.0


def test_a_superseded_animation_track_head_between_two_channels_is_claimed() -> None:
    """`weapons/rifle_rem700/view/v_rifle_rem700` keeps the head of an uncompressed track."""

    head = bytes([116, 116]) + b"\x11" * 30            # 116 samples declared, 15 present
    payload = b"\x01\x02\x03\x04" + head + b"\x05\x06\x07\x08"
    ledger = coverage.ModelLedger("anim.mdl", payload, padding_owner="mdl.padding")
    ledger.claim(0, 4, "mapped", "mdl.localAnimations[7].frames.bone[0].channel[6]")
    ledger.claim(
        len(payload) - 4, 4, "mapped", "mdl.localAnimations[7].frames.bone[1].channel[0]"
    )
    coverage._cover_superseded_animation_tracks(ledger)
    row = ledger.finish()
    entry = next(
        entry for entry in row["ranges"]
        if entry["owner"].startswith("mdl.supersededAnimationTrack@")
    )
    assert (entry["offset"], entry["length"], entry["state"]) == (4, len(head), "omitted-proven")
    assert coverage.omission_reason(entry["owner"]).startswith("superseded-animation-track")


def test_tracks_after_a_bone_table_that_declares_no_channel_are_still_read() -> None:
    """`weapons/handleclaws/ground/g_handleclaws` declares no channel and stores two tracks."""

    data = bytearray(300)
    data[:4] = b"IDST"
    struct.pack_into("<i", data, 4, 2531)
    struct.pack_into("<2i", data, 264, 1, 200)             # one animation descriptor at 200
    struct.pack_into("<i", data, 212, 2)                   # two frames
    payload = bytes(data) + b"\x02\x02\x01\x02\x03\x04" * 2 + b"ULDD" + b"tail\0"
    ledger = coverage.ModelLedger("tracks.mdl", payload, padding_owner="mdl.padding")
    ledger.claim(0, 300, "mapped", "mdl.localAnimations[0].boneRecords")
    ledger.claim(len(payload) - 5, 5, "mapped-string", "mdl.searchPaths[0].value")
    coverage._cover_extra_animation_tracks(ledger)
    row = ledger.finish()
    owners = [entry["owner"] for entry in row["ranges"]]
    assert "mdl.localAnimations[0].boneRecords.unindexedTrack[0]" in owners
    assert "mdl.localAnimations[0].boneRecords.unindexedTrack[1]" in owners
    assert "mdl.localAnimations[0].boneRecords.compilerPackingULDD" in owners
    assert row["coveragePercent"] == 100.0


# --- the retail PHY KeyValues dialect ----------------------------------------------------------


def test_a_phy_block_that_opens_on_a_brace_repeats_the_block_before_it() -> None:
    """`scenery/structural/ventrue_tower/cinderblocks_breakable` names `break` once."""

    blocks, anomalies = physics._blocks(
        'break {"model" "brick1" }\n{"model" "brick2" }\n{"model" "brick3" }\n\0'
    )
    assert [block["type"] for block in blocks] == ["break", "break", "break"]
    assert [block["values"]["model"] for block in blocks] == ["brick1", "brick2", "brick3"]
    assert [row["row"] for row in anomalies] == ["phy-keyvalues-unnamed-block"] * 2


def test_a_phy_field_with_no_value_keeps_its_key_and_the_stray_brace_closes_nothing() -> None:
    """A stray quote in `scenery/misc/plywoodboard/boardsmall` eats a brace and a keyword."""

    blocks, anomalies = physics._blocks('break {"model" "wood2c" "health" }\n }\n\0')
    assert blocks[0]["pairs"] == [
        {"key": "model", "value": "wood2c"}, {"key": "health", "value": ""}
    ]
    assert [row["row"] for row in anomalies] == [
        "phy-keyvalues-field-without-value", "phy-keyvalues-unbalanced-brace"
    ]


def test_a_phy_block_with_no_name_at_all_is_still_refused() -> None:
    with pytest.raises(physics.ModelPhysicsError, match="expected block name"):
        physics._blocks('{"model" "brick1" }\n\0')


# --- a VTX the MDL outgrew ---------------------------------------------------------------------


def _stale_topology() -> bytes:
    """The fixture's VTX with LOD 1 addressing a vertex the MDL's block does not hold."""

    _mdl, topology = _minimal_mdl_vtx()
    topology = bytearray(topology)
    struct.pack_into("<3H", topology, 140 + 20, 0, 1, 9)
    return bytes(topology)


def test_a_vtx_triangle_outside_the_model_is_dropped_named_and_still_publishes() -> None:
    extra = {f"models/{MODEL_KEY}.dx80.vtx": _stale_topology()}
    _closure, unit = _unit(extra=extra)
    stale = [row for row in unit.anomalies if row["row"] == "vtx-vertex-outside-model"]
    assert len(stale) == 1
    assert stale[0]["lod"] == 1 and stale[0]["mesh"] == 0
    assert stale[0]["droppedTriangles"] == 1 and stale[0]["staleVertices"] == [9]
    assert stale[0]["resolved"] is False
    assert [row["index"] for row in unit.lods] == [0, 1]
    document, binary = model_glb.build_document(unit)
    assert len(document["meshes"]) == 1
    validation.validate_document(document, binary, source_members=_closure.members())
    document["extensions"][EXTENSION]["anomalies"][0]["staleVertices"] = [0]
    with pytest.raises(validation.ModelGlbValidationError, match="the MDL does hold stale"):
        validation.validate_document(document, binary, source_members=_closure.members())


def test_a_stale_vtx_publishes_the_sections_that_do_resolve(tmp_path: Path) -> None:
    extra = {f"models/{MODEL_KEY}.dx80.vtx": _stale_topology()}
    destination = _export(tmp_path, extra=extra)
    summary = validation.validate(destination)
    assert summary["byteCoveragePercent"] == 100.0
    assert (summary["unresolved"], summary["unsupported"]) == (0, 0)
    assert "vtx-vertex-outside-model" in summary["anomalies"]
    assert any("vtx-vertex-outside-model" in row for row in validation.warnings_for(summary))


# --- a LOD only the legacy twin carries --------------------------------------------------------


def _one_lod_topology(checksum: int = CHECKSUM) -> bytes:
    """A dx80 twin that carries LOD 0 alone, so LOD 1 is published from the dx7 pair."""

    topology = bytearray(156)
    struct.pack_into("<i", topology, 0, 107)
    struct.pack_into("<I", topology, 16, checksum)
    struct.pack_into("<2i", topology, 20, 1, 140)
    struct.pack_into("<2i", topology, 28, 1, 36)
    struct.pack_into("<2i", topology, 36, 1, 8)
    struct.pack_into("<2i", topology, 44, 1, 8)
    struct.pack_into("<2if", topology, 52, 1, 12, 0.0)
    struct.pack_into("<H", topology, 64, 1)
    struct.pack_into("<i", topology, 68, 8)
    group = 72
    struct.pack_into("<3H", topology, group, 3, 3, 1)
    topology[group + 6] = 0x10
    struct.pack_into("<3i", topology, group + 8, 20, 26, 32)
    struct.pack_into("<3H", topology, group + 20, 0, 1, 2)
    struct.pack_into("<3H", topology, group + 26, 0, 1, 2)
    struct.pack_into("<4H", topology, group + 32, 3, 0, 3, 0)
    return bytes(topology)


def test_a_lod_only_the_legacy_twin_carries_is_held_to_that_twin(tmp_path: Path) -> None:
    """`character/npc/unique/chinatown/barabus` ships one dx80 LOD and seven dx7 LODs."""

    _mdl, both_lods = _minimal_mdl_vtx()
    extra = {
        f"models/{MODEL_KEY}.dx80.vtx": _one_lod_topology(),
        f"models/{MODEL_KEY}.dx7_2bone.vtx": both_lods,
    }
    _closure, unit = _unit(extra=extra)
    assert unit.vtx_comparison["alternateOnlyLods"] == [1]
    assert [row["index"] for row in unit.lods] == [0, 1]
    destination = _export(tmp_path, extra=extra)
    summary = validation.validate(destination)
    assert summary["lods"] == 2 and summary["byteCoveragePercent"] == 100.0
