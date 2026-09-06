#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumSkeletalBuild.generated.h"

/**
 * Editor-only construction of native skeletal assets from the GLB lane's staged projections.
 *
 * The character bake reads VtMB's decoded rig directly and builds the engine's assets through the
 * engine's own authoring path -- FMeshDescription plus FSkeletalMeshAttributes for geometry, skin
 * weights and morph deltas -- which is the same path every shipped importer writes into. Owning
 * this removes the vendored glTFRuntime patch, removes glTF's standing exemption from the repo's
 * "coordinates are read verbatim" rule, and writes mesh-less animation banks as native assets.
 *
 * The persistence trap is whether a mesh built this way keeps its morph targets across a save and
 * a reload, which is where the glTFRuntime bake failed.
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

	/** Build a staged GLB projection, binding existing material assets directly. A non-empty
	 * reference pose re-skins geometry in the same build, for wield models. No private material
	 * instances or texture imports are created by this entry point. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BuildSkeletalMeshFromStage(const FString& SourcePath, const FString& PackageName,
		const FString& SkeletonPackageName, const TMap<FString, FString>& MaterialAssets,
		const TArray<FTransform>& ReferencePose, const FString& RecipeFingerprint = TEXT(""));

	/**
	 * Build and save a `USkeleton` from an `.eskm`'s bone tree alone, with no mesh.
	 *
	 * An animation bank carries no geometry, so it has no mesh to take a tree from -- and a bank
	 * needs a skeleton of its own precisely so its clips can be baked ONCE and shared, rather than
	 * rebuilt against every body skeleton that plays them. Called again with another bank's
	 * container it merges: a bone the tree already carries keeps its index, so the banks that agree
	 * on a rig land on one skeleton.
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
	 * Build and save one `USkeleton` from the union of EVERY named container's bone tree.
	 *
	 * A model names its own container alone, so its skeleton is exactly its own tree. A shared
	 * animation bank names every container the bank is declared over, in that declared order, which
	 * is what makes a bank skeleton a function of the declared membership rather than of whichever
	 * containers a bake happened to slice. Two things downstream depend on that: an untracked bone
	 * falls back to this skeleton's reference pose, and a blend mask is content-addressed by the
	 * bones it owns INTERSECTED with this bone set -- so a skeleton that varies by slice silently
	 * varies both.
	 *
	 * `bRebuild` discards whatever is on disk first. `MergeBonesToBoneTree` only rebuilds an empty
	 * tree and otherwise unions, so without it the skeleton can only ever grow and keeps bones no
	 * named container declares any more.
	 *
	 * Refuses a container that would give the skeleton a second root, which is what
	 * `USkeleton::MergeBonesToBoneTree` rejects far downstream. `OutBones` is the resulting raw
	 * bone count. Returns an empty string on success, otherwise the first thing that went wrong.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BuildFamilySkeleton(const TArray<FString>& SourcePaths,
		const FString& SkeletonPackageName, bool bRebuild, int32& OutBones,
		const FString& RecipeFingerprint = TEXT(""));

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

	/** Declare that this skeleton may play animations authored on the named bank skeletons. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString DeclareCompatibleSkeletons(const FString& SkeletonPackageName,
		const TArray<FString>& SourceSkeletonPackageNames,
		const FString& RecipeFingerprint = TEXT(""));

	/** Stage variant: standard folded names, optional cinematic role, manifest-owned pruning. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BuildAnimSequencesFromStage(const FString& SourcePath, const FString& PackagePath,
		const FString& SkeletonPackageName, const FString& Role, int32& OutClipCount,
		int32& OutDroppedTracks, int32& OutSuppressedAppendixTracks, TArray<FName>& OutSuppressedAppendixBones,
		const FString& RecipeFingerprint = TEXT(""));

	/** Read staged grid JSON directly; no lookup under the legacy export root. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BuildBlendSpacesFromStage(const FString& Json, const FString& PackagePath,
		const FString& SkeletonPackageName, const FString& Role, int32& OutSpaceCount,
		int32& OutSkippedGrids, int32& OutSkippedCells, const FString& RecipeFingerprint = TEXT(""));

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
