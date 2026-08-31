"""Contract tests for the Map-lighting GLB seam.

Every fixture here is synthetic: a BSP built byte by byte in this module, handed to the seam
through a fake index and an injected reader, so nothing in this file depends on an installed copy
of the game. What the tests pin is the seam's own promises -- the identity rule, one ledger per
owned span with every byte claimed exactly once, the `(set, style)` span partition of one face's
samples, the orphan runs the corpus holds, each dependency role, each anomaly the specification
names, and the validator's refusal of a unit that has been tampered with.
"""

from __future__ import annotations

import hashlib
import struct

import pytest

import elysium_pipeline.formats.map_glb.partition as map_partition
from elysium_pipeline.exporters import map_lighting_glb as exporter
from elysium_pipeline.formats import map_lighting_glb as lighting
from elysium_pipeline.formats.unit_contract import ByteLedgerError, encode_glb, read_glb
from elysium_pipeline.validation import map_lighting_glb as validation

MAP_NAME = "testmap"
MAP_KEY = f"maps/{MAP_NAME}.bsp"
EXTENSION = lighting.MAP_LIGHTING_EXTENSION

#: Lump 8, laid out as: a four-byte orphan head, face 0's samples, face 1's samples, a
#: four-byte zero-filled orphan tail.
ORPHAN_HEAD = b"\x01\x02\x03\x04"
FACE0_SAMPLES = bytes(range(16, 32))
FACE1_SAMPLES = bytes(range(32, 64))
ORPHAN_TAIL = b"\0\0\0\0"
LIGHTING_LUMP = ORPHAN_HEAD + FACE0_SAMPLES + FACE1_SAMPLES + ORPHAN_TAIL


# --------------------------------------------------------------------------------------------
# One synthetic BSP, laid out lump by lump.
# --------------------------------------------------------------------------------------------


class BspBuilder:
    """Lay lumps out after the header, remembering the directory rows they need."""

    def __init__(self) -> None:
        self.body = bytearray()
        self.rows: dict[int, tuple[int, int]] = {}
        self.base = map_partition.HEADER_BYTES

    @property
    def cursor(self) -> int:
        return self.base + len(self.body)

    def add(self, index: int, payload: bytes) -> tuple[int, int]:
        offset = self.cursor
        self.body.extend(payload)
        self.rows[index] = (offset, len(payload))
        return offset, len(payload)

    def finish(self, *, revision: int = 11) -> bytes:
        header = bytearray(struct.pack("<4si", b"VBSP", 17))
        for index in range(map_partition.LUMP_COUNT):
            offset, length = self.rows.get(index, (0, 0))
            header.extend(struct.pack("<iii4s", offset, length, 0, b"\0\0\0\0"))
        header.extend(struct.pack("<i", revision))
        assert len(header) == map_partition.HEADER_BYTES
        data = bytes(header) + bytes(self.body)
        trailer = struct.pack("<I", self.rows.get(0, (0, 0))[0])
        return data + trailer + map_partition.TRAILER_SIGNATURE


def _texinfo(flags: int, tex_data: int = 0) -> bytes:
    return struct.pack("<16f2i", *([0.0] * 16), flags, tex_data)


def _face(
    *,
    tex_info: int,
    styles: tuple[int, ...],
    light_offset: int,
    lightmap_size: tuple[int, int] = (1, 1),
    lightmap_mins: tuple[int, int] = (3, 5),
) -> bytes:
    record = bytearray(b"\0" * 104)
    for slot in range(8):
        struct.pack_into("<3Bb", record, slot * 4, slot, slot + 1, slot + 2, -1)
    struct.pack_into("<HBB", record, 32, 0, 0, 0)
    struct.pack_into("<i3hH", record, 36, 0, 4, tex_info, -1, 0)
    struct.pack_into("<8B", record, 48, *styles)
    struct.pack_into("<if", record, 72, light_offset, 16.0)
    struct.pack_into("<2i", record, 80, *lightmap_mins)
    struct.pack_into("<2i", record, 88, *lightmap_size)
    struct.pack_into("<iI", record, 96, -1, 0)
    return bytes(record)


def _world_light(*, index: int, type_: int = 1, style: int = 0) -> bytes:
    return struct.pack(
        "<9f3i7f3i",
        1.0 * index, 2.0 * index, 3.0 * index,
        100.0, 200.0, 300.0,
        0.0, 0.0, -1.0,
        7, type_, style,
        0.5, 0.25, 1.5, 64.0,
        0.0, 0.0, 1.0,
        3, -1, index,
    )


def _dispinfo(*, power: int, alpha_start: int, sample_start: int, map_face: int) -> bytes:
    record = bytearray(b"\0" * 176)
    struct.pack_into("<4i", record, 12, 0, 0, power, 0)
    struct.pack_into("<H", record, 36, map_face)
    struct.pack_into("<2i", record, 40, alpha_start, sample_start)
    return bytes(record)


