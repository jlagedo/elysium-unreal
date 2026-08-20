#include "ElysiumEntityWorld.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEditorLabels.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumLineService.h"
#include "ElysiumPlayer.h"
#include "ElysiumScriptHost.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumDialogueSession.h"
#include "Substrate/ElysiumEntityWorldShared.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSignData.h"
#include "Substrate/ElysiumSoundVolumeTable.h"
#include "ElysiumUseIcons.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogElysiumWorld);

// A/B toggle for the P1.5 brush bodies (per-entity convex collision + trigger overlaps). Read at
// Load, so it takes effect on the next map load (like elysium.BrushCollision for the world hulls).
static TAutoConsoleVariable<int32> CVarBrushBodies(
	TEXT("elysium.BrushBodies"),
	1,
	TEXT("Build per-brush-entity collision/overlap bodies at map load (1, default) or skip them (0)."),
	ECVF_Default);

namespace
{
	bool GElysiumTriggerResolutionEnabled = true;

	// Every world instance takes a unique epoch (game thread only), so a handle minted by one
	// map load never falsely resolves against the next. Starts at 1 — Teardown sets a world's
	// epoch to 0, which no live handle carries.
	uint32 GElysiumNextWorldEpoch = 1;

	// The maximum deliveries the queue may drain in one service pass before the loop guard
	// bails — catches a zero-delay output ring feeding itself (retail data has cycles).
	constexpr int32 GElysiumMaxDrainPerFrame = 10000;

	const TCHAR* const GCallerTarget = TEXT("!caller");
	const TCHAR* const GActivatorTarget = TEXT("!activator");

	float WeatherKeyFloat(const FElysiumEntityDef& Def, const TCHAR* Key, float Default)
	{
		if (const FString* Value = Def.Keys.Find(Key))
		{
			return FCString::Atof(**Value);
		}
		return Default;
	}

	// Teardown's shared shape for the world's weak-held engine components: destroy every one still
	// alive, then drop the list. Weak refs, because the map actor also frees them when it is
	// destroyed — this path covers a world rebuild on a surviving actor (e.g. reload).
	template <typename TComponent>
	void ElysiumWorldDestroyWeakComponents(TArray<TWeakObjectPtr<TComponent>>& Components)
	{
		for (const TWeakObjectPtr<TComponent>& Comp : Components)
		{
			if (TComponent* C = Comp.Get())
			{
				C->DestroyComponent();
			}
		}
		Components.Empty();
	}
}

namespace ElysiumEntityWorldShared
{
	const TCHAR* const GSelfTarget = TEXT("!self");
}

FElysiumEntityWorld::FElysiumEntityWorld(AActor* InOwner, UElysiumGameStateSubsystem* InGameState,
	const FElysiumWorldServices& InServices)
	: Owner(InOwner)
	, GameState(InGameState)
	, WorldServices(InServices)
	, Epoch(GElysiumNextWorldEpoch++)
{
	GameSoundBus = MakeUnique<FElysiumGameSoundBus>();
	// Cycle 10c — the law-record store, built beside the sound bus it is modelled on.
	LawEventBus = MakeUnique<ElysiumNpcWitness::FElysiumLawEventBus>();
	LineService = MakeUnique<FElysiumLineService>(WorldServices.Audio);
	// R5 — the chokepoints are never uninstrumented: the ring buffer (always-on history) and
	// the log/VLOG stream are installed before any entity spawns. Phase 2 UI adds more sinks.
	TUniquePtr<FElysiumRingBufferSink> RingSink = MakeUnique<FElysiumRingBufferSink>(*this, 1000);
	Ring = RingSink.Get();
	Sinks.Add(MoveTemp(RingSink));
	Sinks.Add(MakeUnique<FElysiumLogSink>(*this));
}

FElysiumEntityWorld::~FElysiumEntityWorld()
{
	if (LineService)
	{
		LineService->Shutdown();
	}
	Teardown();
}

void FElysiumEntityWorld::SetTriggerResolutionEnabled(bool bEnabled)
{
	if (GElysiumTriggerResolutionEnabled == bEnabled)
	{
		return;
	}
	GElysiumTriggerResolutionEnabled = bEnabled;
	UE_LOG(LogElysiumWorld, Display, TEXT("map trigger resolution %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

bool FElysiumEntityWorld::IsTriggerResolutionEnabled()
{
	return GElysiumTriggerResolutionEnabled;
}

double FElysiumEntityWorld::NowSeconds() const
{
	// The clock is the game state's. Without one — a headless test world, or the worldless probe
	// entity `elysium.classes` builds — fall back to the last time this world was ticked, so a think
	// that measures elapsed time still reads the caller's clock. Only RunThinks/RunPlayerThink see
	// the tick argument directly; everything else asks here.
	return GameState ? GameState->GameClock().GetNow() : LastTickNow;
}

// --- Load / spawn -----------------------------------------------------------------------

void FElysiumEntityWorld::Load(FElysiumEntityDefs&& InDefs)
{
	bActive = false;
	bSnapshotApplied = false;
	SnapshotEntityIndices.Reset();
	// The item catalogue registers one entity class per `vdata/items` definition, and the registry
	// resolves a classname once, at Create. So the catalogue has to be loaded before this map's
	// item entities are built — otherwise they spawn as inert records and stay that way.
	if (GameState)
	{
		if (UElysiumRulebookSubsystem* Rules = GameState->Rulebook())
		{
			Rules->Items();
		}
	}
	Defs = MoveTemp(InDefs);
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		if (Def.Classname.Equals(TEXT("worldspawn"), ESearchCase::IgnoreCase))
		{
			WeatherState.Configure(
				WeatherKeyFloat(Def, TEXT("wetness_fadein"), 0.0f),
				WeatherKeyFloat(Def, TEXT("wetness_fadeout"), 0.0f),
				WeatherKeyFloat(Def, TEXT("wetness_fadetarget"), 0.0f),
				NowSeconds());
			break;
		}
	}

	EntityList.Reserve(Defs.Num());
	NameIndex.Reserve(Defs.Num());
	ClassIndex.Reserve(Defs.Num());

	for (int32 i = 0; i < Defs.Defs.Num(); ++i)
	{
		const FElysiumEntityDef& D = Defs.Defs[i];
		// The handle index IS the def-array index (R3): stable, never recycled.
		TUniquePtr<FElysiumEntity> Ent = FElysiumClassRegistry::Get().Create(D, FElysiumEntityHandle(i, Epoch));
		Ent->World = this;   // the seam an entity uses to fire outputs (set before Spawn)

		if (!D.TargetName.IsEmpty())
		{
			NameIndex.Add(FName(*D.TargetName), i);
		}
		ClassIndex.Add(FName(*D.Classname), i);
		EntityList.Add(MoveTemp(Ent));
	}

	// Spawn pass — keyvalues are already applied (Construct); Spawn() is the leaf class's own
	// wiring (no-op for base/inert records in P1.4). Then attach the brush body (P1.5): after
	// Spawn() so a leaf class can have adjusted its own state first.
	const bool bBuildBodies = CVarBrushBodies.GetValueOnGameThread() != 0;
	for (const TUniquePtr<FElysiumEntity>& Ent : EntityList)
	{
		if (Ent)
		{
			Ent->bSpawnCalled = true;
			Ent->Spawn();
			Ent->EnsurePlacedModelBody();
			if (bBuildBodies)
			{
				BuildBrushBody(*Ent);
			}
		}
	}

	// Second pass (Source's Activate()): every entity has Spawn()'d and every body exists, so a
	// constraint (phys_hinge) can now resolve and wire its attached bodies.
	for (const TUniquePtr<FElysiumEntity>& Ent : EntityList)
	{
		if (Ent && !Ent->IsDead())
		{
			Ent->PostSpawn();
		}
	}

	// 11.9 — the spawn pass is finished, so this is what a rebuild of this map produces: record it
	// as the omission baseline a freeze diffs against (`docs/architecture/save-architecture.md` §4).
	Baseline.Reset();
	Baseline.SetNum(EntityList.Num());
	for (int32 i = 0; i < EntityList.Num(); ++i)
	{
		CaptureBaseline(i);
	}

	UE_LOG(LogElysiumWorld, Log, TEXT("world '%s' built dormant: %d entities (%d brush bodies), epoch %u"),
		*Defs.MapName, EntityList.Num(), Bodies.Num(), Epoch);
}

void FElysiumEntityWorld::PreloadMapAnimations()
{
	if (bActive)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("animation preload ignored after world '%s' activation"), *Defs.MapName);
		return;
	}
	RefreshAnimationPreload();
}

void FElysiumEntityWorld::RefreshAnimationPreload()
{

	for (const TUniquePtr<FElysiumEntity>& Ent : EntityList)
	{
		if (Ent && !Ent->IsDead())
		{
			Ent->PreloadForActivation();
		}
	}

	// SetAnimation references authored on output wires do not belong to the target entity's own
	// fields, so include them in the same closure. Dynamic Python strings cannot be recovered here;
	// skeletal props cover those by warming their compact per-model catalogs above.
	int32 OutputRefs = 0;
	for (const TUniquePtr<FElysiumEntity>& Source : EntityList)
	{
		if (!Source || Source->IsDead() || !Source->Def)
		{
			continue;
		}
		for (const FElysiumOutputDef& Output : Source->Def->Outputs)
		{
			if (Output.Param.IsEmpty()
				|| (!Output.Input.Equals(TEXT("SetAnimation"), ESearchCase::IgnoreCase)
					&& !Output.Input.Equals(TEXT("SetGesture"), ESearchCase::IgnoreCase)))
			{
				continue;
			}
			auto PreloadTarget = [&Output, &OutputRefs](FElysiumEntity& Target)
			{
				if (Target.PreloadAnimClip(Output.Param))
				{
					++OutputRefs;
				}
			};
			if (Output.Target.Equals(TEXT("!self"), ESearchCase::IgnoreCase))
			{
				PreloadTarget(*Source);
			}
			else if (!Output.Target.Equals(TEXT("!activator"), ESearchCase::IgnoreCase))
			{
				ForEachNamed(Output.Target, PreloadTarget);
			}
		}
	}
	if (bActive)
	{
		UE_LOG(LogElysiumWorld, Verbose,
			TEXT("world '%s' animation references walked (%d output refs, runtime refresh)"),
			*Defs.MapName, OutputRefs);
	}
	else
	{
		UE_LOG(LogElysiumWorld, Log,
			TEXT("world '%s' animation references walked (%d output refs)"),
			*Defs.MapName, OutputRefs);
	}
}

