#include "ElysiumClipData.h"
#include "ElysiumJsonField.h"
#include "Visual/ElysiumAnimPostAdditive.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Animation/AnimSequence.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace ElysiumCookedClip
{
	FString Text(const TSharedPtr<FJsonObject>& Object)
	{
		FString Result;
		FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Result));
		return Result;
	}

	bool Read(UElysiumClipData& Data, const FString& Json, FString& Error)
	{
		TSharedPtr<FJsonObject> Object;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object.IsValid()
			|| ElysiumJson::Str(Object.ToSharedRef(), TEXT("schemaVersion")) != TEXT("1.0.0"))
		{
			Error = TEXT("invalid clip-data JSON/schema");
			return false;
		}
		Data.AssetId = ElysiumJson::Str(Object.ToSharedRef(), TEXT("assetId"));
		Data.OwnerRoot = ElysiumJson::Str(Object.ToSharedRef(), TEXT("ownerRoot"));
		Data.Label = ElysiumJson::Str(Object.ToSharedRef(), TEXT("label"));
		Data.SourceLabel = ElysiumJson::Str(Object.ToSharedRef(), TEXT("sourceLabel"));
		const TSharedPtr<FJsonObject>* Slice = nullptr;
		const TSharedPtr<FJsonObject>* Timeline = nullptr;
		const TSharedPtr<FJsonObject>* Expected = nullptr;
		if (!Object->TryGetObjectField(TEXT("slice"), Slice) || !Slice
			|| !Object->TryGetObjectField(TEXT("timelines"), Timeline) || !Timeline
			|| !Object->TryGetObjectField(TEXT("counts"), Expected) || !Expected)
		{
			Error = TEXT("clip-data declarations are incomplete");
			return false;
		}
		FElysiumNpcClipSet Parsed;
		if (!Parsed.LoadJsonText(Data.AssetId, Text(*Slice), Error)) return false;
		const FElysiumNpcClip* Descriptor = Parsed.Find(Data.Label);
		if (!Descriptor || Parsed.Clips.RowCount() != 1)
		{
			Error = TEXT("clip data does not declare exactly its own descriptor");
			return false;
		}
		Data.Descriptor = *Descriptor;
		FElysiumBlendTable Table;
		// The legacy return value means "has work", so an explicit empty timeline returns false.
		// The stated movement schema and diagnostics below distinguish that value from parse loss.
		Table.LoadJsonText(Text(*Timeline), Error);
		if (!Error.IsEmpty()) return false;
		Data.PoseParams = Table.PoseParams;
		if (const auto* Grid = Table.Find(Data.SourceLabel))
		{
			Data.Grid = *Grid;
			Data.bHasGrid = true;
		}
		if (const auto* Events = Table.Events.Find(Data.Label)) Data.Events = *Events;
		if (const auto* Movement = Table.Movement.Find(Data.Label)) Data.Movement = *Movement;
		Data.bMovementStated = Table.bMovementStated;
		if (!Data.bMovementStated || Table.bMovementSchemaUnreadable || Table.MalformedMovementRows)
		{
			Error = TEXT("clip movement was not stated or could not be decoded");
			return false;
		}
		for (const auto& Pair : TArray<TPair<FString,int32>>{
			{TEXT("swings"),Data.Descriptor.Swings.Num()}, {TEXT("envelopes"),Data.Descriptor.Envelopes.Num()},
			{TEXT("events"),Data.Events.Num()}, {TEXT("movement"),Data.Movement.Records.Num()}})
		{
			int32 Count = INDEX_NONE;
			if (!(*Expected)->TryGetNumberField(Pair.Key, Count) || Count != Pair.Value)
			{
				Error = TEXT("clip parser lost records in ") + Pair.Key;
				return false;
			}
		}
		bool Combo = false;
		if (!(*Expected)->TryGetBoolField(TEXT("combo"), Combo) || Combo != Data.Descriptor.Combo.bStated)
		{
			Error = TEXT("clip parser lost combo statedness");
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Knockbacks = nullptr;
		if (!(*Expected)->TryGetArrayField(TEXT("knockbacks"), Knockbacks) || !Knockbacks
			|| Knockbacks->Num() != Data.Descriptor.Swings.Num())
		{
			Error = TEXT("clip knockback declaration is absent");
			return false;
		}
		for (int32 Swing = 0; Swing < Knockbacks->Num(); ++Swing)
		{
			const TArray<TSharedPtr<FJsonValue>>* Buckets = nullptr;
			const auto& Actual = Data.Descriptor.Swings[Swing].KnockbackNames;
			if (!(*Knockbacks)[Swing]->TryGetArray(Buckets) || !Buckets || Buckets->Num() != Actual.Num())
			{
				Error = TEXT("clip parser lost a knockback bucket");
				return false;
			}
			for (int32 Bucket = 0; Bucket < Buckets->Num(); ++Bucket)
			{
				int32 Count = INDEX_NONE;
				if (!(*Buckets)[Bucket]->TryGetNumber(Count) || Count != Actual[Bucket].Names.Num())
				{
					Error = TEXT("clip parser lost a knockback candidate");
					return false;
				}
			}
		}
		Object->TryGetNumberField(TEXT("cycleSeconds"), Data.CycleSeconds);
		Object->TryGetNumberField(TEXT("groundDistanceCm"), Data.GroundDistanceCm);
		Object->TryGetNumberField(TEXT("groundSpeedCmPerSecond"), Data.GroundSpeedCmPerSecond);
		const TArray<TSharedPtr<FJsonValue>>* Axes = nullptr;
		if (Object->TryGetArrayField(TEXT("axes"), Axes) && Axes)
		{
			for (const auto& Value : *Axes)
			{
				const TSharedPtr<FJsonObject>* Axis = nullptr;
				if (!Value->TryGetObject(Axis) || !Axis) { Error = TEXT("invalid clip axis"); return false; }
				FElysiumClipAxis& Row = Data.Axes.AddDefaulted_GetRef();
				(*Axis)->TryGetStringField(TEXT("name"), Row.Name);
				(*Axis)->TryGetNumberField(TEXT("flags"), Row.Flags);
				(*Axis)->TryGetNumberField(TEXT("loop"), Row.Loop);
			}
		}
		return true;
	}
}