def _world_light_non_finite() -> bytes:
    """An 88-byte record whose `radius` the JSON chunk cannot state."""

    record = bytearray(_world_light(index=2))
    struct.pack_into("<f", record, 60, float("inf"))
    return bytes(record)


def build_bsp(
    *,
    lighting_lump: bytes = LIGHTING_LUMP,
    worldlights: bytes | None = None,
    worldlight_tail: bytes = b"",
    displacements: bool = True,
    dispinfos: bytes | None = None,
    dplt: bytes | None = None,
    faces: bytes | None = None,
) -> bytes:
    """A BSP holding exactly what the lighting seam reads, plus the tables it derives from."""

    builder = BspBuilder()
    builder.add(0, b'{\n"classname" "worldspawn"\n}\n\0')
    builder.add(6, _texinfo(0) + _texinfo(lighting.SURF_BUMPLIGHT))
    if faces is None:
        faces = b"".join(
            (
                # unbumped, one live style, a 2x2 luxel grid: 16 bytes at offset 4
                _face(tex_info=0, styles=(0, 255, 255, 255, 255, 255, 255, 255), light_offset=4),
                # bumped, two live styles, a 1x1 luxel grid: 4 sets x 2 styles at offset 20
                _face(
                    tex_info=1,
                    styles=(0, 6, 255, 255, 255, 255, 255, 255),
                    light_offset=20,
                    lightmap_size=(0, 0),
                ),
                # authored without a lightmap
                _face(
                    tex_info=0,
                    styles=(255,) * 8,
                    light_offset=-1,
                ),
            )
        )
    builder.add(7, faces)
    builder.add(8, lighting_lump)
    if worldlights is None:
        worldlights = _world_light(index=0) + _world_light(index=1, type_=3)
    builder.add(15, worldlights + worldlight_tail)
    if displacements:
        if dispinfos is None:
            dispinfos = (
                _dispinfo(power=2, alpha_start=0, sample_start=0, map_face=0)
                + _dispinfo(power=3, alpha_start=5, sample_start=7, map_face=1)
            )
        builder.add(26, dispinfos)
        builder.add(32, bytes(range(1, 10)))
        builder.add(34, bytes(range(1, 14)))
    if dplt is None:
        dplt = struct.pack("<i", 2) + bytes([10, 20, 30, 0xFE, 4]) + bytes([1, 2, 3, 5, 0])
    if dplt is not None:
        directory_bytes = 4 + 16
        game_offset = builder.cursor
        directory = struct.pack("<i", 1) + struct.pack(
            "<4sHHii", b"dplt"[::-1], 0, 0, game_offset + directory_bytes, len(dplt)
        )
        builder.add(35, directory + dplt)
    return builder.finish()


def make_index() -> dict:
    return {MAP_KEY: ("loose", r"C:\game\Unofficial_Patch\maps\testmap.bsp")}


def reader(data: bytes):
    return lambda index, key: data if key == MAP_KEY else None


def decoded(data: bytes):
    closure = lighting.load_source_closure(make_index(), MAP_NAME, read_bytes=reader(data))
    return closure, lighting.decode_map_lighting(closure)


def built(data: bytes):
    closure, model = decoded(data)
    document, binary = exporter.build_document(model)
    return closure, document, binary


def extension_of(document) -> dict:
    return document["extensions"][EXTENSION]


def export_unit(tmp_path, data: bytes):
    return exporter.export(make_index(), MAP_NAME, tmp_path, read_bytes=reader(data))


def ledger_of(root, path_suffix: str) -> dict:
    for row in root["coverage"]["byteLedger"]:
        if str(row["sourcePath"]).endswith(path_suffix):
            return row
    raise AssertionError(f"no ledger for {path_suffix}")


# --------------------------------------------------------------------------------------------
# Identity, container and the source closure
# --------------------------------------------------------------------------------------------


def test_the_unit_key_is_the_map_stem_and_one_file_carries_one_identity():
    _closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    assert root["identity"]["asset"] == f"vtmb:map-lighting:{MAP_NAME}"
    assert root["identity"]["map"] == MAP_NAME
    assert root["identity"]["sourcePath"] == MAP_KEY
    assert root["identity"]["sourcePolicy"] == "up-first"
    assert lighting.output_relative_path("maps/TestMap.bsp").as_posix() == (
        f"maps/{MAP_NAME}.lighting.glb"
    )
    assert lighting.normalize_key("maps/TestMap.bsp") == MAP_NAME


def test_the_unit_declares_its_extension_used_and_required_and_is_scene_less():
    _closure, document, _binary = built(build_bsp())
    assert document["extensionsUsed"] == [EXTENSION]
    assert document["extensionsRequired"] == [EXTENSION]
    assert document["asset"]["generator"] == "Elysium Map-lighting GLB Exporter"
    for forbidden in ("scenes", "nodes", "meshes", "images", "textures", "samplers"):
        assert forbidden not in document


def test_the_extension_root_opens_with_the_contract_keys_in_order():
    _closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    assert list(root)[:5] == [
        "schemaVersion", "identity", "sourceResolution", "dependencies", "coverage"
    ]


