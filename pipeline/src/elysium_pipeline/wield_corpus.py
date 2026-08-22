"""The wield-model corpus: which model a character holds, and how it is bound to them.

`vdata/items` names a `wieldmodel_f` and a `wieldmodel_m` per definition, and VtMB poses the
resulting entity by copying the wearer's world matrix into every bone whose **name matches**,
leaving the rest to FK off the nearest matched ancestor -- driven by the weapon entity's own
evaluated sequence, not by its bind pose. `docs/vtmb/wielded_weapons.md` owns that behaviour and
`docs/architecture/wielded-weapon-integration.md` owns what it becomes in Unreal.

This module owns the **decisions** that follow from it and performs no manifest I/O: which bone a
model mounts on, which binding mode that implies, which local pose the bake must carry, and whether
the three properties the static collapse rests on actually hold for a given file. The exporter
serialises what it returns; the geometry emitters and the editor bake read the same records rather
than recomputing them.

`shared_corpus` states the same discipline for the static corpus -- decide once over the whole
install, write it down, and let readers join.
"""
from __future__ import annotations

import math
import re
import struct
from typing import NamedTuple

MANIFEST_SCHEMA = "elysium.item-wield-models"
VERSION = 1

REGENERATE = "re-run: uv run elysium export bundle items"

# --- the corpus on disk, under $ELYSIUM_EXPORT_ROOT --------------------------------------------
#: The wield manifest lands beside `ground_models.json`: both are joins over `vdata/items`.
ROOT = "items"
WIELD = "wield"
MANIFEST = "wield_models.json"

# --- the corpus on the /ElysiumBaked mount ------------------------------------------------------
BAKED_ROOT = "/ElysiumBaked/Items/Wield"

#: `vdata/items/*.txt`, one definition per file -- the same enumeration `UE_extract_items` walks.
ITEM_DIR = "vdata/items/"

#: The two model keys, in manifest order. A definition authors both or neither.
SEXES = ("f", "m")

#: The seven bones a character body declares under its hands with zero skin weight, lower-cased.
#: A wield model mounting one of these is posed by the wearer's own animation channel; anything
#: else matches no skeleton and rides the hand by FK. `docs/vtmb/wielded_weapons.md` section 6.
PROP_BONES = frozenset({
    "bat", "bush hook", "handle", "gerber", "sledgehammer", "cylinder01", "tire iron",
})

#: The two hand bones a mount can descend from.
HAND_BONES = frozenset({"bip01 l hand", "bip01 r hand"})

#: A model with no wielded geometry names one of these. They are real, shipped, precached models
#: with zero bones -- an authored value, not a missing file and not a sentinel.
NULL_MODELS = frozenset({"models/w_null.mdl", "models/weapons/w_null.mdl"})

#: Where the character bodies live, for the bone-name join `on_body_scope` and binding resolution
#: perform.
CHARACTER_DIR = "models/character/"

# --- tolerances ---------------------------------------------------------------------------------
#: Inches. Bind positions are authored in Source units and the corpus's real disagreements are
#: whole inches, so this separates a quantisation wobble from a placement difference.
POS_EPS = 1e-3
#: Degrees, against the same reasoning.
ROT_EPS = 0.05


# ---------------------------------------------------------------------------- paths

def wield_dir(export_root):
    """The wield corpus root under an export root. `export_root` is a ``Path`` or a string."""
    from pathlib import Path

    return Path(export_root) / ROOT / WIELD


def manifest_path(export_root):
    from pathlib import Path

    return Path(export_root) / ROOT / MANIFEST


# ------------------------------------------------------------------------ key rules

def normalize(model):
    """A `wieldmodel_*` value as an install key: forward slashes, lower case, `.mdl` present.

    Mirrors `UE_extract_items._normalize`; several definitions omit the extension, which the
    engine resolves as a `.mdl` all the same."""
    key = re.sub(r"/+", "/", str(model or "").strip().replace("\\", "/").lower())
    if not key:
        return ""
    return key if key.endswith(".mdl") else key + ".mdl"


def is_null(model):
    """Whether a normalized key names one of the two authored null models.

    Distinct from an **empty** string, which is a different authored value: 52 definitions name
    no wield model at all, while 296 rows name a null one."""
    return model in NULL_MODELS


