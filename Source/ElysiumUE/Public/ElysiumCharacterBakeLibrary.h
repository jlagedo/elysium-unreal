#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumCharacterBakeLibrary.generated.h"

class UAnimSequence;
class UMaterialInterface;
class USkeletalMesh;
class USkeleton;
class UTexture2D;

/** What one bake call did, and everything it could not do. Empty Errors means success. */
USTRUCT(BlueprintType)
struct FElysiumCharacterBakeResult
{
	GENERATED_BODY()

	/** Assets written. */
	UPROPERTY(BlueprintReadOnly, Category="Elysium|Characters")
	int32 Count = 0;

	/** One line per failure, already carrying the stem/clip it belongs to. */
	UPROPERTY(BlueprintReadOnly, Category="Elysium|Characters")
	TArray<FString> Errors;
};

/**
 * Editor-only implementation behind pipeline/unreal/bake_characters.py.
 *
 * The cast is built offline into real assets on the /ElysiumBaked mount instead of through
 * glTFRuntime at map load. glTFRuntime stays the *reader* — the same parser, the same config, the
 * same morph-target merge — because its vendored multi-primitive morph-target patch is load-bearing
 * and fails silently when lost. What changes is where the objects land: a package rather than the
 * transient one, and one shared skeleton rather than one per body.
 *
 * Four steps live here rather than in Python because they have no scripting surface:
 * USkeleton::AddCurveMetaData, the transient-outer rename every glTFRuntime UAnimSequence needs
 * before it can be saved, the additive flags on the `_delta` family, and reading parameters back off
 * a UMaterialInstanceDynamic.
 */
UCLASS()
class ELYSIUMUE_API UElysiumCharacterBakeLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create or load the one skeleton the whole cast shares. Its bone tree is the union of every
	 * body and every bank merged into it, which is what makes a bank clip one UAnimSequence instead
	 * of one per body.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static USkeleton* EnsureSharedSkeleton(const FString& PackageName);

	/**
	 * Merge one .glb's bone tree into the shared skeleton without building a mesh.
	 *
	 * Both bodies and banks have to go through this. Six of the 58 banks the slice resolves through
	 * carry bones no slice body has — ponytails, breasts, a katana prop bone, the `Bone16..Bone25`
	 * chain — and a track naming a bone the shared skeleton lacks is dropped once, for the whole
	 * cast, rather than per body the way the runtime's RemoveTracks filter drops it today.
	 *
	 * A bank glb declares no skin and no mesh, so its tree is read from its single root node.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString MergeSkeletonFromGlb(const FString& GlbPath, USkeleton* Skeleton);

	/** Bones in a skeleton's tree. The bake and the verifier both report it: the union growing is
	 *  the only visible sign that a merge did anything. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static int32 SkeletonBoneCount(const USkeleton* Skeleton);

	/**
	 * Whether a curve is registered on the skeleton AND flagged as a morph target. Both halves
	 * matter: the flag is what puts the curve into USkeletalMeshComponent::ActiveMorphTargets, so a
	 * registered-but-unflagged curve evaluates to the right weight on a face that cannot receive it.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static bool SkeletonHasMorphCurve(const USkeleton* Skeleton, const FName CurveName);

	/**
	 * The asset name one clip label bakes to, so the bake, the verifier and the runtime all fold
	 * the 14 shipped labels carrying characters illegal in an object name the same way. Wraps
	 * FElysiumContentPaths::BakedAssetName rather than restating it.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString BakedAssetName(const FString& Raw);

	/**
	 * Whether any texture parameter on a material resolves to a real texture.
	 *
	 * The verifier's check against the .glb's own material table, and the one thing about a baked
	 * body that fails completely silently: a material instance keeps its slot, its parent and its
	 * factors whether or not its albedo survived the bake, so a body whose textures were lost looks
	 * exactly like a body whose materials were authored flat.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static bool MaterialHasTexture(const UMaterialInterface* Material);

	/**
	 * Bake one body: the mesh through the runtime's own loader configuration, its materials
	 * snapshotted out of glTFRuntime's dynamic instances into saveable ones, and its morph targets
	 * registered as curves on the shared skeleton.
	 *
	 * `Textures` is keyed by glTF material name and supplies the real Texture2D asset the bake
	 * imported from the sibling tex/*.png; the dynamic instance's own texture is a transient object
	 * that cannot be serialised. `EyeMaterials` and `bPlayerMaterial` are the runtime's material
	 * steering, passed through unchanged.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FElysiumCharacterBakeResult BakeMesh(
		const FString& GlbPath,
		const FString& PackageName,
		USkeleton* Skeleton,
		bool bPlayerMaterial,
		const TArray<FString>& EyeMaterials,
		const TMap<FString, UTexture2D*>& Textures,
		const FString& MaterialPackagePath);

	/**
	 * Bake every named clip of one .glb onto the shared skeleton, one UAnimSequence each.
	 *
	 * Whole-glb rather than per-clip because parsing is the expensive step: a bank is 2-35 MB and
	 * `character_shared_male_move_and_ranged` alone carries 826 clips.
	 *
	 * `ClipFlags` is index-aligned with `ClipNames` and carries the raw StudioSeqDesc.flags the
	 * manifest already stores. Bit 0x4 is VtMB's delta/additive bit; those sequences bake as
	 * local-space additives against the reference pose so Unreal's own additive nodes compose them.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FElysiumCharacterBakeResult BakeClips(
		const FString& GlbPath,
		const FString& PackagePath,
		const TArray<FString>& ClipNames,
		const TArray<int32>& ClipFlags,
		USkeleton* Skeleton);

	/**
	 * Drain the asynchronous animation compression the bake queued, then save every dirty package
	 * under the mount. Called once at the end of a batch: a UAnimSequence saved before its
	 * compression finishes serialises without compressed data.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FElysiumCharacterBakeResult FlushCharacterBake();
};