def test_the_members_are_the_spans_the_root_partition_hands_this_unit():
    data = build_bsp()
    closure, document, _binary = built(data)
    root = extension_of(document)
    spans = {
        (span.offset, span.length)
        for span in map_partition.partition(data).spans_for("map-lighting")
    }
    assert {
        (member["span"]["offset"], member["span"]["length"])
        for member in root["sourceResolution"]["members"]
    } == spans
    assert [member["role"] for member in root["sourceResolution"]["members"]] == [
        "lighting", "worldlights", "disp-lightmap-alphas",
        "disp-lightmap-sample-positions", "detail-prop-lighting",
    ]
    assert [member.path for member in closure.members()] == [
        f"{MAP_KEY}#lump8", f"{MAP_KEY}#lump15", f"{MAP_KEY}#lump32",
        f"{MAP_KEY}#lump34", f"{MAP_KEY}#lump35.dplt",
    ]


def test_the_plural_command_enumerates_one_unit_per_map_below_maps():
    index = make_index()
    index["maps/graphs/testmap.ain"] = ("loose", r"C:\game\Vampire\maps\graphs\testmap.ain")
    index["maps/soundcache/testmap.cache"] = ("loose", r"C:\cache")
    index["maps/OTHER.bsp"] = ("loose", r"C:\game\Vampire\maps\OTHER.bsp")
    assert exporter.source_keys(index) == ["other", MAP_NAME]


def test_a_map_the_index_does_not_hold_is_not_exported(tmp_path):
    with pytest.raises(lighting.MapLightingSourceError):
        exporter.export({}, MAP_NAME, tmp_path, read_bytes=lambda index, key: None)


def test_a_record_that_runs_off_the_end_of_the_file_fails_the_decode():
    import dataclasses

    closure, _model = decoded(build_bsp())
    truncated = dataclasses.replace(closure, data=closure.data[:64])
    with pytest.raises(lighting.MapLightingDecodeError):
        lighting.decode_map_lighting(truncated)


# --------------------------------------------------------------------------------------------
# The ledgers
# --------------------------------------------------------------------------------------------


def test_every_byte_of_every_owned_span_is_claimed_exactly_once():
    data = build_bsp()
    closure, document, _binary = built(data)
    root = extension_of(document)
    ledgers = root["coverage"]["byteLedger"]
    assert len(ledgers) == len(closure.members())
    for row, member in zip(ledgers, closure.members()):
        assert row["sourcePath"] == member.path
        assert row["byteLength"] == member.byte_length
        assert row["accountedBytes"] == member.byte_length
        assert row["coveragePercent"] == 100.0
        assert row["sourceSha256"] == hashlib.sha256(member.data).hexdigest()
        cursor = 0
        for entry in row["ranges"]:
            assert entry["offset"] == cursor
            assert entry["length"] > 0
            cursor += entry["length"]
        assert cursor == member.byte_length


def test_one_face_pays_for_one_range_per_set_and_style():
    _closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    owners = [entry["owner"] for entry in ledger_of(root, "#lump8")["ranges"]]
    assert owners == [
        "lighting.orphan[0]",
        "lighting.faces[0].set[0].style[0]",
        "lighting.faces[1].set[0].style[0]",
        "lighting.faces[1].set[1].style[0]",
        "lighting.faces[1].set[2].style[0]",
        "lighting.faces[1].set[3].style[0]",
        "lighting.faces[1].set[0].style[1]",
        "lighting.faces[1].set[1].style[1]",
        "lighting.faces[1].set[2].style[1]",
        "lighting.faces[1].set[3].style[1]",
        "lighting.orphan[1]",
    ]


def test_a_zero_filled_lump_tail_is_claimed_padding_zero_over_real_zero_bytes():
    _closure, document, _binary = built(build_bsp(worldlight_tail=b"\0\0\0"))
    root = extension_of(document)
    ranges = ledger_of(root, "#lump15")["ranges"]
    assert ranges[-1]["state"] == "padding-zero"
    assert ranges[-1]["owner"] == "worldLights.padding"
    assert [row["role"] for row in root["anomalies"]] == [
        "face-without-lightmap", "worldlight-length-not-multiple"
    ]


def test_a_non_zero_lump_tail_is_an_evidence_backed_omission_instead():
    _closure, document, _binary = built(build_bsp(worldlight_tail=b"\x07\x07\x07"))
    root = extension_of(document)
    ranges = ledger_of(root, "#lump15")["ranges"]
    assert ranges[-1]["state"] == "omitted-proven"
    omission = [row for row in root["omissions"] if row["role"] == "unused-worldlight-bytes"]
    assert omission and omission[0]["byteLength"] == 3
    assert omission[0]["sha256"] == hashlib.sha256(b"\x07\x07\x07").hexdigest()


