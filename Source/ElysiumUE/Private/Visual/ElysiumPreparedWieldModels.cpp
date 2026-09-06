#include "Visual/ElysiumPreparedWieldModels.h"
#include "Visual/ElysiumCharacterModel.h"
#include "ElysiumCharacterProvenance.h"
#include "ElysiumContentPaths.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"

namespace
{
	TMap<FObjectKey, TWeakPtr<FElysiumPreparedWieldModels>> PreparedWield;
	FObjectKey Scope(const UObject* Owner)
	{
		return FObjectKey(Owner && Owner->GetWorld() ? static_cast<const UObject*>(Owner->GetWorld()) : Owner);
	}
}

FElysiumPreparedWieldModels::FElysiumPreparedWieldModels(UObject* InOwner, uint64 InEpoch)
	: Key(Scope(InOwner)), Owner(InOwner), Epoch(InEpoch) {}

FElysiumPreparedWieldModels::~FElysiumPreparedWieldModels()
{
	if (const auto* Entry = PreparedWield.Find(Key))
		if (const auto Current = Entry->Pin(); !Current || Current.Get() == this) PreparedWield.Remove(Key);
}

void FElysiumPreparedWieldModels::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Data);
	for (auto& Pair : Assets) Collector.AddReferencedObject(Pair.Value);
}

TSharedPtr<FElysiumPreparedWieldModels> FElysiumPreparedWieldModels::ForOwner(const UObject* InOwner)
{
	if (!InOwner || !IsInGameThread()) return nullptr;
	const auto* Entry = PreparedWield.Find(Scope(InOwner));
	const auto Result = Entry ? Entry->Pin() : nullptr;
	return Result && Result->IsCurrent() ? Result : nullptr;
}

void FElysiumPreparedWieldModels::Release(const UObject* InOwner)
{
	if (!InOwner || !IsInGameThread()) return;
	const FObjectKey InKey = Scope(InOwner);
	if (const auto* Entry = PreparedWield.Find(InKey))
		if (const auto Current = Entry->Pin(); !Current || Current->Owner.Get() == InOwner) PreparedWield.Remove(InKey);
}

bool FElysiumPreparedWieldModels::IsCurrent() const
{
	if (!Owner.IsValid() || !IsInGameThread()) return false;
	const auto* Entry = PreparedWield.Find(Key);
	return Entry && Entry->Pin().Get() == this;
}

bool FElysiumPreparedWieldModels::GatherPaths(const UElysiumWieldCatalogue* Catalogue,
	TSet<FSoftObjectPath>& Out, FString& Error)
{
	Error.Reset();
	if (!Catalogue) { Error = TEXT("wield catalogue is not resident"); return false; }
	TSet<FSoftObjectPath> Paths;
	for (const auto& Pair : Catalogue->Data.Models)
	{
		const auto& Row = Pair.Value;
		if (Pair.Key != Row.AssetId || !ElysiumCharacterModel::IsCanonicalId(Row.AssetId)
			|| Row.Mesh.IsNull() || Row.Skeleton.IsNull()
			|| Row.Mesh.ToString() != FElysiumContentPaths::BakedUnit(Row.AssetId, TEXT("SK"))
			|| Row.Skeleton.ToString() != FElysiumContentPaths::BakedUnit(Row.AssetId, TEXT("SKEL")))
		{ Error = TEXT("invalid canonical wield representation: ") + Pair.Key; return false; }
		Row.GatherPaths(Paths);
	}
	Out.Append(Paths); return true;
}

