#include "ElysiumModelCatalogues.h"
#include "ElysiumBodyData.h"
#include "ElysiumContentPaths.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#endif

namespace
{
	template<typename T> void AddPath(const TSoftObjectPtr<T>& Ref, TSet<FSoftObjectPath>& Out)
	{
		if (!Ref.IsNull()) Out.Add(Ref.ToSoftObjectPath());
	}

	uint32 RestSeed(const FString& ModelPath, int32 Token)
	{
		// Same bytes as placed_models.fnv1a_32, not GetTypeHash/FName's platform hash.
		uint32 Value = 2166136261u;
		const FTCHARToUTF8 Bytes(*ModelPath);
		for (int32 I = 0; I < Bytes.Length(); ++I) Value = (Value ^ uint8(Bytes.Get()[I])) * 16777619u;
		const uint32 Placement = uint32(FMath::Max(0, Token));
		for (uint32 Shift = 0; Shift < 32; Shift += 8) Value = (Value ^ uint8(Placement >> Shift)) * 16777619u;
		return Value;
	}
}

void FElysiumCatalogueWieldModel::GatherPaths(TSet<FSoftObjectPath>& Out) const
{
	AddPath(Mesh, Out); AddPath(Skeleton, Out); AddPath(ReferenceClip, Out);
	for (const auto& Pair : NativeSequences) AddPath(Pair.Value, Out);
}

EElysiumCatalogueWieldResult UElysiumWieldCatalogue::Resolve(const FString& Classname, bool bFemale,
	const FElysiumCatalogueWieldModel*& OutModel, FString& OutError) const
{
	OutModel = nullptr; OutError.Reset();
	const auto* Item = Data.Items.Find(Classname.ToLower());
	if (!Item)
	{
		OutError = TEXT("item is absent from the cooked wield catalogue: ") + Classname;
		return EElysiumCatalogueWieldResult::UnknownItem;
	}
	if (!Item->bShowsWieldModel) return EElysiumCatalogueWieldResult::WorldModel;
	const auto& Ref = bFemale ? Item->Female : Item->Male;
	if (Ref.State == EElysiumCatalogueReferenceState::Absent)
	{
		OutError = TEXT("authored wield model is absent from source: ") + Ref.AssetId;
		return EElysiumCatalogueWieldResult::SourceAbsent;
	}
	if (Ref.State == EElysiumCatalogueReferenceState::Empty || Ref.State == EElysiumCatalogueReferenceState::Null)
		return EElysiumCatalogueWieldResult::NoGeometry;
	const auto* Model = Data.Models.Find(Ref.AssetId);
	if (!Model || Model->Mesh.IsNull() || Model->Mesh != Ref.Mesh || Model->Binding == EElysiumWieldBinding::None)
	{
		OutError = TEXT("invalid cooked wield reference: ") + Ref.AssetId;
		return EElysiumCatalogueWieldResult::InvalidCatalogue;
	}
	OutModel = Model;
	return EElysiumCatalogueWieldResult::Found;
}

const FElysiumCataloguePlacedClip* FElysiumCataloguePlacedModel::SelectRest(int32 PlacementToken) const
{
	uint64 Total = 0;
	for (const int32 Index : RestCandidates)
	{
		if (!Clips.IsValidIndex(Index)) return nullptr;
		Total += uint64(FMath::Max(1, Clips[Index].Weight));
	}
	if (!Total) return nullptr;
	uint64 Pick = uint64(RestSeed(ModelPath, PlacementToken)) % Total;
	for (const int32 Index : RestCandidates)
	{
		const uint64 Weight = uint64(FMath::Max(1, Clips[Index].Weight));
		if (Pick < Weight) return &Clips[Index];
		Pick -= Weight;
	}
	return nullptr;
}

