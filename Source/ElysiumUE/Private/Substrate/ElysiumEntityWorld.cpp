#include "ElysiumEntityWorld.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumCameraService.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumDlg.h"
#include "ElysiumEditorLabels.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumLineService.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumScriptHost.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumLipTrack.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSignData.h"
#include "ElysiumUseIcons.h"
#include "ElysiumViewState.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"
#include "Substrate/ElysiumSkillClasses.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Algo/Rotate.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumWorld, Log, All);

// One runtime-instanced choreo scene for the current NPC line. Retail's CInstancedSceneEntity is
// not a map entity: the dialogue owner constructs it from the line's sidecar VCD, advances it on
// the conversation clock and destroys it on line replacement/close. Audio, subtitles and facial
// composition already have dialogue-owned presentation paths, so this callback consumes only the
// authored body events and leaves the other event kinds on those existing owners.
struct FElysiumDialogueLineScene final : IElysiumChoreoCallback
{
	void Begin(FElysiumEntityWorld& InWorld, const FElysiumEntityHandle& InSpeaker,
		const FString& DlgSourcePath, int32 LineId, double Now)
	{
		Stop();
		World = &InWorld;
		Speaker = InSpeaker;
		SourceRel = FPaths::SetExtension(
			FElysiumLineService::DialogueLineSource(DlgSourcePath, LineId), TEXT("vcd"));
		Scene = ElysiumScene::Load(SourceRel);
		if (!Scene.IsValid())
		{
			// A bodiless/headless conversation has no body performance to lose. On a live body this is a
			// missing authored input, and it must not collapse silently to a plausible idle.
			const FElysiumEntity* Entity = World->Resolve(Speaker);
			if (Entity != nullptr && Entity->GetSkeletalBody() != nullptr)
			{
				UE_LOG(LogElysiumWorld, Warning,
					TEXT("dialogue %s line %d body scene '%s' did not resolve"),
					*Speaker.ToString(), LineId, *SourceRel);
			}
			Clear();
			return;
		}

		SpeechStart = 0.f;
		bHasSpeechStart = false;
		for (const FElysiumSceneEvent& Event : Scene->Events)
		{
			if (Event.bActive && Event.Type == EElysiumChoreoEvent::Speak)
			{
				SpeechStart = Event.StartTime;
				bHasSpeechStart = true;
				break;
			}
		}

		Player.Begin(Scene, /*InLatency=*/0.f, /*InMaxDuration=*/0.f);
		LastThink = Now;
		SceneTime = 0.f;
		Player.AdvanceTo(SceneTime, *this); // time-zero body events start with the voice submission
	}

	void Advance(double Now)
	{
		if (!Player.IsBound())
		{
			return;
		}
		// CInstancedSceneEntity advances by curtime - lastthink and permanently discards anything over
		// 100 ms. This is deliberately different from a map scene's absolute wall-time clock.
		const float Delta = static_cast<float>(FMath::Clamp(Now - LastThink, 0.0, 0.1));
		LastThink = Now;
		SceneTime += Delta;
		Player.AdvanceTo(SceneTime, *this);
		if (Player.IsFinished())
		{
			Stop();
		}
	}

	void Stop()
	{
		if (Player.IsBound())
		{
			Player.StopActiveEvents(*this);
		}
		Clear();
	}

	bool HasActiveBodyClip() const { return !ActiveClipEvents.IsEmpty(); }

	bool GetSpeechSeconds(float& OutSeconds) const
	{
		if (!Player.IsBound() || !bHasSpeechStart)
		{
			return false;
		}
		OutSeconds = SceneTime - SpeechStart;
		return true;
	}

	virtual void StartEvent(const FElysiumSceneData&, const FElysiumSceneEvent& Event,
		float InSceneTime) override
	{
		if (Event.Type != EElysiumChoreoEvent::Sequence
			&& Event.Type != EElysiumChoreoEvent::Gesture)
		{
			return;
		}
		if (Event.Param.IsEmpty())
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s body scene '%s' has an empty %s clip"),
				*Speaker.ToString(), *SourceRel, ElysiumScene::EventTypeName(Event.Type));
			return;
		}
		FElysiumEntity* Entity = ResolveSpeaker();
		if (Entity == nullptr)
		{
			LogMissingSpeaker();
			return;
		}
		if (!Entity->PlayAnimClip(Event.Param, /*bLoop=*/false))
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s body scene '%s' clip '%s' did not resolve"),
				*Speaker.ToString(), *SourceRel, *Event.Param);
			return;
		}
		const int32 Index = EventIndex(Event);
		if (Index != INDEX_NONE)
		{
			ActiveClipEvents.Add(Index);
		}
		SeekClip(Event, InSceneTime);
	}

	virtual void ProcessEvent(const FElysiumSceneData&, const FElysiumSceneEvent& Event,
		float InSceneTime) override
	{
		if (Event.Type == EElysiumChoreoEvent::Sequence
			|| Event.Type == EElysiumChoreoEvent::Gesture)
		{
			SeekClip(Event, InSceneTime);
		}
	}

	virtual void EndEvent(const FElysiumSceneData&, const FElysiumSceneEvent& Event, float) override
	{
		if (Event.Type != EElysiumChoreoEvent::Sequence
			&& Event.Type != EElysiumChoreoEvent::Gesture)
		{
			return;
		}
		if (!ActiveClipEvents.Remove(EventIndex(Event)))
		{
			return;
		}
		if (ActiveClipEvents.IsEmpty())
		{
			if (FElysiumEntity* Entity = ResolveSpeaker())
			{
				Entity->StopCinematicClip();
			}
		}
	}

private:
	void Clear()
	{
		Player = FElysiumScenePlayer();
		Scene.Reset();
		ActiveClipEvents.Reset();
		World = nullptr;
		Speaker = FElysiumEntityHandle::Invalid();
		SourceRel.Reset();
		LastThink = 0.0;
		SceneTime = 0.f;
		SpeechStart = 0.f;
		bHasSpeechStart = false;
		bLoggedMissingSpeaker = false;
		bLoggedSeekFailure = false;
	}

	FElysiumEntity* ResolveSpeaker() const
	{
		return World != nullptr ? World->Resolve(Speaker) : nullptr;
	}

	int32 EventIndex(const FElysiumSceneEvent& Event) const
	{
		if (!Scene.IsValid() || Scene->Events.IsEmpty())
		{
			return INDEX_NONE;
		}
		const FElysiumSceneEvent* First = Scene->Events.GetData();
		const ptrdiff_t Delta = &Event - First;
		return Delta >= 0 && Delta < Scene->Events.Num() ? static_cast<int32>(Delta) : INDEX_NONE;
	}

	void SeekClip(const FElysiumSceneEvent& Event, float InSceneTime)
	{
		if (!ActiveClipEvents.Contains(EventIndex(Event)))
		{
			return;
		}
		FElysiumEntity* Entity = ResolveSpeaker();
		if (Entity == nullptr)
		{
			LogMissingSpeaker();
			return;
		}
		if (!Entity->SeekCinematicClip(FMath::Max(0.f, InSceneTime - Event.StartTime))
			&& !bLoggedSeekFailure)
		{
			bLoggedSeekFailure = true;
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s body scene '%s' could not seek clip '%s'"),
				*Speaker.ToString(), *SourceRel, *Event.Param);
		}
	}

	void LogMissingSpeaker()
	{
		if (!bLoggedMissingSpeaker)
		{
			bLoggedMissingSpeaker = true;
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s body scene '%s' lost its speaker"),
				*Speaker.ToString(), *SourceRel);
		}
	}

	FElysiumEntityWorld* World = nullptr;
	FElysiumEntityHandle Speaker;
	TSharedPtr<const FElysiumSceneData> Scene;
	FElysiumScenePlayer Player;
	TSet<int32> ActiveClipEvents;
	FString SourceRel;
	double LastThink = 0.0;
	float SceneTime = 0.f;
	float SpeechStart = 0.f;
	bool bHasSpeechStart = false;
	bool bLoggedMissingSpeaker = false;
	bool bLoggedSeekFailure = false;
};

enum class EElysiumDialogueDirectorSource : uint8
{
	None,
	SourceShot,
	AuthoredProfile,
	PlayerView,
};

// The one open conversation record. Everything with conversation lifetime lives here: branch
// cursor, opener provenance, one camera handle and its selected-director state. No member is
// serialized because CanSave refuses the whole session category.
struct FElysiumDialogueSession
{
	FElysiumEntityHandle Owner;
	FElysiumEntityHandle Listener;
	TSharedPtr<FElysiumDlgConversation> Conversation;
	EElysiumDialogOpenerKind Opener = EElysiumDialogOpenerKind::Remote;
	int32 RawFlags = 0;
	int32 DecodedFlags = 0;
	FString DefaultCamera;
	FString NormalizedCamera;
	FElysiumCameraHandle CameraHandle;
	FElysiumCameraRequest CameraRequest;
	EElysiumDialogueDirectorSource DirectorSource = EElysiumDialogueDirectorSource::None;
	EElysiumDialogueShotProfile SelectedProfile = EElysiumDialogueShotProfile::Fallback;
	FString CandidateRejections;
	FString FallbackReason;
	int32 CurrentLineId = INDEX_NONE;
	uint32 TurnRevision = 0;
	FElysiumVoiceHandle CurrentVoice;
	bool bAutomaticFallback = false;
	double SelectedAt = 0.0;
	float MinimumHoldSeconds = 0.0f;
	float ScreenSide = 1.0f;
	FElysiumBodyOwnerToken BodyOwner;
	FElysiumDialogueLineScene LineScene;
};

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

	const TCHAR* const GSelfTarget = TEXT("!self");
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
}

static FAutoConsoleCommand GElysiumTrigger(
	TEXT("elysium.trigger"),
	TEXT("elysium.trigger <on|off> -- globally resume or suspend map trigger resolution, entity thinks, and deferred I/O/Python."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(LogElysiumWorld, Display, TEXT("usage: elysium.trigger <on|off> (currently %s)"),
				FElysiumEntityWorld::IsTriggerResolutionEnabled() ? TEXT("on") : TEXT("off"));
			return;
		}

		if (Args[0].Equals(TEXT("on"), ESearchCase::IgnoreCase))
		{
			FElysiumEntityWorld::SetTriggerResolutionEnabled(true);
		}
		else if (Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			FElysiumEntityWorld::SetTriggerResolutionEnabled(false);
		}
		else
		{
			UE_LOG(LogElysiumWorld, Warning, TEXT("elysium.trigger: expected 'on' or 'off', got '%s'"), *Args[0]);
		}
	}));

FElysiumEntityWorld::FElysiumEntityWorld(AActor* InOwner, UElysiumGameStateSubsystem* InGameState,
	const FElysiumWorldServices& InServices)
	: Owner(InOwner)
	, GameState(InGameState)
	, WorldServices(InServices)
	, Epoch(GElysiumNextWorldEpoch++)
{
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
	if (IElysiumEmbodiment* E = Embodiment())
	{
		E->SetPlayerVisualSuppressed(true);
	}
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
	if (IElysiumEmbodiment* E = Embodiment())
	{
		E->SetPlayerVisualSuppressed(false);
	}
	return true;
}

