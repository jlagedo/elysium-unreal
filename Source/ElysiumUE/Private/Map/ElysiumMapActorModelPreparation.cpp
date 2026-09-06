#include "ElysiumMapActor.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumContentPaths.h"
#include "ElysiumModelCatalogues.h"
#include "ElysiumCastData.h"
#include "Map/ElysiumMapLog.h"
#include "Visual/ElysiumCharacterModel.h"
#include "Visual/ElysiumNativeAnimationData.h"
#include "Visual/ElysiumPreparedPropModels.h"
#include "Visual/ElysiumPreparedWieldModels.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/GameInstance.h"
#include "Materials/MaterialInterface.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

bool AElysiumMapActor::PreparePropAndWieldModels(const FElysiumEntityDefs& Definitions, FString& OutError)
{
	// Loading phase only. Runtime ForOwner/equip/SetModel paths consume these resident contexts.
	ReleasePropAndWieldModels();
	OutError.Reset();
	TArray<TSharedPtr<FStreamableHandle>> Handles;
	auto LoadBatch = [&Handles, &OutError](const TSet<FSoftObjectPath>& Paths)
	{
		if (Paths.IsEmpty()) return true;
		auto Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths.Array());
		if (!Handle.IsValid()) { OutError = TEXT("could not start native model context preload"); return false; }
		Handles.Add(Handle); // Retain all objects until both FGC contexts own their references.
		Handle->WaitUntilComplete();
		if (!Handle->HasLoadCompleted() || Handle->HasError())
		{ OutError = TEXT("native model context preload did not finish successfully"); return false; }
		for (const auto& Path : Paths)
			if (!Path.ResolveObject())
			{ OutError = TEXT("native model preload reference is absent: ") + Path.ToString(); return false; }
		return UElysiumNativeAnimationData::FinishPreparation(Handle, OutError);
	};
	const FSoftObjectPath PlacedPath(TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
	const FSoftObjectPath SkinsPath(TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
	const FSoftObjectPath WieldPath(TEXT("/ElysiumBaked/Models/_Corpus/DA_WieldModels.DA_WieldModels"));
	const TSet<FSoftObjectPath> Catalogues{PlacedPath, SkinsPath, WieldPath};
	if (!LoadBatch(Catalogues)) return false;
	auto* Placed = Cast<UElysiumPlacedModelCatalogue>(PlacedPath.ResolveObject());
	auto* Skins = Cast<UElysiumPropSkinCatalogue>(SkinsPath.ResolveObject());
	auto* Wield = Cast<UElysiumWieldCatalogue>(WieldPath.ResolveObject());
	if (!Placed || !Skins || !Wield)
	{ OutError = TEXT("native model catalogue reference has the wrong asset class"); return false; }

	// Milestone policy: admit the published catalogue, not just current Def.Model rows.
	// This retains old template/SetModel/ground-item coverage without runtime disk fallback.
	// A narrower residency budget is a later optimization, not a completion requirement.
	TSet<FString> Ids;
	for (const auto& Pair : Placed->Data.Models) Ids.Add(Pair.Key);
	for (const auto& Pair : Skins->Data.Models) Ids.Add(Pair.Key);
	TSet<FSoftObjectPath> Paths;
	for (const auto& Def : Definitions.Defs)
	{
		if (!Def.BrushMesh.IsEmpty()) Paths.Add(FSoftObjectPath(FElysiumContentPaths::BakedBrushMesh(MapName, Def.BrushMesh)));
		const FString Annotated = ElysiumCharacterModel::IdFromSource(Def.ModelMesh);
		if (!Annotated.IsEmpty()) Ids.Add(Annotated);
		for (const auto& Key : Def.Keys)
			if (Key.Key.Equals(TEXT("model"), ESearchCase::IgnoreCase))
			{
				const FString Id = ElysiumCharacterModel::IdFromSource(Key.Value);
				if (!Id.IsEmpty()) Ids.Add(Id);
			}
	}
	TArray<FString> ModelIds = Ids.Array(); ModelIds.Sort();
	if (!FElysiumPreparedPropModels::GatherPaths(Placed, Skins, ModelIds, Paths, OutError)
		|| !FElysiumPreparedWieldModels::GatherPaths(Wield, Paths, OutError)) return false;
	// Skin cells are hard references; include every family explicitly in the resident inventory.
	for (const auto& Pair : Skins->Data.Models)
		for (const auto& Rep : Pair.Value.Representations)
			for (const auto& Family : Rep.Families)
				for (const auto& Cell : Family.Cells)
				{
					if (!IsValid(Cell.Material.Get()))
					{ OutError = TEXT("skin catalogue material is absent: ") + Pair.Key + TEXT(" / ") + Cell.MaterialId; return false; }
					Paths.Add(FSoftObjectPath(Cell.Material.Get()));
				}

	// Static/source-absent inventory is not an animation-body request. Only actual cast
	// mesh/BodyData projections participate in the shared native animation adapters.
	auto* Native = GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumNativeAnimationData>() : nullptr;
	if (!Native) { OutError = TEXT("native animation service is absent during model context preparation"); return false; }
	if (!Native->PreparedCast() || !Native->OwnsPreparationEpoch(MapEpoch))
	{
		Native->PrepareMapModels({}, OutError, MapEpoch); // Bootstrap this loading epoch only.
		if (!OutError.IsEmpty()) return false;
	}
	const auto* NativeCast = Native->PreparedCast();
	if (!NativeCast) { OutError = TEXT("native cast was not loaded during model preparation"); return false; }
	TArray<FString> NativeModelIds;
	auto AddNativeBody = [&](const FString& Id)
	{
		if (const auto* Row = Placed->FindModel(Id); Row && Row->bSourceAbsent) return;
		const auto* Entry = NativeCast->Models.Find(Id);
		if (Entry && !Entry->Mesh.IsNull() && !Entry->BodyData.IsNull()) NativeModelIds.AddUnique(Entry->AssetId);
	};
	for (const FString& Id : ModelIds) AddNativeBody(Id);
	for (const auto& Pair : Wield->Data.Models) AddNativeBody(Pair.Key);
	// PrepareMany appends to the current admission; it cannot reset prior character/cinematic data.
	TSharedPtr<FStreamableHandle> NativeHandle;
	if (!NativeModelIds.IsEmpty()) NativeHandle = Native->PrepareMany(NativeModelIds, OutError);
	if (!OutError.IsEmpty()) return false;
	if (NativeHandle.IsValid())
	{
		Handles.Add(NativeHandle); NativeHandle->WaitUntilComplete();
		if (!UElysiumNativeAnimationData::FinishPreparation(NativeHandle, OutError)) return false;
	}
	if (!LoadBatch(Paths)) return false;
	TArray<UObject*> ResidentAssets;
	for (const auto& Path : Paths) ResidentAssets.Add(Path.ResolveObject());
#if WITH_EDITOR
	// This is the existing loading barrier, never a tick/equip fallback.
	FAssetCompilingManager::Get().FinishCompilationForObjects(ResidentAssets);
#endif
	const uint64 ContextEpoch = MapEpoch ? MapEpoch : uint64(GetUniqueID());
	auto Props = FElysiumPreparedPropModels::Create(this, ContextEpoch, Placed, Skins, ModelIds, ResidentAssets, OutError);
	if (!Props.IsValid()) return false;
	auto Weapons = FElysiumPreparedWieldModels::Create(this, ContextEpoch, Wield, ResidentAssets, OutError);
	if (!Weapons.IsValid())
	{
		ElysiumPreparedProps::Release(this);
		return false;
	}
	PropModelPreparation = MoveTemp(Props); WieldModelPreparation = MoveTemp(Weapons);
	UE_LOG(LogElysium, Log, TEXT("prepared native model contexts for %s: %d model IDs, %d resident references"),
		*MapName, ModelIds.Num(), ResidentAssets.Num());
	return true;
}

void AElysiumMapActor::ReleasePropAndWieldModels()
{
	// Wield scopes are world-keyed. Outgoing teardown must not unregister an incoming owner.
	if (ElysiumPreparedProps::ForOwner(this).Get() == PropModelPreparation.Get()) ElysiumPreparedProps::Release(this);
	if (FElysiumPreparedWieldModels::ForOwner(this).Get() == WieldModelPreparation.Get()) FElysiumPreparedWieldModels::Release(this);
	PropModelPreparation.Reset(); WieldModelPreparation.Reset();
}

EElysiumCharacterModelAdmission AElysiumMapActor::RequestCharacterModel(
	const FElysiumEntityHandle& Entity, const FString& ModelId, uint64 Generation, FString& OutError)
{
	OutError.Reset();
	CancelCharacterModel(Entity);
	FElysiumEntity* Live = EntityWorld ? EntityWorld->Resolve(Entity) : nullptr;
	if (!Live || Live->IsDead() || !Live->AsCombatCharacter() || Entity.Epoch != EntityWorld->GetEpoch()
		|| !ElysiumCharacterModel::IsCanonicalId(ModelId) || ElysiumCharacterModel::IdFromSource(Live->Model) != ModelId
		|| bMotorsRetired || RuntimePhase == EElysiumMapRuntimePhase::Failed)
	{ OutError = TEXT("character admission has no current live entity/model in this map epoch"); return EElysiumCharacterModelAdmission::Rejected; }
	auto* Native = GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumNativeAnimationData>() : nullptr;
	if (!Native) { OutError = TEXT("native animation service is unavailable"); return EElysiumCharacterModelAdmission::Rejected; }
	if (!Native->OwnsPreparationEpoch(MapEpoch))
	{ OutError = TEXT("native model preparation belongs to a different map epoch"); return EElysiumCharacterModelAdmission::Rejected; }
	if (Native->IsModelReady(ModelId)) return EElysiumCharacterModelAdmission::Ready;
	const auto Ticket = CharacterModelRequests.Begin(Entity, ModelId, Generation);
	const TWeakObjectPtr<AElysiumMapActor> WeakThis(this);
	const uint64 RequestId = Native->AdmitModelAsync(ModelId, MapEpoch,
		[WeakThis, Ticket](bool bSuccess, const FString& Error)
		{
			if (auto* Map = WeakThis.Get()) Map->CompleteCharacterModel(Ticket, bSuccess, Error);
		}, OutError);
	if (!RequestId)
	{ CharacterModelRequests.Remove(Ticket); return EElysiumCharacterModelAdmission::Rejected; }
	CharacterNativeAdmissionIds.Add(Entity, RequestId);
	return EElysiumCharacterModelAdmission::Pending;
}

void AElysiumMapActor::CancelCharacterModel(const FElysiumEntityHandle& Entity)
{
	CharacterModelRequests.Cancel(Entity);
	uint64 RequestId = 0;
	if (CharacterNativeAdmissionIds.RemoveAndCopyValue(Entity, RequestId))
		if (auto* Game = GetGameInstance())
			if (auto* Native = Game->GetSubsystem<UElysiumNativeAnimationData>()) Native->CancelModelAdmission(RequestId);
}

void AElysiumMapActor::CancelCharacterModelAdmissions()
{
	TArray<FElysiumEntityHandle> Entities; CharacterNativeAdmissionIds.GetKeys(Entities);
	for (const auto& Entity : Entities) CancelCharacterModel(Entity);
	CharacterModelRequests.Reset();
}

void AElysiumMapActor::CompleteCharacterModel(const FElysiumCharacterModelTicket& Ticket, bool bSuccess, const FString& Error)
{
	FElysiumEntity* Live = EntityWorld ? EntityWorld->Resolve(Ticket.Entity) : nullptr;
	const bool bCurrent = EntityWorld && Live && !bMotorsRetired && RuntimePhase != EElysiumMapRuntimePhase::Failed
		&& CharacterModelRequests.IsCurrent(Ticket, EntityWorld->GetEpoch(), Live->Handle,
			ElysiumCharacterModel::IdFromSource(Live->Model), !Live->IsDead());
	// Never let an old completion erase the native token of a replacement request.
	if (CharacterModelRequests.Remove(Ticket)) CharacterNativeAdmissionIds.Remove(Ticket.Entity);
	if (!bCurrent) return;
	if (!bSuccess)
	{
		if (RuntimePhase != EElysiumMapRuntimePhase::Active) bNativeAnimationPreloadFailed = true;
		UE_LOG(LogElysium, Warning, TEXT("character model admission %s (generation %llu) failed: %s"),
			*Ticket.ModelId, Ticket.Generation, *Error);
		return;
	}
	if (auto* Character = Live->AsCombatCharacter())
	{
		Character->CompletePreparedCharacterVisual(Ticket.Generation, Ticket.ModelId);
		if (!Character->GetSkeletalBody() && RuntimePhase != EElysiumMapRuntimePhase::Active)
			bNativeAnimationPreloadFailed = true;
	}
}
