"""Remove baked character assets the declared partition no longer produces.

The character mount is addressed by name, so an asset nobody writes any more is still loadable --
and a rig family that was renamed leaves a complete second answer for every clip label it owned.
That is worse than a missing asset: nothing fails, and which copy wins depends on which skeleton
the mesh happens to point at.

**A sweep is only authorised against a complete inventory.** `Meshes/`, `Materials/`, `Textures/`
and `Skeletons/` are flat folders shared by every family, so a per-family sweep cannot tell "this
family does not own it" from "another family does". The declared partition plus the manifest is
that inventory: between them they name every asset the bake would produce for the whole cast,
whatever subset a given run baked. Anything else under `Characters/` is residue.

This runs in Python rather than in the editor precisely so a fully-cached run still needs no
editor process. Deleting a `.uasset` off disk is what the editor-side sweep already does, and for
the same reason -- a loaded linker holds the file open on Windows, so a package must NOT be loaded
first.
"""
from __future__ import annotations

import json
import os
from pathlib import Path

from elysium_pipeline import asset_names, character_partition

#: Refuse to remove more than this share of the mount without an explicit override. A receipt or
#: partition bug that made everything look orphaned would otherwise delete the corpus silently.
MAX_SHARE = 0.25


def _expected(npc_dir: Path, partition: dict) -> set[str]:
    """Every asset name the declared partition and the manifest would produce, by folder."""
    with (npc_dir / "npc_manifest.json").open(encoding="utf-8-sig") as handle:
        manifest = json.load(handle)

    expected: set[str] = set()
    for family, entry in partition["models"].items():
        expected.add(f"Skeletons/{character_partition.MODEL_SKELETON_PREFIX}{family}")
    for family in partition["banks"]:
        expected.add(f"Skeletons/{character_partition.BANK_SKELETON_PREFIX}{family}")
    for stem in partition["model_family_of"]:
        expected.add(f"Meshes/SK_{stem}")

    textures = npc_dir / "textures.json"
    if textures.is_file():
        with textures.open(encoding="utf-8-sig") as handle:
            for name in json.load(handle).get("textures", {}):
                expected.add(f"Textures/{name}")
    return expected, manifest


def _expected_dirs(partition: dict, manifest: dict) -> set[str]:
    """Every `Anims/<family>/<owner>` folder the partition would write."""
    out = {"Anims/_banks"}
    for stem, family in partition["model_family_of"].items():
        out.add(f"Anims/{family}")
        out.add(f"Anims/{family}/{stem}")
    for bank in partition["bank_family_of"]:
        out.add(f"Anims/_banks/{bank}")
    # A body's own clips can be owned by another body -- the manifest resolves ownership -- so an
    # owner folder is legitimate under any family that reaches it.
    for stem, record in manifest.get("npcs", {}).items():
        family = partition["model_family_of"].get(stem)
        if not family:
            continue
        for owner in record.get("clips", {}).values():
            if owner in partition["model_family_of"]:
                out.add(f"Anims/{family}/{owner}")
    return out


def plan(mount_root: Path, npc_dir: Path, partition: dict) -> dict:
    """{orphan_assets, orphan_dirs, total} -- what a sweep would remove, and out of how many."""
    character_partition.check(partition)
    expected, manifest = _expected(npc_dir, partition)
    expected_dirs = _expected_dirs(partition, manifest)

    characters = mount_root / "Characters"
    orphan_assets: list[Path] = []
    orphan_dirs: set[str] = set()
    total = 0
    if not characters.is_dir():
        return {"orphan_assets": [], "orphan_dirs": [], "total": 0}

    for path in sorted(characters.rglob("*.uasset")):
        total += 1
        rel = path.relative_to(characters).as_posix()
        folder, _, _ = rel.rpartition("/")
        stem = rel[: -len(".uasset")]
        leaf = rel.rpartition("/")[2]
        if folder == "Anims" or folder.startswith("Anims/"):
            # A sequence or blend space is claimed by its FOLDER: the partition names the family
            # and owner pair, and the container names which clips live there. A folder no declared
            # pair produces is residue whole, which is exactly the renamed-family case.
            if folder not in expected_dirs:
                orphan_assets.append(path)
                orphan_dirs.add(folder)
            continue
        if folder == "Materials":
            # A material instance is named for the mesh it was cut from, so it lives or dies with
            # one: MI_SK_<stem>_<slot>. Matched against the declared stems rather than a rebuilt
            # slot list, because the slot names come from the container and this must not open one.
            if not any(leaf.startswith(f"MI_SK_{s}_") for s in partition["model_family_of"]):
                orphan_assets.append(path)
            continue
        if stem not in expected:
            orphan_assets.append(path)

    return {"orphan_assets": orphan_assets, "orphan_dirs": sorted(orphan_dirs), "total": total}


def sweep(mount_root: Path, npc_dir: Path, partition: dict, *,
          apply: bool = False, force: bool = False) -> dict:
    """Report -- and with `apply`, remove -- everything the declared partition does not produce.

    Returns the plan, with `removed` counting what actually went. Refuses a sweep that would take
    more than `MAX_SHARE` of the mount unless `force`, because that shape is a bug in the inputs
    rather than a large legitimate cleanup.
    """
    result = plan(mount_root, npc_dir, partition)
    result["removed"] = 0
    result["refused"] = ""
    orphans = result["orphan_assets"]
    if not orphans or not apply:
        return result

    share = len(orphans) / max(result["total"], 1)
    if share > MAX_SHARE and not force:
        result["refused"] = (
            f"{len(orphans)} of {result['total']} assets ({share:.0%}) look orphaned, over the "
            f"{MAX_SHARE:.0%} guard -- check npc/families.json before forcing"
        )
        return result

    for path in orphans:
        try:
            path.unlink()
            result["removed"] += 1
        except OSError:
            pass
    # Prune the folders emptied by the above, deepest first so a parent goes after its children.
    characters = mount_root / "Characters"
    for folder in sorted(result["orphan_dirs"], key=len, reverse=True):
        directory = characters / folder
        for parent in [directory, *directory.parents]:
            if parent == characters or characters not in parent.parents:
                break
            try:
                next(parent.iterdir())
                break
            except StopIteration:
                parent.rmdir()
            except OSError:
                break
    return result
