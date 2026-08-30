"""One map's lighting spans, decoded into the records the unit publishes.

The bake itself is not decoded: lump 8 is `ColorRGBExp32` and reaches the product as one
`UNSIGNED_BYTE` accessor over the whole lump, byte for byte. What this module decodes is
everything that *locates* those bytes -- the face rows the root's lumps 7 and 6 imply, the world
lights, the displacement runs and the detail-prop lighting table -- and the byte claims that pay
for every owned span.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import replace
import hashlib
import math
import struct
from typing import Any

from elysium_pipeline.formats.map_glb import partition as map_partition
from elysium_pipeline.formats.map_lighting_glb import model as lighting_model
from elysium_pipeline.formats.map_lighting_glb.model import (
    DetailPropLight,
    DisplacementLighting,
    FaceLighting,
    MapLightingModel,
    SampleSpan,
    WorldLight,
)
from elysium_pipeline.formats.unit_contract import dependency

#: `(offset, length, state, owner)`, in the owning member's own coordinates.
Claim = tuple[int, int, str, str]

_AVG_LIGHT = struct.Struct("<3Bb")
_FACE_TEXINFO = struct.Struct("<h")
_FACE_STYLES = struct.Struct("<8B")
_FACE_LIGHTOFS = struct.Struct("<i")
_FACE_LIGHTMAP = struct.Struct("<4i")
_TEXINFO_FLAGS = struct.Struct("<i")
_WORLD_LIGHT = struct.Struct("<9f3i7f3i")
_DISPINFO_POWER = struct.Struct("<i")
_DISPINFO_MAPFACE = struct.Struct("<H")
_DISPINFO_LIGHTING = struct.Struct("<2i")
_INT = struct.Struct("<i")
_DETAIL_PROP_LIGHT = struct.Struct("<3BbB")

#: What a world light's 88 bytes hold, in the order `_WORLD_LIGHT` unpacks them.
_WORLD_LIGHT_FIELDS = (
    "origin.x", "origin.y", "origin.z",
    "intensity.r", "intensity.g", "intensity.b",
    "normal.x", "normal.y", "normal.z",
    "cluster", "type", "style",
    "stopdot", "stopdot2", "exponent", "radius",
    "constantAttn", "linearAttn", "quadraticAttn",
    "flags", "texinfo", "owner",
)

#: How a lump 8 range that a face paid for opens; the digits after it are the face's index.
_FACE_OWNER_PREFIX = "lighting.faces["

#: What an orphan run says about itself when nothing further can be measured about it.
_ORPHAN_REASON = (
    "no face span of lump 7 addresses these bytes; what the format stores here is not established"
)


def _orphan_reason(precedes: int | None, blocks: int) -> str:
    """The evidence one orphan run carries, measured rather than assumed.

    A run that is a whole number of `ColorRGBExp32` blocks and ends exactly at a lit face's
    `lightofs` states that much: it is the only thing about the run this seam can prove. What the
    format calls the field is still not established, so the run stays an omission and the map
    stays `typedUnidentified`.
    """

    if precedes is None:
        return _ORPHAN_REASON
    return (
        f"no face span of lump 7 addresses these bytes; the run is {blocks} whole "
        f"{lighting_model.LUXEL_BYTES}-byte ColorRGBExp32 block(s) ending exactly at the "
        f"lightofs of lit face {precedes}; what the format stores there is not established"
    )


class MapLightingDecodeError(ValueError):
    """A lighting span does not hold the records this seam decodes it as."""


def _anomaly(role: str, **fields: Any) -> dict[str, Any]:
    row: dict[str, Any] = {"role": role}
    row.update(fields)
    return row


def _decode_faces(
    closure, anomalies: list[dict[str, Any]]
) -> tuple[list[FaceLighting], list[Claim]]:
    """One row per root face, and the lump 8 claims the lit ones pay for.

    The row is `derived`: every field it restates is a byte of the root's lump 7 or lump 6, and
    the root's ledger is where those bytes are accounted. What is decided here is the extent each
    face's samples occupy, because that is what makes lump 8 inspectable.
    """

    data = closure.data
    faces_lump = closure.lump(lighting_model.FACE_LUMP)
    texinfo_lump = closure.lump(lighting_model.TEXINFO_LUMP)
    lighting_bytes = closure.lighting.byte_length
    texinfo_count = texinfo_lump.length // lighting_model.TEXINFO_BYTES
    flags = [
        _TEXINFO_FLAGS.unpack_from(
            data, texinfo_lump.offset + index * lighting_model.TEXINFO_BYTES + 64
        )[0]
        for index in range(texinfo_count)
    ]
    rows: list[FaceLighting] = []
    claims: list[Claim] = []
    unlit: list[int] = []
    count = faces_lump.length // lighting_model.FACE_BYTES
    for index in range(count):
        offset = faces_lump.offset + index * lighting_model.FACE_BYTES
        record = data[offset:offset + lighting_model.FACE_BYTES]
        average = tuple(
            _AVG_LIGHT.unpack_from(record, sample * 4) for sample in range(8)
        )
        tex_info = _FACE_TEXINFO.unpack_from(record, 42)[0]
        styles = _FACE_STYLES.unpack_from(record, 48)
        light_offset = _FACE_LIGHTOFS.unpack_from(record, 72)[0]
        mins_x, mins_y, size_x, size_y = _FACE_LIGHTMAP.unpack_from(record, 80)
        if 0 <= tex_info < texinfo_count:
            face_flags = flags[tex_info]
        else:
            face_flags = 0
            anomalies.append(
                _anomaly(
                    "texinfo-out-of-range",
                    face=index,
                    sourceOffset=offset,
                    texInfo=tex_info,
                    texinfoCount=texinfo_count,
                    evidence="the face names no texinfo row, so SURF_BUMPLIGHT is read as unset",
                )
            )
        bumped = bool(face_flags & lighting_model.SURF_BUMPLIGHT)
        sets = lighting_model.BUMP_LIGHTMAP_SETS if bumped else 1
        live = [
            (slot, style) for slot, style in enumerate(styles)
            if style != lighting_model.UNUSED_STYLE
        ]
        luxel_width = size_x + 1
        luxel_height = size_y + 1
        block = luxel_width * luxel_height * lighting_model.LUXEL_BYTES
        byte_length = block * len(live) * sets
        spans: tuple[SampleSpan, ...] = ()
        if light_offset == -1:
            unlit.append(index)
            byte_length = 0
        elif (
            light_offset < 0
            or luxel_width <= 0
            or luxel_height <= 0
            or light_offset + byte_length > lighting_bytes
        ):
            anomalies.append(
                _anomaly(
                    "lightofs-out-of-range",
                    face=index,
                    sourceOffset=offset,
                    lightOffset=light_offset,
                    computedByteLength=byte_length,
                    lumpByteLength=lighting_bytes,
                    evidence="the computed sample extent leaves lump 8, so the face claims none "
                             "of its bytes and the range stays with the orphan runs",
                )
            )
            byte_length = 0
        else:
            built: list[SampleSpan] = []
            for set_index in range(sets):
                for style_index, (_slot, style) in enumerate(live):
                    start = light_offset + (set_index * len(live) + style_index) * block
                    built.append(
                        SampleSpan(
                            set_index=set_index,
                            style_index=style_index,
                            style=style,
                            offset=start,
                            length=block,
                        )
                    )
            spans = tuple(built)
            claims.extend(
                (span.offset, span.length, "mapped", span.owner(index)) for span in spans
            )
        rows.append(
            FaceLighting(
                index=index,
                source_offset=offset,
                tex_info=tex_info,
                flags=face_flags,
                bumped=bumped,
                light_offset=light_offset,
                lightmap_mins=(mins_x, mins_y),
                lightmap_size=(size_x, size_y),
                luxel_width=luxel_width,
                luxel_height=luxel_height,
                styles=tuple(styles),
                style_count=len(live),
                lightmap_sets=sets,
                avg_light_color=average,
                byte_length=byte_length,
                spans=spans,
            )
        )
    if unlit:
        anomalies.append(
            _anomaly(
                "face-without-lightmap",
                count=len(unlit),
                faces=unlit,
                evidence="a lightofs of -1 is authored and common; the face holds no samples",
            )
        )
    return rows, claims


def _owning_face(owner: str) -> int | None:
    """The face index a `lighting.faces[i].set[k].style[s]` owner names."""

    if not owner.startswith(_FACE_OWNER_PREFIX):
        return None
    tail = owner[len(_FACE_OWNER_PREFIX):]
    digits = tail.partition("]")[0]
    return int(digits) if digits.isdigit() else None


def _drop_overlapping_faces(
    closure, faces: list[FaceLighting], claims: list[Claim], anomalies: list[dict[str, Any]]
) -> list[Claim]:
    """A face whose samples collide with an earlier face's keeps its row and owns no bytes.

    Two faces cannot both be paid for one byte, and a face row that still published `spans[]`
    while the orphan runs said nothing addressed those bytes would contradict itself. So the
    later face of a collision loses all of its claims at once -- not just the colliding one --
    and its row is rewritten to the shape a face with no samples already has.
    """

    ordered = sorted(claims, key=lambda claim: claim[0])
    conflicted: dict[int, tuple[Claim, str | None]] = {}
    previous: Claim | None = None
    cursor = 0
    for claim in ordered:
        offset, length, _state, owner = claim
        index = _owning_face(owner)
        if offset < cursor and index is not None and index not in conflicted:
            conflicted[index] = (claim, previous[3] if previous else None)
        if offset + length > cursor:
            cursor = offset + length
            previous = claim
    if not conflicted:
        return ordered
    for index in sorted(conflicted):
        (offset, length, _state, owner), met = conflicted[index]
        anomalies.append(
            _anomaly(
                "span-overlap",
                face=index,
                owner=owner,
                sourceOffset=closure.lighting.span_offset + offset,
                offset=offset,
                byteLength=length,
                overlaps=met,
                evidence="two face spans claim one byte; the later face keeps its row, "
                         "publishes no span and claims none of the lump",
            )
        )
        faces[index] = replace(faces[index], byte_length=0, spans=())
    return [claim for claim in ordered if _owning_face(claim[3]) not in conflicted]


def _orphan_runs(
    closure, faces: list[FaceLighting], claims: list[Claim],
    anomalies: list[dict[str, Any]], omissions: list[dict[str, Any]],
) -> tuple[list[Claim], dict[str, Any] | None]:
    """The lump 8 bytes no face span claims, one `omitted-proven` run each.

    A run is contiguous and evidence-backed by its own digest, and by whatever else about it can
    be measured: a run that is a whole number of luxels and ends at a lit face's `lightofs` says
    so on its own row. What the format stores there is still not established, so the unit carries
    the run -- an omission row per run, one `typedUnidentified` row for the map -- and never
    drops it.
    """

    data = closure.lighting.data
    accepted = _drop_overlapping_faces(closure, faces, claims, anomalies)
    lit_at = {
        face.light_offset: face.index for face in reversed(faces) if face.spans
    }
    runs: list[tuple[int, int]] = []
    cursor = 0
    for offset, length, _state, _owner in accepted:
        if offset > cursor:
            runs.append((cursor, offset - cursor))
        cursor = offset + length
    if cursor < len(data):
        runs.append((cursor, len(data) - cursor))
    accepted = list(accepted)
    preceding = 0
    for index, (offset, length) in enumerate(runs):
        chunk = data[offset:offset + length]
        precedes = lit_at.get(offset + length)
        if precedes is None or length % lighting_model.LUXEL_BYTES:
            precedes = None
        else:
            preceding += 1
        row: dict[str, Any] = {
            "role": "orphan-lighting-bytes",
            "index": index,
            "sourceOffset": closure.lighting.span_offset + offset,
            "offset": offset,
            "byteLength": length,
            "sha256": hashlib.sha256(chunk).hexdigest(),
            "reason": _orphan_reason(precedes, length // lighting_model.LUXEL_BYTES),
        }
        if precedes is not None:
            row["precedesLitFace"] = precedes
            row["luxelBlocks"] = length // lighting_model.LUXEL_BYTES
        omissions.append(row)
        accepted.append((offset, length, "omitted-proven", f"lighting.orphan[{index}]"))
    if not runs:
        return accepted, None
    total = sum(length for _offset, length in runs)
    return accepted, {
        "role": "orphan-lighting-bytes",
        "sourcePath": closure.lighting.path,
        "runs": len(runs),
        "byteLength": total,
        "runsPrecedingALitFace": preceding,
        "reason": (
            f"{preceding} of {len(runs)} run(s) are whole ColorRGBExp32 blocks ending exactly at "
            "a lit face's lightofs; what the format stores there is not established, so every "
            "run is carried byte for byte rather than resolved"
        ),
    }


def _decode_world_lights(
    closure, anomalies: list[dict[str, Any]], omissions: list[dict[str, Any]]
) -> tuple[list[WorldLight], list[Claim]]:
    """Lump 15 as the 88-byte `dworldlight_t` array the format states it is."""

    member = closure.worldlights
    data = member.data
    count = len(data) // lighting_model.WORLD_LIGHT_BYTES
    rows: list[WorldLight] = []
    claims: list[Claim] = []
    for index in range(count):
        offset = index * lighting_model.WORLD_LIGHT_BYTES
        values = _WORLD_LIGHT.unpack_from(data, offset)
        floats = [value for value in values if isinstance(value, float)]
        if not all(math.isfinite(value) for value in floats):
            anomalies.append(
                _anomaly(
                    "non-finite-world-light-field",
                    index=index,
                    sourceOffset=member.span_offset + offset,
                    fields=[
                        name
                        for name, value in zip(_WORLD_LIGHT_FIELDS, values)
                        if isinstance(value, float) and not math.isfinite(value)
                    ],
                    finiteFields={
                        name: value
                        for name, value in zip(_WORLD_LIGHT_FIELDS, values)
                        if not isinstance(value, float) or math.isfinite(value)
                    },
                    sha256=hashlib.sha256(
                        data[offset:offset + lighting_model.WORLD_LIGHT_BYTES]
                    ).hexdigest(),
                    evidence="the compiler wrote a float the JSON chunk cannot state; the "
                             "record keeps its 88 mapped bytes, and this row names the fields "
                             "that could not be stated, digests the record and states the rest",
                )
            )
            claims.append(
                (offset, lighting_model.WORLD_LIGHT_BYTES, "mapped", f"worldLights[{index}]")
            )
            continue
        rows.append(
            WorldLight(
                index=index,
                source_offset=member.span_offset + offset,
                origin=values[0:3],
                intensity=values[3:6],
                normal=values[6:9],
                cluster=values[9],
                type=values[10],
                style=values[11],
                stopdot=values[12],
                stopdot2=values[13],
                exponent=values[14],
                radius=values[15],
                constant_attn=values[16],
                linear_attn=values[17],
                quadratic_attn=values[18],
                flags=values[19],
                texinfo=values[20],
                owner=values[21],
            )
        )
        claims.append(
            (offset, lighting_model.WORLD_LIGHT_BYTES, "mapped", f"worldLights[{index}]")
        )
    tail = len(data) - count * lighting_model.WORLD_LIGHT_BYTES
    if tail:
        offset = count * lighting_model.WORLD_LIGHT_BYTES
        chunk = data[offset:]
        anomalies.append(
            _anomaly(
                "worldlight-length-not-multiple",
                sourcePath=member.path,
                byteLength=len(data),
                recordBytes=lighting_model.WORLD_LIGHT_BYTES,
                remainder=tail,
                evidence="lump 15 is longer than the whole records it holds",
            )
        )
        if any(chunk):
            omissions.append(
                {
                    "role": "unused-worldlight-bytes",
                    "sourcePath": member.path,
                    "sourceOffset": member.span_offset + offset,
                    "offset": offset,
                    "byteLength": tail,
                    "sha256": hashlib.sha256(chunk).hexdigest(),
                    "reason": "no whole dworldlight_t addresses these bytes",
                }
            )
            claims.append((offset, tail, "omitted-proven", "worldLights.unused"))
        else:
            claims.append((offset, tail, "padding-zero", "worldLights.padding"))
    return rows, claims


def _decode_displacements(closure, anomalies: list[dict[str, Any]]) -> list[DisplacementLighting]:
    """One row per dispinfo, locating its run in lumps 32 and 34.

    The runs are `derived` from the root's `lightmapAlphaStart` and
    `lightmapSamplePositionStart`: a run ends where the next displacement's starts, and the last
    ends at the lump. The two lumps are claimed whole, so a row is a reader's index, not a claim.
    """

    data = closure.data
    dispinfo = closure.lump(lighting_model.DISPINFO_LUMP)
    count = dispinfo.length // lighting_model.DISPINFO_BYTES
    if not count:
        return []
    alpha_bytes = closure.disp_alphas.byte_length if closure.disp_alphas else 0
    sample_bytes = (
        closure.disp_sample_positions.byte_length if closure.disp_sample_positions else 0
    )
    starts: list[tuple[int, int, int, int, int]] = []
    for index in range(count):
        offset = dispinfo.offset + index * lighting_model.DISPINFO_BYTES
        power = _DISPINFO_POWER.unpack_from(data, offset + 20)[0]
        map_face = _DISPINFO_MAPFACE.unpack_from(data, offset + 36)[0]
        alpha_start, sample_start = _DISPINFO_LIGHTING.unpack_from(data, offset + 40)
        starts.append((offset, power, map_face, alpha_start, sample_start))

    def _run(start: int, following: int | None, total: int, field: str, index: int) -> int:
        end = total if following is None else following
        if start < 0 or start > total or end < start or end > total:
            anomalies.append(
                _anomaly(
                    "displacement-run-out-of-range",
                    displacement=index,
                    field=field,
                    start=start,
                    end=end,
                    lumpByteLength=total,
                    evidence="the root's start offset does not name a run inside the lump",
                )
            )
            return 0
        return end - start

    rows: list[DisplacementLighting] = []
    for index, (offset, power, map_face, alpha_start, sample_start) in enumerate(starts):
        following = starts[index + 1] if index + 1 < count else None
        rows.append(
            DisplacementLighting(
                index=index,
                source_offset=offset,
                power=power,
                map_face=map_face,
                alpha_start=alpha_start,
                alpha_length=_run(
                    alpha_start, following[3] if following else None, alpha_bytes,
                    "lightmapAlphaStart", index,
                ),
                sample_position_start=sample_start,
                sample_position_length=_run(
                    sample_start, following[4] if following else None, sample_bytes,
                    "lightmapSamplePositionStart", index,
                ),
            )
        )
    return rows


def _decode_detail_prop_lighting(
    closure, anomalies: list[dict[str, Any]], omissions: list[dict[str, Any]]
) -> tuple[list[DetailPropLight], list[Claim]]:
    """The `dplt` payload: an `int` count, then that many five-byte records."""

    member = closure.detail_prop_lighting
    if member is None:
        return [], []
    data = member.data
    if len(data) < 4:
        anomalies.append(
            _anomaly(
                "dplt-count-mismatch",
                sourcePath=member.path,
                byteLength=len(data),
                evidence="the payload is shorter than the count it opens with",
            )
        )
        if len(data):
            omissions.append(
                {
                    "role": "short-dplt-payload",
                    "sourcePath": member.path,
                    "sourceOffset": member.span_offset,
                    "offset": 0,
                    "byteLength": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "reason": "no whole dplt header addresses these bytes",
                }
            )
            return [], [(0, len(data), "omitted-proven", "dplt.unused")]
        return [], []
    declared = _INT.unpack_from(data, 0)[0]
    claims: list[Claim] = [(0, 4, "mapped", "dplt.count")]
    available = (len(data) - 4) // lighting_model.DETAIL_PROP_LIGHT_BYTES
    count = declared if 0 <= declared <= available else available
    if count != declared:
        anomalies.append(
            _anomaly(
                "dplt-count-mismatch",
                sourcePath=member.path,
                declared=declared,
                decoded=count,
                byteLength=len(data),
                evidence="the payload does not hold the number of records its header declares",
            )
        )
    rows: list[DetailPropLight] = []
    for index in range(count):
        offset = 4 + index * lighting_model.DETAIL_PROP_LIGHT_BYTES
        red, green, blue, exponent, style = _DETAIL_PROP_LIGHT.unpack_from(data, offset)
        rows.append(
            DetailPropLight(
                index=index,
                source_offset=member.span_offset + offset,
                r=red,
                g=green,
                b=blue,
                exponent=exponent,
                style=style,
            )
        )
        claims.append(
            (offset, lighting_model.DETAIL_PROP_LIGHT_BYTES, "mapped", f"dplt.records[{index}]")
        )
    used = 4 + count * lighting_model.DETAIL_PROP_LIGHT_BYTES
    tail = len(data) - used
    if tail:
        chunk = data[used:]
        if any(chunk):
            omissions.append(
                {
                    "role": "unused-dplt-bytes",
                    "sourcePath": member.path,
                    "sourceOffset": member.span_offset + used,
                    "offset": used,
                    "byteLength": tail,
                    "sha256": hashlib.sha256(chunk).hexdigest(),
                    "reason": "no whole dplt record addresses these bytes",
                }
            )
            claims.append((used, tail, "omitted-proven", "dplt.unused"))
        else:
            claims.append((used, tail, "padding-zero", "dplt.padding"))
    return rows, claims


def _build_binary(members: list[tuple[str, Any]]):
    """Lay the verbatim lumps into the BIN chunk, each view four-byte aligned.

    Every one of these lumps is `ColorRGBExp32` or a per-luxel byte table: the accessor is the
    lump byte for byte, so the ledger range and the accessor are the same span from two sides.
    """

    payload = bytearray()
    views: list[dict[str, Any]] = []
    accessors: list[dict[str, Any]] = []
    indices: dict[str, int] = {}
    for name, member in members:
        if member is None or not member.byte_length:
            continue
        payload.extend(b"\0" * (-len(payload) % 4))
        views.append(
            {"buffer": 0, "byteOffset": len(payload), "byteLength": member.byte_length,
             "name": name}
        )
        accessors.append(
            {
                "bufferView": len(views) - 1,
                "componentType": 5121,
                "count": member.byte_length,
                "type": "SCALAR",
                "name": name,
            }
        )
        indices[name] = len(accessors) - 1
        payload.extend(member.data)
    return bytes(payload), views, accessors, indices


def _decode(closure) -> MapLightingModel:

    anomalies: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    claims: dict[str, list[Claim]] = {}

    faces, face_claims = _decode_faces(closure, anomalies)
    lighting_claims, orphan_summary = _orphan_runs(
        closure, faces, face_claims, anomalies, omissions
    )
    claims[closure.lighting.path] = lighting_claims
    world_lights, world_claims = _decode_world_lights(closure, anomalies, omissions)
    claims[closure.worldlights.path] = world_claims
    if closure.disp_alphas is not None:
        claims[closure.disp_alphas.path] = [
            (0, closure.disp_alphas.byte_length, "mapped", "dispAlphas")
        ]
    if closure.disp_sample_positions is not None:
        claims[closure.disp_sample_positions.path] = [
            (0, closure.disp_sample_positions.byte_length, "mapped", "dispSamplePositions")
        ]
    displacements = _decode_displacements(closure, anomalies)
    detail_prop_lighting, detail_claims = _decode_detail_prop_lighting(
        closure, anomalies, omissions
    )
    if closure.detail_prop_lighting is not None:
        claims[closure.detail_prop_lighting.path] = detail_claims

    for member in closure.members():
        if not member.byte_length:
            omissions.append(
                {
                    "role": "empty-member",
                    "sourcePath": member.path,
                    "sourceOffset": member.span_offset,
                    "byteLength": 0,
                    "reason": f"the map's {member.role} span is empty",
                }
            )

    binary, views, accessors, indices = _build_binary(
        [
            ("samples", closure.lighting),
            ("dispAlphas", closure.disp_alphas),
            ("dispSamplePositions", closure.disp_sample_positions),
        ],
    )

    def _block(name: str, member, lump: int, extra=None) -> dict[str, Any] | None:
        if member is None:
            return None
        block: dict[str, Any] = {
            # The buffer view is `accessors[accessor].bufferView` and is not restated here: one
            # datum is stated once, in core glTF.
            "accessor": indices.get(name),
            "lump": lump,
            "byteLength": member.byte_length,
            "sha256": member.sha256,
        }
        if extra:
            block.update(extra)
        return block

    samples = _block(
        "samples", closure.lighting, lighting_model.LIGHTING_LUMP,
        {"format": "ColorRGBExp32", "luxelBytes": lighting_model.LUXEL_BYTES},
    )
    disp_alphas = _block("dispAlphas", closure.disp_alphas, lighting_model.DISP_ALPHA_LUMP)
    disp_samples = _block(
        "dispSamplePositions", closure.disp_sample_positions, lighting_model.DISP_SAMPLE_LUMP
    )

    style_census: Counter[int] = Counter()
    for face in faces:
        for style in {
            value for value in face.styles if value != lighting_model.UNUSED_STYLE
        }:
            style_census[style] += 1
    type_census: Counter[int] = Counter(light.type for light in world_lights)

    lumps = []
    for index in lighting_model.OWNED_LUMPS:
        span = closure.lump(index)
        member_path = lighting_model.member_path(closure.key, index)
        published = any(member.path == member_path for member in closure.members())
        lumps.append(
            {
                "lump": index,
                "name": map_partition.LUMP_NAMES[index],
                "offset": span.offset,
                "length": span.length,
                "sourceOffset": span.row_offset,
                "member": member_path if published else None,
            }
        )
    game_lump = None
    if closure.game_lump is not None:
        game_lump = dict(closure.game_lump.to_json())
        game_lump["member"] = (
            closure.detail_prop_lighting.path if closure.detail_prop_lighting else None
        )

    dependencies = [
        dependency(
            "map",
            lighting_model.map_asset_id(closure.key),
            closure.source_path,
            True,
            byteLength=len(closure.data),
            sha256=closure.source_sha256,
        ),
        dependency(
            "map-entities",
            lighting_model.map_entities_asset_id(closure.key),
            closure.source_path,
            True,
        ),
    ]

    typed_unidentified: list[dict[str, Any]] = []
    omitted_proven: list[dict[str, Any]] = []
    if orphan_summary is not None:
        typed_unidentified.append(orphan_summary)
        omitted_proven.append(
            {
                "role": "orphan-lighting-bytes",
                "sourcePath": closure.lighting.path,
                "runs": orphan_summary["runs"],
                "byteLength": orphan_summary["byteLength"],
                "runsPrecedingALitFace": orphan_summary["runsPrecedingALitFace"],
                "evidence": "each run is carried in omissions[] with its offset, length, SHA-256 "
                            "and, where the run is a whole number of luxels ending at a lit "
                            "face's lightofs, the face it precedes",
            }
        )

    return MapLightingModel(
        key=closure.key,
        asset_id=closure.asset_id,
        source_path=closure.source_path,
        source=closure,
        map={
            "stem": closure.key,
            "mapRevision": closure.partition.map_revision,
            "lumps": lumps,
            "gameLump": game_lump,
        },
        samples=samples,
        faces=faces,
        style_census=[
            {"style": style, "faces": style_census[style]} for style in sorted(style_census)
        ],
        world_lights=world_lights,
        light_type_census=[
            {
                "type": value,
                "name": lighting_model.light_type_name(value),
                "count": type_census[value],
            }
            for value in sorted(type_census)
        ],
        disp_alphas=disp_alphas,
        disp_sample_positions=disp_samples,
        displacements=displacements,
        detail_prop_lighting=detail_prop_lighting,
        binary=binary,
        buffer_views=views,
        accessors=accessors,
        dependencies=dependencies,
        anomalies=anomalies,
        omissions=omissions,
        claims=claims,
        mapped=[dict(row) for row in lighting_model.MAPPED_FIELDS],
        typed_unidentified=typed_unidentified,
        omitted_proven=omitted_proven,
        unresolved=[],
        unsupported=[],
    )


def decode_map_lighting(closure) -> MapLightingModel:
    """Decode one map's lighting closure into the model the exporter writes.

    A read that runs off the end of a span is the one failure this seam cannot carry: every
    other departure from the format is a named anomaly or omission, but a record that is not
    there has no offset to publish and no bytes to claim.
    """

    try:
        return _decode(closure)
    except struct.error as error:
        raise MapLightingDecodeError(f"{closure.source_path}: {error}") from error
