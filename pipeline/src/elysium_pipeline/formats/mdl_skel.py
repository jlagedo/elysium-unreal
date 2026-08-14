"""VtMB `.mdl` v2531 skeletal decode — bones, attachments, skin, animation tracks, and the face.

Companion to `mdl.py` (static geometry). Decodes the animated half documented in
`docs/vtmb/animation_and_movers.md` Part A: the `StudioBone` skeleton, per-vertex skin
weights, and the `{valid,total}` RLE animation tracks (7 channels/bone: posXYZ +
quat XYZW). Positions/rotations stay in **Source** coordinates (inches, Z-up); the
glTF writer applies the Source->glTF basis change.

Include-model banks (the shared animation libraries an NPC idles/moves/fights from) are
resolved here: `resolve_tree` walks the studiohdr include tree transitively and
`local_sequences` reads each model's own clips, so the caller can bake an NPC's own clips
and each shared bank's clips into separate glTF assets keyed by bone name (A.7).

The **facial** half (`docs/vtmb/facial_animation.md`) decodes here too: the studiohdr flex
block (flex descs, the 44 flex controllers, the 60-rule RPN, `mstudiomouth_t`), the
per-mesh `StudioFlex` array, and both `StudioVertAnim` encodings. The compressed record
stores *directions*, not deltas — `read_anorms` pulls the unit-vector table it indexes out
of the user's own `StudioRender.dll`. `probe_facial.py` is the survey over the same
decoders; `mdl_gltf.py` bakes their output into glTF morph targets (roadmap PL10).

Blend spaces are decoded but not evaluated: `read_grid` carries a sequence's extents,
pose-parameter binding and every cell out of the descriptor, and `pose_parameters` reads the
axes those cells are driven by. Each cell stays its own clip — the mix belongs to the host,
because evaluate-then-blend and blend-then-evaluate are not the same pose.

`StudioEyeball` decodes here too: the count/index pair sits at `StudioModel`+192/+196 (not
Source's slot), and 301 models carry two records each — the whole character cast, players
included. The record names the eye's bone, its resting basis, the iris scale, and the eyelid
flexdescs the renderer's eye pass writes back into the flex weights; `StudioMesh.materialtype`
flags which meshes it applies to.

Animation events are carried beside each sequence as the old 76-byte VtMB record: cycle,
numeric event id, type, and a fixed 64-byte options string.  The later Source record's event-name
index is absent.  The records remain metadata; dispatch belongs to the host animation runtime.

Not decoded here: procedural bones (`ProcType`!=0) and IK. Authored movement is decoded as
metadata beside the in-place bone tracks; the glTF clip deliberately stays in place so a host
motor can consume the same movement without translating the skeleton a second time.
"""
import math
import os
import struct

import numpy as np
from collections import namedtuple

_i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]
_u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
_h16 = lambda b, o: struct.unpack_from("<h", b, o)[0]
_f32 = lambda b, o: struct.unpack_from("<f", b, o)[0]
_vec3 = lambda b, o: struct.unpack_from("<3f", b, o)
_quat = lambda b, o: struct.unpack_from("<4f", b, o)

#: `StudioModel`, measured off every multi-model bodypart in the install (the flex walk
#: forced the correction; `docs/vtmb/facial_animation.md`). Only a bodygroup's second and later
#: models read differently from the older 160 — 4,423 of 4,444 models have one model per
#: bodypart, and no VtMB character does.
MODEL_STRIDE = 224
MESH_STRIDE = 60
#: `StudioEyeball`. Fixed by its consumer: the renderer's eye pass resolves a record as
#: `model_base + *(int*)(model_base + 0xC4) + materialparam * 0x8C`.
EYEBALL_STRIDE = 140

#: VtMB's trimmed `StudioAttachment`: record-relative name offset, flags, bone and a bone-local 3x4
#: transform. Modern Source's record is wider; the v2531 layout is pinned in
#: `docs/vtmb/animation_and_movers.md` A.6.
ATTACHMENT_STRIDE = 60
_MAX_ATTACHMENTS = 4096


def _cstr(b, o):
    e = b.index(b"\0", o)
    return b[o:e].decode("ascii", "replace")


def _cstr_rel(b, base, field_off):
    """A studio string index stored relative to its own record base. Index 0 means unset
    (Source's convention), which several `StudioSeqDesc.szactivitynameindex` carry."""
    rel = _i32(b, base + field_off)
    return _cstr(b, base + rel) if rel else ""


class Bone:
    __slots__ = ("index", "name", "parent", "pos", "quat", "posscale", "rotscale",
                 "pose_to_bone", "flags")

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)


def read_bones(d):
    """StudioBone[NumBones] (160B each) -> list[Bone] in header order.

    pos/quat are parent-relative bind pose (Source inches / xyzw). posscale/rotscale
    multiply the animation short deltas. pose_to_bone is the 3x4 world->bone
    inverse-bind (row-major, 12 floats). flags@136 carries the studio bone flags. It
    does not alter v2531 animation-channel decoding; the retail quaternion evaluator
    never reads it (see `docs/vtmb/animation_and_movers.md` A.4a)."""
    n = _i32(d, 240)
    base = _i32(d, 244)
    bones = []
    for i in range(n):
        b = base + i * 160
        bones.append(Bone(
            index=i,
            name=_cstr(d, b + _i32(d, b)),
            parent=_i32(d, b + 4),
            pos=_vec3(d, b + 32),
            quat=_quat(d, b + 44),
            posscale=_vec3(d, b + 60),
            rotscale=_quat(d, b + 72),
            pose_to_bone=struct.unpack_from("<12f", d, b + 88),
            flags=_i32(d, b + 136),
        ))
    return bones


Attachment = namedtuple("Attachment", "name flags bone pos quat")


