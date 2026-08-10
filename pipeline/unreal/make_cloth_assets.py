# Generates one UChaosClothAsset per authored VtMB garment, from the exported sidecar.
#
# VtMB carries renderer-side particle cloth in the model image and the game's own StudioRender
# solves it (`docs/vtmb/secondary_motion.md`). The offline decode is
# `elysium_pipeline.formats.mdl_cloth`, which writes `npc/garment/<stem>.json` beside the
# character's `.glb` and in that glb's own basis. This turns each garment in that sidecar into a
# generated asset, so the running game carries no VtMB cloth rule at all -- a stock Chaos solver
# consumes a generated asset, the same way blend grids became `UBlendSpace` assets.
#
# The construction itself is `UElysiumClothBuildLibrary`, not Python, and not by preference: a
# cloth collection is an `FManagedArrayCollection` written through `FCollectionClothFacade`, and
# neither the collection nor the facade nor `FClothGeometryTools` carries a `UFUNCTION`. What is
# ordinary Python -- finding the sidecars, resolving the character mesh, saving the packages --
# stays here, exactly as it does for the player animation graph.
#
# This generator is NOT in `build_content.py`'s umbrella. It needs the character bake to have run
# first, because a cloth asset binds to a skeletal mesh's reference skeleton, and the umbrella runs
# before any character exists.
import os
import sys

import unreal

# Self-bootstrapping rather than `from pipeline.unreal import _bootstrap`: that import is itself
# what puts the repository on `sys.path`, so it only resolves for a script the umbrella already
# imported. This one is launched directly, where the only thing on the path is its own directory.
_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
for _path in (_REPO, os.path.join(_REPO, "pipeline", "src")):
    if _path not in sys.path:
        sys.path.insert(0, _path)

from elysium_pipeline.paths import export_root  # noqa: E402

# Where the character bake puts skeletal meshes, and where generated cloth lands. The mesh mount
# is the bake's, not `/Game`: a cloth asset binds to that mesh's reference skeleton, so it has to
# name the exact package the bake wrote.
MESH_PACKAGE = "/ElysiumBaked/Characters/Meshes"
CLOTH_PACKAGE = "/Game/VtMB/Cloth"

# What each garment is MADE of, and every solver value the build applies. The authored payload
# states a garment's shape and its constraint graph and nothing about its material -- VtMB's
# solver had no density, friction or thickness to state -- so that call is authored here as
# reviewable data rather than compiled into the build library.
TUNING_PATH = os.path.join(_REPO, "pipeline", "unreal", "cloth_tuning.json")


def requested_stems():
    """The `-ClothStems=` slice, or None for every sidecar on disk.

    The character bake is sliceable and this runs behind it, so naming models there must not
    silently regenerate the whole cast's garments beside them.
    """
    for argument in sys.argv:
        if argument.startswith("-ClothStems="):
            named = [s for s in argument[len("-ClothStems="):].split(",") if s]
            return set(named) or None
    return None


def garment_sidecars():
    """Every `npc/garment/<stem>.json` the export wrote, as (stem, path)."""
    root = os.path.join(os.fspath(export_root()), "npc", "garment")
    if not os.path.isdir(root):
        return []
    wanted = requested_stems()
    return sorted(
        (os.path.splitext(name)[0], os.path.join(root, name))
        for name in os.listdir(root)
        if name.endswith(".json")
        and (wanted is None or os.path.splitext(name)[0] in wanted)
    )


def character_mesh(stem):
    """The baked skeletal mesh a garment binds to, or None when the bake has not run.

    A cloth asset resolves its own bone names against this skeleton, so a missing mesh is a
    skipped garment rather than a failure: the bake and this generator are separate steps and
    either can be re-run alone.
    """
    for candidate in (
        "%s/SK_%s.SK_%s" % (MESH_PACKAGE, stem, stem),
        "%s/%s.%s" % (MESH_PACKAGE, stem, stem),
    ):
        # `load_asset` rather than `does_asset_exist`: the latter asks the asset registry, which a
        # commandlet has not scanned, so every package on the bake's own mount reads as missing.
        try:
            if unreal.load_asset(candidate) is not None:
                return candidate
        except Exception:
            pass
    return None


def main():
    sidecars = garment_sidecars()
    if not sidecars:
        # Ordinary for a slice: 60 of the 4,445 installed models author a garment at all, so most
        # names the bake was given have nothing here. Only a whole-cast run finding nothing is odd.
        if requested_stems() is None:
            unreal.log_warning("[make_cloth_assets] no garment sidecars under npc/garment")
        return

    built = skipped = failed = 0
    for stem, path in sidecars:
        mesh = character_mesh(stem)
        if mesh is None:
            unreal.log_warning(
                "[make_cloth_assets] %s: no baked skeletal mesh, skipped" % stem)
            skipped += 1
            continue

        results = unreal.ElysiumClothBuildLibrary.build_cloth_assets_from_sidecar(
            path, CLOTH_PACKAGE, mesh, TUNING_PATH)
        for result in results:
            for error in result.errors:
                unreal.log_error("[make_cloth_assets] %s: %s" % (stem, error))
            if not result.asset_path:
                failed += 1
                continue
            package = result.asset_path.split(".")[0]
            if not unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
                unreal.log_error("[make_cloth_assets] %s: could not save %s" % (stem, package))
                failed += 1
                continue
            # The generated physics asset is a separate package beside the cloth one.
            physics = package + "_PHYS"
            if unreal.EditorAssetLibrary.does_asset_exist(physics):
                unreal.EditorAssetLibrary.save_asset(physics, only_if_is_dirty=False)
            built += 1
            unreal.log(
                "[make_cloth_assets] %s -> %s [%s] (%d sim vertices, %d faces, %d pinned, "
                "%d collision bodies, %d config properties)"
                % (stem, package, result.material or "UNTUNED", result.sim_vertices,
                   result.sim_faces, result.kinematic_vertices, result.collision_bodies,
                   result.config_properties))
            # What the built model carries, not what the build intended. A garment whose
            # particles survive but whose kinematic set or tethers do not simulates as a sheet
            # dropped on the floor, and every stage that could have lost them is silent.
            unreal.log(
                "[make_cloth_assets] %s   built: %d sim vertices, %d kinematic, %d tethers"
                % (stem, result.built_sim_vertices, result.built_kinematic_vertices,
                   result.built_tethers))
            # The render side: how much of the surface the simulation drives, and how much stays
            # skinned. A garment whose skinned count is zero has swallowed its own waistband.
            unreal.log(
                "[make_cloth_assets] %s   render: %d driven, %d skinned, %d orphaned, "
                "%d root-bound particles, %d particles with no normal"
                % (stem, result.driven_vertices, result.skinned_vertices,
                   result.orphaned_bindings, result.root_bound_particles,
                   result.degenerate_sim_normals))

    unreal.log("[make_cloth_assets] %d built, %d skipped, %d failed" % (built, skipped, failed))


main()