// --- Persistence (11.9) ------------------------------------------------------------------
// The map snapshot. Both halves run against the *same* world the game runs against, which is what
// `docs/architecture/save-architecture.md` §5 means by "a snapshot is produced by exactly the same code path a save
// uses": a travel boundary and a Save Game call reach Freeze identically.

FElysiumEntityState FElysiumEntityWorld::CaptureState(const FElysiumEntity& E) const
{
	FElysiumEntityState S;
	S.Index = E.Handle.Index;
	S.ClassName = E.Def ? FName(*E.Def->Classname) : NAME_None;
	S.TargetName = E.TargetName;
	S.bRuntime = E.Handle.Index >= Defs.Defs.Num();
	S.bDead = E.bDead;
	S.bHidden = E.bHidden;
	S.bSpawnCalled = E.bSpawnCalled;
	S.bActivateCalled = E.bActivateCalled;
	S.NextThink = E.NextThink;
	S.SavedNextThink = E.GetSavedNextThink();
	S.OutputTimesRemaining = E.OutputTimesRemaining;
	// The live origin is not a registered field and cannot become one: the def's `origin` key is
	// still the raw Source-space string, and Construct applies every key that has a field, so a
	// registered `origin` would overwrite the converted placement at every spawn. It rides here
	// instead — `point_teleport` and `Entity.SetOrigin` move entities for real (11.4).
	S.Origin = E.Origin;

	// The R2 field walk, in the registry's sorted order so two captures of one state agree byte
	// for byte (§8).
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	if (E.Class)
	{
		for (const FName& FieldName : Reg.SaveFields(*E.Class))
		{
			if (const FElysiumFieldAccessor* Acc = Reg.FindField(*E.Class, FieldName))
			{
				if (Acc->Get)
				{
					S.Fields.Emplace(FieldName, Acc->Get(E));
				}
			}
		}
	}

	// The leaf's derived state (§4). A leaf that does not override Serialize writes nothing.
	{
		FMemoryWriter Writer(S.LeafState, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		const_cast<FElysiumEntity&>(E).Serialize(Ar);
	}
	return S;
}

void FElysiumEntityWorld::CaptureBaseline(int32 Index)
{
	// **The omission baseline is the post-Load state, not the post-Construct state.** The question
	// the rule asks is "would rebuilding this map produce this value anyway?", and a rebuild is
	// Construct *plus* Spawn plus PostSpawn — so the answer has to be taken after all three. Taking
	// it after Construct alone silently drops anything Spawn writes and gameplay later returns to
	// its constructed value: `logic_auto`'s first think is exactly that (Spawn arms it at 0, the
	// think disarms it to never, and never is also the constructed value), and omitting it would
	// re-run every map's ignition on load.
	if (!EntityList.IsValidIndex(Index) || !EntityList[Index])
	{
		return;
	}
	if (Baseline.Num() < EntityList.Num())
	{
		Baseline.SetNum(EntityList.Num());
	}
	Baseline[Index] = CaptureState(*EntityList[Index]);
	Baseline[Index].bCaptured = true;
}

void FElysiumEntityWorld::Freeze(FElysiumMapSnapshot& Out) const
{
	Out = FElysiumMapSnapshot();
	Out.MapName = Defs.MapName;
	Out.DefCount = Defs.Defs.Num();
	Out.FrozenAt = NowSeconds();

	for (const TUniquePtr<FElysiumEntity>& EntPtr : EntityList)
	{
		if (!EntPtr)
		{
			continue;
		}
		const FElysiumEntity& E = *EntPtr;

		// The player is the Player block's, not this map's — it travels, and its record already
		// carries everything the entity holds (11.4's hydrate/dehydrate pair).
		if (Player.IsSet() && E.Handle.Index == Player.Index)
		{
			continue;
		}
		// Anything carried out of the map is recorded absent rather than saved here (§5). Nothing
		// answers true until 9.8 makes items owned entities; the rule is the mechanism, not a stub.
		if (E.TravelsWithPlayer())
		{
			Out.AbsentEntities.Add(E.Handle.Index);
			continue;
		}

		FElysiumEntityState S = CaptureState(E);
		const int32 Index = E.Handle.Index;
		const FElysiumEntityState* Base =
			(Baseline.IsValidIndex(Index) && Baseline[Index].bCaptured) ? &Baseline[Index] : nullptr;

		// A runtime entity has no def on disk, so it is always written and its synthesized def rides
		// along — that is the whole of how it restores as itself.
		if (S.bRuntime && E.Def)
		{
			S.Def = *E.Def;
		}

		bool bDiffers = S.bRuntime || Base == nullptr;
		if (Base)
		{
			bDiffers = bDiffers
				|| S.TargetName != Base->TargetName
				|| S.bDead != Base->bDead
				|| S.bHidden != Base->bHidden
				|| S.bSpawnCalled != Base->bSpawnCalled
				|| S.bActivateCalled != Base->bActivateCalled
				|| S.NextThink != Base->NextThink
				|| S.SavedNextThink != Base->SavedNextThink
				|| S.OutputTimesRemaining != Base->OutputTimesRemaining
				|| !S.Origin.Equals(Base->Origin, 0.0f)
				|| S.LeafState != Base->LeafState;

			// Prune the field list to what actually moved. The baseline is sorted the same way, so
			// this is a merge, not a search.
			TArray<TPair<FName, FElysiumVariant>> Changed;
			int32 b = 0;
			for (const TPair<FName, FElysiumVariant>& F : S.Fields)
			{
				while (b < Base->Fields.Num() && Base->Fields[b].Key != F.Key
					&& Base->Fields[b].Key.LexicalLess(F.Key))
				{
					++b;
				}
				const bool bSame = (b < Base->Fields.Num() && Base->Fields[b].Key == F.Key
					&& Base->Fields[b].Value == F.Value);
				if (!bSame)
				{
					Changed.Add(F);
				}
			}
			if (Changed.Num() > 0)
			{
				bDiffers = true;
			}
			S.Fields = MoveTemp(Changed);

			if (S.LeafState == Base->LeafState)
			{
				S.LeafState.Reset();   // a leaf whose derived state has not moved costs 0 bytes
			}
		}

		if (bDiffers)
		{
			Out.Entities.Add(MoveTemp(S));
		}
	}

	Out.Queue = EventQueue.Pending();
	Out.QueueNextSerial = EventQueue.NextSerialValue();
	Out.QueueLastEnqueue = EventQueue.LastEnqueueValue();

	Out.Fade.bActive      = ScreenFade.bActive;
	Out.Fade.Color        = ScreenFade.Color;
	Out.Fade.MaxAlpha     = ScreenFade.MaxAlpha;
	Out.Fade.Duration     = ScreenFade.Duration;
	Out.Fade.HoldTime     = ScreenFade.HoldTime;
	Out.Fade.bFadeIn      = ScreenFade.bFadeIn;
	Out.Fade.bAutoReverse = ScreenFade.bAutoReverse;
	Out.Fade.StartTime    = ScreenFade.StartTime;
	Out.Weather = WeatherState;

	UE_LOG(LogElysiumWorld, Log,
		TEXT("froze '%s' at %.3f: %d/%d entity records, %d absent, %d queued"),
		*Out.MapName, Out.FrozenAt, Out.Entities.Num(), EntityList.Num(),
		Out.AbsentEntities.Num(), Out.Queue.Num());
}

int32 FElysiumEntityWorld::ApplySnapshot(const FElysiumMapSnapshot& Snapshot)
{
	if (!Snapshot.IsValid())
	{
		return 0;
	}
	bSnapshotApplied = true;
	SnapshotEntityIndices.Reset();
	if (Snapshot.DefCount != Defs.Defs.Num())
	{
		// The map's `.ents` changed under the save. Every record is matched by index and guarded by
		// classname below, so this is a loud warning rather than a refusal — a re-export that only
		// appended still restores the entities it did not move.
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("snapshot '%s' was frozen against %d defs, this build parsed %d — applying by index"),
			*Snapshot.MapName, Snapshot.DefCount, Defs.Defs.Num());
	}

	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

	// Pass 1 — re-create the runtime-spawned entities (npc_maker.Spawn, CreateEntityNoSpawn) from the
	// defs that ride along, in index order, so every one lands back on its saved index. They do land
	// there: the def array fills 0..DefCount-1, the player takes DefCount (SpawnPlayer runs
	// immediately after Load, on every load), and the rest were appended in exactly this order.
	// Creating and spawning them here, before anything is restored, gives them the same
	// "built, then edited by the snapshot" shape the def-array entities already have.
	for (const FElysiumEntityState& S : Snapshot.Entities)
	{
		if (!S.bRuntime || EntityList.IsValidIndex(S.Index))
		{
			continue;
		}
		if (S.Index != EntityList.Num())
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("snapshot '%s': runtime entity #%d cannot be placed (world holds %d) — skipped"),
				*Snapshot.MapName, S.Index, EntityList.Num());
			continue;
		}
		const FElysiumEntityHandle H = CreateRuntimeEntityNoSpawn(S.Def);
		if (FElysiumEntity* Fresh = Resolve(H))
		{
			CallEntitySpawn(*Fresh);
		}
	}

	int32 Applied = 0;
	for (const FElysiumEntityState& S : Snapshot.Entities)
	{
		FElysiumEntity* E = EntityList.IsValidIndex(S.Index) ? EntityList[S.Index].Get() : nullptr;
		if (!E)
		{
			continue;
		}
		if (!S.ClassName.IsNone() && E->Def && FName(*E->Def->Classname) != S.ClassName)
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("snapshot '%s': #%d is %s here but was %s when saved — skipped"),
				*Snapshot.MapName, S.Index, *E->Def->Classname, *S.ClassName.ToString());
			continue;
		}

		// Fields first, so a leaf's Serialize sees the restored keyfields. Matched by name, never by
		// position: a field added to a base class does not invalidate an existing payload, and a name
		// this build no longer registers is skipped with a warning rather than failing the load.
		if (E->Class)
		{
			for (const TPair<FName, FElysiumVariant>& F : S.Fields)
			{
				const FElysiumFieldAccessor* Acc = Reg.FindField(*E->Class, F.Key);
				if (!Acc || !Acc->bSave || !Acc->Set)
				{
					UE_LOG(LogElysiumWorld, Warning,
						TEXT("snapshot '%s': #%d has no saved field '%s' in this build — skipped"),
						*Snapshot.MapName, S.Index, *F.Key.ToString());
					continue;
				}
				if (F.Key == FName(TEXT("targetname")))
				{
					continue;   // the name index has to be re-keyed; handled below, once
				}
				if (F.Value.IsHandle())
				{
					// §6 — a saved handle carries a dead epoch, and re-stamping is this applier's
					// job (the archive drops the epoch by design). An index that no longer exists
					// reads Invalid, the same falsy value a killed entity produces.
					Acc->Set(*E, FElysiumVariant::Handle(RebaseHandle(F.Value.AsHandle)));
					continue;
				}
				Acc->Set(*E, F.Value);
			}
		}

		if (E->TargetName != S.TargetName)
		{
			RenameEntity(*E, S.TargetName);
		}

		// Through the runtime writer, so a leaf's body follows the restored position exactly as it
		// does for a live `point_teleport` — the body was built at the def origin by Spawn().
		if (!E->Origin.Equals(S.Origin, 0.0f))
		{
			E->SetRuntimeOrigin(S.Origin);
		}

		E->bSpawnCalled = S.bSpawnCalled;
		// Activate is part of entity lifecycle, not presentation reconstruction. Replaying it after
		// restoring a live record re-arms NPC admission and replaces the saved schedule/leaf state.
		// Records which omit this field came from an older active-map payload and default to true.
		// point_teleport is the one class whose activation-derived cache must persist; before that
		// cache was serialized, an empty leaf meant Activate had to rebuild it from the restored
		// live transform. Preserve that legacy migration instead of letting the new latch skip it.
		E->bActivateCalled = S.bActivateCalled
			&& !(E->ActivationStateMustPersist() && S.LeafState.IsEmpty());
		E->NextThink = S.NextThink;
		E->SetSavedNextThink(S.SavedNextThink);
		if (S.OutputTimesRemaining.Num() == E->OutputTimesRemaining.Num())
		{
			E->OutputTimesRemaining = S.OutputTimesRemaining;
		}

		if (S.LeafState.Num() > 0)
		{
			FMemoryReader Reader(S.LeafState, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
			E->Serialize(Ar);
		}

		// Dormancy last, and written directly rather than through ScriptHide/ScriptUnhide: those are
		// inputs with side effects (they stash and restore the think we have just restored ourselves).
		E->bHidden = S.bHidden;
		E->bDead = S.bDead;
		// Registered fields can alter a class-specific physical gate (notably trigger StartDisabled)
		// without changing hidden/dead. Re-apply unconditionally after all restored state is present.
		E->OnDormancyChanged();
		// Leaf deserializers and dormancy hooks may arm an entity while rebuilding transient state
		// (NPC patrol/interesting-place recovery does both). The generic saved schedule is the later,
		// authoritative statement: restore it last so freeze -> apply -> freeze remains identical and
		// a loaded entity cannot think earlier than the snapshot said.
		E->NextThink = S.NextThink;
		E->SetSavedNextThink(S.SavedNextThink);
		NotifyVisualChanged(*E);
		SnapshotEntityIndices.Add(S.Index);
		++Applied;
	}

	// Entities carried out with the player are not here any more (§5): kill them, which is exactly
	// what a resolve against them already reports and what gates their body. Last, so a stale record
	// for the same index cannot resurrect one — the absent set is the later statement about it.
	for (int32 Absent : Snapshot.AbsentEntities)
	{
		SnapshotEntityIndices.Add(Absent);
		if (FElysiumEntity* E = Resolve(FElysiumEntityHandle(Absent, Epoch)))
		{
			E->Kill();
		}
	}

	// 9.8 — an inventory's handle list is a CACHE of what the items' own Save-flagged fields say, so
	// it is re-derived rather than serialized twice. It runs here, after every record has landed and
	// every dead flag is final, because an item may restore either side of the character carrying it.
	for (const TUniquePtr<FElysiumEntity>& Candidate : EntityList)
	{
		if (FElysiumCombatCharacter* Char = Candidate ? Candidate->AsCombatCharacter() : nullptr)
		{
			Char->Inventory.RebuildFrom(*Char);
		}
	}

	// Rebind the map-epoch relationship after runtime entities and their dead flags are final. A
	// live saved controller keeps its stable runtime index; a removed/dead one resolves to nothing.
	PlayerControllerEntity = FElysiumEntityHandle::Invalid();
	for (const TUniquePtr<FElysiumEntity>& Candidate : EntityList)
	{
		if (Candidate && !Candidate->IsDead() && Candidate->Def
			&& Candidate->Def->Classname.Equals(TEXT("npc_VPlayerController"), ESearchCase::IgnoreCase))
		{
			PlayerControllerEntity = Candidate->Handle;
			break;
		}
	}

	// The queue is REPLACED: this load's Spawn pass has already queued its own openers, and the
	// saved queue is the one that was actually pending.
	EventQueue.Reset();
	for (const FElysiumIOEvent& Saved : Snapshot.Queue)
	{
		FElysiumIOEvent E = Saved;
		// §6 — the handles inside an event carry a dead epoch. Re-stamp them against this world; an
		// index that no longer resolves reads Invalid, the same falsy value a killed entity produces.
		E.Activator = RebaseHandle(Saved.Activator);
		E.Caller    = RebaseHandle(Saved.Caller);
		EventQueue.AddRestored(MoveTemp(E));
	}
	EventQueue.SetNextSerial(Snapshot.QueueNextSerial);
	// AddRestored deliberately skips the backward-clock guard — a restored deadline is already
	// absolute against the restored clock. The guard's own state comes back with it, so the first
	// live enqueue after the load compares against the time the save was written at.
	EventQueue.SetLastEnqueue(Snapshot.QueueLastEnqueue);

	ScreenFade.bActive      = Snapshot.Fade.bActive;
	ScreenFade.Color        = Snapshot.Fade.Color;
	ScreenFade.MaxAlpha     = Snapshot.Fade.MaxAlpha;
	ScreenFade.Duration     = Snapshot.Fade.Duration;
	ScreenFade.HoldTime     = Snapshot.Fade.HoldTime;
	ScreenFade.bFadeIn      = Snapshot.Fade.bFadeIn;
	ScreenFade.bAutoReverse = Snapshot.Fade.bAutoReverse;
	ScreenFade.StartTime    = Snapshot.Fade.StartTime;
	WeatherState = Snapshot.Weather;
	WeatherState.Tick(NowSeconds());
	PublishWetness();

	UE_LOG(LogElysiumWorld, Log,
		TEXT("applied snapshot '%s': %d/%d entity records, %d absent, %d queued"),
		*Snapshot.MapName, Applied, Snapshot.Entities.Num(), Snapshot.AbsentEntities.Num(),
		Snapshot.Queue.Num());
	return Applied;
}

