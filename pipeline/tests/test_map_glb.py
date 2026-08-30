"""Contract tests for the Map GLB seam.

Every fixture here is synthetic: a BSP built byte by byte in this module, handed to the seam
through a fake index and an injected reader, so nothing in this file depends on an installed
copy of the game. What the tests pin is the seam's own promises -- the partition proof, the
ledger's gaplessness, the identity rule, each dependency role, each evidence-backed omission the
specification names, and the validator's refusal of a unit that has been tampered with.
"""

from __future__ import annotations

import hashlib
import json
import math
import struct
import zlib

import pytest

import elysium_pipeline.formats.map_glb.pakfile as pakfile
import elysium_pipeline.formats.map_glb.partition as map_partition
from elysium_pipeline.exporters import map_glb as exporter
from elysium_pipeline.formats import map_glb
from elysium_pipeline.formats.unit_contract import encode_glb, read_glb
from elysium_pipeline.validation import map_glb as validation

MAP_NAME = "testmap"
MAP_KEY = f"maps/{MAP_NAME}.bsp"
MATERIAL_NAME = "PLASTER/WALL"
PATCHED_NAME = f"maps/{MAP_NAME}/plaster/wall_1_2_3"
PROP_MODEL = "models/props/box.mdl"
DETAIL_MODEL = "models/props/weed.mdl"


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

    def pad(self, count: int, filler: bytes = b"\0") -> None:
        self.body.extend(filler * count)

    def add(self, index: int, payload: bytes) -> tuple[int, int]:
        offset = self.cursor
        self.body.extend(payload)
        self.rows[index] = (offset, len(payload))
        return offset, len(payload)

    def finish(self, *, revision: int = 7, trailer: bool = True) -> bytes:
        header = bytearray(struct.pack("<4si", b"VBSP", 17))
        for index in range(map_partition.LUMP_COUNT):
            offset, length = self.rows.get(index, (0, 0))
            header.extend(struct.pack("<iii4s", offset, length, 0, b"\0\0\0\0"))
        header.extend(struct.pack("<i", revision))
        assert len(header) == map_partition.HEADER_BYTES
        data = bytes(header) + bytes(self.body)
        if trailer:
            entities_offset = self.rows.get(0, (0, 0))[0]
            data += struct.pack("<I", entities_offset) + map_partition.TRAILER_SIGNATURE
        return data


def _zip_member(name: str, payload: bytes) -> tuple[bytes, bytes, int]:
    """One stored ZIP member: its local record, a central record factory and its length."""

    raw = name.encode("latin-1")
    local = struct.pack(
        "<IHHHHHIIIHH", 0x04034B50, 20, 0, 0, 0, 0, zlib.crc32(payload), len(payload),
        len(payload), len(raw), 0,
    ) + raw + payload
    return local, raw, len(payload)


def _pakfile(members: dict[str, bytes], gap: bytes = b"") -> bytes:
    body = bytearray()
    central = bytearray()
    for name, payload in members.items():
        offset = len(body)
        local, raw, size = _zip_member(name, payload)
        body.extend(local)
        central.extend(
            struct.pack(
                "<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, 0, 0, 0, 0, zlib.crc32(payload),
                size, size, len(raw), 0, 0, 0, 0, 0, offset,
            )
            + raw
        )
    body.extend(gap)                    # bytes no ZIP record addresses
    central_offset = len(body)
    body.extend(central)
    body.extend(
        struct.pack(
            "<IHHHHIIH", 0x06054B50, 0, 0, len(members), len(members), len(central),
            central_offset, 0,
        )
    )
    return bytes(body)


def _ivps_solid(*, magic: bytes = b"IVPS", points=None) -> bytes:
    """One compact surface holding a single tetrahedron ledge."""

    points = points or [
        (0.0, 0.0, 0.0, 0.0),
        (1.0, 0.0, 0.0, 0.0),
        (0.0, 1.0, 0.0, 0.0),
        (0.0, 0.0, 1.0, 0.0),
    ]
    triangles = [(0, 1, 2), (0, 2, 3), (0, 3, 1), (1, 3, 2)]
    ledge = bytearray(struct.pack("<iiIhh", 80, 0, (16 << 8) | 1, len(triangles), 0))
    for corners in triangles:
        ledge.extend(struct.pack("<I", 0))
        for corner in corners:
            ledge.extend(struct.pack("<I", corner))
    assert len(ledge) == 16 + len(triangles) * 16
    ledge.extend(b"\0" * (80 - len(ledge)))
    for point in points:
        ledge.extend(struct.pack("<4f", *point))
    tree = struct.pack("<ii", 0, 28) + b"\0" * 20          # leaf node: right == 0
    body = bytearray()
    body.extend(struct.pack("<3f", 0.5, 0.5, 0.5))          # mass centre
    body.extend(struct.pack("<3f", 1.0, 1.0, 1.0))          # rotation inertia
    body.extend(struct.pack("<f", 1.0))                     # upper limit radius
    body.extend(struct.pack("<I", (0 << 8) | 0))            # max deviation / size
    body.extend(struct.pack("<i", 48))                      # ledge tree root
    body.extend(struct.pack("<2i", 0, 0))
    body.extend(magic)
    assert len(body) == 48
    body.extend(tree)
    body.extend(ledge)
    return struct.pack("<i", len(body)) + bytes(body)


def _mopp_solid(payload: bytes) -> bytes:
    body = bytearray()
    body.extend(struct.pack("<3f", 0.0, 0.0, 0.0))
    body.extend(struct.pack("<3f", 1.0, 1.0, 1.0))
    body.extend(struct.pack("<f", 2.0))
    body.extend(struct.pack("<I", 0))
    body.extend(struct.pack("<i", 48))
    body.extend(struct.pack("<2i", 0, 0))
    body.extend(b"MOPP")
    body.extend(payload)
    return struct.pack("<i", len(body)) + bytes(body)


def _physcollide(solids: list[bytes], keytext: bytes) -> bytes:
    data = bytearray()
    body = b"".join(solids)
    data.extend(struct.pack("<4i", 0, len(body), len(keytext), len(solids)))
    data.extend(body)
    data.extend(keytext)
    data.extend(struct.pack("<4i", -1, -1, 0, 0))
    return bytes(data)