def stem(model_path):
    """A wield model's key as its corpus stem: ``w_m_katana``.

    The basename alone rather than the folded whole path `shared_corpus.static_stem` uses -- a
    wield model's basename already carries its sex and family, and it is what the manifest is
    keyed on and what the baked `SM_`/`SK_` asset is named after."""
    from elysium_pipeline.formats import mdl

    key = normalize(model_path)
    return mdl.sanitize(key.rsplit("/", 1)[-1][: -len(".mdl")]) if key else ""


def mesh_asset(name):
    """The ``SM_`` asset one socket model builds under."""
    from elysium_pipeline.asset_names import safe_name

    return "SM_" + safe_name(name)


def skeletal_asset(name):
    """The ``SK_`` asset one skeletal model builds under."""
    from elysium_pipeline.asset_names import safe_name

    return "SK_" + safe_name(name)


def baked_mesh(name):
    return f"{BAKED_ROOT}/{name}/{mesh_asset(name)}"


def baked_skeletal(name):
    return f"{BAKED_ROOT}/{name}/{skeletal_asset(name)}"


# --------------------------------------------------------------------- the item join

class Row(NamedTuple):
    """One `(classname, sex)` row. `model` is ``""`` when the definition authors no wield model."""

    classname: str
    sex: str
    model: str
    anim_prefix: str
    shows_view_model: int
    camera_class: str
    cant_be_last: bool
    discipline_tgt: bool
    reload_single: bool


def _scalar(block, key, default=""):
    """One key's value, resolved **last-wins**.

    `kv.parse` collapses a repeated key into a list where the runtime's `TMap` overwrites, and
    `anim_prefix` is among the keys that repeat."""
    value = block.get(key, default)
    if isinstance(value, list):
        value = value[-1] if value else default
    return value if isinstance(value, str) else default


def _flag(block, key):
    return _scalar(block, key, "0").strip() not in ("", "0")


def item_blocks(idx):
    """``{classname -> WeaponData mapping}`` for every `vdata/items` definition."""
    from elysium_pipeline.formats import install, kv

    out = {}
    for key in sorted(idx):
        if not key.startswith(ITEM_DIR) or not key.endswith(".txt"):
            continue
        raw = install.read(idx, key)
        if raw is None:
            continue
        # `kv.parse` unwraps a single leading root key, so an item file parses straight to its
        # `WeaponData` contents. Accept the wrapped form too rather than depending on that.
        data = kv.parse(raw.decode("ascii", "replace"))
        block = data.get("weapondata")
        if not isinstance(block, dict):
            block = data
        out[key[len(ITEM_DIR):-len(".txt")]] = block
    return out


def wield_rows(idx):
    """Every definition's two `(classname, sex)` rows, in classname then `SEXES` order.

    `shows_view_model` **defaults to 1**: the loader supplies that default in the `GetInt` call
    itself, so the 219 definitions that never name the key inherit an enabled gate."""
    rows = []
    for classname, block in sorted(item_blocks(idx).items()):
        prefix = _scalar(block, "anim_prefix")
        gate = _scalar(block, "shows_view_model", "1").strip() or "1"
        camera = _scalar(block, "camera_class").strip().lower()
        flags = (_flag(block, "bitflag_cantbelast"),
                 _flag(block, "bitflag_discipline_tgt"),
                 _flag(block, "reload_single"))
        for sex in SEXES:
            rows.append(Row(
                classname=classname,
                sex=sex,
                model=normalize(_scalar(block, f"wieldmodel_{sex}")),
                anim_prefix=prefix,
                shows_view_model=1 if gate not in ("0",) else 0,
                camera_class=camera,
                cant_be_last=flags[0],
                discipline_tgt=flags[1],
                reload_single=flags[2],
            ))
    return rows


# ------------------------------------------------------------------- skeleton helpers

def _children(bones):
    kids = {}
    for bone in bones:
        kids.setdefault(bone.parent, []).append(bone.index)
    return kids


def subtree(bones, root):
    """Every bone index at or below `root`."""
    kids, out, stack = _children(bones), set(), [root]
    while stack:
        index = stack.pop()
        out.add(index)
        stack.extend(kids.get(index, ()))
    return out


def _ancestors(bones, index):
    seen, cursor = [], bones[index].parent
    # A parent declared after its child cannot be composed and no v2531 skeleton writes one; the
    # bound also stops a malformed file from looping here rather than hanging.
    while 0 <= cursor < len(bones) and cursor not in seen:
        seen.append(cursor)
        cursor = bones[cursor].parent
    return seen