def _matrix_quat(matrix):
    """Unit (x,y,z,w) quaternion for a row-major 3x3 rotation matrix."""
    m00, m01, m02, m10, m11, m12, m20, m21, m22 = matrix
    trace = m00 + m11 + m22
    if trace > 0.0:
        s = 2.0 * math.sqrt(trace + 1.0)
        q = ((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s)
    elif m00 > m11 and m00 > m22:
        s = 2.0 * math.sqrt(1.0 + m00 - m11 - m22)
        q = (0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s)
    elif m11 > m22:
        s = 2.0 * math.sqrt(1.0 + m11 - m00 - m22)
        q = ((m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s)
    else:
        s = 2.0 * math.sqrt(1.0 + m22 - m00 - m11)
        q = ((m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s)
    length = math.sqrt(sum(value * value for value in q))
    if not math.isfinite(length) or length <= 1e-6:
        raise ValueError("attachment carries an invalid rotation matrix")
    return tuple(value / length for value in q)


def attachments(d):
    """`StudioAttachment[NumLocalAttachments]` -> bone-local Source transforms.

    The name index is relative to its attachment record. The 3x4 matrix is row-major: its last
    column is the Source-inch translation and its 3x3 block is an ordinary proper rotation.
    """
    count = _i32(d, 328)
    base = _i32(d, 332)
    if count < 0 or count > _MAX_ATTACHMENTS:
        raise ValueError(f"invalid attachment count {count}")
    if count == 0:
        return []
    if base < 0 or base + count * ATTACHMENT_STRIDE > len(d):
        raise ValueError("attachment array runs past the model image")

    bone_count = _i32(d, 240)
    result = []
    for index in range(count):
        at = base + index * ATTACHMENT_STRIDE
        name_at = at + _i32(d, at)
        if name_at <= 0 or name_at >= len(d):
            raise ValueError(f"attachment {index} has invalid name offset {name_at}")
        bone = _i32(d, at + 8)
        if bone < 0 or bone >= bone_count:
            raise ValueError(f"attachment {index} names bone {bone} of {bone_count}")
        values = struct.unpack_from("<12f", d, at + 12)
        if not all(math.isfinite(value) for value in values):
            raise ValueError(f"attachment {index} carries a non-finite transform")
        rotation = (values[0], values[1], values[2],
                    values[4], values[5], values[6],
                    values[8], values[9], values[10])
        result.append(Attachment(
            name=_cstr(d, name_at),
            flags=_i32(d, at + 4),
            bone=bone,
            pos=(values[3], values[7], values[11]),
            quat=_matrix_quat(rotation),
        ))
    return result


def find_anim(d, name):
    """Locate a local anim by name -> (animdesc_base, numframes, fps). Names carry a
    leading '@'; match with or without it."""
    n = _i32(d, 264)
    base = _i32(d, 268)
    want = name.lstrip("@").lower()
    for i in range(n):
        ab = base + i * 72
        nm = _cstr(d, ab + _i32(d, ab)).lstrip("@").lower()
        if nm == want:
            return ab, _i32(d, ab + 12), _f32(d, ab + 4)
    return None


# --- include-model banks (shared animation libraries) -----------------------------
# An NPC .mdl carries only its own clips (mostly dialogue); locomotion, combat and idle
# come from shared banks pulled in by the studiohdr include-model mechanism, which forms
# a recursive tree (docs/vtmb/animation_and_movers.md A.7). NumIncludeModels@404 /
# IncludeModelIndex@408 -> StudioModelGroup[] (stride 116: int FilenameIndex@0 relative to
# the group-entry base, int LabelIndex@4, int Filler[27]). Every bank bone name is present
# in the NPC's Biped skeleton, so a clip decoded on its owning model's own bones plays on
# any NPC by name-matching the tracks to the skeleton at load time (glTFRuntime does this).
_MODELGROUP_STRIDE = 116


def read_includes(d):
    """Direct include-model paths (StudioModelGroup[NumIncludeModels]@404), each normalized
    to a `models/`-rooted, forward-slashed key. One level deep -- walk transitively via
    `resolve_tree`."""
    n = _i32(d, 404)
    base = _i32(d, 408)
    out = []
    for i in range(n):
        gb = base + i * _MODELGROUP_STRIDE
        p = _cstr(d, gb + _i32(d, gb)).replace("\\", "/")
        if p:
            out.append(p if p.startswith("models/") else "models/" + p)
    return out


_SEQDESC_STRIDE = 764

#: `MAXSTUDIOBLENDS`. The inline `anim[16][16]` grid keeps this row stride whatever the
#: authored extents are, so an authored grid is a sub-rectangle of the fixed array.
MAXSTUDIOBLENDS = 16

_POSEPARAM_STRIDE = 20

_AUTOLAYER_STRIDE = 4

#: VtMB v2531's old ``mstudioevent_t``.  The record stops after ``options[64]``; unlike
#: Source 2013's 80-byte form it has no descriptor-relative event-name index.
_EVENT_STRIDE = 76

#: The shipped maximum is 11.  The generous format guard prevents a damaged descriptor from
#: walking arbitrary model bytes while retaining room for independently authored models.
_MAX_EVENTS = 256

#: The gate on `numautolayers`@660. Seven single-sequence scenery and weapon models read 764
#: there — the descriptor tail running past the end of the file into the string table — so a
#: count this side of plausible is the first of the three bounds
#: `docs/vtmb/animation_and_movers.md` A.3 requires. The shipped maximum is 2.
_MAX_AUTOLAYERS = 16

#: `mstudiomovement_t`, addressed by one StudioAnimDesc's `nummovements`@16 and
#: `movementindex`@20. The index is relative to the animdesc. `position` is the cumulative
#: Source-space displacement at this record's end frame; the final record therefore carries one
#: complete cycle's travel.
_MOVEMENT_STRIDE = 44
Movement = namedtuple("Movement", "endframe motionflags v0 v1 angle vector position")

#: One sequence-timeline event. ``cycle`` is normalized over the sequence, ``event`` is the
#: numeric dispatch id, ``type`` is the old event-type field, and ``options`` is its decoded
#: NUL-terminated 64-byte payload.
Event = namedtuple("Event", "cycle event type options")

#: The scalar part a route motor needs from an in-place locomotion clip. Distances are converted
#: from Source inches to centimetres here, at the offline seam; no coordinate direction is emitted.
MovementSummary = namedtuple(
    "MovementSummary", "cycle_seconds ground_distance_cm ground_speed_cm_s"
)

#: One `mstudioposeparamdesc_t` (`NumLocalPoseParameters`@384 / `LocalPoseParamIndex`@388).
#: A non-zero `loop` is the wrap modulus an axis folds the parameter through before it
#: normalizes over `start`..`end`.
PoseParam = namedtuple("PoseParam", "index name flags start end loop")

#: One authored blend-grid cell: its position on the two axes and the local animation index
#: it selects. `anim` is read verbatim and may fall outside `NumLocalAnims`.
Cell = namedtuple("Cell", "axis0 axis1 anim")

#: One sequence's blend space. `numblends`@52 is the declared cell count and `groupsize`@572
#: the two axis extents; `paramindex`@580 names the pose parameter driving each axis (-1 when
#: unused) and `paramstart`@588 / `paramend`@596 give that axis's range in the parameter's own
#: units. `cells` is every cell inside the extents, row-major over axis 0.
Grid = namedtuple("Grid", "numblends groupsize paramindex paramstart paramend cells")

#: What a `Seq` built outside `local_sequences` carries — a raw animation baked as a clip
#: answers to no descriptor, so it declares no cells rather than claiming a base cell.
_NO_GRID = Grid(numblends=1, groupsize=(1, 1), paramindex=(-1, -1),
                paramstart=(0.0, 0.0), paramend=(0.0, 0.0), cells=())

#: One game-facing sequence. The first four fields are the bake inputs; `activity`,
#: `actweight` and `flags` are the engine's own selection keys (see `local_sequences`);
#: `grid` is the blend space the label names (see `read_grid`); `bbmin`/`bbmax` are the
#: sequence's own model-space bounding box in Source units (see `local_sequences`);
#: `fade` is the authored transition duration in seconds (see `local_sequences`);
#: `autolayers` names the sequences this one is composed with (see `read_autolayers`);
#: `events` carries the sequence timeline records (see `read_events`).
Seq = namedtuple("Seq",
                 "label base frames fps activity actweight flags grid bbmin bbmax fade autolayers events",
                 defaults=(_NO_GRID, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0.2, (), ()))


def pose_parameters(d):
    """This model's pose parameters -> list[PoseParam] in header order.

    `NumLocalPoseParameters`@384 / `LocalPoseParamIndex`@388 address 20-byte records:
    `nameindex`@0 (relative to the record), `flags`@4, `start`@8, `end`@12, `loop`@16. The
    name is the durable key — a sequence axis binds by *index* into this array, so the two
    are read together or an axis cannot be named (`docs/vtmb/animation_and_movers.md` A.3)."""
    n = _i32(d, 384)
    base = _i32(d, 388)
    out = []
    for i in range(n):
        pb = base + i * _POSEPARAM_STRIDE
        out.append(PoseParam(index=i, name=_cstr_rel(d, pb, 0), flags=_i32(d, pb + 4),
                             start=_f32(d, pb + 8), end=_f32(d, pb + 12),
                             loop=_f32(d, pb + 16)))
    return out


def read_grid(d, sb):
    """One StudioSeqDesc's blend space at descriptor base `sb` -> Grid.

    `groupsize`@572 gives the extents and `numblends`@52 the cell count; the product of the
    two extents equals `numblends` on all 294 multi-blend sequences of the installed character
    tree, which is what establishes these as the axis extents. Extents outside 1..16 (or a
    product disagreeing with `numblends`) fall back to the base cell alone, so a descriptor
    the format cannot explain yields the clip it always did rather than a grid of noise.

    A cell sits at `56 + (i0 * 16 + i1) * 2` — axis 0 takes the fixed 16-short row stride and
    axis 1 the column. That orientation is retail's own: on a 3x3 `smith_aim_layer` the
    witnessed cell `[1, 0]` decodes the four animations at rows 1-2, columns 0-1, and the
    transposed address would name a different four.

    `paramindex`@580 binds each axis to a `pose_parameters` index and is -1 when the axis is
    unused; `paramstart`@588 / `paramend`@596 are that axis's range in the parameter's units."""
    numblends = _i32(d, sb + 52)
    groupsize = struct.unpack_from("<2i", d, sb + 572)
    paramindex = struct.unpack_from("<2i", d, sb + 580)
    grid = Grid(numblends=numblends, groupsize=groupsize, paramindex=paramindex,
                paramstart=struct.unpack_from("<2f", d, sb + 588),
                paramend=struct.unpack_from("<2f", d, sb + 596),
                cells=())
    n0, n1 = groupsize
    if not (1 <= n0 <= MAXSTUDIOBLENDS and 1 <= n1 <= MAXSTUDIOBLENDS) or n0 * n1 != numblends:
        n0 = n1 = 1
    cells = tuple(
        Cell(axis0=i0, axis1=i1,
             anim=_h16(d, sb + 56 + (i0 * MAXSTUDIOBLENDS + i1) * 2))
        for i0 in range(n0) for i1 in range(n1)
    )
    return grid._replace(cells=cells)


def read_autolayers(d, sb, ns):
    """One StudioSeqDesc's autolayer entries at descriptor base `sb` -> tuple of local
    sequence indices, in the order the dispatcher walks them.

    `numautolayers`@660 / `autolayerindex`@664 declare a list of 4-byte entries, each a bare
    sequence index — VtMB's record carries no pose parameter, flags or ramp, so the binding is
    the whole payload. The client dispatcher supplies caller weight 1.0 and the accumulator then
    multiplies it by the target animation's per-bone mask
    (`docs/vtmb/animation_and_movers.md` A.3).

    **The index is relative to the descriptor and the entries address this model's own local
    sequence array.** Both halves are measured, not assumed: read that way, all 345 entries on
    the male `move_and_ranged` bank and all 340 on the female resolve in range and every one
    names a `_layer` or `_delta` sequence, where reading `autolayerindex` as an absolute file
    offset resolves 64 and 43 of them and names none. Do not re-derive this.

    Three bounds, all required. The count is gated because seven single-sequence scenery and
    weapon models read 764 there and their array address runs off the end of the image; the
    array is bounded against the image; and every entry is bounded against `ns`
    (`NumLocalSeq`@272). A descriptor failing any of them yields no entries rather than
    entries read out of someone else's bytes."""
    n = _i32(d, sb + 660)
    if not (0 < n <= _MAX_AUTOLAYERS):
        return ()
    base = sb + _i32(d, sb + 664)
    if base <= 0 or base + n * _AUTOLAYER_STRIDE > len(d):
        return ()
    entries = struct.unpack_from(f"<{n}i", d, base)
    if any(not (0 <= t < ns) for t in entries):
        return ()
    return entries


def read_events(d, sb):
    """One StudioSeqDesc's animation timeline -> tuple[Event, ...].

    ``numevents``@20 and ``eventindex``@24 address descriptor-relative 76-byte records:
    ``float cycle``@0, ``int event``@4, ``int type``@8 and ``char options[64]``@12.  VtMB's
    v2531 form predates the later event-name index.  A non-positive relative index, implausible
    count, out-of-image array, or options field without a NUL terminator yields no records rather
    than interpreting an adjacent descriptor as events.
    """
    count = _i32(d, sb + 20)
    relative = _i32(d, sb + 24)
    if not (0 < count <= _MAX_EVENTS) or relative <= 0:
        return ()
    base = sb + relative
    end = base + count * _EVENT_STRIDE
    if base < 0 or end > len(d):
        return ()

    events = []
    for index in range(count):
        record = base + index * _EVENT_STRIDE
        options_raw = bytes(d[record + 12:record + _EVENT_STRIDE])
        terminator = options_raw.find(b"\0")
        if terminator < 0:
            return ()
        events.append(Event(
            cycle=_f32(d, record),
            event=_i32(d, record + 4),
            type=_i32(d, record + 8),
            options=options_raw[:terminator].decode("ascii", "replace"),
        ))
    return tuple(events)


def local_animation(d, index):
    """The local animation at `index` -> (name, animdesc_base, numframes, fps), or None when
    the index falls outside `NumLocalAnims`@264. The name carries a leading '@' on the disk
    bytes; it is stripped here, exactly as `find_anim` matches."""
    n = _i32(d, 264)
    if not (0 <= index < n):
        return None
    ab = _i32(d, 268) + index * 72
    return _cstr(d, ab + _i32(d, ab)).lstrip("@"), ab, _i32(d, ab + 12), _f32(d, ab + 4)


def read_movements(d, animdesc_base):
    """One StudioAnimDesc's authored movement records -> tuple[Movement, ...].

    `movementindex` is relative to the descriptor, like `animindex`. A malformed count/range is
    treated as no movement: callers retain their existing gait fallback rather than reading past a
    damaged model image. The raw values remain in Source units because they are format data; use
    :func:`movement_summary` for the centimetre scalar exported to Unreal.
    """
    count = _i32(d, animdesc_base + 16)
    relative = _i32(d, animdesc_base + 20)
    if count <= 0 or relative <= 0:
        return ()
    base = animdesc_base + relative
    end = base + count * _MOVEMENT_STRIDE
    if base < 0 or end > len(d):
        return ()

    out = []
    for index in range(count):
        record = base + index * _MOVEMENT_STRIDE
        values = struct.unpack_from("<ii9f", d, record)
        out.append(Movement(
            endframe=values[0], motionflags=values[1], v0=values[2], v1=values[3],
            angle=values[4], vector=values[5:8], position=values[8:11],
        ))
    return tuple(out)


def movement_summary(d, animdesc_base, frames, fps):
    """Authored cycle movement -> MovementSummary in seconds and centimetres, or ``None``.

    Source's ground-speed calculation is the complete movement vector's length divided by the
    animation duration. glTF samples frame ``i`` at ``i / fps``, so a clip with ``frames`` samples
    spans ``(frames - 1) / fps`` seconds. Zero/invalid motion stays absent and lets the runtime keep
    its established fallback speed.
    """
    movements = read_movements(d, animdesc_base)
    if not movements or frames <= 1 or not math.isfinite(fps) or fps <= 0.0:
        return None
    cycle_seconds = (frames - 1) / fps
    position = movements[-1].position
    ground_distance_cm = math.sqrt(sum(value * value for value in position)) * 2.54
    if (not math.isfinite(cycle_seconds) or cycle_seconds <= 0.0
            or not math.isfinite(ground_distance_cm) or ground_distance_cm <= 0.0):
        return None
    return MovementSummary(
        cycle_seconds=cycle_seconds,
        ground_distance_cm=ground_distance_cm,
        ground_speed_cm_s=ground_distance_cm / cycle_seconds,
    )


def local_sequences(d):
    """This model's own game-facing sequences -> list[Seq].

    StudioSeqDesc[NumLocalSeq@272] (stride 764): label@0 (rel. seq base), anim[0][0]@56 (the
    blend grid's base cell) -> a local anim index into LocalAnims. A label whose base cell is
    out of range is skipped. Deduped by lowercased label (first wins); the label is the name
    the game references (scripted_sequence `m_iszPlay`, activities).

    `base`/`frames`/`fps` are the base cell's, so a single-cell sequence bakes as it always
    did. `grid` carries the whole blend space beside it — extents, pose-parameter binding and
    every cell — because a 9x1 walk grid selects a different animation per `move_yaw` and
    baking the base cell as the clip drops the other eight. The cells are data beside the
    clips: blending them is the host's job, not the exporter's.

    Three more fields carry how the *engine* picks a sequence, rather than how content names
    one. `szactivitynameindex`@4 is the activity literal (`ACT_IDLE`, `ACT_WALK`,
    `ACT_DISPOSITION`, ...), empty on a layer/plumbing sequence; `actweight`@16 is the
    weighted-random share among the sequences sharing an activity (`claws_aggressive_run` 7 vs
    its two alts at 3); `flags`@8 carries the studio sequence bits. The sibling `activity`@12
    int stays -1 on disk — the game DLL resolves the name to an enum at model load, so the
    *name* is the durable key. `events` carries the descriptor-relative timeline decoded from
    `numevents`/`eventindex`@20/24.

    `bbox`@28 is two Vectors — the sequence's model-space bounding box over every frame it
    animates, in Source units. It is the authored envelope of the *posed* model, not of the
    reference pose, so on a rig whose bones carry the motion it is far larger than the mesh:
    `cin_sheriff_sword`'s `scene` spans 881 units while its vertices span 28. The
    header's `ViewBBMin`/`ViewBBMax`@204/216 are zero across this corpus, which leaves this
    the only per-clip bound the files carry.

    `fade`@612 is the sequence's authored transition duration in seconds, the value the
    engine combines across a sequence pair to time a base-sequence crossfade
    (`docs/vtmb/animation_and_movers.md`). Three floats sit at @612/@616/@620 and are
    byte-identical in every shipped sequence, so the first is read and the other two are
    left alone."""
    ns = _i32(d, 272); sbase = _i32(d, 276)
    na = _i32(d, 264); abase = _i32(d, 268)
    # Every descriptor's label by index, read before the walk because an autolayer entry
    # addresses this array directly and may name a descriptor the dedup below drops. A dropped
    # descriptor is a duplicate label, so the name it resolves to is the same either way.
    labels = [_cstr_rel(d, sbase + i * _SEQDESC_STRIDE, 0) for i in range(ns)]
    out, seen = [], set()
    for i in range(ns):
        sb = sbase + i * _SEQDESC_STRIDE
        label = labels[i]
        # Read before the skips, so every declared descriptor's grid is read off the same
        # walk that reads its label rather than only the ones that survive the dedup.
        grid = read_grid(d, sb)
        a0 = grid.cells[0].anim
        key = label.lower()
        if not label or key in seen or not (0 <= a0 < na):
            continue
        seen.add(key)
        ab = abase + a0 * 72
        out.append(Seq(label=label, base=ab, frames=_i32(d, ab + 12), fps=_f32(d, ab + 4),
                       activity=_cstr_rel(d, sb, 4), actweight=_i32(d, sb + 16),
                       flags=_i32(d, sb + 8), grid=grid,
                       bbmin=_vec3(d, sb + 28), bbmax=_vec3(d, sb + 40),
                       fade=_f32(d, sb + 612),
                       autolayers=tuple(labels[t] for t in read_autolayers(d, sb, ns)),
                       events=read_events(d, sb)))
    return out


def resolve_tree(load, model_key):
    """Depth-first include-model resolution with cycle dedup -> ordered [(key, d), ...]: the
    NPC model first, then its banks transitively (docs/vtmb/animation_and_movers.md A.7).

    `load(key)` returns the model's raw `.mdl` bytes (or None if it cannot be read). A model
    reachable by several include paths (frenzy / pc_idles) is visited exactly once, so the
    order also fixes clip ownership: the first model that defines a label owns it."""
    order, seen = [], set()

    def visit(key):
        k = key.lower()
        if k in seen:
            return
        seen.add(k)
        d = load(key)
        if d is None:
            return
        order.append((key, d))
        for inc in read_includes(d):
            visit(inc)

    visit(model_key)
    return order


def bone_names(d):
    """Bone names in header order (for name-keyed retarget diagnostics)."""
    n = _i32(d, 240); base = _i32(d, 244)
    return [_cstr(d, base + i * 160 + _i32(d, base + i * 160)) for i in range(n)]


def _rle_channel(v, p, numframes):
    """Decode one `mstudioanimvalue_t` RLE channel into a per-frame value list.

    Each run: byte valid, byte total, then `valid` int16 keys. Frames [0,valid) take
    the explicit keys; [valid,total) clamp to the last key. Returns numframes shorts.

    A run may claim more keys than the file stores. Retail's `ExtractAnimValue` walks the
    runs lazily and stops at the frame it asked for, so it never reads past the keys it
    needs and a shipped model may legitimately end mid-run -- `stage_light.mdl` is 11,400
    bytes and a literal walk asks for 11,419. Decoding every frame eagerly has to clamp
    instead: read what is stored, hold the last readable key across the rest of the run,
    and pad a channel that ends early. The cursor still advances by the *authored* stride
    so a later readable run lands at the right offset."""
    out = []
    remaining = numframes
    end = len(v)
    while remaining > 0 and p + 2 <= end:
        valid = v[p]
        total = v[p + 1]
        p += 2
        if total <= 0:
            break                      # a zero-length run cannot terminate the walk
        stored = min(valid, max(0, (end - p) // 2))
        keys = struct.unpack_from(f"<{stored}h", v, p) if stored else (0,)
        p += 2 * valid                 # authored stride, not the clamped read
        last = keys[-1]                # also covers valid == 0, which indexes nothing
        for f in range(total):
            out.append(keys[f] if f < stored else last)
        remaining -= total
    if len(out) < numframes:
        out.extend([out[-1] if out else 0] * (numframes - len(out)))
    return out[:numframes]


def read_anim(d, bones, animdesc_base, numframes):
    """Decode an animation into per-frame per-bone (pos3, quat4) in Source coords.

    At animdesc_base + animindex(@48): one 32B record per bone (weight@0, offset[7]@4
    = posX,posY,posZ,rotX,rotY,rotZ,rotW, each relative to the record start; 0 =
    channel not animated, use bind value). Sample*scale is a delta on the bind.

    `weight`@0 is a zero test rather than a factor. Both retail channel decoders compare it
    against zero before anything else and, on zero, write a zero position and a zero
    quaternion and return — reading no channel offset, no track and no bind field. So a
    zero-weight bone yields exact zeros here too, not the bind pose and not an identity
    rotation.

    It is a binary authored mask and shipped content exercises it: the field is only ever
    0.0 or 1.0, and the partial-body `*_layer` overlays carry the bulk of the zeros — a
    `<weapon>_aim_layer` keeps the spine-up chain and both arms, `lookback_left_layer` keeps
    `Bip01 Head` alone. Gating on the channel offsets instead would reach the same bytes,
    because no zero-weight record carries a non-zero offset, but it would conflate two
    different states: a masked-OUT bone (zeros, the base pose survives it) against a bone
    inside the mask that simply holds its bind (a real authored pose). Census and the mask
    inventory: `docs/vtmb/animation_and_movers.md` §A.4."""
    animindex = _i32(d, animdesc_base + 48)
    recs = animdesc_base + animindex
    frames = [[None] * len(bones) for _ in range(numframes)]
    for bi, bone in enumerate(bones):
        rb = recs + bi * 32
        if _f32(d, rb) == 0.0:
            for f in range(numframes):
                frames[f][bi] = ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 0.0))
            continue
        offs = struct.unpack_from("<7i", d, rb + 4)
        chan = []
        for c in range(7):
            if offs[c]:
                chan.append(_rle_channel(d, rb + offs[c], numframes))
            else:
                chan.append(None)
        px, py, pz, qx, qy, qz, qw = chan
        bpos, bq = bone.pos, bone.quat
        ps, rs = bone.posscale, bone.rotscale
        for f in range(numframes):
            # position: sample*scale is a DELTA on the bind position
            pos = (
                bpos[0] + (px[f] * ps[0] if px else 0.0),
                bpos[1] + (py[f] * ps[1] if py else 0.0),
                bpos[2] + (pz[f] * ps[2] if pz else 0.0),
            )
            # rotation: each quaternion component is `sample*rotscale` when its channel
            # is animated, else the **bind** component (not 0). A channel with no offset
            # means that axis isn't animated, so it holds its bind value — filling it with
            # 0 collapses a bone whose bind carries a real rotation (the clavicles, whose
            # animations often store only W) onto identity, throwing the arm/upper body.
            # Where the bind component is ~0 (most limb bones) this equals a 0 fill, which
            # is why it only shows on bones with a large bind rotation.
            q = (
                qx[f] * rs[0] if qx else bq[0],
                qy[f] * rs[1] if qy else bq[1],
                qz[f] * rs[2] if qz else bq[2],
                qw[f] * rs[3] if qw else bq[3],
            )
            n = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]) ** 0.5 or 1.0
            q = (q[0] / n, q[1] / n, q[2] / n, q[3] / n)
            frames[f][bi] = (pos, q)
    return frames


def decode_skinned(d, v, mesh_map=None):
    """Decode LOD0 geometry per material, carrying skin + the raw vertex id.

    Like `mdl.decode` but for skinned characters: returns a dict material -> surface
    where surface = {pos:[(x,y,z)], nrm:[(x,y,z)], uv:[(u,v)], joints:[[b0..b3]],
    weights:[[w..]], tris:[(i,j,k)]} in **Source** coords, one deduped vertex list per
    material. The 44-byte format carries skin and an authored shading normal. Compact 12- and
    8-byte rigid models carry neither: their vertices bind to their sole bone and their normals are
    reconstructed geometrically by the Unreal-native exporter.

    `mesh_map`, when given a list, is filled with one record per StudioMesh:
    `{material, model_base, mesh_index, vertex_offset, remap}` where `remap` maps the
    model-global vertex id to this material's surface index. That is the join the flex
    bake needs — a `StudioVertAnim` addresses a mesh-local vertex, and the surface
    dedupes across meshes, so nothing else can line the two up."""
    from elysium_pipeline.formats import mdl
    hmin, hmax = _vec3(d, 180), _vec3(d, 192)
    materials = []
    ntex = _i32(d, 292); tex_index = _i32(d, 296)
    for i in range(ntex):
        to = tex_index + i * 20
        materials.append(_cstr(d, to + _i32(d, to)))
    num_bp = _i32(d, 320); bp_index = _i32(d, 324)
    vtx_bp_off = _i32(v, 32)
    surfaces = {}
    for bp in range(num_bp):
        mbp = bp_index + bp * 16
        num_models = _i32(d, mbp + 4)
        model_index = _i32(d, mbp + 12)
        vbp = vtx_bp_off + bp * 8
        vtx_model_off = _i32(v, vbp + 4)
        for m in range(num_models):
            model_base = mbp + model_index + m * MODEL_STRIDE
            num_meshes = _i32(d, model_base + 136)
            mesh_index = _i32(d, model_base + 140)
            num_verts = _i32(d, model_base + 144)
            vertex_index = _i32(d, model_base + 148)
            vlist = _i32(d, model_base + 156)
            vstride = mdl.VSTRIDE.get(vlist, 44)
            skin = read_skin(d, model_base, vertex_index, num_verts, vlist)
            vmodel = vbp + vtx_model_off + m * 8
            vlod = vmodel + _i32(v, vmodel + 4)
            vtx_mesh_off = _i32(v, vlod + 4)
            for mi in range(num_meshes):
                mesh_base = model_base + mesh_index + mi * MESH_STRIDE
                material = _i32(d, mesh_base + 0)
                mesh_numverts = _i32(d, mesh_base + 8)
                vertex_offset = _i32(d, mesh_base + 12)
                vmesh = vlod + vtx_mesh_off + mi * 8
                num_sg = _u16(v, vmesh + 0)
                sg_off = _i32(v, vmesh + 4)
                matname = materials[material] if material < len(materials) else f"mat{material}"
                surf = surfaces.setdefault(matname, dict(pos=[], nrm=[], uv=[], joints=[],
                                                         weights=[], tris=[]))
                remap = {}
                for sg in range(num_sg):
                    sgb = vmesh + sg_off + sg * mdl.STRIPGROUP_STRIDE
                    sg_numverts = _u16(v, sgb + 0)
                    numstrips = _u16(v, sgb + 4)
                    vtable = sgb + _i32(v, sgb + 8)
                    itable = sgb + _i32(v, sgb + 12)
                    strip_off = _i32(v, sgb + 16)
                    form = mdl._vtable_form(v, vtable, sg_numverts, mesh_numverts)
                    vt_stride, vt_off = form
                    for s in range(numstrips):
                        sh = sgb + strip_off + s * 16
                        num_indices = _u16(v, sh + 0)
                        strip_index_off = _u16(v, sh + 2)
                        for k in range(0, num_indices, 3):
                            tri = []
                            for j in range(3):
                                local = _u16(v, itable + (strip_index_off + k + j) * 2)
                                gvid = vertex_offset + _u16(v, vtable + local * vt_stride + vt_off)
                                if gvid not in remap:
                                    remap[gvid] = len(surf["pos"])
                                    sv = model_base + vertex_index + gvid * vstride
                                    px, py, pz, u, vv = mdl._read_vertex(
                                        d, sv, vlist, hmin, hmax)
                                    bs, ws = skin[gvid]
                                    j4 = (bs + [0, 0, 0, 0])[:4]
                                    w4 = (ws + [0.0, 0.0, 0.0, 0.0])[:4]
                                    surf["pos"].append((px, py, pz))
                                    # Compact rigid formats carry only a packed normal that the
                                    # static decoder deliberately does not interpret. Zero selects
                                    # the skeletal exporter's area-weighted geometric fallback.
                                    surf["nrm"].append(
                                        _vec3(d, sv + 24) if vlist == 0 else (0.0, 0.0, 0.0))
                                    surf["uv"].append((u, vv))
                                    surf["joints"].append(j4)
                                    surf["weights"].append(w4)
                                tri.append(remap[gvid])
                            surf["tris"].append(tuple(tri))
                if mesh_map is not None:
                    # `materialtype`@24 is 1 on an eyeball mesh and `materialparam`@28 selects
                    # which eyeball; the draw loop dispatches its eye path on that field alone.
                    mesh_map.append(dict(material=matname, model_base=model_base,
                                         mesh_index=mi, vertex_offset=vertex_offset,
                                         materialtype=_i32(d, mesh_base + 24),
                                         materialparam=_i32(d, mesh_base + 28),
                                         remap=remap))
    return surfaces


def read_skin(d, model_base, vertex_index, num_vertices, vlist=0):
    """Per-vertex skin from the SKINNED StudioVertex BoneWeight (44B verts).

    BoneWeight @ vert+0: byte Weight[3], short Bone[3]@4, byte NumBones@10 (unused on
    VtMB — derive from nonzero weights). Returns [(bones3, weights3), ...] with
    weights normalized to sum 1.

    Compact vertex formats carry no weights. They occur on rigid one-bone studio models, so their
    only faithful skin is weight 1 on bone 0; a compact model with any other bone count is refused
    rather than guessed."""
    if vlist != 0:
        num_bones = _i32(d, 240)
        if num_bones != 1:
            raise ValueError(
                f"compact vertex list {vlist} has {num_bones} bones; rigid binding is ambiguous")
        return [([0], [1.0]) for _ in range(num_vertices)]
    out = []
    for i in range(num_vertices):
        sv = model_base + vertex_index + i * 44
        w = struct.unpack_from("<3B", d, sv + 0)
        bn = struct.unpack_from("<3h", d, sv + 4)
        infl = [(bn[k], w[k]) for k in range(3) if w[k] > 0]
        if not infl:
            infl = [(bn[0], 255)]
        s = sum(x[1] for x in infl) or 1
        out.append(([b for b, _ in infl], [wt / s for _, wt in infl]))
    return out


# --- the face (docs/vtmb/facial_animation.md) ------------------------------------------
# The studiohdr facial block starts at 344 -- eight bytes past a naive VAMPTools field
# walk, which drops two ints between LocalAttachmentIndex@332 and NumFlexDescs. These are
# the offsets every array closes on (count x stride exactly, on all 4,444 models).
#: The phoneme filter -- two floats the lipsync blend width is clamped to, per model.
#: `client.dll` FUN_100c3be0 reads them as the fallback for the phonemefilter_min/max ConVars,
#: which are inert unless BOTH are set, so these are what every shipped line actually uses.
H_PHONEME_FILTER = 232

H_NUM_FLEXDESC, H_FLEXDESC = 344, 348
H_NUM_FLEXCTRL, H_FLEXCTRL = 352, 356
H_NUM_FLEXRULE, H_FLEXRULE = 360, 364
H_NUM_MOUTH, H_MOUTH = 376, 380

FLEX_STRIDE = 32            # StudioFlex
VA_STRIDE = {1: 8, 0: 20}   # StudioFlex.VertAnimType -> StudioVertAnim stride

#: Source's `StudioFlexOp_t`. Only codes 1-7 appear across the 201 rigged VtMB models.
FLEX_OPS = {1: "CONST", 2: "FETCH1", 3: "FETCH2", 4: "ADD", 5: "SUB", 6: "MUL",
            7: "DIV", 8: "NEG", 9: "EXP", 10: "OPEN", 11: "CLOSE", 12: "COMMA",
            13: "MAX", 14: "MIN"}

# StudioRender.dll (imagebase 0x2C000000): the 5,314-entry unit-vector table a compressed
# vertex-animation record indexes by BYTE offset, and the two magnitude scales
# (`8.0` at 0x2C06C4FC for position, the `FADD ST0,ST0` doubling on the normal path).
ANORM_VA = 0x2C06E008
ANORM_COUNT = 5314
DELTA_SCALE = 8.0
NDELTA_SCALE = 2.0


def read_anorms(dll_path=None):
    """The unit-vector table out of the user's own `Bin/StudioRender.dll` -> [(x,y,z)] in
    Source space, indexed by `u16 // 12`.

    It is compiled into the renderer, not shipped as data, so it is game-derived like every
    other VtMB byte: extracted on demand at export time, never committed. Raises if the DLL
    is absent or the address falls outside every section."""
    if dll_path is None:
        from elysium_pipeline.formats import install
        dll_path = os.path.join(install.GAME_ROOT, "Bin", "StudioRender.dll")
    with open(dll_path, "rb") as f:
        data = f.read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    imgbase = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    base = pe + 24 + optsz
    for i in range(nsec):
        s = base + i * 40
        va = imgbase + struct.unpack_from("<I", data, s + 12)[0]
        span = max(struct.unpack_from("<I", data, s + 8)[0],
                   struct.unpack_from("<I", data, s + 16)[0])
        if va <= ANORM_VA < va + span:
            off = struct.unpack_from("<I", data, s + 20)[0] + (ANORM_VA - va)
            return [struct.unpack_from("<3f", data, off + i * 12) for i in range(ANORM_COUNT)]
    raise RuntimeError(f"{dll_path}: no section holds {ANORM_VA:#x}")


def flex_descs(d):
    """`mstudioflexdesc_t[NumFlexDescs]` (4 B) -> the FACS names, index = flexdesc id.
    Every rigged VtMB character ships the same 65 in the same order."""
    n, base = _i32(d, H_NUM_FLEXDESC), _i32(d, H_FLEXDESC)
    return [_cstr_rel(d, base + i * 4, 0) for i in range(n)]


def flex_controllers(d):
    """`mstudioflexcontroller_t[NumFlexControllers]` (20 B) -> [(type, name, min, max)].

    Both string indices are relative to the record base. `localToGlobal`@8 is -1 on disk
    (the engine remaps it at load), so it is not carried. The 44 shipped controllers group
    by type into eyelid/brow/nose/mouth/phoneme."""
    n, base = _i32(d, H_NUM_FLEXCTRL), _i32(d, H_FLEXCTRL)
    out = []
    for i in range(n):
        b = base + i * 20
        out.append((_cstr_rel(d, b, 0), _cstr_rel(d, b, 4), _f32(d, b + 12), _f32(d, b + 16)))
    return out


def flex_rules(d):
    """`mstudioflexrule_t[NumFlexRules]` (12 B + an 8 B op array) -> [(flexdesc, ops)].

    An op is `(opname, int_operand, float_operand)`; the same 4 bytes read both ways, since
    `CONST` carries a float and `FETCH1`/`FETCH2` a controller index. The stack machine
    evaluates left to right and its final value is the weight of the rule's flexdesc."""
    n, base = _i32(d, H_NUM_FLEXRULE), _i32(d, H_FLEXRULE)
    out = []
    for i in range(n):
        b = base + i * 12
        numops, opidx = _i32(d, b + 4), _i32(d, b + 8)
        ops = []
        for q in range(numops):
            ob = b + opidx + q * 8
            code = _i32(d, ob)
            ops.append((FLEX_OPS.get(code, f"op{code}"), _i32(d, ob + 4), _f32(d, ob + 4)))
        out.append((_i32(d, b), ops))
    return out


def mouths(d):
    """`mstudiomouth_t[NumMouths]` (20 B) -> [(bone, forward, flexdesc)]. One per rigged
    character, pointing at the `mouth` flexdesc: Source's audio-amplitude jaw, driven by the
    envelope of the line rather than by the phoneme track."""
    n, base = _i32(d, H_NUM_MOUTH), _i32(d, H_MOUTH)
    return [(_i32(d, base + i * 20), _vec3(d, base + i * 20 + 4), _i32(d, base + i * 20 + 16))
            for i in range(n)]


def phoneme_filter(d):
    """The `studiohdr` pair at 232/236 -> (min, max) seconds, the bounds a phoneme's own
    duration is clamped to before it becomes the lipsync blend width. Recorded as an
    `int[2] Unknown` by every prior field walk; they are floats. Rigged characters carry
    (0.080, 0.100) -- `Jeanette` alone (0.080, 0.105) -- and unrigged models (0.065, 0.100)
    or (0, 0)."""
    return struct.unpack_from("<2f", d, H_PHONEME_FILTER)


def eyeballs(d, model_base):
    """`StudioEyeball[NumEyeballs]` (140 B) for one `StudioModel` -> [dict].

    `NumEyeballs`@192 / `EyeballIndex`@196, the latter relative to the model record. Two per
    character model, mirrored in Z.

    `uppertarget`/`lowertarget` are **linear offsets in eyeball units, not angles** — the
    renderer takes `asin(target / radius)`. Reading them as radians still moves a lid, which
    is what makes the error easy to keep.

    `texture`/`iris_material`/`glint_material` hold real texture-table indices but the renderer
    reads none of them (Source's `texture`/`unused1`/`unused2`); the bound material is the eye
    mesh's own skinref and the iris comes from the `.vmt`'s `$iris`. They are carried for
    diagnostics only. The 16 bytes at +124 are read by nothing, so there is no data-driven
    gaze limit."""
    n, rel = _i32(d, model_base + 192), _i32(d, model_base + 196)
    out = []
    for i in range(max(n, 0)):
        b = model_base + rel + i * EYEBALL_STRIDE
        out.append(dict(
            index=i, bone=_i32(d, b + 4),
            org=_vec3(d, b + 8), zoffset=_f32(d, b + 20), radius=_f32(d, b + 24),
            up=_vec3(d, b + 28), forward=_vec3(d, b + 40),
            iris_scale=_f32(d, b + 60),
            upperflexdesc=[_i32(d, b + 68 + 4 * q) for q in range(3)],
            lowerflexdesc=[_i32(d, b + 80 + 4 * q) for q in range(3)],
            uppertarget=[_f32(d, b + 92 + 4 * q) for q in range(3)],
            lowertarget=[_f32(d, b + 104 + 4 * q) for q in range(3)],
            upperlidflexdesc=_i32(d, b + 116), lowerlidflexdesc=_i32(d, b + 120),
            # unread by the renderer; kept so a survey can resolve the eye's texture names
            texture=_i32(d, b + 52), iris_material=_i32(d, b + 56),
            glint_material=_i32(d, b + 64)))
    return out


def mesh_flexes(d, model_base, mesh_index):
    """One `StudioMesh`'s `StudioFlex[]` -> [{flexdesc, targets[4], numverts, vertanimtype}].

    `StudioMesh.NumFlexes`@16 / `FlexIndex`@20, the latter relative to the mesh record;
    `base` is kept because `StudioFlex.VertIndex` is in turn relative to the flex record.
    `targets` is the trapezoid the flexdesc's weight is remapped through at draw time — a
    runtime remap, so it rides in the manifest rather than being baked into the morph."""
    mshb = model_base + _i32(d, model_base + 140) + mesh_index * MESH_STRIDE
    n, rel = _i32(d, mshb + 16), _i32(d, mshb + 20)
    out = []
    for i in range(max(n, 0)):
        b = mshb + rel + i * FLEX_STRIDE
        out.append(dict(base=b, flexdesc=_i32(d, b),
                        targets=[_f32(d, b + 4 + 4 * q) for q in range(4)],
                        numverts=_i32(d, b + 20), vertindex=_i32(d, b + 24),
                        vertanimtype=_i32(d, b + 28)))
    return out


def vert_anims(d, flex, anorms=None):
    """One flex's `StudioVertAnim[]` -> [(mesh-local vertex, delta, ndelta)] in Source inches.

    Type 1 (8 B, 17,846 of 17,960 flexes) stores **directions, not deltas**: two u16 byte
    offsets into `anorms` plus two `n/255` magnitudes, scaled by 8.0 (position) and 2.0
    (normal). Type 0 (20 B, only `mingxiao_transformation`) stores the position delta as a
    plain Vector, past the 8-inch ceiling the compressed form can reach. Without `anorms`
    only the vertex indices come back."""
    stride = VA_STRIDE.get(flex["vertanimtype"])
    if stride is None:
        return []
    out = []
    for q in range(flex["numverts"]):
        vb = flex["base"] + flex["vertindex"] + q * stride
        if vb + stride > len(d):
            break
        idx = _u16(d, vb)
        if flex["vertanimtype"] == 1:
            if anorms is None:
                out.append((idx, None, None))
                continue
            dm = d[vb + 6] / 255.0 * DELTA_SCALE
            nm = d[vb + 7] / 255.0 * NDELTA_SCALE
            dv = anorms[_u16(d, vb + 2) // 12]
            nv = anorms[_u16(d, vb + 4) // 12]
            out.append((idx, tuple(c * dm for c in dv), tuple(c * nm for c in nv)))
        else:
            nv = None
            if anorms is not None:
                nm = d[vb + 18] / 255.0 * NDELTA_SCALE
                nv = tuple(c * nm for c in anorms[_u16(d, vb + 16) // 12])
            out.append((idx, _vec3(d, vb + 4), nv))
    return out


# ---------------------------------------------------------------------------------------
# Facts decoded from the model that carry no basis: the unit-vector table, a cinematic's actor
# roots, and the blend-grid / autolayer plan. They live here rather than beside an exporter
# because every consumer needs them and none of them states a coordinate -- a cell is a clip
# name and an animation index, a root is a bone-name prefix, and the table is raw as shipped.
# ---------------------------------------------------------------------------------------

def load_anorms():
    """The unit-vector table a compressed vertex-animation record indexes, or None with a
    warning if the user's `StudioRender.dll` cannot be read. It is compiled into the renderer
    rather than shipped as data, so it is extracted from the install at export time and never
    committed; without it the flexes have directions but no magnitudes, and the export drops
    morph targets rather than baking wrong ones."""
    try:
        return read_anorms()
    except (OSError, RuntimeError, struct.error) as e:
        print(f"  ! no unit-vector table ({e}) - exporting without morph targets")
        return None


def _bone_root(name):
    """A bone's root token if it names one of a cinematic model's actor skeletons.

    `Bip01 Spine1` -> `Bip01`; `Dummy01` -> None. The match is on the leading word only, so
    every bone of one actor's skeleton answers the same root.
    """
    head = name.split()[0] if name.split() else name
    low = head.lower()
    if low.startswith("bip") and low[3:].isdigit():
        return head
    return None


def cinematic_roots(bones):
    """The distinct `BipNN` roots a model carries, in first-seen order."""
    roots, seen = [], set()
    for b in bones:
        r = _bone_root(b.name)
        if r and r.lower() not in seen:
            seen.add(r.lower())
            roots.append(r)
    return roots


def blend_clip_plan(d, clips):
    """Expand a model's sequences into the clips their blend grids need -> (extra, blends).

    A multi-cell sequence selects a different animation per pose-parameter value, and baking
    the base cell alone drops the rest — the male and female `move_and_ranged` `walk` grids
    are nine `walk_0`..`walk_315` animations behind one label. `extra` is one `mdl_skel.Seq`
    per animation an active cell selects that the base-cell bake does not already reach,
    labelled by the animation's own name, so every cell ships as its own clip. `blends` maps a
    sequence label to `{numblends, groupsize, paramindex, paramstart, paramend, cells}`, each
    cell carrying its position, the owner-local animation index it selects, the clip that
    animation baked as, and optional authored movement metadata. The index travels because it is
    the identity a contribution record names, so a cell joins back to the model image it was read
    from.

    **Nothing is blended here.** The grid rides beside the clips because the mix depends on a
    pose parameter the exporter cannot know, and because blending clips is not the same pose
    as blending the transforms they decode to — the host evaluates each cell and mixes the
    results (`docs/vtmb/animation_and_movers.md` A.3).

    A cell whose animation index falls outside the model's declaration resolves to `None`,
    which is a shortfall carried in the sidecar rather than a silently shortened grid."""
    by_base = {}
    taken = set()
    for c in clips:
        by_base.setdefault(c.base, c.label)
        taken.add(c.label.lower())

    extra, blends = [], {}
    for c in clips:
        grid = c.grid
        if len(grid.cells) <= 1:
            continue
        cells = []
        for cell in grid.cells:
            found = local_animation(d, cell.anim)
            if found is None:
                cells.append(
                    {"axis": [cell.axis0, cell.axis1], "anim": cell.anim, "clip": None}
                )
                continue
            name, ab, frames, fps = found
            if ab not in by_base:
                # The animation's own name is the cell's clip name. It collides with a
                # sequence label only where content gave a sequence and an animation the
                # same string, so the index disambiguates and the common case reads
                # `walk_45` rather than a synthetic id.
                base_name = name or f"anim{cell.anim}"
                clip, suffix = base_name, 0
                while clip.lower() in taken:
                    suffix += 1
                    clip = (f"{base_name}#{cell.anim}" if suffix == 1
                            else f"{base_name}#{cell.anim}#{suffix}")
                taken.add(clip.lower())
                by_base[ab] = clip
                extra.append(Seq(label=clip, base=ab, frames=frames, fps=fps,
                                   activity="", actweight=0, flags=0))
            exported = {
                "axis": [cell.axis0, cell.axis1], "anim": cell.anim,
                "clip": by_base[ab],
            }
            motion = movement_summary(d, ab, frames, fps)
            if motion is not None:
                exported["motion"] = {
                    "cycle_seconds": round(motion.cycle_seconds, 6),
                    "ground_distance_cm": round(motion.ground_distance_cm, 6),
                    "ground_speed_cm_s": round(motion.ground_speed_cm_s, 6),
                }
            cells.append(exported)
        blends[c.label] = {
            "numblends": grid.numblends,
            "groupsize": list(grid.groupsize),
            "paramindex": list(grid.paramindex),
            "paramstart": [round(v, 4) for v in grid.paramstart],
            "paramend": [round(v, 4) for v in grid.paramend],
            "cells": cells,
        }
    return extra, blends


def blend_sidecar(d, blends, clips):
    """The blend table and autolayer binding a model ships beside its clips, or `{}` when it
    authors neither.

    The pose parameters travel with it because a grid's `paramindex` is an index into this
    model's own array — the axis cannot be named, wrapped or normalized without it.

    `autolayers` maps a host clip's label to the labels it is composed with, in the order the
    dispatcher walks them, and is read from the same 764-byte sequence descriptor the grids
    are. It is a binding rather than a mix: the host is the base pose and each entry is
    evaluated beside it and accumulated, a masked overlay or an additive according to its own
    flags. Order is part of the data — an overlay blends toward its own pose and would
    overwrite an additive already accumulated onto the bones it owns."""
    autolayers = {c.label: list(c.autolayers) for c in clips if c.autolayers}
    if not blends and not autolayers:
        return {}
    return {
        "pose_parameters": [
            {"index": p.index, "name": p.name, "flags": p.flags,
             "start": round(p.start, 4), "end": round(p.end, 4), "loop": round(p.loop, 4)}
            for p in pose_parameters(d)
        ],
        "grids": blends,
        **({"autolayers": autolayers} if autolayers else {}),
    }


def autolayer_orphans(clips):
    """The autolayer targets none of `clips` baked -> sorted labels.

    A target names a sequence of the declaring model's own array, so it should bake as a clip
    of the same glb. One that does not is a dangling binding the host cannot compose, and the
    sidecar still carries it because the file states it — this is the census that names them."""
    baked = {c.label.lower() for c in clips}
    return sorted({t for c in clips for t in c.autolayers if t.lower() not in baked})


# ---------------------------------------------------------------------------------------
# Magnitudes and Source-space rotation helpers. A radius survives a change of basis unchanged,
# so the measurement belongs beside the decode rather than beside whichever exporter happened
# to need it first.
# ---------------------------------------------------------------------------------------

#: VtMB authors in inches; every exported length is metres.
SCALE = 0.0254


def rot_matrix(q):
    """Source-space 3x3 rotation matrix of a quaternion (x,y,z,w)."""
    x, y, z, w = q
    return np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w),   2*(x*z+y*w)],
        [2*(x*y+z*w),   1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w),   2*(y*z+x*w),   1-2*(x*x+y*y)],
    ])