def _face(
    *,
    first_edge: int,
    num_edges: int,
    tex_info: int,
    disp_info: int = -1,
    plane: int = 0,
    lightmap_size=(1, 1),
) -> bytes:
    record = bytearray(b"\0" * 104)
    struct.pack_into("<HBB", record, 32, plane, 0, 0)
    struct.pack_into("<i3hH", record, 36, first_edge, num_edges, tex_info, disp_info, 0)
    struct.pack_into("<8B", record, 48, *([0] * 8))
    struct.pack_into("<if", record, 72, -1, 16.0)
    struct.pack_into("<2i", record, 80, 0, 0)
    struct.pack_into("<2i", record, 88, *lightmap_size)
    struct.pack_into("<iI", record, 96, -1, 0)
    return bytes(record)


def _texinfo(tex_data: int, flags: int = 0) -> bytes:
    vectors = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0]
    lightmap = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0]
    return struct.pack("<16f2i", *vectors, *lightmap, flags, tex_data)


def _static_prop(
    *, prop_type=0, flags=0, lighting=(1.0, 2.0, 3.0), origin=(16.0, 32.0, 48.0)
) -> bytes:
    return struct.pack(
        "<6f3H2Bi5f",
        *origin,
        0.0, 90.0, 0.0,
        prop_type, 0, 1,
        6, flags,
        0,
        0.0, 0.0,
        *lighting,
    )


def _detail_prop() -> bytes:
    return struct.pack(
        "<6f2H4BI4B",
        8.0, 4.0, 2.0,
        0.0, 0.0, 0.0,
        0, 0,
        255, 255, 255, 255,
        0,
        1, 2, 3, 4,
    )


def _dictionary(name: str) -> bytes:
    raw = name.encode("latin-1")
    return raw + b"\0" * (128 - len(raw))


def build_bsp(
    *,
    lighting_origin_nonfinite: bool = False,
    detail_trailing: bool = False,
    inter_lump_fill: bytes | None = None,
    mopp: bytes | None = None,
    prop_model: str = PROP_MODEL,
    pakfile_extra: dict[str, bytes] | None = None,
    pakfile_gap: bytes = b"",
    displacement: bool = False,
    zero_area_face: bool = False,
    unreferenced_records: bool = False,
    texdata_view_size: tuple[int, int] = (64, 64),
    prop_type: int = 0,
    prop_origin: tuple[float, float, float] = (16.0, 32.0, 48.0),
) -> bytes:
    builder = BspBuilder()
    builder.add(0, b'{\n"classname" "worldspawn"\n}\n\0')
    builder.pad(2)                                          # padding-zero between two lumps
    builder.add(1, struct.pack("<4fi", 0.0, 0.0, 1.0, 0.0, 2) * 2)
    string_blob = (MATERIAL_NAME + "\0" + PATCHED_NAME + "\0").encode("latin-1")
    builder.add(43, string_blob)
    builder.add(44, struct.pack("<2i", 0, len(MATERIAL_NAME) + 1))
    view_width, view_height = texdata_view_size
    builder.add(
        2,
        struct.pack("<3f5i", 0.5, 0.5, 0.5, 0, 64, 64, view_width, view_height)
        + struct.pack("<3f5i", 0.5, 0.5, 0.5, 1, 64, 64, 64, 64),
    )
    vertices = [
        (0.0, 0.0, 0.0), (64.0, 0.0, 0.0), (64.0, 64.0, 0.0), (0.0, 64.0, 0.0),
    ]
    edges = [(index, (index + 1) % 4) for index in range(4)]
    surf_edges = [0, 1, 2, 3]
    faces = [_face(first_edge=0, num_edges=4, tex_info=0)]
    power, side = 2, 5
    if displacement:
        vertices += [
            (0.0, 0.0, 64.0), (64.0, 0.0, 64.0), (64.0, 64.0, 64.0), (0.0, 64.0, 64.0),
        ]
        edges += [(4 + index, 4 + (index + 1) % 4) for index in range(4)]
        surf_edges += [4, 5, 6, 7]
        faces.append(_face(first_edge=4, num_edges=4, tex_info=0, disp_info=0))
        dispinfo = bytearray(b"\0" * 176)
        struct.pack_into("<3f", dispinfo, 0, *vertices[4])
        struct.pack_into("<4i", dispinfo, 12, 0, 0, power, 0)
        builder.add(26, bytes(dispinfo))
        builder.add(
            33,
            b"".join(
                struct.pack("<5f", 0.0, 0.0, 1.0, float(index % 3), index / (side * side))
                for index in range(side * side)
            ),
        )
        builder.add(48, struct.pack(f"<{2 * (side - 1) * (side - 1)}H", *([0] * 32)))
    if zero_area_face:
        # Four collinear corners: three edges or more, and no area at all.
        first = len(vertices)
        vertices += [(float(step) * 16.0, 0.0, 0.0) for step in range(4)]
        edges += [(first + step, first + (step + 1) % 4) for step in range(4)]
        first_edge = len(surf_edges)
        surf_edges += [len(edges) - 4 + step for step in range(4)]
        faces.append(_face(first_edge=first_edge, num_edges=4, tex_info=0))
    normals = [(0.0, 0.0, 1.0)]
    if unreferenced_records:
        # Two records at the end of their lumps that no edge and no normal index names.
        vertices = vertices + [(128.0, 128.0, 128.0)]
        normals = normals + [(0.0, 1.0, 0.0)]
    builder.add(3, b"".join(struct.pack("<3f", *point) for point in vertices))
    builder.add(12, b"".join(struct.pack("<2H", *edge) for edge in edges))
    builder.add(13, struct.pack(f"<{len(surf_edges)}i", *surf_edges))
    builder.add(6, _texinfo(0) + _texinfo(1))
    builder.add(7, b"".join(faces))
    builder.add(27, _face(first_edge=0, num_edges=4, tex_info=0))
    builder.add(30, b"".join(struct.pack("<3f", *normal) for normal in normals))
    builder.add(31, struct.pack(f"<{4 * len(faces)}H", *([0] * 4 * len(faces))))
    builder.add(4, struct.pack("<i", 0) + b"\x01\x02\x03\x04")
    builder.add(8, b"\x10\x20\x30\x40" * 4)
    builder.add(15, b"\0" * 88)
    builder.add(5, struct.pack("<3i", 0, -1, -1) + struct.pack("<3h", 0, 0, 0)
                + struct.pack("<3h", 64, 64, 64) + struct.pack("<2H2h", 0, 1, 0, 0))
    builder.add(10, struct.pack("<ihH", 1, 0, 0) + struct.pack("<3h", 0, 0, 0)
                + struct.pack("<3h", 64, 64, 64) + struct.pack("<4H", 0, 1, 0, 0)
                + struct.pack("<2h", -1, 0))
    builder.add(16, struct.pack("<H", 0))
    builder.add(17, struct.pack("<H", 0))
    builder.add(18, struct.pack("<3i", 0, 1, 1))
    builder.add(19, struct.pack("<H3h", 0, 0, -1, 0))
    builder.add(20, struct.pack("<2i", 0, 0))
    builder.add(21, struct.pack("<4Hi", 1, 0, 0, 0, 0))
    builder.add(14, struct.pack("<9f3i", -1.0, -1.0, -1.0, 65.0, 65.0, 65.0, 0.0, 0.0, 0.0,
                                0, 0, len(faces)))
    builder.add(42, struct.pack("<4i", 1, 2, 3, 0))
    builder.add(46, struct.pack("<H", 0))
    builder.add(47, struct.pack("<h", -1))
    if inter_lump_fill:
        builder.pad(len(inter_lump_fill), inter_lump_fill)
    solids = [_ivps_solid()]
    if mopp is not None:
        solids.append(_mopp_solid(mopp))
    builder.add(
        29,
        _physcollide(solids, b'solid {\n"index" "0"\n"surfaceprop" "concrete"\n}\n\0'),
    )
    members = {
        f"materials/{PATCHED_NAME}.vmt": b'"LightmappedGeneric"\n{\n}\n',
        f"materials/maps/{MAP_NAME}/c1_2_3.tth": b"TTH-BYTES",
    }
    members.update(pakfile_extra or {})
    builder.add(40, _pakfile(members, pakfile_gap))

    sprp = bytearray(struct.pack("<i", 1))
    sprp.extend(_dictionary(prop_model))
    sprp.extend(struct.pack("<i", 1))
    sprp.extend(struct.pack("<H", 0))
    sprp.extend(struct.pack("<i", 1))
    lighting = (float("nan"), 2.0, 3.0) if lighting_origin_nonfinite else (1.0, 2.0, 3.0)
    sprp.extend(_static_prop(lighting=lighting, prop_type=prop_type, origin=prop_origin))
    dprp = bytearray(struct.pack("<i", 1))
    dprp.extend(_dictionary(DETAIL_MODEL))
    dprp.extend(struct.pack("<i", 1))
    dprp.extend(_detail_prop())
    if detail_trailing:
        dprp.extend(b"\x01\x02\x03\x04")
    dplt = struct.pack("<i", 1) + b"\x10\x20\x30\x01\x02"

    directory_bytes = 4 + 3 * 16
    game_offset = builder.cursor
    payload_offset = game_offset + directory_bytes
    directory = bytearray(struct.pack("<i", 3))
    for identifier, version, payload in (
        (b"sprp", 4, bytes(sprp)), (b"dprp", 2, bytes(dprp)), (b"dplt", 0, dplt)
    ):
        directory.extend(
            struct.pack("<4sHHii", identifier[::-1], 0, version, payload_offset, len(payload))
        )
        payload_offset += len(payload)
    builder.add(35, bytes(directory) + bytes(sprp) + bytes(dprp) + dplt)
    return builder.finish()