FElysiumEntityHandle FElysiumEntityWorld::RebaseHandle(const FElysiumEntityHandle& Saved) const
{
	if (!Saved.IsSet() || !EntityList.IsValidIndex(Saved.Index))
	{
		return FElysiumEntityHandle::Invalid();
	}
	return FElysiumEntityHandle(Saved.Index, Epoch);
}

void FElysiumEntityWorld::Detach()
{
	Player = FElysiumEntityHandle::Invalid();
	bDetached = true;
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

void FElysiumEntityWorld::RouteEntityTouch(const FElysiumEntityHandle& Brush,
	const FElysiumEntityHandle& Activator, bool bBegin)
{
	const uint64 TouchKey = (static_cast<uint64>(static_cast<uint32>(Brush.Index)) << 32)
		| static_cast<uint32>(Activator.Index);
	if (!bBegin)
	{
		// Collision is switched off as part of Hide/Kill, after the entity has become inert. Release
		// the physical pair before the liveness gate so a later Unhide while still intersecting can
		// produce a fresh begin edge.
		if (ActiveTouches.Remove(TouchKey) == 0)
		{
			return;
		}
	}
	// Engine overlap callbacks can arrive while procedural collision and the pawn placement are
	// still settling. Dormant begins are deliberately forgotten: activation reconciles final
	// containment after authoritative placement. Ends still release an already-retained pair above,
	// even while the gameplay gate is closed or the brush has become inert.
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		return;
	}

	FElysiumEntity* E = Resolve(Brush);
	if (!E || E->IsInert())
	{
		// A dormant/dead brush cannot be touched (R6). On the end edge this is also the retail
		// asymmetry: `~CBaseEntity` reaches PhysicsRemoveTouchedList, which notifies the OTHER side
		// of each link and frees it without PhysicsRemoveToucher — so a dying trigger never receives
		// its own EndTouch, and a self-removing trigger_once emits no final OnEndTouch to occupants
		// still inside it (entity_io.md). Kill() flips bDead before releasing contacts, which is
		// what routes the release here (`Elysium.Substrate.DyingTriggerEndTouch`).
		return;
	}

	// Begin/end are edges, not level-triggered calls. Engine movement normally supplies exactly
	// one of each, but a teleport reconciliation also asks which brushes contain the player after
	// the transform. Collapse that second observation here so an authored trigger never double-
	// fires; an end releases the pair so a later genuine re-entry remains an edge.
	if (bBegin)
	{
		if (!E->CanBeginTouch(Activator))
		{
			return;
		}
		if (ActiveTouches.Contains(TouchKey))
		{
			return;
		}
		ActiveTouches.Add(TouchKey);
	}

	if (bBegin)
	{
		++TouchBeginCount;
		E->OnTouchStart(Activator);
	}
	else
	{
		++TouchEndCount;
		E->OnTouchEnd(Activator);
	}
	UE_LOG(LogElysiumWorld, Verbose, TEXT("(%8.3f) touch %s %s"),
		NowSeconds(), bBegin ? TEXT("begin") : TEXT("end"), *E->DebugString());
}

void FElysiumEntityWorld::EndBrushTouches(const FElysiumEntityHandle& Brush)
{
	if (!Brush.IsSet() || Brush.Epoch != Epoch)
	{
		return;
	}
	TArray<int32> ActivatorIndices;
	for (uint64 Key : ActiveTouches)
	{
		const int32 BrushIndex = static_cast<int32>(static_cast<uint32>(Key >> 32));
		if (BrushIndex == Brush.Index)
		{
			ActivatorIndices.Add(static_cast<int32>(static_cast<uint32>(Key)));
		}
	}
	ActivatorIndices.Sort();
	for (int32 ActivatorIndex : ActivatorIndices)
	{
		RouteBrushTouch(Brush, FElysiumEntityHandle(ActivatorIndex, Epoch), /*bBegin*/ false);
	}
}

// --- Player interaction ----------------------------------------------------------------

namespace
{
	constexpr double GPromptFadeInSeconds = 0.10;
	constexpr double GPromptFadeOutSeconds = 0.15;
}

void FElysiumEntityWorld::QueuePlayerUseEdge(EElysiumUseEdge Edge)
{
	if (bActive)
	{
		PendingUseEdges.Add(Edge);
	}
}

void FElysiumEntityWorld::ReconcilePlayerTouches(TConstArrayView<FElysiumEntityHandle> CurrentBrushes)
{
	if (!bActive || !Player.IsSet() || !IsTriggerResolutionEnabled())
	{
		return;
	}

	TSet<int32> CurrentIndices;
	for (const FElysiumEntityHandle& Brush : CurrentBrushes)
	{
		if (FElysiumEntity* E = Resolve(Brush); E && E->CanBeginTouch(Player))
		{
			CurrentIndices.Add(Brush.Index);
		}
	}

	TArray<int32> Ends;
	for (uint64 Key : ActiveTouches)
	{
		const int32 ActivatorIndex = static_cast<int32>(static_cast<uint32>(Key));
		const int32 BrushIndex = static_cast<int32>(static_cast<uint32>(Key >> 32));
		if (ActivatorIndex == Player.Index && !CurrentIndices.Contains(BrushIndex))
		{
			Ends.Add(BrushIndex);
		}
	}
	TArray<int32> Begins = CurrentIndices.Array();
	Begins.RemoveAll([this](int32 BrushIndex)
	{
		const uint64 Key = (static_cast<uint64>(static_cast<uint32>(BrushIndex)) << 32)
			| static_cast<uint32>(Player.Index);
		return ActiveTouches.Contains(Key);
	});
	Ends.Sort();
	Begins.Sort();
	for (int32 BrushIndex : Ends)
	{
		RouteBrushTouch(FElysiumEntityHandle(BrushIndex, Epoch), Player, /*bBegin*/ false);
	}
	for (int32 BrushIndex : Begins)
	{
		RouteBrushTouch(FElysiumEntityHandle(BrushIndex, Epoch), Player, /*bBegin*/ true);
	}
}