UElysiumClipData* UElysiumClipData::ApplyJson(UAnimationAsset* Asset, const FString& Json, FString& OutError)
{
	if (!Asset) { OutError = TEXT("clip asset is absent"); return nullptr; }
	auto* Data = NewObject<UElysiumClipData>(Asset);
	if (!ElysiumCookedClip::Read(*Data, Json, OutError)) return nullptr;
	if (const UAnimSequence* Sequence = Cast<UAnimSequence>(Asset))
	{
		const bool Tagged = Sequence->FindMetaDataByClass<UElysiumAnimPostAdditive>() != nullptr;
		if (Tagged != Data->Descriptor.IsAdditive() || Sequence->AdditiveAnimType != AAT_None)
		{
			OutError = TEXT("clip flags disagree with native post-additive metadata");
			return nullptr;
		}
	}
	while (auto* Previous = Asset->FindMetaDataByClass<UElysiumClipData>()) Asset->RemoveMetaData(Previous);
	Asset->AddMetaData(Data);
	Asset->MarkPackageDirty();
	OutError.Reset();
	return Data;
}

FString UElysiumClipData::Verify(UAnimationAsset* Asset, const FString& Json)
{
	const auto* Actual = Asset ? Asset->FindMetaDataByClass<UElysiumClipData>() : nullptr;
	if (!Actual) return TEXT("clip metadata is absent");
	auto* Expected = NewObject<UElysiumClipData>();
	FString Error;
	if (!ElysiumCookedClip::Read(*Expected, Json, Error)) return Error;
	for (TFieldIterator<FProperty> It(StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		if (!It->Identical_InContainer(Actual, Expected)) return TEXT("saved clip data differs in ") + It->GetName();
	return FString();
}