def make_index(data: bytes, *, models=(PROP_MODEL, DETAIL_MODEL)) -> dict:
    index = {MAP_KEY: ("loose", r"C:\game\Unofficial_Patch\maps\testmap.bsp")}
    for path in models:
        index[path] = ("loose", r"C:\game\Vampire\\" + path.replace("/", "\\"))
    index["materials/plaster/wall.vmt"] = ("vpk", ("pack001.vpk", 16, 32))
    index["scripts/surfaceproperties.txt"] = ("loose", r"C:\game\Vampire\scripts\sp.txt")
    return index


def reader(data: bytes):
    return lambda index, key: data if key == MAP_KEY else b""


def export_unit(tmp_path, data: bytes, **kwargs):
    index = make_index(data, **kwargs)
    return exporter.export(index, MAP_NAME, tmp_path, read_bytes=reader(data)), index


def decoded(data: bytes, **kwargs):
    index = make_index(data, **kwargs)
    closure = map_glb.load_source_closure(index, MAP_NAME, read_bytes=reader(data))
    return closure, map_glb.decode_map(closure, member_exists=lambda path: path.lower() in index)


def extension_of(document) -> dict:
    return document["extensions"][map_glb.MAP_EXTENSION]


# --------------------------------------------------------------------------------------------
# The partition
# --------------------------------------------------------------------------------------------


def test_the_partition_accounts_for_every_byte_of_the_file_exactly_once():
    data = build_bsp()
    part = map_partition.partition(data)
    cursor = 0
    for region in part.regions:
        assert region.offset == cursor
        cursor += region.length
    assert cursor == len(data)


def test_the_partition_hands_each_sub_unit_the_lumps_its_seam_owns():
    part = map_partition.partition(build_bsp())
    owners = part.owners()
    assert owners["map-entities"] == [0]
    assert owners["map-lighting"] == [8, 15]
    assert owners["map-visibility"] == [4]
    lighting = part.spans_for("map-lighting")
    assert {span.lump for span in lighting} == {8, 15, 35}
    assert [span.game_lump for span in lighting if span.lump == 35] == ["dplt"]


def test_the_root_keeps_the_game_lump_directory_and_gives_away_only_the_dplt_payload():
    part = map_partition.partition(build_bsp())
    directory = [
        region for region in part.regions if region.owner == "header.gameLumpDirectory"
    ]
    assert len(directory) == 1 and directory[0].unit == "map"
    dplt = [region for region in part.regions if region.game_lump == "dplt"]
    assert dplt and dplt[0].unit == "map-lighting"


def test_a_lump_that_overlaps_another_fails_the_map():
    data = bytearray(build_bsp())
    offset, length = struct.unpack_from("<ii", data, 8 + 1 * 16)
    struct.pack_into("<ii", data, 8 + 5 * 16, offset, length)   # NODES onto PLANES
    with pytest.raises(map_partition.PartitionError):
        map_partition.partition(bytes(data))