bool FElysiumCataloguePlacedModel::CanUseStatic(bool bNeedsAnimation) const
{
	return !bNeedsAnimation && !bSourceAbsent && !bHasCloth && bStaticTopologyEquivalent && !StaticMesh.IsNull()
		&& (bStaticSourceRepresentation || (bStaticRestSuffices && bStaticEquivalentProven && bStaticEquivalent));
}

void FElysiumCataloguePlacedModel::GatherPaths(TSet<FSoftObjectPath>& Out) const
{
	AddPath(StaticMesh, Out); AddPath(SkeletalMesh, Out); AddPath(BodyData, Out);
	for (const auto& Pair : NativeSequences) AddPath(Pair.Value, Out);
	for (const auto& Pair : NativeBlendSpaces) AddPath(Pair.Value, Out);
	for (const auto& Clip : Clips)
	{
		AddPath(Clip.Sequence, Out); AddPath(Clip.BlendSpace, Out); AddPath(Clip.BaseCell, Out);
	}
}

bool UElysiumPropSkinCatalogue::ResolveMaterials(const FString& AssetId, bool bSkeletal, int32 Family,
	TArray<FElysiumCatalogueResolvedMaterial>& Out, FString& OutError) const
{
	Out.Reset(); OutError.Reset();
	const auto* Model = Data.Models.Find(AssetId);
	if (!Model) { OutError = TEXT("model is absent from cooked skins: ") + AssetId; return false; }
	const FString Kind = bSkeletal ? TEXT("skeletal") : TEXT("static");
	const auto* Rep = Model->Representations.FindByPredicate([&](const auto& R) { return R.Kind == Kind; });
	if (!Rep || Model->FamilyCount <= 0)
	{ OutError = TEXT("skin representation has no family table: ") + AssetId; return false; }
	const int32 Index = FMath::Clamp(Family, 0, Model->FamilyCount - 1);
	if (!Rep->Families.IsValidIndex(Index)) { OutError = TEXT("truncated cooked skin index space: ") + AssetId; return false; }
	const auto& Cells = Rep->Families[Index].Cells;
	TArray<FElysiumCatalogueResolvedMaterial> Result;
	for (const auto& Slot : Rep->Slots)
	{
		if (Slot.SkinReferences.IsEmpty() || !Cells.IsValidIndex(Slot.SkinReferences[0]) || !Cells[Slot.SkinReferences[0]].Material)
		{ OutError = TEXT("cooked skin has an invalid slot or missing material: ") + AssetId; return false; }
		Result.Add({Slot.Index, Slot.SlotName, Cells[Slot.SkinReferences[0]].Material.Get()});
	}
	Out = MoveTemp(Result);
	return true;
}

#if WITH_EDITOR
namespace
{
	template<typename T> bool IsNative(const FString& Id, const TCHAR* Prefix, const TSoftObjectPtr<T>& Ref)
	{
		const FString Path = FElysiumContentPaths::BakedUnit(Id, Prefix);
		return !Path.IsEmpty() && Ref.ToSoftObjectPath().ToString() == Path;
	}