void FElysiumEntityWorld::QueuePlayerFeedEdge(EElysiumUseEdge Edge)
{
	if (bActive)
	{
		PendingFeedEdges.Add(Edge);
	}
}

void FElysiumEntityWorld::UpdatePlayerFeed()
{
	if (PendingFeedEdges.IsEmpty())
	{
		return;
	}
	TArray<EElysiumUseEdge, TInlineAllocator<2>> Edges = MoveTemp(PendingFeedEdges);
	PendingFeedEdges.Reset();
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		return;
	}
	FElysiumPlayer* PlayerEnt = FindPlayer();
	if (!PlayerEnt || PlayerEnt->IsInert())
	{
		return;
	}

	for (const EElysiumUseEdge Edge : Edges)
	{
		if (Edge == EElysiumUseEdge::Released)
		{
			// The low-level `-feed` edge only releases the command button. Feeding is a toggle-style
			// action: the first PRESS starts it and a later PRESS requests the paired release family.
			// Treating this edge as cancellation makes the patch's 0.1-second `vm_feed` tap abort
			// before the first blood pulse.
			continue;
		}
		// A second press while this player is the feeder requests the ordinary release transition.
		// The continuation latch belongs to the paired action; it is not the physical button's held
		// state. A victim-role player is not part of the ordinary slice and cannot cancel its attacker.
		if (PlayerEnt->IsFeedPaired())
		{
			if (!PlayerEnt->FeedState.bVictim && PlayerEnt->FeedState.bContinuation)
			{
				PlayerEnt->SetFeedContinuation(false);
				UE_LOG(LogElysiumWorld, Display, TEXT("INFO - Feed stop requested"));
			}
			continue;
		}
		// One unpaired press is one `Replenish` request. A miss does not become a held retry.
		FElysiumEntityHandle Candidate = FElysiumEntityHandle::Invalid();
		if (IElysiumEmbodiment* Bodily = Embodiment())
		{
			Candidate = Bodily->QueryFeedTarget();
		}
		FElysiumEntity* TargetEnt = Resolve(Candidate);
		FElysiumCombatCharacter* Victim = TargetEnt ? TargetEnt->AsCombatCharacter() : nullptr;
		if (!Victim)
		{
			UE_LOG(LogElysiumWorld, Display, TEXT("INFO - Feed missed: no live target"));
			UE_LOG(LogElysiumWorld, Verbose,
				TEXT("%s feed request missed: candidate #%d is not a live combat character"),
				*PlayerEnt->DebugString(), Candidate.IsSet() ? Candidate.Index : INDEX_NONE);
			continue;   // nothing in the hull, or what is there is not a character
		}
		PlayerEnt->AttemptFeed(*Victim);
	}
}

float FElysiumEntityWorld::InteractionPromptAlpha(double Now) const
{
	if (!InteractionPrompt.DisplayOwner.IsSet())
	{
		return 0.0f;
	}
	const double Duration = InteractionPrompt.bFadingIn
		? GPromptFadeInSeconds : GPromptFadeOutSeconds;
	const float Target = InteractionPrompt.bFadingIn ? 1.0f : 0.0f;
	const float T = Duration > 0.0
		? FMath::Clamp(static_cast<float>((Now - InteractionPrompt.TransitionTime) / Duration), 0.0f, 1.0f)
		: 1.0f;
	return FMath::Lerp(InteractionPrompt.StartAlpha, Target, T);
}

void FElysiumEntityWorld::TransitionUseFocus(const FElysiumUseCandidate* Candidate)
{
	const FElysiumEntityHandle Next = Candidate ? Candidate->Owner : FElysiumEntityHandle::Invalid();
	if (Next == FocusedUsable)
	{
		if (FElysiumEntity* Current = Resolve(FocusedUsable))
		{
			InteractionPrompt.Icon = Current->ResolveUseIcon(Player);
			InteractionPrompt.bLocked = Current->IsUseLocked();
		}
		if (Candidate)
		{
			FocusContext.AnchorPoint = Candidate->AnchorPoint;
			FocusContext.Selection = Candidate->Selection;
		}
		return;
	}

	const double Now = NowSeconds();
	const float CurrentAlpha = InteractionPromptAlpha(Now);
	if (FElysiumEntity* Old = Resolve(FocusedUsable))
	{
		Old->OnUseCursorLeave();
	}
	FocusedUsable = Next;
	FocusContext = FElysiumUseContext();
	FocusContext.Activator = Player;
	FocusContext.Owner = Next;
	FocusContext.TimeSeconds = Now;
	if (Candidate)
	{
		FocusContext.AnchorPoint = Candidate->AnchorPoint;
		FocusContext.Selection = Candidate->Selection;
	}

	if (FElysiumEntity* New = Resolve(FocusedUsable))
	{
		New->OnUseCursorEnter();
		InteractionPrompt.DisplayOwner = New->Handle;
		InteractionPrompt.Icon = New->ResolveUseIcon(Player);
		InteractionPrompt.bLocked = New->IsUseLocked();
		InteractionPrompt.bFadingIn = true;
		InteractionPrompt.StartAlpha = CurrentAlpha;
		InteractionPrompt.TransitionTime = Now;
	}
	else if (InteractionPrompt.DisplayOwner.IsSet())
	{
		InteractionPrompt.bFadingIn = false;
		InteractionPrompt.StartAlpha = CurrentAlpha;
		InteractionPrompt.TransitionTime = Now;
	}

	UE_LOG(LogElysiumWorld, Verbose, TEXT("(%8.3f) interaction-focus -> %s"),
		Now, FocusedUsable.IsSet() ? *DescribeHandle(FocusedUsable) : TEXT("<none>"));
}

void FElysiumEntityWorld::EndActiveUse(EElysiumUseEndReason Reason)
{
	if (!ActiveUse.IsSet())
	{
		return;
	}
	const FActiveUse Ending = ActiveUse.GetValue();
	ActiveUse.Reset();
	if (FElysiumEntity* Entity = Resolve(Ending.Context.Owner))
	{
		Entity->EndPlayerUse(Ending.Context, Reason);
	}
	LastUseOutcome = (Reason == EElysiumUseEndReason::Released
		|| Reason == EElysiumUseEndReason::Completed)
		? EElysiumUseOutcome::Completed : EElysiumUseOutcome::Cancelled;
}

bool FElysiumEntityWorld::EndPlayerUseSession(const FElysiumEntityHandle& OwnerHandle,
	EElysiumUseEndReason Reason)
{
	if (!ActiveUse.IsSet() || (OwnerHandle.IsSet() && ActiveUse->Context.Owner != OwnerHandle))
	{
		return false;
	}
	EndActiveUse(Reason);
	return true;
}

FElysiumUseBeginResult FElysiumEntityWorld::BeginPlayerUseSession(
	const FElysiumEntityHandle& OwnerHandle, const FElysiumEntityHandle& Activator)
{
	if (ActiveUse.IsSet() || DialogueSession || OpenSignOwner.IsSet())
	{
		LastUseOutcome = EElysiumUseOutcome::Busy;
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Busy);
	}
	FElysiumEntity* Entity = Resolve(OwnerHandle);
	FElysiumUseContext Context;
	Context.Activator = Activator;
	Context.Owner = OwnerHandle;
	Context.TimeSeconds = NowSeconds();
	if (!Entity)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("programmatic +use session failed: owner %s does not resolve"),
			*OwnerHandle.ToString());
		LastUseOutcome = EElysiumUseOutcome::Unavailable;
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	if (!Entity->CanPlayerFocus(Context))
	{
		LastUseOutcome = EElysiumUseOutcome::Unavailable;
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	if (!Entity->UseFilterName.IsEmpty())
	{
		FElysiumEntity* Filter = FindByName(Entity->UseFilterName);
		if (!Filter)
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("%s use_filter_name '%s' did not resolve; allowing use"),
				*Entity->DebugString(), *Entity->UseFilterName);
		}
		if (Filter && !Filter->PassesFilter(Activator))
		{
			LastUseOutcome = EElysiumUseOutcome::Unavailable;
			return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
		}
	}
	const FElysiumUseBeginResult Result = Entity->BeginPlayerUse(Context);
	if (Result.Outcome == EElysiumUseOutcome::SessionStarted
		&& Result.SessionKind != EElysiumUseSessionKind::None)
	{
		FActiveUse Session;
		Session.Context = Context;
		Session.Kind = Result.SessionKind;
		ActiveUse = Session;
	}
	LastUseOutcome = Result.Outcome;
	return Result;
}