def quat_angle(a, b):
    """Degrees between two (x,y,z,w) quaternions, sign-insensitive."""
    dot = abs(sum(x * y for x, y in zip(a, b)))
    return math.degrees(2.0 * math.acos(max(-1.0, min(1.0, dot))))


def skinned_bones(d, v, surfaces=None):
    """Every bone index any vertex carries weight on.

    `surfaces`, when given, is a pre-decoded `mdl_skel.decode_skinned(d, v)` result, so one decode
    can serve every geometry consumer of a model -- the exporter decodes each model once and
    passes it here, to `classify` and to `trail_tip` alike.
    """
    from elysium_pipeline.formats import mdl_skel

    if surfaces is None:
        surfaces = mdl_skel.decode_skinned(d, v)
    out = set()
    for surface in surfaces.values():
        for joints, weights in zip(surface["joints"], surface["weights"]):
            out.update(bone for bone, weight in zip(joints, weights) if weight > 0.0)
    return frozenset(out)


# ------------------------------------------------------------------------- the pose

class Pose(NamedTuple):
    """The local pose the bake must carry, and whether it is the bind pose.

    The engine poses an unmatched bone from the weapon entity's **own evaluated sequence**, so the
    faithful local transform is the clip's, not the container's bind. On most of the corpus the two
    agree; where they do not, the clip is what retail renders.
    """

    locals: tuple           # ((pos, quat), ...) indexed by bone
    source: str             # "clip" | "bind"
    label: str              # the clip it came from, "" when there is none
    bind_offset_pos: float  # inches the clip's frame 0 sits from the bind pose
    bind_offset_rot: float  # degrees, same


def bake_pose(d, bones):
    """The local pose to bake, taken from the model's own first clip at frame 0.

    Falls back to the bind pose only when the model declares no usable clip. `Seq.base` is already
    the animation descriptor's byte offset -- it is **not** an index into `LocalAnims`, and reading
    it as one silently yields no frames at all.
    """
    from elysium_pipeline.formats import mdl_skel

    bind = tuple((tuple(bone.pos), tuple(bone.quat)) for bone in bones)
    for seq in mdl_skel.local_sequences(d):
        frames = seq.frames
        if not isinstance(frames, int) or frames <= 0:
            continue
        pose = mdl_skel.read_anim(d, bones, seq.base, frames)
        first = tuple((tuple(pose[0][i][0]), tuple(pose[0][i][1])) for i in range(len(bones)))
        dp = max((max(abs(a - b) for a, b in zip(first[i][0], bind[i][0]))
                  for i in range(len(bones))), default=0.0)
        dr = max((quat_angle(first[i][1], bind[i][1]) for i in range(len(bones))), default=0.0)
        return Pose(locals=first, source="clip", label=seq.label,
                    bind_offset_pos=dp, bind_offset_rot=dr)
    return Pose(locals=bind, source="bind", label="", bind_offset_pos=0.0, bind_offset_rot=0.0)


# -------------------------------------------------------------------- classification

class Classification(NamedTuple):
    """One model's binding decision. Field names are the manifest schema."""

    binding: str | None     # None when the non-socket case needs a body join to resolve
    mount_bone: str
    hand_bone: str
    collapse_bone: str
    grip: str               # "left" | "right" | ""
    mount_bind: tuple       # (pos, quat) parent-relative, () when there is no mount
    bone_count: int
    skinned_bone_count: int
    anomalies: tuple

#: Every binding mode, in the order `docs/architecture/wielded-weapon-integration.md` lists them.
BINDINGS = ("socket_prop", "socket_hand", "leader_pose", "copy_pose", "projectile")


def _skinned_roots(bones, skinned):
    """The topmost skinned bones inside each hand's subtree.

    Taken as "no skinned proper ancestor below the hand" rather than "direct child of the hand":
    several models park unskinned authoring leftovers beside the real mount, and the general form
    still finds a mount that sits one unskinned bone further down."""
    lower = [bone.name.lower() for bone in bones]
    hands = {i for i, name in enumerate(lower) if name in HAND_BONES}
    roots = []
    for hand in hands:
        for index in subtree(bones, hand):
            if index == hand or index not in skinned:
                continue
            chain = _ancestors(bones, index)
            between = chain[: chain.index(hand)] if hand in chain else chain
            if not any(other in skinned for other in between):
                roots.append(index)
    return sorted(set(roots)), hands


