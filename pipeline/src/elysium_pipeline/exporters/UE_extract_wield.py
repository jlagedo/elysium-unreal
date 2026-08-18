"""Export the wield-model corpus: `wield_corpus.json`'s decisions serialised to disk.

`wield_corpus.py` owns every decision this module states -- which bone a model mounts on, which
binding mode that implies, which local pose the bake must carry, and whether the three properties
the static socket collapse rests on hold for a given file. This module re-derives none of it: it
walks `vdata/items`, loads each real model once, calls the decision functions, emits one `.eskm`
per model through `UE_mdl_skeletal.write_model`, decodes the texture channels no other lane
reaches, and writes the join as one deterministic manifest. `docs/project/plans/pipeline.md` (PL20)
and `docs/architecture/wielded-weapon-integration.md` are the contract; the measured census this
module asserts is theirs, not derived here.

Usage:
  uv run elysium export bundle items            # write the wield-model manifest (with ground_models.json)
"""
from __future__ import annotations

import json
import os
import sys

from elysium_pipeline import wield_corpus as W
from elysium_pipeline.exporters.UE_mdl_skeletal import write_model, unreal_bones, _reparent_local
from elysium_pipeline.formats import bsp, eskm as ESKM, install, mdl, mdl_skel
from elysium_pipeline.formats.tex_to_png import decode as decode_texture
from elysium_pipeline.paths import export_root

#: The measured census `docs/vtmb/wielded_weapons.md` and `wielded-weapon-integration.md` state.
#: Asserted, never adjusted -- a mismatch here means the *install* or the *decision layer*
#: disagrees with the documented corpus, and that is what the failure has to say.
EXPECTED_CENSUS = {
    "definitions": 244,
    "definitions_naming_a_wield_model": 192,
    "rows": 488,
    "null_rows": 296,
    "empty_rows": 104,
    "absent_rows": 8,
    "absent_paths": 2,
    "real_rows": 80,
    "real_models": 67,
    "texture_union": 89,
}

EXPECTED_BINDINGS = {
    "socket_hand": 37,
    "socket_prop": 20,
    "leader_pose": 2,
    "copy_pose": 6,
    "projectile": 2,
}

#: Float32 round-trip tolerances for the frame-0 reference-pose readback assertion: how far a
#: value can drift between `bake_pose`'s float64 answer and the float32 the container stores and
#: this reads back, and nothing more. The rotation figure is not a guess -- `quat_angle`'s
#: `acos` blows up near a dot product of 1, so a same-precision float32 quaternion a hair off its
#: float64 source (measured up to ~0.02deg on the corpus) reads as a larger angle than the raw
#: component error suggests; `wield_corpus.ROT_EPS` (an authoring tolerance, not a rounding one)
#: happens to clear it with headroom, so this reuses that figure rather than inventing a second.
_REF_POSE_POS_TOL = 1e-3
_REF_POSE_ROT_TOL = W.ROT_EPS


# ------------------------------------------------------------------------- row classification

def _load_all_real_models(idx, rows):
    """`{normalized model key: (d, v) or None}` for every non-null, non-empty model any row names.

    Loaded once per distinct key regardless of how many `(classname, sex)` rows share it -- 80 real
    rows resolve to 67 distinct models. `None` records a model the install lacks, which is an
    authored possibility (`docs/vtmb/wielded_weapons.md` section 6), never fatal.
    """
    cache = {}
    for row in rows:
        model = row.model
        if not model or W.is_null(model) or model in cache:
            continue
        cache[model] = mdl.load(idx, model)
    return cache


def _row_kind(model, loaded):
    """One row's model value classified into the manifest's four `kind`s.

    `real`/`null`/`empty`/`absent` are four distinct authored possibilities, not three plus a
    sentinel -- `is_null` and an empty string are different authored values, and `loaded` (from
    `_load_all_real_models`) is what tells a present-but-uninstalled path apart from a decodable
    one."""
    if not model:
        return "empty"
    if W.is_null(model):
        return "null"
    return "real" if loaded.get(model) is not None else "absent"


