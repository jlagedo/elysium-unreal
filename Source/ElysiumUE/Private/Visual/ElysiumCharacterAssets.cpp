#include "Visual/ElysiumCharacterAssets.h"

#include "ElysiumBodyData.h"
#include "ElysiumCastData.h"
#include "ElysiumContentPaths.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCharacterAssets, Log, All);

UElysiumCastData* ElysiumCharacterAssets::Cast()
{
	static TStrongObjectPtr<UElysiumCastData> Data;
	if (!Data.IsValid())
		Data.Reset(LoadObject<UElysiumCastData>(nullptr, *FElysiumContentPaths::CastData()));
	return Data.Get();
}

const FElysiumCastModel* ElysiumCharacterAssets::Model(const FString& Name, FString& OutError)
{
	UElysiumCastData* Data = Cast();
	if (!Data) { OutError = TEXT("native DA_Cast is absent; run uv run elysium import characters"); return nullptr; }
	return Data->FindModel(Name, OutError);
}

UElysiumBodyData* ElysiumCharacterAssets::Body(const FString& Name, FString& OutError)
{
	UElysiumCastData* Data = Cast();
	if (!Data) { OutError = TEXT("native DA_Cast is absent"); return nullptr; }
	const FElysiumCastModel* Entry = Data->FindModel(Name, OutError);
	TSoftObjectPtr<UElysiumBodyData> Ref;
	if (Entry) Ref = Entry->BodyData;
	else
	{
		// Existing debug/capture cinematic aliases carry an exact root suffix. Resolve the
		// model through the cast, then the authored root through its cinematic table.
		FString ModelName, Root;
		if (!Name.Split(TEXT("__"), &ModelName, &Root, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			return nullptr;
		const FElysiumCinematicOwnerRef* Owner = Data->FindCinematic(ModelName, Root, OutError);
		if (!Owner) return nullptr;
		Ref = Owner->BodyData;
	}
	UElysiumBodyData* Result = Ref.LoadSynchronous();
	if (!Result) OutError = TEXT("native body data is absent: ") + Ref.ToString();
	else OutError.Reset();
	return Result;
}

FString ElysiumCharacterAssets::MeshPath(const FString& Name)
{
	FString Error;
	const FElysiumCastModel* Entry = Model(Name, Error);
	if (!Entry) UE_LOG(LogElysiumCharacterAssets, Warning, TEXT("mesh '%s': %s"), *Name, *Error);
	return Entry ? Entry->Mesh.ToString() : FString();
}

FString ElysiumCharacterAssets::SkeletonPath(const FString& Name)
{
	FString Error;
	const FElysiumCastModel* Entry = Model(Name, Error);
	if (Entry && !Entry->Skeleton.IsNull()) return Entry->Skeleton.ToString();
	UE_LOG(LogElysiumCharacterAssets, Warning, TEXT("skeleton '%s': %s"), *Name, *Error);
	return FString();
}

FString ElysiumCharacterAssets::AnimationPath(const FString& Owner, const FString& Label, bool bBlendSpace)
{
	FString Error;
	const UElysiumBodyData* Data = Body(Owner, Error);
	if (!Data)
	{
		UE_LOG(LogElysiumCharacterAssets, Warning, TEXT("animation owner '%s': %s"), *Owner, *Error);
		return FString();
	}
	if (bBlendSpace)
	{
		for (const auto& Pair : Data->NativeBlendSpaces)
			if (Pair.Key.Equals(Label, ESearchCase::IgnoreCase)) return Pair.Value.ToString();
	}
	else
	{
		for (const auto& Pair : Data->NativeSequences)
			if (Pair.Key.Equals(Label, ESearchCase::IgnoreCase)) return Pair.Value.ToString();
	}
	return FString(); // A caller may probe an optional derived clip before its raw counterpart.
}

TArray<FString> ElysiumCharacterAssets::BodyNames()
{
	TArray<FString> Names;
	if (const UElysiumCastData* Data = Cast())
		for (const auto& Pair : Data->Models)
			if (!Pair.Value.Mesh.IsNull()) Names.Add(Pair.Value.Stem);
	Names.Sort();
	return Names;
}