def rot_matrices(q):
    """`rot_matrix` over an (N,4) array of (x,y,z,w) quaternions -> (N,3,3)."""
    x, y, z, w = q[:, 0], q[:, 1], q[:, 2], q[:, 3]
    return np.stack([
        1-2*(y*y+z*z), 2*(x*y-z*w),   2*(x*z+y*w),
        2*(x*y+z*w),   1-2*(x*x+z*z), 2*(y*z-x*w),
        2*(x*z-y*w),   2*(y*z+x*w),   1-2*(x*x+y*y),
    ], axis=-1).reshape(-1, 3, 3)


def clip_extent(d, bones, animdesc_base, nframes):
    """The furthest any bone reaches from the model origin over one clip, in this glb's
    metres -- the radius a renderer needs to keep the posed model on screen.

    This is the *measured* answer to the question `mdl_skel.Seq.bbmin`/`bbmax` already
    answers from the file. Both exist because a descriptor carrying zeros would otherwise
    hand the runtime a bound smaller than the geometry it has to cover, and this corpus does
    ship zeroed bounds -- every model's header `ViewBBMin`/`ViewBBMax` is (0,0,0).

    Composed in Source space and scaled once at the end: M is a rotation, so a magnitude
    survives the basis change and the per-frame conversion is wasted work here."""
    if not bones or nframes <= 0:
        return 0.0
    frames = read_anim(d, bones, animdesc_base, nframes)
    n = len(bones)
    lt = np.array([[frames[f][i][0] for i in range(n)] for f in range(nframes)], dtype=np.float64)
    lq = np.array([[frames[f][i][1] for i in range(n)] for f in range(nframes)], dtype=np.float64)
    lr = rot_matrices(lq.reshape(-1, 4)).reshape(nframes, n, 3, 3)
    wt, wr = np.empty_like(lt), np.empty_like(lr)
    for i, b in enumerate(bones):
        p = b.parent
        # A root, or a parent declared after its child -- which the composition cannot honour
        # and no v2531 skeleton writes. Either way the bone stands on the model origin.
        if not 0 <= p < i:
            wt[:, i], wr[:, i] = lt[:, i], lr[:, i]
            continue
        wt[:, i] = wt[:, p] + np.einsum("fab,fb->fa", wr[:, p], lt[:, i])
        wr[:, i] = np.einsum("fab,fbc->fac", wr[:, p], lr[:, i])
    return float(np.abs(wt).max()) * SCALE


