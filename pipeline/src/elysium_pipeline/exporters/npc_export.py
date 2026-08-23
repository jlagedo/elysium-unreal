"""Batch character export: the cast's resolution manifest, sidecars, and texture decode.

Two seeds, one product. The maps' own `npc_*` model references cover every NPC; the **player
bodies** are named by no entity at all, so they come from `vdata/system/clandoc000.txt`
instead (56 distinct models over its 84 `M_Body0..5`/`F_Body0..5` slots, roadmap PL13). Both
are the same v2531 skeletal format, so both go through the same decode: each model's
include-model tree (`docs/vtmb/animation_and_movers.md` A.7) resolves into shared banks, and the
run writes under `$ELYSIUM_EXPORT_ROOT/npc/`:

  npc_manifest.json  per-NPC {clip -> owning-stem} resolution + a bank/mesh index
  tex/               every character texture, decoded once for the texture corpus
  facial/<npc>.json  the flex rig above the morphs: controllers, rules, ramps (PL10)

The shipped container is the `.eskm` the character bake consumes; the game plays only baked
assets off the mount. This export decodes each model once to state the manifest facts -- which
clips actually bake, their activity keys, bounds, morphs, procedural rules -- and writes no
model container of its own. A `.glb` inspection twin exists only on demand
(`uv run elysium export model <mdl>`). Bank sharing is VtMB's own virtualmodel shape: one
skeleton, many meshes, a shared animation library keyed by bone name -- not a per-NPC
monolith.

The manifest carries each clip's engine-facing selection keys -- the `ACT_*` activity
literal, its weighted-random `weight`, `flags`, `frames` and `fps` -- stored once on the stem
that OWNS the clip rather than on every NPC that resolves it. That is what lets a consumer
ask for an `ACT_IDLE` (or the `ACT_DISPOSITION` stance a `default_disposition` names) instead
of pattern-matching a label: `regular_cop` resolves 229 clips with "idle" in the name, and
`Stance_Dead_Idle_1` is not one of the useful ones.

Manifest v3 adds the **face** (roadmap PL10). Each rigged NPC's `.mdl` flexes bake into
morph targets of its skeletal mesh, and the three layers that drive them -- 44 flex controllers,
60 RPN flex rules, and each flex's four-value target ramp -- ride beside it in
`facial/<stem>.json`, because none of the three is a vertex displacement a morph can hold.
Baking needs the unit-vector table out of the user's own `Bin/StudioRender.dll`
(`mdl_skel.read_anorms`, the same one `probe_facial.py --anorms` dumps); it is game-derived,
so it is read at export time and never committed.

Manifest v5 adds the **eyes**: the pair of `StudioEyeball` records every character model
carries, in `eyes/<stem>.json`. It is a separate sidecar from the flex rig rather than a
field on it, because 57 of the 59 player bodies carry eyeballs and no flex data at all.
Format for both: `docs/vtmb/facial_animation.md`.

The public tooling CLI exposes full and targeted exports.  Targeted runtime
integration merges into the existing manifest; it never replaces the complete
index with a one-model partial index.
"""
import glob
import json
import os
import re

from elysium_pipeline.formats import install, kv, mdl, mdl_gltf
from elysium_pipeline.formats.bsp import INCH_TO_CM
from elysium_pipeline.exporters import UE_mdl_cloth, UE_mdl_skeletal as UEK
from elysium_pipeline.formats import mdl_skel as S
from elysium_pipeline.paths import export_root
from elysium_pipeline import placed_models as PM
from elysium_pipeline.exporters.source_warnings import (
    animated_prop_warning,
    missing_intrinsic_prop_clips_warning,
    missing_npc_warning,
)

OUT = os.fspath(export_root())
NPC_DIR = os.path.join(OUT, "npc")
CLANDOC = os.path.join("vdata", "system", "clandoc000.txt")
_BODY_SLOT = re.compile(r"^([mf])_body(\d+)$")
MANIFEST = os.path.join(NPC_DIR, "npc_manifest.json")
INDEX = os.path.join(NPC_DIR, "npc_index.json")
CLIPS_DIR = os.path.join(NPC_DIR, "clips")
FACIAL_DIR = os.path.join(NPC_DIR, "facial")
PROCEDURAL_DIR = os.path.join(NPC_DIR, "procedural")
BLENDS_DIR = os.path.join(NPC_DIR, "blends")
GARMENT_DIR = os.path.join(NPC_DIR, "garment")
ANIMATED_PROP_DIR = os.path.join(NPC_DIR, "animated_props")
PLACED_MODEL_DIR = os.path.join(NPC_DIR, "placed_models")
MANIFEST_VERSION = 8


def npc_models_from_ents(out_root=OUT):
    """Every distinct `npc_*` `model` key across the exported maps' `.ents`, normalized to a
    `models/`-rooted, forward-slashed, lowercased path."""
    models = set()
    for ents in glob.glob(os.path.join(out_root, "*", "*.ents")):
        try:
            data = json.load(open(ents, encoding="utf-8"))
        except Exception:
            continue
        for ent in data.get("entities", []):
            if not ent.get("classname", "").startswith("npc_"):
                continue
            m = ent.get("keys", {}).get("model", "").strip().lower().replace("\\", "/")
            if m.endswith(".mdl"):
                models.add(m if m.startswith("models/") else "models/" + m)
    return sorted(models)


def _meaningful_sequence(value):
    return isinstance(value, str) and value.strip().lower() not in ("", "none", "null", "0")


def has_animation(d):
    """True when the model declares any sequence pose.

    Frame count decides whether a sequence moves over time, not whether its frame zero is an
    authored pose. Retail still evaluates a single-frame sequence through `CBaseAnimating`, so
    excluding it would display the storage bind instead of the selected held pose.
    """
    try:
        return bool(S.local_sequences(d))
    except Exception:
        return False


def animated_prop_models_from_ents(out_root=OUT):
    """Skeletal prop models that gameplay can animate.

    A prop qualifies when it authors a meaningful default/loop sequence or when any exported
    output targets it with SetAnimation. Target matching reproduces the shipped trailing-* prefix
    rule; engine aliases and other dynamic targets cannot identify a model offline and are skipped.
    """
    docs = []
    for ents in glob.glob(os.path.join(out_root, "*", "*.ents")):
        try:
            docs.append(json.load(open(ents, encoding="utf-8")))
        except Exception:
            continue

    animation_targets = set()
    for data in docs:
        for ent in data.get("entities", []):
            for wire in ent.get("outputs", []):
                if str(wire.get("input", "")).lower() == "setanimation":
                    target = str(wire.get("target", "")).strip().lower()
                    if target and not target.startswith("!"):
                        animation_targets.add(target)

    def targeted(name):
        name = (name or "").lower()
        return any(name.startswith(t[:-1]) if t.endswith("*") else name == t
                   for t in animation_targets)

    models = set()
    for data in docs:
        for ent in data.get("entities", []):
            if not str(ent.get("classname", "")).startswith("prop_dynamic"):
                continue
            keys = ent.get("keys", {})
            animated = (_meaningful_sequence(keys.get("demo_sequence"))
                        or _meaningful_sequence(keys.get("LoopSequence"))
                        or targeted(ent.get("targetname", "")))
            model = str(keys.get("model", "")).strip().lower().replace("\\", "/")
            if animated and model.endswith(".mdl"):
                models.add(model if model.startswith("models/") else "models/" + model)
    return sorted(models)