def test_a_file_that_is_not_a_v17_bsp_is_refused():
    data = bytearray(build_bsp())
    struct.pack_into("<i", data, 4, 19)
    with pytest.raises(map_partition.PartitionError):
        map_partition.partition(bytes(data))


def test_the_partition_digest_of_a_sub_units_spans_is_the_concatenation_of_them():
    data = build_bsp()
    part = map_partition.partition(data)
    digest = hashlib.sha256()
    for span in part.spans_for("map-visibility"):
        digest.update(data[span.offset:span.offset + span.length])
    assert part.sha256_for("map-visibility", data) == digest.hexdigest()


# --------------------------------------------------------------------------------------------
# Identity and source resolution
# --------------------------------------------------------------------------------------------


def test_one_identity_per_file_below_the_maps_family_directory():
    assert map_glb.asset_id("sp_tutorial_1") == "vtmb:map:sp_tutorial_1"
    assert map_glb.output_relative_path("sp_tutorial_1").as_posix() == "maps/sp_tutorial_1.glb"


def test_the_key_tolerates_the_root_prefix_and_the_source_extension():
    assert map_glb.normalize_key("maps/SP_Tutorial_1.bsp") == "sp_tutorial_1"
    assert map_glb.normalize_key("sp_tutorial_1.bsp") == "sp_tutorial_1"
    with pytest.raises(map_glb.MapKeyError):
        map_glb.normalize_key("maps/sub/dir")


def test_source_keys_lists_only_the_maps_directly_below_the_maps_directory():
    index = {
        "maps/a.bsp": ("loose", "a"),
        "maps/graphs/b.bsp": ("loose", "b"),
        "materials/x.vmt": ("loose", "x"),
    }
    assert exporter.source_keys(index) == ["a"]


def test_the_sub_unit_identities_name_the_three_siblings_and_their_spans():
    closure, model = decoded(build_bsp())
    assets = [row["asset"] for row in model.sub_units]
    assert assets == [
        f"vtmb:map-entities:{MAP_NAME}",
        f"vtmb:map-lighting:{MAP_NAME}",
        f"vtmb:map-visibility:{MAP_NAME}",
    ]
    lighting = model.sub_units[1]
    assert lighting["path"] == f"maps/{MAP_NAME}.lighting.glb"
    assert lighting["byteLength"] == sum(span["length"] for span in lighting["spans"])


# --------------------------------------------------------------------------------------------
# The byte ledger
# --------------------------------------------------------------------------------------------


def test_every_byte_of_the_member_is_claimed_exactly_once():
    data = build_bsp()
    _closure, model = decoded(data)
    from elysium_pipeline.formats.map_glb import coverage as coverage_module

    ledger = coverage_module.byte_ledger(model)[0]
    assert ledger["byteLength"] == len(data)
    assert ledger["accountedBytes"] == len(data)
    assert ledger["coveragePercent"] == 100.0
    cursor = 0
    for span in ledger["ranges"]:
        assert span["offset"] == cursor
        cursor += span["length"]
    assert cursor == len(data)
    assert sum(ledger["stateBytes"].values()) == len(data)


def test_the_bytes_between_two_lumps_are_padding_only_when_the_source_stores_zeros():
    from elysium_pipeline.formats.map_glb import coverage as coverage_module

    _closure, zeroed = decoded(build_bsp())
    padding = [
        row for row in coverage_module.byte_ledger(zeroed)[0]["ranges"]
        if row["state"] == "padding-zero"
    ]
    assert padding, "the fixture lays zero fill between two lumps"

    _closure, filled = decoded(build_bsp(inter_lump_fill=b"\xcd\xcd"))
    fills = [row["role"] for row in filled.omissions]
    assert "inter-lump-fill" in fills
    proven = [
        row for row in coverage_module.byte_ledger(filled)[0]["ranges"]
        if row["owner"].endswith("inter-lump-fill")
    ]
    assert proven and proven[0]["state"] == "omitted-proven"


def test_a_sub_units_lumps_enter_the_root_ledger_as_omitted_proven_spans():
    from elysium_pipeline.formats.map_glb import coverage as coverage_module

    _closure, model = decoded(build_bsp())
    rows = [
        row for row in coverage_module.byte_ledger(model)[0]["ranges"]
        if row["owner"].startswith("subUnits[")
    ]
    assert rows and {row["state"] for row in rows} == {"omitted-proven"}
    proven = {row["unit"] for row in model.omitted_proven if row["role"] == "sub-unit-span"}
    assert proven == {"map-entities", "map-lighting", "map-visibility"}


def test_a_ledger_range_never_leaves_the_partition_region_its_owner_names():
    from elysium_pipeline.formats.map_glb import coverage as coverage_module

    data = build_bsp()
    _closure, model = decoded(data)
    regions = model.partition.rows()
    ranges = coverage_module.byte_ledger(model)[0]["ranges"]
    assert ranges[0] == {
        "offset": 0,
        "length": map_partition.HEADER_BYTES,
        "state": "mapped",
        "owner": "header",
    }
    for entry in ranges:
        holder = [
            region for region in regions
            if region["offset"] <= entry["offset"]
            and entry["offset"] + entry["length"] <= region["offset"] + region["length"]
        ]
        assert holder, f"{entry} spans more than one partition region"
        unit = holder[0]["unit"]
        if entry["owner"].startswith("subUnits["):
            assert entry["owner"].startswith(f"subUnits[{unit}]")
        else:
            assert unit == "map"


def test_two_adjacent_claims_with_unrelated_owners_stay_two_ledger_rows():
    from elysium_pipeline.formats.map_glb import claims as claim_tools

    merged = claim_tools.merge(
        [(0, 4, "mapped", "textures"), (4, 4, "mapped", "planes")]
    )
    assert merged == [(0, 4, "mapped", "textures"), (4, 4, "mapped", "planes")]
    siblings = claim_tools.merge(
        [
            (0, 4, "mapped", "pakfile.entries[0].centralHeader"),
            (4, 4, "mapped", "pakfile.entries[1].centralHeader"),
        ]
    )
    assert siblings == [(0, 8, "mapped", "pakfile")]
    split = claim_tools.merge(
        [
            (0, 4, "mapped", "pakfile.entries[0].centralHeader"),
            (4, 4, "mapped", "pakfile.entries[1].centralHeader"),
        ],
        boundaries=[4],
    )
    assert len(split) == 2