AXIS_INTERP = 1                    # StudioBone.ProcType; the only rule kind VtMB declares
BONE_STRIDE = 160
BONE_FLAGS, BONE_PROC_TYPE, BONE_PROC_INDEX = 136, 140, 144
AXIS_INTERP_BYTES = 176
#: The three Source axes a rule's six entries are indexed by, in the order the file uses them.
SOURCE_AXES = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


def axis_interp_records(d, bones):
    """Every `ProcType == 1` correction table the model declares, in **Source** space.

    A procedural bone ignores its animation channels: its local transform is recomputed from
    the current orientation of a control bone through a six-entry table authored into the
    model, after the locals blend and the hierarchy composes. That correction is in no clip,
    so it ships as a sidecar. The rule, its evidence and the order it evaluates in are
    `docs/vtmb/procedural_bones.md`.

    `axis` rides as an INDEX into `SOURCE_AXES` rather than as a vector, because a change of
    basis conjugates a bone local: the axis a rule names is not the same axis afterwards and
    may be negated. Carrying the index leaves that decision to whichever exporter states the
    table in a frame -- `UE_mdl_skeletal.axis_interp_rules` is the one that does.

    Returns `(records, faults)`. A rule that does not resolve inside the image, or names a
    control bone or an axis out of range, is a named fault rather than a silent drop.
    """
    base = struct.unpack_from("<i", d, 244)[0]
    records, faults = [], []
    for b in bones:
        record = base + b.index * BONE_STRIDE
        proc_type = struct.unpack_from("<i", d, record + BONE_PROC_TYPE)[0]
        # Either field identifies a driven bone on its own over the shipped corpus, so a
        # disagreement is a model this reader has not seen before rather than a preference.
        if bool(b.flags & 0x1) != bool(proc_type):
            faults.append(f"{b.name}: Flags 0x{b.flags:x} and ProcType {proc_type} disagree")
        if not proc_type:
            continue
        if proc_type != AXIS_INTERP:
            faults.append(f"{b.name}: ProcType {proc_type} is not axis interpolation")
            continue
        offset = record + struct.unpack_from("<i", d, record + BONE_PROC_INDEX)[0]
        if offset < 0 or offset + AXIS_INTERP_BYTES > len(d):
            faults.append(f"{b.name}: ProcIndex resolves to {offset}, outside the image")
            continue
        control, axis = struct.unpack_from("<ii", d, offset)
        if not 0 <= control < len(bones):
            faults.append(f"{b.name}: control bone {control} outside 0..{len(bones) - 1}")
            continue
        if not 0 <= axis <= 2:
            faults.append(f"{b.name}: axis {axis} outside 0..2")
            continue
        records.append({
            "bone": b.name, "bone_index": b.index,
            "control": bones[control].name, "control_index": control,
            "axis_index": axis,
            "pos": [struct.unpack_from("<3f", d, offset + 8 + 12 * i) for i in range(6)],
            "quat": [struct.unpack_from("<4f", d, offset + 80 + 16 * i) for i in range(6)],
        })
    return records, faults