# ------------------------------------------------------------------------------ binding override

def _binding_for(cls, motion_check):
    """`classify`'s topology, overridden by the one fact only a decoded clip can supply.

    `classify` decides socket_hand/socket_prop from bone parentage alone, which is exactly right
    for a rigid sub-rig but says nothing about whether the sub-rig actually holds still --
    `check_motion` is that measurement. A single-mount model whose own clip moves below the
    collapse root is not a rigid socket transform in retail either, so it ships `copy_pose` instead
    (`wielded-weapon-integration.md`'s frame-constancy note). The non-socket bindings
    (`leader_pose`/`copy_pose`/`projectile`) come out of `classify` already decided against the
    body corpus and are left alone -- `check_motion` is vacuously true for them (no collapse bone
    to measure), so this can never re-route them.
    """
    if cls.binding in ("socket_hand", "socket_prop") and not motion_check.ok:
        return "copy_pose"
    return cls.binding


# ------------------------------------------------------------------------------- reference pose

def _assert_ref_pose(model_key, path, bones, pose):
    """The just-written `.eskm`'s SKEL section equals `bake_pose`'s answer, on every bone.

    This is PL20's load-bearing guarantee (`wielded-weapon-integration.md` "Verification"): the
    bake's reference pose is the model's own clip at frame 0, not its container bind, and nothing
    downstream re-derives that -- so what the container actually holds has to be checked against
    what `bake_pose` computed, by reading the file back rather than trusting the write.

    Matched by emitted INDEX (`unreal_bones`'s own `bone_map`, recomputed here from `bones` rather
    than threaded out of `write_model`, so this checks what the file holds against a fresh
    resolution rather than trusting the write's internal state) -- never by name. A corpus body
    can carry two bones sharing one literal name (`brian`'s two `lower_teeth`), which a
    name-keyed lookup would silently collapse onto whichever row happened to be read last.

    `w_f_severed_arm` is the corpus's one multi-rooted model (`bake_wield.py`): `unreal_bones`
    resolves its fork onto one of its own bones rather than a spliced synthetic one, so a
    reparented stray's row holds a LOCAL transform relative to the CHOSEN root, not `pose.locals`'
    model-space entry for it directly (`reparented`, `unreal_bones`'s third return). Such a bone's
    expected local is recomposed with the same `_reparent_local` math the write used, from
    `pose.locals` at both the stray and the chosen root, rather than compared to `pose.locals`
    verbatim.
    """
    rows = ESKM.bone_locals(ESKM.read(path))
    _emitted_rows, bone_map, reparented = unreal_bones(bones)
    mismatches = []
    if len(rows) != len(bone_map):
        mismatches.append(
            f"container carries {len(rows)} bone(s), unreal_bones expects {len(bone_map)}")
    else:
        for index, bone in enumerate(bones):
            slot = bone_map[index]
            name, _parent, got_pos, got_quat = rows[slot]
            if name != bone.name:
                mismatches.append(
                    f"{bone.name}: emitted slot {slot} holds {name!r}, not this bone's own row")
                continue
            chosen = reparented.get(index)
            if chosen is None:
                want_pos = bsp.source_to_unreal(*pose.locals[index][0])
                want_quat = bsp.source_quat_to_unreal(*pose.locals[index][1])
            else:
                root_pos, root_quat = pose.locals[chosen]
                stray_pos, stray_quat = pose.locals[index]
                local_pos, local_quat = _reparent_local(root_pos, root_quat, stray_pos, stray_quat)
                want_pos = bsp.source_to_unreal(*local_pos)
                want_quat = bsp.source_quat_to_unreal(*local_quat)
            dp = max(abs(a - b) for a, b in zip(want_pos, got_pos))
            dr = W.quat_angle(want_quat, got_quat)
            if dp > _REF_POSE_POS_TOL or dr > _REF_POSE_ROT_TOL:
                mismatches.append(
                    f"{bone.name}: pos delta {dp:.5f}cm, rot delta {dr:.4f}deg "
                    f"(bake_pose wants pos={want_pos} quat={want_quat}, "
                    f"container holds pos={got_pos} quat={got_quat})")
    if mismatches:
        raise AssertionError(
            f"[wield] {model_key}: emitted reference pose disagrees with bake_pose on "
            f"{len(mismatches)} bone(s):\n  " + "\n  ".join(mismatches))


