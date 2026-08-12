#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumCharacterBakeLibrary.generated.h"

class UMaterialInterface;
class USkeleton;

/**
 * The four editor-only questions `pipeline/unreal/bake_characters.py` and its verifier ask that
 * have no Python scripting surface of their own.
 *
 * Building a character is not one of them: a body, its clips and its blend grids are constructed
 * from the `.eskm` container by `UElysiumSkeletalBuildLibrary`, and this library only reads back
 * what that produced.
 */
UCLASS()
class ELYSIUMUE_API UElysiumCharacterBakeLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Bones in a skeleton's tree. The bake and the verifier both report it. */
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
	 * The verifier's check against the container's own material table, and the one thing about a
	 * baked body that fails completely silently: a material instance keeps its slot, its parent and
	 * its factors whether or not its albedo survived the bake, so a body whose textures were lost
	 * looks exactly like a body whose materials were authored flat.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static bool MaterialHasTexture(const UMaterialInterface* Material);
};