void FElysiumEntityWorld::Activate(double Now)
{
	if (bActive)
	{
		return;
	}
	LastTickNow = Now;
	// The pawn has reached its final frozen placement by this point. Publish its Source feet/view
	// transform before any late entity activation resolves !player (point_teleport spawnflag 1).
	if (FElysiumPlayer* PlayerEnt = FindPlayer())
	{
		PlayerEnt->SyncFromBody();
	}
	for (const TUniquePtr<FElysiumEntity>& Ent : EntityList)
	{
		if (Ent && !Ent->IsDead())
		{
			CallEntityActivate(*Ent);
		}
	}
	// Maker-owned NPCs and their bodies can be admitted by Activate. Resolve physical parenting only
	// after that complete barrier, while retaining logical parents for deliberately bodiless nodes.
	for (const TUniquePtr<FElysiumEntity>& Ent : EntityList)
	{
		if (Ent && !Ent->IsDead())
		{
			Ent->ResolveParentAttachment(true);
		}
	}
	// A fresh rebuild's omission baseline includes Source Activate. On restore, entities absent from
	// the sparse snapshot are also fresh rebuilds and take that same baseline. Only indices whose
	// state was actually restored retain the construction baseline, keeping their activation-derived
	// differences explicit without making every untouched map entity appear changed.
	for (int32 Index = 0; Index < EntityList.Num(); ++Index)
	{
		if (EntityList[Index]
			&& !EntityList[Index]->ActivationStateMustPersist()
			&& (!bSnapshotApplied || !SnapshotEntityIndices.Contains(Index)))
		{
			CaptureBaseline(Index);
		}
	}
	bActive = true;
	WeatherState.Tick(Now);
	PublishWetness();
	UE_LOG(LogElysiumWorld, Log, TEXT("(%8.3f) world '%s' activated, epoch %u"),
		Now, *Defs.MapName, Epoch);
}

void FElysiumEntityWorld::BuildBrushBody(FElysiumEntity& Ent)
{
	// R1 — only brush entities get a body; point/logic entities never do. A killed entity (a
	// class Spawn() may have self-destructed) gets nothing.
	if (!Owner || !Ent.Def || !Ent.Def->IsBrush() || Ent.Def->Hulls.Num() == 0 || Ent.IsDead())
	{
		return;
	}
	USceneComponent* Root = Owner->GetRootComponent();
	if (!Root)
	{
		return;
	}

	EElysiumBrushSolidity Sol = ElysiumBrushSolidityForClass(Ent.Def->Classname);
	if ((Ent.Def->Classname.Equals(TEXT("func_door"), ESearchCase::IgnoreCase)
		|| Ent.Def->Classname.Equals(TEXT("func_door_rotating"), ESearchCase::IgnoreCase))
		&& (Ent.SpawnFlags & 0x8) != 0)
	{
		Sol = EElysiumBrushSolidity::Passable;
	}
	// func_rotating NOT_SOLID (0x40) is per-entity, not per-class: the clock hands and the la_hub
	// blade set it, the sky's cloud/lightning rotators do not.
	if (Ent.Def->Classname.Equals(TEXT("func_rotating"), ESearchCase::IgnoreCase)
		&& (Ent.SpawnFlags & 0x40) != 0)
	{
		Sol = EElysiumBrushSolidity::Passable;
	}

	// Standard runtime-component recipe: NewObject → cook the setup + place → SetupAttachment →
	// RegisterComponent (which creates the physics body from the now-valid setup, at the origin).
	// P1.7 — a readable Outliner name (Body_<idx>_<name>_<class>); the exact canonical debug string
	// rides along as a component tag (engine-core.md: labels mirror the debug string).
	FName BodyName = NAME_None;
#if WITH_EDITOR
	const FString EntName = Ent.TargetName.IsEmpty() ? TEXT("noname") : Ent.TargetName;
	BodyName = ElysiumEditorObjectName(FString::Printf(TEXT("Body_%d_%s_%s"),
		Ent.Handle.Index, *EntName, *Ent.Def->Classname));
#endif
	UElysiumBrushComponent* Body = NewObject<UElysiumBrushComponent>(Owner, BodyName);
	Body->InitBrush(Ent.Handle, Ent.Def->Hulls, Sol);
	Body->SetupAttachment(Root);
	Body->SetRelativeLocation(Ent.Origin);   // hulls are entity-local; the live origin places them
	Body->RegisterComponent();
	Owner->AddInstanceComponent(Body);            // shows the body in the editor Outliner
#if WITH_EDITOR
	Body->ComponentTags.Add(FName(*Ent.DebugString()));
#endif

	Ent.Body = Body;
	Bodies.Add(Body);
	if (Ent.IsUsable() && !Ent.Def->bSky)
	{
		RegisterUseAnchor(Body, Ent.Handle);
	}
	if (!Ent.Def->BrushMesh.IsEmpty())
	{
		if (IElysiumEmbodiment* Embodiment = WorldServices.Embodiment)
		{
			Body->SetVisual(Embodiment->BuildBrushVisual(
				Ent.Def->BrushMesh, Body, Embodiment->BodyScaleFor(*Ent.Def), Ent.Def->bSky));
		}
	}

	// Construct/Spawn ran before a body existed. Apply every class's complete physical gate now;
	// trigger StartDisabled participates without pretending the entity is hidden.
	Ent.RefreshBrushBodyState();
}

FElysiumEntityHandle FElysiumEntityWorld::CreateRuntimeEntityNoSpawn(FElysiumEntityDef Def)
{
	if (Def.Classname.IsEmpty())
	{
		return FElysiumEntityHandle::Invalid();
	}

	// The synthesized def outlives the entity (it holds Def*), so own it here. Moving the unique_ptr
	// into RuntimeDefs does not move the pointed-to object, so a Def* taken before the move stays valid.
	TUniquePtr<FElysiumEntityDef> Owned = MakeUnique<FElysiumEntityDef>(MoveTemp(Def));
	const FElysiumEntityDef& Ref = *Owned;

	// The handle index continues past the map's def array; Resolve indexes EntityList directly, so an
	// append is all identity needs. (ResolveTargets copies target pointers before firing, so appending
	// mid-delivery — the maker's Spawn input runs during ServiceEvents — never invalidates a live scan.)
	const int32 Idx = EntityList.Num();
	TUniquePtr<FElysiumEntity> Ent = FElysiumClassRegistry::Get().Create(Ref, FElysiumEntityHandle(Idx, Epoch));
	Ent->World = this;

	if (!Ref.TargetName.IsEmpty())
	{
		NameIndex.Add(FName(*Ref.TargetName), Idx);
	}
	ClassIndex.Add(FName(*Ref.Classname), Idx);

	FElysiumEntity* Raw = Ent.Get();
	EntityList.Add(MoveTemp(Ent));
	RuntimeDefs.Add(MoveTemp(Owned));

	// The entity is live for I/O and findable immediately, but NOT yet Spawn()'d — CreateEntityNoSpawn's
	// contract, so a script can SetModel/SetName/SetOrigin on it before CallEntitySpawn runs its wiring.
	UE_LOG(LogElysiumWorld, Log, TEXT("(%8.3f) runtime create (no spawn) %s"), NowSeconds(), *Raw->DebugString());
	return Raw->Handle;
}

void FElysiumEntityWorld::CallEntitySpawn(FElysiumEntity& Ent)
{
	if (Ent.bSpawnCalled || Ent.IsDead())
	{
		return;   // idempotent: CallEntitySpawn on an already-spawned (or killed) entity is a no-op
	}
	Ent.bSpawnCalled = true;
	// Spawn() is the leaf's own wiring (the FElysiumNpc leaf stands its body/visual here); then attach a
	// brush body if this runtime entity is a brush (point NPCs/props/items early-out of BuildBrushBody).
	Ent.Spawn();
	Ent.EnsurePlacedModelBody();
	if (CVarBrushBodies.GetValueOnGameThread() != 0)
	{
		BuildBrushBody(Ent);
	}
	// A runtime-spawned entity has no "all entities" barrier to wait on; its attach targets (if any)
	// already exist, so run its second-phase init immediately after Spawn().
	Ent.PostSpawn();
	if (bActive)
	{
		CallEntityActivate(Ent);
		Ent.ResolveParentAttachment(true);
		// A runtime entity may be the late parent of map-authored children (the common maker-owned
		// NPC case). Rebind just that named cohort; no frame polling or unrelated PostSpawn reruns.
		if (!Ent.TargetName.IsEmpty())
		{
			for (const TUniquePtr<FElysiumEntity>& Candidate : EntityList)
			{
				if (Candidate && Candidate.Get() != &Ent && !Candidate->IsDead()
					&& Candidate->ParentName.Equals(Ent.TargetName, ESearchCase::IgnoreCase))
				{
					Candidate->ResolveParentAttachment(true);
				}
			}
		}
	}
	// 11.9 — a runtime entity's rebuild is this same create+spawn replayed from its saved def, so
	// its baseline is taken at the same point in its life as a def entity's.
	CaptureBaseline(Ent.Handle.Index);
	UE_LOG(LogElysiumWorld, Log, TEXT("(%8.3f) runtime spawn %s"), NowSeconds(), *Ent.DebugString());
}