def test_a_zero_state_claim_over_a_non_zero_byte_aborts_publication():
    from elysium_pipeline.formats.map_lighting_glb.coverage import SpanLedger

    ledger = SpanLedger("maps/testmap.bsp#lump15", b"\x01\x02\x03\x04")
    with pytest.raises(ByteLedgerError):
        ledger.claim(0, 4, "padding-zero", "worldLights.padding")


def test_the_ordered_ledger_still_refuses_two_claims_on_one_byte():
    from elysium_pipeline.formats.map_lighting_glb.coverage import SpanLedger

    ledger = SpanLedger("maps/testmap.bsp#lump8", b"\x01" * 16)
    ledger.claim(0, 8, "mapped", "lighting.faces[0].set[0].style[0]")
    with pytest.raises(ByteLedgerError):
        ledger.claim(4, 8, "mapped", "lighting.faces[1].set[0].style[0]")


# --------------------------------------------------------------------------------------------
# The bake and the faces
# --------------------------------------------------------------------------------------------


def test_the_bake_reaches_the_unit_as_one_accessor_over_the_whole_lump():
    _closure, document, binary = built(build_bsp())
    root = extension_of(document)
    accessor = document["accessors"][root["samples"]["accessor"]]
    assert accessor["componentType"] == 5121
    assert accessor["type"] == "SCALAR"
    assert accessor["count"] == len(LIGHTING_LUMP)
    assert root["samples"]["format"] == "ColorRGBExp32"
    view = document["bufferViews"][accessor["bufferView"]]
    start = view["byteOffset"]
    assert binary[start:start + accessor["count"]] == LIGHTING_LUMP
    assert root["samples"]["sha256"] == hashlib.sha256(LIGHTING_LUMP).hexdigest()


def test_a_bumped_face_publishes_one_span_per_set_and_style_in_style_major_order():
    _closure, document, _binary = built(build_bsp())
    face = extension_of(document)["faces"][1]
    assert face["bumped"] is True and face["lightmapSets"] == 4 and face["styleCount"] == 2
    assert face["byteLength"] == 4 * 4 * 2
    assert [(span["set"], span["styleIndex"], span["style"], span["offset"], span["length"])
            for span in face["spans"]] == [
        (0, 0, 0, 20, 4), (1, 0, 0, 24, 4),
        (2, 0, 0, 28, 4), (3, 0, 0, 32, 4),
        (0, 1, 6, 36, 4), (1, 1, 6, 40, 4),
        (2, 1, 6, 44, 4), (3, 1, 6, 48, 4),
    ]


def test_a_switched_off_style_owns_the_contiguous_run_of_zero_blocks():
    """The four blocks of one style sit together, so a dark style is a contiguous dark run.

    Over the 108 installed maps, 2,572 bumped two-style faces hold exactly four all-zero blocks
    and every one of those runs is contiguous -- never the stride a set-major layout would put
    them on. The fixture states that fact: style 6 contributes nothing to face 1, so the spans
    labelled `styleIndex` 1 must be exactly the four zero blocks.
    """

    lit = bytes(range(0x20, 0x30))                     # style 0's four sets, no zero byte in them
    dark = bytes(16)                                   # style 6 is off: four all-zero sets
    lump = ORPHAN_HEAD + FACE0_SAMPLES + lit + dark + ORPHAN_TAIL
    _closure, document, binary = built(build_bsp(lighting_lump=lump))
    root = extension_of(document)
    accessor = document["accessors"][root["samples"]["accessor"]]
    view = document["bufferViews"][accessor["bufferView"]]
    samples = binary[view["byteOffset"]:view["byteOffset"] + accessor["count"]]
    face = root["faces"][1]
    dark_spans = {
        span["offset"] for span in face["spans"]
        if not any(samples[span["offset"]:span["offset"] + span["length"]])
    }
    assert dark_spans == {36, 40, 44, 48}
    assert {span["offset"] for span in face["spans"] if span["styleIndex"] == 1} == dark_spans
    assert {span["style"] for span in face["spans"] if span["offset"] in dark_spans} == {6}
    assert sorted(span["set"] for span in face["spans"] if span["offset"] in dark_spans) == [
        0, 1, 2, 3
    ]


def test_an_unbumped_face_restates_the_root_fields_that_locate_its_samples():
    _closure, document, _binary = built(build_bsp())
    face = extension_of(document)["faces"][0]
    assert face["bumped"] is False and face["lightmapSets"] == 1
    assert face["lightmapMins"] == [3, 5] and face["lightmapSize"] == [1, 1]
    assert face["luxelWidth"] == 2 and face["luxelHeight"] == 2
    assert face["byteLength"] == 16 and len(face["spans"]) == 1
    assert face["avgLightColor"][0] == [0, 1, 2, -1]


def test_a_face_without_a_lightmap_publishes_no_span_and_is_counted_not_failed():
    _closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    assert root["faces"][2]["lightOffset"] == -1
    assert root["faces"][2]["spans"] == [] and root["faces"][2]["byteLength"] == 0
    anomaly = [row for row in root["anomalies"] if row["role"] == "face-without-lightmap"][0]
    assert anomaly["count"] == 1 and anomaly["faces"] == [2]