void FElysiumEntityWorld::UpdatePlayerInteraction()
{
	if (!bActive || !IsTriggerResolutionEnabled())
	{
		PendingUseEdges.Reset();
		TransitionUseFocus(nullptr);
		return;
	}

	if (ActiveUse.IsSet())
	{
		FElysiumEntity* ActiveEntity = Resolve(ActiveUse->Context.Owner);
		FElysiumEntity* ActiveUser = Resolve(ActiveUse->Context.Activator);
		if (!ActiveEntity || ActiveEntity->IsInert() || !ActiveUser || ActiveUser->IsInert())
		{
			EndActiveUse(EElysiumUseEndReason::TargetInvalid);
		}
	}

	FElysiumUseQueryResult Query;
	if (IElysiumEmbodiment* Bodily = Embodiment())
	{
		Query = Bodily->QueryPlayerUse(FocusedUsable);
	}

	const FElysiumUseCandidate* Selected = nullptr;
	for (const FElysiumUseCandidate& Candidate : Query.Candidates)
	{
		FElysiumEntity* Entity = Resolve(Candidate.Owner);
		FElysiumUseContext Context;
		Context.Activator = Player;
		Context.Owner = Candidate.Owner;
		Context.AnchorPoint = Candidate.AnchorPoint;
		Context.Selection = Candidate.Selection;
		Context.TimeSeconds = NowSeconds();
		if (Entity && Entity->CanPlayerFocus(Context))
		{
			Selected = &Candidate;
			break;
		}
		// An exact entity hit fails closed. Assistance must never jump through the object under
		// the reticle to something merely close to it.
		if (Candidate.Selection == EElysiumUseSelection::Exact)
		{
			break;
		}
	}
	TransitionUseFocus(Selected);
	LastUseOutcome = Selected ? EElysiumUseOutcome::Completed : Query.MissOutcome;

	const TArray<EElysiumUseEdge, TInlineAllocator<2>> Edges = MoveTemp(PendingUseEdges);
	PendingUseEdges.Reset();
	for (EElysiumUseEdge Edge : Edges)
	{
		if (Edge == EElysiumUseEdge::Released)
		{
			if (ActiveUse.IsSet() && ActiveUse->Kind == EElysiumUseSessionKind::WhileHeld)
			{
				EndActiveUse(EElysiumUseEndReason::Released);
			}
			continue;
		}

		if (ActiveUse.IsSet() || DialogueSession || OpenSignOwner.IsSet())
		{
			LastUseOutcome = EElysiumUseOutcome::Busy;
			continue;
		}
		FElysiumEntity* Entity = Resolve(FocusedUsable);
		if (!Entity || !Entity->CanPlayerFocus(FocusContext))
		{
			LastUseOutcome = EElysiumUseOutcome::Unavailable;
			continue;
		}

		if (!Entity->UseFilterName.IsEmpty())
		{
			FElysiumEntity* Filter = FindByName(Entity->UseFilterName);
			// Retail treats an unresolved filter target as no filter. A resolved filter is the
			// authoritative gate and sees the player as its activator, exactly like trigger filters.
			if (!Filter)
			{
				UE_LOG(LogElysiumWorld, Warning,
					TEXT("%s use_filter_name '%s' did not resolve; allowing use"),
					*Entity->DebugString(), *Entity->UseFilterName);
			}
			if (Filter && !Filter->PassesFilter(Player))
			{
				LastUseOutcome = EElysiumUseOutcome::Unavailable;
				continue;
			}
		}
		FocusContext.TimeSeconds = NowSeconds();
		const bool bWasLocked = Entity->IsUseLocked();
		const FElysiumUseBeginResult Result = Entity->BeginPlayerUse(FocusContext);
		LastUseOutcome = bWasLocked ? EElysiumUseOutcome::Locked : Result.Outcome;
		if (Result.Outcome == EElysiumUseOutcome::SessionStarted
			&& Result.SessionKind != EElysiumUseSessionKind::None)
		{
			FActiveUse Session;
			Session.Context = FocusContext;
			Session.Kind = Result.SessionKind;
			ActiveUse = Session;
		}
	}
}

FElysiumInteractionView FElysiumEntityWorld::GetInteractionView() const
{
	FElysiumInteractionView View;
	if (ActiveUse.IsSet() && ActiveUse->Kind == EElysiumUseSessionKind::Explicit)
	{
		return View;
	}
	View.PromptAlpha = InteractionPromptAlpha(NowSeconds());
	View.bVisible = InteractionPrompt.DisplayOwner.IsSet()
		&& View.PromptAlpha > KINDA_SMALL_NUMBER;
	View.bActionable = FocusedUsable.IsSet() && !ActiveUse.IsSet();
	View.Icon = InteractionPrompt.Icon;
	View.bLocked = InteractionPrompt.bLocked;
	return View;
}

namespace
{
	FElysiumItemContainer* FindOpenLootContainer(FElysiumEntityWorld& World)
	{
		const FElysiumEntityHandle Player = World.PlayerHandle();
		for (const TUniquePtr<FElysiumEntity>& Candidate : World.Entities())
		{
			FElysiumItemContainer* Container = Candidate ? Candidate->AsItemContainer() : nullptr;
			if (Container && !Container->IsDead() && Container->CurrentUser == Player)
			{
				return Container;
			}
		}
		return nullptr;
	}
}

bool FElysiumEntityWorld::BuildLootView(FElysiumLootView& Out) const
{
	Out = FElysiumLootView();
	FElysiumEntityWorld& Mutable = const_cast<FElysiumEntityWorld&>(*this);
	FElysiumItemContainer* Container = FindOpenLootContainer(Mutable);
	const FElysiumPlayer* PlayerEntity = FindPlayer();
	if (!Container || !PlayerEntity)
	{
		return false;
	}
	Container->BuildLootView(Out, *PlayerEntity);
	return true;
}

bool FElysiumEntityWorld::BuildTerminalView(FElysiumTerminalView& Out) const
{
	Out = FElysiumTerminalView();
	if (!ActiveUse.IsSet() || ActiveUse->Kind != EElysiumUseSessionKind::Explicit)
	{
		return false;
	}
	const FElysiumEntity* Entity = Resolve(ActiveUse->Context.Owner);
	const FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	if (!Terminal || Terminal->CurrentUser != ActiveUse->Context.Activator)
	{
		return false;
	}
	Terminal->BuildView(Out);
	return Out.IsOpen();
}

bool FElysiumEntityWorld::SubmitTerminalCommand(const FElysiumEntityHandle& Owner,
	uint32 SessionSerial, const FString& Command)
{
	if (!ActiveUse.IsSet() || ActiveUse->Context.Owner != Owner
		|| ActiveUse->Context.Activator != Player)
	{
		return false;
	}
	FElysiumEntity* Entity = Resolve(Owner);
	FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	return Terminal && Terminal->CurrentUser == Player && Terminal->Submit(SessionSerial, Command);
}

bool FElysiumEntityWorld::SubmitActiveTerminalCommand(const FString& Command)
{
	if (!ActiveUse.IsSet())
	{
		return false;
	}
	FElysiumEntity* Entity = Resolve(ActiveUse->Context.Owner);
	FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	return Terminal && SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial, Command);
}

bool FElysiumEntityWorld::PlayerBeginTerminalHack(const FElysiumEntityHandle& Owner,
	uint32 SessionSerial)
{
	if (!ActiveUse.IsSet() || ActiveUse->Context.Owner != Owner
		|| ActiveUse->Context.Activator != Player)
	{
		return false;
	}
	FElysiumEntity* Entity = Resolve(Owner);
	FElysiumTerminal* Terminal = Entity ? Entity->AsTerminal() : nullptr;
	return Terminal && Terminal->CurrentUser == Player && Terminal->BeginHack(SessionSerial);
}

bool FElysiumEntityWorld::PlayerLootTake(int32 Slot)
{
	FElysiumItemContainer* Container = FindOpenLootContainer(*this);
	FElysiumPlayer* PlayerEntity = FindPlayer();
	if (!Container || !PlayerEntity)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("loot take failed: no open container/player for slot %d"), Slot);
		return false;
	}
	return Container->TakeToPlayer(*PlayerEntity, Slot);
}

bool FElysiumEntityWorld::PlayerLootGive(int32 Slot)
{
	FElysiumItemContainer* Container = FindOpenLootContainer(*this);
	FElysiumPlayer* PlayerEntity = FindPlayer();
	if (!Container || !PlayerEntity)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("loot give failed: no open container/player for slot %d"), Slot);
		return false;
	}
	return Container->GiveFromPlayer(*PlayerEntity, Slot);
}

bool FElysiumEntityWorld::PlayerCloseLoot()
{
	FElysiumItemContainer* Container = FindOpenLootContainer(*this);
	if (!Container)
	{
		UE_LOG(LogElysiumWorld, Warning, TEXT("loot close failed: no open container session"));
		return false;
	}
	if (!EndPlayerUseSession(Container->Handle, EElysiumUseEndReason::Completed))
	{
		UE_LOG(LogElysiumWorld, Warning, TEXT("loot close failed: %s did not own active +use"),
			*Container->DebugString());
		return false;
	}
	return true;
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

// --- Open dialogue (P9 9.1 / B4) ------------------------------------------------------------

FElysiumDlgConversation* FElysiumEntityWorld::GetOpenDialog() const
{
	return DialogueSession ? DialogueSession->Conversation.Get() : nullptr;
}

FElysiumEntityHandle FElysiumEntityWorld::GetOpenDialogOwner() const
{
	return DialogueSession ? DialogueSession->Owner : FElysiumEntityHandle::Invalid();
}

bool FElysiumEntityWorld::HasActiveDialogueBodyClip(const FElysiumEntityHandle& Speaker) const
{
	return DialogueSession && DialogueSession->Owner == Speaker
		&& DialogueSession->LineScene.HasActiveBodyClip();
}

bool FElysiumEntityWorld::CanPlayerAdvanceAutomatic() const
{
	return DialogueSession && DialogueSession->bAutomaticFallback
		&& DialogueSession->Conversation.IsValid()
		&& DialogueSession->Conversation->IsAwaitingAutomatic();
}

void FElysiumEntityWorld::BeginDialogueTurn()
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid())
	{
		return;
	}
	FElysiumDlgConversation& Conversation = *DialogueSession->Conversation;
	const FElysiumDlgLine* Line = Conversation.CurrentNpcLine();
	if (!Line)
	{
		if (!Conversation.IsOver())
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s has no current NPC line for revision %u"),
				*DescribeHandle(DialogueSession->Owner), Conversation.Revision());
		}
		return;
	}

	DialogueSession->CurrentLineId = Line->Id;
	DialogueSession->TurnRevision = Conversation.Revision();
	DialogueSession->CurrentVoice = FElysiumVoiceHandle::Invalid();
	DialogueSession->bAutomaticFallback = false;

	// The spoken line is the presented turn. Any pending Auto-Link/Auto-End remains attached to it
	// until this exact voice handle completes; it is never submitted as a subtitle or response.
	SelectDialogueCamera(/*bLineBoundary*/ true);
	DialogueSession->LineScene.Begin(*this, DialogueSession->Owner,
		Conversation.File().SourcePath, Line->Id, NowSeconds());
	if (LineService)
	{
		FElysiumEntity* Speaker = Resolve(DialogueSession->Owner);
		DialogueSession->CurrentVoice = LineService->PlayDialogueTurn(DialogueSession->Owner,
			Conversation.File().SourcePath, Line->Id,
			Speaker ? Speaker->Origin : FVector::ZeroVector,
			Speaker ? Speaker->GetSkeletalBody() : nullptr);
		if (DialogueSession->CurrentVoice.IsValid())
		{
			BeginDialogueLipsync(Conversation.File().SourcePath, Line->Id);
		}
	}

	if (Conversation.IsAwaitingAutomatic() && !DialogueSession->CurrentVoice.IsValid())
	{
		// A null audio service is a supported headless configuration. In a playable world, an invalid
		// submission is an unexpected failure and must be diagnosable. Either way the line stays on
		// screen and presentation exposes Continue, so "Alright." cannot disappear in a zero-time hop.
		DialogueSession->bAutomaticFallback = true;
		if (Audio())
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s line %d could not start voice for pending automatic row %d; awaiting manual advance"),
				*DescribeHandle(DialogueSession->Owner), Line->Id,
				Conversation.PendingAutomatic() ? Conversation.PendingAutomatic()->Id : INDEX_NONE);
		}
	}
}