def test_a_ledger_whose_range_spans_two_lumps_is_refused(tmp_path):
    from elysium_pipeline.formats.unit_contract import ranges_sha256

    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    ledger = extension_of(document)["coverage"]["byteLedger"][0]
    ranges = ledger["ranges"]
    fused = next(
        index for index in range(len(ranges) - 1)
        if ranges[index]["state"] == ranges[index + 1]["state"]
        and ranges[index]["owner"] != ranges[index + 1]["owner"]
    )
    ranges[fused]["length"] += ranges[fused + 1]["length"]
    del ranges[fused + 1]
    ledger["rangesSha256"] = ranges_sha256(
        ledger["sourcePath"], ledger["byteLength"], ranges
    )
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary)


def test_the_file_trailer_is_carried_as_a_typed_but_unidentified_record():
    data = build_bsp()
    _closure, model = decoded(data)
    trailer = [row for row in model.typed_unidentified if row["field"] == "trailer"]
    assert len(trailer) == 1
    assert trailer[0]["byteLength"] == map_partition.TRAILER_BYTES
    assert trailer[0]["matchesEntitiesLumpOffset"] is True


# --------------------------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------------------------


def test_every_reference_the_unit_makes_has_exactly_one_dependency_row():
    _closure, model = decoded(build_bsp())
    keys = [(row["role"], row["asset"]) for row in model.dependencies]
    assert len(keys) == len(set(keys))
    roles = {row["role"] for row in model.dependencies}
    assert roles == {
        "material", "model", "texture", "surface-property",
        "map-entities", "map-lighting", "map-visibility",
    }


def test_a_patched_material_and_its_base_are_two_units_the_row_table_names_both():
    _closure, model = decoded(build_bsp())
    patched = next(row for row in model.textures if row["patched"])
    assert patched["asset"] == f"vtmb:material:maps/{MAP_NAME}/plaster/wall_1_2_3"
    assert patched["baseAsset"] == "vtmb:material:plaster/wall"
    assert patched["cubemapOrigin"] == [1, 2, 3]
    assets = {row["asset"] for row in model.dependencies}
    assert patched["asset"] in assets and patched["baseAsset"] in assets


def test_a_pakfile_member_routes_to_a_material_or_texture_unit_and_never_decodes_here():
    _closure, model = decoded(build_bsp())
    units = {entry["name"]: entry["unit"] for entry in model.pakfile["entries"]}
    assert units[f"materials/{PATCHED_NAME}.vmt"] == f"vtmb:material:maps/{MAP_NAME}/plaster/wall_1_2_3"
    assert units[f"materials/maps/{MAP_NAME}/c1_2_3.tth"] == f"vtmb:texture:maps/{MAP_NAME}/c1_2_3"
    assert model.pakfile["memberOrigin"]["kind"] == "bsp-pakfile"
    for entry in model.pakfile["entries"]:
        assert "bytesHex" not in entry and "rawData" not in entry


def test_a_static_prop_whose_model_the_install_lacks_keeps_a_missing_model_sentinel():
    data = build_bsp()
    _closure, model = decoded(data, models=(DETAIL_MODEL,))
    prop = model.static_props["props"][0]
    assert prop["asset"] == "vtmb:missing-model:props/box"
    assert prop["asset"] not in {row["asset"] for row in model.dependencies}
    reasons = [row for row in model.omitted_proven if row["role"] == "missing-model"]
    assert reasons and reasons[0]["asset"] == prop["asset"]


def test_a_detail_prop_whose_model_the_install_lacks_is_the_static_props_sentinel(tmp_path):
    data = build_bsp()
    _closure, model = decoded(data, models=(PROP_MODEL,))
    record = model.detail_props["records"][0]
    assert record["asset"] == "vtmb:missing-model:props/weed"
    assert record["asset"] not in {row["asset"] for row in model.dependencies}
    proven = [
        row for row in model.omitted_proven
        if row["role"] == "missing-model" and row["asset"] == record["asset"]
    ]
    assert len(proven) == 1
    index = make_index(data, models=(PROP_MODEL,))
    destination = exporter.export(index, MAP_NAME, tmp_path, read_bytes=reader(data))
    assert validation.validate(destination)["unresolved"] == 0


def test_a_sentinel_without_its_omitted_proven_row_is_refused(tmp_path):
    data = build_bsp()
    index = make_index(data, models=(PROP_MODEL,))
    destination = exporter.export(index, MAP_NAME, tmp_path, read_bytes=reader(data))
    document, binary = read_glb(destination)
    coverage = extension_of(document)["coverage"]
    coverage["omittedProven"] = [
        row for row in coverage["omittedProven"] if row.get("role") != "missing-model"
    ]
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary)


def test_the_physcollide_surfaceprop_produces_a_surface_property_dependency():
    _closure, model = decoded(build_bsp())
    rows = [row for row in model.dependencies if row["role"] == "surface-property"]
    assert [row["asset"] for row in rows] == ["vtmb:surface-property:concrete"]


# --------------------------------------------------------------------------------------------
# Non-canonical storage
# --------------------------------------------------------------------------------------------


def test_an_uninitialized_static_prop_lighting_origin_is_proven_rather_than_published():
    _closure, model = decoded(build_bsp(lighting_origin_nonfinite=True))
    prop = model.static_props["props"][0]
    assert prop["lightingOrigin"] is None
    assert len(bytes.fromhex(prop["lightingOriginRawHex"])) == 12
    rows = [
        row for row in model.omitted_proven
        if row["role"] == "static-prop-lighting-origin-uninitialized"
    ]
    assert rows and rows[0]["count"] == 1


def test_the_detail_prop_payloads_trailing_bytes_are_an_evidence_backed_omission():
    _closure, model = decoded(build_bsp(detail_trailing=True))
    rows = [row for row in model.omissions if row["role"] == "detail-prop-trailing-bytes"]
    assert rows and rows[0]["byteLength"] == 4
    assert rows[0]["bytesHex"] == "01020304"