def test_the_style_census_counts_faces_per_style_value():
    _closure, document, _binary = built(build_bsp())
    assert extension_of(document)["styleCensus"] == [
        {"style": 0, "faces": 2}, {"style": 6, "faces": 1}
    ]


def test_a_face_whose_extent_leaves_the_lump_is_an_anomaly_and_claims_nothing():
    face = _face(tex_info=0, styles=(0,) + (255,) * 7, light_offset=len(LIGHTING_LUMP) - 4)
    _closure, document, _binary = built(build_bsp(faces=face))
    root = extension_of(document)
    anomaly = [row for row in root["anomalies"] if row["role"] == "lightofs-out-of-range"][0]
    assert anomaly["face"] == 0 and anomaly["computedByteLength"] == 16
    assert root["faces"][0]["spans"] == []
    assert [entry["state"] for entry in ledger_of(root, "#lump8")["ranges"]] == ["omitted-proven"]


# --------------------------------------------------------------------------------------------
# The orphan runs
# --------------------------------------------------------------------------------------------


def test_bytes_no_face_span_claims_are_carried_as_orphan_lighting_runs():
    _closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    runs = [row for row in root["omissions"] if row["role"] == "orphan-lighting-bytes"]
    assert [(row["offset"], row["byteLength"]) for row in runs] == [(0, 4), (52, 4)]
    assert runs[0]["sha256"] == hashlib.sha256(ORPHAN_HEAD).hexdigest()
    assert runs[1]["sha256"] == hashlib.sha256(ORPHAN_TAIL).hexdigest()
    assert all("not established" in row["reason"] for row in runs)
    states = {
        entry["owner"]: entry["state"] for entry in ledger_of(root, "#lump8")["ranges"]
    }
    assert states["lighting.orphan[0]"] == "omitted-proven"
    assert states["lighting.orphan[1]"] == "omitted-proven"


def test_orphan_runs_are_carried_as_typed_unidentified_rather_than_dropped():
    _closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    typed = root["coverage"]["typedUnidentified"]
    assert [row["role"] for row in typed] == ["orphan-lighting-bytes"]
    assert typed[0]["runs"] == 2 and typed[0]["byteLength"] == 8
    assert root["coverage"]["unresolved"] == [] and root["coverage"]["unsupported"] == []


def test_a_map_whose_faces_claim_every_luxel_carries_no_orphan_row():
    _closure, document, _binary = built(
        build_bsp(lighting_lump=FACE0_SAMPLES + FACE1_SAMPLES, faces=b"".join((
            _face(tex_info=0, styles=(0,) + (255,) * 7, light_offset=0),
            _face(
                tex_info=1,
                styles=(0, 6) + (255,) * 6,
                light_offset=16,
                lightmap_size=(0, 0),
            ),
        )))
    )
    root = extension_of(document)
    assert [row for row in root["omissions"] if row["role"] == "orphan-lighting-bytes"] == []
    assert root["coverage"]["typedUnidentified"] == []


# --------------------------------------------------------------------------------------------
# World lights, displacements and detail-prop lighting
# --------------------------------------------------------------------------------------------


def test_world_lights_are_the_88_byte_records_the_lump_holds():
    closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    assert len(root["worldLights"]) == 2
    first = root["worldLights"][0]
    assert first["sourceOffset"] == closure.worldlights.span_offset
    assert first["intensity"] == [100.0, 200.0, 300.0]
    assert first["typeName"] == "point"
    assert first["radius"]["source"] == 64.0
    assert first["radius"]["metres"] == pytest.approx(64.0 * 0.0254)
    assert first["normal"]["source"] == [0.0, 0.0, -1.0]
    assert first["normal"]["gltf"] == [0.0, -1.0, -0.0]
    assert root["worldLights"][1]["typeName"] == "skylight"
    assert root["lightTypeCensus"] == [
        {"type": 1, "name": "point", "count": 1},
        {"type": 3, "name": "skylight", "count": 1},
    ]


def test_displacement_runs_are_derived_from_the_root_dispinfo_starts():
    _closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    assert [
        (row["alphaStart"], row["alphaLength"], row["samplePositionStart"],
         row["samplePositionLength"])
        for row in root["displacements"]
    ] == [(0, 5, 0, 7), (5, 4, 7, 6)]
    assert root["dispAlphas"]["byteLength"] == 9
    assert root["dispSamplePositions"]["byteLength"] == 13
    alphas = document["accessors"][root["dispAlphas"]["accessor"]]
    assert alphas["count"] == 9


def test_a_map_without_displacement_lighting_publishes_neither_accessor():
    closure, document, _binary = built(build_bsp(displacements=False))
    root = extension_of(document)
    assert root["dispAlphas"] is None and root["dispSamplePositions"] is None
    assert root["displacements"] == []
    assert len(document["accessors"]) == 1
    assert closure.disp_alphas is None