void FElysiumEntityWorld::UpdateDialogueAutomatic()
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid()
		|| !DialogueSession->Conversation->IsAwaitingAutomatic()
		|| DialogueSession->bAutomaticFallback)
	{
		return;
	}
	TSharedPtr<FElysiumDlgConversation> Conversation = DialogueSession->Conversation;
	const FElysiumDlgLine* Line = Conversation->CurrentNpcLine();
	if (!Line || Line->Id != DialogueSession->CurrentLineId
		|| Conversation->Revision() != DialogueSession->TurnRevision)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("dialogue %s automatic join lost its turn identity (line=%d revision=%u)"),
			*DescribeHandle(DialogueSession->Owner), DialogueSession->CurrentLineId,
			DialogueSession->TurnRevision);
		DialogueSession->bAutomaticFallback = true;
		return;
	}
	if (!DialogueSession->CurrentVoice.IsValid() || !Audio())
	{
		DialogueSession->bAutomaticFallback = true;
		return;
	}
	if (Audio()->IsVoicePlaying(DialogueSession->CurrentVoice))
	{
		return;
	}

	Conversation->ResolveAutomatic();
	if (!DialogueSession || DialogueSession->Conversation != Conversation)
	{
		// The automatic row's action may synchronously close or replace dialogue. Its old voice
		// completion owns no state in the resulting session.
		return;
	}
	if (Conversation->IsOver())
	{
		EndDialogSession(/*bSilent*/ false);
		return;
	}
	BeginDialogueTurn();
}

void FElysiumEntityWorld::OpenDialog(const FElysiumEntityHandle& NewOwner,
	TSharedRef<FElysiumDlgConversation> Conversation, EElysiumDialogOpenerKind Opener,
	int32 RawFlags, const FString& DefaultCamera, const FElysiumBodyOwnerToken& SuppliedBodyOwner)
{
	if (ActiveUse.IsSet())
	{
		EndActiveUse(EElysiumUseEndReason::Cancelled);
	}
	FElysiumEntity* OwnerEntity = Resolve(NewOwner);
	FElysiumBodyOwnerToken BodyOwner = SuppliedBodyOwner;
	// A different NPC can acquire before the old session is displaced, making replacement atomic:
	// refusal leaves the old conversation untouched. The same NPC must release its existing token
	// first; that path is already live and can only fail after owner loss, when retaining the old
	// conversation would be invalid too.
	const bool bSameOwnerReplacement = DialogueSession && DialogueSession->Owner == NewOwner;
	if (!BodyOwner.IsSet() && OwnerEntity && !bSameOwnerReplacement)
	{
		BodyOwner = OwnerEntity->BeginDialogueBodySession();
	}
	if (!OwnerEntity || (!BodyOwner.IsSet() && !bSameOwnerReplacement))
	{
		UE_LOG(LogElysiumWorld, Warning, TEXT("dialogue body acquisition refused for %s"),
			*DescribeHandle(NewOwner));
		return;
	}
	// Any accepted second acquisition replaces the running session silently. Release its camera
	// before selecting the new request so one conversation never owns two handles.
	if (DialogueSession)
	{
		EndDialogSession(/*bSilent*/ true);
	}
	if (bSameOwnerReplacement)
	{
		OwnerEntity = Resolve(NewOwner);
		BodyOwner = OwnerEntity ? OwnerEntity->BeginDialogueBodySession()
			: FElysiumBodyOwnerToken();
		if (!BodyOwner.IsSet())
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("same-owner dialogue replacement lost body ownership for %s"),
				*DescribeHandle(NewOwner));
			return;
		}
	}
	DialogueSession = MakeUnique<FElysiumDialogueSession>();
	DialogueSession->Owner = NewOwner;
	DialogueSession->Listener = PlayerHandle();
	DialogueSession->Conversation = Conversation;
	DialogueSession->Opener = Opener;
	DialogueSession->RawFlags = RawFlags;
	DialogueSession->DefaultCamera = DefaultCamera;
	DialogueSession->BodyOwner = BodyOwner;
	DialogueSession->NormalizedCamera = ElysiumCameraShots::NormalizeKey(DefaultCamera);
	DialogueSession->ScreenSide = (NewOwner.Index & 1) == 0 ? 1.0f : -1.0f;

	// Camera acquisition precedes the presentation announcement. A headless/null-camera world still
	// runs exactly the same dialogue and event order.
	BeginDialogueTurn();

	if (IElysiumPresenter* P = Presenter())
	{
		P->OpenDialog(NewOwner, *Conversation);
	}

	// A conversation that opened already closed (no content NPC line) ends at once, so the beat still
	// advances (OnDialogEnd -> DialogPostProcess) rather than hanging on an empty panel.
	if (Conversation->IsOver())
	{
		EndDialogSession(/*bSilent*/ false);
	}
}

void FElysiumEntityWorld::PlayerDialogChoose(int32 VisibleIndex)
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid())
	{
		return;
	}
	TSharedPtr<FElysiumDlgConversation> Conversation = DialogueSession->Conversation;
	const uint32 BeforeRevision = Conversation->Revision();
	Conversation->Choose(VisibleIndex);
	if (Conversation->Revision() == BeforeRevision)
	{
		// Invalid/stale input, including a response submitted after an automatic wait became active,
		// must not restart the current voice and postpone its completion edge.
		return;
	}
	if (!DialogueSession || DialogueSession->Conversation != Conversation)
	{
		return; // the chosen row's action synchronously closed/replaced the session
	}
	if (!Conversation->IsOver())
	{
		BeginDialogueTurn();
	}
	if (Conversation->IsOver())
	{
		EndDialogSession(/*bSilent*/ false);
	}
}

void FElysiumEntityWorld::PlayerDialogAdvance()
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid())
	{
		return;
	}
	if (DialogueSession->Conversation->IsAwaitingAutomatic())
	{
		if (!DialogueSession->bAutomaticFallback)
		{
			return;
		}
		TSharedPtr<FElysiumDlgConversation> Conversation = DialogueSession->Conversation;
		Conversation->ResolveAutomatic();
		if (!DialogueSession || DialogueSession->Conversation != Conversation)
		{
			return;
		}
		if (Conversation->IsOver())
		{
			EndDialogSession(/*bSilent*/ false);
		}
		else
		{
			BeginDialogueTurn();
		}
		return;
	}
	DialogueSession->Conversation->AdvanceTerminal();
	if (DialogueSession->Conversation->IsOver())
	{
		EndDialogSession(/*bSilent*/ false);
	}
}

void FElysiumEntityWorld::CloseDialog(bool bSilent)
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid())
	{
		return;
	}
	DialogueSession->Conversation->Close();
	EndDialogSession(bSilent);
}

void FElysiumEntityWorld::SelectDialogueCamera(bool bLineBoundary)
{
	if (!DialogueSession)
	{
		return;
	}
	IElysiumCameraService* Service = Camera();
	if (!Service)
	{
		DialogueSession->FallbackReason = TEXT("camera service unavailable");
		return;
	}
	FElysiumEntity* Speaker = Resolve(DialogueSession->Owner);
	FElysiumPlayer* Listener = FindPlayer();
	if (!Speaker || Speaker->IsInert() || !Listener)
	{
		DialogueSession->FallbackReason = TEXT("dialogue target unavailable");
		return;
	}

	auto Publish = [this, Service](FElysiumCameraRequest Request,
		EElysiumDialogueDirectorSource Source, EElysiumDialogueShotProfile Profile,
		float MinimumHold)
	{
		DialogueSession->CameraRequest = MoveTemp(Request);
		DialogueSession->DirectorSource = Source;
		DialogueSession->SelectedProfile = Profile;
		DialogueSession->MinimumHoldSeconds = MinimumHold;
		DialogueSession->SelectedAt = NowSeconds();
		if (Service->IsCameraLive(DialogueSession->CameraHandle))
		{
			Service->UpdateCamera(DialogueSession->CameraHandle, DialogueSession->CameraRequest);
		}
		else
		{
			DialogueSession->CameraHandle = Service->AcquireCamera(DialogueSession->CameraRequest);
		}
	};

	const FVector SpeakerEye = Speaker->EyePosition();
	const FVector ListenerEye = Listener->EyePosition();
	DialogueSession->CandidateRejections.Reset();

	if (Service->DialogueCamerasEnabled() && !DialogueSession->NormalizedCamera.IsEmpty())
	{
		const FElysiumCameraShotDef* Def = ElysiumCameraShots::Load(DialogueSession->NormalizedCamera);
		FElysiumCameraShot Shot;
		if (Def && Def->IsValid()
			&& FElysiumCameraDirector::Resolve(this, *Def, DialogueSession->Owner, Shot))
		{
			FElysiumCameraRequest Request;
			Request.Kind = EElysiumCameraRequestKind::Dialogue;
			Request.Owner = DescribeHandle(DialogueSession->Owner);
			Request.DebugName = FString::Printf(TEXT("Dialogue:%s"), *Def->Name);
			Request.Priority = 500;
			Request.bOverridePose = true;
			Request.Shot = Shot;
			Request.BlendInSeconds = Shot.BlendSeconds;
			Request.BlendOutSeconds = Shot.BlendSeconds;
			Request.Control = EElysiumCameraControlPolicy::Preserve;
			Request.bShowHud = Def->Constraints.bShowHud;
			Request.bDrawViewmodel = Def->Constraints.bDrawViewmodel;
			Request.bShowPlayerBody = !Def->Constraints.bDrawViewmodel;
			Request.bDialogPOV = Def->Constraints.bDialogPOV;
			Request.bRequireSubtitleSafe = false; // source shot authors its own subject framing
			Request.Fallback = EElysiumCameraFallback::SourceShot;
			Request.SourceShot = DialogueSession->NormalizedCamera;
			Request.SelectedProfile = TEXT("source-shot");
			Request.SpeakerAnchor.Position = SpeakerEye;
			Request.SpeakerAnchor.Name = TEXT("speaker-eye");
			Request.SpeakerAnchor.bValid = true;
			Request.ListenerAnchor.Position = ListenerEye;
			Request.ListenerAnchor.Name = TEXT("listener-eye");
			Request.ListenerAnchor.bValid = true;
			FString Reject;
			if (Service->EvaluateDialogueCandidate(Request, Reject))
			{
				Publish(MoveTemp(Request), EElysiumDialogueDirectorSource::SourceShot,
					EElysiumDialogueShotProfile::Fallback, 2.0f);
				return;
			}
			DialogueSession->CandidateRejections += FString::Printf(TEXT("%s: %s"),
				*DialogueSession->NormalizedCamera, *Reject);
		}
		else
		{
			DialogueSession->CandidateRejections += FString::Printf(TEXT("%s: missing/unresolved"),
				*DialogueSession->NormalizedCamera);
		}
	}

	if (Service->DialogueCamerasEnabled())
	{
		// Hold a selected grammar shot until its boundary minimum expires. Its anchors still track each
		// frame through UpdateSelectedDialogueCamera; only candidate selection is held.
		if (bLineBoundary
			&& DialogueSession->DirectorSource == EElysiumDialogueDirectorSource::AuthoredProfile
			&& NowSeconds() - DialogueSession->SelectedAt < DialogueSession->MinimumHoldSeconds)
		{
			UpdateSelectedDialogueCamera();
			return;
		}

		FElysiumDialogueCameraContext Context;
		Context.SpeakerEye = SpeakerEye;
		Context.ListenerEye = ListenerEye;
		Context.ScreenSide = DialogueSession->ScreenSide;
		Context.bAllowCloseUp = false; // only explicit line/profile metadata may enable it
		Context.Owner = DescribeHandle(DialogueSession->Owner);
		TArray<FElysiumDialogueCameraProfile> Profiles;
		Service->GetDialogueProfiles(Profiles);
		if (DialogueSession->DirectorSource != EElysiumDialogueDirectorSource::None && Profiles.Num() > 1)
		{
			const int32 Rotate = FMath::Abs(DialogueSession->CurrentLineId) % (Profiles.Num() - 1);
			Algo::Rotate(Profiles, Rotate);
		}
		for (const FElysiumDialogueCameraProfile& Profile : Profiles)
		{
			if (Profile.Kind == EElysiumDialogueShotProfile::Fallback
				|| (Profile.bAllowCloseUp && !Context.bAllowCloseUp))
			{
				continue;
			}
			FElysiumCameraRequest Request = ElysiumDialogueCamera::BuildRequest(Profile, Context);
			if (DialogueSession->CameraRequest.bOverridePose
				&& !ElysiumDialogueCamera::PreservesScreenSide(DialogueSession->CameraRequest, Request))
			{
				DialogueSession->CandidateRejections += FString::Printf(TEXT("%s: screen-side; "),
					*Profile.Name.ToString());
				continue;
			}
			FString Reject;
			if (Service->EvaluateDialogueCandidate(Request, Reject))
			{
				Publish(MoveTemp(Request), EElysiumDialogueDirectorSource::AuthoredProfile,
					Profile.Kind, Profile.MinimumHoldSeconds);
				return;
			}
			DialogueSession->CandidateRejections += FString::Printf(TEXT("%s: %s; "),
				*Profile.Name.ToString(), *Reject);
		}
	}

	FElysiumDialogueCameraProfile Fallback;
	Fallback.Name = TEXT("PlayerViewFallback");
	Fallback.Kind = EElysiumDialogueShotProfile::Fallback;
	FElysiumDialogueCameraContext Context;
	Context.SpeakerEye = SpeakerEye;
	Context.ListenerEye = ListenerEye;
	Context.Owner = DescribeHandle(DialogueSession->Owner);
	FElysiumCameraRequest Request = ElysiumDialogueCamera::BuildRequest(Fallback, Context);
	Request.FallbackReason = Service->DialogueCamerasEnabled()
		? (DialogueSession->CandidateRejections.IsEmpty() ? TEXT("no camera candidate")
			: DialogueSession->CandidateRejections)
		: TEXT("dialogue cameras disabled by user");
	DialogueSession->FallbackReason = Request.FallbackReason;
	Publish(MoveTemp(Request), EElysiumDialogueDirectorSource::PlayerView,
		EElysiumDialogueShotProfile::Fallback, 0.0f);
}

