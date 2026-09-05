#pragma once

#include "CoreMinimal.h"
#include "Engine/SkeletalMesh.h"
#include "ElysiumSkeletalMesh.generated.h"

/** Keeps imported morph deltas through the engine's build, DDC and cook paths. */
UCLASS()
class ELYSIUMUE_API UElysiumSkeletalMesh final : public USkeletalMesh
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual void BuildLODModel(FSkeletalMeshRenderData& RenderData, const ITargetPlatform* TargetPlatform, int32 LODIndex) override;
	virtual FString BuildDerivedDataKey(const ITargetPlatform* TargetPlatform) override;

	/** Reconstitute native morph rows from saved attributes and the built vertex mapping. */
	FString RestoreAuthoredMorphs(int32 LODIndex);
	FString RestoreAuthoredBasis(int32 LODIndex);
	FString GetMorphBuildError() const;
private:
	TMap<int32, FString> MorphBuildErrors;
#endif
};