def test_the_detail_prop_lighting_table_is_one_row_per_five_byte_record():
    closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    assert root["detailPropLighting"] == [
        {
            "index": 0,
            "sourceOffset": closure.detail_prop_lighting.span_offset + 4,
            "r": 10, "g": 20, "b": 30, "exponent": -2, "style": 4,
        },
        {
            "index": 1,
            "sourceOffset": closure.detail_prop_lighting.span_offset + 9,
            "r": 1, "g": 2, "b": 3, "exponent": 5, "style": 0,
        },
    ]
    owners = [entry["owner"] for entry in ledger_of(root, "#lump35.dplt")["ranges"]]
    assert owners == ["dplt.count", "dplt.records[0]", "dplt.records[1]"]


def test_a_dplt_payload_of_four_bytes_is_a_complete_empty_table():
    _closure, document, _binary = built(build_bsp(dplt=struct.pack("<i", 0)))
    root = extension_of(document)
    assert root["detailPropLighting"] == []
    assert ledger_of(root, "#lump35.dplt")["ranges"] == [
        {"offset": 0, "length": 4, "state": "mapped", "owner": "dplt.count"}
    ]
    assert root["anomalies"] == [
        row for row in root["anomalies"] if row["role"] != "dplt-count-mismatch"
    ]


def test_a_dplt_count_the_payload_cannot_hold_is_an_anomaly_and_not_a_silent_skip():
    _closure, document, _binary = built(
        build_bsp(dplt=struct.pack("<i", 9) + bytes([1, 2, 3, 4, 5]))
    )
    root = extension_of(document)
    anomaly = [row for row in root["anomalies"] if row["role"] == "dplt-count-mismatch"][0]
    assert anomaly["declared"] == 9 and anomaly["decoded"] == 1
    assert len(root["detailPropLighting"]) == 1


# --------------------------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------------------------


def test_each_reference_the_unit_makes_has_one_dependency_row():
    _closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    assert [(row["role"], row["asset"], row["resolved"]) for row in root["dependencies"]] == [
        ("map", f"vtmb:map:{MAP_NAME}", True),
        ("map-entities", f"vtmb:map-entities:{MAP_NAME}", True),
    ]
    assert root["dependencies"][0]["sha256"] == hashlib.sha256(build_bsp()).hexdigest()


# --------------------------------------------------------------------------------------------
# Validation
# --------------------------------------------------------------------------------------------


def test_a_published_unit_round_trips_through_validation(tmp_path):
    data = build_bsp()
    destination = export_unit(tmp_path, data)
    assert destination.name == f"{MAP_NAME}.lighting.glb"
    assert destination.parent.name == "maps"
    summary = validation.validate(destination)
    assert summary["asset"] == f"vtmb:map-lighting:{MAP_NAME}"
    assert summary["byteCoveragePercent"] == [100.0] * 5
    assert summary["unresolved"] == 0 and summary["unsupported"] == 0
    assert summary["orphanRuns"] == 2 and summary["orphanBytes"] == 8
    assert summary["worldLights"] == 2 and summary["faces"] == 3 and summary["litFaces"] == 2
    assert any("orphan-lighting-bytes" in line for line in validation.warnings_for(summary))
    document, binary = read_glb(destination)
    assert document["extensions"][EXTENSION]["schemaVersion"] == lighting.SCHEMA_VERSION
    assert binary


def test_the_same_source_yields_the_same_bytes(tmp_path):
    data = build_bsp()
    first = export_unit(tmp_path / "one", data).read_bytes()
    second = export_unit(tmp_path / "two", data).read_bytes()
    assert first == second


