# Composes one Niagara system per VtMB particle root -- `NS_<root>` under
# `/Game/ElysiumGenerated/VFX` -- from an authored base emitter and the staged `particleTrees{}`.
#
# The ruling is `docs/project/niagara_authoring_strategy.md` 4.1 (option A+B): ~10 hand-authored
# assets, and one generated system per root with one *inherited* emitter per drawing node. The
# design note for this lane is `E:/elysium-work/scratch/effects/generator_design.md`.
#
# **Not the legacy lane.** `make_particle_systems.py` flattens a per-map `<map>.particles.json`
# closure into `FElysiumParticleLayer` rows and writes onto the map's baked mount. It is untouched
# and still on the bake. This script reads the staged manifest's `particleTrees{}` -- the same value
# `bake_map_v2._write_tree` puts on the placed actor -- and hands each tree to
# `UElysiumParticleAssetBuilder::BuildRootSystem` as JSON. Nothing here is wired into the bake yet.
#
# **The tree travels as JSON, not as a USTRUCT array.** `FElysiumParticleNode` is a plain
# `USTRUCT()` and cannot cross a `UFUNCTION` boundary without changing the runtime header the R7.3
# actor contract is written against; and the staged document is already the bake's own transport
# for exactly this shape, so there is one parser rather than a second field-mapping table here.
#
# **The host must be able to render.** `UNiagaraSystem::IsReadyToRun()` short-circuits false when
# `FApp::CanEverRender()` is false, so the commandlet needs `-AllowCommandletRendering` or every
# root fails its readiness gate for a reason that has nothing to do with the asset.
import json
import os
import sys

import unreal

# Self-bootstrapping rather than `from pipeline.unreal import _bootstrap`: that import is itself
# what puts the repository on `sys.path`, so it only resolves for a script the umbrella already
# imported. This one is launched directly.
_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
for _path in (_REPO, os.path.join(_REPO, "pipeline", "src")):
    if _path not in sys.path:
        sys.path.insert(0, _path)

from elysium_pipeline import mounts  # noqa: E402


PACKAGE = mounts.VFX

#: The authored base emitter every generated emitter inherits from. It does not exist yet; the
#: spike falls back to the stock Fountain template, which is a *copy* rather than a child --
#: `UNiagaraSystem::AddEmitterHandle` strips the parent from any emitter marked
#: `bIsInheritable = false`, which every stock template asset is. The builder reports which it got.
BASE_EMITTER = "/Game/ElysiumAuthored/VFX/Base/E_VtMBLeaf"
FOUNTAIN_TEMPLATE = "/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain"

#: Where the census agent staged the three working maps.
DEFAULT_STAGED = "E:/elysium-work/scratch/effects/staged.json"


def _argument(name, default=None):
    prefix = "-%s=" % name
    for token in sys.argv:
        if token.startswith(prefix):
            return token[len(prefix):]
    return default


def _flag(name):
    return _argument(name) not in (None, "", "0", "false", "False")


def _drain():
    """Finish every queued Niagara compile before a package is force-deleted.

    Force-deleting a package the asset compiler still owns crashes in CoreUObject; the legacy lane
    learned this the hard way when the particle stage was run in isolation.
    """
    unreal.ElysiumParticleAssetBuilder.finish_asset_compilation()


def _base_emitter():
    """The authored base emitter, or the stock Fountain template with a warning.

    Loaded fresh per system: a wrapper held across a force-delete's garbage collection is not a
    reference the engine honours.
    """
    emitter = unreal.load_asset(BASE_EMITTER)
    if emitter:
        return emitter, False
    emitter = unreal.load_asset(FOUNTAIN_TEMPLATE)
    if not emitter:
        raise SystemExit("[root-systems] neither %s nor the stock Fountain template loads"
                         % BASE_EMITTER)
    return emitter, True


def _staged_trees(path, wanted_map):
    """`{root key: tree}` over the staged document, one map or all of them.

    A root placed on two maps stages the same tree twice; the last one wins, which is correct --
    the tree is a property of the root definition, not of the placement.
    """
    with open(path, "r", encoding="utf-8") as handle:
        document = json.load(handle)
    trees = {}
    for name, staged in sorted(document.items()):
        if wanted_map and name != wanted_map:
            continue
        for key, tree in (staged.get("particleTrees") or {}).items():
            trees[key] = tree
    return trees


def _asset_name(tree, key):
    """`NS_<root>` in the root's authored spelling.

    The key is the normalized id (`vtmb:particle:barrelfireemitter`) and is the identity; `name` is
    how the definition spelled itself, which is sometimes the bare name (`BarrelFireEmitter`) and
    sometimes the include path (`particles/Fire1_emitter.txt`). The asset takes the authored
    spelling of the stem, because that is what the archetype table and the owner's verdicts name --
    but only when it casefolds back to the key, so the name can never move the asset off its root.
    """
    stem = key.rsplit(":", 1)[-1]
    raw = (tree.get("name") or "").replace("\\", "/").rsplit("/", 1)[-1]
    if raw.casefold().endswith(".txt"):
        raw = raw[:-4]
    return "NS_" + (raw if raw.casefold() == stem.casefold() else stem)


