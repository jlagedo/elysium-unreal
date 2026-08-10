"""Decode the ground models VtMB's `vdata/items` table names into the shared item corpus.

A loose world item is an entity whose mesh the map never states: a placed `item_*` carries no
`model` keyvalue, and `CBaseCombatWeapon::Spawn` sets its world model from the item definition's
`playermodel` instead. The map exporter's prop pass only follows `model` keys, so those meshes are
absent from every map's `props/` -- 46 placed items game-wide stand bodiless, and a scripted
`GiveItem`/drop can name any of the 244 definitions in any map.

So they are not a map's props. They are decoded once, from the same `vdata/items` table the runtime
loads, into `$ELYSIUM_EXPORT_ROOT/items/props/` -- the same layout a map's `props/` directory has, so
the map bake's own prop stage reads them with no new geometry code -- and baked once onto
`/ElysiumBaked/items`.

Unreal-native by construction: every mesh goes through `UE_bsp_to_scene.decode_prop_models`, the
same MDL -> OBJ writer the two map prop paths share (cm, Z-up, left-handed, winding reversed). This
module states no coordinate rule of its own.

A model the install does not contain is skipped and recorded, never fatal: `vdata/items` names a
couple of models no shipped VPK carries, and one absent mesh must not cost the other 123.

Usage:
  uv run elysium export bundle items            # decode item ground models, then bake them
  uv run elysium export bundle items --force    # clear the corpus directory first
"""
import json
import os
import sys

from elysium_pipeline.exporters.UE_bsp_to_scene import decode_prop_models
from elysium_pipeline.formats import install, kv, phy
from elysium_pipeline.paths import export_root

OUT = os.fspath(export_root())

# The corpus subtree, and the manifest naming what landed in it. `props/` rather than `models/`
# because that is the directory name the bake's prop stage reads.
ROOT = "items"
PROPS = "props"
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
    idx = index if index is not None else install.build_index()
    wanted = ground_models(idx)

    props_dir = os.path.join(OUT, ROOT, PROPS)
    os.makedirs(props_dir, exist_ok=True)
    if force:
        for entry in os.listdir(props_dir):
            path = os.path.join(props_dir, entry)
            if os.path.isfile(path):
                os.remove(path)

    # Decoded through the map exporter's own prop path: shared texture cache, shared skin-family
    # sidecars, one OBJ per distinct model under the stem MDL.sanitize gives it.
    tex_cache = {}
    resolved, ok, missing = decode_prop_models(idx, wanted.keys(), props_dir, tex_cache, set())

    # VtMB's own convex collision where the model ships a `.phy`. A ground item is not a
    # `prop_physics` today, so nothing simulates against it yet; the sidecar is what the bake needs
    # the moment a dropped item does.
    phy.write_physics_phys(idx, props_dir, {stem: model for model, stem in resolved.items()})

    skipped = [
        {"model": model, "reason": _reason(idx, model), "classes": wanted[model]}
        for model in sorted(set(wanted) - set(resolved))
    ]
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
    with open(manifest_path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=1, sort_keys=True)
        handle.write("\n")

    print(f"[items] {len(wanted)} distinct ground models ({ok} decoded, {missing} skipped), "
          f"{sum(1 for v in tex_cache.values() if v)} textures -> {ROOT}/{PROPS}/", flush=True)
    for entry in skipped:
        print(f"  ! {entry['model']}: {entry['reason']} "
              f"({', '.join(entry['classes'][:3])}"
              f"{' ...' if len(entry['classes']) > 3 else ''})", flush=True)


if __name__ == "__main__":
    main(force="--force" in sys.argv[1:])