def classify(d, v, bodies=None, surfaces=None):
    """One decoded model to its binding decision.

    `bodies` is a `body_index` mapping; supply it to resolve the non-socket modes, which cannot be
    told apart from the model alone -- whether a rig is worn at all is a fact about the character
    corpus, not about the file. `surfaces` is `skinned_bones`' optional pre-decoded geometry.
    """
    from elysium_pipeline.formats import mdl_skel

    bones = mdl_skel.read_bones(d)
    # Whether the model drives its own rig separates `copy_pose` from `leader_pose`, and it is a
    # per-frame question -- a clip's frame count says nothing, and most of the corpus ships a
    # 60-75 frame sequence that never leaves its first frame.
    animated = False
    for seq in mdl_skel.local_sequences(d):
        frames = seq.frames
        if not isinstance(frames, int) or frames <= 0:
            continue
        pose = mdl_skel.read_anim(d, bones, seq.base, frames)
        if frame_variance(bones, pose, range(len(bones))):
            animated = True
            break
    return classify_bones(bones, skinned_bones(d, v, surfaces=surfaces),
                          bodies=bodies, animated=animated)


def classify_bones(bones, skinned, *, bodies=None, animated=False):
    """The pure half of `classify`, over an already-decoded skeleton.

    Separated so the decision can be exercised without synthesising a container: the byte reading
    belongs to `mdl_skel`, and this is the part that is ours.
    """
    lower = [bone.name.lower() for bone in bones]
    roots, hands = _skinned_roots(bones, skinned)
    anomalies = []

    if len(roots) == 1:
        mount = roots[0]
        chain = _ancestors(bones, mount)
        hand = next((i for i in chain if i in hands), -1)
        prop = lower[mount] in PROP_BONES
        collapse = mount if prop else hand
        bind = (tuple(bones[mount].pos), tuple(bones[mount].quat))
        # A literal identity local bind is a retail authoring defect, not a placement: it is
        # reproduced rather than corrected, so it is recorded to keep the row from reading ordinary.
        if (max(abs(value) for value in bind[0]) < POS_EPS
                and quat_angle(bind[1], (0.0, 0.0, 0.0, 1.0)) < ROT_EPS):
            anomalies.append("degenerate_bind")
        return Classification(
            binding="socket_prop" if prop else "socket_hand",
            mount_bone=bones[mount].name,
            hand_bone=bones[hand].name if hand >= 0 else "",
            collapse_bone=bones[collapse].name,
            grip="left" if hand >= 0 and lower[hand].startswith("bip01 l") else
                 ("right" if hand >= 0 else ""),
            mount_bind=bind,
            bone_count=len(bones),
            skinned_bone_count=len(skinned),
            anomalies=tuple(anomalies),
        )

    anomalies.append("no_single_skinned_root")
    binding = None
    if bodies is not None:
        need = {lower[i] for i in skinned}
        wearable = sum(1 for names in bodies.values() if need <= set(names))
        # Nothing can wear it, so it is never follow-attached in a meaningful frame -- the two
        # thrown items reach the corpus this way.
        binding = "projectile" if not wearable else ("copy_pose" if animated else "leader_pose")
    return Classification(
        binding=binding,
        mount_bone="",
        hand_bone="",
        collapse_bone="",
        grip="",
        mount_bind=(),
        bone_count=len(bones),
        skinned_bone_count=len(skinned),
        anomalies=tuple(anomalies),
    )


# ------------------------------------------------------------------------ the trail tip