void FElysiumEntityWorld::UpdateSelectedDialogueCamera()
{
	if (!DialogueSession || !Camera()
		|| !Camera()->IsCameraLive(DialogueSession->CameraHandle))
	{
		return;
	}
	FElysiumEntity* Speaker = Resolve(DialogueSession->Owner);
	FElysiumPlayer* Listener = FindPlayer();
	if (!Speaker || Speaker->IsInert() || !Listener)
	{
		EndDialogSession(/*bSilent*/ true);
		return;
	}

	if (DialogueSession->DirectorSource == EElysiumDialogueDirectorSource::SourceShot)
	{
		const FElysiumCameraShotDef* Def = ElysiumCameraShots::Load(DialogueSession->NormalizedCamera);
		FElysiumCameraShot Shot;
		if (!Def || !FElysiumCameraDirector::Resolve(this, *Def, DialogueSession->Owner, Shot))
		{
			EndDialogSession(/*bSilent*/ true);
			return;
		}
		DialogueSession->CameraRequest.Shot = Shot;
	}
	else if (DialogueSession->DirectorSource == EElysiumDialogueDirectorSource::AuthoredProfile)
	{
		FElysiumDialogueCameraContext Context;
		Context.SpeakerEye = Speaker->EyePosition();
		Context.ListenerEye = Listener->EyePosition();
		Context.ScreenSide = DialogueSession->ScreenSide;
		Context.Owner = DescribeHandle(DialogueSession->Owner);
		TArray<FElysiumDialogueCameraProfile> Profiles;
		Camera()->GetDialogueProfiles(Profiles);
		for (const FElysiumDialogueCameraProfile& Profile : Profiles)
		{
			if (Profile.Kind == DialogueSession->SelectedProfile)
			{
				FElysiumCameraRequest Updated = ElysiumDialogueCamera::BuildRequest(Profile, Context);
				Updated.FallbackReason = DialogueSession->CameraRequest.FallbackReason;
				DialogueSession->CameraRequest = MoveTemp(Updated);
				break;
			}
		}
	}
	Camera()->UpdateCamera(DialogueSession->CameraHandle, DialogueSession->CameraRequest);
}

void FElysiumEntityWorld::RefreshDialogueCamera()
{
	if (!DialogueSession)
	{
		return;
	}
	if (!Resolve(DialogueSession->Owner) || !FindPlayer())
	{
		EndDialogSession(/*bSilent*/ true);
		return;
	}
	UpdateSelectedDialogueCamera();
}

bool FElysiumEntityWorld::GetDialogueCameraGaze(FVector& OutPoint) const
{
	if (!DialogueSession || !DialogueSession->CameraRequest.bDialogPOV
		|| !DialogueSession->CameraRequest.bOverridePose)
	{
		return false;
	}
	OutPoint = DialogueSession->CameraRequest.Shot.Origin;
	return true;
}

bool FElysiumEntityWorld::DialogueCameraHidesHud() const
{
	return DialogueSession && !DialogueSession->CameraRequest.bShowHud;
}

void FElysiumEntityWorld::GetDialogueDebugState(
	TArray<TPair<FString, FString>>& Out) const
{
	if (!DialogueSession)
	{
		Out.Emplace(TEXT("Dialogue"), TEXT("closed"));
		return;
	}
	Out.Emplace(TEXT("Opener"), ElysiumDialogueCamera::LexToString(DialogueSession->Opener));
	Out.Emplace(TEXT("Raw flags"), FString::FromInt(DialogueSession->RawFlags));
	Out.Emplace(TEXT("Decoded flags"), FString::FromInt(DialogueSession->DecodedFlags));
	Out.Emplace(TEXT("default_camera"), DialogueSession->DefaultCamera);
	Out.Emplace(TEXT("Camera handle"), FString::Printf(TEXT("slot=%d gen=%u epoch=%llu"),
		DialogueSession->CameraHandle.Slot, DialogueSession->CameraHandle.Generation,
		DialogueSession->CameraHandle.Epoch));
	Out.Emplace(TEXT("Source shot"), DialogueSession->CameraRequest.SourceShot);
	Out.Emplace(TEXT("Profile"), DialogueSession->CameraRequest.SelectedProfile);
	Out.Emplace(TEXT("Fallback"), DialogueSession->FallbackReason);
	Out.Emplace(TEXT("Candidate rejects"), DialogueSession->CandidateRejections);
	Out.Emplace(TEXT("Body owner"), FString::Printf(TEXT("%s gen=%u"),
		LexToString(DialogueSession->BodyOwner.Owner), DialogueSession->BodyOwner.Generation));
	if (const FElysiumEntity* Speaker = Resolve(DialogueSession->Owner))
	{
		const float RenderedYaw = ElysiumSkeletalBasis::FromSourceAngles(Speaker->Angles).Yaw;
		const FVector ToSpeaker = Speaker->EyePosition() - DialogueSession->CameraRequest.Shot.Origin;
		const FVector SpeakerForward = FRotator(0.0f, RenderedYaw, 0.0f).Vector();
		const float Error = ToSpeaker.IsNearlyZero() ? 0.0f
			: FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct((-ToSpeaker).GetSafeNormal2D(), SpeakerForward), -1.0f, 1.0f)));
		Out.Emplace(TEXT("Speaker yaw"), FString::Printf(TEXT("source %.2f / rendered %.2f"),
			Speaker->Angles.Y, RenderedYaw));
		Out.Emplace(TEXT("Camera-to-speaker"), ToSpeaker.ToCompactString());
		Out.Emplace(TEXT("Forward angular error"), FString::Printf(TEXT("%.2f deg"), Error));
		Out.Emplace(TEXT("Gaze target"), DialogueSession->CameraRequest.bDialogPOV
			? DialogueSession->CameraRequest.Shot.Origin.ToCompactString() : TEXT("listener"));
	}
}

