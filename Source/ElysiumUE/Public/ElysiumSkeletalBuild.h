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
		const FString& MaterialPackagePath, const TMap<FString, FString>& MaterialTextures);

	/**
	 * Build and save one `UAnimSequence` per clip in an `.eskm`, as `<PackagePath>/A_<clip>`.
	 *
	 * Every clip in the file is baked in one pass because a shared animation bank holds hundreds
	 * of them and re-reading the container per clip is the whole cost of the bake.
	 *
	 * Tracks bind to the skeleton by bone NAME, which is what lets a bank recorded on one rig
	 * play on every body in the family. A track naming a bone the skeleton does not carry is
	 * dropped -- the clip still bakes, that bone simply has nothing to drive.
	 *
	 * Returns an empty string on success, otherwise the first thing that went wrong.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BuildAnimSequencesFromSource(const FString& SourcePath, const FString& PackagePath,
		const FString& SkeletonPackageName, int32& OutClipCount);

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