def test_the_validator_rejects_a_tampered_ledger(tmp_path):
    _closure, document, binary = built(build_bsp())
    root = document["extensions"][EXTENSION]
    ranges = root["coverage"]["byteLedger"][0]["ranges"]
    ranges[0] = dict(ranges[0], length=ranges[0]["length"] + 1)
    with pytest.raises(validation.MapLightingGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_rejects_a_face_span_outside_the_samples_accessor():
    _closure, document, binary = built(build_bsp())
    root = document["extensions"][EXTENSION]
    face = root["faces"][0]
    face["spans"][0]["offset"] = len(LIGHTING_LUMP)
    face["lightOffset"] = len(LIGHTING_LUMP)
    with pytest.raises(validation.MapLightingGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_rejects_an_accessor_that_is_not_the_lump_it_claims(tmp_path):
    data = build_bsp()
    closure, document, binary = built(data)
    tampered = bytearray(binary)
    tampered[0] ^= 0xFF
    with pytest.raises(validation.MapLightingGlbValidationError):
        validation.validate_document(
            document, bytes(tampered), source_members=closure.members(), map_bytes=closure.data
        )


def test_the_validator_rejects_a_face_row_the_root_does_not_state():
    data = build_bsp()
    closure, document, binary = built(data)
    document["extensions"][EXTENSION]["faces"][0]["lightmapMins"] = [99, 99]
    with pytest.raises(validation.MapLightingGlbValidationError):
        validation.validate_document(
            document, binary, source_members=closure.members(), map_bytes=closure.data
        )


def test_the_validator_rejects_a_unit_that_declares_a_scene():
    _closure, document, binary = built(build_bsp())
    document["scenes"] = [{"nodes": []}]
    with pytest.raises(validation.MapLightingGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_rejects_a_missing_dependency_row():
    _closure, document, binary = built(build_bsp())
    document["extensions"][EXTENSION]["dependencies"] = [
        document["extensions"][EXTENSION]["dependencies"][0]
    ]
    with pytest.raises(validation.MapLightingGlbValidationError):
        validation.validate_document(document, binary)


def test_a_container_that_is_not_this_seam_is_refused(tmp_path):
    destination = tmp_path / "not-a-unit.glb"
    destination.write_bytes(encode_glb({"asset": {"version": "2.0"}}, b""))
    with pytest.raises(validation.MapLightingGlbValidationError):
        validation.validate(destination)


# --------------------------------------------------------------------------------------------
# The anomaly and omission roles the specification names
# --------------------------------------------------------------------------------------------


def test_two_faces_that_claim_one_byte_leave_the_later_face_owning_nothing():
    overlapping = b"".join(
        (
            _face(tex_info=0, styles=(0,) + (255,) * 7, light_offset=4),
            _face(tex_info=0, styles=(0,) + (255,) * 7, light_offset=8),
        )
    )
    _closure, document, _binary = built(build_bsp(faces=overlapping))
    root = extension_of(document)
    anomaly = [row for row in root["anomalies"] if row["role"] == "span-overlap"][0]
    assert anomaly["face"] == 1 and anomaly["offset"] == 8
    assert anomaly["overlaps"] == "lighting.faces[0].set[0].style[0]"
    # The losing face keeps its row and states that it owns none of the lump, so the orphan run
    # over those bytes contradicts no face row.
    assert root["faces"][1]["spans"] == [] and root["faces"][1]["byteLength"] == 0
    owners = [entry["owner"] for entry in ledger_of(root, "#lump8")["ranges"]]
    assert owners == [
        "lighting.orphan[0]", "lighting.faces[0].set[0].style[0]", "lighting.orphan[1]"
    ]


def test_a_face_naming_no_texinfo_row_is_an_anomaly_and_reads_as_unbumped():
    face = _face(tex_info=99, styles=(0,) + (255,) * 7, light_offset=4)
    _closure, document, _binary = built(build_bsp(faces=face))
    root = extension_of(document)
    anomaly = [row for row in root["anomalies"] if row["role"] == "texinfo-out-of-range"][0]
    assert anomaly["face"] == 0 and anomaly["texInfo"] == 99 and anomaly["texinfoCount"] == 2
    assert root["faces"][0]["bumped"] is False and root["faces"][0]["lightmapSets"] == 1


def test_a_displacement_start_outside_its_lump_is_an_anomaly_and_claims_no_run():
    _closure, document, _binary = built(
        build_bsp(dispinfos=_dispinfo(power=2, alpha_start=50, sample_start=0, map_face=0))
    )
    root = extension_of(document)
    anomaly = [
        row for row in root["anomalies"] if row["role"] == "displacement-run-out-of-range"
    ][0]
    assert anomaly["displacement"] == 0 and anomaly["field"] == "lightmapAlphaStart"
    assert anomaly["start"] == 50 and anomaly["lumpByteLength"] == 9
    assert root["displacements"][0]["alphaLength"] == 0


def test_a_world_light_field_the_json_chunk_cannot_state_keeps_its_bytes_and_a_digest():
    data = build_bsp(worldlight_tail=_world_light_non_finite())
    _closure, document, _binary = built(data)
    root = extension_of(document)
    anomaly = [
        row for row in root["anomalies"] if row["role"] == "non-finite-world-light-field"
    ][0]
    assert anomaly["index"] == 2 and anomaly["fields"] == ["radius"]
    assert anomaly["sha256"] == hashlib.sha256(_world_light_non_finite()).hexdigest()
    assert "bytesHex" not in anomaly and anomaly["finiteFields"]["cluster"] == 7
    assert [light["index"] for light in root["worldLights"]] == [0, 1]
    ranges = ledger_of(root, "#lump15")["ranges"]
    assert [entry["owner"] for entry in ranges] == [
        "worldLights[0]", "worldLights[1]", "worldLights[2]"
    ]


def test_dplt_bytes_no_record_addresses_are_an_omission_when_they_are_not_zero():
    payload = struct.pack("<i", 1) + bytes([1, 2, 3, 4, 5]) + b"\x09\x09\x09"
    _closure, document, _binary = built(build_bsp(dplt=payload))
    root = extension_of(document)
    omission = [row for row in root["omissions"] if row["role"] == "unused-dplt-bytes"][0]
    assert omission["offset"] == 9 and omission["byteLength"] == 3
    assert omission["sha256"] == hashlib.sha256(b"\x09\x09\x09").hexdigest()
    ranges = ledger_of(root, "#lump35.dplt")["ranges"]
    assert ranges[-1] == {
        "offset": 9, "length": 3, "state": "omitted-proven", "owner": "dplt.unused"
    }


def test_a_zero_filled_dplt_tail_is_claimed_padding_zero_instead():
    payload = struct.pack("<i", 1) + bytes([1, 2, 3, 4, 5]) + b"\0\0\0"
    _closure, document, _binary = built(build_bsp(dplt=payload))
    root = extension_of(document)
    assert [row for row in root["omissions"] if row["role"] == "unused-dplt-bytes"] == []
    ranges = ledger_of(root, "#lump35.dplt")["ranges"]
    assert ranges[-1] == {
        "offset": 9, "length": 3, "state": "padding-zero", "owner": "dplt.padding"
    }


def test_a_dplt_payload_too_short_to_hold_its_count_word_is_carried_whole(tmp_path):
    _closure, document, _binary = built(build_bsp(dplt=b"\x01\x02"))
    root = extension_of(document)
    assert [row["role"] for row in root["anomalies"] if row["role"] == "dplt-count-mismatch"]
    omission = [row for row in root["omissions"] if row["role"] == "short-dplt-payload"][0]
    assert omission["byteLength"] == 2
    assert omission["sha256"] == hashlib.sha256(b"\x01\x02").hexdigest()
    assert root["detailPropLighting"] == []
    assert ledger_of(root, "#lump35.dplt")["ranges"] == [
        {"offset": 0, "length": 2, "state": "omitted-proven", "owner": "dplt.unused"}
    ]
    summary = validation.validate(export_unit(tmp_path, build_bsp(dplt=b"\x01\x02")))
    assert summary["detailPropLighting"] == 0


def test_an_empty_owned_span_is_published_as_an_evidence_backed_omission(tmp_path):
    data = build_bsp(worldlights=b"")
    _closure, document, _binary = built(data)
    root = extension_of(document)
    omission = [row for row in root["omissions"] if row["role"] == "empty-member"][0]
    assert omission["sourcePath"] == f"{MAP_KEY}#lump15" and omission["byteLength"] == 0
    assert root["worldLights"] == [] and root["lightTypeCensus"] == []
    summary = validation.validate(export_unit(tmp_path, data))
    assert summary["worldLights"] == 0
    assert summary["byteCoveragePercent"] == [100.0] * 5


# --------------------------------------------------------------------------------------------
# Coverage vocabulary, the accessor blocks and the ledger-to-face join
# --------------------------------------------------------------------------------------------


def test_every_mapped_field_carries_the_semantic_state_the_specification_assigns_it():
    _closure, document, _binary = built(build_bsp())
    mapped = extension_of(document)["coverage"]["mapped"]
    assert {row["field"]: row["state"] for row in mapped} == {
        "map": "mapped",
        "samples": "mapped",
        "faces": "derived",
        "faces[].bumped": "derived",
        "styleCensus": "derived",
        "worldLights": "mapped",
        "lightTypeCensus": "derived",
        "dispAlphas": "mapped",
        "dispSamplePositions": "mapped",
        "displacements": "derived",
        "detailPropLighting": "mapped",
    }


def test_an_accessor_block_names_its_accessor_and_never_restates_the_buffer_view():
    _closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    for name in ("samples", "dispAlphas", "dispSamplePositions"):
        block = root[name]
        assert "bufferView" not in block
        assert document["accessors"][block["accessor"]]["bufferView"] is not None


def test_an_orphan_run_that_ends_at_a_lit_face_says_which_face_it_precedes():
    _closure, document, _binary = built(build_bsp())
    root = extension_of(document)
    runs = [row for row in root["omissions"] if row["role"] == "orphan-lighting-bytes"]
    assert runs[0]["precedesLitFace"] == 0 and runs[0]["luxelBlocks"] == 1
    assert "ending exactly at the lightofs of lit face 0" in runs[0]["reason"]
    assert "precedesLitFace" not in runs[1]
    assert all("not established" in row["reason"] for row in runs)
    typed = root["coverage"]["typedUnidentified"][0]
    assert typed["runsPrecedingALitFace"] == 1 and typed["runs"] == 2


def test_the_validator_rejects_a_ledger_range_that_is_not_the_face_span_it_names():
    _closure, document, binary = built(build_bsp())
    root = document["extensions"][EXTENSION]
    ranges = root["coverage"]["byteLedger"][0]["ranges"]
    face, orphan = ranges[1], ranges[2]
    # A range of the right length at the wrong offset: the sweep still finds the table gapless.
    ranges[1] = dict(face, length=face["length"] - 4)
    ranges[2] = dict(orphan, offset=orphan["offset"] - 4, length=orphan["length"] + 4)
    with pytest.raises(validation.MapLightingGlbValidationError):
        validation.validate_document(document, binary)