# ---------------------------------------------------------------------------------- textures

def _register_texture(idx, tex_dir, key, texture_index):
    """Every distinct install `materials/<key>.tth`+`.ttz` the corpus's albedo/envmask/bump
    fields name, decoded once and recorded under one flat key namespace shared by all three
    channels -- **not** one entry per `(channel, key)`. Channel role is manifest metadata carried
    on each material row (`wielded-weapon-integration.md`'s "render semantics ride the manifest"
    split extends to texture closure too): a handful of keys serve double duty, e.g.
    `models/weapons/fire_axe/fireaxe` is one material's albedo and another's envmask, and it is
    ONE install file either way. Texture closure's job is making sure that file decodes to some
    PNG; which material graph slot a bake wires it into is decided from the role field, never from
    a second copy of the same texture.

    A key `write_model` already decoded for a model's own albedo (`model_materials`' rows, which
    is every row but the skin-family override case) is referenced by the same naming
    (`mdl.sanitize(key) + ".png"`) rather than re-decoded -- existence-checked, since a manifest
    entry pointing at a file that turned out not to be there would be a silent lie the next stage
    discovers only at import.

    A decode failure is a console warning and the key is left out of `texture_index`, never fatal:
    PL20's one known corpus defect is the handleclaws null *albedo*, already recorded as a failure
    by `wield_corpus._resolve_material_row`; nothing else in the shipped set is expected to fail,
    but a genuinely broken `.ttz` must not stop the other 88 keys' closure.
    """
    if not key:
        return
    key = key.lower()
    if key in texture_index:
        return
    filename = mdl.sanitize(key) + ".png"
    path = os.path.join(tex_dir, filename)
    if not os.path.isfile(path):
        tth = install.read(idx, f"materials/{key}.tth")
        ttz = install.read(idx, f"materials/{key}.ttz")
        if not (tth and ttz):
            print(f"[wield] texture files missing for {key!r}", flush=True)
            return
        try:
            decode_texture(tth, ttz).convert("RGBA").save(path)
        except Exception as exc:                   # noqa: BLE001 - reported, never hidden
            print(f"[wield] texture decode failed for {key!r}: "
                  f"{type(exc).__name__}: {exc}", flush=True)
            return
    texture_index[key] = f"tex/{filename}"


# --------------------------------------------------------------------------------- serialisation

def _to_json(value):
    """Recursively turn a tuple/frozenset-bearing decision-layer value into plain JSON shapes."""
    if isinstance(value, (tuple, list)):
        return [_to_json(v) for v in value]
    if isinstance(value, frozenset):
        return sorted(value)
    return value


def _serialize_check(check):
    return {"ok": check.ok, "detail": _to_json(check.detail)}


def _serialize_material(row):
    return {"name": row.name, "albedo": row.albedo, "envmask": row.envmask, "bump": row.bump,
            "flags": sorted(row.flags), "failure": row.failure}


def _serialize_skin_override(row):
    return {"family": row.family, "slot": row.slot, "material": row.material,
            "albedo": row.albedo, "envmask": row.envmask, "bump": row.bump,
            "flags": sorted(row.flags), "failure": row.failure}


def _serialize_motion(rows):
    """Non-`Bip01*` bones only. The wearer overwrites the arm chain end to end, so recording it
    would bury the one signal the manifest exists to carry -- the sub-rig's own motion."""
    return [{"name": row.name, "max_pos": row.max_pos, "max_rot": row.max_rot,
             "skinned": row.skinned}
            for row in rows if not row.name.lower().startswith("bip01")]


# -------------------------------------------------------------------------------------- census

