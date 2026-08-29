from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import tempfile
import unittest

import pytest

from elysium_pipeline.formats import mdl_cloth
from elysium_pipeline.exporters import character_glb
from elysium_pipeline.formats.character_glb import coverage, expressions, physics, vtx
from elysium_pipeline.formats.character_glb import decode_character
from elysium_pipeline.formats.character_glb.decode import (
    _flex_operation,
    _header,
    _hitbox_sets,
    _include_models,
    _local_sequences,
    _sequence_groups,
    _swing_records_complete,
    _texture_records,
)
from elysium_pipeline.formats.character_glb.model import CharacterModel, SourceIdentity
from elysium_pipeline.formats.character_glb.source import (
    CharacterSourceError,
    SourceMember,
    load_source_closure,
)
from elysium_pipeline.validation import character_glb as validation


def _source(role: str = "mdl") -> SourceIdentity:
    payload = b"synthetic-source"
    return SourceIdentity(
        role=role,
        path="models/character/synthetic/body." + ("mdl" if role == "mdl" else "dx80.vtx"),
        origin={"kind": "synthetic"},
        byte_length=len(payload),
        sha256=hashlib.sha256(payload).hexdigest(),
    )


def _byte_coverage() -> dict:
    source = _source()
    payload = b"synthetic-source"
    ledger = coverage.ByteLedger(source.path, payload)
    ledger.claim(0, len(payload), "mapped", "synthetic")
    return ledger.finish()


def _model() -> CharacterModel:
    return CharacterModel(
        model_path="models/character/synthetic/body.mdl",
        asset_id="vtmb:character-body:synthetic/body",
        sources=[_source()],
        header={"version": 2531, "checksum": 7},
        bones=[
            {
                "index": 0,
                "name": "root",
                "parent": -1,
                "position": (0.0, 0.0, 0.0),
                "rotation": (0.0, 0.0, 0.0, 1.0),
            }
        ],
        lods=[
            {
                "index": 0,
                "switchPoints": [0.0],
                "primitives": [
                    {
                        "bodyPart": 0,
                        "model": 0,
                        "modelBase": 0,
                        "mesh": 0,
                        "skinReference": 0,
                        "material": "body",
                        "materialType": 0,
                        "materialParam": 0,
                        "vertexOffset": 0,
                        "sourceVertices": [0, 1, 2],
                        "positions": [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)],
                        "normals": [(0.0, 0.0, 1.0)] * 3,
                        "uvs": [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)],
                        "joints": [[0, 0, 0, 0]] * 3,
                        "weights": [[1.0, 0.0, 0.0, 0.0]] * 3,
                        "triangles": [(0, 1, 2)],
                    }
                ],
            }
        ],
        materials=[
            {
                "slot": 0,
                "sourceName": "body",
                "sourcePath": "materials/models/character/synthetic/body.vmt",
                "material": "vtmb:material:models/character/synthetic/body",
            }
        ],
        skin_families=[["vtmb:material:models/character/synthetic/body"]],
        local_animations=[],
        sequences=[],
        pose_parameters=[],
        attachments=[],
        hitbox_sets=[],
        ik_chains=[],
        facial={"morphTargets": []},
        procedural={"axisInterpolation": []},
        secondary_motion=[],
        cloth={"garments": [], "sourceModels": []},
        physics=None,
        dependencies=[],
        variant_comparison={"primary": "vtx-dx80", "alternate": None, "equivalent": None},
        byte_coverage=[_byte_coverage()],
        typed_unidentified=[],
        omitted_proven=[],
    )