void FElysiumEntityWorld::CallEntityActivate(FElysiumEntity& Ent)
{
	if (Ent.bActivateCalled || !Ent.bSpawnCalled || Ent.IsDead())
	{
		return;
	}
	Ent.bActivateCalled = true;
	Ent.Activate();
}

FElysiumEntityHandle FElysiumEntityWorld::SpawnRuntimeEntity(FElysiumEntityDef Def)
{
	// The fused form (npc_maker.Spawn): create + spawn in one call.
	const FElysiumEntityHandle H = CreateRuntimeEntityNoSpawn(MoveTemp(Def));
	if (FElysiumEntity* E = Resolve(H))
	{
		CallEntitySpawn(*E);
	}
	return H;
}

// --- The player entity (11.4, S3) --------------------------------------------------------

FElysiumEntityHandle FElysiumEntityWorld::SpawnPlayer()
{
	if (Player.IsSet())
	{
		return Player;   // one player per world
	}

	FElysiumEntityDef Def;
	Def.Classname  = ElysiumPlayerClassName().ToString();
	Def.TargetName = ElysiumPlayerTargetName();
	if (GameState)
	{
		const FElysiumPlayerRecord& Record = GameState->PlayerRecord();
		if (UElysiumRulebookSubsystem* Rules = GameState->Rulebook())
		{
			const FString PlayerModel = Rules->Clans().PlayerBodyModel(Record.Sheet.Clan(),
				/*bFemale*/ !Record.Sheet.IsMale(), FMath::Clamp(Record.ArmorSlot, 0, 5));
			if (!PlayerModel.IsEmpty())
			{
				Def.Keys.Add(TEXT("model"), PlayerModel);
			}
		}
	}
	// The origin is the pawn's; SpawnPlayer runs before the first tick and FElysiumPlayer::Spawn
	// samples the body, so the def's zero is never read as a position.
	Player = CreateRuntimeEntityNoSpawn(MoveTemp(Def));

	FElysiumPlayer* Ent = FindPlayer();
	if (!Ent)
	{
		Player = FElysiumEntityHandle::Invalid();
		UE_LOG(LogElysiumWorld, Error, TEXT("player entity could not be created"));
		return Player;
	}
	// Hydrate BEFORE Spawn(): Spawn seeds a health ceiling only when the record carried none.
	if (GameState)
	{
		Ent->Hydrate(GameState->PlayerRecord());
	}
	CallEntitySpawn(*Ent);

	// These are engine-created companions of the player, not map-authored entities. The Unofficial
	// Patch's IsIdling() indexes FindEntitiesByClass("viewmodel")[3] during setPlus(), before the
	// first-person rendering programme exists, so stand up the four addressable slots now and let
	// their ordinary CBaseAnimating `model` field carry the script write.
	for (int32 Slot = 0; Slot < ElysiumViewModelSlotCount; ++Slot)
	{
		FElysiumEntityDef ViewModelDef;
		ViewModelDef.Classname = ElysiumViewModelClassName().ToString();
		SpawnRuntimeEntity(MoveTemp(ViewModelDef));
	}

	UE_LOG(LogElysiumWorld, Log, TEXT("player entity live: %s (%d viewmodel slots)"),
		*Ent->DebugString(), ElysiumViewModelSlotCount);
	return Player;
}

FElysiumPlayer* FElysiumEntityWorld::FindPlayer() const
{
	FElysiumEntity* E = const_cast<FElysiumEntityWorld*>(this)->Resolve(Player);
	// The handle is only ever set by SpawnPlayer, so the static_cast is exact; going through
	// AsCombatCharacter would answer for NPCs too.
	return E ? static_cast<FElysiumPlayer*>(E) : nullptr;
}

FElysiumEntity* FElysiumEntityWorld::FindPlayerController() const
{
	return const_cast<FElysiumEntityWorld*>(this)->Resolve(PlayerControllerEntity);
}

FElysiumEntityHandle FElysiumEntityWorld::CreatePlayerControllerEntity()
{
	if (FindPlayerController())
	{
		return PlayerControllerEntity;
	}
	FElysiumPlayer* Source = FindPlayer();
	if (!Source)
	{
		return FElysiumEntityHandle::Invalid();
	}

	FElysiumEntityDef Def;
	Def.Classname = TEXT("npc_VPlayerController");
	Def.TargetName = TEXT("!playercontroller");
	Def.Origin = Source->Origin;
	if (!Source->Model.IsEmpty())
	{
		Def.Keys.Add(TEXT("model"), Source->Model);
	}
	Def.Keys.Add(TEXT("angles"), FString::Printf(TEXT("%g %g %g"),
		Source->Angles.X, Source->Angles.Y, Source->Angles.Z));

	PlayerControllerEntity = CreateRuntimeEntityNoSpawn(MoveTemp(Def));
	FElysiumCombatCharacter* Controller = static_cast<FElysiumCombatCharacter*>(Resolve(PlayerControllerEntity));
	if (!Controller)
	{
		PlayerControllerEntity = FElysiumEntityHandle::Invalid();
		return PlayerControllerEntity;
	}

	// This entity is an embodied duplicate, not a second character. Copy only state that can affect
	// the performance; the controller leaf has no AI or collision of its own.
	Controller->Origin = Source->Origin;
	Controller->Angles = Source->Angles;
	Controller->Model = Source->Model;
	Controller->Skin = Source->Skin;
	Controller->Disposition = Source->Disposition;
	Controller->DispositionLevel = Source->DispositionLevel;
	Controller->Sheet = Source->Sheet;
	Controller->Effects = Source->Effects;
	Controller->Health = Source->Health;
	Controller->MaxHealth = Source->MaxHealth;
	CallEntitySpawn(*Controller);
	// **Retail hides the real player's body and suppresses input.** `npc_VPlayerController` 
	// is the cinematic double; the real pawn receives `EF_NODRAW` (+0x60) and movement is 
	// blocked because the input layer respects the controller handle (`player + 0x1db0`).
	Source->SetHiddenByController(true);

	UE_LOG(LogElysiumWorld, Log, TEXT("player controller entity live: %s"), *Controller->DebugString());
	return PlayerControllerEntity;
}

bool FElysiumEntityWorld::RemovePlayerControllerEntity()
{
	FElysiumCombatCharacter* Controller = static_cast<FElysiumCombatCharacter*>(FindPlayerController());
	FElysiumPlayer* Dest = FindPlayer();
	if (!Controller)
	{
		PlayerControllerEntity = FElysiumEntityHandle::Invalid();
		return false;
	}

	if (Dest)
	{
		// Apply the final pose anchor before the stand-in disappears. SetModel goes through the
		// player's rebuild path only when the controller actually changed it. Origin and view are one
		// body transaction: splitting them would briefly place the pawn at the final mark with its old
		// view, and would issue two Unreal teleports for one retail SetAbs transform.
		Dest->SetRuntimeTransform(Controller->Origin, Controller->Angles);
		if (Dest->Model != Controller->Model)
		{
			Dest->SetRuntimeModel(Controller->Model);
		}
		Dest->Skin = Controller->Skin;
		Dest->Disposition = Controller->Disposition;
	}

	// Kill first: npc_VPlayerController releases its scripted motor while the skeletal component is
	// still a valid child of that motor. The visual can then be destroyed without leaving the
	// engine-side path follower holding a dead attachment.
	Controller->Kill();
	if (Controller->Visual)
	{
		Controller->Visual->DestroyComponent();
		Controller->Visual = nullptr;
	}
	PlayerControllerEntity = FElysiumEntityHandle::Invalid();

	// Un-hide the real body in its new pose.
	if (Dest)
	{
		Dest->SetHiddenByController(false);
	}

	return true;
}

void FElysiumEntityWorld::RenameEntity(FElysiumEntity& Ent, const FString& NewName)
{
	const int32 Idx = Ent.Handle.Index;
	if (!Ent.TargetName.IsEmpty())
	{
		NameIndex.RemoveSingle(FName(*Ent.TargetName), Idx);
	}
	Ent.TargetName = NewName;
	if (!NewName.IsEmpty())
	{
		NameIndex.Add(FName(*NewName), Idx);
	}
	NotifyVisualChanged(Ent);   // the debug label carries the name
}

void FElysiumEntityWorld::RegisterNpcBody(USkeletalMeshComponent* Component)
{
	if (Component)
	{
		NpcBodies.Add(Component);
	}
}

void FElysiumEntityWorld::RegisterPropBody(UPrimitiveComponent* Component,
	const FElysiumEntityHandle& UseOwner)
{
	if (Component)
	{
		PropBodies.Add(Component);
		if (UseOwner.IsSet())
		{
			RegisterUseAnchor(Component, UseOwner);
		}
	}
}

void FElysiumEntityWorld::RegisterUseAnchor(UPrimitiveComponent* Component,
	const FElysiumEntityHandle& OwnerHandle)
{
	if (!Component || !OwnerHandle.IsSet())
	{
		return;
	}
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		Bodily->RegisterUseAnchor(Component, OwnerHandle);
		const FElysiumEntity* Entity = Resolve(OwnerHandle);
		Bodily->SetUseAnchorEnabled(OwnerHandle, Entity && !Entity->IsInert());
	}
}

void FElysiumEntityWorld::RegisterTouchAnchor(UPrimitiveComponent* Component,
	const FElysiumEntityHandle& OwnerHandle)
{
	if (!Component || !OwnerHandle.IsSet())
	{
		return;
	}
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		Bodily->RegisterTouchAnchor(Component, OwnerHandle);
		const FElysiumEntity* Entity = Resolve(OwnerHandle);
		Bodily->SetTouchAnchorEnabled(OwnerHandle, Entity && !Entity->IsInert());
	}
}

void FElysiumEntityWorld::SetUseAnchorEnabled(const FElysiumEntityHandle& OwnerHandle, bool bEnabled)
{
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		Bodily->SetUseAnchorEnabled(OwnerHandle, bEnabled);
	}
}