def _assert_census(census):
    """Fail loudly, naming every drifted count, rather than adjusting a number to make it pass.

    These are measured facts about the shipped install (`docs/vtmb/wielded_weapons.md`,
    `wielded-weapon-integration.md`), not invariants this module derives -- a mismatch means the
    install or the decision layer disagrees with the documented corpus, and that is exactly what
    should stop the export.
    """
    problems = [f"{key}: expected {EXPECTED_CENSUS[key]}, got {census[key]}"
                for key in EXPECTED_CENSUS if census[key] != EXPECTED_CENSUS[key]]
    if census["bindings"] != EXPECTED_BINDINGS:
        problems.append(f"bindings: expected {EXPECTED_BINDINGS}, got {census['bindings']}")
    if problems:
        raise AssertionError(
            "[wield] census drifted from the documented corpus:\n  " + "\n  ".join(problems))


# ------------------------------------------------------------------------------------------ main

def main(index=None, force=False):
    """Write the wield-model manifest and every real model's `.eskm`, into ``items/wield/``
    under the export root.

    `force` is accepted for interface parity with the sibling exporters; it is not read here
    because `UE_mdl_skeletal._write_container` already skips a byte-identical rewrite on content,
    which is the only staleness question a re-run needs answered.
    """
    idx = index if index is not None else install.build_index()
    root = export_root()
    out_dir = W.wield_dir(root)
    tex_dir = os.path.join(os.fspath(out_dir), "tex")
    os.makedirs(tex_dir, exist_ok=True)

    rows = W.wield_rows(idx)
    npc_carried = W.npc_carried(idx)
    bodies = W.body_index(idx)
    loaded = _load_all_real_models(idx, rows)

    # --- rows: the item join, one entry per classname, sexed sub-rows keyed by kind -------------
    row_out = {}
    for row in rows:
        entry = row_out.setdefault(row.classname, {
            "anim_prefix": row.anim_prefix,
            "shows_view_model": row.shows_view_model,
            "camera_class": row.camera_class,
            "cant_be_last": row.cant_be_last,
            "discipline_tgt": row.discipline_tgt,
            "reload_single": row.reload_single,
            "npc_carried": row.classname.lower() in npc_carried,
        })
        kind = _row_kind(row.model, loaded)
        entry[row.sex] = {"source": row.model, "stem": W.stem(row.model), "kind": kind}

    # No definition authors one sex without the other (`docs/vtmb/wielded_weapons.md` section 6);
    # a violation here is a decision the doc does not know about and has to stop the export rather
    # than silently disagree with it.
    for classname, entry in row_out.items():
        if ("f" in entry) != ("m" in entry) or (entry.get("f", {}).get("kind") == "empty") != (
                entry.get("m", {}).get("kind") == "empty"):
            raise AssertionError(
                f"[wield] {classname}: names a wield model for one sex only, "
                "which the corpus states never happens")

    # --- models: one real model at a time, all sharing out_dir/tex_dir/texture_index ------------
    real_keys = sorted(m for m, data in loaded.items() if data is not None)
    absent_keys = sorted(m for m, data in loaded.items() if data is None)
    model_out = {}
    texture_index = {}
    binding_counts = {}

    for model_key in real_keys:
        d, v = loaded[model_key]
        bones = mdl_skel.read_bones(d)
        skinned = W.skinned_bones(d, v)
        cls = W.classify(d, v, bodies=bodies)
        subtree_check = W.check_subtree(bones, skinned, cls)
        collapse_check = W.check_collapse(bones, skinned, cls)
        motion_check = W.check_motion(d, bones, cls)
        binding = _binding_for(cls, motion_check)
        binding_counts[binding] = binding_counts.get(binding, 0) + 1

        pose = W.bake_pose(d, bones)
        stem = W.stem(model_key)
        if stem in model_out:
            raise AssertionError(
                f"[wield] {model_key}: stem {stem!r} collides with an earlier model's -- "
                "the manifest is keyed on it and would lose one")

        write_model(idx, model_key, os.fspath(out_dir), stem=stem, ref_pose=list(pose.locals))
        eskm_path = os.path.join(os.fspath(out_dir), stem + ".eskm")
        _assert_ref_pose(model_key, eskm_path, bones, pose)

        materials = W.model_materials(d, v, idx)
        skins = W.skin_families(d, idx)
        for material_row in materials:
            _register_texture(idx, tex_dir, material_row.albedo, texture_index)
            _register_texture(idx, tex_dir, material_row.envmask, texture_index)
            _register_texture(idx, tex_dir, material_row.bump, texture_index)
        for skin_row in skins:
            _register_texture(idx, tex_dir, skin_row.albedo, texture_index)
            _register_texture(idx, tex_dir, skin_row.envmask, texture_index)
            _register_texture(idx, tex_dir, skin_row.bump, texture_index)

        on_body = (W.on_body_scope(idx, cls.mount_bone, bodies=bodies)
                  if cls.mount_bone else None)
        clips = [{"label": seq.label, "frames": seq.frames, "activity": seq.activity}
                 for seq in mdl_skel.local_sequences(d)]
        mount_bind = ({"pos": list(cls.mount_bind[0]), "quat": list(cls.mount_bind[1])}
                      if cls.mount_bind else None)

        model_out[stem] = {
            "source": model_key,
            "eskm": os.path.relpath(eskm_path, os.fspath(root)).replace(os.sep, "/"),
            "binding": binding,
            "mount_bone": cls.mount_bone,
            "hand_bone": cls.hand_bone,
            "collapse_bone": cls.collapse_bone,
            "grip": cls.grip,
            "mount_bind": mount_bind,
            "bone_count": cls.bone_count,
            "skinned_bone_count": cls.skinned_bone_count,
            "on_body": on_body,
            "ref_pose": "clip_frame0" if pose.source == "clip" else "bind",
            "clips": clips,
            "motion": _serialize_motion(W.bone_motion(d, v, bones)),
            "materials": [_serialize_material(row) for row in materials],
            "skin_families": [_serialize_skin_override(row) for row in skins],
            "checks": {"subtree": _serialize_check(subtree_check),
                      "collapse": _serialize_check(collapse_check),
                      "motion": _serialize_check(motion_check)},
            "anomalies": list(cls.anomalies),
        }

    # --- census, asserted loud -------------------------------------------------------------------
    kinds = [_row_kind(row.model, loaded) for row in rows]
    census = {
        "definitions": len(row_out),
        "definitions_naming_a_wield_model": sum(
            1 for entry in row_out.values() if entry.get("f", {}).get("kind") != "empty"),
        "rows": len(rows),
        "null_rows": kinds.count("null"),
        "empty_rows": kinds.count("empty"),
        "absent_rows": kinds.count("absent"),
        "absent_paths": len(absent_keys),
        "real_rows": kinds.count("real"),
        "real_models": len(model_out),
        "bindings": binding_counts,
        "texture_union": len(texture_index),
    }
    _assert_census(census)

    manifest = {
        "schema": W.MANIFEST_SCHEMA,
        "version": W.VERSION,
        "rows": {classname.lower(): entry for classname, entry in row_out.items()},
        "models": model_out,
        "textures": texture_index,
        "census": census,
    }
    manifest_path = W.manifest_path(root)
    os.makedirs(os.path.dirname(manifest_path), exist_ok=True)
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)
        handle.write("\n")

    print(f"[wield] {census['rows']} rows over {census['definitions']} definitions "
          f"({census['null_rows']} null / {census['empty_rows']} empty / "
          f"{census['absent_rows']} absent / {census['real_rows']} real over "
          f"{census['real_models']} models), {census['texture_union']} textures, "
          f"bindings {binding_counts} -> {W.ROOT}/{W.MANIFEST}", flush=True)
    for model in absent_keys:
        print(f"  ! {model}: not in the install", flush=True)


if __name__ == "__main__":
    main(force="--force" in sys.argv[1:])