def _minimal_mdl_vtx() -> tuple[bytes, bytes]:
    """One bone, one mesh, one triangle, and two independently-addressed VTX LODs."""
    mdl_data = bytearray(1124)
    mdl_data[:4] = b"IDST"
    struct.pack_into("<iI", mdl_data, 4, 2531, 0x13572468)
    internal_name = b"synthetic/body.mdl\0"
    mdl_data[12:12 + len(internal_name)] = internal_name
    struct.pack_into("<i", mdl_data, 140, len(mdl_data))
    struct.pack_into("<2f", mdl_data, 232, 0.065, 0.1)
    struct.pack_into("<2i", mdl_data, 240, 1, 424)
    struct.pack_into("<2i", mdl_data, 292, 1, 600)
    struct.pack_into("<2i", mdl_data, 300, 1, 632)
    struct.pack_into("<3i", mdl_data, 308, 1, 1, 640)
    struct.pack_into("<2i", mdl_data, 320, 1, 644)
    struct.pack_into("<i", mdl_data, 420, 1)

    bone = 424
    struct.pack_into("<2i", mdl_data, bone, 160, -1)
    struct.pack_into("<3f", mdl_data, bone + 32, 0.0, 0.0, 0.0)
    struct.pack_into("<4f", mdl_data, bone + 44, 0.0, 0.0, 0.0, 1.0)
    struct.pack_into("<3f", mdl_data, bone + 60, 1.0, 1.0, 1.0)
    struct.pack_into("<4f", mdl_data, bone + 72, 1.0, 1.0, 1.0, 1.0)
    struct.pack_into(
        "<12f",
        mdl_data,
        bone + 88,
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
    )
    struct.pack_into("<i", mdl_data, bone + 156, 1)
    mdl_data[584:589] = b"root\0"

    struct.pack_into("<i", mdl_data, 600, 20)
    mdl_data[620:625] = b"body\0"
    struct.pack_into("<i", mdl_data, 632, 636)
    mdl_data[636] = 0
    struct.pack_into("<h", mdl_data, 640, 0)
    struct.pack_into("<i", mdl_data, 648, 1)
    struct.pack_into("<i", mdl_data, 656, 16)

    model = 660
    mdl_data[model:model + 10] = b"synthetic\0"
    struct.pack_into("<3i", mdl_data, model + 136, 1, 224, 3)
    struct.pack_into("<i", mdl_data, model + 148, 284)
    struct.pack_into("<i", mdl_data, model + 152, 416)
    struct.pack_into("<i", mdl_data, model + 156, 0)
    mesh = model + 224
    struct.pack_into("<4i", mdl_data, mesh, 0, 0, 3, 0)
    vertices = model + 284
    for index, (position, uv) in enumerate((
        ((0.0, 0.0, 0.0), (0.0, 0.0)),
        ((1.0, 0.0, 0.0), (1.0, 0.0)),
        ((0.0, 1.0, 0.0), (0.0, 1.0)),
    )):
        vertex = vertices + index * 44
        struct.pack_into("<4B4h", mdl_data, vertex, 255, 0, 0, 1, 0, 0, 0, 0)
        struct.pack_into("<3f", mdl_data, vertex + 12, *position)
        struct.pack_into("<3f", mdl_data, vertex + 24, 0.0, 0.0, 1.0)
        struct.pack_into("<2f", mdl_data, vertex + 36, *uv)
        struct.pack_into("<4f", mdl_data, 1076 + index * 16, 1.0, 0.0, 0.0, 1.0)

    vtx_data = bytearray(204)
    struct.pack_into("<i", vtx_data, 0, 107)
    struct.pack_into("<I", vtx_data, 16, 0x13572468)
    struct.pack_into("<2i", vtx_data, 20, 2, 188)
    struct.pack_into("<2i", vtx_data, 28, 1, 36)
    struct.pack_into("<2i", vtx_data, 36, 1, 8)
    struct.pack_into("<2i", vtx_data, 44, 2, 8)
    struct.pack_into("<2if", vtx_data, 52, 1, 24, 0.0)
    struct.pack_into("<2if", vtx_data, 64, 1, 20, 10.0)
    struct.pack_into("<H", vtx_data, 76, 1)
    struct.pack_into("<i", vtx_data, 80, 16)
    struct.pack_into("<H", vtx_data, 84, 1)
    struct.pack_into("<i", vtx_data, 88, 56)
    for stripgroup in (92, 140):
        struct.pack_into("<H", vtx_data, stripgroup, 3)
        struct.pack_into("<H", vtx_data, stripgroup + 2, 3)
        struct.pack_into("<H", vtx_data, stripgroup + 4, 1)
        vtx_data[stripgroup + 6] = 0x10
        struct.pack_into("<3i", vtx_data, stripgroup + 8, 20, 26, 32)
        struct.pack_into("<3H", vtx_data, stripgroup + 20, 0, 1, 2)
        struct.pack_into("<3H", vtx_data, stripgroup + 26, 0, 1, 2)
        struct.pack_into("<4H", vtx_data, stripgroup + 32, 3, 0, 3, 0)
    return bytes(mdl_data), bytes(vtx_data)


