"""Decode the ground models VtMB's `vdata/items` table names into the shared item corpus.

A loose world item is an entity whose mesh the map never states: a placed `item_*` carries no
`model` keyvalue, and `CBaseCombatWeapon::Spawn` sets its world model from the item definition's
`playermodel` instead. The map exporter's prop pass only follows `model` keys, so those meshes are
absent from every map's `props/` -- 46 placed items game-wide stand bodiless, and a scripted
`GiveItem`/drop can name any of the 244 definitions in any map.

So they are not a map's props. They are decoded once, from the same `vdata/items` table the runtime
loads, into the shared corpus, and baked once onto `/ElysiumBaked/Shared/Meshes` like every other
static model.

The meshes are the shared corpus's -- `UE_extract_corpus` decodes an item's ground model
alongside every other static model, once. This module states no coordinate rule of its own and
decodes nothing; it writes the join, and reports every definition whose model the install lacks.

A model the install does not contain is skipped and recorded, never fatal: `vdata/items` names a
couple of models no shipped VPK carries, and one absent mesh must not cost the other 123.

Usage:
  uv run elysium export bundle items            # write the ground-model manifest
"""
import json
import os
import sys

from elysium_pipeline import shared_corpus as SC
from elysium_pipeline.formats import install, kv
from elysium_pipeline.paths import export_root

OUT = os.fspath(export_root())

# The manifest naming which item definition stands on which shared-corpus mesh.
ROOT = "items"
MANIFEST = "ground_models.json"
SCHEMA = "elysium.item-ground-models"
VERSION = 1

# `vdata/items/*.txt`, one definition per file -- the same enumeration FElysiumItemTable::Load
# performs, including the cut ` - hunter` / ` - vampire` companions (a classname is the file's base
# name, suffix and all).
ITEM_DIR = "vdata/items/"


def _normalize(model):
    """A `playermodel` value as an install key: forward slashes, lower case, `.mdl` present.

    Two definitions write `models/weapons/throwing_star/ground/g_throwing_star` with no extension,
    which the engine resolves as a `.mdl` all the same."""
    key = model.strip().replace("\\", "/").lower()
    if not key:
        return ""
    return key if key.endswith(".mdl") else key + ".mdl"


def ground_models(idx):
    """{install key -> sorted classnames} for every distinct `playermodel` in `vdata/items`.

    Distinct by resolved key, so the 25 definitions sharing `models/items/key/ground/key.mdl`
    decode once. A definition with an empty `playermodel` contributes nothing -- armour and the
    disciplines have no ground body at all."""
    by_model = {}
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
        raw_model = block.get("playermodel", "")
        if not isinstance(raw_model, str):
            continue   # a repeated key parses to a list; no definition ships one
        model = _normalize(raw_model)
        if not model:
            continue
        classname = os.path.splitext(key[len(ITEM_DIR):])[0]
        by_model.setdefault(model, []).append(classname)
    return {model: sorted(names) for model, names in sorted(by_model.items())}


def _reason(idx, model):
    return "not in the install" if install.read(idx, model) is None else "decode failed"


def _face_count(path):
    """Triangles in a decoded OBJ. `models/weapons/w_null.mdl` -- the deliberate empty model 17
    definitions point at -- decodes cleanly and writes none, and the bake authors no asset for it,
    so the manifest has to say which models actually carry geometry."""
    faces = 0
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if line.startswith("f "):
                faces += 1
    return faces


def main(index=None, force=False):
    """Write the ground-model manifest. The meshes themselves are the shared corpus's.

    An item's world model is a static model like any other, so `UE_extract_corpus` decodes it once
    into `shared/props` alongside every prop. What stays here is the join the runtime needs -- which
    `vdata/items` definition stands on which mesh stem, and which models the install does not carry.
    """
    idx = index if index is not None else install.build_index()
    wanted = ground_models(idx)
    props_dir = os.fspath(SC.props_dir(OUT))

    resolved, skipped = {}, []
    for model in sorted(wanted):
        stem = SC.static_stem(model)
        if os.path.isfile(os.path.join(props_dir, stem + ".obj")):
            resolved[model] = stem
        else:
            skipped.append({"model": model, "reason": _reason(idx, model),
                            "classes": wanted[model]})

    manifest = {
        "schema": SCHEMA,
        "version": VERSION,
        "models": {
            model: {
                "stem": resolved[model],
                "classes": wanted[model],
                "faces": _face_count(os.path.join(props_dir, resolved[model] + ".obj")),
            }
            for model in sorted(resolved)
        },
        "skipped": skipped,
    }
    manifest_path = os.path.join(OUT, ROOT, MANIFEST)
    os.makedirs(os.path.dirname(manifest_path), exist_ok=True)
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=1, sort_keys=True)
        handle.write("\n")

    print(f"[items] {len(wanted)} distinct ground models "
          f"({len(resolved)} in the shared corpus, {len(skipped)} absent) -> {ROOT}/{MANIFEST}",
          flush=True)
    for entry in skipped:
        print(f"  ! {entry['model']}: {entry['reason']} "
              f"({', '.join(entry['classes'][:3])}"
              f"{' ...' if len(entry['classes']) > 3 else ''})", flush=True)


if __name__ == "__main__":
    main(force="--force" in sys.argv[1:])