def trail_tip_from_geometry(bones, cls, positions):
    """A synthetic `TrailTip` attachment for a `socket_prop` model: `(pos, quat)` bone-local to
    `cls.mount_bone`, Source inches, identity quat -- the model's own geometry's farthest point
    from the mount along its own long axis, the other two axes at the geometry's midpoint.

    Meaningful only when `cls.binding == "socket_prop"`. `positions` is the model's decoded
    vertex list in the same bind-space object frame `bind_world_transforms` composes (Source
    inches, e.g. every `pos` from `mdl_skel.decode_skinned`'s surfaces). The long axis is picked
    per model from its own geometry -- never assumed -- because it is not a fixed convention
    across the corpus: `w_m_bushhook`'s is local-Y, `w_f_bushhook`'s is local-Z. The mount's own
    bind need not be well-formed for this to work (`w_f_bushhook`'s is a degenerate identity) --
    only the geometry's bbox is read, not the bind rotation/translation magnitude.
    """
    import numpy as np

    mount = next(b.index for b in bones if b.name == cls.mount_bone)
    inv_mount = np.linalg.inv(bind_world_transforms(bones)[mount])
    if not positions:
        return (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)
    local = np.array([(inv_mount @ np.array([p[0], p[1], p[2], 1.0]))[:3] for p in positions])
    mins, maxs = local.min(axis=0), local.max(axis=0)
    axis = int(np.argmax(maxs - mins))
    point = [(mins[i] + maxs[i]) / 2.0 for i in range(3)]
    point[axis] = maxs[axis] if abs(maxs[axis]) >= abs(mins[axis]) else mins[axis]
    return tuple(float(c) for c in point), (0.0, 0.0, 0.0, 1.0)


def trail_tip(d, v, bones, cls, surfaces=None):
    """Decode-and-call wrapper over `trail_tip_from_geometry`, mirroring the `classify`/
    `classify_bones` split: the byte reading belongs to `mdl_skel`, this is the part that composes
    the decision. Returns `None` for any binding other than `socket_prop`. `surfaces` is
    `skinned_bones`' optional pre-decoded geometry, so the exporter's one decode serves this too.
    """
    from elysium_pipeline.formats import mdl_skel

    if cls.binding != "socket_prop":
        return None
    if surfaces is None:
        surfaces = mdl_skel.decode_skinned(d, v)
    positions = [p for surface in surfaces.values() for p in surface["pos"]]
    return trail_tip_from_geometry(bones, cls, positions)


# ----------------------------------------------------------------------- the checks

class Check(NamedTuple):
    """One assertion's structured result. `detail` names what failed, never just that it did."""

    name: str
    ok: bool
    detail: tuple


def check_subtree(bones, skinned, cls):
    """Every skinned bone lies inside the collapse subtree.

    A mesh blending weight across two independently matched bones deforms between them and cannot
    be placed by one socket."""
    if not cls.collapse_bone:
        return Check("subtree", True, ())
    root = next(b.index for b in bones if b.name == cls.collapse_bone)
    allowed = subtree(bones, root)
    outside = tuple(sorted(bones[i].name for i in skinned if i not in allowed))
    return Check("subtree", not outside, outside)


def bind_world_transforms(bones):
    """[4x4 bind-space world matrix per bone index], composed from parent-relative locals.

    Shared by `check_collapse` (which asserts the stored inverse bind agrees with this
    composition) and `trail_tip_from_geometry` (which uses it to bring mesh geometry into a
    mount bone's local frame).
    """
    import numpy as np
    from elysium_pipeline.formats import mdl_skel

    world = [None] * len(bones)
    for bone in bones:
        local = np.eye(4)
        local[:3, :3] = mdl_skel.rot_matrix(bone.quat)
        local[:3, 3] = bone.pos
        parent = bone.parent
        world[bone.index] = (world[parent] @ local
                             if 0 <= parent < bone.index and world[parent] is not None
                             else local)
    return world


def check_collapse(bones, skinned, cls):
    """The stored inverse bind agrees with the one composed from the bind locals.

    This is what licenses treating a whole sub-rig as one rigid transform: the cancellation is
    algebraic *given* a self-consistent file, so the thing worth asserting is the consistency.
    """
    import numpy as np

    worst, name = 0.0, ""
    world = bind_world_transforms(bones)
    for index in sorted(skinned):
        stored = np.array(bones[index].pose_to_bone, dtype=np.float64).reshape(3, 4)
        composed = np.linalg.inv(world[index])[:3, :4]
        delta = float(np.abs(stored - composed).max())
        if delta > worst:
            worst, name = delta, bones[index].name
    # Inches on the translation column and a unitless rotation term; one tolerance covers both
    # because a rigid 3x4's rotation entries are bounded by 1.
    return Check("collapse", worst <= 1e-2, () if worst <= 1e-2 else ((name, round(worst, 5)),))


