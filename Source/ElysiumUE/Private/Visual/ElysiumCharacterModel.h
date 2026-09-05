#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UElysiumCharacterProvenance;

namespace ElysiumCharacterModel
{
	/** Pure source spelling -> full unit identity. No basename inference or asset I/O. */
	FString IdFromSource(const FString& Model);
	bool IsCanonicalId(const FString& ModelId);
	/** Validates the body actually supplied by native preparation, including mesh-aligned metadata. */
	const UElysiumCharacterProvenance* Validate(const FString& ModelId, const USkeletalMesh* Mesh, FString& OutError);
	/** Identity plus the actual mesh/recipe; never a basename or material permutation guess. */
	FString AnimationCacheIdentity(const FString& ModelId, const USkeletalMesh* Mesh);
}