def static_model_fallbacks_from_ents(out_root=OUT):
    """Models whose per-map entity record already names decoded static geometry."""

    models = set()
    for ents in glob.glob(os.path.join(out_root, "*", "*.ents")):
        try:
            data = json.load(open(ents, encoding="utf-8"))
        except Exception:
            continue
        for ent in data.get("entities", []):
            if not ent.get("model_mesh"):
                continue
            model = str(ent.get("keys", {}).get("model", "")).strip().lower()
            model = model.replace("\\", "/")
            if model.endswith(".mdl"):
                models.add(model if model.startswith("models/") else "models/" + model)
    return models


def cinematic_models_from_ents(out_root=OUT):
    """Every distinct anim-set `.mdl` the exported maps' `logic_choreographed_scene` entities
    name (`BaseAnim` / `MaleAnim` / `FemaleAnim`).

    Neither of the other two seeds reaches these: no `npc_*` entity carries a cinematic model and
    the rulebook never names one, so the whole `models/cinematic/**` tree (106 models in the
    merged install) stayed in the VPKs. They are the whole-cast performances a scene's
    `sequence "entire_scene"` plays -- see `docs/vtmb/choreographed_scenes.md`.
    """
    models = set()
    for ents in glob.glob(os.path.join(out_root, "*", "*.ents")):
        try:
            data = json.load(open(ents, encoding="utf-8"))
        except Exception:
            continue
        for ent in data.get("entities", []):
            if ent.get("classname") != "logic_choreographed_scene":
                continue
            for key in ("BaseAnim", "MaleAnim", "FemaleAnim"):
                m = ent.get("keys", {}).get(key, "").strip().lower().replace("\\", "/")
                if m.endswith(".mdl"):
                    models.add(m if m.startswith("models/") else "models/" + m)
    return sorted(models)


def pc_models_from_clandoc(out_root=OUT):
    """Every distinct player-body `.mdl` `vdata/system/clandoc000.txt` names (roadmap PL13).

    No entity in any map carries a player model, so the `.ents` seed above cannot reach the
    PC bodies. The rulebook is the seed instead -- the same table 8.11a selects a body
    through, so the exported set cannot drift from it: each playable `ClanData.General`
    carries `M_Body0..5`/`F_Body0..5`, with the top two slots repeating the tier-3 suit.
    The base clan blocks contribute 84 references; multiplayer and unused templates raise
    the raw indexed-key count to 216 without adding paths, so the union is **56 distinct
    models**. Un-indexed `M_Body`/`F_Body` keys name NPC models and are not read.

    Requires PL5b's `$ELYSIUM_EXPORT_ROOT/vdata/` mirror; returns `[]` (with a
    note) when it is absent."""
    path = os.path.join(out_root, CLANDOC)
    if not os.path.exists(path):
        print(f"  ! [npc] {path} not found - PC bodies skipped (run the vdata export first)")
        return []
    with open(path, encoding="utf-8", errors="replace") as f:
        doc = kv.parse(f.read())
    blocks = doc.get("clandata", [])
    if isinstance(blocks, dict):
        blocks = [blocks]
    models = set()
    for block in blocks:
        for key, val in block.get("general", {}).items():
            if _BODY_SLOT.match(key) and isinstance(val, str) and val.lower().endswith(".mdl"):
                m = val.strip().lower().replace("\\", "/")
                models.add(m if m.startswith("models/") else "models/" + m)
    return sorted(models)


def bank_stem(model_key):
    """Path-safe stem for a shared bank -- keeps the sub-path so male/female (or clan) banks
    that share a basename (`shared/male/misc` vs `shared/female/misc`) stay distinct."""
    key = model_key.lower()
    if key.startswith("models/"):
        key = key[len("models/"):]
    if key.endswith(".mdl"):
        key = key[:-4]
    return mdl.sanitize(key)


def _basename_stem(model_key):
    return mdl.sanitize(os.path.basename(model_key)[:-4] if model_key.lower().endswith(".mdl")
                        else os.path.basename(model_key))


def _clip_meta(c, bounds_radius_m=None):
    """One baked clip's engine-facing selection keys (`docs/vtmb/animation_and_movers.md` A.3).

    `activity` is the `ACT_*` literal the engine selects on (empty on a layer/plumbing
    sequence), `weight` its weighted-random share among the clips sharing that activity, and
    `flags` the studio sequence bits, and `fade` the authored transition duration in seconds
    that the engine combines across a sequence pair to time a base-sequence crossfade. Stored
    once per owning stem, not per NPC that resolves it -- 157 characters x ~1,400 resolved
    clips would be two orders of magnitude more rows.

    `reach_cm` and `blocked_reaction` are the melee pair the same descriptor carries: the
    swing's own target-acquisition distance, converted from the file's Source units to the
    centimetres every sidecar is stated in, and the `ACT_*` literal the attacker plays when that
    swing is blocked. Both are present only on the sequences that state them -- 581 and 147
    descriptors respectively out of the install's 14,012 -- because a column carried as a null on
    every ordinary clip would cost more than the fact is worth.

    `swings` is the contact half of the same block: the authored segments the swing sweeps, the
    slice of the clip cycle each one is live for, and the knockback candidates it answers with
    (`mdl_skel.read_swing_records`, stated in Unreal centimetres by `UEK.unreal_swings`). It rides
    on the same terms -- 574 descriptors state one, so the key is absent everywhere else.

    `combo` is its chain half (`mdl_skel.read_combo_chain`), on the same terms again at 208
    descriptors: the button-state mask direction-keyed attack selection matches, the DODGE
    activity the sequence answers with, the two successor sequence labels the attack hands off to,
    and the three cycle fractions bounding the hand-off. Nothing in it is a length or a direction,
    so unlike `reach_cm` and `swings` it crosses the seam unconverted -- a button mask is a mask,
    an activity and a sequence label are names, and a fraction of a clip cycle has no units.

    `bounds_radius_m` appears only where it has been reconciled against the baked clip set
    (`clip_bounds_radius_m`). Its presence is therefore a promise that the number covers the
    geometry, which is the whole reason a consumer would trust it over the mesh's own bounds."""
    meta = {"activity": c.activity, "weight": c.actweight, "flags": c.flags,
            "frames": c.frames, "fps": round(c.fps, 4), "fade": round(c.fade, 4)}
    if c.reach is not None:
        meta["reach_cm"] = round(c.reach * INCH_TO_CM, 4)
    if c.blocked_reaction:
        meta["blocked_reaction"] = c.blocked_reaction
    if c.swings:
        meta["swings"] = UEK.unreal_swings(c.swings)
    if c.combo:
        # The whole record or nothing: `read_combo_chain` already answered "is any of this
        # authored", and once it says yes every field is stated -- including a window that reads
        # as the file's unauthored default, which a consumer cannot re-derive and must not guess.
        meta["combo"] = {"mask": c.combo.mask, "dodge": c.combo.dodge, "chain": c.combo.chain,
                         "chain_alt": c.combo.chain_alt,
                         "w_open": round(c.combo.w_open, 6),
                         "w_close": round(c.combo.w_close, 6),
                         "w_hold": round(c.combo.w_hold, 6)}
    if bounds_radius_m is not None:
        meta["bounds_radius_m"] = round(bounds_radius_m, 4)
    return meta