void FElysiumEntityWorld::SetTouchAnchorEnabled(const FElysiumEntityHandle& OwnerHandle, bool bEnabled)
{
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		Bodily->SetTouchAnchorEnabled(OwnerHandle, bEnabled);
	}
}

void FElysiumEntityWorld::RegisterConstraintBody(UPhysicsConstraintComponent* Component)
{
	if (Component)
	{
		Constraints.Add(Component);
	}
}

void FElysiumEntityWorld::AddSink(TUniquePtr<IElysiumIOSink> InSink)
{
	// Appended to the live sink list, so it is notified at the same points as the always-on ring
	// buffer and log sinks. Owned here; Teardown frees it with the rest when the world dies.
	if (InSink)
	{
		Sinks.Add(MoveTemp(InSink));
	}
}

// --- Screen fade (P4.5 env_fade) --------------------------------------------------------

void FElysiumEntityWorld::StartScreenFade(const FLinearColor& Color, float Duration, float HoldTime,
	float MaxAlpha, bool bFadeIn, bool bAutoReverse)
{
	ScreenFade.bActive      = true;
	ScreenFade.Color        = Color;
	ScreenFade.MaxAlpha     = FMath::Clamp(MaxAlpha, 0.0f, 1.0f);
	ScreenFade.Duration     = FMath::Max(Duration, 0.0f);
	ScreenFade.HoldTime     = FMath::Max(HoldTime, 0.0f);
	ScreenFade.bFadeIn      = bFadeIn;
	ScreenFade.bAutoReverse = bAutoReverse;
	ScreenFade.StartTime    = NowSeconds();

	// 11.2 — announce it as well as hold it. The state stays here because it has the map's lifetime;
	// the announcement is what tells the publisher a fade *started* this frame (11.8).
	if (IElysiumPresenter* P = Presenter())
	{
		P->StartFade(ScreenFade.Color, ScreenFade.Duration, ScreenFade.HoldTime, ScreenFade.MaxAlpha,
			ScreenFade.bFadeIn, ScreenFade.bAutoReverse);
	}
}