def _images(*, checksum: int = 0x12345678):
    mdl = bytearray(424)
    mdl[:4] = b"IDST"
    struct.pack_into("<I", mdl, 8, checksum)
    vtx_data = bytearray(36)
    struct.pack_into("<I", vtx_data, 16, checksum)
    return bytes(mdl), bytes(vtx_data)


def test_dx80_is_primary_and_dx7_is_an_independent_alternate() -> None:
    mdl, topology = _images()
    paths = {
        "models/character/test/body.mdl": mdl,
        "models/character/test/body.dx80.vtx": topology,
        "models/character/test/body.dx7_2bone.vtx": topology,
    }
    index = {
        key: ("loose", f"C:/VTMB/Unofficial_Patch/{key}") for key in paths
    }
    closure = load_source_closure(
        index,
        "models/character/test/body",
        read_bytes=lambda _index, key: paths.get(key),
    )
    assert closure.primary_vtx.role == "vtx-dx80"
    assert closure.alternate_vtx.role == "vtx-dx7-2bone"
    assert len(closure.members()) == 3


def test_a_companion_checksum_mismatch_is_refused() -> None:
    mdl, _ = _images(checksum=1)
    _, topology = _images(checksum=2)
    paths = {
        "models/character/test/body.mdl": mdl,
        "models/character/test/body.dx80.vtx": topology,
    }
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in paths}
    with pytest.raises(CharacterSourceError, match="checksum mismatch"):
        load_source_closure(
            index,
            "models/character/test/body.mdl",
            read_bytes=lambda _index, key: paths.get(key),
        )


def test_a_selected_facial_resource_is_retained_without_a_twin() -> None:
    mdl, topology = _images()
    paths = {
        "models/character/test/body.mdl": mdl,
        "models/character/test/body.dx80.vtx": topology,
        "expressions/body_phonemes.txt": b"$keys jaw\n",
    }
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in paths}
    closure = load_source_closure(
        index,
        "models/character/test/body.mdl",
        read_bytes=lambda _index, key: paths.get(key),
    )
    assert [member.role for member in closure.facial] == ["facial-phonemes-txt"]


def test_phy_checksum_mismatch_is_retained_as_provenance() -> None:
    mdl, topology = _images(checksum=1)
    phy = bytearray(16)
    struct.pack_into("<4i", phy, 0, 16, 0, 1, 2)
    paths = {
        "models/character/test/body.mdl": mdl,
        "models/character/test/body.dx80.vtx": topology,
        "models/character/test/body.phy": bytes(phy),
    }
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in paths}
    closure = load_source_closure(
        index,
        "models/character/test/body.mdl",
        read_bytes=lambda _index, key: paths.get(key),
    )
    assert struct.unpack_from("<I", closure.phy.data, 12)[0] == 2


def test_weighted_txt_and_vfe_header_agree_on_the_row_count() -> None:
    table = expressions.decode_txt(
        b'$keys jaw smile\n$hasweighting\n"aa" "a" 0.5 1.0 0.25 0.75 "open"\n'
    )
    assert table["keys"] == ["jaw", "smile"]
    assert table["rows"][0]["values"][1]["weight"] == 0.75

    vfe = bytearray(144)
    vfe[:4] = b"EFV\0"
    internal_name = b"expressions/test.vfe\0"
    vfe[8:8 + len(internal_name)] = internal_name
    struct.pack_into("<ii", vfe, 136, len(vfe), 1)
    header = expressions.decode_vfe_header(bytes(vfe))
    assert header["rowCount"] == len(table["rows"])