def warn_combo_chain_orphans(model, clips):
    """Name every chain successor `clips` states that the same model does not define -> the count.

    The four shipped dangling links are authoring bugs in retail's own banks, not decode
    failures, so the sidecar carries the string verbatim and this says so out loud rather than
    dropping it (`mdl_skel.combo_chain_orphans`). A silent hand-off to a sequence that is not
    there is exactly the kind of missing prerequisite the runtime would otherwise meet as a
    no-op, and only the exporter is in a position to see both ends of the link."""
    orphans = S.combo_chain_orphans(clips)
    for label, target in orphans:
        print(f"  ! {model}: '{label}' chains to '{target}', which the model does not define")
    return len(orphans)


def authored_radius_m(c):
    """A sequence's own model-space bound as a radius about the model origin, in the metres
    the decode is stated in -- `mdl_skel.Seq.bbmin`/`bbmax` reduced to its largest coordinate.

    A radius rather than the box: the box is in Source axes and the runtime holds the model in
    the repo's canonical Unreal frame, so a box would have to carry its basis across the seam
    while a magnitude does not care which way the axes point."""
    return max(max(abs(v) for v in c.bbmin), max(abs(v) for v in c.bbmax)) * S.SCALE


def clip_bounds_radius_m(c, measured_m):
    """The radius a clip's rendered geometry needs, reconciling what the file declares against
    what actually baked. The larger wins: retail's own number is the faithful answer and is
    what this normally emits, but it is zero on a descriptor that was never filled in, and a
    bound that does not contain the pose culls the model out of its own cutscene."""
    return max(authored_radius_m(c), measured_m)


def write_facial(stem, model, rig):
    """Write one character's flex rig to `facial/<stem>.json` -> the manifest fields naming it.

    Kept out of `npc_manifest.json` for the same reason the clip vocabularies are: the rig is
    ~65 flexdescs + 44 controllers + 60 rules + a morph row each, and a map places 17-22 NPC
    models, so the runtime should parse only the ones it places. `{}` for a model with no
    flex rig -- 78 of the 157 exported characters are rigged, `shovelhead` carries the header
    without a single flex record, and **no player body carries one at all** (PL13)."""
    if not rig:
        return {}
    os.makedirs(FACIAL_DIR, exist_ok=True)
    path = os.path.join(FACIAL_DIR, stem + ".json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"stem": stem, "model": model,
                   "note": "morphs[i] describes morph target i of the stem's decoded mesh, in "
                           "order; the baked skeletal mesh preserves that order. "
                           "`targets` is the four-value ramp the flexdesc's weight is remapped "
                           "through before it becomes that morph's weight; the weight itself "
                           "comes from `rules` (RPN over `controllers`). See "
                           "docs/vtmb/facial_animation.md.",
                   **rig}, f, separators=(",", ":"))
    return {"facial": "facial/" + os.path.basename(path), "morphs": len(rig["morphs"])}


def write_eyes(stem, model, rig):
    """Write one character's eyeball records to `eyes/<stem>.json` -> the manifest fields.

    A separate sidecar from the flex rig, and deliberately: **57 of the 59 player bodies carry
    a pair of eyeball records and no flex data at all**, so a rig-gated write would give the
    player no eyes. Eye aiming needs no flex data — it is a renderer-side basis — while the
    lids the same record names need a flexdesc to land on. `{}` for a model with none, which
    is every gib, prop and piece of scenery.

    The record is the authored bridge the four eyelid rules run into: `upper/lowerflexdesc`
    are the rule outputs and `upper/lowerlidflexdesc` the morph-carrying flexdescs the eye
    pass overwrites after the rules have run. `docs/vtmb/facial_animation.md`."""
    if not rig:
        return {}
    eyes_dir = os.path.join(NPC_DIR, "eyes")
    os.makedirs(eyes_dir, exist_ok=True)
    path = os.path.join(eyes_dir, stem + ".json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"stem": stem, "model": model,
                   "note": "org/up/forward are Unreal-native - centimetres, Z-up, left-handed - "
                           "written by the same conversion the body's own bones are, so the "
                           "runtime reads them verbatim. uppertarget/lowertarget are LINEAR "
                           "OFFSETS in eyeball units, not angles - the renderer takes "
                           "asin(t/radius); reading them as radians still moves a lid. `meshes` "
                           "joins a material name to the eyeball it draws. See "
                           "docs/vtmb/facial_animation.md.",
                   **UEK.unreal_eye_rig(rig)}, f, separators=(",", ":"))
    return {"eyes": "eyes/" + os.path.basename(path), "eyeballs": len(rig["eyeballs"])}


def write_garment(stem, model, idx, model_key):
    """Write one character's authored renderer-cloth garments to `garment/<stem>.json`.

    The payload is VtMB's own simulated-garment data -- particles, constraints, collision
    capsules and spheres, and the per-render-vertex substitution maps that replace skinned
    output after skinning (`docs/vtmb/secondary_motion.md`). `{}` for a model that does not
    carry it, which is 4,385 of the 4,445 installed models.

    Unreal-native like every coordinate-bearing sidecar beside it, and the conversion lives in
    `UE_mdl_cloth.py` where the naming rule requires it.
    """
    del model  # named by the sidecar itself, through the writer
    return UE_mdl_cloth.write(stem, model_key, idx, GARMENT_DIR)


def write_procedural(stem, model, rules, prefix=""):
    """Write one character's procedural bone rules to `<prefix>procedural/<stem>.json` -> the
    manifest fields naming it.

    Kept out of `npc_manifest.json` for the reason the flex rigs are: a rigged model carries
    12-21 driven bones, each a 176-byte table, and a map places 17-22 models. `{}` for a model
    with none -- 110 of the 339 v2531 models in the patch tree declare any.

    A driven bone's animation channels are decoded and then discarded: the runtime recomputes
    its local from this table after the graph blends and the hierarchy composes, which is why
    the correction cannot be baked into the clips. The evaluation is
    `docs/vtmb/procedural_bones.md`; the Unreal stage that runs it is
    `docs/architecture/animation-architecture.md`."""
    if not rules:
        return {}
    rel = "/".join(filter(None, (prefix, "procedural", stem + ".json")))
    path = os.path.join(NPC_DIR, *rel.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"stem": stem, "model": model,
                   "note": "A rule replaces its driven bone's local transform. Rotate `axis` "
                           "by the control bone's local rotation to get w; term k's signed "
                           "weight is dot(driver_axes[k], w), selecting entry 2k when "
                           "positive and 2k+1 when negative and weighting it by |weight|. "
                           "pos/quat are Unreal-native, written by the same conversion the "
                           "body's own bones are, so the runtime reads them verbatim. See "
                           "docs/vtmb/procedural_bones.md.",
                   "driver_axes": UEK.DRIVER_AXES,
                   "rules": UEK.unreal_axis_rules(rules)}, f, separators=(",", ":"))
    return {"procedural": rel, "procedural_bones": len(rules)}


