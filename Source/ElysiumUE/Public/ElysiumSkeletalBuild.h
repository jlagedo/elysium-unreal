#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumSkeletalBuild.generated.h"

/**
 * Editor-only construction of skeletal assets from Elysium's own data, without glTF.
 *
 * The character bake reads VtMB's decoded rig directly and builds the engine's assets through the
 * engine's own authoring path -- FMeshDescription plus FSkeletalMeshAttributes for geometry, skin
 * weights and morph deltas -- which is the same path every shipped importer writes into. Owning
 * this removes the vendored glTFRuntime patch, removes glTF's standing exemption from the repo's
 * "coordinates are read verbatim" rule, and removes the mesh-less animation bank as a special case.
 *
 * This header currently carries the construction spike that proves the path before the format work
 * commits to it: the one risk in the whole approach is whether a mesh built this way keeps its
 * morph targets across a save and a reload, which is exactly where the glTFRuntime bake failed.
 */
UCLASS()
class ELYSIUMUE_API UElysiumSkeletalBuildLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Build a minimal skeletal mesh -- two bones, one triangle, one morph target -- entirely in C++
	 * and save it. Synthetic on purpose: it settles the API and the persistence question without
	 * depending on any exported game data, so it can run anywhere and stay as a regression.
	 *
	 * Returns an empty string on success, otherwise the first thing that went wrong.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BuildProbeSkeletalMesh(const FString& PackageName);

	/**
	 * Build and save a skeletal mesh from an `.eskm` container -- skeleton, LOD0 geometry, skin
	 * weights and facial morph targets.
	 *
	 * `SkeletonPackageName` names the shared rig-family skeleton to bind to; it is created on
	 * first use and merged into on every later model, so the bone tree becomes the union across
	 * the family. Left empty, the mesh gets a private skeleton beside it.
	 *
	 * Each material section gets a `UMaterialInstanceConstant` under `MaterialPackagePath`,
	 * parented to `MaterialParentPath` and carrying the albedo named for its slot in
	 * `MaterialTextures`. Instances are named after the MESH asset rather than the model, because
	 * two meshes of the same model would otherwise write the same instance names and the second
	 * would silently take the first's slots.
	 *
	 * Returns an empty string on success, otherwise the first thing that went wrong.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BuildSkeletalMeshFromSource(const FString& SourcePath, const FString& PackageName,
		const FString& SkeletonPackageName, const FString& MaterialParentPath,
		const FString& MaterialPackagePath, const TMap<FString, FString>& MaterialTextures,
		const TMap<FString, FString>& MaterialParents);

	/**
	 * Build and save a `USkeleton` from an `.eskm`'s bone tree alone, with no mesh.
	 *
	 * An animation bank carries no geometry, so it has no mesh to take a tree from -- and a bank
	 * needs a skeleton of its own precisely so its clips can be baked ONCE and shared, rather than
	 * rebuilt against every rig family that plays them. Called again with another bank's container
	 * it merges: a bone the tree already carries keeps its index, so the banks that agree on a rig
	 * land on one skeleton.
	 *
	 * Refuses a container that would give the skeleton a second root, which is what
	 * `USkeleton::MergeBonesToBoneTree` rejects far downstream.
	 *
	 * Returns an empty string on success, otherwise the first thing that went wrong.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BuildSkeletonFromSource(const FString& SourcePath,
		const FString& SkeletonPackageName);

	/**
	 * Build and save one rig family's `USkeleton` from EVERY declared member's bone tree.
	 *
	 * The union of a family's trees, in the order the partition declares, is what makes the
	 * skeleton a function of the declared partition rather than of whichever members a bake
	 * happened to name. Two things downstream depend on that: an untracked bone falls back to this
	 * skeleton's reference pose, and a blend mask is content-addressed by the bones it owns
	 * INTERSECTED with this bone set -- so a skeleton that varies by slice silently varies both.
	 *
	 * `bRebuild` discards whatever is on disk first. `MergeBonesToBoneTree` only rebuilds an empty
	 * tree and otherwise unions, so without it a family skeleton can only grow and keeps the bones
	 * of members that a later partition moved elsewhere.
	 *
	 * `TranslatedBones` names the bones that keep an animation's own translation; every other bone
	 * takes its translation from the body playing the clip, which is VtMB's own rule. The set is
	 * derived rather than named -- `ScanTranslatedBones` answers it -- and it must cover every
	 * container whose clips this skeleton can be the TARGET of, because that is the skeleton the
	 * engine reads the mode from. A bone the tree does not carry is ignored.
	 *
	 * Refuses a container that would give the skeleton a second root, which is what
	 * `USkeleton::MergeBonesToBoneTree` rejects far downstream. `OutBones` is the resulting raw
	 * bone count. Returns an empty string on success, otherwise the first thing that went wrong.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BuildFamilySkeleton(const TArray<FString>& SourcePaths,
		const TArray<FString>& TranslatedBones, const FString& SkeletonPackageName, bool bRebuild,
		int32& OutBones);

	/**
	 * Name the bones whose translation actually MOVES anywhere in the given `.eskm` containers.
	 *
	 * The answer feeds `BuildFamilySkeleton`, and it exists as a separate call because the set a
	 * skeleton needs is not the set its own members declare: the engine reads a bone's translation
	 * retargeting mode from the skeleton being POSED, so a body's skeleton has to keep an
	 * animation's translation on every bone that any bank it is declared compatible with moves.
	 * Scanning is one pass per container and decodes no clip, so the caller scans each container
	 * once and unions.
	 *
	 * `OutBones` is sorted and de-duplicated. Returns an empty string on success, otherwise the
	 * first thing that went wrong.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString ScanTranslatedBones(const TArray<FString>& SourcePaths, TArray<FString>& OutBones);

	/**
	 * Drop every saved package under `PackagePath` from memory, and return how many were released.
	 *
	 * The bake writes ~10,000 assets in one process and they all stay resident to exit, because a
	 * baked asset carries `RF_Standalone` and that is exactly what garbage collection is told to
	 * keep while `GIsEditor`. Clearing the flag first is what makes a collect take anything.
	 * A dirty package is skipped -- it is unsaved output, not slack.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static int32 ReleaseBakedPackages(const FString& PackagePath);

	/**
	 * Declare that this skeleton may play animations authored on the named ones.
	 *
	 * **Editor-side bookkeeping, not the mechanism.** What actually lets a bank clip evaluate on a
	 * foreign body is `DecompressPose`, which asks `FSkeletonRemappingRegistry` for a name-keyed
	 * bone map for the (sequence skeleton, mesh skeleton) pair unconditionally -- there is no
	 * compatibility gate anywhere on that path, and every reader of `CompatibleSkeletons` is inside
	 * `WITH_EDITORONLY_DATA`. The declaration is what makes the editor offer these assets together:
	 * the animation asset browser, `UBlendSpace::ValidateSampleInput`, anim-blueprint compatibility
	 * and preview-mesh fixup all consult it, and a bake that skipped it would still produce a
	 * working game but an editor that refuses to show one.
	 *
	 * The direction is not symmetric. The TARGET names the skeletons it may play, so the body's
	 * family skeleton is the target and each bank skeleton is an argument.
	 *
	 * Returns an empty string on success, otherwise the first thing that went wrong.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString DeclareCompatibleSkeletons(const FString& SkeletonPackageName,
		const TArray<FString>& SourceSkeletonPackageNames);

	/**
	 * Build and save one `UAnimSequence` per clip in an `.eskm`, as `<PackagePath>/A_<clip>`.
	 *
	 * Every clip in the file is baked in one pass because a shared animation bank holds hundreds
	 * of them and re-reading the container per clip is the whole cost of the bake.
	 *
	 * Tracks bind to the skeleton by bone NAME, which is what lets a bank recorded on one rig play
	 * on every body in the family. How hard an unresolved name is depends on which kind of
	 * container this is, and the container says which: a body carries its own geometry, a bank
	 * carries none.
	 *
	 * - A **body's own** container is checked up front and the whole call fails if any of its bones
	 *   is missing from the skeleton. That skeleton was merged from this very body's mesh, so a
	 *   missing bone means the merge lost one, and the clip would bake a track short and play part
	 *   of the rig at bind pose with nothing reported.
	 * - A **bank** is recorded against another body's rig and legitimately names bones this family
	 *   has never had -- the Gangrel hair chain, the Ventrue ponytail. Those tracks are dropped, and
	 *   `OutDroppedTracks` counts them so a bake that quietly loses more than it should is visible.
	 *
	 * A clip that owns only part of the rig -- VtMB's partial-body `*_layer` overlays -- also gets a
	 * `UBlendProfile` blend mask on the shared skeleton and a `UElysiumAnimLayerMask` naming it, and
	 * its owned-but-unanimated bones are written out at the container's bind pose rather than left
	 * to the skeleton's reference pose.
	 *
	 * Returns an empty string on success, otherwise the first thing that went wrong.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BuildAnimSequencesFromSource(const FString& SourcePath, const FString& PackagePath,
		const FString& SkeletonPackageName, int32& OutClipCount, int32& OutDroppedTracks);

	/**
	 * Build and save one `UBlendSpace` per blend grid in `npc/blends/<owner>.json`, as
	 * `<PackagePath>/BS_<label>`, sampling the `A_<clip>` sequences already written there.
	 *
	 * A VtMB sequence label does not always name one animation. 275 of them name a **grid**: a 9x1
	 * fan of `walk_0`..`walk_315` selected by the `move_yaw` pose parameter, or a 3x3 weapon-aim
	 * layer on `aim_yaw`/`aim_pitch`. The exporter bakes every cell as its own clip and writes the
	 * axes beside them; this turns the axes into the asset that mixes them.
	 *
	 * `BlendsRelPath` is `npc_index.json`'s own `blends` value, so one call reads both
	 * "blends/<stem>.json" and "animated_props/blends/<stem>.json". An owner that declares no grid
	 * has no sidecar at all, which is most of them -- the caller skips rather than asking.
	 *
	 * Must run AFTER `BuildAnimSequencesFromSource` for the same owner: a sample is one of the
	 * sequences that pass writes, and a blend space whose samples do not resolve is not written.
	 *
	 * A cell the exporter recorded as null, or one whose sequence is absent, is skipped and counted
	 * in `OutSkippedCells` -- the schema permits a hole and the runtime reader tolerates one. A grid
	 * left with fewer than two live samples is not a blend space and is skipped whole, counted in
	 * `OutSkippedGrids`.
	 *
	 * Returns an empty string on success, otherwise the first thing that went wrong.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BuildBlendSpacesFromGrids(const FString& BlendsRelPath, const FString& PackagePath,
		const FString& SkeletonPackageName, int32& OutSpaceCount, int32& OutSkippedGrids,
		int32& OutSkippedCells);

	/** Report what a saved sequence contains, for a fresh process to check against. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString DescribeAnimSequence(const FString& AssetPath);

	/**
	 * Report what a saved skeletal mesh actually contains, for a *fresh* process to check against.
	 * The distinction matters: the glTFRuntime bake produced a mesh with 53 morph targets in memory
	 * and 0 after a reload, and only a second process can tell those apart.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString DescribeSkeletalMesh(const FString& AssetPath);
};
