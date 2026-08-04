"""Batch character export: mesh glbs + shared animation-bank glbs + a resolution manifest.

Two seeds, one product. The maps' own `npc_*` model references cover every NPC; the **player
bodies** are named by no entity at all, so they come from `vdata/system/clandoc000.txt`
instead (56 distinct models over its 84 `M_Body0..5`/`F_Body0..5` slots, roadmap PL13). Both
are the same v2531 skeletal format, so both go through the same decode: each model's
include-model tree (`docs/vtmb/animation_and_movers.md` A.7) resolves into shared banks, and the
run writes under `$ELYSIUM_EXPORT_ROOT/npc/`:

  <npc>.glb          skinned mesh + skeleton + own clips (dialogue anims) + morph targets
  banks/<bank>.glb   a shared bank's skeleton + all its clips, no mesh
  npc_manifest.json  per-NPC {clip -> owning-stem} resolution + a bank/mesh index
  facial/<npc>.json  the flex rig above the morphs: controllers, rules, ramps (PL10)

The runtime (roadmap 8.5) reads the manifest, loads a clip's owning glb once -- shared across
every NPC that uses it -- and applies it to the NPC skeletal mesh by bone name via glTFRuntime
(`LoadSkeletalAnimation(mesh, ...)` matches tracks to the ref skeleton by bone name). This is
VtMB's own virtualmodel bank-sharing in the modern-engine shape: one skeleton, many meshes, a
shared animation library keyed by bone name -- not a per-NPC monolith.

The manifest carries each clip's engine-facing selection keys -- the `ACT_*` activity
literal, its weighted-random `weight`, `flags`, `frames` and `fps` -- stored once on the stem
that OWNS the clip rather than on every NPC that resolves it. That is what lets a consumer
ask for an `ACT_IDLE` (or the `ACT_DISPOSITION` stance a `default_disposition` names) instead
of pattern-matching a label: `regular_cop` resolves 229 clips with "idle" in the name, and
`Stance_Dead_Idle_1` is not one of the useful ones.

Manifest v3 adds the **face** (roadmap PL10). Each rigged NPC's `.mdl` flexes bake into glTF
morph targets in its own glb, and the three layers that drive them -- 44 flex controllers,
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
from elysium_pipeline.formats import mdl_skel as S
from elysium_pipeline.paths import export_root
from elysium_pipeline.exporters.source_warnings import (
    animated_prop_warning,
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
ANIMATED_PROP_DIR = os.path.join(NPC_DIR, "animated_props")
MANIFEST_VERSION = 6


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
    """True when the model declares a sequence carrying more than one frame.

    A `prop_dynamic` may author `LoopSequence` on a model whose only sequence is a single
    static frame. Six models in the shipped seed do exactly that -- `stage_light`,
    `lampfloor`, `glassa`, `junkyardcraneb`, `bottleb` and `bottlec` each declare one
    1-frame `idle`. They are static dressing wearing an animation keyvalue: there is no
    motion to bake, and standing a skeletal body for one replaces the baked static mesh
    with a bind pose.

    The test is *any* sequence with more than one frame, not every one: `clamp`'s `idle`
    is a single frame beside its real 45-frame `open`/`close`, and `wolf_form` carries
    twelve 1-frame hit poses among its real clips.
    """
    try:
        return any(s.frames > 1 for s in S.local_sequences(d))
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
        print(f"[npc] {path} not found - PC bodies skipped (run the vdata export first)")
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
    `flags` the studio sequence bits. Stored once per owning stem, not per NPC that resolves
    it -- 157 characters x ~1,400 resolved clips would be two orders of magnitude more rows.

    `bounds_radius_m` appears only where it has been reconciled against the baked glb
    (`clip_bounds_radius_m`). Its presence is therefore a promise that the number covers the
    geometry, which is the whole reason a consumer would trust it over the mesh's own bounds."""
    meta = {"activity": c.activity, "weight": c.actweight, "flags": c.flags,
            "frames": c.frames, "fps": round(c.fps, 4)}
    if bounds_radius_m is not None:
        meta["bounds_radius_m"] = round(bounds_radius_m, 4)
    return meta