def write_blends(stem, model, table, prefix=""):
    """Write one model's blend grids, autolayer binding, event timelines and authored movement
    paths to `<prefix>blends/<stem>.json` -> the manifest fields naming it. The first three are
    read from the same 764-byte sequence descriptor and the fourth off the animation descriptor
    each sequence selects, so they ship in one file; a model authoring none of them writes none.

    Movement rides here rather than on the clip slice for the reason the timelines do: the slice
    is per resolving character and this is per owning model. Inlined into the rows it would
    repeat every bank sequence's path across all 157 characters that resolve it -- 942,739
    records and ~83 MB against the 13,505 records and 0.9 MB the owning models declare between
    them -- and the melee consumer already holds the `owner` column the slice row states, which
    is the key this table is reached by.

    Kept out of `npc_manifest.json` for the reason the flex rigs and procedural tables are:
    `move_and_ranged` alone authors 253 grids, and a map places 17-22 models. `{}` for a model
    that authors none — 913 of the 1,166 sequences on either `move_and_ranged` are a single
    cell, which is a clip and needs no table.

    A grid names one of *this* stem's baked clips for each cell; the mix is the host's, driven by
    the pose parameter each axis binds to. The cells are not pre-blended and must not be: the
    engine evaluates every cell and mixes the resulting transforms, which is a different pose
    from mixing the clips first."""
    if not table:
        return {}
    rel = "/".join(filter(None, (prefix, "blends", stem + ".json")))
    path = os.path.join(NPC_DIR, *rel.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"stem": stem, "model": model,
                   "note": "grids[label] is one sequence's blend space. `groupsize` gives the "
                           "two axis extents and `cells[].axis` a cell's position on them; "
                           "`paramindex[a]` indexes `pose_parameters` for axis a and is -1 "
                           "when that axis is unused. Resolve an axis by wrapping the "
                           "parameter through a non-zero `loop`, normalizing over the "
                           "parameter's start..end, remapping through this grid's "
                           "paramstart[a]..paramend[a], clamping to 0..1 and scaling by the "
                           "extent; that yields a cell index and a fraction to the next. "
                           "`cells[].clip` names an animation this stem bakes, or is null "
                           "where the cell's animation did not bake. A cell whose animation "
                           "carries authored movement also has a `motion` summary in seconds, "
                           "centimetres and centimetres/second for an in-place host motor; the "
                           "whole authored path is in `movement` below, per sequence. "
                           "Evaluate each cell and "
                           "blend the results — never blend the clips. "
                           "autolayers[label] names the clips composed WITH that host, in "
                           "the order the engine walks them: the host is the base pose and "
                           "each entry is evaluated beside it and accumulated onto it, as a "
                           "masked overlay or as an additive according to that clip's own "
                           "flags and mask. The order is data, not a convention — an overlay "
                           "blends toward its own pose and would overwrite an additive "
                           "already accumulated onto the bones it owns. The record carries no "
                           "weight, ramp or flags; the weight a layer arrives with is the "
                           "host's. events[label] is that sequence's timeline, one row per "
                           "record in the columns `event_fields` names: cycle normalized over "
                           "the sequence, the numeric dispatch id, the record type, and an "
                           "index into `event_options` for the record's 64-byte options "
                           "payload. Rows stay in the descriptor's own order, which is the "
                           "order the dispatcher fires records sharing a cycle in. See "
                           "docs/vtmb/animation_and_movers.md A.3 and its sequence-event "
                           "section. movement[label] is that sequence's authored displacement "
                           "path, one row per record in the columns `movement_fields` names. "
                           "EVERY VALUE IS ALREADY UNREAL-NATIVE: `pos_*_cm` is the cumulative "
                           "displacement at `end_frame` in centimetres on Unreal axes (X "
                           "forward, Y right, Z up) in the clip's own local frame, `dir_*` the "
                           "unit direction on the same axes, `v0_cm`/`v1_cm` the ease "
                           "coefficients of v0*f + 0.5*(v1-v0)*f*f over the block fraction (a "
                           "length, not a rate; the block travels 0.5*(v0+v1)) and `yaw_deg` "
                           "the turn about Z in Unreal's sense. Convert nothing and negate "
                           "nothing -- the Y reflection retail spends at the point of use is "
                           "already spent here. Sample the path piecewise: walk the records in "
                           "order, take the last one whose `end_frame` is below the frame you "
                           "want as the base, and ease into the next. A label absent from "
                           "`movement` while `movement_fields` is present authors no record at "
                           "all and displaces nothing.",
                   **table}, f, separators=(",", ":"))
    return {"blends": rel, "blend_grids": len(table.get("grids", {})),
            "event_sequences": len(table.get("events", {})),
            "movement_sequences": len(table.get("movement", {}))}


def animated_prop_index_row(rec):
    """One placed-model manifest record projected into the v6-compatible runtime row.

    `clips` becomes the model's own sequence table in DECLARATION ORDER with the selection keys
    beside each label, and each row carries its ordinal explicitly. Order is semantic: retail's
    `CBaseProp::Spawn` stands a prop on `SelectWeightedSequence(ACT_IDLE)` falling back to sequence
    **index 0**, so sorting the labels — as versions 4 and 5 did — picks the wrong rest pose for
    any model whose first declared sequence is not also its alphabetically first (`drknobantique`,
    `clamp`, `wolf_form`).

    Each row also carries `bounds_radius_m`, the reach the clip needs about the model origin in
    the decode's own metres (`clip_bounds_radius_m`). The runtime widens the mesh's bind-pose
    bounds by it, because a prop animated in place draws where its bones go and is culled on
    where its component sits.
    """
    return {
        # The container the character bake actually reads.
        "eskm": rec["eskm"],
        "model": rec["model"],
        "bones": rec.get("bones", 0),
        "split_bones": rec.get("split_bones", []),
        **({"procedural": rec["procedural"],
            "procedural_bones": rec["procedural_bones"]} if rec.get("procedural") else {}),
        **({"blends": rec["blends"], "blend_grids": rec["blend_grids"],
            "event_sequences": rec["event_sequences"],
            "movement_sequences": rec.get("movement_sequences", 0)}
           if rec.get("blends") else {}),
        "clips": [{"name": label, "index": i, **meta}
                  for i, (label, meta) in enumerate(rec.get("clips", {}).items())],
    }


def placed_model_index_row(rec):
    """The v7 complete placed-model row, retaining declaration order and bake policy."""
    row = animated_prop_index_row(rec)
    row.update({
        "static_stem": rec.get("static_stem", rec.get("stem", "")),
        "clip_mode": rec.get("clip_mode", "rest"),
        "rest_candidates": list(rec.get("rest_candidates", [])),
        "static_equivalent": bool(rec.get("static_equivalent", False)),
    })
    return row


