"""Batch NPC export: mesh glbs + shared animation-bank glbs + a resolution manifest.

Scans the exported maps for `npc_*` model references, resolves each NPC's include-model tree
(`docs/animation_and_movers.md` A.7) into shared banks, and writes under `out/npc/`:

  <npc>.glb          skinned mesh + skeleton + the NPC's own clips (its dialogue anims)
  banks/<bank>.glb   a shared bank's skeleton + all its clips, no mesh
  npc_manifest.json  per-NPC {clip -> owning-stem} resolution + a bank/mesh index

The runtime (roadmap 8.5) reads the manifest, loads a clip's owning glb once -- shared across
every NPC that uses it -- and applies it to the NPC skeletal mesh by bone name via glTFRuntime
(`LoadSkeletalAnimation(mesh, ...)` matches tracks to the ref skeleton by bone name). This is
VtMB's own virtualmodel bank-sharing in the modern-engine shape: one skeleton, many meshes, a
shared animation library keyed by bone name -- not a per-NPC monolith.

CLI:
  python tools/npc_export.py                 # every npc_* model the exported maps reference
  python tools/npc_export.py <model.mdl ...> # only the named models (+ their banks)
`export_all.py --npc` calls `main()` at the end of a run.
"""
import glob
import json
import os
import sys

import install
import mdl
import mdl_gltf
import mdl_skel as S

OUT = "out"
NPC_DIR = os.path.join(OUT, "npc")
MANIFEST = os.path.join(NPC_DIR, "npc_manifest.json")


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


def main(only=None):
    idx = install.build_index()
    load_mdl = lambda k: (r if (r := install.read(idx, (k[:-4] if k.lower().endswith(".mdl")
                                                         else k) + ".mdl")) else None)

    npcs = [m for m in (only or npc_models_from_ents()) if load_mdl(m) is not None]
    missing = [m for m in (only or []) if load_mdl(m) is None]
    for m in missing:
        print(f"  ! {m}: no .mdl in install - skipped")
    if not npcs:
        print("[npc] no NPC models to export")
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
            for label, _ab, _nf, _fps in seqs:
                ll = label.lower()
                if ll in assigned:
                    continue
                assigned[ll] = stem
                clips[label] = stem
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
            bank_index[info["stem"]] = {"glb": info["glb"], "model": info["model"],
                                        "clips": info["clips"]}

    # Export the NPC mesh glbs (mesh + skeleton + own clips).
    print(f"[npc] exporting {len(npcs)} NPC mesh glb(s) -> {NPC_DIR}/ ...", flush=True)
    npc_index = {}
    for m in npcs:
        try:
            info = mdl_gltf.export_npc(idx, m, NPC_DIR, npc_stem[m])
        except Exception as e:
            print(f"  !! npc {npc_stem[m]} FAILED: {e}")
            continue
        npc_index[info["stem"]] = {
            "glb": info["glb"], "model": info["model"], "bones": info["bones"],
            "clips": npc_records.get(m, {}),
        }

    manifest = {
        "note": "clip -> stem; if stem is a key in `banks`, load banks/<stem>.glb and retarget "
                "to the NPC skeletal mesh by bone name, else the clip is in the NPC's own glb",
        "npcs": npc_index,
        "banks": bank_index,
    }
    with open(MANIFEST, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)

    n_clips = sum(len(r["clips"]) for r in npc_index.values())
    bank_bytes = sum(os.path.getsize(os.path.join(NPC_DIR, b["glb"]))
                     for b in bank_index.values() if os.path.exists(os.path.join(NPC_DIR, b["glb"])))
    npc_bytes = sum(os.path.getsize(os.path.join(NPC_DIR, n["glb"]))
                    for n in npc_index.values() if os.path.exists(os.path.join(NPC_DIR, n["glb"])))
    print(f"[npc] done: {len(npc_index)} NPCs, {len(bank_index)} banks, "
          f"{n_clips} resolved clip refs -> {MANIFEST}")
    print(f"[npc] size: banks {bank_bytes/1e6:.0f} MB (shared) + meshes {npc_bytes/1e6:.0f} MB")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    main(only=args or None)