def test_compiled_vfe_decodes_to_the_same_semantic_row_as_txt() -> None:
    txt = expressions.decode_txt(
        b'$keys jaw\n$hasweighting\n"aa" "a" 0.5 1.0 "open"\n'
    )
    vfe = bytearray(224)
    vfe[:4] = b"EFV\0"
    struct.pack_into("<9i", vfe, 136, 224, 1, 172, 0, 0, 0, 1, 216, 220)
    struct.pack_into("<6i", vfe, 172, 36, 0, 1, ord("a"), 0, 24)
    struct.pack_into("<iff", vfe, 196, 0, 0.5, 1.0)
    vfe[208:211] = b"aa\0"
    vfe[211:215] = b"jaw\0"
    struct.pack_into("<i", vfe, 216, 211)
    struct.pack_into("<i", vfe, 220, -1)
    compiled = expressions.decode_vfe(bytes(vfe))
    expressions.compare_txt_vfe(txt, compiled)
    assert compiled["settings"][0]["values"][0]["controller"] == "jaw"


def test_unknown_txt_directive_is_not_silently_ignored() -> None:
    with pytest.raises(expressions.CharacterFacialError, match="unsupported directive"):
        expressions.decode_txt(b"$keys jaw\n$unknown\n")


def test_zero_key_table_retains_labelled_empty_rows() -> None:
    table = expressions.decode_txt(
        b'$keys\n$hasweighting\n"neutral" "_" "No controller values"\n'
    )
    assert table["keys"] == []
    assert table["rows"][0]["values"] == []
    assert table["rows"][0]["description"] == "No controller values"


def test_unused_flex_operand_does_not_project_nan() -> None:
    row = _flex_operation(4, -1, float("nan"))
    assert row["operandKind"] == "unused"
    assert row["rawOperandBits"] == 0xFFFFFFFF
    assert "value" not in row
    named = _flex_operation("FETCH1", 3, float("nan"))
    assert named["operandKind"] == "flexControllerIndex"
    assert named["index"] == 3
    assert "value" not in named


def test_ragdoll_constraint_fields_are_retained() -> None:
    blocks = physics._blocks(
        'ragdollconstraint {\n"parent" "0"\n"child" "3"\n'
        '"xmin" "-25.0"\n"xmax" "20.0"\n}\n\0'
    )
    assert blocks[0]["type"] == "ragdollconstraint"
    assert blocks[0]["values"]["child"] == "3"
    assert blocks[0]["values"]["xmin"] == "-25.0"


def test_known_physics_values_are_typed() -> None:
    values = physics._typed_values(
        "ragdollconstraint", {"parent": "0", "child": "3", "xmin": "-25.0"}
    )
    assert values == {"parent": 0, "child": 3, "xmin": -25.0}


def test_inline_break_block_preserves_backslash_path_and_health() -> None:
    blocks = physics._blocks(
        'break { "model" "character\\monster\\gib.mdl" "health" "100" }'
    )
    assert blocks[0]["values"]["model"] == "character\\monster\\gib.mdl"
    assert physics._typed_values("break", blocks[0]["values"])["health"] == 100


def test_ledge_keeps_triangle_edges_and_point_w() -> None:
    data = bytearray(144)
    struct.pack_into("<iiIhh", data, 0, 80, 12, 0x123408, 4, 7)
    faces = ((0, 1, 2), (0, 3, 1), (0, 2, 3), (1, 3, 2))
    for triangle, corners in enumerate(faces):
        record = 16 + triangle * 16
        struct.pack_into("<I", data, record, 0xA0000000 + triangle)
        for edge, point in enumerate(corners):
            raw = point | ((edge + 1) << 16)
            if triangle == 0 and edge == 0:
                raw |= 0x80000000
            struct.pack_into("<I", data, record + 4 + edge * 4, raw)
    points = ((0.0, 0.0, 0.0, 1.0), (1.0, 0.0, 0.0, 2.0),
              (0.0, 1.0, 0.0, 3.0), (0.0, 0.0, 1.0, 4.0))
    for index, point in enumerate(points):
        struct.pack_into("<4f", data, 80 + index * 16, *point)
    ledge = physics._ledge(bytes(data), 0)
    assert ledge["padding"] == 7
    assert ledge["sourcePoints"][3]["ivp"][3] == 4.0
    assert ledge["triangleRecords"][0]["edges"][0]["virtual"]
    assert ledge["triangleRecords"][0]["edges"][0]["oppositeIndex"] == 1