def test_a_face_whose_winding_encloses_no_area_is_an_anomaly_and_keeps_its_triangles():
    data = build_bsp(zero_area_face=True)
    _closure, model = decoded(data)
    rows = [row for row in model.anomalies if row["role"] == "degenerate-face"]
    assert [row["reason"] for row in rows] == ["the winding encloses no area"]
    degenerate = model.faces[rows[0]["face"]]
    assert degenerate["indexCount"] == 6                    # a four-corner fan, stated anyway


def test_bytes_no_pakfile_record_addresses_are_named_rather_than_left_as_a_gap():
    _closure, zeroed = decoded(build_bsp(pakfile_gap=bytes(4)))
    padding = [
        row for row in zeroed.claims
        if row[3] == "pakfile.padding" and row[2] == "padding-zero"
    ]
    assert padding and padding[0][1] == 4

    _closure, filled = decoded(build_bsp(pakfile_gap=bytes((0xAB, 0xCD))))
    rows = [row for row in filled.omissions if row["role"] == "pakfile-container-fill"]
    assert rows and rows[0]["byteLength"] == 2 and rows[0]["bytesHex"] == "abcd"
    assert rows[0]["lump"] == 40


def test_a_pakfile_offset_that_leaves_the_lump_is_refused_as_a_pakfile_error():
    """A file-supplied offset is bounds-checked before it is read.

    `struct.error` is not a `ValueError`, so a dereference of an unchecked offset would leave
    this module past both `PakfileError` and the map seam's own error type.
    """

    blob = bytearray(_pakfile({"materials/one.vmt": b"data"}))
    eocd = blob.rfind(struct.pack("<I", pakfile.EOCD_SIGNATURE))
    central_offset = struct.unpack_from("<I", blob, eocd + 16)[0]
    struct.pack_into("<I", blob, central_offset + 42, len(blob) + 1000)
    with pytest.raises(pakfile.PakfileError, match="local header offset"):
        pakfile.parse(bytes(blob), 0, len(blob))


def test_a_truncated_end_of_central_directory_record_is_refused_as_a_pakfile_error():
    blob = bytes(4) + struct.pack("<I", pakfile.EOCD_SIGNATURE) + bytes(5)
    with pytest.raises(pakfile.PakfileError, match="end-of-central-directory"):
        pakfile.parse(blob, 0, len(blob))


def test_a_central_record_that_straddles_the_end_of_the_directory_is_refused():
    body = bytes(8)
    blob = body + struct.pack(
        "<IHHHHIIH", pakfile.EOCD_SIGNATURE, 0, 0, 1, 1, 0, len(body) + pakfile.EOCD_BYTES - 2, 0
    )
    with pytest.raises(pakfile.PakfileError, match="runs past the directory"):
        pakfile.parse(blob, 0, len(blob))


def test_a_displacement_becomes_its_own_mesh_and_the_face_states_no_triangles_of_its_own():
    data = build_bsp(displacement=True)
    _closure, model = decoded(data)
    document, _binary = exporter.build_document(model)
    row = extension_of(document)["displacements"][0]
    assert (row["vertexCount"], row["triangleCount"]) == (25, 32)
    assert row["face"] == 1
    mesh = document["meshes"][row["mesh"]]
    primitive = mesh["primitives"][0]
    assert document["accessors"][primitive["attributes"]["POSITION"]]["count"] == 25
    assert document["accessors"][primitive["attributes"]["_ALPHA"]]["count"] == 25
    assert document["accessors"][primitive["indices"]]["count"] == 96
    displaced = extension_of(document)["faces"][1]
    assert displaced["dispInfo"] == 0 and displaced["primitive"] is None
    assert [scene["nodes"] for scene in document["scenes"]][2] == [row["node"]]


def test_a_map_with_a_displacement_round_trips_through_validation(tmp_path):
    destination, _index = export_unit(tmp_path, build_bsp(displacement=True))
    summary = validation.validate(destination)
    assert summary["displacements"] == 1 and summary["byteCoveragePercent"] == 100.0


def test_a_dprp_payload_states_that_version_two_carries_no_sprite_dictionary():
    _closure, model = decoded(build_bsp())
    assert model.detail_props["sprites"] == []
    assert "no sprite dictionary" in model.detail_props["spritesOmitted"]["reason"]


def test_a_havok_mopp_solid_is_carried_verbatim_and_counted_as_typed_but_unidentified():
    payload = bytes(range(64))
    _closure, model = decoded(build_bsp(mopp=payload))
    solids = model.physics["models"][0]["solids"]
    assert [solid["kind"] for solid in solids] == ["ivps-compact-surface", "havok-mopp"]
    assert solids[1]["moppCode"]["byteLength"] == len(payload)
    carried = [row for row in model.typed_unidentified if row["field"].endswith(".moppCode")]
    assert len(carried) == 1
    assert model.binary[
        model.physics["moppCode"]["entries"][0]["firstByte"]:
    ][: len(payload)] != b""


def test_a_game_lump_version_the_seam_does_not_decode_is_refused():
    data = bytearray(build_bsp())
    offset, _length = struct.unpack_from("<ii", data, 8 + 35 * 16)
    struct.pack_into("<H", data, offset + 4 + 6, 5)          # sprp version 5
    index = make_index(bytes(data))
    closure = map_glb.load_source_closure(index, MAP_NAME, read_bytes=reader(bytes(data)))
    with pytest.raises(Exception) as error:
        map_glb.decode_map(closure, member_exists=lambda path: path.lower() in index)
    assert "sprp version 5" in str(error.value)


# --------------------------------------------------------------------------------------------
# The product
# --------------------------------------------------------------------------------------------


def test_the_unit_declares_its_extension_used_and_required_and_names_the_exporter():
    _closure, model = decoded(build_bsp())
    document, _binary = exporter.build_document(model)
    assert document["asset"]["generator"] == "Elysium Map GLB Exporter"
    assert map_glb.MAP_EXTENSION in document["extensionsUsed"]
    assert document["extensionsRequired"] == [map_glb.MAP_EXTENSION]


def test_a_cross_reference_extension_is_declared_only_where_an_object_binds_it(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    bound = {
        name
        for group in ("materials", "nodes", "meshes")
        for entry in document.get(group) or []
        for name in (entry.get("extensions") or {})
    }
    assert set(document["extensionsUsed"]) == bound | {map_glb.MAP_EXTENSION}
    for node in document["nodes"]:
        (node.get("extensions") or {}).pop("ELYSIUM_texture_reference", None)
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary)