def check_motion(d, bones, cls):
    """The model's own clips hold still on every bone below the collapse root.

    Measured **across frames**, not against the bind pose: a clip that sits at a constant offset
    from bind is still a rigid weapon and is handled by baking the clip's pose (`bake_pose`). Only
    frame-to-frame variation would make a socket wrong.
    """
    from elysium_pipeline.formats import mdl_skel

    if not cls.collapse_bone:
        return Check("motion", True, ())
    root = next(b.index for b in bones if b.name == cls.collapse_bone)
    live = sorted(subtree(bones, root) - {root}) or [root]
    worst = []
    for seq in mdl_skel.local_sequences(d):
        frames = seq.frames
        if not isinstance(frames, int) or frames <= 0:
            continue
        pose = mdl_skel.read_anim(d, bones, seq.base, frames)
        worst.extend((seq.label, name, dp, dr)
                     for name, dp, dr in frame_variance(bones, pose, live))
    return Check("motion", not worst, tuple(worst))


def frame_variance(bones, pose, live):
    """`live` bones that move across an already-read frame list, measured against frame 0.

    The pure half of `check_motion`, so the tolerance rule can be exercised without synthesising an
    animation section."""
    out = []
    frames = len(pose)
    for index in live:
        first_p, first_q = pose[0][index][0], pose[0][index][1]
        dp = max(max(abs(pose[f][index][0][k] - first_p[k]) for k in range(3))
                 for f in range(frames))
        dr = max(quat_angle(pose[f][index][1], first_q) for f in range(frames))
        if dp > POS_EPS or dr > ROT_EPS:
            out.append((bones[index].name, round(dp, 4), round(dr, 3)))
    return out


# -------------------------------------------------------------------- the body join

def body_index(idx):
    """``{character model key -> {bone name lower: parent name lower}}`` over the whole cast.

    Built once and passed to `on_body_scope` and `classify`: both ask questions of the same 485
    skeletons, and re-reading them per wield model would decode the cast 67 times.
    """
    from elysium_pipeline.formats import install
    from elysium_pipeline.formats import mdl_skel

    out = {}
    for key in sorted(idx):
        if not key.startswith(CHARACTER_DIR) or not key.endswith(".mdl"):
            continue
        raw = install.read(idx, key)
        if not raw:
            continue
        try:
            bones = mdl_skel.read_bones(raw)
        except Exception as exc:                       # noqa: BLE001 - reported, never hidden
            print(f"[wield] {key}: bone table unreadable ({type(exc).__name__}: {exc})")
            continue
        if not bones:
            continue
        out[key] = {bone.name.lower():
                    (bones[bone.parent].name.lower() if 0 <= bone.parent < len(bones) else "")
                    for bone in bones}
    return out


def on_body_scope(idx, mount_name, bodies=None):
    """Which character skeletons declare `mount_name`, **and under which parent**.

    A scope rather than a boolean: `Box01` and `Box02` occur on four NPC bodies but hang from
    `Bip01 Pelvis` and `Bip01 R Finger1`, so a yes/no answer would route four item families to a
    hip socket.
    """
    bodies = body_index(idx) if bodies is None else bodies
    key = (mount_name or "").lower()
    parents = {}
    for names in bodies.values():
        if key in names:
            parents[names[key]] = parents.get(names[key], 0) + 1
    return {"bodies": sum(parents.values()),
            "parents": dict(sorted(parents.items())),
            "under_hand": all(parent in HAND_BONES for parent in parents) if parents else False}


# -------------------------------------------------------------------------- materials

#: The five VMT-derived facts a wield material's bake needs to know. `MaterialRow.flags` names
#: only the ones present rather than carrying every field as a bool.
MATERIAL_FLAG_FIELDS = ("alphatest", "translucent", "additive", "envmap", "selfillum")