def write_sidecars(manifest):
    """The runtime-facing split of `npc_manifest.json` (roadmap 8.5).

    The whole manifest is 15.8 MB / 218k clip rows because 157 characters each resolve ~1,400
    clips out of the same shared banks. A map places 17-22 distinct models (and one player
    body), so the runtime is made to parse only those:

      npc_index.json      every character + bank, container path and counts, no clip maps (~47 KB)
      clips/<stem>.json   one character's whole resolved vocabulary

    A slice interns its owner stems and activity literals into two small arrays and stores
    each clip as `[owner_i, activity_i, weight, flags, frames, fps]`. The strings repeat
    across ~1,400 rows (67 owners, a few hundred activities), so interning pays for the
    activity column and still lands under the un-interned label->owner map it replaces.
    Index 0 of `owners` is always the NPC itself; index 0 of `activities` is always `""`
    (a layer/plumbing sequence the engine composes rather than selects).

    A row is truncated at its last stated column, which is how the schema carries a sparse
    fact: `fields` names every column, and a reader takes a column it does not reach as
    unstated. `fade` has always been optional that way, and the melee pair `reach_cm` /
    `blocked_reaction` is appended behind it on the same terms -- 581 and 147 of the install's
    14,012 sequences state them, so a fixed-width row would spend two columns per clip on a
    fact ~4% of clips carry. `blocked_reaction` states its `ACT_*` literal inline rather than
    interning into `activities`: that array is the stem's playable vocabulary, unioned by
    conformance checks to answer "can some model play this activity", and a reaction a clip
    only reacts to (never performs) has no business answering yes. `swings` sits behind them on
    the same terms, and its knockback candidates stay inline for the same reason the reaction
    does -- they are activities the *victim* plays, not this stem. `combo` closes the melee set
    at column 10, 208 sequences wide, and its two successor labels stay inline too because they
    are sequence labels rather than activities and `activities` is not a table they index."""
    os.makedirs(CLIPS_DIR, exist_ok=True)
    index = {
        "manifest_version": manifest["manifest_version"],
        "note": "counts only for npcs/banks; a character clip vocabulary lives in "
                "clips/<stem>.json, a flex rig in facial/<stem>.json, an eyeball pair in "
                "eyes/<stem>.json, a procedural bone rule table in procedural/<stem>.json and "
                "a blend-grid table, autolayer binding and sequence event timelines in "
                "blends/<stem>.json. Those paths are relative to this file's "
                "directory. placed_models carry their "
                "baked clip vocabulary inline, in the model's own sequence-declaration order.",
        "npcs": {s: {"model": r["model"], "bones": r["bones"],
                     "split_bones": r.get("split_bones", []),
                     "clips": len(r["clips"]), "own_clips": len(r["own_clips"]),
                     **({"facial": r["facial"], "morphs": r["morphs"]} if r.get("facial")
                        else {}),
                     **({"eyes": r["eyes"], "eyeballs": r["eyeballs"]} if r.get("eyes")
                        else {}),
                     **({"procedural": r["procedural"],
                         "procedural_bones": r["procedural_bones"]} if r.get("procedural")
                        else {}),
                     **({"blends": r["blends"], "blend_grids": r["blend_grids"],
                         "event_sequences": r["event_sequences"],
                         "movement_sequences": r.get("movement_sequences", 0)}
                        if r.get("blends") else {}),
                     **({"garment": r["garment"],
                         "garment_particles": r["garment_particles"]}
                        if r.get("garment") else {})}
                 for s, r in manifest["npcs"].items()},
        "banks": {s: {"model": r["model"], "clips": len(r["clips"]),
                      **({"blends": r["blends"], "blend_grids": r["blend_grids"],
                          "event_sequences": r["event_sequences"],
                          "movement_sequences": r.get("movement_sequences", 0)}
                         if r.get("blends") else {})}
                  for s, r in manifest["banks"].items()},
        # 12.1 — a choreo scene's anim-set model key -> the per-bone-root banks it was split
        # into. The runtime resolves (BaseAnim/MaleAnim/FemaleAnim, the actor's bonerename
        # source) through this to a bank stem in `banks` above.
        "cinematics": manifest.get("cinematics", {}),
        # v4 — skeletal prop models selected by prop_dynamic. Version 3 readers see no field;
        # version 4 readers get the GLB plus the exact baked clip inventory.
        #
        # `clips` carries the emitted sequence table in DECLARATION ORDER, with the
        # selection keys beside each label. Order is semantic, not presentation: retail's
        # CBaseProp::Spawn stands a prop on SelectWeightedSequence(ACT_IDLE) and falls back to
        # sequence **index 0**, so the first entry is the rest pose for the 16 of 19 models that
        # tag no ACT_IDLE. The v4/v5 shape sorted the labels alphabetically, which picked the
        # wrong sequence 0 for `drknobantique` (`handle_locked` over `idle`), `clamp` (`close`
        # over `idle`) and `wolf_form`. Inlined rather than sliced into a sidecar because the
        # whole prop corpus is ~80 clips: a `clips/<stem>.json` slice would also have to enter
        # the stem namespace props already share with NPCs, where a collision aliases a rig.
        "animated_props": {stem: animated_prop_index_row(rec)
                           for stem, rec in manifest.get("animated_props", {}).items()},
        # v7 — every non-character MDL placed by .ents or GAME_LUMP. A row either carries all
        # clips because gameplay names them, or only the possible authored resting clips.
        "placed_models": {stem: placed_model_index_row(rec)
                          for stem, rec in manifest.get("placed_models", {}).items()},
        "warnings": manifest.get("warnings", []),
    }
    with open(INDEX, "w", encoding="utf-8") as f:
        json.dump(index, f, indent=1)

    banks, total = manifest["banks"], 0
    for stem, rec in manifest["npcs"].items():
        owners, acts = [stem], [""]
        owner_i, act_i = {stem: 0}, {"": 0}
        clips = {}
        for label, owner in rec["clips"].items():
            meta = (rec["own_clips"] if owner == stem
                    else banks.get(owner, {}).get("clips", {})).get(label)
            if meta is None:
                continue                      # reconcile already dropped these; belt and braces
            if owner not in owner_i:
                owner_i[owner] = len(owners); owners.append(owner)
            act = meta["activity"]
            if act not in act_i:
                act_i[act] = len(acts); acts.append(act)
            row = [owner_i[owner], act_i[act], meta["weight"], meta["flags"],
                   meta["frames"], meta["fps"], meta.get("fade", 0.2)]
            reach = meta.get("reach_cm")
            blocked = meta.get("blocked_reaction")
            swings = meta.get("swings")
            combo = meta.get("combo")
            if combo:
                # The combo column reaches past all three, and 28 of the 208 carriers -- the
                # `meleeshared_onehand` flying-knockback reaction chain, which is what the victim
                # plays rather than an attack -- state none of them. So all three placeholders
                # are needed, and the third is the empty list for the same reason the second is
                # the empty literal: that is what "no swing" already means to a reader taking
                # column 9 as an array.
                row += [reach, blocked or "", swings or [], combo]
            elif swings:
                # Every column a stated one sits behind is held open, whatever it holds: all 574
                # swing carriers state a reach but 427 of them name no blocked reaction, and a
                # row that closed that column up would put a list where a literal belongs. The
                # two placeholders differ because their readers do: `reach_cm` is read guarded
                # against a null, `blocked_reaction` is taken as a string unconditionally, and
                # the empty literal is what "no reaction" already means to it.
                row += [reach, blocked or "", swings]
            elif blocked:
                # `reach_cm` holds the column blocked_reaction sits behind, so a sequence that
                # named a reaction without a reach still lands its reaction in the right column.
                # The literal is inlined rather than interned: only 147 rows carry it, and the
                # `activities` array is the stem's playable vocabulary the runtime unions for
                # conformance, so a referenced-but-unselectable reaction would pollute it.
                row += [reach, blocked]
            elif reach is not None:
                row.append(reach)
            clips[label] = row
        path = os.path.join(CLIPS_DIR, stem + ".json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"stem": stem, "owners": owners, "activities": acts,
                       "fields": ["owner", "activity", "weight", "flags", "frames", "fps",
                                  "fade", "reach_cm", "blocked_reaction", "swings", "combo"],
                       "clips": clips}, f, separators=(",", ":"))
        total += os.path.getsize(path)
    print(f"[npc] sidecars: {INDEX} ({os.path.getsize(INDEX)/1024:.0f} KB) + "
          f"{len(manifest['npcs'])} clip slices in {CLIPS_DIR} "
          f"({total/1e6:.1f} MB total, {total/max(1, len(manifest['npcs']))/1024:.0f} KB each)")