bool FElysiumEntityWorld::GetScreenFade(FLinearColor& OutColor) const
{
	// The curve is the one CViewEffects::FadeCalculate runs (client.dll FUN_10197190) over the fade
	// CViewEffects::Fade built (FUN_10196fe0), in closed form: alpha ramps against FadeEnd, holds
	// until FadeReset, and the fade is then *dropped* — unless its auto-reverse bit is set, which
	// flips it to a fade-in and gives it one more Duration to uncover. `Fade` with a zero duration
	// never rebases FadeEnd/FadeReset onto the clock, so such a fade dies on its first frame.
	if (!ScreenFade.bActive || ScreenFade.Duration <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float T = (float)(NowSeconds() - ScreenFade.StartTime);
	const float Dur = ScreenFade.Duration;
	const float Max = ScreenFade.MaxAlpha;
	const float HoldEnd = Dur + ScreenFade.HoldTime;
	float Alpha;
	if (ScreenFade.bFadeIn)
	{
		// SF_FADE_IN clears every animating bit rather than setting one (CEnvFade::InputFade leaves
		// fadeFlags at 0), so FadeCalculate takes its flat branch: the colour sits at full alpha for
		// HoldTime + Duration and then vanishes. Reproduced as-is — no tutorial env_fade sets it.
		if (T > ScreenFade.HoldTime + Dur)
		{
			return false;
		}
		Alpha = Max;
	}
	else if (T < Dur)                                          // covering: 0 -> MaxAlpha
	{
		Alpha = Max * (T / Dur);
	}
	else if (T <= HoldEnd)                                     // held covered
	{
		Alpha = Max;
	}
	else if (ScreenFade.bAutoReverse && T < HoldEnd + Dur)     // uncovering: MaxAlpha -> 0
	{
		Alpha = Max * ((HoldEnd + Dur - T) / Dur);
	}
	else
	{
		return false;                                          // expired
	}
	OutColor = ScreenFade.Color;
	OutColor.A = FMath::Clamp(Alpha, 0.0f, 1.0f);
	return OutColor.A > KINDA_SMALL_NUMBER;
}

// --- Open sign window (P4.10) -----------------------------------------------------------

void FElysiumEntityWorld::OpenSign(const FElysiumEntityHandle& NewOwner,
	TSharedPtr<const FElysiumSignData> Data, float FadeInSeconds)
{
	if (ActiveUse.IsSet() && ActiveUse->Context.Owner != NewOwner)
	{
		EndActiveUse(EElysiumUseEndReason::Cancelled);
	}
	// A second OpenWindow replaces the first (CSignUI keeps one panel). The outgoing sign closes
	// silently: retail does not fire OnUseEnd for a panel the player never dismissed.
	if (OpenSignOwner.IsSet() && OpenSignOwner != NewOwner)
	{
		if (ActiveUse.IsSet() && ActiveUse->Context.Owner == OpenSignOwner)
		{
			EndActiveUse(EElysiumUseEndReason::Cancelled);
		}
		else
		{
			CloseSign(/*bSilent*/ true);
		}
	}
	OpenSignOwner = NewOwner;
	OpenSignData = MoveTemp(Data);
	OpenSignFadeIn = FMath::Max(FadeInSeconds, 0.0f);
	OpenSignTime = NowSeconds();

	if (IElysiumPresenter* P = Presenter())
	{
		P->OpenSign(OpenSignOwner, OpenSignData, OpenSignFadeIn);
	}
}

void FElysiumEntityWorld::CloseSign(bool bSilent)
{
	if (!OpenSignOwner.IsSet())
	{
		return;
	}
	// A prop_sign is an explicit substrate session. Route a user/script close through its owner so
	// OnUseEnd and OnReadEnd fire together and the active-use latch cannot outlive the panel.
	if (!bSilent && ActiveUse.IsSet() && ActiveUse->Context.Owner == OpenSignOwner)
	{
		EndActiveUse(EElysiumUseEndReason::Completed);
		return;
	}
	const FElysiumEntityHandle Closing = OpenSignOwner;
	OpenSignOwner = FElysiumEntityHandle();
	OpenSignData.Reset();
	OpenSignFadeIn = 0.0f;
	OpenSignTime = 0.0;

	if (IElysiumPresenter* P = Presenter())
	{
		P->CloseSign();
	}

	if (!bSilent)
	{
		// OnUseEnd is the tutorial's whole progression hook (popup_3 -> popup_4, popup_6 ->
		// chopdoor_brush.ScriptHide). Fire it through the entity so it takes the real output path.
		if (FElysiumEntity* Ent = Resolve(Closing))
		{
			static const FName OnUseEnd(TEXT("OnUseEnd"));
			Ent->FireOutput(OnUseEnd, FElysiumEntityHandle());
		}
	}
}

bool FElysiumEntityWorld::CanPlayerDismissSign() const
{
	if (!OpenSignOwner.IsSet() || !OpenSignData.IsValid())
	{
		return false;
	}
	if (!OpenSignData->bCloseOnLeftClick)
	{
		return false;   // a Rules block can opt out (the panel then waits for a scripted CloseWindow)
	}
	if (OpenSignData->MinShowTime > 0.0f &&
		(NowSeconds() - OpenSignTime) < double(OpenSignData->MinShowTime))
	{
		return false;   // still inside the enforced dwell
	}
	return true;
}

bool FElysiumEntityWorld::PlayerDismissSign()
{
	if (!CanPlayerDismissSign())
	{
		return false;
	}
	CloseSign(/*bSilent*/ false);
	return true;
}

FElysiumEntityHandle FElysiumEntityWorld::GetOpenSign(double* OutOpenTime) const
{
	if (OutOpenTime)
	{
		*OutOpenTime = OpenSignTime;
	}
	return OpenSignOwner;
}

void FElysiumEntityWorld::SetScriptedCamera(const FString& ShotFile, const FElysiumEntityHandle& Subject)
{
	IElysiumEmbodiment* E = Embodiment();
	if (!E)
	{
		return;
	}
	// "*The* cinematic camera mode": a second SetCamera replaces the first rather than stacking, so
	// the channel underneath never accumulates shots a conversation forgot to remove.
	ClearScriptedCamera();
	ScriptedCameraShot = E->PushCameraShot(ShotFile, Subject);
	ScriptedCameraFile = ScriptedCameraShot != 0 ? ShotFile : FString();
}

void FElysiumEntityWorld::ClearScriptedCamera()
{
	if (ScriptedCameraShot == 0)
	{
		return;
	}
	if (IElysiumEmbodiment* E = Embodiment())
	{
		E->PopCameraShot(ScriptedCameraShot);
	}
	ScriptedCameraShot = 0;
	ScriptedCameraFile.Reset();
}

bool FElysiumEntityWorld::SelectTrackCameraRole(bool bTargetRole,
	const FElysiumEntityHandle& TrackOwner)
{
	if (!TrackOwner.IsSet())
	{
		return false;
	}
	FElysiumEntityHandle& OwnerSlot = bTargetRole
		? TrackCameraTargetOwner
		: TrackCameraPositionOwner;
	const bool bRoleChanged = OwnerSlot != TrackOwner;
	OwnerSlot = TrackOwner;
	return bRoleChanged;
}

void FElysiumEntityWorld::PublishTrackCamera(bool bTargetRole,
	const FElysiumEntityHandle& TrackOwner, const FVector& Point, const FRotator& Rotation,
	float Roll, float FieldOfView, float BlendInSeconds, bool bCameraCut)
{
	const FElysiumEntityHandle SelectedOwner = bTargetRole
		? TrackCameraTargetOwner
		: TrackCameraPositionOwner;
	if (!TrackOwner.IsSet() || SelectedOwner != TrackOwner)
	{
		return; // a superseded track still advances its authored clock, but cannot reclaim the role
	}
	IElysiumEmbodiment* E = Embodiment();
	if (!E)
	{
		return;
	}
	if (bTargetRole)
	{
		TrackCameraTarget = Point;
	}
	else
	{
		TrackCameraPosition = Point;
		TrackCameraRotation = Rotation;
		TrackCameraRoll = Roll;
		TrackCameraFov = FieldOfView;
	}

	// A target may publish one queue entry before its paired position. Seed the missing half from
	// the live player view once; the position track replaces it later in the same drain.
	if (!TrackCameraPositionOwner.IsSet() && TrackCameraShot == 0)
	{
		FVector ViewPoint;
		FRotator ViewRotation;
		if (E->GetPlayerViewPoint(ViewPoint, ViewRotation))
		{
			TrackCameraPosition = ViewPoint;
			TrackCameraRotation = ViewRotation;
		}
	}

	FElysiumCameraShot Shot;
	Shot.Origin = TrackCameraPosition;
	Shot.Roll = TrackCameraRoll;
	Shot.FieldOfView = TrackCameraFov;
	Shot.BlendSeconds = FMath::Max(0.0f, BlendInSeconds);
	// camera_track already owns the full per-frame path, including target interpolation and hard
	// cuts. The generic shot channel's default 90-degree/second tracking limiter is for moving
	// entity anchors; applying it here adds a second interpolator and turns authored cuts into pans.
	Shot.MaxTurnRate = FVector::ZeroVector;
	// Selecting a new zero-blend owner supplies bCameraCut from the caller; a zero-time keyframe
	// crossing does the same while the selected owner continues. The first shot is also a cut.
	Shot.bCameraCut = bCameraCut
		|| (BlendInSeconds <= KINDA_SMALL_NUMBER && TrackCameraShot == 0);
	Shot.DebugName = TEXT("camera_track");
	if (TrackCameraTargetOwner.IsSet())
	{
		Shot.bUseLookAt = true;
		Shot.LookAt = TrackCameraTarget;
	}
	else
	{
		Shot.bUseLookAt = false;
		Shot.Rotation = TrackCameraRotation;
	}

	if (TrackCameraShot == 0)
	{
		TrackCameraShot = E->PushCameraShotValue(Shot);
	}
	else
	{
		E->UpdateCameraShotValue(TrackCameraShot, Shot);
	}
}

void FElysiumEntityWorld::RestoreTrackCamera(bool bTargetRole,
	const FElysiumEntityHandle& TrackOwner, float BlendOutSeconds)
{
	FElysiumEntityHandle& OwnerSlot = bTargetRole
		? TrackCameraTargetOwner
		: TrackCameraPositionOwner;
	if (OwnerSlot != TrackOwner)
	{
		return; // a stale Restore cannot tear down a newer track that owns the role
	}
	OwnerSlot = FElysiumEntityHandle::Invalid();

	if (!TrackCameraPositionOwner.IsSet() && !TrackCameraTargetOwner.IsSet())
	{
		ClearTrackCamera(BlendOutSeconds);
		return;
	}

	// Refresh the surviving role without repushing; UpdateCameraShotValue preserves the original
	// blend-in while changing whether this value carries an authored look-at.
	if (TrackCameraPositionOwner.IsSet())
	{
		PublishTrackCamera(false, TrackCameraPositionOwner, TrackCameraPosition,
			TrackCameraRotation, TrackCameraRoll, TrackCameraFov, 0.0f);
	}
	else
	{
		PublishTrackCamera(true, TrackCameraTargetOwner, TrackCameraTarget,
			TrackCameraRotation, TrackCameraRoll, TrackCameraFov, 0.0f);
	}
}

void FElysiumEntityWorld::ClearTrackCamera(float BlendOutSeconds)
{
	if (TrackCameraShot != 0)
	{
		if (IElysiumEmbodiment* E = Embodiment())
		{
			E->PopCameraShot(TrackCameraShot, FMath::Max(0.0f, BlendOutSeconds));
		}
	}
	TrackCameraShot = 0;
	TrackCameraPositionOwner = FElysiumEntityHandle::Invalid();
	TrackCameraTargetOwner = FElysiumEntityHandle::Invalid();
}

// --- Tick (move-first, then think — retail order) ----------------------------------------

void FElysiumEntityWorld::RunPlayerThink(double Now)
{
	if (!bActive)
	{
		return;
	}
	LastTickNow = Now;
	if (!IsTriggerResolutionEnabled())
	{
		return;
	}
	// The pre-move pass. Retail runs the player's own think inside CPlayerMove::RunCommand — the
	// PreThink -> think -> move -> PostThink shell the engine drives while draining `clc_move` —
	// and NOT in Physics_RunThinkFunctions, which is why it is a separate call rather than an
	// ordering inside RunThinks. It reads the body where the previous frame's move left it, which
	// is where the body still is: nothing has moved it since.
	FElysiumPlayer* PlayerEnt = FindPlayer();
	if (!PlayerEnt)
	{
		return;
	}
	if (PlayerEnt->IsInert() || PlayerEnt->NextThink == ELYSIUM_NEVER_THINK || PlayerEnt->NextThink > Now)
	{
		return;
	}
	PlayerEnt->NextThink = ELYSIUM_NEVER_THINK;
	PlayerEnt->Think();
}

void FElysiumEntityWorld::Tick(double Now)
{
	if (!bActive)
	{
		return;
	}
	LastTickNow = Now;
	if (!IsTriggerResolutionEnabled())
	{
		return;
	}
	WeatherState.Tick(Now);
	PublishWetness();
	// 11.4 — sample the pawn into the player entity first, so everything this frame reads (a think
	// measuring distance, a landmark offset, `pc.GetOrigin()`) sees where the player actually is.
	// The body moved earlier in THIS frame (step 4), which is the relationship retail has: the move
	// writes the player's origin out of the packet drain, and every think in `GameFrame` reads it.
	if (FElysiumPlayer* PlayerEnt = FindPlayer())
	{
		PlayerEnt->SyncFromBody();
	}
	// LIFE5 — the sequence-event pass, immediately before the thinks. Retail dispatches a body's
	// animation events out of the animating object's own frame advance, ahead of the AI that reads
	// what they set, so a footstep, an attachment toggle or a weapon-state event is already applied
	// when the think that depends on it runs.
	//
	// **Inert in production this slice**: nothing publishes a clip phase yet, so
	// `GetBodyClipPhase` answers false on every body and every cursor stays unarmed. The pass is
	// here now so the real arms land against a walk that already exists rather than against a walk
	// and a pass at once.
	AdvanceAnimEvents(Now);
	RunThinks(Now);
	ServiceEvents(Now);
	// Auto-Link/Auto-End observes the exact submitted voice handle after world events have had their
	// chance to replace or close the session. A stale completion therefore cannot advance a newer turn.
	UpdateDialogueAutomatic();
	// Dialogue's CInstancedSceneEntity equivalent is session-owned rather than an entity think. Run it
	// after automatic advancement so a completed voice tears down its old body clip before the new
	// turn's time-zero event is submitted, with no stale scene receiving another tick.
	if (DialogueSession)
	{
		DialogueSession->LineScene.Advance(Now);
	}
	// After the thinks, so a turn opened this frame already has its track bound — the same ordering
	// FElysiumChoreoScene::Think uses for RefreshFacialPose.
	RefreshDialogueLipsync(Now);
}

const FElysiumGameSoundBus& FElysiumEntityWorld::GameSounds() const
{
	return *GameSoundBus;
}

FElysiumGameSoundBus& FElysiumEntityWorld::GameSounds()
{
	return *GameSoundBus;
}

// ================= Cycle 10c — the world-event law lane's record store =========================
const ElysiumNpcWitness::FElysiumLawEventBus& FElysiumEntityWorld::LawEvents() const
{
	return *LawEventBus;
}

ElysiumNpcWitness::FElysiumLawEventBus& FElysiumEntityWorld::LawEvents()
{
	return *LawEventBus;
}
// ==============================================================================================

void FElysiumEntityWorld::EmitGameSound(const FVector& PositionCm, FName Category, float RadiusCm,
	const FElysiumEntityHandle& Source, float StealthHearingReductionCm)
{
	// Bind the authored table on the first emission rather than at construction: the rulebook loads
	// lazily, and a world that never makes a noise should never force the file open. An invalid
	// table stays unbound so the bus takes its silent normal-level fallback — the rulebook has
	// already logged why the load failed, and re-reporting it per category would bury it.
	if (!bSoundVolumesBound && GameState != nullptr)
	{
		bSoundVolumesBound = true;
		if (UElysiumRulebookSubsystem* Rules = GameState->Rulebook())
		{
			const FElysiumSoundVolumeTable& Table = Rules->SoundVolumes();
			if (Table.IsValid())
			{
				GameSoundBus->SetVolumeTable(&Table);
			}
		}
	}

	FElysiumGameSoundRequest Request;
	Request.Position = PositionCm;
	Request.Category = Category;
	Request.RadiusCm = RadiusCm;
	Request.Source = Source;
	Request.StealthHearingReductionCm = StealthHearingReductionCm;
	GameSoundBus->Emit(Request, NowSeconds());
}

void FElysiumEntityWorld::FadeGlobalWetness(float Target)
{
	WeatherState.Retarget(Target, NowSeconds());
	PublishWetness();
}

void FElysiumEntityWorld::PublishWetness()
{
	if (IElysiumWeather* Service = Weather())
	{
		FElysiumWeatherTransition Value;
		Value.CurrentWetness = WeatherState.CurrentWetness;
		Value.TargetWetness = WeatherState.TargetWetness;
		Value.StartTime = WeatherState.TransitionStart;
		Value.Duration = WeatherState.TransitionDuration;
		Service->ApplyWetness(Value);
	}
}

void FElysiumEntityWorld::AdvanceAnimEvents(double Now)
{
	// The same walk `RunThinks` makes, and deliberately so: an entity's timeline belongs to the same
	// list its think does, in the same order. It differs in its two gates — a body rather than a due
	// think, and no player skip, because the player's clips carry events too and there is no
	// pre-move pass that already advanced them.
	for (int32 Index = 0; Index < EntityList.Num(); ++Index)
	{
		const TUniquePtr<FElysiumEntity>& EntPtr = EntityList[Index];
		if (!EntPtr || EntPtr->GetSkeletalBody() == nullptr)
		{
			continue;   // nothing without a skeletal body has a clip to advance
		}
		EntPtr->AdvanceAnimEvents(Now);
	}
}

void FElysiumEntityWorld::RunThinks(double Now)
{
	// Retail: Physics_RunThinkFunctions runs before the event queue. A due, non-inert entity
	// thinks; its next-think is cleared first (Source semantics) so a Think() that doesn't
	// reschedule stops firing.
	//
	// The player is skipped: its think already ran in the pre-move pass, where retail runs it.
	// Leaving it in would think it twice a frame, and on the wrong side of the move.
	const int32 PlayerIndex = Player.IsSet() ? Player.Index : INDEX_NONE;
	for (int32 Index = 0; Index < EntityList.Num(); ++Index)
	{
		const TUniquePtr<FElysiumEntity>& EntPtr = EntityList[Index];
		if (!EntPtr || Index == PlayerIndex)
		{
			continue;
		}
		FElysiumEntity& Ent = *EntPtr;
		if (Ent.IsInert() || Ent.NextThink == ELYSIUM_NEVER_THINK || Ent.NextThink > Now)
		{
			continue;
		}
		Ent.NextThink = ELYSIUM_NEVER_THINK;
		Ent.Think();
	}
}

void FElysiumEntityWorld::ServiceEvents(double Now)
{
	// Drain every due event, including zero-delay chains queued *during* this pass, until the
	// queue has nothing due or the loop guard trips. Pause holds delivery unless steps are armed.
	int32 Delivered = 0;
	while (EventQueue.HasDue(Now))
	{
		if (EventQueue.IsPaused())
		{
			if (EventQueue.StepsPending() <= 0)
			{
				break;
			}
			EventQueue.ConsumeStep();
		}

		if (Delivered >= GElysiumMaxDrainPerFrame)
		{
			++LoopGuardTripCount;
			for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
			{
				Sink->OnLoopGuard(Now, Delivered);
			}
			break;
		}

		FElysiumIOEvent Ev;
		EventQueue.PopEarliest(Ev);
		DeliverEvent(Ev, Now);
		++Delivered;
	}
}

// --- Chokepoints ------------------------------------------------------------------------

void FElysiumEntityWorld::AddEvent(FElysiumIOEvent&& Event)
{
	// Chokepoint 2 (R5): the sole entry to the queue. Notify sinks before the queue consumes the
	// event (Add sorts it into place, so it is not necessarily the tail afterwards); sinks read
	// the event's fields, not its Serial, which Add assigns.
	const double Now = NowSeconds();

	// Retail's backward-clock guard: an enqueue at a curtime below the last one observed shifts the
	// new deadline forward by the rewind plus 0.005 rather than landing spuriously in the past
	// (`vampire.dll FUN_100ce210`, epsilon qword at 0x10454050; `docs/vtmb/game_runtime.md` → "Queue
	// service order, recursion and starvation"). Applied here because this is the one enqueue every
	// producer funnels through; the restore path uses AddRestored and is deliberately outside it.
	const double LastEnqueue = EventQueue.LastEnqueueValue();
	if (Now < LastEnqueue)
	{
		Event.FireTime += (LastEnqueue - Now) + 0.005;
	}
	EventQueue.SetLastEnqueue(Now);

	for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
	{
		Sink->OnQueued(Now, Event);
	}
	EventQueue.Add(MoveTemp(Event));
}

void FElysiumEntityWorld::FireOutput(FElysiumEntity& Source, FName OutputName, const FElysiumEntityHandle& Activator,
	const FElysiumVariant& ValueOverride)
{
	if (!Source.Def)
	{
		return;
	}
	const double Now = NowSeconds();
	// Retail PREPENDS each parsed action to the output object's linked list and then fires that list
	// head to tail, so repeated rows for one output resolve in reverse lump/export order
	// (`docs/vtmb/entity_io.md` → "Output-list and queue order"). The def keeps authoring order, so
	// the walk runs backwards; OutputTimesRemaining is indexed by the def row and stays aligned.
	for (int32 i = Source.Def->Outputs.Num() - 1; i >= 0; --i)
	{
		const FElysiumOutputDef& O = Source.Def->Outputs[i];
		if (FName(*O.Name) != OutputName)   // FName compare folds case
		{
			continue;
		}

		// The wire this row IS. Built before the `times` gate so an exhausted row is still counted
		// against its own identity — a wire that stops firing because it is spent is a different
		// finding from one that never fired, and only the tally can tell them apart.
		FElysiumWireRef Wire;
		Wire.SourceIndex = Source.Handle.Index;
		Wire.Output = FName(*O.Name);
		Wire.Row = i;

		// `times` countdown lives on the entity (the def is immutable); 0 = spent, -1 = unlimited.
		int32& Remaining = Source.OutputTimesRemaining[i];
		if (Remaining == 0)
		{
			// No sink fires here and no event exists, so the tally is the only witness that the row
			// was reached at all.
			if (FElysiumWireTally* Row = WireRowFor(Wire))
			{
				++Row->TimesExhausted;
			}
			continue;
		}
		if (Remaining > 0)
		{
			--Remaining;
		}

		if (FElysiumWireTally* Row = WireRowFor(Wire))
		{
			++Row->Fired;
		}

		for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
		{
			Sink->OnOutputFired(Now, Source, O);
		}

		FElysiumIOEvent Ev;
		Ev.Wire = Wire;
		Ev.FireTime = Now + O.Delay;
		Ev.Target = O.Target;
		Ev.Input = FName(*O.Input);
		// A Source COutput<T> fires with its runtime value only where the map author left the param
		// blank; a specified param always wins. Void override => keep the (possibly empty) map param.
		Ev.Param = (!O.Param.IsEmpty() || ValueOverride.IsVoid())
			? FElysiumVariant::String(O.Param) : ValueOverride;
		Ev.PythonSrc = O.Python;
		Ev.Activator = Activator;
		Ev.Caller = Source.Handle;
		AddEvent(MoveTemp(Ev));
	}
}

void FElysiumEntityWorld::EnqueueInput(const FString& Target, FName Input, const FElysiumVariant& Param,
	double Delay, const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller)
{
	// Hand-made injection through chokepoint 2: build the event and hand it to AddEvent, exactly as
	// FireOutput does for a game output. No Def row, so nothing counts down; the target string is
	// resolved (with !self/!activator) at dispatch, same as any queued delivery.
	FElysiumIOEvent Ev;
	Ev.FireTime = NowSeconds() + FMath::Max(0.0, Delay);
	Ev.Target = Target;
	Ev.Input = Input;
	Ev.Param = Param;
	Ev.Activator = Activator;
	Ev.Caller = Caller;
	AddEvent(MoveTemp(Ev));
}

void FElysiumEntityWorld::EnqueuePython(const FString& Source, double Delay,
	const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller)
{
	// ScheduleTask(delay, "<source>") (P5 5.4): a python-only deferred event — no I/O target, just a
	// field-6 source string that DeliverEvent hands to the script host at fire time. Same chokepoint
	// (2) and same queue as a delayed output, so it single-steps and serializes like everything else.
	FElysiumIOEvent Ev;
	Ev.FireTime = NowSeconds() + FMath::Max(0.0, Delay);
	Ev.PythonSrc = Source;
	Ev.Activator = Activator;
	Ev.Caller = Caller;
	AddEvent(MoveTemp(Ev));
}

FElysiumVariant FElysiumEntityWorld::EvalCondition(const FString& Source,
	const FElysiumEntityHandle& Self, const FElysiumEntityHandle& Activator)
{
	// logic_pythoncheck's Test (and, later, dlg conditions): evaluate the expression through the
	// installed script host so it obeys the same live/off switch as field-6 and lands in the eval
	// log. Empty source or no host -> Void (error-to-false -> the caller reads OnFalse).
	if (Source.IsEmpty() || !GameState)
	{
		return FElysiumVariant::Void();
	}
	FElysiumScriptContext Ctx;
	Ctx.Self = Self;
	Ctx.Activator = Activator;
	Ctx.World = this;
	return GameState->ScriptHost().Eval(Source, Ctx);
}

void FElysiumEntityWorld::AcceptInput(const FString& Target, FName Input, const FElysiumVariant& Param,
	const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller)
{
	// A hand-made dispatch belongs to no authored row, so it carries no wire and lands in no tally.
	AcceptInputFromWire(Target, Input, Param, Activator, Caller, FElysiumWireRef());
}

void FElysiumEntityWorld::AcceptInputFromWire(const FString& Target, FName Input,
	const FElysiumVariant& Param, const FElysiumEntityHandle& Activator,
	const FElysiumEntityHandle& Caller, const FElysiumWireRef& Wire)
{
	// Chokepoint 1 (R5): the sole input path. A transient event carries the dispatch context to
	// the sinks whether the caller is the queue (DeliverEvent) or a hand-fired console verb.
	if (!IsTriggerResolutionEnabled())
	{
		return;
	}
	const double Now = NowSeconds();
	FElysiumIOEvent Ev;
	Ev.FireTime = Now;
	Ev.Target = Target;
	Ev.Input = Input;
	Ev.Param = Param;
	Ev.Activator = Activator;
	Ev.Caller = Caller;
	Ev.Wire = Wire;

	TArray<FElysiumEntity*> Targets;
	ResolveTargets(Ev, Targets);
	if (Targets.Num() == 0)
	{
		++UnknownTargetCount;
		if (FElysiumWireTally* Row = WireRowFor(Wire))
		{
			++Row->UnknownTarget;
		}
		const FString Key = FString::Printf(TEXT("%s.%s"), *Target, *Input.ToString());
		// A missing receiver is an authored/runtime-state outcome, not an unimplemented surface:
		// retail data contains stale wires, and a valid target can also have been killed before a
		// later unlimited output fires. Keep the drop visible in the I/O sink and per-wire tally,
		// but do not put it on the implementation work list.
		if (!UnknownLogged.Contains(Key))
		{
			UnknownLogged.Add(Key);
			for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
			{
				Sink->OnUnknownTarget(Now, Ev);
			}
		}
		return;
	}

	for (FElysiumEntity* T : Targets)
	{
		DeliverInputTo(*T, Ev, Now);
	}
}

void FElysiumEntityWorld::AcceptInput(const FElysiumEntityHandle& Target, FName Input,
	const FElysiumVariant& Param, const FElysiumEntityHandle& Activator,
	const FElysiumEntityHandle& Caller)
{
	if (!IsTriggerResolutionEnabled())
	{
		return;
	}
	const double Now = NowSeconds();
	FElysiumIOEvent Ev;
	Ev.FireTime = Now;
	Ev.Target = DescribeHandle(Target);
	Ev.Input = Input;
	Ev.Param = Param;
	Ev.Activator = Activator;
	Ev.Caller = Caller;
	if (FElysiumEntity* Resolved = Resolve(Target))
	{
		DeliverInputTo(*Resolved, Ev, Now);
		return;
	}
	++UnknownTargetCount;
	// The same ordinary missing-receiver accounting as the by-name overload. A stale handle is
	// still counted and reported to sinks on every attempt, but is not an implementation stub.
	for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
	{
		Sink->OnUnknownTarget(Now, Ev);
	}
}

void FElysiumEntityWorld::DeliverInputTo(
	FElysiumEntity& Target, const FElysiumIOEvent& Event, double Now)
{
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	const FElysiumInputThunk Thunk = Target.Class
		? Reg.FindInput(*Target.Class, Event.Input) : nullptr;
	if (!Thunk)
	{
		++UnknownInputCount;
		if (FElysiumWireTally* Row = WireRowFor(Event.Wire))
		{
			++Row->UnknownInput;
		}
		const FString Key = FString::Printf(
			TEXT("%s.%s"), *Target.Def->Classname, *Event.Input.ToString());
		// The generic stub surface: an input the R2 walk cannot resolve is unimplemented whether
		// the classname has a leaf that lacks this one input or no leaf at all (an unregistered
		// classname resolves to the inert base record, whose chain owns only the base inputs). Both
		// report here, so every unwired input in every map is on the work list without each
		// classname having to be enumerated first.
		ElysiumStub::Fired(TEXT("input"), Key, Target.DebugString(),
			FString::Printf(TEXT("param=%s activator=%s caller=%s"),
				*Event.Param.Describe(), *Event.Activator.ToString(), *Event.Caller.ToString()),
			Target.IsRecordOnly()
				? FString::Printf(TEXT("'%s' has no runtime class — inert record on %s"),
					*Target.Def->Classname, *ElysiumBaseClassName().ToString())
				: FString::Printf(TEXT("'%s' is registered but wires no '%s' input"),
					*Target.Def->Classname, *Event.Input.ToString()));
		if (!UnknownLogged.Contains(Key))
		{
			UnknownLogged.Add(Key);
			for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
			{
				Sink->OnUnknownInput(Now, Target, Event);
			}
		}
		return;
	}

	FElysiumInputArgs Args;
	Args.Param = Event.Param;
	Args.Activator = Event.Activator;
	Args.Caller = Event.Caller;
	Args.Input = Event.Input;
	Thunk(Target, Args);
	// Counted per TARGET, not per event: one fire of a `patrol_cop_*` wire is one Fired and as many
	// Delivered as the pattern matched. "The output reached a receiver that accepted it" is the fact
	// acceptance needs, and it is a per-receiver fact.
	if (FElysiumWireTally* Row = WireRowFor(Event.Wire))
	{
		++Row->Delivered;
	}
	for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
	{
		Sink->OnDelivered(Now, Target, Event);
	}
}

void FElysiumEntityWorld::DeliverEvent(const FElysiumIOEvent& Event, double Now)
{
	// The I/O half goes through AcceptInput (the one input path); the field-6 Python half goes
	// to the script host. An output can carry both (105 in the game do).
	if (!Event.Target.IsEmpty())
	{
		AcceptInputFromWire(Event.Target, Event.Input, Event.Param, Event.Activator, Event.Caller,
			Event.Wire);
	}

	if (!Event.PythonSrc.IsEmpty() && GameState)
	{
		FElysiumScriptContext Ctx;
		Ctx.Self = Event.Caller;
		Ctx.Activator = Event.Activator;
		Ctx.World = this;
		// Counted at the hand-off, not at the result: a payload that raised still ran, while a row
		// whose Python never reached a host (no game state) reads as authored-but-not-forwarded,
		// which is the distinction between "the script failed" and "the script never happened".
		if (FElysiumWireTally* Row = WireRowFor(Event.Wire))
		{
			++Row->PythonForwarded;
		}
		const FElysiumVariant Result = GameState->ScriptHost().Eval(Event.PythonSrc, Ctx);
		for (const TUniquePtr<IElysiumIOSink>& Sink : Sinks)
		{
			Sink->OnPython(Now, Event, Result);
		}
	}
}

void FElysiumEntityWorld::ResolveTargets(const FElysiumIOEvent& Event, TArray<FElysiumEntity*>& Out)
{
	const FString& T = Event.Target;
	// Runtime references resolve at dispatch time (R3), against the event's provenance.
	if (T.Equals(ElysiumEntityWorldShared::GSelfTarget, ESearchCase::IgnoreCase) || T.Equals(GCallerTarget, ESearchCase::IgnoreCase))
	{
		if (FElysiumEntity* E = Resolve(Event.Caller))
		{
			Out.Add(E);
		}
		return;
	}
	if (T.Equals(GActivatorTarget, ESearchCase::IgnoreCase))
	{
		if (FElysiumEntity* E = Resolve(Event.Activator))
		{
			Out.Add(E);
		}
		return;
	}
	if (T.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
	{
		if (FElysiumEntity* E = FindPlayerController())
		{
			Out.Add(E);
		}
		return;
	}
	// `!player` and `!pvsplayer` both name the one player. `!player` is also the player entity's
	// literal targetname, so the index would answer it — it is named here so it does not fall into
	// the unrecognized-`!` case below.
	if (T.Equals(ElysiumPlayerTargetName(), ESearchCase::IgnoreCase)
		|| T.Equals(TEXT("!pvsplayer"), ESearchCase::IgnoreCase))
	{
		if (FElysiumEntity* E = FindPlayer())
		{
			Out.Add(E);
		}
		return;
	}
	// Every other leading-`!` name is retail's separate single-result path (RE29) with no case for
	// it, so it resolves to nothing rather than fanning out over the name index. AcceptInput counts
	// the empty result as an unknown target — the same non-fatal posture as a dead wire (K2).
	if (T.StartsWith(TEXT("!"), ESearchCase::CaseSensitive))
	{
		return;
	}

	// Targetnames are non-unique — fan out over every live (non-dead) match. A wire may name a
	// trailing-`*` prefix (RE29); 68 shipped outputs do, `patrol_cop_*` alone 51 times.
	ForEachMatch(T, [&Out](FElysiumEntity& E) { Out.Add(&E); return true; });
}

// --- Per-wire accounting ------------------------------------------------------------------

FElysiumWireTally* FElysiumEntityWorld::WireRowFor(const FElysiumWireRef& Wire)
{
	// An unset wire is the normal case for a console injection, a ScheduleTask and every direct
	// AcceptInput; it stores nothing rather than accumulating a nameless bucket, so the map's size
	// is bounded by the authored surface.
	return Wire.IsSet() ? &WireTally.FindOrAdd(Wire) : nullptr;
}

FElysiumWireTally FElysiumEntityWorld::WireTallyFor(const FElysiumWireRef& Wire) const
{
	const FElysiumWireTally* Row = WireTally.Find(Wire);
	return Row ? *Row : FElysiumWireTally();
}

void FElysiumEntityWorld::BuildWireReport(TArray<FElysiumWireReportRow>& Out) const
{
	Out.Reset();

	// Walk the live entities rather than the def array: it covers the authored defs (whose entity
	// index IS their def index) and the runtime-spawned entities past them, and it reads each row
	// off the def the entity actually holds. A killed entity keeps its slot, so its wires stay in
	// the report with whatever they managed before they died.
	for (int32 Index = 0; Index < EntityList.Num(); ++Index)
	{
		const FElysiumEntity* Ent = EntityList[Index].Get();
		if (!Ent || !Ent->Def)
		{
			continue;
		}
		const FElysiumEntityDef& Def = *Ent->Def;

		// The per-output ordinal an offline enumeration counts in ("the second OnTrigger row"),
		// keyed by the case-folded output name so it agrees with how the rows are matched.
		TMap<FName, int32> SeenPerOutput;

		for (int32 RowIndex = 0; RowIndex < Def.Outputs.Num(); ++RowIndex)
		{
			const FElysiumOutputDef& O = Def.Outputs[RowIndex];

			FElysiumWireReportRow Report;
			Report.Wire.SourceIndex = Index;
			Report.Wire.Output = FName(*O.Name);
			Report.Wire.Row = RowIndex;
			Report.OutputRow = SeenPerOutput.FindOrAdd(Report.Wire.Output)++;
			Report.SourceName = Def.TargetName;
			Report.SourceClass = Def.Classname;
			Report.Target = O.Target;
			Report.Input = O.Input;
			Report.Param = O.Param;
			Report.Python = O.Python;
			Report.Delay = O.Delay;
			Report.AuthoredTimes = O.Times;
			Report.bRuntimeSource = Index >= Defs.Defs.Num();
			Report.Tally = WireTallyFor(Report.Wire);
			Out.Add(MoveTemp(Report));
		}
	}
}

// --- Resolution -------------------------------------------------------------------------

FElysiumEntity* FElysiumEntityWorld::Resolve(const FElysiumEntityHandle& Handle)
{
	if (!Handle.IsSet() || Handle.Epoch != Epoch || !EntityList.IsValidIndex(Handle.Index))
	{
		return nullptr;
	}
	FElysiumEntity* E = EntityList[Handle.Index].Get();
	return (E && !E->IsDead()) ? E : nullptr;
}

const FElysiumEntity* FElysiumEntityWorld::Resolve(const FElysiumEntityHandle& Handle) const
{
	return const_cast<FElysiumEntityWorld*>(this)->Resolve(Handle);
}

FElysiumEntity* FElysiumEntityWorld::FindByName(const FString& Name)
{
	if (Name.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
	{
		if (FElysiumEntity* Controller = FindPlayerController())
		{
			return Controller;
		}
		// During snapshot reconstruction the relationship handle is rebound after the state walk;
		// fall through to the entity's literal targetname so scene restore can bind in that window.
	}
	// FindEntityByName with a null start entity: the first live match in entity-list order, under the
	// same matching rule everything else uses (RE29) — so a trailing-`*` name resolves here too.
	FElysiumEntity* Found = nullptr;
	ForEachMatch(Name, [&Found](FElysiumEntity& E) { Found = &E; return false; });
	return Found;
}

bool FElysiumEntityWorld::IsNpcMakerSceneBlocked() const
{
	for (const TUniquePtr<FElysiumEntity>& Ent : EntityList)
	{
		if (Ent.IsValid() && !Ent->IsDead() && Ent->BlocksNpcMakerSpawns())
		{
			return true;
		}
	}
	return false;
}

FElysiumEntity* FElysiumEntityWorld::FindLandmark(const FString& Name)
{
	// info_landmark lookup for the P4.6 landmark transition (the source-map anchor a
	// trigger_changelevel measures the player against, and the dest-map anchor the next load places
	// against). Same name index as FindByName, but filtered to the info_landmark classname so a
	// coincidental targetname reuse can't be mistaken for the landmark.
	static const FString LandmarkClass(TEXT("info_landmark"));
	const FName N(*Name);
	for (auto It = NameIndex.CreateConstKeyIterator(N); It; ++It)
	{
		if (EntityList.IsValidIndex(It.Value()))
		{
			FElysiumEntity* E = EntityList[It.Value()].Get();
			if (E && !E->IsDead() && E->Def && E->Def->Classname == LandmarkClass)
			{
				return E;
			}
		}
	}
	return nullptr;
}

bool FElysiumEntityWorld::NameMatches(const FString& TargetName, const FString& Pattern)
{
	// RE29 (vampire.dll FUN_100f7770). An empty pattern matches nothing — the image tests
	// `*szName == '\0'` and returns null before it walks anything — and a nameless entity is never a
	// candidate, because the walk skips a null targetname pointer.
	const int32 Len = Pattern.Len();
	if (Len == 0 || TargetName.IsEmpty())
	{
		return false;
	}
	// Only the FINAL character is special. The image indexes `szName[len-1]` and nothing else, so a
	// `*` in any other position is a literal — there is no glob here, and no `?`.
	if (Pattern[Len - 1] == TEXT('*'))
	{
		// _strnicmp(targetname, pattern, len-1): case-insensitive over everything before the star. A
		// bare "*" is len-1 == 0, which _strnicmp answers 0 (equal) for — so it matches every named
		// entity, exactly as the image does.
		const int32 Prefix = Len - 1;
		return TargetName.Len() >= Prefix
			&& FCString::Strnicmp(*TargetName, *Pattern, Prefix) == 0;
	}
	// _stricmp — the ordinary case-insensitive exact match.
	return TargetName.Equals(Pattern, ESearchCase::IgnoreCase);
}

void FElysiumEntityWorld::ForEachMatch(const FString& Pattern, TFunctionRef<bool(FElysiumEntity&)> Fn)
{
	if (Pattern.IsEmpty())
	{
		return;
	}

	// The common case is an exact name, and the name index already folds case (FName), so it answers
	// the same question the image's _stricmp does — take the hash.
	if (Pattern[Pattern.Len() - 1] != TEXT('*'))
	{
		const FName Name(*Pattern);
		// A TMultiMap hands its values back most-recently-added first, which would visit duplicate
		// targetnames in reverse and put a runtime-spawned entity (a maker's child) ahead of every
		// map entity. Retail delivers in global entity-list order, so gather and sort by index.
		TArray<int32> Matches;
		for (auto It = NameIndex.CreateConstKeyIterator(Name); It; ++It)
		{
			Matches.Add(It.Value());
		}
		Matches.Sort();
		for (int32 Index : Matches)
		{
			if (EntityList.IsValidIndex(Index))
			{
				if (FElysiumEntity* E = EntityList[Index].Get())
				{
					if (!E->IsDead() && !Fn(*E))
					{
						return;
					}
				}
			}
		}
		return;
	}

	// A prefix pattern cannot use the hash, so walk the list — which is also the image's own order
	// (entity-list order), so "the first match" means the same thing on both sides.
	for (const TUniquePtr<FElysiumEntity>& Owned : EntityList)
	{
		FElysiumEntity* E = Owned.Get();
		if (E && !E->IsDead() && NameMatches(E->TargetName, Pattern) && !Fn(*E))
		{
			return;
		}
	}
}

void FElysiumEntityWorld::ForEachNamed(const FString& Pattern, TFunctionRef<void(FElysiumEntity&)> Fn)
{
	ForEachMatch(Pattern, [&Fn](FElysiumEntity& E) { Fn(E); return true; });
}

// --- Formatting -------------------------------------------------------------------------

FString FElysiumEntityWorld::DescribeHandle(const FElysiumEntityHandle& Handle) const
{
	if (!Handle.IsSet())
	{
		return TEXT("#<null>");
	}
	if (const FElysiumEntity* E = Resolve(Handle))
	{
		return E->DebugString();
	}
	return FString::Printf(TEXT("#%d <stale>"), Handle.Index);
}

FString FElysiumEntityWorld::FormatEventLine(double Now, const FElysiumIOEvent& Event,
	const FString& TargetLabel, const TCHAR* Note) const
{
	return FString::Printf(TEXT("(%8.3f) %s -> %s.%s(%s)%s%s"),
		Now, *DescribeHandle(Event.Caller), *TargetLabel,
		*Event.Input.ToString(), *Event.Param.ToString(),
		Note ? TEXT(" ") : TEXT(""), Note ? Note : TEXT(""));
}

// --- Teardown ---------------------------------------------------------------------------

void FElysiumEntityWorld::Teardown()
{
	// Dialogue cursors and scoped camera handles never enter a map snapshot. Release silently before
	// the teardown freeze so travel cannot serialize a half-open scripted session.
	if (DialogueSession)
	{
		EndDialogSession(/*bSilent*/ true);
	}

	// A captured interaction belongs to this map epoch. Give the leaf its cancellation edge while
	// its handle and any presentation/session owner are still valid.
	EndActiveUse(EElysiumUseEndReason::WorldTeardown);
	TransitionUseFocus(nullptr);
	PendingUseEdges.Reset();
	InteractionPrompt = FInteractionPrompt();
	LastUseOutcome = EElysiumUseOutcome::NoTarget;
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		Bodily->ClearUseAnchors();
		Bodily->ClearTouchAnchors();
	}

	bActive = false;
	ActiveTouches.Empty();

	// 11.4 — the player's live state goes back into the session record before the entity holding it
	// dies. This is the only dehydrate point, and it covers every way a map epoch ends: a travel, a
	// reload, quit-to-menu, and the world being rebuilt on a surviving actor.
	if (GameState)
	{
		if (const FElysiumPlayer* PlayerEnt = FindPlayer())
		{
			PlayerEnt->Dehydrate(GameState->PlayerRecord());

			// 11.9 — and the map itself is frozen into the session, beside the record, by the same
			// call a save uses (`docs/architecture/save-architecture.md` §5). Gated on there having been a player: a
			// menu backdrop and a headless logic world run the substrate in full but are not part of
			// anyone's run, so they must not join the visited-map set.
			if (!bDetached && !Defs.MapName.IsEmpty())
			{
				FElysiumMapSnapshot Snapshot;
				Freeze(Snapshot);
				GameState->StoreMapSnapshot(MoveTemp(Snapshot));
			}
		}
	}
	Player = FElysiumEntityHandle::Invalid();

	// Scripted cameras do not outlive the map that pushed them.
	ClearTrackCamera(/*BlendOutSeconds*/ 0.0f);
	ClearScriptedCamera();

	// Epoch 0 matches no minted handle, so every outstanding handle goes stale at once (R3).
	Epoch = 0;
	EventQueue.Reset();
	NameIndex.Empty();
	ClassIndex.Empty();

	// Bodies are the world's embodiments — destroy them with the world. (The map actor also frees
	// them when it is destroyed; this handles a world rebuild on a surviving actor, e.g. reload.)
	ElysiumWorldDestroyWeakComponents(Bodies);

	// NPC skeletal bodies (B3): components of the map actor, destroyed here for the same reason as
	// Bodies — a world rebuild on a surviving actor (reload) must not leak them.
	ElysiumWorldDestroyWeakComponents(NpcBodies);

	// Dynamic-prop bodies (8.3): same reason as NpcBodies — components of the map actor, destroyed
	// here so a world rebuild on a surviving actor (reload) does not leak them.
	ElysiumWorldDestroyWeakComponents(PropBodies);

	// phys_hinge constraints (8.4): destroyed with the map, like the bodies they wired.
	ElysiumWorldDestroyWeakComponents(Constraints);

	EntityList.Empty();
	Baseline.Empty();
	RuntimeDefs.Empty();
	// The tally is keyed by entity index, which means something only inside one map epoch.
	WireTally.Empty();
	Ring = nullptr;
	Sinks.Empty();
}
