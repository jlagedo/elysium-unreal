#include "Visual/ElysiumPreparedOrnamentModels.h"

#include "ElysiumContentPaths.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"

namespace
{
	TMap<FObjectKey, TWeakPtr<FElysiumPreparedOrnamentModels>> PreparedOrnaments;
	FObjectKey Scope(const UObject* Owner)
	{
		return FObjectKey(Owner && Owner->GetWorld() ? static_cast<const UObject*>(Owner->GetWorld()) : Owner);
	}
}

FElysiumPreparedOrnamentModels::FElysiumPreparedOrnamentModels(UObject* InOwner, uint64 InEpoch)
	: Key(Scope(InOwner)), Owner(InOwner), Epoch(InEpoch) {}

FElysiumPreparedOrnamentModels::~FElysiumPreparedOrnamentModels()
{
	if (const auto* Entry = PreparedOrnaments.Find(Key))
		if (const auto Current = Entry->Pin(); !Current || Current.Get() == this) PreparedOrnaments.Remove(Key);
}

void FElysiumPreparedOrnamentModels::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Data);
	for (auto& Pair : Assets) Collector.AddReferencedObject(Pair.Value);
}

TSharedPtr<FElysiumPreparedOrnamentModels> FElysiumPreparedOrnamentModels::ForOwner(const UObject* InOwner)
{
	if (!InOwner || !IsInGameThread()) return nullptr;
	const auto* Entry = PreparedOrnaments.Find(Scope(InOwner));
	const auto Result = Entry ? Entry->Pin() : nullptr;
	return Result && Result->IsCurrent() ? Result : nullptr;
}

void FElysiumPreparedOrnamentModels::Release(const UObject* InOwner)
{
	if (!InOwner || !IsInGameThread()) return;
	const FObjectKey InKey = Scope(InOwner);
	if (const auto* Entry = PreparedOrnaments.Find(InKey))
		if (const auto Current = Entry->Pin(); !Current || Current->Owner.Get() == InOwner) PreparedOrnaments.Remove(InKey);
}

bool FElysiumPreparedOrnamentModels::IsCurrent() const
{
	if (!Owner.IsValid() || !IsInGameThread()) return false;
	const auto* Entry = PreparedOrnaments.Find(Key);
	return Entry && Entry->Pin().Get() == this;
}

bool FElysiumPreparedOrnamentModels::GatherPaths(const UElysiumOrnamentCatalogue* Catalogue,
	TSet<FSoftObjectPath>& Out, FString& Error)
{
	Error.Reset();
	if (!Catalogue) { Error = TEXT("ornament catalogue is not resident"); return false; }
	TSet<FSoftObjectPath> Paths;
	for (const auto& Pair : Catalogue->Data.Models)
	{
		const auto& Row = Pair.Value;
		// A recorded absence references nothing; there is no mesh to make resident for a path the
		// shipped install never carried.
		if (Row.bSourceAbsent) continue;
		if (Row.Mesh.IsNull() || Row.Skeleton.IsNull()
			|| Row.Mesh.ToString() != FElysiumContentPaths::BakedUnit(Row.AssetId, TEXT("SK"))
			|| Row.Skeleton.ToString() != FElysiumContentPaths::BakedUnit(Row.AssetId, TEXT("SKEL")))
		{ Error = TEXT("invalid canonical ornament representation: ") + Pair.Key; return false; }
		Row.GatherPaths(Paths);
	}
	Out.Append(Paths); return true;
}

TSharedPtr<FElysiumPreparedOrnamentModels> FElysiumPreparedOrnamentModels::Create(UObject* InOwner, uint64 InEpoch,
	UElysiumOrnamentCatalogue* Catalogue, const TArray<UObject*>& ResidentAssets, FString& Error)
{
	Error.Reset();
	if (!InOwner || !InEpoch || !IsInGameThread())
	{ Error = TEXT("ornament preparation requires owner, epoch and game thread"); return nullptr; }
	PreparedOrnaments.Remove(Scope(InOwner));
	TSet<FSoftObjectPath> Paths;
	if (!GatherPaths(Catalogue, Paths, Error)) return nullptr;
	TSharedPtr<FElysiumPreparedOrnamentModels> Result = MakeShareable(new FElysiumPreparedOrnamentModels(InOwner, InEpoch));
	Result->Data = Catalogue;
	for (UObject* Asset : ResidentAssets)
	{
		if (!IsValid(Asset)) { Error = TEXT("ornament preparation received an absent asset"); return nullptr; }
		Result->Assets.Add(FSoftObjectPath(Asset), Asset);
	}
	// Residency is the whole point: the request arrives from an animation event, mid-frame, with no
	// chance to wait for a stream. A path the preparation did not admit is a bake defect, not a
	// reason to load one here.
	for (const auto& Path : Paths)
		if (!Result->Assets.Contains(Path)) { Error = TEXT("ornament native reference was not prepared: ") + Path.ToString(); return nullptr; }
	PreparedOrnaments.Add(Result->Key, Result); return Result;
}

const UElysiumOrnamentCatalogue* FElysiumPreparedOrnamentModels::Catalogue() const
{
	return IsCurrent() ? Data.Get() : nullptr;
}

const FElysiumCatalogueOrnamentModel* FElysiumPreparedOrnamentModels::FindModel(const FString& RetailPath) const
{
	return IsCurrent() && Data ? Data->FindModel(RetailPath) : nullptr;
}

USkeletalMesh* FElysiumPreparedOrnamentModels::Mesh(const FString& RetailPath, FString& OutError) const
{
	OutError.Reset();
	if (!IsCurrent()) { OutError = TEXT("ornament preparation is released or superseded"); return nullptr; }
	const FElysiumCatalogueOrnamentModel* Row = Data ? Data->FindModel(RetailPath) : nullptr;
	if (!Row)
	{
		OutError = TEXT("not in DA_OrnamentModels");
		return nullptr;
	}
	if (Row->bSourceAbsent)
	{
		// The catalogue KNOWS this path: retail formatted it and the shipped install has no such
		// model, so retail's own `GetModelPtr` returned null and it took the failure tail. A
		// recorded absence rather than a gap in the bake.
		OutError = TEXT("the shipped install carries no such model (retail would take the "
			"'Could not create ornament prop model: %s' tail)");
		return nullptr;
	}
	USkeletalMesh* const Mesh = Cast<USkeletalMesh>(Assets.FindRef(Row->Mesh.ToSoftObjectPath()).Get());
	if (!Mesh) { OutError = TEXT("ornament mesh is not resident: ") + Row->AssetId; }
	return Mesh;
}