def main(only=None, *, placed_uses=None, index=None, integrate=False, strict=False):
    """Export the runtime skeletal corpus.

    ``only`` selects character model keys; ``placed_uses`` selects already-discovered
    non-character placements with their exact clip policy.  With ``integrate=True`` those
    records and any newly required banks are merged into the current complete manifest before
    its runtime projections are regenerated.  Without integration, targeted calls intentionally
    produce a standalone manifest and should therefore use an isolated output root configured by
    the caller.

    ``strict`` keeps best-effort decoding inside individual format operations
    but refuses to publish a new manifest if a requested model/bank failed.
    """
    idx = index if index is not None else install.build_index()
    failures = []
    warnings = []
    load_mdl = lambda k: (r if (r := install.read(idx, (k[:-4] if k.lower().endswith(".mdl")
                                                         else k) + ".mdl")) else None)

    # The default seed is two lists, because the two halves are referenced differently: NPCs by
    # the maps' own entities, the player bodies only by the rulebook (PL13).
    if only is None and placed_uses is None:
        from_ents = npc_models_from_ents()
        pc_models = set(pc_models_from_clandoc())
        cinematics = cinematic_models_from_ents()
        animated_props = animated_prop_models_from_ents()
        placed_uses = PM.discover(OUT, idx)
        # A model with no sequence has no authored pose for the animation layer to select. A
        # single-frame sequence does and stays skeletal: frame count is not a bind-pose licence.
        still = [m for m in animated_props
                 if (r := load_mdl(m)) is not None and not has_animation(r)]
        if still:
            animated_props = [m for m in animated_props if m not in set(still)]
            print(f"  ! [npc] {len(still)} animated-prop candidate(s) declare no sequence pose and "
                  f"stay static: {', '.join(_basename_stem(m) for m in still)}")
        print(f"[npc] seed: {len(from_ents)} npc model(s) from the exported .ents + "
              f"{len(pc_models)} player body model(s) from {CLANDOC} + "
              f"{len(cinematics)} cinematic anim-set(s) + "
              f"{len(placed_uses)} placed model(s) "
              f"({sum(use.full_clips for use in placed_uses)} full-clip)")
        seed = sorted(set(from_ents) | pc_models)
    else:
        seed = list(only or ())
        pc_models, cinematics, animated_props = set(), [], []
        placed_uses = list(placed_uses or ())

    npcs = [m for m in seed if load_mdl(m) is not None]
    missing = [m for m in seed if load_mdl(m) is None]
    for m in missing:
        warning = missing_npc_warning(m)
        if warning:
            warnings.append(warning)
            print(f"  ! warning {warning['code']}: {m} - {warning['detail']}")
        else:
            print(f"  ! {m}: no .mdl in install - skipped")
            failures.append(f"missing model: {m}")
    if not npcs and not cinematics and not placed_uses:
        message = "[npc] no character, cinematic, or animated-prop models to export"
        print(message)
        if strict:
            raise RuntimeError(message)
        return

    # NPC stems are basenames (the console/`elysium.npc.load` ergonomic); fall back to a
    # path-safe stem for any basename two different models share. Integration preserves the
    # complete catalogue's existing stem: a one-model slice must not rename a collision merely
    # because the other model is outside this invocation.
    from collections import Counter
    counts = Counter(_basename_stem(m) for m in npcs)
    previous_by_model = {}
    occupied = {}
    if integrate and os.path.isfile(MANIFEST):
        with open(MANIFEST, encoding="utf-8") as handle:
            previous_manifest = json.load(handle)
        for previous_stem, record in previous_manifest.get("npcs", {}).items():
            previous_model = str(record.get("model", "")).lower().replace("\\", "/")
            if previous_model:
                previous_by_model[previous_model] = previous_stem
                occupied[previous_stem] = previous_model

    npc_stem = {}
    for model in npcs:
        key = model.lower().replace("\\", "/")
        if key in previous_by_model:
            npc_stem[model] = previous_by_model[key]
            continue
        basename = _basename_stem(model)
        owner = occupied.get(basename)
        npc_stem[model] = (basename if counts[basename] == 1 and owner in (None, key)
                           else bank_stem(model))

    # Resolve every NPC's include tree once: its own clips, plus which bank owns each shared
    # clip (first model in tree order to define a label owns it).
    os.makedirs(NPC_DIR, exist_ok=True)
    print(f"[npc] resolving include trees for {len(npcs)} NPC model(s) ...", flush=True)
    npc_records = {}
    banks_needed = {}  # bank model_key -> bank_stem
    # A shared bank is reached from every NPC that fights out of it, so its chain census is
    # reported against the model that declares the link and only the first time the walk
    # arrives -- one line per authoring bug, not one per character.
    chain_censused = set()
    dangling_chains = 0
    for m in npcs:
        tree = S.resolve_tree(load_mdl, m)
        assigned, clips = {}, {}
        for key, d in tree:
            is_npc = (key == m)
            stem = npc_stem[m] if is_npc else bank_stem(key)
            seqs = S.local_sequences(d)
            if key not in chain_censused:
                chain_censused.add(key)
                dangling_chains += warn_combo_chain_orphans(key, seqs)
            if not is_npc and seqs:
                banks_needed.setdefault(key, stem)
            for c in seqs:
                ll = c.label.lower()
                if ll in assigned:
                    continue
                assigned[ll] = stem
                clips[c.label] = stem
        npc_records[m] = clips
    if dangling_chains:
        print(f"[npc] {dangling_chains} combo chain link(s) name a sequence their own model does "
              "not define; the sidecars carry the authored string")

    # Export the shared banks once each (the heavy decode pass -- ~one 90 MB set shared by all).
    print(f"[npc] exporting {len(banks_needed)} shared bank(s) -> {NPC_DIR}/banks/ ...", flush=True)
    bank_index = {}
    for key in sorted(banks_needed):
        try:
            info = mdl_gltf.export_bank(idx, key, NPC_DIR, banks_needed[key])
        except (Exception, SystemExit) as e:
            print(f"  !! bank {banks_needed[key]} FAILED: {e}")
            failures.append(f"bank {banks_needed[key]}: {e}")
            continue
        if info:
            bank_index[info["stem"]] = {
                "model": info["model"],
                "clips": {c.label: _clip_meta(c) for c in info["clips"]},
                **write_blends(info["stem"], info["model"], info["blends"]),
            }

    # The cinematic anim sets (12.1). Each is a whole multi-actor performance in one file, so it
    # is split into one bank per bone root with the prefix folded back to Bip01 -- which is what
    # a scene's `bonerename "BipNN" "Bip01"` selects. They ride the bank index like any other
    # bank; `cinematic_roots` records which roots a model offers so the runtime can resolve
    # (anim set, bonerename-from) -> bank.
    cinematic_index = {}
    if cinematics:
        print(f"[npc] exporting {len(cinematics)} cinematic anim-set(s) -> {NPC_DIR}/banks/ ...",
              flush=True)
    for key in cinematics:
        if load_mdl(key) is None:
            print(f"  ! {key}: no .mdl in install - skipped")
            failures.append(f"missing cinematic model: {key}")
            continue
        stem = bank_stem(key)
        try:
            banks = mdl_gltf.export_cinematic(idx, key, NPC_DIR, stem)
        except (Exception, SystemExit) as e:
            print(f"  !! cinematic {stem} FAILED: {e}")
            failures.append(f"cinematic {stem}: {e}")
            continue
        if not banks:
            continue
        roots = []
        for info in banks:
            bank_index[info["stem"]] = {
                "model": info["model"],
                "clips": {c.label: _clip_meta(c) for c in info["clips"]},
                **write_blends(info["stem"], info["model"], info["blends"]),
            }
            if info.get("root"):
                roots.append({"root": info["root"], "bank": info["stem"]})
        cinematic_index[key] = {"stem": stem, "roots": roots}

    # Export the NPC mesh glbs (mesh + skeleton + own clips + facial morph targets). The
    # unit-vector table the compressed vertex-animation records index is read once, from the
    # user's own StudioRender.dll -- without it the morph magnitudes are unknowable, so the
    # export ships the meshes and skips the faces rather than baking wrong deltas.
    anorms = S.load_anorms() if npcs else None
    print(f"[npc] decoding {len(npcs)} NPC mesh(es) -> {NPC_DIR}/ ...", flush=True)
    npc_index = {}
    procedural_faults = []
    eye_faults = []
    for m in npcs:
        try:
            info = mdl_gltf.export_npc(idx, m, NPC_DIR, npc_stem[m], anorms)
        except (Exception, SystemExit) as e:
            print(f"  !! npc {npc_stem[m]} FAILED: {e}")
            failures.append(f"npc {npc_stem[m]}: {e}")
            continue
        npc_index[info["stem"]] = {
            "model": info["model"], "bones": info["bones"],
            "split_bones": info["split_bones"],
            "clips": npc_records.get(m, {}),
            "own_clips": {c.label: _clip_meta(c) for c in info["clips"]},
            **write_facial(info["stem"], info["model"], info["facial"]),
            **write_eyes(info["stem"], info["model"], info["eyes"]),
            **write_procedural(info["stem"], info["model"], info["procedural"]),
            **write_blends(info["stem"], info["model"], info["blends"]),
            **write_garment(info["stem"], info["model"], idx, m),
        }
        procedural_faults.extend((info["stem"], f) for f in info["procedural_faults"])
        eye_faults.extend((info["stem"], f) for f in info["eye_faults"])

    # Every placed non-character MDL is a native skeletal model. Models reached by an authored
    # animation request retain their complete vocabulary; ordinary dressing carries only the
    # possible resting clips. GAME_LUMP may keep a static actor only when every such pose was
    # proven equivalent to storage geometry.
    placed_model_index = {}
    static_fallbacks = static_model_fallbacks_from_ents()
    if placed_uses:
        os.makedirs(PLACED_MODEL_DIR, exist_ok=True)
        print(f"[npc] indexing {len(placed_uses)} placed model(s) -> "
              f"{PLACED_MODEL_DIR}/ ...", flush=True)
    for use in placed_uses:
        model, stem = use.model, use.stem
        if load_mdl(model) is None:
            print(f"  ! placed model {model}: no .mdl in install - skipped")
            failures.append(f"missing placed model: {model}")
            continue
        try:
            d, v = mdl.load(idx, model)
            bones = S.read_bones(d)
            sequences = S.local_sequences(d)
            candidates = PM.rest_candidates(sequences)
            if not candidates:
                raise ValueError("model declares no sequence 0 resting pose")
            required_labels = {label.lower() for label in use.required_clips}
            available_labels = {sequence.label.lower() for sequence in sequences}
            missing_required = sorted(required_labels - available_labels)
            if missing_required:
                warning = missing_intrinsic_prop_clips_warning(model, missing_required)
                if warning is None:
                    raise ValueError(
                        "runtime-required clip(s) absent: " + ", ".join(missing_required)
                    )
                warnings.append(warning)
                print(f"  ! warning {warning['code']}: {stem} uses "
                      f"{warning['fallback']} ({warning['detail']})")
            selected = (sequences if use.full_clips else
                        [sequence for sequence in sequences
                         if sequence in candidates or sequence.label.lower() in required_labels])

            # A full-clip model is decoded whole so its manifest facts -- which clips bake,
            # measured extents, procedural rules -- come off the same decode the bake performs.
            # Rest-only models go straight from MDL facts.
            if use.full_clips:
                info = mdl_gltf.export_npc(idx, model, PLACED_MODEL_DIR, stem, anorms=None,
                                           measure_extents=True)
                baked_by_name = {clip.label: clip for clip in info["clips"]}
                candidate_names = {clip.label for clip in candidates}
                selected = [baked_by_name.get(clip.label, clip) for clip in selected
                            if clip.label in baked_by_name or clip.label in candidate_names]
                rules = info["procedural"]
                rule_faults = info["procedural_faults"]
                blends = info["blends"]
                extents = info["clip_extents"]
            else:
                rules, rule_faults = S.axis_interp_records(d, bones)
                blends = {}
                extents = {clip.label: S.clip_extent(d, bones, clip.base, clip.frames)
                           for clip in selected}
        except (Exception, SystemExit) as e:
            warning = animated_prop_warning(
                model,
                e,
                has_static_fallback=model in static_fallbacks,
            )
            if warning:
                warnings.append(warning)
                print(
                    f"  ! warning {warning['code']}: {stem} uses "
                    f"{warning['fallback']} ({warning['detail']})"
                )
            else:
                print(f"  !! placed model {stem} FAILED: {e}")
                failures.append(f"placed model {stem}: {e}")
            continue

        prop_clips = {}
        for c in selected:
            measured = extents.get(c.label, 0.0)
            if authored_radius_m(c) < measured - 1e-4:
                print(f"  ! {stem}: clip '{c.label}' declares r={authored_radius_m(c):.3f} m "
                      f"but bakes out to {measured:.3f} m - using the baked extent")
            prop_clips[c.label] = _clip_meta(c, clip_bounds_radius_m(c, measured))
        placed_model_index[stem] = {
            "stem": stem,
            "eskm": "placed_models/%s.eskm" % stem,
            "model": model,
            "static_stem": use.static_stem,
            "clip_mode": ("full" if use.full_clips else
                          "required" if use.required_clips else "rest"),
            "rest_candidates": [clip.label for clip in candidates],
            "static_equivalent": PM.rest_pose_static_equivalent(d, v, candidates),
            "bones": len(bones),
            "split_bones": [bone.name for bone in bones if bone.flags & 0x2],
            "clips": prop_clips,
            **write_procedural(stem, model, rules, "placed_models"),
            **write_blends(stem, model, blends, "placed_models"),
        }
        procedural_faults.extend((stem, f) for f in rule_faults)

    # v4-v6 readers see the complete-clip subset under the old field. A v7 reader uses
    # placed_models for every body and never infers static representation from this projection.
    animated_prop_index = {
        stem: record for stem, record in placed_model_index.items()
        if record["clip_mode"] == "full"
    }
    covered_models = {record["model"] for record in placed_model_index.values()}
    missing_placed = sorted(use.model for use in placed_uses if use.model not in covered_models)
    if missing_placed:
        raise RuntimeError(
            "placed-model catalogue incomplete (%d missing): %s" %
            (len(missing_placed), ", ".join(missing_placed[:8]))
        )

    # Reconcile: a sequence the include tree advertises but whose owner failed to bake (empty
    # tracks, or a bank export that raised) must not appear as resolvable. Filtering here is
    # what lets the runtime treat a hit in `clips` as a promise the owner's bake can answer.
    bank_baked = {stem: set(rec["clips"]) for stem, rec in bank_index.items()}

    def _activity(stem, rec, label, owner):
        meta = (rec["own_clips"] if owner == stem
                else bank_index.get(owner, {}).get("clips", {})).get(label)
        return (meta or {}).get("activity", "")

    dropped = 0
    foreign_clan = 0
    for stem, rec in npc_index.items():
        own = set(rec["own_clips"])
        # Resolve exactly the way the runtime does -- own stem first, then the bank index --
        # so the two can never disagree even if a bank and an NPC ever share a stem.
        keep = {lbl: owner for lbl, owner in rec["clips"].items()
                if lbl in (own if owner == stem else bank_baked.get(owner, ()))}
        dropped += len(rec["clips"]) - len(keep)
        # A player body's include tree reaches `pcidles_allsequences`, which chains ALL SEVEN clan
        # banks, so every PC body resolves every clan's character-sheet fidget:
        # `tremere_female_armor_0` sees 24 of them and weighted order puts `Malk_Female_Idle2` (10)
        # ahead of its own `Tremere_Female_Idle2` (3). The sheet screen picks by clan, and a body
        # IS its clan -- the Tremere fidgets are the ones its own container carries -- so ownership
        # is the clan test and needs no clan table.
        #
        # Scoped to this one activity deliberately. Every other shared label is shared on purpose:
        # a walk from `character_shared_female_move_and_ranged` is the same walk for all of them.
        clan = {lbl for lbl, owner in keep.items()
                if owner != stem and _activity(stem, rec, lbl, owner) == "ACT_CHARSHEET_FIDGET"}
        foreign_clan += len(clan)
        rec["clips"] = {lbl: owner for lbl, owner in keep.items() if lbl not in clan}
    if dropped:
        print(f"[npc] dropped {dropped} unresolvable clip refs (owner did not bake them)")
    if foreign_clan:
        print(f"[npc] dropped {foreign_clan} charsheet fidget(s) belonging to another clan")

    manifest = {
        "manifest_version": MANIFEST_VERSION,
        "note": "npcs[stem].clips maps a clip label -> the stem that OWNS it. If that stem is a "
                "key in `banks`, the clip is a baked bank sequence played through skeleton "
                "compatibility; otherwise it is the NPC's own. Per-clip metadata "
                "(activity/weight/flags/frames/fps) lives once on the owner: banks[owner].clips "
                "for a bank, npcs[stem].own_clips for the NPC's own. Every label in `clips` is "
                "backed by an animation the owner's decode bakes. npcs[stem].facial, when "
                "present, names the flex rig driving that body's morph targets.",
        "npcs": npc_index,
        "banks": bank_index,
        "cinematics": cinematic_index,
        "animated_props": animated_prop_index,
        "placed_models": placed_model_index,
        "warnings": warnings,
    }

    if strict and failures:
        raise RuntimeError("; ".join(failures))

    if integrate and os.path.isfile(MANIFEST):
        with open(MANIFEST, encoding="utf-8") as f:
            previous = json.load(f)
        version = previous.get("manifest_version")
        if version != MANIFEST_VERSION:
            raise RuntimeError(
                f"cannot integrate into NPC manifest v{version}; expected v{MANIFEST_VERSION}"
            )
        for key in ("npcs", "banks", "cinematics", "animated_props", "placed_models"):
            merged = dict(previous.get(key, {}))
            merged.update(manifest.get(key, {}))
            manifest[key] = merged
        by_key = {
            (item.get("code"), item.get("model")): item
            for item in previous.get("warnings", [])
        }
        by_key.update(
            {
                (item.get("code"), item.get("model")): item
                for item in manifest.get("warnings", [])
            }
        )
        manifest["warnings"] = [
            by_key[key] for key in sorted(by_key, key=lambda item: tuple(map(str, item)))
        ]

    with open(MANIFEST, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    write_sidecars(manifest)

    n_clips = sum(len(r["clips"]) for r in npc_index.values())
    metas = [m for r in bank_index.values() for m in r["clips"].values()]
    metas += [m for r in npc_index.values() for m in r["own_clips"].values()]
    acts = {m["activity"] for m in metas if m["activity"]}
    n_pc = sum(1 for r in npc_index.values() if r["model"] in pc_models)
    print(f"[npc] done: {len(npc_index)} characters ({len(npc_index) - n_pc} NPCs + {n_pc} PC "
          f"bodies), {len(bank_index)} banks, {len(animated_prop_index)} animated props, "
          f"{n_clips} resolved clip refs -> {MANIFEST}")
    print(f"[npc] clips: {len(metas)} distinct baked, {sum(1 for m in metas if m['activity'])} "
          f"carry an activity ({len(acts)} distinct, e.g. ACT_IDLE/ACT_DISPOSITION)")
    rigged = [r for r in npc_index.values() if r.get("facial")]
    print(f"[npc] faces: {len(rigged)}/{len(npc_index)} rigged, "
          f"{sum(r['morphs'] for r in rigged)} morph targets -> {FACIAL_DIR}/")
    driven = [r for r in (*npc_index.values(), *animated_prop_index.values())
              if r.get("procedural")]
    scanned = len(npc_index) + len(animated_prop_index)
    print(f"[npc] procedural bones: {len(driven)}/{scanned} models carry a rule table, "
          f"{sum(r['procedural_bones'] for r in driven)} driven bones -> {PROCEDURAL_DIR}/")
    for stem, fault in procedural_faults:
        print(f"  ! {stem}: procedural rule - {fault}")
    eyed = [r for r in npc_index.values() if r.get("eyes")]
    print(f"[npc] eyeballs: {len(eyed)}/{len(npc_index)} models carry a pair, "
          f"{sum(r['eyeballs'] for r in eyed)} records -> {NPC_DIR}/eyes/")
    for stem, fault in eye_faults:
        print(f"  ! {stem}: eyeball - {fault}")
    # A sidecar is written for any of the four blocks it can carry, so the grid count is taken
    # over the models that actually author a grid rather than over every model with a file.
    sidecars = [r for r in (*npc_index.values(), *bank_index.values(),
                            *animated_prop_index.values()) if r.get("blends")]
    gridded = [r for r in sidecars if r["blend_grids"]]
    print(f"[npc] blend grids: {len(gridded)} model(s) author one, "
          f"{sum(r['blend_grids'] for r in gridded)} grids -> {BLENDS_DIR}/")
    evented = [r for r in sidecars if r["event_sequences"]]
    print(f"[npc] sequence events: {len(evented)} model(s) author a timeline, "
          f"{sum(r['event_sequences'] for r in evented)} sequences -> {BLENDS_DIR}/")
    moved = [r for r in sidecars if r.get("movement_sequences")]
    print(f"[npc] authored movement: {len(moved)} model(s) author a path, "
          f"{sum(r['movement_sequences'] for r in moved)} sequences -> {BLENDS_DIR}/")
    print(f"[npc] size: manifest {os.path.getsize(MANIFEST)/1e6:.1f} MB")
    if warnings:
        print(
            f"[npc] warnings: {len(warnings)} known source issue(s) recorded in "
            f"{MANIFEST} and {INDEX}"
        )
def reindex() -> None:
    """Re-derive runtime sidecars from the canonical manifest."""

    with open(MANIFEST, encoding="utf-8") as f:
        write_sidecars(json.load(f))