def test_lod_matrix_and_complete_particle_vertex_map_are_retained() -> None:
    data = bytearray(520)
    struct.pack_into("<2i", data, 200, 1, 220)
    struct.pack_into("<2i", data, 220, 228, 360)
    for lod, record in enumerate((228, 360)):
        struct.pack_into("<f4i", data, record, 3.0, 3, 1, 2, 92)
        struct.pack_into("<3H", data, record + 92, 10 + lod, 20 + lod, 30 + lod)
    _table, rows, columns, records = mdl_cloth.definition_table(bytes(data), 0)
    decoded = mdl_cloth.read_definition(bytes(data), 0, 0, lod=1)
    assert (rows, columns) == (2, 1)
    assert records == [228, 360]
    assert decoded["particle_vertex_indices"] == [11, 21, 31]
    assert decoded["anchor_vertex_indices"] == [11]


def test_compact_hitbox_set_keeps_bone_group_and_bounds() -> None:
    data = bytearray(384)
    struct.pack_into("<2i", data, 256, 1, 300)
    struct.pack_into("<3i", data, 300, 80, 1, 12)
    struct.pack_into(
        "<2i3f3f",
        data,
        312,
        0,
        3,
        -1.0,
        -2.0,
        -3.0,
        4.0,
        5.0,
        6.0,
    )
    data[380:382] = b"A\0"
    sets = _hitbox_sets(bytes(data), [{"name": "root"}])
    assert sets[0]["name"] == "A"
    assert sets[0]["boxes"][0]["group"] == 3
    assert sets[0]["boxes"][0]["boundsMax"] == (4.0, 5.0, 6.0)


def test_complete_descriptor_keeps_fixed_table_and_typed_tail() -> None:
    source, _topology = _minimal_mdl_vtx()
    data = bytearray(source)
    sequence_base = len(data)
    animation_base = sequence_base + 764
    data.extend(b"\0" * (764 + 72 + 32))
    label = animation_base + 72
    activity = label + 5
    data.extend(b"idle\0ACT_IDLE\0")
    struct.pack_into("<2i", data, 264, 1, animation_base)
    struct.pack_into("<2i", data, 272, 1, sequence_base)
    struct.pack_into("<i", data, sequence_base, label - sequence_base)
    struct.pack_into("<i", data, sequence_base + 4, activity - sequence_base)
    struct.pack_into("<i", data, sequence_base + 52, 1)
    struct.pack_into("<h", data, sequence_base + 56, 0)
    struct.pack_into("<2i", data, sequence_base + 572, 1, 1)
    struct.pack_into("<2i", data, sequence_base + 580, -1, -1)
    struct.pack_into("<3f", data, sequence_base + 612, 0.2, 0.2, 0.2)
    struct.pack_into("<3f", data, sequence_base + 672, -1.0, -2.0, -3.0)
    struct.pack_into("<3f", data, sequence_base + 684, 4.0, 5.0, 6.0)
    struct.pack_into("<i", data, sequence_base + 696, 9)
    struct.pack_into("<f", data, sequence_base + 716, 1.17549435e-38)
    struct.pack_into("<f", data, sequence_base + 720, 3.40282347e38)
    struct.pack_into("<5i", data, sequence_base + 724, -1, -1, -1, -1, -1)
    struct.pack_into("<2i", data, sequence_base + 744, -1, -1)
    struct.pack_into("<3f", data, sequence_base + 752, 0.0, 1.0, 1.0)
    struct.pack_into("<f", data, animation_base + 4, 30.0)
    struct.pack_into("<i", data, animation_base + 12, 1)
    rows = _local_sequences(bytes(data))
    assert len(rows[0]["animationTable"]) == 256
    assert rows[0]["statGate"] == 9
    assert rows[0]["secondaryBoundsMax"] == (4.0, 5.0, 6.0)
    assert rows[0]["comboWindow"] == [0.0, 1.0, 1.0]