TSharedPtr<FElysiumPreparedWieldModels> FElysiumPreparedWieldModels::Create(UObject* InOwner, uint64 InEpoch,
	UElysiumWieldCatalogue* Catalogue, const TArray<UObject*>& ResidentAssets, FString& Error)
{
	Error.Reset();
	if (!InOwner || !InEpoch || !IsInGameThread()) { Error = TEXT("wield preparation requires owner, epoch and game thread"); return nullptr; }
	PreparedWield.Remove(Scope(InOwner));
	TSet<FSoftObjectPath> Paths;
	if (!GatherPaths(Catalogue, Paths, Error)) return nullptr;
	TSharedPtr<FElysiumPreparedWieldModels> Result = MakeShareable(new FElysiumPreparedWieldModels(InOwner, InEpoch));
	Result->Data = Catalogue;
	for (UObject* Asset : ResidentAssets)
	{
		if (!IsValid(Asset)) { Error = TEXT("wield preparation received an absent asset"); return nullptr; }
		Result->Assets.Add(FSoftObjectPath(Asset), Asset);
	}
	for (const auto& Path : Paths)
		if (!Result->Assets.Contains(Path)) { Error = TEXT("wield native reference was not prepared: ") + Path.ToString(); return nullptr; }
	for (const auto& Pair : Catalogue->Data.Models)
	{
		const auto& Row = Pair.Value;
		auto* Mesh = Cast<USkeletalMesh>(Result->Assets.FindRef(Row.Mesh.ToSoftObjectPath()).Get());
		auto* Skeleton = Cast<USkeleton>(Result->Assets.FindRef(Row.Skeleton.ToSoftObjectPath()).Get());
		if (!Mesh || !Skeleton || Mesh->GetSkeleton() != Skeleton)
		{ Error = TEXT("wield mesh/private skeleton differ: ") + Row.AssetId; return nullptr; }
		if (!Row.ReferenceClip.IsNull() && !Cast<UAnimSequence>(Result->Assets.FindRef(Row.ReferenceClip.ToSoftObjectPath()).Get()))
		{ Error = TEXT("wield reference pose clip is not resident: ") + Row.AssetId; return nullptr; }
		for (const auto& Clip : Row.NativeSequences)
			if (!Cast<UAnimSequence>(Result->Assets.FindRef(Clip.Value.ToSoftObjectPath()).Get()))
			{ Error = TEXT("wield sequence is not resident: ") + Row.AssetId + TEXT(" / ") + Clip.Key; return nullptr; }
	}
	PreparedWield.Add(Result->Key, Result); return Result;
}

const UElysiumWieldCatalogue* FElysiumPreparedWieldModels::Catalogue() const { return IsCurrent() ? Data.Get() : nullptr; }

EElysiumCatalogueWieldResult FElysiumPreparedWieldModels::Resolve(const FString& Classname, bool bFemale,
	const FElysiumCatalogueWieldModel*& OutModel, FString& Error) const
{
	OutModel = nullptr;
	if (!IsCurrent()) { Error = TEXT("wield preparation is released or superseded"); return EElysiumCatalogueWieldResult::InvalidCatalogue; }
	return Data->Resolve(Classname, bFemale, OutModel, Error);
}

FElysiumWieldModelRef FElysiumPreparedWieldModels::AttachmentRef(const FElysiumCatalogueWieldModel& Model)
{
	FElysiumWieldModelRef Ref;
	Ref.Mesh = Model.Mesh; Ref.Binding = Model.Binding; Ref.MountBone = Model.MountBone; Ref.HandBone = Model.HandBone;
	return Ref;
}

USkeletalMesh* FElysiumPreparedWieldModels::Mesh(const FElysiumWieldModelRef& Ref,
	const USkeletalMesh* Wearer, FString& Error) const
{
	Error.Reset();
	if (!IsCurrent()) { Error = TEXT("wield preparation is released or superseded"); return nullptr; }
	const FElysiumCatalogueWieldModel* Row = nullptr;
	for (const auto& Pair : Data->Data.Models) if (Pair.Value.Mesh == Ref.Mesh) { Row = &Pair.Value; break; }
	if (!Row || Row->Binding != Ref.Binding || Row->MountBone != Ref.MountBone || Row->HandBone != Ref.HandBone)
	{ Error = TEXT("attachment differs from the prepared wield decision"); return nullptr; }
	const auto* Provenance = Wearer ? UElysiumCharacterProvenance::Find(Wearer) : nullptr;
	const auto* Compatibility = Provenance ? Row->Bodies.Find(Provenance->AssetId) : nullptr;
	if (!Compatibility) { Error = TEXT("wearer has no cooked wield compatibility record"); return nullptr; }
	const auto& Rig = Wearer->GetRefSkeleton();
	if (Compatibility->bMountPresent != (Rig.FindBoneIndex(Row->MountBone) != INDEX_NONE)
		|| Compatibility->bHandPresent != (Rig.FindBoneIndex(Row->HandBone) != INDEX_NONE))
	{ Error = TEXT("wearer bones differ from cooked wield compatibility"); return nullptr; }
	for (FName Bone : Compatibility->MatchedBones)
		if (Rig.FindBoneIndex(Bone) == INDEX_NONE) { Error = TEXT("wearer lost a matched wield bone: ") + Bone.ToString(); return nullptr; }
	return Cast<USkeletalMesh>(Assets.FindRef(Row->Mesh.ToSoftObjectPath()).Get());
}