def authored_radius_m(c):
    """A sequence's own model-space bound as a radius about the model origin, in the metres
    the glb is written in -- `mdl_skel.Seq.bbmin`/`bbmax` reduced to its largest coordinate.

    A radius rather than the box: the box is in Source axes and the runtime holds the model in
    glTFRuntime's, so a box would have to carry its basis across the seam while a magnitude
    does not care which way the axes point."""
    return max(max(abs(v) for v in c.bbmin), max(abs(v) for v in c.bbmax)) * mdl_gltf.SCALE


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
                   "note": "morphs[i] describes glTF morph target i of <stem>.glb, in order. "
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
                   "note": "org/up/forward are in <stem>.glb's own basis, written by the same "
                           "conversion that basis's mesh and clips are. uppertarget/lowertarget "
                           "are LINEAR OFFSETS in eyeball units, not angles - the renderer takes "
                           "asin(t/radius); reading them as radians still moves a lid. `meshes` "
                           "joins a glTF material name to the eyeball it draws. See "
                           "docs/vtmb/facial_animation.md.",
                   **rig}, f, separators=(",", ":"))
    return {"eyes": "eyes/" + os.path.basename(path), "eyeballs": len(rig["eyeballs"])}


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
                           "pos/quat are in <stem>.glb's own basis, written by the same "
                           "conversion that basis's mesh and clips are. See "
                           "docs/vtmb/procedural_bones.md.",
                   "driver_axes": mdl_gltf.DRIVER_AXES,
                   "rules": rules}, f, separators=(",", ":"))
    return {"procedural": rel, "procedural_bones": len(rules)}


