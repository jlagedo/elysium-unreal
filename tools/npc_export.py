"""Batch character export: mesh glbs + shared animation-bank glbs + a resolution manifest.

Two seeds, one product. The maps' own `npc_*` model references cover every NPC; the **player
bodies** are named by no entity at all, so they come from `vdata/system/clandoc000.txt`
instead (56 distinct models over its 84 `M_Body0..5`/`F_Body0..5` slots, roadmap PL13). Both
are the same v2531 skeletal format, so both go through the same decode: each model's
include-model tree (`docs/animation_and_movers.md` A.7) resolves into shared banks, and the
run writes under `out/npc/`:

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
so it is read at export time and never committed. No model in the install carries eyeball
data, so there is none to export. Format: `docs/facial_animation.md`.

CLI:
  python tools/npc_export.py                 # both seeds: the maps' npc_* models + the PC bodies
  python tools/npc_export.py <model.mdl ...> # only the named models (+ their banks)
`export_all.py --npc` calls `main()` at the end of a run.
"""
import glob
import json
import os
import re
import sys

import install
import kv
import mdl
import mdl_gltf
import mdl_skel as S

OUT = "out"
NPC_DIR = os.path.join(OUT, "npc")
CLANDOC = os.path.join("vdata", "system", "clandoc000.txt")
_BODY_SLOT = re.compile(r"^([mf])_body(\d+)$")
MANIFEST = os.path.join(NPC_DIR, "npc_manifest.json")
INDEX = os.path.join(NPC_DIR, "npc_index.json")
CLIPS_DIR = os.path.join(NPC_DIR, "clips")
FACIAL_DIR = os.path.join(NPC_DIR, "facial")
ANIMATED_PROP_DIR = os.path.join(NPC_DIR, "animated_props")
MANIFEST_VERSION = 4


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


def cinematic_models_from_ents(out_root=OUT):
    """Every distinct anim-set `.mdl` the exported maps' `logic_choreographed_scene` entities
    name (`BaseAnim` / `MaleAnim` / `FemaleAnim`).

    Neither of the other two seeds reaches these: no `npc_*` entity carries a cinematic model and
    the rulebook never names one, so the whole `models/cinematic/**` tree (106 models in the
    merged install) stayed in the VPKs. They are the whole-cast performances a scene's
    `sequence "entire_scene"` plays -- see `docs/choreographed_scenes.md`.
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
    through, so the exported set cannot drift from it: each `ClanData.General` carries
    `M_Body0..5`/`F_Body0..5`, 7 playable clans x 2 sexes x 6 armour slots whose top two
    repeat the tier-3 suit, so 84 slots resolve to **56 distinct models**. The multiplayer and
    `unused*` templates repeat the same paths and the un-indexed `M_Body`/`F_Body` of the
    human templates name NPC models, so only the indexed keys are read.

    Requires PL5b's `out/vdata/` mirror; returns `[]` (with a note) when it is absent."""
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


def _clip_meta(c):
    """One baked clip's engine-facing selection keys (`docs/animation_and_movers.md` A.3).

    `activity` is the `ACT_*` literal the engine selects on (empty on a layer/plumbing
    sequence), `weight` its weighted-random share among the clips sharing that activity, and
    `flags` the studio sequence bits. Stored once per owning stem, not per NPC that resolves
    it -- 157 characters x ~1,400 resolved clips would be two orders of magnitude more rows."""
    return {"activity": c.activity, "weight": c.actweight, "flags": c.flags,
            "frames": c.frames, "fps": round(c.fps, 4)}


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
                           "docs/facial_animation.md.",
                   **rig}, f, separators=(",", ":"))
    return {"facial": "facial/" + os.path.basename(path), "morphs": len(rig["morphs"])}


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
        "note": "counts only; a clip vocabulary lives in clips/<stem>.json and a flex rig in "
                "facial/<stem>.json. Both paths, like the bank glb paths, are relative to "
                "this file's directory.",
        "npcs": {s: {"glb": r["glb"], "model": r["model"], "bones": r["bones"],
                     "clips": len(r["clips"]), "own_clips": len(r["own_clips"]),
                     **({"facial": r["facial"], "morphs": r["morphs"]} if r.get("facial")
                        else {})}
                 for s, r in manifest["npcs"].items()},
        "banks": {s: {"glb": r["glb"], "model": r["model"], "clips": len(r["clips"])}
                  for s, r in manifest["banks"].items()},
        # 12.1 — a choreo scene's anim-set model key -> the per-bone-root banks it was split
        # into. The runtime resolves (BaseAnim/MaleAnim/FemaleAnim, the actor's bonerename
        # source) through this to a bank stem in `banks` above.
        "cinematics": manifest.get("cinematics", {}),
        # v4 — skeletal prop models selected by prop_dynamic. Version 3 readers see no field;
        # version 4 readers get the GLB plus the exact baked clip inventory.
        "animated_props": {
            stem: {"glb": rec["glb"], "model": rec["model"],
                   "bones": rec.get("bones", 0), "clips": sorted(rec.get("clips", {}))}
            for stem, rec in manifest.get("animated_props", {}).items()
        },
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


