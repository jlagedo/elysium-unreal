"""A whole-corpus bank partition, independent of the selected import slice."""
from __future__ import annotations

import hashlib
import json
import zlib

from elysium_pipeline.asset_paths import corpus_path
from elysium_pipeline.formats import eskm, mdl_skel
from elysium_pipeline.skeletal_stage import payload, sequences, cinematics
from elysium_pipeline.skeletal_stage.unit import ModelUnit


def _order_key(asset_id, root):
    # Preserve the measured first-fit partition's traversal order. This fold is ONLY an order
    # key, never an address. Identity/root break a tie instead of dropping either source.
    key = asset_id.removeprefix("vtmb:model:")
    folded = "".join(c if c.isalnum() or c in "._-" else "_" for c in key.lower())
    return folded + ("__" + root.lower() if root else ""), asset_id, root


def build(units, cinematic_sets):
    """Return the partition plus skeleton-only payloads needed to build its complete families.

    Reads every bank candidate, even on a scoped body import. No animation BIN is read: the
    same root/subset rules produce SKEL from joint metadata, and ownership masks prove whether
    a bank has any playable contribution to that root.
    """
    members, trees, rig_payloads = {}, {}, {}
    for id, entry in sorted(units.items()):
        if "include-only" not in entry["identity"].get("roles", ()) and id not in cinematic_sets:
            continue
        unit = ModelUnit.metadata(entry["path"])
        if not unit.sequences:
            continue
        extra, _ = sequences.blend_clip_plan(unit, unit.sequences)
        clips = unit.sequences + extra
        bindings = payload._derived_bindings(unit, unit.bones, clips, payload._cell_names(clips))
        live = {i for clip in clips + [row[0] for row in bindings] if clip.frames > 0
                for i, w in enumerate(unit.bone_weights(clip.base)) if w}
        roots = mdl_skel.cinematic_roots(unit.bones) if id in cinematic_sets else []
        slices = roots if len(roots) > 1 else [""]
        for root in slices:
            if root:
                subset = [b for b in unit.bones if (mdl_skel._bone_root(b.name) or "").lower() == root.lower()]
                rows, order, _ = cinematics.actor_rows(subset, root)
                indices = set(order)
            else:
                rows, _, _ = payload.unreal_bones(unit.bones)
                indices = {b.index for b in unit.bones}
            if not indices.intersection(live):
                continue
            key = id.removeprefix("vtmb:model:")
            rig = f"_banks/{key}{'/' + root if root else ''}.rig.skel"
            data = payload._assemble([(b"SKEL", payload._skel_section(rows))])
            tree = {name: rows[parent][0] if parent >= 0 else "" for name, parent, _, _ in rows}
            order_key = _order_key(id, root)
            trees[order_key] = tree
            members[order_key] = {"assetId": id, "root": root, "rig": rig,
                                  "rigSha256": hashlib.sha256(data).hexdigest()}
            rig_payloads[rig] = data
    groups, assignments = [], []
    for family in eskm.rig_families(trees, list(trees)):
        ordered = [members[key] for key in family["stems"]]
        identities = sorted((m["assetId"], m["root"]) for m in ordered)
        digest = zlib.crc32(json.dumps(identities, separators=(",", ":")).encode()) & 0xffffffff
        path = corpus_path("model", "SKEL", f"Family_{digest:08x}")
        # FName comparison is case-insensitive. Keep one shape row per name and preserve the
        # exact source spelling in each member's own SKEL rather than duplicating family nodes.
        tree = {name.lower(): parent.lower() for name, parent in family["tree"].items()}
        shape = hashlib.sha256(json.dumps(sorted(tree.items()), separators=(",", ":")).encode()).hexdigest()
        group = {"skeletonAsset": path, "members": ordered, "boneCount": len(tree),
                 "tree": tree, "treeSha256": shape}
        groups.append(group)
        assignments.extend({"assetId": m["assetId"], "root": m["root"],
                            "skeletonAsset": path, "treeSha256": shape} for m in ordered)
    paths = [g["skeletonAsset"] for g in groups]
    if len(paths) != len(set(paths)):
        raise ValueError("bank-family CRC32 collision; family assets must not alias")
    return {"scope": "whole-model-corpus", "families": groups, "owners": assignments}, rig_payloads