	bool Validate(const FElysiumWieldCatalogueData& Data, FString& Error)
	{
		TSet<FString> Used;
		for (const auto& Pair : Data.Items)
		{
			const auto& Item = Pair.Value;
			if (Pair.Key != Item.Classname.ToLower() || !Item.AssetId.StartsWith(TEXT("vtmb:vdata:items/")))
			{ Error = TEXT("invalid item identity: ") + Pair.Key; return false; }
			for (const auto* Ref : {&Item.Female, &Item.Male})
			{
				if (Ref->State == EElysiumCatalogueReferenceState::Real)
				{
					const auto* Model = Data.Models.Find(Ref->AssetId);
					if (!Model || Model->Mesh != Ref->Mesh) { Error = TEXT("wield model closure differs: ") + Ref->AssetId; return false; }
					Used.Add(Ref->AssetId);
				}
				else if (!Ref->Mesh.IsNull()) { Error = TEXT("non-real wield reference carries a mesh"); return false; }
			}
		}
		if (Used.Num() != Data.Models.Num()) { Error = TEXT("unreferenced wield model"); return false; }
		for (const auto& Pair : Data.Models)
		{
			const auto& Model = Pair.Value;
			if (Pair.Key != Model.AssetId || !IsNative(Pair.Key, TEXT("SK"), Model.Mesh)
				|| !IsNative(Pair.Key, TEXT("SKEL"), Model.Skeleton) || Model.ReferenceOwner != Pair.Key
				|| Model.Binding == EElysiumWieldBinding::None || Model.ReferencePose.IsEmpty() || Model.Bodies.IsEmpty()
				|| (Model.Binding == EElysiumWieldBinding::SocketProp) != Model.bHasTrailTip
				|| (Model.bHasTrailTip && (Model.TrailTipBone != Model.MountBone || Model.TrailTipPosition.ContainsNaN() || Model.TrailTipRotation.ContainsNaN()))
				|| (Model.ReferencePoseSource != TEXT("bind") && Model.ReferencePoseSource != TEXT("clip"))
				|| (Model.ReferencePoseSource == TEXT("clip") && Model.ReferenceClip.IsNull()))
			{ Error = TEXT("invalid wield decision: ") + Pair.Key; return false; }
			TSet<FName> Bones;
			for (int32 I = 0; I < Model.ReferencePose.Num(); ++I)
			{
				const auto& Bone = Model.ReferencePose[I];
				if (Bone.Name.IsNone() || Bones.Contains(Bone.Name) || Bone.Parent < -1 || Bone.Parent >= I
					|| Bone.Position.ContainsNaN() || Bone.Rotation.ContainsNaN())
				{ Error = TEXT("invalid wield reference pose: ") + Pair.Key; return false; }
				Bones.Add(Bone.Name);
			}
		}
		return true;
	}

	bool Validate(const FElysiumPlacedCatalogueData& Data, FString& Error)
	{
		TSet<FString> Paths;
		for (const auto& Pair : Data.Models)
		{
			const auto& Row = Pair.Value;
			if (Pair.Key != Row.AssetId || Row.ModelPath.IsEmpty() || Paths.Contains(Row.ModelPath) || !Row.AcceptanceIssues.IsEmpty()
				|| (!Row.StaticMesh.IsNull() && !IsNative(Pair.Key, TEXT("SM"), Row.StaticMesh))
				|| (!Row.SkeletalMesh.IsNull() && !IsNative(Pair.Key, TEXT("SK"), Row.SkeletalMesh))
				|| (Row.bHasCloth && Row.SkeletalMesh.IsNull())
				|| (Row.bStaticSourceRepresentation && !Row.CanUseStatic(false))
				|| (Row.bStaticRestSuffices && (!Row.CanUseStatic(false) || !Row.bStaticEquivalentProven || !Row.bStaticEquivalent)))
			{ Error = TEXT("unaccepted placed model: ") + Pair.Key; return false; }
			Paths.Add(Row.ModelPath);
			if (Row.bSourceAbsent && (Row.SourceReason.IsEmpty() || !Row.StaticMesh.IsNull() || !Row.SkeletalMesh.IsNull() || !Row.Clips.IsEmpty()))
			{ Error = TEXT("invalid absent source reference: ") + Pair.Key; return false; }
			for (int32 I = 0; I < Row.Clips.Num(); ++I)
			{
				const auto& Clip = Row.Clips[I];
				if (Clip.Index != I || Clip.Owner != Row.AssetId || Clip.Label.IsEmpty() || !FMath::IsFinite(Clip.Fps)
					|| !FMath::IsFinite(Clip.BoundsRadiusCm) || Clip.BoundsRadiusCm < 0.
					|| (Row.bFullClipsRequired && Clip.State != TEXT("native"))
					|| (Clip.State == TEXT("native") && Clip.Sequence.IsNull() && Clip.BlendSpace.IsNull())
					|| (!Clip.BlendSpace.IsNull() && Clip.BaseCell.IsNull())
					|| (Clip.State != TEXT("native")
						&& !(Clip.State == TEXT("static-rest-only") && Row.bStaticRestSuffices && Row.CanUseStatic(false))
						&& !(Clip.State == TEXT("source-only") && Row.bStaticSourceRepresentation && Row.CanUseStatic(false))))
				{ Error = TEXT("invalid placed clip: ") + Pair.Key; return false; }
			}
			for (int32 Index : Row.RestCandidates)
				if (!Row.Clips.IsValidIndex(Index)) { Error = TEXT("placed rest index outside vocabulary"); return false; }
			for (const auto& Required : Row.RequiredClips)
				if (!Row.Clips.ContainsByPredicate([&](const auto& Clip) { return Clip.Label.Equals(Required, ESearchCase::IgnoreCase) && Clip.State == TEXT("native"); }))
				{ Error = TEXT("missing intrinsic native clip: ") + Pair.Key + TEXT(" / ") + Required; return false; }
		}
		return true;
	}

