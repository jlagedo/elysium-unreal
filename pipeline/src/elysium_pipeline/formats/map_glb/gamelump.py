"""The two game-lump payloads the root owns: `sprp` static props and `dprp` detail props.

The root owns lump 35's directory and the `sprp`/`dprp` payloads; the `dplt` payload is the
lighting unit's, so it is never read here. Both payloads are version-pinned by the census over
all 108 maps -- `sprp` is version 4 (`DStaticPropV4`, 56 bytes) and `dprp` is version 2 -- and a
payload that declares another version is refused rather than guessed at.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Any

#: `sprp` version 4: `DStaticPropV4`, 56 bytes, with no per-prop lightmap or DX level fields.
STATIC_PROP_VERSION = 4
STATIC_PROP_BYTES = 56

#: `dprp` version 2: a name dictionary, then the object records. The sprite dictionary that later
#: versions carry between the two does not exist here, which the census confirms on all 108 maps.
DETAIL_PROP_VERSION = 2
DETAIL_PROP_BYTES = 40

#: Both dictionaries store one fixed-width, NUL-padded model path per entry.
DICTIONARY_NAME_BYTES = 128


class GameLumpError(ValueError):
    """A game-lump payload is not the version or shape this seam decodes."""


@dataclass(frozen=True, slots=True)
class Decoded:
    """One payload's rows and the byte claims that pay for them, in file coordinates."""

    block: dict[str, Any]
    claims: tuple[tuple[int, int, str, str], ...]
    dictionary: tuple[str, ...]