def main(only=None):
    idx = install.build_index()
    load_mdl = lambda k: (r if (r := install.read(idx, (k[:-4] if k.lower().endswith(".mdl")
                                                         else k) + ".mdl")) else None)

    # The default seed is two lists, because the two halves are referenced differently: NPCs by
    # the maps' own entities, the player bodies only by the rulebook (PL13).
    if only is None:
        from_ents = npc_models_from_ents()
        pc_models = set(pc_models_from_clandoc())
        cinematics = cinematic_models_from_ents()
        animated_props = animated_prop_models_from_ents()
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
        print(f"  ! {m}: no .mdl in install - skipped")
    if not npcs and not cinematics and not animated_props:
        print("[npc] no character, cinematic, or animated-prop models to export")
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
        except Exception as e:
            print(f"  !! bank {banks_needed[key]} FAILED: {e}")
            continue
        if info:
            bank_index[info["stem"]] = {
                "glb": info["glb"], "model": info["model"],
                "clips": {c.label: _clip_meta(c) for c in info["clips"]},
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
            continue
        stem = bank_stem(key)
        try:
            banks = mdl_gltf.export_cinematic(idx, key, NPC_DIR, stem)
        except Exception as e:
            print(f"  !! cinematic {stem} FAILED: {e}")
            continue
        if not banks:
            continue
        roots = []
        for info in banks:
            bank_index[info["stem"]] = {
                "glb": info["glb"], "model": info["model"],
                "clips": {c.label: _clip_meta(c) for c in info["clips"]},
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
    for m in npcs:
        try:
            info = mdl_gltf.export_npc(idx, m, NPC_DIR, npc_stem[m], anorms)
        except Exception as e:
            print(f"  !! npc {npc_stem[m]} FAILED: {e}")
            continue
        npc_index[info["stem"]] = {
            "glb": info["glb"], "model": info["model"], "bones": info["bones"],
            "clips": npc_records.get(m, {}),
            "own_clips": {c.label: _clip_meta(c) for c in info["clips"]},
            **write_facial(info["stem"], info["model"], info["facial"]),
        }

    # Skeletal prop GLBs are intentionally separate from NPCs: prop_dynamic selects them only when
    # this index promises an animated representation, while ordinary props retain their baked
    # Nanite/static path. Props own their clips directly; no NPC include-bank vocabulary is needed.
    animated_prop_index = {}
    if animated_props:
        os.makedirs(ANIMATED_PROP_DIR, exist_ok=True)
        print(f"[npc] exporting {len(animated_props)} animated prop model(s) -> "
              f"{ANIMATED_PROP_DIR}/ ...", flush=True)
    prop_counts = Counter(_basename_stem(m) for m in animated_props)
    for model in animated_props:
        if load_mdl(model) is None:
            print(f"  ! animated prop {model}: no .mdl in install - skipped")
            continue
        stem = (_basename_stem(model) if prop_counts[_basename_stem(model)] == 1
                else bank_stem(model))
        try:
            info = mdl_gltf.export_npc(idx, model, ANIMATED_PROP_DIR, stem, anorms=None)
        except Exception as e:
            print(f"  !! animated prop {stem} FAILED: {e}")
            continue
        animated_prop_index[stem] = {
            "glb": "animated_props/" + info["glb"],
            "model": info["model"],
            "bones": info["bones"],
            "clips": {c.label: _clip_meta(c) for c in info["clips"]},
        }

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
    }
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
    print(f"[npc] size: banks {bank_bytes/1e6:.0f} MB (shared) + meshes {npc_bytes/1e6:.0f} MB, "
          f"manifest {os.path.getsize(MANIFEST)/1e6:.1f} MB")


if __name__ == "__main__":
    if "--reindex" in sys.argv:
        # Re-derive the runtime sidecars from the manifest already on disk. Everything they
        # carry is a projection of it, so this needs no install and no glb re-bake.
        with open(MANIFEST, encoding="utf-8") as f:
            write_sidecars(json.load(f))
        sys.exit(0)
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    main(only=args or None)