	bool Validate(const FElysiumPropSkinCatalogueData& Data, FString& Error)
	{
		for (const auto& Pair : Data.Models)
		{
			const auto& Model = Pair.Value;
			if (Model.AssetId != Pair.Key || Model.FamilyCount < 0 || Model.SkinReferenceCount < 0
				|| (Model.Representations.IsEmpty() && Model.SourceOnlyReason.IsEmpty()))
			{ Error = TEXT("invalid skin model identity/count"); return false; }
			TSet<FString> Kinds;
			for (const auto& Rep : Model.Representations)
			{
				const bool bSkeletal = Rep.Kind == TEXT("skeletal");
				if (Kinds.Contains(Rep.Kind) || (Rep.Kind != TEXT("static") && !bSkeletal)
					|| (bSkeletal ? !IsNative(Pair.Key, TEXT("SK"), Rep.SkeletalMesh) : !IsNative(Pair.Key, TEXT("SM"), Rep.StaticMesh))
					|| Rep.Families.Num() != Model.FamilyCount)
				{ Error = TEXT("invalid skin representation: ") + Pair.Key; return false; }
				Kinds.Add(Rep.Kind);
				for (int32 I = 0; I < Rep.Families.Num(); ++I)
				{
					const auto& Family = Rep.Families[I];
					if (Family.Index != I || Family.Cells.Num() != Model.SkinReferenceCount)
					{ Error = TEXT("truncated skin family/column index space: ") + Pair.Key; return false; }
					for (int32 J = 0; J < Family.Cells.Num(); ++J)
						if (Family.Cells[J].SkinReference != J || Family.Cells[J].SourceSlot < 0 || !Family.Cells[J].Material)
						{ Error = TEXT("skin material missing or misindexed: ") + Pair.Key; return false; }
				}
				TSet<FName> Names;
				for (int32 I = 0; I < Rep.Slots.Num(); ++I)
				{
					const auto& Slot = Rep.Slots[I];
					if (Slot.Index != I || Slot.SlotName.IsNone() || Names.Contains(Slot.SlotName) || Slot.SkinReferences.IsEmpty())
					{ Error = TEXT("invalid skin render slot: ") + Pair.Key; return false; }
					Names.Add(Slot.SlotName);
					for (const int32 Ref : Slot.SkinReferences)
					{
						if (Ref < 0 || Ref >= Model.SkinReferenceCount)
						{ Error = TEXT("skin reference outside column space"); return false; }
						for (const auto& Family : Rep.Families)
							if (Family.Cells[Ref].MaterialId != Family.Cells[Slot.SkinReferences[0]].MaterialId)
							{ Error = TEXT("folded render slot loses alternate material"); return false; }
					}
				}
			}
		}
		return true;
	}