def test_swing_unknown_block_is_split_into_recovered_fields() -> None:
    data = bytearray(952)
    struct.pack_into("<2i", data, 708, 1, 764)
    record = 764
    struct.pack_into("<2fi", data, record, 0.1, 0.2, 0)
    struct.pack_into("<6f", data, record + 12, *(0.0,) * 6)
    struct.pack_into("<i", data, record + 0x24, 0)
    struct.pack_into("<4i", data, record + 0x28, 1, 0, 0, 0)
    struct.pack_into("<16i", data, record + 0x38, *([-1] * 16))
    data[record + 0xB8:record + 0xBC] = bytes((0, 1, 2, 0))
    row = _swing_records_complete(bytes(data), 0, ["foot"])[0]
    assert row["kickOnlyMarker"] == 0
    assert row["candidateCounts"] == [1, 0, 0, 0]
    assert row["resolvedKnockbackActivities"] == [-1] * 16
    assert row["bucket0LowHeightMarker"] == 1
    assert "unidentified" not in row


def test_texture_record_keeps_vtmb_float_fields() -> None:
    data = bytearray(325)
    struct.pack_into("<2i", data, 292, 1, 300)
    struct.pack_into("<2i3f", data, 300, 20, 5, 1.5, 2.5, 0.25)
    data[320:325] = b"body\0"
    row = _texture_records(bytes(data))[0]
    assert row["flags"] == 5
    assert (row["width"], row["height"], row["maxWorldUnitsPerTexel"]) == (1.5, 2.5, 0.25)


def test_sequence_group_decodes_both_record_relative_strings() -> None:
    data = bytearray(332)
    struct.pack_into("<2i", data, 284, 1, 300)
    struct.pack_into("<4i", data, 300, 16, 24, 0, 0)
    data[316:324] = b"default\0"
    data[324] = 0
    row = _sequence_groups(bytes(data))[0]
    assert (row["label"], row["name"]) == ("default", "")


def test_include_group_decodes_pose_maps_and_bone_remap() -> None:
    data = bytearray(700)
    struct.pack_into("<2i", data, 404, 1, 500)
    struct.pack_into("<5i", data, 500, 172, 0, 0x7FFFFFFF, 0, 116)
    struct.pack_into("<24h", data, 520, *([-1] * 24))
    struct.pack_into("<24h", data, 568, *([-1] * 24))
    struct.pack_into("<h2B2h", data, 616, -1, 0, 0, -1, -1)
    struct.pack_into(
        "<12f",
        data,
        624,
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
    )
    data[672:688] = b"shared/test.mdl\0"
    row = _include_models(bytes(data), [{"name": "root"}])[0]
    assert row["path"] == "models/shared/test.mdl"
    assert row["globalToLocalPoseParameters"] == [-1] * 24
    assert row["boneRemap"][0]["sourceBone"] == (-1)


def test_signature_uses_source_vertices_instead_of_output_numbering() -> None:
    first = [{
        "index": 0,
        "primitives": [{
            "bodyPart": 0, "model": 0, "mesh": 0, "skinReference": 0,
            "sourceVertices": [7, 8, 9], "triangles": [(0, 1, 2)],
        }],
    }]
    reordered = [{
        "index": 0,
        "primitives": [{
            "bodyPart": 0, "model": 0, "mesh": 0, "skinReference": 0,
            "sourceVertices": [9, 7, 8], "triangles": [(1, 2, 0)],
        }],
    }]
    assert vtx.topology_signature(first) == vtx.topology_signature(reordered)


def test_every_declared_lod_is_decoded() -> None:
    mdl_data, topology = _minimal_mdl_vtx()
    lods = vtx.decode_lods(mdl_data, topology, variant="synthetic")
    assert [lod["index"] for lod in lods] == [0, 1]
    assert [lod["switchPoints"] for lod in lods] == [[0.0], [10.0]]
    assert lods[1]["primitives"][0]["triangles"] == [(0, 1, 2)]
    assert lods[0]["primitives"][0]["tangents"][0] == (1.0, 0.0, 0.0, 1.0)
    assert vtx.decode_material_replacements(topology) == [
            {"lod": 0, "count": 0, "relativeOffset": 0, "replacements": []},
            {"lod": 1, "count": 0, "relativeOffset": 0, "replacements": []},
        ]


def test_vtmb_packed_material_replacement_is_typed() -> None:
    data = bytearray(55)
    struct.pack_into("<i", data, 20, 1)
    struct.pack_into("<i", data, 24, 36)
    struct.pack_into("<2i", data, 36, 1, 8)
    struct.pack_into("<hi", data, 44, 2, 6)
    data[50:55] = b"skin\0"
    assert vtx.decode_material_replacements(bytes(data)) == [{
            "lod": 0,
            "count": 1,
            "relativeOffset": 8,
            "replacements": [{"index": 0, "material": 2, "name": "skin"}],
        }]