def test_the_extension_root_opens_with_the_contracts_keys_in_order():
    _closure, model = decoded(build_bsp())
    document, _binary = exporter.build_document(model)
    root = extension_of(document)
    assert list(root)[:5] == [
        "schemaVersion", "identity", "sourceResolution", "dependencies", "coverage"
    ]


def test_the_unit_publishes_the_four_scenes_the_specification_names():
    _closure, model = decoded(build_bsp())
    document, _binary = exporter.build_document(model)
    assert [scene["name"] for scene in document["scenes"]] == [
        "world", "brushModels", "displacements", "placements"
    ]


def test_a_faces_triangles_are_stated_once_by_the_index_accessor_and_located_by_the_face_row():
    _closure, model = decoded(build_bsp())
    document, _binary = exporter.build_document(model)
    face = extension_of(document)["faces"][0]
    assert face["indexCount"] == (face["vertexCount"] - 2) * 3
    mesh = document["meshes"][document["nodes"][0]["mesh"]]
    accessor = document["accessors"][mesh["primitives"][face["primitive"]]["indices"]]
    assert face["firstIndex"] + face["indexCount"] <= accessor["count"]


def test_a_static_props_placement_lives_in_its_node_and_not_a_second_time_in_the_record():
    _closure, model = decoded(build_bsp())
    document, _binary = exporter.build_document(model)
    prop = extension_of(document)["staticProps"]["props"][0]
    assert "origin" not in prop and "angles" not in prop
    node = document["nodes"][prop["node"]]
    assert node["extensions"]["ELYSIUM_model_reference"]["asset"] == "vtmb:model:props/box"
    assert node["translation"] == pytest.approx([0.4064, 1.2192, -0.8128], rel=1e-6)
    assert math.isclose(sum(value * value for value in node["rotation"]), 1.0, rel_tol=1e-9)


def test_the_same_input_produces_the_same_bytes():
    data = build_bsp()
    first = encode_glb(*exporter.build_document(decoded(data)[1]))
    second = encode_glb(*exporter.build_document(decoded(data)[1]))
    assert first == second


# --------------------------------------------------------------------------------------------
# Validation
# --------------------------------------------------------------------------------------------