	void NormalizeBooleans(const TSharedPtr<FJsonValue>& Value)
	{
		if (Value->Type == EJson::Array)
		{
			for (const auto& Child : Value->AsArray()) NormalizeBooleans(Child);
		}
		else if (Value->Type == EJson::Object)
		{
			const auto Object = Value->AsObject();
			TMap<FString, TSharedPtr<FJsonValue>> Aliases;
			for (const auto& Pair : Object->Values)
			{
				NormalizeBooleans(Pair.Value);
				if (Pair.Value->Type == EJson::Boolean && !Pair.Key.IsEmpty())
				{
					FString Name(Pair.Key); Name[0] = FChar::ToUpper(Name[0]);
					Aliases.Add(TEXT("b") + Name, Pair.Value);
				}
			}
			for (const auto& Pair : Aliases) Object->SetField(Pair.Key, Pair.Value);
		}
	}

	template<typename T> T* Apply(T* Asset, const FString& Json, const TCHAR* Kind, FString& Error)
	{
		Error.Reset();
		TSharedPtr<FJsonObject> Object;
		FString Version, ActualKind;
		const TSharedPtr<FJsonObject>* Data = nullptr;
		if (!Asset || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object.IsValid()
			|| !Object->TryGetStringField(TEXT("schemaVersion"), Version) || Version != TEXT("1.0.0")
			|| !Object->TryGetStringField(TEXT("catalogueKind"), ActualKind) || ActualKind != Kind
			|| !Object->TryGetObjectField(TEXT("data"), Data) || !Data)
		{ Error = TEXT("invalid catalogue schema/kind/data"); return nullptr; }
		NormalizeBooleans(MakeShared<FJsonValueObject>(*Data));
		decltype(Asset->Data) Candidate;
		FText ConversionError;
		if (!FJsonObjectConverter::JsonObjectToUStruct(Data->ToSharedRef(), &Candidate, 0, 0, true, &ConversionError))
		{ Error = TEXT("invalid catalogue fields: ") + ConversionError.ToString(); return nullptr; }
		if (!Validate(Candidate, Error)) return nullptr;
		// All validation precedes mutation, including hard material resolution during editor import.
		Asset->Data = MoveTemp(Candidate);
		Asset->MarkPackageDirty();
		return Asset;
	}
}
#endif

// Public editor APIs are inert in cooked builds; no parser or asset load is on a runtime lookup.
#define ELYSIUM_CATALOGUE_API(Class, Kind) \
Class* Class::ApplyJson(Class* Asset, const FString& Json, FString& Error) \
{ return ApplyCatalogue(Asset, Json, TEXT(Kind), Error); } \
FString Class::Verify(Class* Asset, const FString& Json) \
{ \
	FString Error; Class* Expected = NewObject<Class>(); \
	if (!Asset || !ApplyJson(Expected, Json, Error)) return Error.IsEmpty() ? TEXT("catalogue asset absent") : Error; \
	const FProperty* Field = FindFProperty<FProperty>(Class::StaticClass(), TEXT("Data")); \
	return Field && Field->Identical_InContainer(Asset, Expected) ? FString() : FString(TEXT("saved catalogue differs")); \
}

template<typename T> T* ApplyCatalogue(T* Asset, const FString& Json, const TCHAR* Kind, FString& Error)
{
#if WITH_EDITOR
	return Apply(Asset, Json, Kind, Error);
#else
	Error = TEXT("catalogue authoring is editor only"); return nullptr;
#endif
}

ELYSIUM_CATALOGUE_API(UElysiumWieldCatalogue, "WieldModels")
ELYSIUM_CATALOGUE_API(UElysiumPlacedModelCatalogue, "PlacedModels")
ELYSIUM_CATALOGUE_API(UElysiumPropSkinCatalogue, "PropSkins")
#undef ELYSIUM_CATALOGUE_API