def _closure_inputs():
    mdl_data, topology = _minimal_mdl_vtx()
    material = b'VertexLitGeneric\n{\n"$basetexture" "synthetic/body"\n}\n'
    paths = {
        "models/character/synthetic/body.mdl": mdl_data,
        "models/character/synthetic/body.dx80.vtx": topology,
        "materials/body.vmt": material,
    }
    index = {key: ("loose", f"C:/VTMB/Vampire/{key}") for key in paths}
    return mdl_data, paths, index


def test_minimal_direct_closure_decodes_end_to_end() -> None:
    _mdl_data, paths, index = _closure_inputs()
    closure = load_source_closure(
        index,
        "models/character/synthetic/body.mdl",
        read_bytes=lambda _index, key: paths.get(key),
    )
    model = decode_character(
        closure,
        index,
        read_bytes=lambda _index, key: paths.get(key),
    )
    assert model.asset_id == "vtmb:character-body:synthetic/body"
    assert len(model.bones) == 1
    assert len(model.lods) == 2
    assert model.materials[0]["material"] == "vtmb:material:body"
    assert (model.header["reserved412"], model.header["reserved416"]) == (0, 0)
    assert model.header["bodyParts"][0]["models"][0]["reserved184"] == [0, 0]
    assert [row["path"] for row in model.typed_unidentified] == [
            "mdl.secondaryMotion[].unusedAuthoredPreset",
        ]


def test_declared_image_allows_bounded_patch_newline() -> None:
    mdl_data, _topology = _minimal_mdl_vtx()
    header = _header(mdl_data + b"\n")
    assert header["length"] == len(mdl_data)
    assert header["physicalLength"] == len(mdl_data) + 1
    assert header["trailingPatchWhitespace"] == "0a"


def test_public_exporter_writes_the_isolated_relative_path() -> None:
    _mdl_data, paths, index = _closure_inputs()
    with tempfile.TemporaryDirectory() as temporary:
        destination = character_glb.export(
            index,
            "models/character/synthetic/body.mdl",
            Path(temporary),
            read_bytes=lambda _index, key: paths.get(key),
            anorms=[],
        )
        summary = validation.validate(destination)
        document, _binary = validation.read_glb(destination)
        relative = destination.relative_to(temporary).as_posix()
    assert relative == "synthetic/body.glb"
    assert summary["lods"] == 2
    assert "TANGENT" in document["meshes"][0]["primitives"][0]["attributes"]


def test_complete_synthetic_character_publishes_and_validates() -> None:
    model = _model()
    document, binary = character_glb.build_document(model, b"")
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "body.glb"
        character_glb.write_glb(document, binary, path)
        summary = validation.validate(path)
    assert summary["asset"] == model.asset_id
    assert summary["bones"] == 1
    assert summary["lods"] == 1
    assert summary["animations"] == 0
    assert summary["sourceBytes"] == summary["accountedBytes"]
    assert summary["byteCoveragePercent"] == 100.0
    coverage = document["extensions"]["ELYSIUM_vtmb_character"]["coverage"]
    assert "typedUnidentified" in coverage
    assert coverage["byteLedger"][0]["coveragePercent"] == 100.0
    assert "states" not in coverage


def test_validator_refuses_an_opaque_source_blob() -> None:
    document, binary = character_glb.build_document(_model(), b"")
    extension = document["extensions"]["ELYSIUM_vtmb_character"]
    extension["mdl"]["rawData"] = "SUQ="
    with pytest.raises(
        validation.CharacterGlbValidationError, match="opaque source payload"
    ):
        validation.validate_document(document, binary)


def test_validator_refuses_a_byte_ledger_gap() -> None:
    document, binary = character_glb.build_document(_model(), b"")
    ledger = document["extensions"]["ELYSIUM_vtmb_character"]["coverage"][
        "byteLedger"
    ][0]
    ledger["ranges"][0]["length"] -= 1
    with pytest.raises(
        validation.CharacterGlbValidationError, match="byte ledger"
    ):
        validation.validate_document(document, binary)