class MaterialRow(NamedTuple):
    """One material's resolved render data, in the model's own header material order.

    `albedo` is the resolved texture's install-relative key, folded the way
    `mdl.material_channels` folds it -- or ``""`` when the material's `.vmt` never resolved a
    drawable texture, or its albedo bytes fail to decode. `failure` names which of those
    happened; it is ``""`` both on an ordinary success and on an ordinary VMT that draws no
    basetexture at all. The corpus's one real decode failure is `handleclaws`'s ``null``
    material, whose `.ttz` is a 20-byte empty file -- a retail defect, reproduced rather than
    repaired.

    `envmask` and `bump` are the resolved `$envmapmask`/`$bumpmap` keys, straight off
    `mdl.material_channels` -- or ``""`` when the VMT names neither, which is the normal case for
    most of the corpus. Unlike `albedo` they are **not** decode-validated: `failure` stays scoped
    to the one channel a bake actually needs decoded to prove out a texture budget, and a mask or
    bump texture's own byte-level validity is the texture export's concern to verify, not this
    function's.
    """

    name: str
    albedo: str
    flags: frozenset
    failure: str
    envmask: str
    bump: str


def _resolve_material_row(name, search, read_bytes):
    """`name`'s albedo/envmask/bump keys, render flags and any resolve/decode failure.

    Applies the semantics `mdl._resolve_material` applies -- `mdl.material_channels` over
    `mdl.resolve_vmt`'s header search-path walk -- without its PNG side effect: nothing is
    written to disk, and only the albedo texture is decoded, once, in memory, purely to prove
    that it decodes. Shared by `model_materials` and `skin_families`, which both resolve a
    material name through the same model header and need the same answer.

    `resolve_vmt` is consulted on its own first because `material_channels` answers ``None`` for
    two different situations that must not be conflated: no `.vmt` resolved at all (a failure),
    and a `.vmt` that resolved but draws no basetexture, refraction layer or water (routine --
    the Eyes/decal materials this corpus's non-drawing VMTs actually are). Returns
    ``(albedo, flags, failure, envmask, bump)``.
    """
    from elysium_pipeline.formats import mdl
    from elysium_pipeline.formats.tex_to_png import decode as decode_texture

    _vmt_path, info = mdl.resolve_vmt(name, search, read_bytes)
    if info is None:
        return "", frozenset(), f"vmt did not resolve for material {name!r}", "", ""

    channels = mdl.material_channels(name, search, read_bytes)
    if channels is None:
        return "", frozenset(), "", "", ""

    flags = frozenset(field for field in MATERIAL_FLAG_FIELDS if channels.get(field))
    envmask, bump = channels["envmask"], channels["bump"]
    albedo = channels["albedo"]
    if not albedo:
        return "", flags, "", envmask, bump

    tth, ttz = read_bytes(f"materials/{albedo}.tth"), read_bytes(f"materials/{albedo}.ttz")
    if not (tth and ttz):
        return "", flags, f"texture files missing for {albedo!r}", envmask, bump
    try:
        decode_texture(tth, ttz)
    except Exception as exc:                        # noqa: BLE001 - reported, never hidden
        return "", flags, f"{type(exc).__name__}: {exc}", envmask, bump
    return albedo, flags, "", envmask, bump


def model_materials(d, v, idx):
    """Per-material render data for one loaded model, in header material order.

    Mirrors what `UE_mdl_skeletal.write_model` resolves through `mdl._resolve_material` for the
    same materials, without decoding or writing a texture PNG to disk -- this only proves each
    material's name resolves and its albedo bytes decode, which is what a bake plan needs to know
    before it commits to a texture budget.
    """
    from elysium_pipeline.formats import install, mdl, mdl_skel

    search = mdl.search_paths(d)
    read_bytes = lambda key: install.read(idx, key)
    surfaces = mdl_skel.decode_skinned(d, v)
    return [MaterialRow(name, *_resolve_material_row(name, search, read_bytes))
            for name in surfaces]


# ---------------------------------------------------------------------- skin families

class SkinOverride(NamedTuple):
    """One extra skin family's material-slot override, resolved through the same VMT path as the
    authored set.

    `slot` is family 0's material name for the skinref that changes; `material` is what family
    `family` repaints it to. Most of the corpus authors a single family and contributes no rows;
    the one real case is `w_{m,f}_fire_axe.mdl` family 1, whose slot repaints to `Transparent`
    (`materials/models/weapons/Fire_Axe/Transparent.vmt`, translucent, basetexture under
    `models/character/npc/unique/santa_monica/ghost/`).

    `albedo`, `flags`, `failure`, `envmask` and `bump` carry `MaterialRow`'s same fields for the
    override material, resolved the same way -- see `MaterialRow` for what each means.
    """

    family: int
    slot: str
    material: str
    albedo: str
    flags: frozenset
    failure: str
    envmask: str
    bump: str


