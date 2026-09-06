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
#include "Substrate/ElysiumItemClasses.h"   // ElysiumItems::Find — an item record's ground model
#include "Substrate/ElysiumItemTable.h"     // FElysiumItemDef::PlayerModel
#include "Substrate/ElysiumNpcLoadout.h"    // the fists fallback and the none sentinel
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/GameInstance.h"
#include "Materials/MaterialInterface.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif

namespace
{
	// The prepared context's own load inventory for a set of model IDs: the placed/skin rows'
	// soft references plus the skin cells' hard materials (resident with the catalogue, listed so
	// the context can verify them by path). One gatherer serves the load-time batch and a late one.
	bool GatherPlacedModelPaths(const UElysiumPlacedModelCatalogue* Placed, const UElysiumPropSkinCatalogue* Skins,
		const TArray<FString>& ModelIds, TSet<FSoftObjectPath>& OutPaths, FString& OutError)
	{
		if (!FElysiumPreparedPropModels::GatherPaths(Placed, Skins, ModelIds, OutPaths, OutError)) return false;
		for (const FString& Id : ModelIds)
		{
			const auto* Skin = Skins->Data.Models.Find(Id);
			if (!Skin) continue;
			for (const auto& Rep : Skin->Representations)
				for (const auto& Family : Rep.Families)
					for (const auto& Cell : Family.Cells)
					{
						if (!IsValid(Cell.Material.Get()))
						{ OutError = TEXT("skin catalogue material is absent: ") + Id + TEXT(" / ") + Cell.MaterialId; return false; }
						OutPaths.Add(FSoftObjectPath(Cell.Material.Get()));
					}
		}
		return true;
	}

	bool AssetsCompiling(const TArray<UObject*>& Assets)
	{
#if WITH_EDITOR
		for (UObject* Asset : Assets)
		{
			if (const auto* Skeletal = Cast<USkeletalMesh>(Asset); Skeletal && Skeletal->IsCompiling()) return true;
			if (const auto* Static = Cast<UStaticMesh>(Asset); Static && Static->IsCompiling()) return true;
		}
#endif
		return false;
	}
}

void AElysiumMapActor::CollectMapModelIds(const FElysiumEntityDefs& Definitions,
	const FElysiumPreparedPropModels& Context, TSet<FString>& OutIds, TSet<FSoftObjectPath>& OutBrushPaths) const
{
	// Retail's precache list, derived the same way: every spawned entity names what it will show.
	// A derived ID the catalogues do not carry is dropped here rather than failing the map — the
	// entity that reaches for it reports the absence once, exactly as it did before.
	auto AddKnown = [&](const FString& Source)
	{
		const FString Id = ElysiumCharacterModel::IdFromSource(Source);
		if (!Id.IsEmpty() && Context.Knows(Id)) OutIds.Add(Id);
	};
	auto AddItemGroundModel = [&](const FString& Classname)
	{
		if (const FElysiumItemDef* Record = ElysiumItems::Find(Classname.TrimStartAndEnd())) AddKnown(Record->PlayerModel);
	};
	for (const auto& Def : Definitions.Defs)
	{
		if (!Def.BrushMesh.IsEmpty()) OutBrushPaths.Add(FSoftObjectPath(FElysiumContentPaths::BakedBrushMesh(MapName, Def.BrushMesh)));
		// The authored `model` key and its decoded static stem: props, NPCs, containers and the
		// makers (an npc_maker carries the model of the NPC it will spawn).
		const FString Annotated = ElysiumCharacterModel::IdFromSource(Def.ModelMesh);
		if (!Annotated.IsEmpty()) OutIds.Add(Annotated);
		for (const auto& Key : Def.Keys)
		{
			if (Key.Key.Equals(TEXT("model"), ESearchCase::IgnoreCase))
			{
				const FString Id = ElysiumCharacterModel::IdFromSource(Key.Value);
				if (!Id.IsEmpty()) OutIds.Add(Id);
			}
			// The weapon an NPC spawns wielding, and therefore the ground model it drops.
			else if (Key.Key.Equals(TEXT("additionalequipment"), ESearchCase::IgnoreCase)
				&& !ElysiumNpcLoadout::IsNoneSentinel(Key.Value)) AddItemGroundModel(Key.Value);
		}
		// A placed item authors no `model`: its loose body is the record's `playermodel`
		// (`CBaseCombatWeapon::Spawn`), and a container's authored spawn input names one by class.
		AddItemGroundModel(Def.Classname);
		for (const auto& Output : Def.Outputs)
			if (Output.Input.Equals(TEXT("SpawnItemInContainer"), ESearchCase::IgnoreCase)) AddItemGroundModel(Output.Param);
	}
	AddItemGroundModel(ElysiumNpcLoadout::FistsClassname);
	// The player's body is chargen data, not map data: the clan/sex/armor the record names.
	if (EntityWorld) AddKnown(EntityWorld->InitialPlayerModel());
}

