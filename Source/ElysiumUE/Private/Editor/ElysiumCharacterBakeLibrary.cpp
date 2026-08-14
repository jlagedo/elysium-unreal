#include "ElysiumCharacterBakeLibrary.h"

#if WITH_EDITOR
#include "ElysiumContentPaths.h"

#include "Animation/Skeleton.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialParameters.h"
#include "UObject/Package.h"

namespace
{
	bool IsUnsaveable(const UObject* Object)
	{
		return Object != nullptr &&
			(Object->HasAnyFlags(RF_Transient) || Object->GetOutermost() == GetTransientPackage());
	}
}
#endif // WITH_EDITOR

int32 UElysiumCharacterBakeLibrary::SkeletonBoneCount(const USkeleton* Skeleton)
{
#if WITH_EDITOR
	return Skeleton != nullptr ? Skeleton->GetReferenceSkeleton().GetNum() : 0;
#else
	return 0;
#endif
}

bool UElysiumCharacterBakeLibrary::SkeletonHasMorphCurve(const USkeleton* Skeleton,
	const FName CurveName)
{
#if WITH_EDITOR
	if (Skeleton == nullptr)
	{
		return false;
	}
	const FCurveMetaData* MetaData = Skeleton->GetCurveMetaData(CurveName);
	return MetaData != nullptr && MetaData->Type.bMorphtarget;
#else
	return false;
#endif
}

FString UElysiumCharacterBakeLibrary::BakedAssetName(const FString& Raw)
{
#if WITH_EDITOR
	return FElysiumContentPaths::BakedAssetName(Raw);
#else
	return Raw;
#endif
}

bool UElysiumCharacterBakeLibrary::MaterialHasTexture(const UMaterialInterface* Material)
{
#if WITH_EDITOR
	if (Material == nullptr)
	{
		return false;
	}
	// OVERRIDDEN values only. `GetTextureParameterValue` defaults to resolving through the instance
	// chain to the parent's own default, and both character masters give every texture parameter a
	// real engine asset as its default -- WhiteSquareTexture and DefaultNormal. Reading the resolved
	// value therefore returns true on the first parameter of every material ever passed here, which
	// makes this the one check that cannot fail: an instance whose albedo never reached disk still
	// answers yes.
	const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
	TArray<FMaterialParameterInfo> Params;
	TArray<FGuid> Ids;
	Material->GetAllParameterInfoOfType(EMaterialParameterType::Texture, Params, Ids);
	for (const FMaterialParameterInfo& Info : Params)
	{
		UTexture* Value = nullptr;
		const bool bFound = Instance != nullptr
			? Instance->GetTextureParameterValue(Info, Value, /*bOveriddenOnly=*/true)
			: Material->GetTextureParameterValue(Info, Value);
		if (bFound && Value != nullptr && !IsUnsaveable(Value))
		{
			return true;
		}
	}
	return false;
#else
	return false;
#endif
}