def _name(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("latin-1")


def _dictionary(
    data: bytes, offset: int, end: int, owner: str
) -> tuple[list[dict[str, Any]], int, list[tuple[int, int, str, str]]]:
    if offset + 4 > end:
        raise GameLumpError(f"{owner} has no dictionary count")
    count = struct.unpack_from("<i", data, offset)[0]
    if count < 0 or offset + 4 + count * DICTIONARY_NAME_BYTES > end:
        raise GameLumpError(f"{owner} declares {count} dictionary entries")
    claims = [(offset, 4, "mapped", f"{owner}.dictionaryCount")]
    rows: list[dict[str, Any]] = []
    for index in range(count):
        row = offset + 4 + index * DICTIONARY_NAME_BYTES
        raw = data[row:row + DICTIONARY_NAME_BYTES]
        rows.append({"index": index, "name": _name(raw), "sourceOffset": row})
    if count:
        claims.append(
            (
                offset + 4,
                count * DICTIONARY_NAME_BYTES,
                "mapped-string",
                f"{owner}.dictionary",
            )
        )
    return rows, offset + 4 + count * DICTIONARY_NAME_BYTES, claims


def decode_static_props(data: bytes, offset: int, length: int, version: int) -> Decoded:
    """`sprp` v4: the model dictionary, the leaf table and one 56-byte record per prop."""

    if version != STATIC_PROP_VERSION:
        raise GameLumpError(f"sprp version {version}; this seam decodes v{STATIC_PROP_VERSION}")
    end = offset + length
    entries, cursor, claims = _dictionary(data, offset, end, "staticProps")
    if cursor + 4 > end:
        raise GameLumpError("sprp has no leaf count")
    leaf_count = struct.unpack_from("<i", data, cursor)[0]
    if leaf_count < 0 or cursor + 4 + leaf_count * 2 > end:
        raise GameLumpError(f"sprp declares {leaf_count} leaf entries")
    claims.append((cursor, 4, "mapped", "staticProps.leafCount"))
    leaf_offset = cursor + 4
    leaves = list(struct.unpack_from(f"<{leaf_count}H", data, leaf_offset)) if leaf_count else []
    if leaf_count:
        claims.append((leaf_offset, leaf_count * 2, "mapped", "staticProps.leaves"))
    cursor = leaf_offset + leaf_count * 2
    if cursor + 4 > end:
        raise GameLumpError("sprp has no prop count")
    prop_count = struct.unpack_from("<i", data, cursor)[0]
    claims.append((cursor, 4, "mapped", "staticProps.propCount"))
    cursor += 4
    if prop_count < 0 or cursor + prop_count * STATIC_PROP_BYTES > end:
        raise GameLumpError(f"sprp declares {prop_count} props")
    props: list[dict[str, Any]] = []
    for index in range(prop_count):
        row = cursor + index * STATIC_PROP_BYTES
        (
            origin_x,
            origin_y,
            origin_z,
            pitch,
            yaw,
            roll,
            prop_type,
            first_leaf,
            leaf_number,
            solid,
            flags,
            skin,
            fade_min,
            fade_max,
            lighting_x,
            lighting_y,
            lighting_z,
        ) = struct.unpack_from("<6f3H2Bi5f", data, row)
        props.append(
            {
                "index": index,
                "sourceOffset": row,
                "propType": int(prop_type),
                "firstLeaf": int(first_leaf),
                "leafCount": int(leaf_number),
                "solid": int(solid),
                "flags": int(flags),
                "skin": int(skin),
                "fadeMinDist": fade_min,
                "fadeMaxDist": fade_max,
                "origin": (origin_x, origin_y, origin_z),
                "angles": (pitch, yaw, roll),
                "lightingOrigin": (lighting_x, lighting_y, lighting_z),
            }
        )
    if prop_count:
        claims.append((cursor, prop_count * STATIC_PROP_BYTES, "mapped", "staticProps.props"))
    cursor += prop_count * STATIC_PROP_BYTES
    trailing = end - cursor
    block = {
        "version": version,
        "sourceOffset": offset,
        "byteLength": length,
        "recordBytes": STATIC_PROP_BYTES,
        "dictionary": entries,
        "leaves": {"sourceOffset": leaf_offset, "count": leaf_count, "values": leaves},
        "props": props,
        "trailingBytes": trailing,
    }
    return Decoded(block, tuple(claims), tuple(entry["name"] for entry in entries))


def decode_detail_props(data: bytes, offset: int, length: int, version: int) -> Decoded:
    """`dprp` v2: the model dictionary and one 40-byte object record per placement.

    Version 2 has no sprite dictionary; `sprites` is published empty with the reason, because an
    absent table and an empty one are different claims.
    """

    if version != DETAIL_PROP_VERSION:
        raise GameLumpError(f"dprp version {version}; this seam decodes v{DETAIL_PROP_VERSION}")
    end = offset + length
    entries, cursor, claims = _dictionary(data, offset, end, "detailProps")
    if cursor + 4 > end:
        raise GameLumpError("dprp has no object count")
    object_count = struct.unpack_from("<i", data, cursor)[0]
    claims.append((cursor, 4, "mapped", "detailProps.objectCount"))
    cursor += 4
    if object_count < 0 or cursor + object_count * DETAIL_PROP_BYTES > end:
        raise GameLumpError(f"dprp declares {object_count} objects")
    objects: list[dict[str, Any]] = []
    for index in range(object_count):
        row = cursor + index * DETAIL_PROP_BYTES
        (
            origin_x,
            origin_y,
            origin_z,
            pitch,
            yaw,
            roll,
            detail_model,
            leaf,
            light_r,
            light_g,
            light_b,
            light_a,
            light_styles,
            light_style_count,
            sway_amount,
            shape_angle,
            shape_size,
        ) = struct.unpack_from("<6f2H4BI4B", data, row)
        objects.append(
            {
                "index": index,
                "sourceOffset": row,
                "detailModel": int(detail_model),
                "leaf": int(leaf),
                "lighting": [int(light_r), int(light_g), int(light_b), int(light_a)],
                "lightStyles": int(light_styles),
                "lightStyleCount": int(light_style_count),
                "swayAmount": int(sway_amount),
                "shapeAngle": int(shape_angle),
                "shapeSize": int(shape_size),
                "origin": (origin_x, origin_y, origin_z),
                "angles": (pitch, yaw, roll),
            }
        )
    if object_count:
        claims.append(
            (cursor, object_count * DETAIL_PROP_BYTES, "mapped", "detailProps.records")
        )
    cursor += object_count * DETAIL_PROP_BYTES
    trailing = end - cursor
    block = {
        "version": version,
        "sourceOffset": offset,
        "byteLength": length,
        "recordBytes": DETAIL_PROP_BYTES,
        "dictionary": entries,
        "sprites": [],
        "records": objects,
        "trailingBytes": trailing,
        "trailingSourceOffset": cursor if trailing else None,
    }
    return Decoded(block, tuple(claims), tuple(entry["name"] for entry in entries))