bool AElysiumMapActor::PreparePropAndWieldModels(const FElysiumEntityDefs& Definitions, FString& OutError,
	bool bAdmitWholeCatalogue)
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

	// The context is created empty and admits the map's models once they are resident; that is
	// also the shape a late admission takes, so both paths run the same validation.
	const uint64 ContextEpoch = MapEpoch ? MapEpoch : uint64(GetUniqueID());
	auto Props = FElysiumPreparedPropModels::Create(this, ContextEpoch, Placed, Skins, {}, {}, OutError);
	if (!Props.IsValid()) return false;

	TSet<FString> Ids;
	TSet<FSoftObjectPath> Paths;
	if (bAdmitWholeCatalogue)
	{
		for (const auto& Pair : Placed->Data.Models) Ids.Add(Pair.Key);
		for (const auto& Pair : Skins->Data.Models) Ids.Add(Pair.Key);
	}
	CollectMapModelIds(Definitions, *Props, Ids, Paths);
	TArray<FString> ModelIds = Ids.Array(); ModelIds.Sort();
	if (!GatherPlacedModelPaths(Placed, Skins, ModelIds, Paths, OutError)
		|| !FElysiumPreparedWieldModels::GatherPaths(Wield, Paths, OutError)) return false;

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
	if (!Props->Admit(ModelIds, ResidentAssets, OutError)) { ElysiumPreparedProps::Release(this); return false; }
	auto Weapons = FElysiumPreparedWieldModels::Create(this, ContextEpoch, Wield, ResidentAssets, OutError);
	if (!Weapons.IsValid())
	{
		ElysiumPreparedProps::Release(this);
		return false;
	}
	PropModelPreparation = MoveTemp(Props); WieldModelPreparation = MoveTemp(Weapons);
	UE_LOG(LogElysium, Log, TEXT("prepared native model contexts for %s: %d model IDs (%s), %d resident references"),
		*MapName, ModelIds.Num(), bAdmitWholeCatalogue ? TEXT("whole catalogue") : TEXT("entity-derived"), ResidentAssets.Num());
	return true;
}

bool AElysiumMapActor::EnsurePlacedModelAdmitted(const FString& ModelPath)
{
	// True hands the call to the ordinary prepared path, including its own one-time failure
	// report for a model the catalogues never carried. False is the one case that improves with
	// time: a known model that simply was not admitted at load.
	const FString Id = ElysiumPreparedProps::ModelId(ModelPath);
	if (!PropModelPreparation.IsValid() || Id.IsEmpty() || PropModelPreparation->IsAdmitted(Id)
		|| !PropModelPreparation->Knows(Id) || bMotorsRetired || RuntimePhase == EElysiumMapRuntimePhase::Failed) return true;
	AdmitPlacedModelAsync(Id);
	return false;
}

void AElysiumMapActor::AdmitPlacedModelAsync(const FString& ModelId)
{
	if (PlacedModelAdmissions.Contains(ModelId) || !PropModelPreparation.IsValid()) return;
	UE_LOG(LogElysium, Warning,
		TEXT("late model admission on %s: '%s' was declared by no entity at load; loading it now (retail precached it synchronously inside SetModel)"),
		*MapName, *ModelId);
	FElysiumPlacedModelAdmission& Admission = PlacedModelAdmissions.Add(ModelId);
	// A cast body (an NPC-class model standing as a prop, or a script re-skinning a character)
	// needs the native animation service's own admission first; its callback continues here.
	auto* Native = GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumNativeAnimationData>() : nullptr;
	const auto* CastData = Native ? Native->PreparedCast() : nullptr;
	const auto* Entry = CastData ? CastData->Models.Find(ModelId) : nullptr;
	const auto* Row = PropModelPreparation->PlacedCatalogue()->FindModel(ModelId);
	const bool bNativeBody = Entry && !Entry->Mesh.IsNull() && !Entry->BodyData.IsNull() && !(Row && Row->bSourceAbsent);
	if (bNativeBody && !Native->IsModelReady(ModelId))
	{
		FString Error;
		const TWeakObjectPtr<AElysiumMapActor> WeakThis(this);
		Admission.NativeRequestId = Native->AdmitModelAsync(ModelId, MapEpoch,
			[WeakThis, ModelId](bool bSuccess, const FString& NativeError)
			{
				auto* Map = WeakThis.Get();
				if (!Map) return;
				if (auto* Found = Map->PlacedModelAdmissions.Find(ModelId)) Found->NativeRequestId = 0;
				if (bSuccess) Map->ContinuePlacedModelAdmission(ModelId);
				else Map->FinishPlacedModelAdmission(ModelId, TEXT("native body admission failed: ") + NativeError);
			}, Error);
		if (!Admission.NativeRequestId) FinishPlacedModelAdmission(ModelId, Error);
		return;
	}
	ContinuePlacedModelAdmission(ModelId);
}