def test_a_published_unit_round_trips_through_standalone_validation(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    summary = validation.validate(destination)
    assert summary["asset"] == f"vtmb:map:{MAP_NAME}"
    assert summary["byteCoveragePercent"] == 100.0
    assert summary["sourceBytes"] == len(data)
    assert summary["unresolved"] == 0 and summary["unsupported"] == 0
    assert summary["subUnits"] == [
        f"vtmb:map-entities:{MAP_NAME}",
        f"vtmb:map-lighting:{MAP_NAME}",
        f"vtmb:map-visibility:{MAP_NAME}",
    ]
    assert any("typed but unidentified" in line for line in validation.warnings_for(summary))


def test_export_time_validation_weighs_the_ledger_against_the_selected_member(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    closure = map_glb.load_source_closure(make_index(data), MAP_NAME, read_bytes=reader(data))
    summary = validation.validate_document(
        document, binary, source_members=closure.members()
    )
    assert summary["accountedBytes"] == len(data)


def test_a_tampered_ledger_is_refused_on_read_back(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    ledger = extension_of(document)["coverage"]["byteLedger"][0]
    ledger["ranges"][0]["length"] += 1
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary)


def test_a_ledger_that_drops_a_range_is_refused(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    ledger = extension_of(document)["coverage"]["byteLedger"][0]
    del ledger["ranges"][1]
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary)


def test_a_material_without_a_dependency_row_is_refused(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    root = extension_of(document)
    root["dependencies"] = [
        row for row in root["dependencies"] if row["role"] != "material"
    ]
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary)


def test_a_face_whose_triangles_leave_its_primitive_is_refused(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    extension_of(document)["faces"][0]["indexCount"] += 3
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary)


def test_a_static_prop_node_that_disagrees_with_its_record_is_refused(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    prop = extension_of(document)["staticProps"]["props"][0]
    document["nodes"][prop["node"]]["translation"][0] += 1.0
    closure = map_glb.load_source_closure(make_index(data), MAP_NAME, read_bytes=reader(data))
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_pakfile_member_no_seam_claims_fails_the_map(tmp_path):
    data = build_bsp(pakfile_extra={"materials/maps/testmap/readme.txt": b"notes"})
    with pytest.raises(validation.MapGlbValidationError):
        export_unit(tmp_path, data)


def test_a_partition_that_does_not_cover_the_member_is_refused(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    extension_of(document)["partition"].pop()
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary)


def test_the_directory_row_keeps_the_offset_it_was_read_from(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, _binary = read_glb(destination)
    rows = extension_of(document)["header"]["lumps"]
    for index, row in enumerate(rows):
        assert row["sourceOffset"] == map_partition.LUMP_DIRECTORY_OFFSET + index * 16


def test_the_written_file_is_a_json_then_bin_container(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    raw = destination.read_bytes()
    magic, version, total = struct.unpack_from("<III", raw)
    assert (magic, version, total) == (0x46546C67, 2, len(raw))
    document, binary = read_glb(destination)
    declared = int(document["buffers"][0]["byteLength"])
    assert 0 <= len(binary) - declared < 4                   # the BIN chunk's own zero padding
    assert json.dumps(document)                              # the JSON chunk survives a round trip


def test_the_displacement_triangle_tag_lump_is_published_where_its_ledger_owner_says_it_is():
    data = build_bsp(displacement=True)
    _closure, model = decoded(data)
    document, _binary = exporter.build_document(model)
    root = extension_of(document)
    table = root["displacementTriangleTags"]
    assert table["count"] == 32 and len(table["values"]) == 32
    ranges = root["coverage"]["byteLedger"][0]["ranges"]
    owned = [row for row in ranges if row["owner"] == "displacementTriangleTags"]
    assert len(owned) == 1
    assert owned[0]["offset"] == table["sourceOffset"]
    assert owned[0]["length"] == table["count"] * table["stride"]
    assert owned[0]["state"] == "mapped"


def test_the_vertex_normal_index_lump_is_published_as_the_table_the_faces_cut():
    data = build_bsp()
    _closure, model = decoded(data)
    document, _binary = exporter.build_document(model)
    root = extension_of(document)
    table = root["normalIndices"]
    assert table["count"] == len(table["values"]) == 4 * len(root["faces"])
    face = root["faces"][0]
    assert face["normalIndexStart"] == 0 and face["normalIndexCount"] == face["numEdges"]
    owned = [
        row for row in root["coverage"]["byteLedger"][0]["ranges"]
        if row["owner"] == "normalIndices"
    ]
    assert len(owned) == 1 and owned[0]["length"] == table["count"] * table["stride"]


def test_a_ledger_owner_that_names_no_published_table_is_refused(tmp_path):
    from elysium_pipeline.formats.unit_contract import ranges_sha256

    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    root = extension_of(document)
    del root["normalIndices"]
    ledger = root["coverage"]["byteLedger"][0]
    ledger["rangesSha256"] = ranges_sha256(
        ledger["sourcePath"], ledger["byteLength"], ledger["ranges"]
    )
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary)


def test_a_vertex_no_edge_names_is_omitted_proven_rather_than_mapped():
    data = build_bsp(unreferenced_records=True)
    _closure, model = decoded(data)
    document, _binary = exporter.build_document(model)
    root = extension_of(document)
    ranges = root["coverage"]["byteLedger"][0]["ranges"]
    for owner, label in (
        ("meshes.position.unreferenced", "unreferenced-vertexes"),
        ("meshes.normal.unreferenced", "unreferenced-vertexNormals"),
    ):
        owned = [row for row in ranges if row["owner"] == owner]
        assert len(owned) == 1
        assert owned[0]["state"] == "omitted-proven" and owned[0]["length"] == 12
        row = next(
            entry for entry in root["coverage"]["omittedProven"] if entry["role"] == label
        )
        assert row["count"] == 1 and row["owner"] == owner
        assert row["ranges"] == [
            {
                "sourceOffset": owned[0]["offset"],
                "byteLength": 12,
                "firstRecord": row["of"] - 1,
                "recordCount": 1,
            }
        ]
    assert root["census"]["referencedVertexes"] == root["census"]["vertexes"] - 1
    assert root["census"]["referencedVertexNormals"] == root["census"]["vertexNormals"] - 1


def test_a_ledger_that_grades_an_unreferenced_vertex_run_mapped_is_refused(tmp_path):
    from elysium_pipeline.formats.unit_contract import ranges_sha256

    data = build_bsp(unreferenced_records=True)
    destination, index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    ledger = extension_of(document)["coverage"]["byteLedger"][0]
    for row in ledger["ranges"]:
        if row["owner"] == "meshes.position.unreferenced":
            row["owner"] = "meshes.position"
            row["state"] = "mapped"
            # Restate the totals and the digest too, so the check that fires is the seam's own
            # and not the shared contract's arithmetic.
            ledger["stateBytes"]["mapped"] += row["length"]
            ledger["stateBytes"]["omitted-proven"] -= row["length"]
    ledger["rangesSha256"] = ranges_sha256(
        ledger["sourcePath"], ledger["byteLength"], ledger["ranges"]
    )
    closure = map_glb.load_source_closure(index, MAP_NAME, read_bytes=reader(data))
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_texdata_row_names_its_string_and_never_restates_it():
    data = build_bsp()
    _closure, model = decoded(data)
    document, _binary = exporter.build_document(model)
    root = extension_of(document)
    texture = root["textures"][0]
    assert "name" not in texture
    assert root["textureStrings"]["names"][texture["nameStringTableID"]] == MATERIAL_NAME
    assert texture["asset"] == "vtmb:material:plaster/wall"


def test_a_texdata_row_restating_its_string_is_refused(tmp_path):
    data = build_bsp()
    destination, _index = export_unit(tmp_path, data)
    document, binary = read_glb(destination)
    root = extension_of(document)
    root["textures"][0]["name"] = MATERIAL_NAME
    closure = map_glb.load_source_closure(make_index(data), MAP_NAME, read_bytes=reader(data))
    with pytest.raises(validation.MapGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_texdata_whose_two_sizes_disagree_is_an_anomaly():
    _closure, model = decoded(build_bsp(texdata_view_size=(32, 16)))
    row = next(
        entry for entry in model.anomalies if entry["role"] == "texdata-view-size-mismatch"
    )
    assert row["size"] == [64, 64] and row["viewSize"] == [32, 16]
    assert not [
        entry for entry in decoded(build_bsp())[1].anomalies
        if entry["role"] == "texdata-view-size-mismatch"
    ]


def test_a_prop_whose_dictionary_index_is_out_of_range_keeps_the_row_shape_of_every_prop(
    tmp_path,
):
    data = build_bsp(prop_type=9)
    _closure, model = decoded(data)
    document, _binary = exporter.build_document(model)
    root = extension_of(document)
    prop = root["staticProps"]["props"][0]
    # The placement is real even though the dictionary index is not, so the record keeps a node
    # -- with no model reference on it -- and the same normalised fields as every other prop.
    assert prop["asset"] is None
    assert (document["nodes"][prop["node"]].get("extensions") or {}) == {}
    assert "origin" not in prop and "angles" not in prop
    assert prop["lightingOrigin"] == pytest.approx([0.0254, 0.0762, -0.0508], rel=1e-6)
    assert any(
        row["role"] == "static-prop-dictionary-range" for row in root["anomalies"]
    )
    destination, _index = export_unit(tmp_path, data)
    assert validation.validate(destination)["byteCoveragePercent"] == 100.0


def test_a_prop_placement_that_is_not_finite_is_carried_as_bytes_and_an_anomaly(tmp_path):
    data = build_bsp(prop_origin=(float("inf"), 32.0, 48.0))
    destination, _index = export_unit(tmp_path, data)
    document, _binary = read_glb(destination)
    root = extension_of(document)
    prop = root["staticProps"]["props"][0]
    assert prop["node"] is None
    assert len(prop["placementRawHex"]) == 48
    assert any(
        row["role"] == "static-prop-placement-nonfinite" for row in root["anomalies"]
    )
    assert validation.validate(destination)["byteCoveragePercent"] == 100.0