def write_blends(stem, model, table, prefix=""):
    """Write one model's blend grids to `<prefix>blends/<stem>.json` -> the manifest fields
    naming it.

    Kept out of `npc_manifest.json` for the reason the flex rigs and procedural tables are:
    `move_and_ranged` alone authors 253 grids, and a map places 17-22 models. `{}` for a model
    that authors none — 913 of the 1,166 sequences on either `move_and_ranged` are a single
    cell, which is a clip and needs no table.

    A grid names the clip in *this* stem's glb for each cell; the mix is the host's, driven by
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
                           "`cells[].clip` names an animation of this stem's glb, or is null "
                           "where the cell's animation did not bake. A cell whose animation "
                           "carries authored movement also has a `motion` summary in seconds, "
                           "centimetres and centimetres/second for an in-place host motor. "
                           "Evaluate each cell and "
                           "blend the results — never blend the clips. See "
                           "docs/vtmb/animation_and_movers.md A.3.",
                   **table}, f, separators=(",", ":"))
    return {"blends": rel, "blend_grids": len(table["grids"])}


def animated_prop_index_row(rec):
    """One animated-prop manifest record projected into the runtime index (v6).

    `clips` becomes the model's own sequence table in DECLARATION ORDER with the selection keys
    beside each label, and each row carries its ordinal explicitly. Order is semantic: retail's
    `CBaseProp::Spawn` stands a prop on `SelectWeightedSequence(ACT_IDLE)` falling back to sequence
    **index 0**, so sorting the labels — as versions 4 and 5 did — picks the wrong rest pose for
    any model whose first declared sequence is not also its alphabetically first (`drknobantique`,
    `clamp`, `wolf_form`).

    Each row also carries `bounds_radius_m`, the reach the clip needs about the model origin in
    the glb's own metres (`clip_bounds_radius_m`). The runtime widens the mesh's bind-pose
    bounds by it, because a prop animated in place draws where its bones go and is culled on
    where its component sits.
    """
    return {
        "glb": rec["glb"],
        "model": rec["model"],
        "bones": rec.get("bones", 0),
        "split_bones": rec.get("split_bones", []),
        **({"procedural": rec["procedural"],
            "procedural_bones": rec["procedural_bones"]} if rec.get("procedural") else {}),
        **({"blends": rec["blends"], "blend_grids": rec["blend_grids"]}
           if rec.get("blends") else {}),
        "clips": [{"name": label, "index": i, **meta}
                  for i, (label, meta) in enumerate(rec.get("clips", {}).items())],
    }


def write_sidecars(manifest):
    """The runtime-facing split of `npc_manifest.json` (roadmap 8.5).

    The whole manifest is 15.8 MB / 218k clip rows because 157 characters each resolve ~1,400
    clips out of the same shared banks. A map places 17-22 distinct models (and one player
    body), so the runtime is made to parse only those:

      npc_index.json      every character + bank, glb path and counts, no clip maps (~47 KB)
      clips/<stem>.json   one character's whole resolved vocabulary

    A slice interns its owner stems and activity literals into two small arrays and stores
    each clip as `[owner_i, activity_i, weight, flags, frames, fps]`. The strings repeat
    across ~1,400 rows (67 owners, a few hundred activities), so interning pays for the
    activity column and still lands under the un-interned label->owner map it replaces.
    Index 0 of `owners` is always the NPC itself; index 0 of `activities` is always `""`
    (a layer/plumbing sequence the engine composes rather than selects)."""
    os.makedirs(CLIPS_DIR, exist_ok=True)
    index = {
        "manifest_version": manifest["manifest_version"],
        "note": "counts only for npcs/banks; a character clip vocabulary lives in "
                "clips/<stem>.json, a flex rig in facial/<stem>.json, an eyeball pair in "
                "eyes/<stem>.json, a procedural bone rule table in procedural/<stem>.json and "
                "a blend-grid table in blends/<stem>.json. Those paths, like the bank glb "
                "paths, are relative to this file's directory. animated_props carry their "
                "whole clip vocabulary inline, in the model's own sequence-declaration order.",
        "npcs": {s: {"glb": r["glb"], "model": r["model"], "bones": r["bones"],
                     "split_bones": r.get("split_bones", []),
                     "clips": len(r["clips"]), "own_clips": len(r["own_clips"]),
                     **({"facial": r["facial"], "morphs": r["morphs"]} if r.get("facial")
                        else {}),
                     **({"eyes": r["eyes"], "eyeballs": r["eyeballs"]} if r.get("eyes")
                        else {}),
                     **({"procedural": r["procedural"],
                         "procedural_bones": r["procedural_bones"]} if r.get("procedural")
                        else {}),
                     **({"blends": r["blends"], "blend_grids": r["blend_grids"]}
                        if r.get("blends") else {})}
                 for s, r in manifest["npcs"].items()},
        "banks": {s: {"glb": r["glb"], "model": r["model"], "clips": len(r["clips"]),
                      **({"blends": r["blends"], "blend_grids": r["blend_grids"]}
                         if r.get("blends") else {})}
                  for s, r in manifest["banks"].items()},
        # 12.1 — a choreo scene's anim-set model key -> the per-bone-root banks it was split
        # into. The runtime resolves (BaseAnim/MaleAnim/FemaleAnim, the actor's bonerename
        # source) through this to a bank stem in `banks` above.
        "cinematics": manifest.get("cinematics", {}),
        # v4 — skeletal prop models selected by prop_dynamic. Version 3 readers see no field;
        # version 4 readers get the GLB plus the exact baked clip inventory.
        #
        # v6 — `clips` carries the model's own sequence table in DECLARATION ORDER, with the
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
            clips[label] = [owner_i[owner], act_i[act], meta["weight"], meta["flags"],
                            meta["frames"], meta["fps"]]
        path = os.path.join(CLIPS_DIR, stem + ".json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"stem": stem, "owners": owners, "activities": acts,
                       "fields": ["owner", "activity", "weight", "flags", "frames", "fps"],
                       "clips": clips}, f, separators=(",", ":"))
        total += os.path.getsize(path)
    print(f"[npc] sidecars: {INDEX} ({os.path.getsize(INDEX)/1024:.0f} KB) + "
          f"{len(manifest['npcs'])} clip slices in {CLIPS_DIR} "
          f"({total/1e6:.1f} MB total, {total/max(1, len(manifest['npcs']))/1024:.0f} KB each)")


def main(only=None, *, index=None, integrate=False, strict=False):
    """Export the runtime skeletal corpus.

    ``only`` selects model keys.  With ``integrate=True`` those records and any
    newly required banks are merged into the current complete manifest before
    its runtime projections are regenerated.  Without integration, targeted
    calls intentionally produce a standalone manifest and should therefore use
    an isolated output root configured by the caller.

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
    if only is None:
        from_ents = npc_models_from_ents()
        pc_models = set(pc_models_from_clandoc())
        cinematics = cinematic_models_from_ents()
        animated_props = animated_prop_models_from_ents()
        # An authored LoopSequence is not proof of motion. Drop the models whose sequences
        # are all single frames before anything tries to bake them: they keep their decoded
        # static mesh, which is what they already look like in the original game.
        still = [m for m in animated_props
                 if (r := load_mdl(m)) is not None and not has_animation(r)]
        if still:
            animated_props = [m for m in animated_props if m not in set(still)]
            print(f"[npc] {len(still)} animated-prop candidate(s) declare no multi-frame "
                  f"sequence and stay static: {', '.join(_basename_stem(m) for m in still)}")
        print(f"[npc] seed: {len(from_ents)} npc model(s) from the exported .ents + "
              f"{len(pc_models)} player body model(s) from {CLANDOC} + "
              f"{len(cinematics)} cinematic anim-set(s) + "
              f"{len(animated_props)} animated prop model(s)")
        seed = sorted(set(from_ents) | pc_models)
    else:
        seed, pc_models, cinematics, animated_props = only, set(), [], []

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
    if not npcs and not cinematics and not animated_props:
        message = "[npc] no character, cinematic, or animated-prop models to export"
        print(message)
        if strict:
            raise RuntimeError(message)
        return

    # NPC stems are basenames (the console/`elysium.npc.load` ergonomic); fall back to a
    # path-safe stem for any basename two different models share.
    from collections import Counter
    counts = Counter(_basename_stem(m) for m in npcs)
    npc_stem = {m: (_basename_stem(m) if counts[_basename_stem(m)] == 1 else bank_stem(m))
                for m in npcs}

    # Resolve every NPC's include tree once: its own clips, plus which bank owns each shared
    # clip (first model in tree order to define a label owns it).
    os.makedirs(NPC_DIR, exist_ok=True)
    print(f"[npc] resolving include trees for {len(npcs)} NPC model(s) ...", flush=True)
    npc_records = {}
    banks_needed = {}  # bank model_key -> bank_stem
    for m in npcs:
        tree = S.resolve_tree(load_mdl, m)
        assigned, clips = {}, {}
        for key, d in tree:
            is_npc = (key == m)
            stem = npc_stem[m] if is_npc else bank_stem(key)
            seqs = S.local_sequences(d)
            if not is_npc and seqs:
                banks_needed.setdefault(key, stem)
            for c in seqs:
                ll = c.label.lower()
                if ll in assigned:
                    continue
                assigned[ll] = stem
                clips[c.label] = stem
        npc_records[m] = clips

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
                "glb": info["glb"], "model": info["model"],
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
                "glb": info["glb"], "model": info["model"],
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
    anorms = mdl_gltf.load_anorms()
    print(f"[npc] exporting {len(npcs)} NPC mesh glb(s) -> {NPC_DIR}/ ...", flush=True)
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
            "glb": info["glb"], "model": info["model"], "bones": info["bones"],
            "split_bones": info["split_bones"],
            "clips": npc_records.get(m, {}),
            "own_clips": {c.label: _clip_meta(c) for c in info["clips"]},
            **write_facial(info["stem"], info["model"], info["facial"]),
            **write_eyes(info["stem"], info["model"], info["eyes"]),
            **write_procedural(info["stem"], info["model"], info["procedural"]),
            **write_blends(info["stem"], info["model"], info["blends"]),
        }
        procedural_faults.extend((info["stem"], f) for f in info["procedural_faults"])
        eye_faults.extend((info["stem"], f) for f in info["eye_faults"])

    # Skeletal prop GLBs are intentionally separate from NPCs: prop_dynamic selects them only when
    # this index promises an animated representation, while ordinary props retain their baked
    # Nanite/static path. Props own their clips directly; no NPC include-bank vocabulary is needed.
    animated_prop_index = {}
    static_fallbacks = static_model_fallbacks_from_ents()
    if animated_props:
        os.makedirs(ANIMATED_PROP_DIR, exist_ok=True)
        print(f"[npc] exporting {len(animated_props)} animated prop model(s) -> "
              f"{ANIMATED_PROP_DIR}/ ...", flush=True)
    prop_counts = Counter(_basename_stem(m) for m in animated_props)
    for model in animated_props:
        if load_mdl(model) is None:
            print(f"  ! animated prop {model}: no .mdl in install - skipped")
            failures.append(f"missing animated prop model: {model}")
            continue
        stem = (_basename_stem(model) if prop_counts[_basename_stem(model)] == 1
                else bank_stem(model))
        try:
            info = mdl_gltf.export_npc(idx, model, ANIMATED_PROP_DIR, stem, anorms=None,
                                       measure_extents=True)
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
                print(f"  !! animated prop {stem} FAILED: {e}")
                failures.append(f"animated prop {stem}: {e}")
            continue
        # A cinematic prop is animated in place: nothing moves its component, so its render
        # bound has to come from the clip rather than from the reference pose the mesh carries.
        prop_clips = {}
        for c in info["clips"]:
            measured = info["clip_extents"].get(c.label, 0.0)
            if authored_radius_m(c) < measured - 1e-4:
                print(f"  ! {stem}: clip '{c.label}' declares r={authored_radius_m(c):.3f} m "
                      f"but bakes out to {measured:.3f} m - using the baked extent")
            prop_clips[c.label] = _clip_meta(c, clip_bounds_radius_m(c, measured))
        animated_prop_index[stem] = {
            "glb": "animated_props/" + info["glb"],
            "model": info["model"],
            "bones": info["bones"],
            "split_bones": info["split_bones"],
            "clips": prop_clips,
            **write_procedural(stem, info["model"], info["procedural"], "animated_props"),
            **write_blends(stem, info["model"], info["blends"], "animated_props"),
        }
        procedural_faults.extend((stem, f) for f in info["procedural_faults"])

    # Reconcile: a sequence the include tree advertises but whose owner failed to bake (empty
    # tracks, or a bank export that raised) must not appear as resolvable. Filtering here is
    # what lets the runtime treat a hit in `clips` as a promise the glb can answer.
    bank_baked = {stem: set(rec["clips"]) for stem, rec in bank_index.items()}
    dropped = 0
    for stem, rec in npc_index.items():
        own = set(rec["own_clips"])
        # Resolve exactly the way the runtime does -- own stem first, then the bank index --
        # so the two can never disagree even if a bank and an NPC ever share a stem.
        keep = {lbl: owner for lbl, owner in rec["clips"].items()
                if lbl in (own if owner == stem else bank_baked.get(owner, ()))}
        dropped += len(rec["clips"]) - len(keep)
        rec["clips"] = keep
    if dropped:
        print(f"[npc] dropped {dropped} unresolvable clip refs (owner did not bake them)")

    manifest = {
        "manifest_version": MANIFEST_VERSION,
        "note": "npcs[stem].clips maps a clip label -> the stem that OWNS it. If that stem is a "
                "key in `banks`, load banks/<stem>.glb and retarget onto the NPC skeletal mesh "
                "by bone name; otherwise it is the NPC's own glb. Per-clip metadata "
                "(activity/weight/flags/frames/fps) lives once on the owner: banks[owner].clips "
                "for a bank, npcs[stem].own_clips for the NPC's own. Every label in `clips` is "
                "backed by a baked animation in the owner's glb. npcs[stem].facial, when "
                "present, names the flex rig driving that glb's morph targets.",
        "npcs": npc_index,
        "banks": bank_index,
        "cinematics": cinematic_index,
        "animated_props": animated_prop_index,
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
        for key in ("npcs", "banks", "cinematics", "animated_props"):
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
    bank_bytes = sum(os.path.getsize(os.path.join(NPC_DIR, b["glb"]))
                     for b in bank_index.values() if os.path.exists(os.path.join(NPC_DIR, b["glb"])))
    npc_bytes = sum(os.path.getsize(os.path.join(NPC_DIR, n["glb"]))
                    for n in npc_index.values() if os.path.exists(os.path.join(NPC_DIR, n["glb"])))
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
    gridded = [r for r in (*npc_index.values(), *bank_index.values(),
                           *animated_prop_index.values()) if r.get("blends")]
    print(f"[npc] blend grids: {len(gridded)} model(s) author one, "
          f"{sum(r['blend_grids'] for r in gridded)} grids -> {BLENDS_DIR}/")
    print(f"[npc] size: banks {bank_bytes/1e6:.0f} MB (shared) + meshes {npc_bytes/1e6:.0f} MB, "
          f"manifest {os.path.getsize(MANIFEST)/1e6:.1f} MB")
    if warnings:
        print(
            f"[npc] warnings: {len(warnings)} known source issue(s) recorded in "
            f"{MANIFEST} and {INDEX}"
        )
def reindex() -> None:
    """Re-derive runtime sidecars from the canonical manifest."""

    with open(MANIFEST, encoding="utf-8") as f:
        write_sidecars(json.load(f))
