#include "ElysiumBodyData.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#if WITH_EDITOR
#include "JsonObjectConverter.h"
#endif
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

FElysiumNpcClip FElysiumBodySequence::SelectionClip() const
{
	FElysiumNpcClip Result;
	Result.Owner = Owner; Result.RawIndex = RawIndex; Result.Activity = Activity; Result.Weight = Weight;
	Result.Flags = Flags; Result.Frames = Frames; Result.Fps = Fps; Result.Fade = Fade;
	Result.ReachCm = ReachCm; Result.LowReachCm = LowReachCm;
	Result.Combo.Mask = ComboMask; Result.Combo.bStated = bHasCombo;
	return Result;
}

FElysiumNpcClipSet UElysiumBodyData::SelectionVocabulary(const FString& SelectorStem) const
{
	FElysiumNpcClipSet Result;
	// The deterministic draw has always been seeded by the cast key; moving the asset must
	// not change the selected variant. The cast lookup supplies that key explicitly.
	Result.Stem = SelectorStem;
	for (const auto& Row : Sequences) Result.Clips.Add(Row.Label, Row.SelectionClip());
	return Result;
}

const FElysiumBodySequence* UElysiumBodyData::Find(const FString& Label, const FString& Owner) const
{
	return Sequences.FindByPredicate([&](const auto& Row) {
		return Row.Label.Equals(Label, ESearchCase::IgnoreCase)
			&& (Owner.IsEmpty() || Row.Owner.Equals(Owner, ESearchCase::IgnoreCase));
	});
}

void UElysiumBodyData::GatherAnimationPaths(TSet<FSoftObjectPath>& Out) const
{
	auto Add = [&Out](const FElysiumBodyAnimationRef& Ref) {
		for (const auto& Path : {Ref.Sequence.ToSoftObjectPath(), Ref.BlendSpace.ToSoftObjectPath(), Ref.BaseCell.ToSoftObjectPath()})
			if (Path.IsValid()) Out.Add(Path);
	};
	for (const auto& Row : Sequences)
	{
		Add(Row.Assets);
		for (const auto& Layer : Row.Layers) Add(Layer);
	}
	for (const auto& Pair : NativeSequences) if (!Pair.Value.IsNull()) Out.Add(Pair.Value.ToSoftObjectPath());
	for (const auto& Pair : NativeBlendSpaces) if (!Pair.Value.IsNull()) Out.Add(Pair.Value.ToSoftObjectPath());
}

UElysiumBodyData* UElysiumBodyData::ApplyJson(UElysiumBodyData* Asset, const FString& Json, FString& OutError)
{
#if WITH_EDITOR
	if (!Asset) { OutError = TEXT("body data asset is absent"); return nullptr; }
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object.IsValid())
	{
		OutError = TEXT("invalid body-data JSON"); return nullptr;
	}
	FString Version;
	if (!Object->TryGetStringField(TEXT("schemaVersion"), Version) || Version != TEXT("1.0.0"))
	{
		OutError = TEXT("unsupported body-data schema"); return nullptr;
	}
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Owners = nullptr;
	if (!Object->TryGetArrayField(TEXT("sequences"), Rows) || !Rows
		|| !Object->TryGetArrayField(TEXT("includeOwners"), Owners) || !Owners)
	{
		OutError = TEXT("body-data declarations are incomplete"); return nullptr;
	}
	TArray<FElysiumBodySequence> ParsedSequences;
	TArray<FElysiumBodyIncludeOwner> ParsedOwners;
	for (const auto& Value : *Rows)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		FElysiumBodySequence Converted;
		if (!Value->TryGetObject(Row) || !Row)
		{
			OutError = TEXT("invalid body sequence row"); return nullptr;
		}
		TSharedRef<FJsonObject> Input = MakeShared<FJsonObject>(**Row);
		bool HasCombo = false;
		if (!Input->TryGetBoolField(TEXT("hasCombo"), HasCombo))
		{
			OutError = TEXT("body sequence omits combo statedness"); return nullptr;
		}
		Input->SetBoolField(TEXT("bHasCombo"), HasCombo);
		FText Reason;
		if (!FJsonObjectConverter::JsonObjectToUStruct(Input, &Converted, 0, 0, true, &Reason))
		{
			OutError = TEXT("body sequence: ") + Reason.ToString(); return nullptr;
		}
		ParsedSequences.Add(MoveTemp(Converted));
	}
	for (const auto& Value : *Owners)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		FElysiumBodyIncludeOwner Converted;
		if (!Value->TryGetObject(Row) || !Row || !FJsonObjectConverter::JsonObjectToUStruct((*Row).ToSharedRef(), &Converted, 0, 0, true))
		{
			OutError = TEXT("invalid body include owner"); return nullptr;
		}
		ParsedOwners.Add(MoveTemp(Converted));
	}
	Object->TryGetStringField(TEXT("assetId"), Asset->AssetId);
	Object->TryGetStringField(TEXT("ownerRoot"), Asset->OwnerRoot);
	Asset->Sequences = MoveTemp(ParsedSequences); Asset->IncludeOwners = MoveTemp(ParsedOwners);
	Asset->NativeSequences.Reset(); Asset->NativeBlendSpaces.Reset();
	const TSharedPtr<FJsonObject>* Products = nullptr;
	if (Object->TryGetObjectField(TEXT("nativeSequences"), Products) && Products)
		for (const TPair<FString,TSharedPtr<FJsonValue>>& Pair : (*Products)->Values)
			Asset->NativeSequences.Add(Pair.Key,TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(Pair.Value->AsString())));
	if (Object->TryGetObjectField(TEXT("nativeBlendSpaces"), Products) && Products)
		for (const TPair<FString,TSharedPtr<FJsonValue>>& Pair : (*Products)->Values)
			Asset->NativeBlendSpaces.Add(Pair.Key,TSoftObjectPtr<UBlendSpace>(FSoftObjectPath(Pair.Value->AsString())));
#if WITH_EDITORONLY_DATA
	Asset->AuthoringEvidence = Json;
#endif
	Asset->MarkPackageDirty();
	OutError.Reset();
	return Asset;
#else
	OutError = TEXT("editor only");
	return nullptr;
#endif
}

FString UElysiumBodyData::Verify(UElysiumBodyData* Asset, const FString& Json)
{
	auto* Expected = NewObject<UElysiumBodyData>();
	FString Error;
	if (!Asset || !ApplyJson(Expected, Json, Error)) return Error.IsEmpty() ? TEXT("body data is absent") : Error;
	for (const TCHAR* Field : {TEXT("AssetId"), TEXT("OwnerRoot"), TEXT("IncludeOwners"), TEXT("Sequences"), TEXT("NativeSequences"), TEXT("NativeBlendSpaces")})
	{
		const FProperty* Property = FindFProperty<FProperty>(StaticClass(), Field);
		if (!Property || !Property->Identical_InContainer(Asset, Expected)) return FString(TEXT("saved body data differs in ")) + Field;
	}
	return FString();
}