def test_prepublication_validation_rechecks_source_bytes() -> None:
    document, binary = character_glb.build_document(_model(), b"")
    source = _source()
    changed = SourceMember(
        role="mdl",
        path=source.path,
        data=b"changed-source",
        origin={"kind": "synthetic"},
    )
    with pytest.raises(
        validation.CharacterGlbValidationError,
        match="prepublication source bytes disagree",
    ):
        validation.validate_document(
            document,
            binary,
            source_members=(changed,),
        )


def test_unclaimed_nonzero_byte_is_a_hard_failure() -> None:
    ledger = coverage.ByteLedger("source.bin", b"\0\x7f")
    ledger.claim(0, 1, "mapped", "header")
    with pytest.raises(
        coverage.CharacterByteCoverageError, match="unclaimed non-zero byte"
    ):
        ledger.finish()


def test_zero_gap_is_verified_and_accounted() -> None:
    ledger = coverage.ByteLedger("source.bin", b"A\0\0")
    ledger.claim(0, 1, "mapped", "value")
    result = ledger.finish()
    assert result["accountedBytes"] == 3
    assert result["coveragePercent"] == 100.0
    assert result["stateBytes"]["padding-zero"] == 2


def test_minimal_mdl_has_gapless_byte_ledger() -> None:
    mdl_data, topology = _minimal_mdl_vtx()
    result = coverage.cover_mdl("synthetic.mdl", mdl_data, vtx_data=topology)
    assert result["coveragePercent"] == 100.0
    assert result["accountedBytes"] == len(mdl_data)


def test_qndbtm_eof_trailer_is_a_mapped_compiler_record() -> None:
    payload = b"skin\0" + bytes.fromhex("64001100516e4462546d")
    ledger = coverage.ByteLedger("tail.bin", payload)
    ledger.claim(0, 5, "mapped-string", "texture[9].name")
    coverage._cover_retained_mdl_payloads(ledger, payload, bone_count=1)
    result = ledger.finish()
    assert result["ranges"][-1]["owner"] == "compilerTrailerQnDbTm"
    assert result["ranges"][-1]["state"] == "mapped"
    assert result["ranges"][-1]["length"] == 10
    trailer = coverage.compiler_trailer(payload)
    assert trailer["magic"] == "QnDbTm"
    assert trailer["pathOffset"] == 0x00110064


def test_cloth_map_layout_uses_the_pointer_span() -> None:
    verts = 4
    selectors = bytes([0, 0xFF, 0, 0xFF]) + bytes([0xFF] * 4)
    positions = bytes(verts * 2 * 2)
    tangents = bytes(verts * 2 * 2)
    image = bytearray(60 + len(selectors) + len(positions) + len(tangents))
    struct.pack_into("<i", image, 8, verts)
    struct.pack_into("<i", image, 48, 60)
    struct.pack_into("<i", image, 52, 60 + len(selectors))
    struct.pack_into("<i", image, 56, 60 + len(selectors) + len(positions))
    image[60:60 + len(selectors)] = selectors
    layout = mdl_cloth.map_layout(bytes(image), 0)
    assert layout["rows"] == 2
    assert layout["position_rows"] == 2
    assert layout["selector_align"] == 0
    assert layout["vertex_count"] == verts


def test_overlapping_vtx_strip_groups_reuse_the_declared_range() -> None:
    ledger = coverage.ByteLedger("mesh.vtx", bytes(44))
    ledger.array(4, 1, 20, "lod[0].mesh[0].stripGroups")
    ledger.array(4, 1, 20, "lod[1].mesh[0].stripGroups", allow_existing=True)
    result = ledger.finish()
    assert result["ranges"][1]["owner"] == "lod[0].mesh[0].stripGroups"


def test_writer_records_the_source_to_gltf_transform() -> None:
    document, _binary = character_glb.build_document(_model(), b"")
    extension = document["extensions"]["ELYSIUM_vtmb_character"]
    assert extension["identity"]["sourcePolicy"] == "up-first"
    assert extension["coordinateTransform"]["scale"] == 0.0254
    assert extension["coverage"]["unresolved"] == []
    assert extension["coverage"]["unsupported"] == []
