#pragma once

#include "CoreMinimal.h"

class UElysiumCastData;
class UElysiumBodyData;
struct FElysiumCastModel;

// Synchronous preparation/debug access only. Gameplay ticks use NativeAnimationData's resident
// references. Names resolve through DA_Cast; no filename scan or legacy mount fallback exists.
namespace ElysiumCharacterAssets
{
	UElysiumCastData* Cast();
	const FElysiumCastModel* Model(const FString& Name, FString& OutError);
	UElysiumBodyData* Body(const FString& Name, FString& OutError);
	FString MeshPath(const FString& Name);
	FString SkeletonPath(const FString& Name);
	FString AnimationPath(const FString& Owner, const FString& Label, bool bBlendSpace = false);
	TArray<FString> BodyNames();
}