void AElysiumMapActor::ContinuePlacedModelAdmission(const FString& ModelId)
{
	FElysiumPlacedModelAdmission* Admission = PlacedModelAdmissions.Find(ModelId);
	if (!Admission || !PropModelPreparation.IsValid()) return;
	if (!Admission->Handle.IsValid())
	{
		TSet<FSoftObjectPath> Paths; FString Error;
		if (!GatherPlacedModelPaths(PropModelPreparation->PlacedCatalogue(), PropModelPreparation->SkinCatalogue(), {ModelId}, Paths, Error))
		{ FinishPlacedModelAdmission(ModelId, Error); return; }
		const TWeakObjectPtr<AElysiumMapActor> WeakThis(this);
		// Stalled until the handle is stored: an already-resident batch completes synchronously.
		auto Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths.Array(),
			FStreamableDelegate::CreateWeakLambda(this, [WeakThis, ModelId]
			{
				if (auto* Map = WeakThis.Get()) Map->ContinuePlacedModelAdmission(ModelId);
			}), FStreamableManager::DefaultAsyncLoadPriority, false, true);
		if (!Handle.IsValid()) { FinishPlacedModelAdmission(ModelId, TEXT("could not start the async model load")); return; }
		Admission->Handle = Handle;
		Handle->StartStalledHandle();
		return;
	}
	if (!Admission->Handle->HasLoadCompleted() || Admission->Handle->HasError())
	{ FinishPlacedModelAdmission(ModelId, TEXT("async model load failed")); return; }
	// Editor builds may still be compiling the meshes that just loaded. Poll rather than block:
	// this runs on a live frame, not inside the loading screen.
	TArray<UObject*> Loaded; Admission->Handle->GetLoadedAssets(Loaded);
	if (AssetsCompiling(Loaded))
	{
		if (!Admission->Ticker.IsValid())
			Admission->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
				[this, ModelId](float)
				{
					if (auto* Pending = PlacedModelAdmissions.Find(ModelId))
					{
						Pending->Ticker.Reset();
						ContinuePlacedModelAdmission(ModelId);
					}
					return false;
				}), .05f);
		return;
	}
	FinishPlacedModelAdmission(ModelId, FString());
}

void AElysiumMapActor::FinishPlacedModelAdmission(const FString& ModelId, const FString& LoadError)
{
	FElysiumPlacedModelAdmission Admission;
	if (!PlacedModelAdmissions.RemoveAndCopyValue(ModelId, Admission)) return;
	if (Admission.Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Admission.Ticker);
	FString Error = LoadError;
	if (Error.IsEmpty() && PropModelPreparation.IsValid())
	{
		TSet<FSoftObjectPath> Paths;
		if (GatherPlacedModelPaths(PropModelPreparation->PlacedCatalogue(), PropModelPreparation->SkinCatalogue(), {ModelId}, Paths, Error))
		{
			TArray<UObject*> Resident;
			for (const auto& Path : Paths)
			{
				UObject* Object = Path.ResolveObject();
				if (!Object) { Error = TEXT("late model reference is absent: ") + Path.ToString(); break; }
				Resident.Add(Object);
			}
			if (Error.IsEmpty())
			{
#if WITH_EDITOR
				FAssetCompilingManager::Get().FinishCompilationForObjects(Resident); // the poll above drained the meshes; this is the cheap final barrier
#endif
				UElysiumNativeAnimationData::FinishPreparation(Admission.Handle, Error);
			}
			if (Error.IsEmpty()) PropModelPreparation->Admit({ModelId}, Resident, Error);
		}
	}
	if (!Error.IsEmpty())
	{
		UE_LOG(LogElysium, Warning, TEXT("late model admission on %s: '%s' failed: %s"), *MapName, *ModelId, *Error);
		return;
	}
	UE_LOG(LogElysium, Log, TEXT("late model admission on %s: '%s' is resident; rebuilding its bodies"), *MapName, *ModelId);
	RebuildBodiesForModel(ModelId);
}

void AElysiumMapActor::CancelPlacedModelAdmissions()
{
	auto* Native = GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumNativeAnimationData>() : nullptr;
	for (auto& Pair : PlacedModelAdmissions)
	{
		if (Pair.Value.Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Pair.Value.Ticker);
		if (Pair.Value.NativeRequestId && Native) Native->CancelModelAdmission(Pair.Value.NativeRequestId);
		if (Pair.Value.Handle.IsValid())
		{
			if (Pair.Value.Handle->HasLoadCompleted()) Pair.Value.Handle->ReleaseHandle();
			else Pair.Value.Handle->CancelHandle();
		}
	}
	PlacedModelAdmissions.Reset();
}

void AElysiumMapActor::RebuildBodiesForModel(const FString& ModelId)
{
	if (!EntityWorld || bMotorsRetired) return;
	// Characters own a separate admission (RequestCharacterModel); everything else that stands
	// on this model had its build stood down while the model was absent, and replays it now.
	TArray<FElysiumEntityHandle> Standing;
	for (const auto& Entity : EntityWorld->Entities())
		if (Entity && !Entity->IsDead() && !Entity->AsCombatCharacter()
			&& ElysiumCharacterModel::IdFromSource(Entity->Model) == ModelId) Standing.Add(Entity->Handle);
	for (const auto& Handle : Standing)
		if (FElysiumEntity* Entity = EntityWorld->Resolve(Handle)) Entity->OnRuntimeModelAdmitted();
}

void AElysiumMapActor::ReleasePropAndWieldModels()
{
	CancelPlacedModelAdmissions();
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