FString FElysiumEntityWorld::ScriptedSessionSaveBlockReason() const
{
	if (DialogueSession)
	{
		return TEXT("a conversation is open");
	}
	if (HasTrackCamera())
	{
		return TEXT("an authored camera track is active");
	}
	if (HasScriptedCamera())
	{
		return TEXT("an authored legacy camera is active");
	}
	for (const TUniquePtr<FElysiumEntity>& Entity : EntityList)
	{
		if (Entity && !Entity->IsInert())
		{
			if (const TCHAR* Reason = Entity->SaveBlockReason())
			{
				return Reason;
			}
		}
	}
	return FString();
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

// --- 12.5, the dialogue half of lipsync ----------------------------------------------------------
//
// The same join as a map scene's — the line's `.lip`, the speaker's
// `expressions/<stem>_phonemes` table, and the model's phoneme filter. A dialogue line now owns its
// instanced VCD timeline too, so the face reads that scene's authored speak offset and remains in
// lockstep with its body events. The submission timestamp remains the diagnostic fallback for a
// line whose VCD is absent.
static TAutoConsoleVariable<int32> CVarDialogueLipsync(
	TEXT("elysium.DialogueLipsync"),
	1,
	TEXT("A .dlg conversation turn drives the speaking NPC's mouth from the line's .lip phoneme track (1, default) or leaves it at rest (0)."),
	ECVF_Default);

void FElysiumEntityWorld::BeginDialogueLipsync(const FString& DlgSourcePath, int32 LineId)
{
	DialogueLipsync.Reset();
	DialogueLineStart = -1.0;
	DialogueFaceOwner = GetOpenDialogOwner();
	FElysiumEntity* Speaker = Resolve(GetOpenDialogOwner());
	if (FElysiumCombatCharacter* Character = Speaker ? Speaker->AsCombatCharacter() : nullptr)
	{
		Character->SetDispositionTalking(true);
	}
	if (CVarDialogueLipsync.GetValueOnGameThread() == 0)
	{
		return;
	}
	if (Speaker == nullptr)
	{
		return;
	}

	FElysiumLipSyncBinding Binding;
	// The audio path this turn resolves to, with the extension swapped — the one place the two
	// halves of the join have to agree, so it goes through the line service's own rule.
	Binding.Track = ElysiumLip::Load(FElysiumLineService::DialogueLineSource(DlgSourcePath, LineId));
	const FString Stem = FPaths::GetBaseFilename(Speaker->Model).ToLower();
	if (!Stem.IsEmpty())
	{
		Binding.Table = ElysiumExpressions::Load(Stem, TEXT("phonemes"));
	}
	if (!Binding.Table.IsValid())
	{
		Binding.Table = ElysiumExpressions::Load(TEXT("phonemes"), TEXT("phonemes"));
	}
	// This speaker's own blend width, same read the cutscene driver makes. A body with no rig keeps
	// the binding's modal default.
	Speaker->GetPhonemeFilter(Binding.BlendMin, Binding.BlendMax);
	if (!Binding.IsValid())
	{
		return;
	}
	DialogueLineStart = NowSeconds();
	DialogueLipsync = MakeShared<FElysiumLipSyncBinding>(MoveTemp(Binding));
}

void FElysiumEntityWorld::RefreshDialogueLipsync(double Now)
{
	if (!DialogueSession && !DialogueLipsync.IsValid() && DialogueFacialPose.IsEmpty())
	{
		return;
	}

	TMap<FString, float> Next;
	FElysiumEntity* Speaker = Resolve(DialogueFaceOwner.IsSet()
		? DialogueFaceOwner : GetOpenDialogOwner());
	FElysiumCombatCharacter* Character = Speaker ? Speaker->AsCombatCharacter() : nullptr;
	if (Character)
	{
		Character->AccumulateDispositionFacialPose(Next);
	}
	if (DialogueLipsync.IsValid() && DialogueLineStart >= 0.0
		&& CVarDialogueLipsync.GetValueOnGameThread() != 0)
	{
		float LineSeconds = static_cast<float>(Now - DialogueLineStart);
		if (DialogueSession)
		{
			DialogueSession->LineScene.GetSpeechSeconds(LineSeconds);
		}
		if (LineSeconds >= 0.f && LineSeconds <= DialogueLipsync->Track->LatestTime)
		{
			DialogueLipsync->Accumulate(LineSeconds, Next, nullptr);
		}
		else if (LineSeconds > DialogueLipsync->Track->LatestTime && Character)
		{
			Character->SetDispositionTalking(false);
			Next.Reset();
			Character->AccumulateDispositionFacialPose(Next);
		}
	}

	if (Speaker == nullptr)
	{
		// The face went away mid-line. Drop the bookkeeping rather than holding a pose for an entity
		// that no longer exists.
		DialogueFacialPose.Reset();
		DialogueFaceOwner = FElysiumEntityHandle();
		return;
	}

	// Same compose-diff-push as FElysiumChoreoScene::RefreshFacialPose, for one face.
	TArray<FElysiumFlexWrite> Writes;
	bool bChanged = DialogueFacialPose.Num() != Next.Num();
	for (const TPair<FString, float>& Key : Next)
	{
		const float* Was = DialogueFacialPose.Find(Key.Key);
		bChanged |= Was == nullptr || *Was != Key.Value;
		Writes.Add({ Key.Key, Key.Value });
	}
	for (const TPair<FString, float>& Key : DialogueFacialPose)
	{
		if (!Next.Contains(Key.Key))
		{
			bChanged = true;
			Writes.Add({ Key.Key, 0.f });
		}
	}
	if (bChanged && !Writes.IsEmpty())
	{
		Speaker->SetFlexControllers(Writes, nullptr);
	}
	DialogueFacialPose = MoveTemp(Next);
	if (!DialogueSession && !DialogueLipsync.IsValid())
	{
		// The default disposition pose now lives on the character. Forget only this compositor's
		// previous-pose bookkeeping; clearing its keys would erase the baseline we just restored.
		DialogueFacialPose.Reset();
		DialogueFaceOwner = FElysiumEntityHandle();
	}
}

void FElysiumEntityWorld::EndDialogSession(bool bSilent)
{
	const FElysiumEntityHandle Closing = GetOpenDialogOwner();
	FElysiumBodyOwnerToken ClosingBodyOwner;
	if (DialogueSession)
	{
		ClosingBodyOwner = DialogueSession->BodyOwner;
		DialogueSession->LineScene.Stop();
	}
	if (LineService)
	{
		LineService->CancelDialogue(Closing);
	}
	// Drop the phoneme track but NOT the pose: the next RefreshDialogueLipsync writes every key this
	// turn was driving back to zero, exactly as a choreo scene's release pass does. Clearing the pose
	// here instead would leave the last phoneme latched on the face for the rest of the map.
	DialogueLipsync.Reset();
	DialogueLineStart = -1.0;
	if (FElysiumEntity* Speaker = Resolve(Closing))
	{
		if (FElysiumCombatCharacter* Character = Speaker->AsCombatCharacter())
		{
			Character->SetDispositionTalking(false);
		}
	}

	if (DialogueSession && Camera())
	{
		Camera()->ReleaseCamera(DialogueSession->CameraHandle);
	}
	if (FElysiumEntity* OwnerEntity = Resolve(Closing))
	{
		OwnerEntity->EndDialogueBodySession(ClosingBodyOwner, bSilent);
	}
	DialogueSession.Reset();

	if (IElysiumPresenter* P = Presenter())
	{
		P->CloseDialog();
	}

	if (!bSilent && Closing.IsSet())
	{
		// Route EndDialog to exactly the owning NPC (its InputEndDialog clears bInDialog and fires
		// OnDialogEnd -> DialogPostProcess). Queued through chokepoint 2 like every other input, with
		// the owner as `!self` so no name lookup can hit a same-named entity.
		static const FName EndDialogInput(TEXT("EndDialog"));
		EnqueueInput(GSelfTarget, EndDialogInput, FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle(), Closing);
	}
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
	// new deadline forward by the rewind plus 0.01 rather than landing spuriously in the past
	// (`docs/vtmb/game_runtime.md` → "Queue service order, recursion and starvation"). Applied here
	// because this is the one enqueue every producer funnels through; the restore path uses
	// AddRestored and is deliberately outside it.
	const double LastEnqueue = EventQueue.LastEnqueueValue();
	if (Now < LastEnqueue)
	{
		Event.FireTime += (LastEnqueue - Now) + 0.01;
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
	if (T.Equals(GSelfTarget, ESearchCase::IgnoreCase) || T.Equals(GCallerTarget, ESearchCase::IgnoreCase))
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
	for (const TWeakObjectPtr<UElysiumBrushComponent>& Body : Bodies)
	{
		if (UElysiumBrushComponent* B = Body.Get())
		{
			B->DestroyComponent();
		}
	}
	Bodies.Empty();

	// NPC skeletal bodies (B3): components of the map actor, destroyed here for the same reason as
	// Bodies — a world rebuild on a surviving actor (reload) must not leak them.
	for (const TWeakObjectPtr<USkeletalMeshComponent>& Comp : NpcBodies)
	{
		if (USkeletalMeshComponent* C = Comp.Get())
		{
			C->DestroyComponent();
		}
	}
	NpcBodies.Empty();

	// Dynamic-prop bodies (8.3): same reason as NpcBodies — components of the map actor, destroyed
	// here so a world rebuild on a surviving actor (reload) does not leak them.
	for (const TWeakObjectPtr<UPrimitiveComponent>& Comp : PropBodies)
	{
		if (UPrimitiveComponent* C = Comp.Get())
		{
			C->DestroyComponent();
		}
	}
	PropBodies.Empty();

	// phys_hinge constraints (8.4): destroyed with the map, like the bodies they wired.
	for (const TWeakObjectPtr<UPhysicsConstraintComponent>& Comp : Constraints)
	{
		if (UPhysicsConstraintComponent* C = Comp.Get())
		{
			C->DestroyComponent();
		}
	}
	Constraints.Empty();

	EntityList.Empty();
	Baseline.Empty();
	RuntimeDefs.Empty();
	// The tally is keyed by entity index, which means something only inside one map epoch.
	WireTally.Empty();
	Ring = nullptr;
	Sinks.Empty();
}

// --- Verification / test verbs ----------------------------------------------------------
// The P1.4 test harness: prove the world, the two chokepoints, the queue, and the ring buffer
// end-to-end with nothing but the log. Phase 2 replaces these with the Cog entity/queue windows
// and the full `ent_*` verb set (which fire through this same AcceptInput/queue).

static FElysiumEntityWorld* ElysiumCurrentWorld(UWorld* W)
{
	if (!W)
	{
		return nullptr;
	}
	if (const UGameInstance* GI = W->GetGameInstance())
	{
		if (UElysiumMapSubsystem* Maps = GI->GetSubsystem<UElysiumMapSubsystem>())
		{
			if (AElysiumMapActor* Map = Maps->GetCurrentMap())
			{
				return Map->GetEntityWorld();
			}
		}
	}
	return nullptr;
}

static FAutoConsoleCommandWithWorldAndArgs GElysiumWorldCmd(
	TEXT("elysium.world"),
	TEXT("elysium.world — summarize the live entity world (counts, classname histogram, queue, ring, dead wires)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		FElysiumEntityWorld* EW = ElysiumCurrentWorld(World);
		if (!EW)
		{
			UE_LOG(LogElysiumWorld, Warning, TEXT("elysium.world: no live world (load a map first)"));
			return;
		}

		UE_LOG(LogElysiumWorld, Display, TEXT("world: %d entities, epoch %u, now %.3fs"),
			EW->NumEntities(), EW->GetEpoch(), EW->NowSeconds());

		TMap<FString, int32> Histo;
		for (const TUniquePtr<FElysiumEntity>& E : EW->Entities())
		{
			if (E && E->Def)
			{
				++Histo.FindOrAdd(E->Def->Classname);
			}
		}
		Histo.ValueSort([](int32 A, int32 B) { return A > B; });
		int32 Shown = 0;
		for (const TPair<FString, int32>& Pair : Histo)
		{
			UE_LOG(LogElysiumWorld, Display, TEXT("  %5d  %s"), Pair.Value, *Pair.Key);
			if (++Shown >= 15)
			{
				UE_LOG(LogElysiumWorld, Display, TEXT("  ... (%d classnames total)"), Histo.Num());
				break;
			}
		}

		UE_LOG(LogElysiumWorld, Display,
			TEXT("queue: %d pending%s | ring: %d/%d | dead wires: %d unknown targets, %d unknown inputs"),
			EW->Queue().Num(), EW->Queue().IsPaused() ? TEXT(" (paused)") : TEXT(""),
			EW->RingBuffer().Num(), EW->RingBuffer().Capacity(),
			EW->UnknownTargets(), EW->UnknownInputs());
		UE_LOG(LogElysiumWorld, Display, TEXT("brush bodies: %d | touches: %d begin, %d end"),
			EW->NumBrushBodies(), EW->TouchBegins(), EW->TouchEnds());
	}));

static FAutoConsoleCommandWithWorldAndArgs GElysiumWorldIoCmd(
	TEXT("elysium.world.io"),
	TEXT("elysium.world.io [n] — dump the last n I/O history lines from the ring buffer (default 40)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		FElysiumEntityWorld* EW = ElysiumCurrentWorld(World);
		if (!EW)
		{
			UE_LOG(LogElysiumWorld, Warning, TEXT("elysium.world.io: no live world (load a map first)"));
			return;
		}
		const int32 N = Args.Num() >= 1 ? FCString::Atoi(*Args[0]) : 40;
		TArray<FString> Lines;
		EW->RingBuffer().CollectOrdered(N, Lines);
		UE_LOG(LogElysiumWorld, Display, TEXT("I/O history: %d lines (of %d recorded)"),
			Lines.Num(), EW->RingBuffer().Num());
		for (const FString& L : Lines)
		{
			UE_LOG(LogElysiumWorld, Display, TEXT("%s"), *L);
		}
	}));