def _report(label, result, saved):
    """One line per root: what the system is, and every way it fell short."""
    counts = [count for count in (result.particle_counts or []) if count >= 0]
    unreal.log(
        "[root-systems] %s: %d emitter(s), %s, %s, %d skipped write(s), particles %s, %s"
        % (label, result.emitter_count,
           "ready" if result.ready else "NOT READY",
           "inherited" if result.inherited else "copied",
           len(result.skipped or []),
           ("/".join(str(count) for count in counts) if counts else "not probed"),
           "saved" if saved else "not saved"))
    for skipped in (result.skipped or []):
        unreal.log_warning("[root-systems] %s   skipped %s" % (label, skipped))
    for error in (result.errors or []):
        unreal.log_error("[root-systems] %s   %s" % (label, error))


def _build(asset_name, tree, ticks):
    """Author, compile, gate and save one root. Returns True when the asset landed."""
    asset = "%s/%s" % (PACKAGE, asset_name)
    # `load_asset` rather than `does_asset_exist`: the latter asks the asset registry, which a
    # commandlet has not scanned, so a package written by a previous run reads as missing and the
    # create below lands on top of it.
    if unreal.load_asset(asset) is not None:
        _drain()
        unreal.EditorAssetLibrary.delete_asset(asset)
    # Loaded after the delete: that delete collects garbage, and the base emitter must be a live
    # object when the builder reads it.
    base, fallback = _base_emitter()
    if fallback:
        unreal.log_warning(
            "[root-systems] %s: %s does not exist; using the stock Fountain template, which "
            "copies rather than inherits" % (asset_name, BASE_EMITTER))
    system, result = unreal.ElysiumParticleAssetBuilder.build_root_system(
        asset_name, PACKAGE, base, json.dumps(tree))
    if not system:
        _report(asset_name, result, saved=False)
        return False
    if ticks > 0:
        probe = unreal.ElysiumParticleAssetBuilder.probe_system(system, ticks, 1.0 / 30.0)
        result.particle_counts = probe.particle_counts
        result.errors = list(result.errors) + list(probe.errors)
    if not result.ready:
        # Mechanical failures never reach the owner (`effects_authoring.md` -> the review loop):
        # a system that does not compile is not saved and not offered for a verdict.
        _report(asset_name, result, saved=False)
        return False
    saved = unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False)
    _report(asset_name, result, saved=saved)
    return bool(saved)


# ---------------------------------------------------------------------------- the spike

#: Strategy 5 step 1, the go/no-go on the whole generated-asset family: a template emitter plus two
#: stock modules, composed, compiled and saved without the Niagara editor ever being opened. It runs
#: through the real `BuildRootSystem` rather than a private path, so what it proves is the lane the
#: bake will use. One drawing node carrying a rate and a burst is what makes the builder add
#: `SpawnBurst_Instantaneous` and drive the template's own `SpawnRate` -- the two stock modules.
SPIKE_TREE = {
    "root": "vtmb:particle:elysium_spike",
    "name": "Spike",
    "stats": {"leafCount": 1, "depth": 1, "maxKeyframes": 1},
    "nodes": [
        {
            "index": 0, "id": "vtmb:particle:elysium_spike", "name": "Spike", "kind": "root",
            "draws": False, "spawns": True, "parent": None, "via": "spawn",
            "resolved": True, "lifetime_s": 1.0, "loop": True,
        },
        {
            "index": 1, "id": "vtmb:particle:elysium_spike_leaf", "name": "SpikeLeaf",
            "kind": "leaf", "draws": True, "spawns": False, "parent": 0, "via": "spawn",
            "resolved": True, "fps": 30.0,
            "lifetime_s": 2.0, "lifetime_min_s": 1.5, "lifetime_max_s": 2.5, "loop": False,
            "size_cm": [[0.0, 25.4, 25.4], [1.0, 5.08, 5.08]],
            "red": [[0.0, 1.0, 1.0]], "green": [[0.0, 0.6, 0.6]], "blue": [[0.0, 0.2, 0.2]],
            "mask": [[0.0, 1.0, 1.0]],
            "elevation_speed_cm_s": [[0.0, 50.8, 152.4]],
            "spawn": {
                "rate": [[0.0, 20.0, 20.0]],
                "burst": [[0.0, 5.0, 5.0]],
                "radius_cm": [[0.0, 0.0, 12.7]],
                "theta_deg": [[0.0, 0.0, 360.0]],
                "timescale": 1.0,
            },
        },
    ],
}


def spike(ticks):
    unreal.log("[root-systems] spike: NS_Spike from %s" % BASE_EMITTER)
    return _build("NS_Spike", SPIKE_TREE, ticks)


def main():
    ticks = int(_argument("RootSystemTicks", "0") or 0)
    if _flag("RootSystemSpike"):
        raise SystemExit(0 if spike(ticks) else 1)

    staged = _argument("RootSystemStaged", DEFAULT_STAGED)
    if not os.path.isfile(staged):
        raise SystemExit("[root-systems] no staged document at %s" % staged)
    trees = _staged_trees(staged, _argument("RootSystemMap"))
    if not trees:
        raise SystemExit("[root-systems] the staged document holds no particleTrees{}")

    named = [name for name in (_argument("RootSystems") or "").split(",") if name]
    wanted = {name.casefold() for name in named}

    built = failed = 0
    for key, tree in sorted(trees.items()):
        asset_name = _asset_name(tree, key)
        if wanted and not (
            key.casefold() in wanted
            or (tree.get("name") or "").casefold() in wanted
            or asset_name.casefold() in wanted
        ):
            continue
        if _build(asset_name, tree, ticks):
            built += 1
        else:
            failed += 1
    _drain()
    unreal.log("[root-systems] %d built, %d refused" % (built, failed))
    if failed:
        raise SystemExit(1)


main()