def eye_records(d, bones, mesh_map, matinfo, sanitize):
    """The model's `StudioEyeball` records in **Source** space, joined to the eye meshes.

    Two per character model. The record carries the eye's bone and resting basis, the iris
    scale, and the eyelid flexdescs the renderer's eye pass writes back into the flex weights
    after the rules have run -- so it is the authored bridge between the four eyelid rules and
    the lid morphs, not a reconstruction. `StudioMesh.materialtype == 1` flags which meshes the
    pass applies to and `materialparam` says which eyeball, which is how a material name (and
    therefore the `.vmt`'s `$iris` and `$vampire`) reaches a record.

    The bone rides as a **name**: a model with more than one parent-less bone gets a synthetic
    root appended at assembly, so the emitted skeleton's bone order is not the `.mdl`'s.

    `uppertarget`/`lowertarget` stay verbatim -- they are linear offsets read through
    `asin(t / radius)` against a radius in the same units, so converting either alone breaks
    the ratio. Same for `zoffset` and `radius`. Only `org`, `up` and `forward` are geometry,
    and they are left in Source space for the exporter to state in its own frame.

    `sanitize` names a material the way the artifact naming it does, so this parser owes
    nothing to whichever exporter calls it. Format and the whole system these feed:
    `docs/vtmb/facial_animation.md`.

    Returns `(rig, faults)`; `rig` is None when the model authors no eyeball.
    """
    faults = []
    eye_mat, seen = {}, {}
    for r in mesh_map:
        seen.setdefault(r["material"], set()).add(r.get("materialtype", 0))
        if r.get("materialtype") != 1:
            continue
        param = r.get("materialparam", 0)
        if param not in (0, 1):
            faults.append(f"eye mesh {r['material']}: materialparam {param} outside 0..1")
            continue
        eye_mat.setdefault(param, r["material"])
    for name, types in seen.items():
        if len(types) > 1:
            faults.append(f"{name}: used by both eye and non-eye meshes, so the primitives merge")

    records = []
    for model_base in dict.fromkeys(r["model_base"] for r in mesh_map):
        for e in eyeballs(d, model_base):
            if not 0 <= e["bone"] < len(bones):
                faults.append(f"eyeball {e['index']}: bone {e['bone']} outside 0..{len(bones) - 1}")
                continue
            mat = eye_mat.get(e["index"])
            info = matinfo.get(mat) or {}
            records.append({
                "index": e["index"],
                "bone": bones[e["bone"]].name, "bone_index": e["bone"],
                "org": tuple(e["org"]), "up": tuple(e["up"]), "forward": tuple(e["forward"]),
                "zoffset": float(e["zoffset"]), "radius": float(e["radius"]),
                "iris_scale": float(e["iris_scale"]),
                "upperflexdesc": e["upperflexdesc"], "lowerflexdesc": e["lowerflexdesc"],
                "uppertarget": [round(t, 6) for t in e["uppertarget"]],
                "lowertarget": [round(t, 6) for t in e["lowertarget"]],
                "upperlidflexdesc": e["upperlidflexdesc"],
                "lowerlidflexdesc": e["lowerlidflexdesc"],
                "material": sanitize(mat) if mat else None,
                "iris": ("tex/" + info["iris"]) if info.get("iris") else None,
                "vampire": bool(info.get("vampire")),
            })
            if mat is None:
                faults.append(f"eyeball {e['index']}: no mesh carries materialtype 1 for it")

    if not records:
        return None, faults
    if len(records) != 2:
        faults.append(f"{len(records)} eyeball records; every shipped character carries two")
    return {"eyeballs": records,
            "meshes": [{"material": sanitize(m), "eyeball": p}
                       for p, m in sorted(eye_mat.items())]}, faults