def skin_families(d, idx):
    """This model's skin families beyond family 0, as resolved material-slot overrides.

    `mdl.skin_families(d)` reads the raw table resolved through the texture list; index 0 is the
    authored set every other family is diffed against. A model with no alternate family -- the
    normal case -- returns ``[]``.
    """
    from elysium_pipeline.formats import install, mdl

    search = mdl.search_paths(d)
    read_bytes = lambda key: install.read(idx, key)
    families = mdl.skin_families(d)
    if len(families) < 2:
        return []
    base = families[0]
    out = []
    for family in range(1, len(families)):
        row = families[family]
        for slot in range(min(len(base), len(row))):
            if row[slot] == base[slot]:
                continue
            albedo, flags, failure, envmask, bump = _resolve_material_row(
                row[slot], search, read_bytes)
            out.append(SkinOverride(family=family, slot=base[slot], material=row[slot],
                                    albedo=albedo, flags=flags, failure=failure,
                                    envmask=envmask, bump=bump))
    return out


# ------------------------------------------------------------------------ bone motion

class BoneMotion(NamedTuple):
    """One bone's motion envelope across every local sequence, measured from that sequence's own
    frame 0.

    `skinned` is carried alongside the numbers rather than folded into them: an unskinned mover
    moves no vertex, so a bake that only asked "does it move" would treat
    `w_m_flamethrower`'s unskinned `trigger` (0.3203in / 1.2535deg) the same as a mount whose
    motion actually deforms geometry.
    """

    name: str
    max_pos: float   # inches, the largest per-axis delta any sequence reaches from its frame 0
    max_rot: float   # degrees, same
    skinned: bool


def bone_motion(d, v, bones):
    """Every bone's motion envelope across the model's own local sequences.

    The same per-frame delta `frame_variance` measures, kept for every bone at full precision
    rather than filtered to the ones a socket check would flag -- this answers "how much", not
    "too much". `Seq.base` is an animdesc byte offset, not a `LocalAnims` index; passing it
    straight to `read_anim` is what `bake_pose` and `check_motion` already do.
    """
    from elysium_pipeline.formats import mdl_skel

    skinned = skinned_bones(d, v)
    max_pos = [0.0] * len(bones)
    max_rot = [0.0] * len(bones)
    for seq in mdl_skel.local_sequences(d):
        frames = seq.frames
        if not isinstance(frames, int) or frames <= 0:
            continue
        pose = mdl_skel.read_anim(d, bones, seq.base, frames)
        first = pose[0]
        for i in range(len(bones)):
            first_pos, first_quat = first[i]
            dp = max(max(abs(pose[f][i][0][axis] - first_pos[axis]) for axis in range(3))
                     for f in range(len(pose)))
            dr = max(quat_angle(pose[f][i][1], first_quat) for f in range(len(pose)))
            max_pos[i] = max(max_pos[i], dp)
            max_rot[i] = max(max_rot[i], dr)
    return [BoneMotion(name=bones[i].name, max_pos=round(max_pos[i], 4),
                       max_rot=round(max_rot[i], 4), skinned=i in skinned)
            for i in range(len(bones))]


# --------------------------------------------------------------------- NPC reachability

def npc_carried(idx):
    """Item classnames any placed `npc_*` names through `additionalequipment`/`alternateequipment`.

    Player obtainability is **not** derivable here -- it rests on secondary walkthrough evidence
    that `docs/vtmb/wielded_weapons.md` carries -- so only the measured half is reported.
    """
    from elysium_pipeline.exporters.UE_bsp_to_scene import _parse_ent_blocks
    from elysium_pipeline.formats import bsp, install

    out = set()
    for name in sorted(install.all_map_names()):
        try:
            with open(install.map_path(name), "rb") as handle:
                data = handle.read()
            text = bsp.read_lump(data, 0).decode("ascii", "replace")
        except (OSError, struct.error) as exc:
            print(f"[wield] {name}: entity lump unreadable ({type(exc).__name__}: {exc})")
            continue
        for pairs in _parse_ent_blocks(text):
            keys = {key.casefold(): value for key, value in pairs}
            if not keys.get("classname", "").startswith("npc_"):
                continue
            for field in ("additionalequipment", "alternateequipment"):
                value = keys.get(field, "").strip()
                if value:
                    out.add(value.lower())
    return out


