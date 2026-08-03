#include "ElysiumMapActor.h"

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumUseIcons.h"
#include "Audio/ElysiumSoundScheme.h"
#include "Map/ElysiumMapCollision.h"
#include "Player/ElysiumCameraShots.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumNpcAnimSubsystem.h"
#include "Visual/ElysiumNpcBody.h"
#include "Visual/ElysiumMapVisuals.h"

#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/FileHelper.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysium, Log, All);
static TAtomic<uint64> GNextElysiumAudioMapEpoch(0);

static TAutoConsoleVariable<float> CVarRainEnhancement(
	TEXT("elysium.RainEnhancement"), 0.0f,
	TEXT("Wetness presentation tuning: 0 is the authored reference; 1 enables the enhanced branch."));
static TAutoConsoleVariable<float> CVarRainRateScale(
	TEXT("elysium.RainRateScale"), 1.0f, TEXT("Enhanced rain emission multiplier."));
static TAutoConsoleVariable<float> CVarRainMist(
	TEXT("elysium.RainMist"), 0.20f, TEXT("Additional enhanced rain mist amount."));
static TAutoConsoleVariable<float> CVarRainWetDarken(
	TEXT("elysium.RainWetDarken"), 0.06f, TEXT("Maximum enhanced full-wet base-color darkening."));
static TAutoConsoleVariable<float> CVarRainWetRoughness(
	TEXT("elysium.RainWetRoughness"), 0.10f, TEXT("Maximum enhanced full-wet roughness reduction."));
static TAutoConsoleVariable<float> CVarRainLightResponse(
	TEXT("elysium.RainLightResponse"), 0.25f, TEXT("Translucent rain response to local lights."));
static TAutoConsoleVariable<float> CVarRainSourceRetain(
	TEXT("elysium.RainSourceRetain"), 1.0f,
	TEXT("Source cubemap weight retained at full wetness enhancement."));
static TAutoConsoleVariable<float> CVarRainWetSpecular(
	TEXT("elysium.RainWetSpecular"), 0.50f,
	TEXT("Enhanced wet-surface dielectric specular level."));
static TAutoConsoleVariable<int32> CVarRainReflectionDebug(
	TEXT("elysium.RainReflectionDebug"), 0,
	TEXT("Wet reflection view: 0 final, 1 raw mask, 2 coarse mask, 3 wet factor, "
		"4 cube sample, 5 source contribution, 6 enhanced coverage."));
static TAutoConsoleVariable<int32> CVarEnvironmentWetnessOverride(
	TEXT("elysium.EnvironmentWetnessOverride"), 0,
	TEXT("1 = present the manual environment wetness value; 0 = present authored entity state."));
static TAutoConsoleVariable<float> CVarEnvironmentWetness(
	TEXT("elysium.EnvironmentWetness"), 1.0f,
	TEXT("Manual 0..1 wetness value used while EnvironmentWetnessOverride is enabled."));
static TAutoConsoleVariable<float> CVarEnvironmentWetnessScale(
	TEXT("elysium.EnvironmentWetnessScale"), 1.0f,
	TEXT("Global multiplier over each material's authored GlobalWetness proxy scale."));

namespace
{
	TOptional<bool> GPendingWeatherTimer;

	AElysiumMapActor* ActiveWeatherMap()
	{
		for (TObjectIterator<AElysiumMapActor> It; It; ++It)
		{
			if (IsValid(*It) && It->IsRuntimeActive())
			{
				return *It;
			}
		}
		return nullptr;
	}

	void FireOrQueueWeatherTimer(const bool bRainOn)
	{
		if (AElysiumMapActor* Map = ActiveWeatherMap())
		{
			Map->FireWeatherTimer(bRainOn);
			return;
		}
		// -ExecCmds can run before the boot map reaches RuntimeActive. Preserve the request and
		// deliver it on the first active weather tick so automated captures still exercise the
		// authored timer and entity I/O chain.
		GPendingWeatherTimer = bRainOn;
	}
}

static FAutoConsoleCommand GElysiumRainOn(
	TEXT("elysium.weather.rain_on"),
	TEXT("Fire sm_hub_1's authored rain_on_timer through the entity I/O queue."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FireOrQueueWeatherTimer(true);
	}));

static FAutoConsoleCommand GElysiumRainOff(
	TEXT("elysium.weather.rain_off"),
	TEXT("Fire sm_hub_1's authored rain_off_timer through the entity I/O queue."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FireOrQueueWeatherTimer(false);
	}));

const TCHAR* ElysiumMapRuntimePhaseName(EElysiumMapRuntimePhase Phase)
{
	switch (Phase)
	{
	case EElysiumMapRuntimePhase::Building:                return TEXT("Building");
	case EElysiumMapRuntimePhase::WaitingForPrerequisites: return TEXT("WaitingForPrerequisites");
	case EElysiumMapRuntimePhase::Activating:              return TEXT("Activating");
	case EElysiumMapRuntimePhase::Active:                  return TEXT("Active");
	case EElysiumMapRuntimePhase::Failed:                  return TEXT("Failed");
	default:                                               return TEXT("Unknown");
	}
}

FString FElysiumMapRuntimePrerequisites::Missing() const
{
	TArray<FString> MissingItems;
	if (!bConstructionComplete) { MissingItems.Add(TEXT("runtime construction")); }
	if (!bEntityWorldReady)     { MissingItems.Add(TEXT("entity substrate")); }
	if (!bAudioCatalogReady)    { MissingItems.Add(TEXT("audio catalog")); }
	if (bCollisionFailed)       { MissingItems.Add(TEXT("world collision failed")); }
	else if (!bCollisionReady)  { MissingItems.Add(TEXT("world collision cooking")); }
	if (bNavigationFailed)      { MissingItems.Add(TEXT("runtime navigation failed")); }
	else if (bNavigationRequired && !bNavigationReady)
	{
		MissingItems.Add(TEXT("runtime navigation building"));
	}

	if (!bMenuBackdrop)
	{
		if (!bSpawnTransformReady)    { MissingItems.Add(TEXT("final spawn transform")); }
		if (!bPlayerEntityReady)      { MissingItems.Add(TEXT("player entity")); }
		if (!bPossessedPawnReady)     { MissingItems.Add(TEXT("possessed player pawn")); }
		if (!bPlayerBodyReady)        { MissingItems.Add(TEXT("player body freeze contract")); }
		if (!bFinalPlacementReady)    { MissingItems.Add(TEXT("final player placement")); }
		if (!bTickPrerequisitesReady) { MissingItems.Add(TEXT("player tick prerequisites")); }
	}
	return FString::Join(MissingItems, TEXT(", "));
}

EElysiumMapReadinessResult FElysiumMapRuntimePrerequisites::Evaluate(
	double WaitSeconds, FString& OutFailure) const
{
	OutFailure.Reset();
	if (bCollisionFailed)
	{
		OutFailure = TEXT("required world collision failed");
		return EElysiumMapReadinessResult::Failed;
	}
	if (bNavigationFailed)
	{
		OutFailure = TEXT("required runtime navigation failed");
		return EElysiumMapReadinessResult::Failed;
	}
	// Once construction is declared complete, these inputs cannot arrive on a later engine tick.
	if (bConstructionComplete && !bEntityWorldReady)
	{
		OutFailure = TEXT("runtime construction produced no entity substrate");
		return EElysiumMapReadinessResult::Failed;
	}
	if (bConstructionComplete && !bAudioCatalogReady && WaitSeconds >= WatchdogSeconds)
	{
		OutFailure = TEXT("audio catalog did not become ready");
		return EElysiumMapReadinessResult::Failed;
	}
	if (bConstructionComplete && !bMenuBackdrop
		&& (!bSpawnTransformReady || !bPlayerEntityReady))
	{
		OutFailure = !bSpawnTransformReady
			? TEXT("gameplay map has no final spawn transform")
			: TEXT("gameplay map did not construct its player entity");
		return EElysiumMapReadinessResult::Failed;
	}

	if (Missing().IsEmpty())
	{
		return EElysiumMapReadinessResult::Ready;
	}
	if (WaitSeconds >= WatchdogSeconds)
	{
		OutFailure = FString::Printf(TEXT("activation watchdog expired; missing: %s"), *Missing());
		return EElysiumMapReadinessResult::Failed;
	}
	return EElysiumMapReadinessResult::Waiting;
}

// ------------------------------------------------------------------------------------------
// S2 — the pre-move tick function (runtime-architecture.md §3, steps 2-3).
// ------------------------------------------------------------------------------------------

void FElysiumPreMoveTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && IsValidChecked(Target) && !Target->IsUnreachable())
	{
		FScopeCycleCounterUObject ActorScope(Target);
		Target->PreMoveTick(DeltaTime);
	}
}

FString FElysiumPreMoveTickFunction::DiagnosticMessage()
{
	return GetFullNameSafe(Target) + TEXT("[AElysiumMapActor::PreMoveTick]");
}

FName FElysiumPreMoveTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("ElysiumMapActorPreMove/%s"), *GetFullNameSafe(Target)));
	}
	return FName(TEXT("ElysiumMapActorPreMove"));
}

// ------------------------------------------------------------------------------------------
// S2 — the gameplay tick function (runtime-architecture.md §3, steps 5-6).
// ------------------------------------------------------------------------------------------

void FElysiumGameplayTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && IsValidChecked(Target) && !Target->IsUnreachable())
	{
		FScopeCycleCounterUObject ActorScope(Target);
		Target->GameplayTick(DeltaTime);
	}
}

FString FElysiumGameplayTickFunction::DiagnosticMessage()
{
	return GetFullNameSafe(Target) + TEXT("[AElysiumMapActor::GameplayTick]");
}

FName FElysiumGameplayTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("ElysiumMapActorGameplay/%s"), *GetFullNameSafe(Target)));
	}
	return FName(TEXT("ElysiumMapActorGameplay"));
}

// ------------------------------------------------------------------------------------------
// S2 — the post-move tick function (runtime-architecture.md §3, step 8).
// ------------------------------------------------------------------------------------------

void FElysiumPostMoveTickFunction::ExecuteTick(float DeltaTime, ELevelTick TickType,
	ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && IsValidChecked(Target) && !Target->IsUnreachable())
	{
		FScopeCycleCounterUObject ActorScope(Target);
		Target->PostMoveTick(DeltaTime);
	}
}

FString FElysiumPostMoveTickFunction::DiagnosticMessage()
{
	return GetFullNameSafe(Target) + TEXT("[AElysiumMapActor::PostMoveTick]");
}

FName FElysiumPostMoveTickFunction::DiagnosticContext(bool bDetailed)
{
	if (bDetailed)
	{
		return FName(*FString::Printf(TEXT("ElysiumMapActorPostMove/%s"), *GetFullNameSafe(Target)));
	}
	return FName(TEXT("ElysiumMapActorPostMove"));
}

AElysiumMapActor::AElysiumMapActor()
{
	// S2 — the frame order is declared with tick groups and prerequisites, not left to registration
	// order. Pre-move advances the clock/player think; the actor tick is the map-owned floor's
	// movement-base barrier; gameplay runs after player and NPC movement; post-move runs after
	// physics. Before activation the first three may poll through a hold; activation restores the
	// normal pause-stops-gameplay rule (§4).
	PreMoveTickFunction.bCanEverTick = true;
	PreMoveTickFunction.bStartWithTickEnabled = true;
	PreMoveTickFunction.TickGroup = TG_PrePhysics;
	// The lifecycle-bearing pre-physics functions must continue through the activation barrier if a
	// persistent dev hold survived travel. ActivateRuntime restores normal pause behaviour atomically.
	PreMoveTickFunction.bTickEvenWhenPaused = true;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	PrimaryActorTick.bTickEvenWhenPaused = true;

	GameplayTickFunction.bCanEverTick = true;
	GameplayTickFunction.bStartWithTickEnabled = true;
	GameplayTickFunction.TickGroup = TG_PrePhysics;
	GameplayTickFunction.bTickEvenWhenPaused = true;

	PostMoveTickFunction.bCanEverTick = true;
	PostMoveTickFunction.bStartWithTickEnabled = true;
	PostMoveTickFunction.TickGroup = TG_PostPhysics;
	PostMoveTickFunction.bTickEvenWhenPaused = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// The three halves this actor is not. Each builds its own child components at load, so a map
	// with no sky builds no backdrop and a map with no `.hulls` builds no collider. The sun, sky
	// light and height fog are actors in the baked level, adopted by the visuals — this actor owns
	// no lighting, no geometry and no material of its own.
	Visuals = CreateDefaultSubobject<UElysiumMapVisuals>(TEXT("Visuals"));
	Visuals->SetupAttachment(SceneRoot);
	Collision = CreateDefaultSubobject<UElysiumMapCollision>(TEXT("Collision"));
	Collision->SetupAttachment(SceneRoot);
	Bodies = CreateDefaultSubobject<UElysiumEntityBodies>(TEXT("Bodies"));
}

void AElysiumMapActor::RegisterActorTickFunctions(bool bRegister)
{
	Super::RegisterActorTickFunctions(bRegister);

	if (bRegister)
	{
		if (PreMoveTickFunction.bCanEverTick)
		{
			PreMoveTickFunction.Target = this;
			PreMoveTickFunction.SetTickFunctionEnable(PreMoveTickFunction.bStartWithTickEnabled);
			PreMoveTickFunction.RegisterTickFunction(GetLevel());
			// Pre-move before the map-owned floor barrier, unconditionally. The pawn's movement
			// component is wired between them when one exists (EnsureTickPrerequisites); gameplay
			// then depends on this barrier even on a backdrop/headless world with no pawn.
			PrimaryActorTick.AddPrerequisite(this, PreMoveTickFunction);
		}
		if (GameplayTickFunction.bCanEverTick)
		{
			GameplayTickFunction.Target = this;
			GameplayTickFunction.SetTickFunctionEnable(GameplayTickFunction.bStartWithTickEnabled);
			GameplayTickFunction.RegisterTickFunction(GetLevel());
			// The actor tick is the movement-base barrier for characters standing on this map's
			// collision. GameFrame is a separate dependent node so NPC movement can sit between them.
			GameplayTickFunction.AddPrerequisite(this, PrimaryActorTick);
		}
		if (PostMoveTickFunction.bCanEverTick)
		{
			PostMoveTickFunction.Target = this;
			PostMoveTickFunction.SetTickFunctionEnable(PostMoveTickFunction.bStartWithTickEnabled);
			PostMoveTickFunction.RegisterTickFunction(GetLevel());
			// The tick groups already separate these two passes; the prerequisite says so in the
			// graph as well, so the dependency survives anyone re-grouping either end.
			PostMoveTickFunction.AddPrerequisite(this, GameplayTickFunction);
		}
	}
	else
	{
		if (PreMoveTickFunction.IsTickFunctionRegistered())
		{
			PreMoveTickFunction.UnRegisterTickFunction();
		}
		if (GameplayTickFunction.IsTickFunctionRegistered())
		{
			GameplayTickFunction.UnRegisterTickFunction();
		}
		if (PostMoveTickFunction.IsTickFunctionRegistered())
		{
			PostMoveTickFunction.UnRegisterTickFunction();
		}
	}
}

void AElysiumMapActor::EnsureTickPrerequisites()
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	// Step 1 -> steps 2-3: the frame's input sample lands before the clock advances, because the
	// command that sample builds is what the move about to follow is timed against.
	if (PrereqController.Get() != PC)
	{
		PreMoveTickFunction.AddPrerequisite(PC, PC->PrimaryActorTick);
		PrereqController = PC;
	}

	// Steps 2-3 -> step 4 -> steps 5-6 (RE21). Retail never runs the player move inside `GameFrame`:
	// the engine drains the client's `clc_move` message and runs the whole ProcessUsercmds ->
	// CPlayerMove::RunCommand chain there, so the pawn has already moved by the time the first think
	// or queued event runs. The tunnelling hazard the old order guarded against is absorbed on the
	// mover's side instead — a mover sweeps its own body and resolves what it hits.
	UPawnMovementComponent* Move = PC->GetPawn() ? PC->GetPawn()->GetMovementComponent() : nullptr;
	if (Move && PrereqMovement.Get() != Move)
	{
		// A replaced pawn (the elysium.SourceMovement A/B swaps the whole body) leaves the outgoing
		// component's edge behind, and the gameplay pass would then wait on a tick function that is
		// never going to run again. Drop it before wiring the new one.
		if (UPawnMovementComponent* Previous = PrereqMovement.Get())
		{
			PrimaryActorTick.RemovePrerequisite(Previous, Previous->PrimaryComponentTick);
		}
		Move->PrimaryComponentTick.AddPrerequisite(this, PreMoveTickFunction);
		PrimaryActorTick.AddPrerequisite(Move, Move->PrimaryComponentTick);
		PrereqMovement = Move;

		// 11.4 — tell the body which entity it embodies. Done here rather than at SpawnPlayer
		// because a fresh world has no pawn yet when the map builds, and this already runs each
		// gameplay tick until the pawn appears (and again if it is replaced).
		if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(PC->GetPawn()))
		{
			Body->SetPlayerEntity(EntityWorld ? EntityWorld->PlayerHandle() : FElysiumEntityHandle::Invalid());
		}
	}
}

void AElysiumMapActor::BeginPlay()
{
	Super::BeginPlay();
	AudioMapEpoch = ++GNextElysiumAudioMapEpoch;
	RuntimePhase = EElysiumMapRuntimePhase::Building;
	RuntimeWaitStartSeconds = FPlatformTime::Seconds();

	// Engine pause and time dilation are per-world; the clock is not. Re-stamp them onto this
	// world so a hold or a time scale set before travel survives the map change (S1).
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			GameState->TimeControl().ApplyToWorld();
		}
	}
	EnsureTickPrerequisites();

	// The look-tuning cvar callbacks are bound by UElysiumMapVisuals::BeginPlay, which Super has
	// already run — so a knob turned during the load still reaches the surfaces it built.
	LoadMap();
	bRuntimeConstructionComplete = true;
	RuntimePhase = EElysiumMapRuntimePhase::WaitingForPrerequisites;
	UE_LOG(LogElysium, Log, TEXT("map runtime %s: %s (collision %s)"), *MapName,
		ElysiumMapRuntimePhaseName(RuntimePhase),
		ElysiumCollisionBuildStateName(Collision->GetBuildState()));
}

void AElysiumMapActor::LoadMap()
{
	const double Start = FPlatformTime::Seconds();
	if (NavigationBounds)
	{
		NavigationBounds->Destroy();
		NavigationBounds = nullptr;
	}
	bNavigationBuildRequested = false;
	bNavigationBuildFailed = false;

	// Per-phase timing for the Maps Cog window: stamp closes the running phase and opens the next.
	LoadPhases.Reset();
	double PhaseStart = Start;
	auto Phase = [this, &PhaseStart](const TCHAR* Name)
	{
		const double Now = FPlatformTime::Seconds();
		LoadPhases.Add({ Name, (Now - PhaseStart) * 1000.0 });
		PhaseStart = Now;
	};

	LoadedMap = MapName;
	Bodies->SetMap(MapName);

	// B7 — the 3D-skybox miniature's placement transform (`<map>.sky`), read first because three
	// later steps need it: the light rig scales a miniature source's reach by it, the `.ents`
	// parser carries sky-scope entities through it, and a miniature body takes its mesh scale
	// from it. The identity (scale 1) on the 65 maps with no `sky_camera`.
	SkyDef = FElysiumSkyDef();
	FElysiumSkyDef::Parse(FElysiumContentPaths::MapSky(MapName), SkyDef);

	// P1.7 — label the map actor and drop it in an Elysium Outliner folder, so the PIE World
	// Outliner reads as a live scene browser (debug-tooling.md Layer 0).
#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("Map:%s"), *MapName));
	SetFolderPath(TEXT("Elysium"));
#endif

	// The look is already here — this actor was spawned into the map's baked level. The visuals
	// take hold of its actors, hand the light rig its sources, and stand the material overrides up.
	const int32 Adopted = Visuals->AdoptBakedLevel(MapName, SkyDef);
	Phase(TEXT("Adopt baked level"));

	// 8.6 — a menu backdrop builds the map in full, entity substrate included: the NPCs standing and
	// idling in frame *are* entities, so a look-only build has no one in it (owner call, see
	// What a backdrop skips is only the player's placement — it seats no pawn.
	UElysiumMapSubsystem* MapSubsystem =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	bMenuBackdrop = MapSubsystem && MapSubsystem->IsMenuBackdrop();

	// The walkable surface. Baked world geometry carries no gameplay collision, so the brush
	// sidecars are the only world collider: .hulls convex (which carries the invisible PLAYERCLIP
	// volumes and drops geometry the designer clipped off) plus the .dispcol displacement trimesh.
	Collision->Build(MapName);
	Phase(TEXT("Collision"));

	Visuals->BuildRopes(MapName);
	Phase(TEXT("Ropes"));

	// Sky cubemap + backdrop, the sky light's IBL off the same cube, and the map's PPV knobs.
	Visuals->ApplyEnvironment(MapName);
	Visuals->ApplyPostProcessKnobs();
	Phase(TEXT("Environment"));

	UE_LOG(LogElysium, Log,
		TEXT("baked '%s': %d actors (%d world, %d sky, %d props, %d decals), %d lights, %d hulls"),
		*MapName, Adopted, Visuals->WorldSurfaceCount, Visuals->SkySurfaceCount,
		Visuals->PropInstanceCount, Visuals->DecalCount, Visuals->WorldLightCount,
		Collision->HullCount);

	if (!bMenuBackdrop && ReadSpawn(PendingSpawnLoc, PendingSpawnYaw))
	{
		bSpawnPending = true;
	}

	if (bMenuBackdrop)
	{
		UE_LOG(LogElysium, Log, TEXT("menu backdrop '%s': full build, no player placement"), *MapName);
	}

	// Track-B entity substrate (P1.4): parse `.ents`, build the live world, run the spawn pass.
	// Map-load ignition (OnMapLoad) is the logic_auto class's own first-think (P1.6), not a
	// separate pass. The world ticks from AElysiumMapActor::Tick.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			FElysiumEntityDefs EntDefs;
			if (FElysiumEntityDefs::Parse(FElysiumContentPaths::MapEnts(MapName), EntDefs,
				SkyDef.Scale, SkyDef.OriginCm))
			{
				EntityCount = EntDefs.Num();

				// P9 9.3 — import this map's `worldspawn.levelscript` module before anything can
				// evaluate against it. VtMB's own load order: the level script's top-level code
				// (constants like cCelerity, `from vamputil import *`, the On* defs) runs first,
				// then entities spawn and fire their field-6 payloads into that namespace.
				GameState->LoadLevelScript(EntDefs.LevelScriptModule());

				// The scheme manager must exist before the spawn pass: a start_enabled
				// ambient_soundscheme fades its scheme in from its own Spawn() (P6.3), and it
				// reaches it through this actor's IElysiumAudio.
				SchemeManager = MakePimpl<FElysiumSoundSchemeManager>();
				SchemeManager->SetMapEpoch(AudioMapEpoch);

				// 11.2 — hand the substrate its outbound seam. This actor is three of the four
				// services; the fourth is the world-scoped presentation subsystem (11.8), which is
				// null only where there is no publisher at all (an editor preview world, a
				// Substrate-tier world with no engine behind it).
				FElysiumWorldServices Services;
				Services.Embodiment = this;
				Services.Audio      = this;
				Services.Travel     = this;
				Services.Presenter  = UElysiumPresentationSubsystem::Get(GetWorld());
				// The weather seam is live for the material-wetness slice. env_particle state also
				// crosses the same one-system boundary, but ApplyEmitter deliberately retains it as
				// data only: Niagara stays disconnected until its retail semantics are resolved.
				Services.Weather    = this;
				EntityWorld = MakePimpl<FElysiumEntityWorld>(this, GameState, Services);
				EntityWorld->Load(MoveTemp(EntDefs));
				BrushBodyCount = EntityWorld->NumBrushBodies();

				// 11.4 (S3) — the player is an entity, created here because the map is where a
				// player exists at all: a backdrop seats no pawn, so it gets no player entity and
				// everything that looks for one handles its absence. Created after the spawn pass
				// and before the first tick, so `!player` resolves for the map's own logic_auto
				// ignition; it hydrates from the session record the previous map dehydrated into.
				if (!bMenuBackdrop)
				{
					EntityWorld->SpawnPlayer();

					// 11.9 — if the run has been here before (this session, or a loaded save), the
					// map is not new: apply the frozen snapshot over the freshly-built world
					// (`docs/architecture/save-architecture.md` §5). After SpawnPlayer, so the player exists for the
					// records that reference it, and before the first Tick, so nothing has run yet.
					//
					// A dev fresh-state entry (`elysium.newgame_ttd`) drops the snapshot here rather
					// than at the command, because the travel it issued tore this map down on the way
					// out and froze it again.
					UElysiumMapSubsystem* MapsForState =
						GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
					if (MapsForState && MapsForState->ConsumeFreshMapState())
					{
						GameState->ClearMapSnapshot(MapName);
						UE_LOG(LogElysium, Log, TEXT("fresh map state: %s forgotten"), *MapName);
					}
					else if (const FElysiumMapSnapshot* Snapshot = GameState->FindMapSnapshot(MapName))
					{
						EntityWorld->ApplySnapshot(*Snapshot);
					}
				}
			}
			else
			{
				UE_LOG(LogElysium, Log, TEXT("no %s.ents — entity world not built"), *MapName);
			}
		}
	}

	// P4.6 — a landmark transition places the player against the destination info_landmark instead of
	// info_player_start. Runs after the entity world is built (the landmark is one of its entities),
	// and not at all on a backdrop, which has no entity world and seats no player.
	if (!bMenuBackdrop)
	{
		ResolveLandmarkSpawn();
		ResolveRestorePlacement();
	}

	Phase(TEXT("Entities"));

	const double TotalMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	LoadPhases.Add({ TEXT("Total"), TotalMs });
	UE_LOG(LogElysium, Log, TEXT("loaded %s in %.2fs"), *MapName, TotalMs / 1000.0);
}

float AElysiumMapActor::BodyScaleFor(const FElysiumEntityDef& Def) const
{
	return Def.bSky ? SkyDef.Scale : 1.f;
}

// ============================================================================================
// The world services (11.2) — the substrate's engine side. Everything here is a forward: the
// body factory, the player's pawn, the GI-scoped audio subsystem, this map's scheme manager, the
// map subsystem. Nothing under FElysiumEntityWorld knows any of those exist.
// ============================================================================================

USkeletalMeshComponent* AElysiumMapActor::BuildNpcVisual(const FString& Stem, const FVector& Location,
	const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant)
{
	return Bodies->BuildNpcVisual(Stem, Location, Rotation, UniformScale, Disposition, IdleVariant);
}

IElysiumNpcMotor* AElysiumMapActor::BuildNpcMotor(USkeletalMeshComponent* Body,
	const FVector& FeetOrigin, float YawDegrees)
{
	if (!Body || bMenuBackdrop || !GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.OverrideLevel = GetLevel();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AElysiumNpcBody* Motor = GetWorld()->SpawnActor<AElysiumNpcBody>(
		AElysiumNpcBody::StaticClass(), FTransform::Identity, Params);
	if (!Motor)
	{
		UE_LOG(LogElysium, Warning, TEXT("failed to spawn native NPC body at %s"), *FeetOrigin.ToString());
		return nullptr;
	}

	Motor->InitializeAtFeet(FeetOrigin, YawDegrees);
	Motor->SetRuntimeReady(RuntimePhase == EElysiumMapRuntimePhase::Active);
	NpcMotors.Add(Motor);
	Body->AttachToComponent(Motor->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	if (UCharacterMovementComponent* Movement = Motor->GetCharacterMovement())
	{
		// CharacterMovement adds the map actor's primary tick as a prerequisite when this character
		// stands on map-owned collision. The separate gameplay tick can safely form the forward edge;
		// using PrimaryActorTick here would close a cycle through that automatic movement-base edge.
		GameplayTickFunction.AddPrerequisite(Movement, Movement->PrimaryComponentTick);
	}
	return Motor;
}

void AElysiumMapActor::DestroyNpcMotor(IElysiumNpcMotor* Motor)
{
	if (bMotorsRetired)
	{
		// EndPlay already released them and the engine owns the actors now. This is the teardown
		// call: ~AElysiumMapActor destroys the entity world, every FElysiumNpc destructor on the way
		// out calls here, and by then the motor's UObject index has been freed — at which point even
		// IsValid() asserts, because it reaches FUObjectArray::IndexToObject with index -1.
		return;
	}
	AElysiumNpcBody* Body = static_cast<AElysiumNpcBody*>(Motor);
	if (!Body)
	{
		return;
	}
	if (!IsValid(Body))
	{
		NpcMotors.RemoveSingleSwap(Body);
		return; // world teardown already owns the pending-kill actor
	}
	if (UCharacterMovementComponent* Movement = Body->GetCharacterMovement())
	{
		GameplayTickFunction.RemovePrerequisite(Movement, Movement->PrimaryComponentTick);
	}
	NpcMotors.RemoveSingleSwap(Body);
	Body->Destroy();
}

bool AElysiumMapActor::RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Disposition, int32 IdleVariant)
{
	return Bodies->RefreshNpcIdle(Body, Stem, Disposition, IdleVariant);
}

bool AElysiumMapActor::PlayNpcActivity(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Activity, int32 Variant, bool bLoop, float* OutSeconds)
{
	return Bodies->PlayNpcActivity(Body, Stem, Activity, Variant, bLoop, OutSeconds);
}

bool AElysiumMapActor::PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	return Bodies->PlayNpcClip(Body, Stem, ClipName, bLoop, OutSeconds);
}

bool AElysiumMapActor::PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName,
	bool bLoop, float* OutSeconds)
{
	// The anim-set model + the actor's bonerename root name a bank the offline split produced.
	UGameInstance* GI = GetGameInstance();
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
	if (Anims == nullptr)
	{
		return false;
	}
	const FString Bank = Anims->GetIndex().CinematicBank(AnimSetModel, BoneRoot);
	if (Bank.IsEmpty())
	{
		return false;
	}
	return Bodies->PlayCinematicClip(Body, Stem, Bank, ClipName, bLoop, OutSeconds);
}

bool AElysiumMapActor::SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds)
{
	return Bodies && Bodies->SeekCinematicClip(Body, PositionSeconds);
}

void AElysiumMapActor::StopCinematicClip(USkeletalMeshComponent* Body)
{
	if (Bodies)
	{
		Bodies->StopCinematicClip(Body);
	}
}

int32 AElysiumMapActor::SetFlexControllers(USkeletalMeshComponent* Body,
	TArrayView<const FElysiumFlexWrite> Writes, TArray<FString>* OutMissing)
{
	return Bodies ? Bodies->SetFlexControllers(Body, Writes, OutMissing) : INDEX_NONE;
}

bool AElysiumMapActor::SetMouthOpen(USkeletalMeshComponent* Body, float Open)
{
	return Bodies ? Bodies->SetMouthOpen(Body, Open) : false;
}

FString AElysiumMapActor::AnimatedPropStemForModel(const FString& ModelPath) const
{
	return Bodies ? Bodies->AnimatedPropStemForModel(ModelPath) : FString();
}

USkeletalMeshComponent* AElysiumMapActor::BuildAnimatedPropVisual(const FString& Stem,
	const FVector& Location, const FQuat& Rotation, float UniformScale)
{
	return Bodies ? Bodies->BuildAnimatedPropVisual(Stem, Location, Rotation, UniformScale) : nullptr;
}

bool AElysiumMapActor::PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	return Bodies && Bodies->PlayAnimatedPropClip(Body, Stem, ClipName, bLoop, OutSeconds);
}

void AElysiumMapActor::ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp,
	const FString& StaticStem, int32 Family)
{
	if (Bodies)
	{
		Bodies->ApplyAnimatedPropSkin(Comp, StaticStem, Family);
	}
}

UStaticMeshComponent* AElysiumMapActor::BuildBrushVisual(const FString& Stem,
	USceneComponent* ParentBody, float UniformScale, bool bSky)
{
	UStaticMeshComponent* Comp = Bodies
		? Bodies->BuildBrushVisual(Stem, ParentBody, UniformScale, bSky) : nullptr;
	if (Comp && Visuals)
	{
		Visuals->RegisterRuntimeBrush(Comp, bSky);
	}
	return Comp;
}

UStaticMeshComponent* AElysiumMapActor::BuildPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	return Bodies->BuildPropVisual(Stem, Location, Rotation, UniformScale);
}

UStaticMeshComponent* AElysiumMapActor::BuildPhysPropVisual(const FString& Stem, const FVector& Location,
	const FQuat& Rotation, float UniformScale)
{
	return Bodies->BuildPhysPropVisual(Stem, Location, Rotation, UniformScale);
}

void AElysiumMapActor::ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family)
{
	Bodies->ApplyPropSkin(Comp, Stem, Family);
}

USkeletalMeshComponent* AElysiumMapActor::BuildPlayerVisual(const FString& Stem,
	const FString& Disposition, int32 IdleVariant)
{
	APawn* Pawn = ResolvePlayerPawn();
	IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
	if (!Body || !Bodies || Stem.IsEmpty())
	{
		return nullptr;
	}
	ClearPlayerVisual();

	FVector Feet = Pawn->GetActorLocation();
	Feet.Z -= Body->GetBodyHalfHeight();
	USkeletalMeshComponent* Visual = Bodies->BuildNpcVisual(Stem, Feet,
		ElysiumSkeletalBasis::FromUnrealYaw(Pawn->GetActorRotation().Yaw),
		/*UniformScale*/ 1.0f, Disposition, IdleVariant, /*bPlayerMaterial=*/true);
	if (!Visual || !Pawn->GetRootComponent())
	{
		return Visual;
	}

	// The glTF model's root is authored at Source absorigin (the feet), while both movement pawns
	// are centred. Attach after loading through the shared skeletal cache, then offset one body
	// half-height so movement, crouching and controller yaw carry the surface automatically.
	Visual->AttachToComponent(Pawn->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	Visual->SetRelativeLocation(FVector(0.0f, 0.0f, -Body->GetBodyHalfHeight()));
	Visual->SetRelativeRotation(ElysiumSkeletalBasis::RelativeToPawn());
	Body->SetPlayerVisual(Visual);
	return Visual;
}

void AElysiumMapActor::ClearPlayerVisual()
{
	APawn* Pawn = ResolvePlayerPawn();
	IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
	if (!Body)
	{
		return;
	}
	if (USkeletalMeshComponent* Visual = Body->GetPlayerVisual())
	{
		Body->SetPlayerVisual(nullptr);
		Visual->DestroyComponent();
	}
}

APawn* AElysiumMapActor::ResolvePlayerPawn() const
{
	const UWorld* W = GetWorld();
	const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

bool AElysiumMapActor::GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* Pawn = ResolvePlayerPawn();
	if (!Pawn)
	{
		return false;
	}
	if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->GetPlayerViewPoint(OutLocation, OutRotation);
	}
	else
	{
		// No controller (a detached/possessed-later pawn): the body's own transform is the best
		// available eye, which is what the trigger_look path has always fallen back to.
		OutLocation = Pawn->GetActorLocation();
		OutRotation = Pawn->GetActorRotation();
	}
	return true;
}

bool AElysiumMapActor::GetPlayerOrigin(FVector& OutLocation, float& OutYaw) const
{
	const APawn* Pawn = ResolvePlayerPawn();
	if (!Pawn)
	{
		return false;
	}
	OutLocation = Pawn->GetActorLocation();
	const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	OutYaw = PC ? (float)PC->GetControlRotation().Yaw : (float)Pawn->GetActorRotation().Yaw;
	return true;
}

void AElysiumMapActor::TeleportPlayer(const FVector& FeetOrigin, float Yaw)
{
	APawn* Pawn = ResolvePlayerPawn();
	if (!Pawn)
	{
		return;
	}
	// Source places the entity's absorigin (feet); both Unreal bodies are centred, so lift by the
	// body's half-height to seat the player on the destination rather than in the floor. This is
	// the body's own geometry, which is why the compensation lives here and not in point_teleport,
	// and why the number comes from the body rather than from an assumed shape.
	FVector Dest = FeetOrigin;
	if (const IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn))
	{
		Dest.Z += Body->GetBodyHalfHeight();
	}
	Pawn->SetActorLocation(Dest, false, nullptr, ETeleportType::TeleportPhysics);
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
	}
	ReconcilePlayerBrushTouches(Pawn);
}

void AElysiumMapActor::ReconcilePlayerBrushTouches(APawn* Pawn)
{
	if (!Pawn || !EntityWorld)
	{
		return;
	}

	// AActor::SetActorLocation returns early for a zero transform delta. That is observable on map
	// entry because the baked APlayerStart and the runtime .spawn placement name the same place:
	// the pawn can already be inside a newly registered trigger, with no movement edge left to wake
	// it. Dirty UE's overlap-skip cache and perform the query now that both player and entity world
	// are live.
	if (USceneComponent* Root = Pawn->GetRootComponent())
	{
		Root->ClearSkipUpdateOverlaps();
	}
	Pawn->UpdateOverlaps(/*bDoNotifies*/ true);

	TInlineComponentArray<UElysiumBrushComponent*> BrushComponents(this);
	for (UElysiumBrushComponent* Brush : BrushComponents)
	{
		if (Brush && Brush->IsOverlappingActor(Pawn))
		{
			EntityWorld->RouteBrushTouch(
				Brush->GetOwningEntity(), EntityWorld->PlayerHandle(), /*bBegin*/ true);
		}
	}
}

void AElysiumMapActor::DamagePlayer(float Amount)
{
	APawn* Pawn = ResolvePlayerPawn();
	if (Pawn && Amount > 0.f)
	{
		// This actor is the damage causer: every entity body is one of its components, so it is
		// what the old per-call `Body->GetOwner()` resolved to anyway.
		UGameplayStatics::ApplyDamage(Pawn, Amount, nullptr, const_cast<AElysiumMapActor*>(this),
			UDamageType::StaticClass());
	}
}

FElysiumEntityHandle AElysiumMapActor::TraceUseCursor(const FVector& Start, const FVector& End) const
{
	UWorld* W = GetWorld();
	if (!W)
	{
		return FElysiumEntityHandle::Invalid();
	}

	// A single blocking trace naturally handles occlusion: a wall (or any solid) closer than the
	// button ends the ray. The dedicated +use channel (ELYSIUM_USE_CHANNEL, default-Block) keeps
	// world + solid bodies as occluders while staying isolated from ECC_Visibility;
	// func_button/func_door bodies are Solid (BlockAll), so they block it.
	FCollisionQueryParams Params(FName(TEXT("ElysiumUseCursor")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(ResolvePlayerPawn());
	FHitResult H;
	if (W->LineTraceSingleByChannel(H, Start, End, ELYSIUM_USE_CHANNEL, Params))
	{
		if (const UElysiumBrushComponent* B = Cast<UElysiumBrushComponent>(H.GetComponent()))
		{
			return B->GetOwningEntity();
		}
	}
	return FElysiumEntityHandle::Invalid();
}

UElysiumCameraComponent* AElysiumMapActor::PlayerCamera() const
{
	const APawn* Pawn = ResolvePlayerPawn();
	const IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn);
	return Body ? Body->GetCameraComponent() : nullptr;
}

int32 AElysiumMapActor::PushCameraShot(const FString& ShotFile, const FElysiumEntityHandle& Subject)
{
	if (!CameraDirector)
	{
		CameraDirector = MakePimpl<FElysiumCameraDirector>();
	}
	return CameraDirector->Push(EntityWorld.Get(), PlayerCamera(), ShotFile, Subject);
}

int32 AElysiumMapActor::PushCameraShotValue(const FElysiumCameraShot& Shot)
{
	if (!CameraDirector)
	{
		CameraDirector = MakePimpl<FElysiumCameraDirector>();
	}
	return CameraDirector->PushValue(PlayerCamera(), Shot);
}

bool AElysiumMapActor::UpdateCameraShotValue(int32 ShotId, const FElysiumCameraShot& Shot)
{
	return CameraDirector
		? CameraDirector->UpdateValue(PlayerCamera(), ShotId, Shot)
		: false;
}

bool AElysiumMapActor::PopCameraShot(int32 ShotId, float BlendOutSeconds)
{
	return CameraDirector ? CameraDirector->Pop(PlayerCamera(), ShotId, BlendOutSeconds) : false;
}

FElysiumVoiceHandle AElysiumMapActor::Submit(FElysiumAudioRequest Request)
{
	Request.Owner.MapEpoch = AudioMapEpoch;
	UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	return Audio ? Audio->Submit(Request) : FElysiumVoiceHandle::Invalid();
}

void AElysiumMapActor::Prefetch(const FElysiumAudioSource& Source)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->Prefetch(Source);
	}
}

void AElysiumMapActor::PauseVoice(FElysiumVoiceHandle Handle, bool bPaused)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->Pause(Handle, bPaused);
	}
}

void AElysiumMapActor::SeekVoice(FElysiumVoiceHandle Handle, float MediaOffsetSeconds)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->Seek(Handle, MediaOffsetSeconds);
	}
}

void AElysiumMapActor::SetVoicePitch(FElysiumVoiceHandle Handle, float Pitch)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->SetPitch(Handle, Pitch);
	}
}

void AElysiumMapActor::CancelAudioOwner(FElysiumAudioOwner AudioOwner, float FadeSeconds)
{
	AudioOwner.MapEpoch = AudioMapEpoch;
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->CancelOwner(AudioOwner, FadeSeconds);
	}
}

FElysiumAudioVoiceHandle AElysiumMapActor::PlayVoice(const FString& Rel, const FElysiumPlayParams& Params)
{
	FElysiumAudioRequest Request;
	Request.Source = FElysiumAudioSource::Path(Rel);
	Request.Owner.Kind = EElysiumAudioOwnerKind::GameplaySystem;
	Request.Owner.StableId = TEXT("legacy.map");
	Request.Gain = Params.Volume;
	Request.Pitch = Params.Pitch;
	Request.bLooping = Params.bLooping;
	Request.Placement.bSpatialized = Params.b3D;
	Request.Placement.Location = Params.Location;
	Request.Placement.AttachTo = Params.AttachTo;
	Request.AttenuationRadiusCm = Params.AttenuationRadiusCm;
	Request.FadeInSeconds = Params.FadeInSeconds;
	Request.StartOffsetSeconds = Params.StartTimeSeconds;
	return Submit(MoveTemp(Request));
}

void AElysiumMapActor::StopVoice(FElysiumAudioVoiceHandle Handle, float FadeSeconds)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->StopVoice(Handle, FadeSeconds);
	}
}

void AElysiumMapActor::SetVoiceVolume(FElysiumAudioVoiceHandle Handle, float Volume)
{
	if (UElysiumAudioSubsystem* Audio = GetAudioSubsystem())
	{
		Audio->SetVoiceVolume(Handle, Volume);
	}
}

bool AElysiumMapActor::IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const
{
	const UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	return Audio && Audio->IsVoicePlaying(Handle);
}

void AElysiumMapActor::FadeInScheme(const FString& SchemeRel, const FVector& Anchor, float FadeSeconds)
{
	if (RuntimePhase != EElysiumMapRuntimePhase::Active)
	{
		if (SchemeManager)
		{
			SchemeManager->PrimeScheme(GetAudioSubsystem(), SchemeRel);
		}
		bHasDeferredSchemeFadeIn = true;
		DeferredSchemeRel = SchemeRel;
		DeferredSchemeAnchor = Anchor;
		DeferredSchemeFadeSeconds = FadeSeconds;
		return;
	}
	if (SchemeManager)
	{
		SchemeManager->FadeInScheme(GetAudioSubsystem(), SchemeRel, Anchor, FadeSeconds);
	}
}

void AElysiumMapActor::FadeOutScheme(const FString& SchemeRel, float FadeSeconds)
{
	if (bHasDeferredSchemeFadeIn && DeferredSchemeRel == SchemeRel)
	{
		bHasDeferredSchemeFadeIn = false;
		DeferredSchemeRel.Reset();
	}
	if (SchemeManager)
	{
		SchemeManager->FadeOutScheme(GetAudioSubsystem(), SchemeRel, FadeSeconds);
	}
}

FString AElysiumMapActor::ActiveSchemeRel() const
{
	return SchemeManager ? SchemeManager->ActiveSchemeRel() : FString();
}

float AElysiumMapActor::OutputLeadSeconds() const
{
	const UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	return Audio ? Audio->OutputLeadSeconds() : ElysiumAudioLatency::FallbackLeadSeconds;
}

void AElysiumMapActor::RequestLandmarkTravel(const FString& Map, const FString& Landmark,
	const FVector& Offset, float Yaw)
{
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		Maps->RequestLandmarkTravel(Map, Landmark, Offset, Yaw);
	}
}

void AElysiumMapActor::ChangeMap(const FString& Map)
{
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		Maps->Travel(Map);
	}
}

UElysiumAudioSubsystem* AElysiumMapActor::GetAudioSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UElysiumAudioSubsystem>() : nullptr;
}

UElysiumMapSubsystem* AElysiumMapActor::GetMapSubsystem() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
}

bool AElysiumMapActor::ReadSpawn(FVector& OutLocation, float& OutYaw) const
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *FElysiumContentPaths::MapSpawn(MapName)))
	{
		return false;
	}

	bool bHasOrigin = false;
	float YawSrc = 0.f;
	FVector Origin = FVector::ZeroVector;

	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() == 4 && Tok[0] == TEXT("origin"))
		{
			Origin = FVector(FCString::Atod(*Tok[1]), FCString::Atod(*Tok[2]), FCString::Atod(*Tok[3]));
			bHasOrigin = true;
		}
		else if (Tok.Num() == 2 && Tok[0] == TEXT("yaw"))
		{
			YawSrc = FCString::Atof(*Tok[1]);
		}
	}

	if (!bHasOrigin)
	{
		return false;
	}

	// Lift off the floor so the pawn's collision capsule clears the ground on spawn.
	OutLocation = Origin + FVector(0.f, 0.f, 100.f);
	// .spawn already carries Unreal-space yaw (UE_bsp_to_scene negates it at export).
	OutYaw = YawSrc;
	return true;
}

void AElysiumMapActor::ResolveLandmarkSpawn()
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps || !EntityWorld)
	{
		return;
	}

	FString Landmark; FVector Offset; float Yaw; bool bHasYaw;
	if (!Maps->ConsumeLandmarkSpawn(Landmark, Offset, Yaw, bHasYaw))
	{
		return;   // not a landmark transition — keep the info_player_start placement (ReadSpawn)
	}

	FElysiumEntity* Lm = EntityWorld->FindLandmark(Landmark);
	if (!Lm || !Lm->Def)
	{
		// Both maps must carry an info_landmark of the same name; a missing one is a data error.
		// Fall back to info_player_start rather than dumping the player at the origin (matches the
		// decompiled "can't find landmark" warning path).
		UE_LOG(LogElysium, Warning,
			TEXT("landmark '%s' not found in %s — spawning at info_player_start"), *Landmark, *MapName);
		return;
	}

	// new pos = destination landmark origin + the offset captured at the source landmark. The offset
	// already carries the player's capsule-centre height above the landmark, so a real transition
	// needs no extra lift; a direct/console landmark entry (offset zero) seats the capsule centre by
	// lifting off the landmark's feet origin, and faces the landmark's own angles.
	PendingSpawnLoc = Lm->Def->Origin + Offset;
	if (bHasYaw)
	{
		PendingSpawnYaw = Yaw;   // preserve the player's view yaw across the transition
	}
	else
	{
		PendingSpawnLoc.Z += 100.0f;   // lift the capsule off the landmark feet (as ReadSpawn does)
		PendingSpawnYaw = -Lm->Angles.Y;   // face the landmark's angles (Source yaw negated to Unreal)
	}
	bSpawnPending = true;
	EntryLandmark = Landmark;

	// Fire the landmark's OnEnterMapHere (e.g. pawnshop's newgame/haven -> Radio2.Deactivate). The
	// spawn pass is complete, so every wire target exists; FireOutput queues it on the event queue,
	// serviced on the first tick (after any logic_auto OnMapLoad, matching the map-enter ordering).
	static const FName OnEnterMapHere(TEXT("OnEnterMapHere"));
	Lm->FireOutput(OnEnterMapHere, Lm->Handle);

	UE_LOG(LogElysium, Log, TEXT("landmark spawn: %s @ %s -> %s (yaw %.0f)"),
		*MapName, *Landmark, *PendingSpawnLoc.ToString(), PendingSpawnYaw);
}

void AElysiumMapActor::ResolveRestorePlacement()
{
	UGameInstance* GI = GetGameInstance();
	UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (!Maps)
	{
		return;
	}

	// 11.9 — a loaded save carries the pose the player was actually standing in, so it outranks both
	// info_player_start and a landmark offset. It is already a pawn-space (capsule-centre) location:
	// the save read it off the body, so it goes back verbatim with no lift.
	FVector Origin; float Yaw;
	if (!Maps->ConsumeRestorePlacement(Origin, Yaw))
	{
		return;
	}
	PendingSpawnLoc = Origin;
	PendingSpawnYaw = Yaw;
	bSpawnPending = true;
	UE_LOG(LogElysium, Log, TEXT("restore placement: %s @ %s (yaw %.0f)"),
		*MapName, *PendingSpawnLoc.ToString(), PendingSpawnYaw);
}

void AElysiumMapActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Retire the NPC motors while they are still real objects. The entity world is destroyed with
	// this actor, and every FElysiumNpc destructor on that path calls DestroyNpcMotor — but by then
	// the level's actors are gone and their UObject indices freed, so touching one at all is fatal.
	// The engine destroys the motor actors itself; releasing the tracking array is the whole job.
	bMotorsRetired = true;
	NpcMotors.Reset();

	// The audio subsystem is GameInstance-scoped and outlives this map actor, but every voice it
	// holds is map-scoped (ambient_generic + the scheme bed/music/random one-shots). Stop them all
	// on unload so nothing bleeds into the next map. StopAllVoices also covers the scheme voices, so
	// the scheme manager only needs to drop its (now-dead) handles.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumAudioSubsystem* Audio = GI->GetSubsystem<UElysiumAudioSubsystem>())
		{
			if (SchemeManager)
			{
				SchemeManager->StopAll(Audio);
			}
			Audio->RetireMapEpoch(AudioMapEpoch);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AElysiumMapActor::PreMoveTick(float DeltaSeconds)
{
	// Steps 2-3 — everything that must be settled before the pawn moves.

	// The controller and the pawn appear after this actor does, so keep looking until the frame
	// order is fully declared (a menu backdrop map never seats a pawn, and that is fine). This is
	// the frame's first pass, so an edge wired here is in force from the same frame it is bound.
	EnsureTickPrerequisites();
	if (RuntimePhase != EElysiumMapRuntimePhase::Active)
	{
		if (RuntimePhase == EElysiumMapRuntimePhase::WaitingForPrerequisites)
		{
			PollRuntimeActivation();
		}
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			// Step 2 — the only place `Now` moves (S1). DeltaSeconds is already dilated by the
			// engine, and the clock applies no factor of its own, so a time scale is applied once.
			// It sits ahead of the move because retail rebinds `frametime`/`curtime` to the user
			// command's own timing for the move's duration: the move runs at this frame's `now`,
			// never the previous frame's.
			GameState->TimeControl().AdvanceFrame(DeltaSeconds);

			// Step 3 — the player's OWN think, which retail runs inside CPlayerMove::RunCommand
			// (PreThink -> think -> move -> PostThink) rather than in Physics_RunThinkFunctions.
			// Every other entity thinks in the gameplay pass, after the move.
			if (EntityWorld)
			{
				EntityWorld->RunPlayerThink(GameState->GameClock().GetNow());
			}
		}
	}

}

void AElysiumMapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// ACharacter movement automatically depends on the primary tick of the actor owning its
	// movement base. Runtime world collision is owned here, so this native tick is deliberately an
	// empty barrier between player movement and NPC movement. GameplayTick performs GameFrame after
	// both without creating the reverse edge that caused the patrol-era tick cycle.
}

void AElysiumMapActor::GameplayTick(float DeltaSeconds)
{
	if (RuntimePhase == EElysiumMapRuntimePhase::Activating)
	{
		ActivateRuntime();
		return;
	}
	if (RuntimePhase != EElysiumMapRuntimePhase::Active)
	{
		return;
	}

	// Steps 5-6 — `GameFrame` itself: the pawn has already moved (step 4), which is the order
	// RE21 pins. This whole branch is admitted only after the activation transaction.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			// Steps 5-6 — the substrate, think-first (retail order: Physics_RunThinkFunctions,
			// then CEventQueue::ServiceEvents).
			if (EntityWorld)
			{
				EntityWorld->Tick(GameState->GameClock().GetNow());
			}

			TickAudio(DeltaSeconds);
			// The environment output is sampled every frame so Cog/cvar tuning responds immediately.
			// Particle presentation remains dormant inside ApplyEmitter.
			TickWeatherPresentation();
		}
	}
}

void AElysiumMapActor::ApplyWetness(const FElysiumWeatherTransition& Transition)
{
	WetnessTransition = Transition;
	ApplyWeatherTuning();
}

void AElysiumMapActor::ApplyEmitter(const FElysiumWeatherEmitterState& Emitter)
{
	// The rain emitter keeps its own presentation path: its system is a hand-authored global asset
	// driven by the wetness tuning below, not one of the per-map baked closures.
	if (Emitter.ParticleDefinition.Equals(TEXT("rain_follow_emitter"), ESearchCase::IgnoreCase))
	{
		RainEmitterStates.Add(Emitter.Entity.Index, Emitter);
		return;
	}
	if (!Emitter.bActive)
	{
		RemoveEmitter(Emitter.Entity);
		return;
	}

	UNiagaraComponent* Component = RainComponents.FindRef(Emitter.Entity.Index);
	if (!Component)
	{
		const FString Path = FElysiumContentPaths::BakedParticleSystem(MapName, Emitter.ParticleDefinition);
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Path);
		if (!System)
		{
			// A definition the offline contract could not compile has no system; the map records it
			// under `unresolved` and the emitter simply draws nothing.
			return;
		}
		Component = NewObject<UNiagaraComponent>(this);
		Component->SetAsset(System);
		Component->SetAutoActivate(false);
		Component->SetupAttachment(GetRootComponent());
		Component->RegisterComponent();
		AddInstanceComponent(Component);
		RainComponents.Add(Emitter.Entity.Index, Component);
	}

	AttachEmitter(Emitter, Component);
	Component->SetVariableFloat(TEXT("User.RateScale"), Emitter.RateScale);
	Component->Activate();
	RainEmitterStates.Add(Emitter.Entity.Index, Emitter);
}

// `attach_type` 2 is `point`: ride a named point on another entity's body. The parent is resolved
// here rather than at PostSpawn because none of the cinematic emitters' parents exist at map load.
void AElysiumMapActor::AttachEmitter(
	const FElysiumWeatherEmitterState& Emitter, UNiagaraComponent* Component)
{
	if (!Component)
	{
		return;
	}
	USceneComponent* ParentBody = nullptr;
	if (Emitter.AttachType == 2 && !Emitter.ParentName.IsEmpty() && EntityWorld)
	{
		if (FElysiumEntity* Parent = EntityWorld->FindByName(Emitter.ParentName))
		{
			ParentBody = Parent->GetAttachBody();
		}
	}
	if (!ParentBody)
	{
		Component->AttachToComponent(GetRootComponent(),
			FAttachmentTransformRules::KeepRelativeTransform);
		Component->SetRelativeLocation(Emitter.LocationCm);
		return;
	}
	// VtMB bone names carry spaces (`Bip01 Neck`) and survive the glTF export unchanged, so the
	// authored name is used verbatim. A body that has not got the bone falls back to its root.
	const FName Bone(*Emitter.AttachBone);
	const bool bHasBone = !Emitter.AttachBone.IsEmpty()
		&& ParentBody->DoesSocketExist(Bone);
	Component->AttachToComponent(ParentBody,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale, bHasBone ? Bone : NAME_None);
	Component->SetRelativeLocation(FVector::ZeroVector);
	if (!bHasBone && !Emitter.AttachBone.IsEmpty())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[particles] '%s' has no bone '%s'"),
			*Emitter.ParentName, *Emitter.AttachBone);
	}
}

void AElysiumMapActor::RemoveEmitter(const FElysiumEntityHandle& Entity)
{
	RainEmitterStates.Remove(Entity.Index);
	if (TObjectPtr<UNiagaraComponent> Component; RainComponents.RemoveAndCopyValue(Entity.Index, Component))
	{
		if (Component) { Component->DestroyComponent(); }
	}
}

void AElysiumMapActor::TickWeatherPresentation()
{
	if (GPendingWeatherTimer.IsSet())
	{
		const bool bRainOn = GPendingWeatherTimer.GetValue();
		GPendingWeatherTimer.Reset();
		FireWeatherTimer(bRainOn);
	}
	ApplyWeatherTuning();
}

void AElysiumMapActor::ApplyWeatherTuning()
{
	const float Enhancement = FMath::Clamp(CVarRainEnhancement.GetValueOnGameThread(), 0.0f, 1.0f);
	bEnvironmentWetnessOverride = CVarEnvironmentWetnessOverride.GetValueOnGameThread() != 0;
	PresentedWetness = bEnvironmentWetnessOverride
		? FMath::Clamp(CVarEnvironmentWetness.GetValueOnGameThread(), 0.0f, 1.0f)
		: FMath::Clamp(WetnessTransition.CurrentWetness, 0.0f, 1.0f);
	PresentedWetnessScale = FMath::Clamp(
		CVarEnvironmentWetnessScale.GetValueOnGameThread(), 0.0f, 4.0f);
	if (!EnvironmentParameters)
	{
		EnvironmentParameters = LoadObject<UMaterialParameterCollection>(
			nullptr, TEXT("/Game/VtMB/Materials/MPC_ElysiumEnvironment.MPC_ElysiumEnvironment"));
	}
	if (EnvironmentParameters && GetWorld())
	{
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("GlobalWetness"), PresentedWetness);
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("WetnessOutputScale"), PresentedWetnessScale);
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainEnhancement"), Enhancement);
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainWetDarken"), FMath::Max(0.0f, CVarRainWetDarken.GetValueOnGameThread()));
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainWetRoughness"), FMath::Max(0.0f, CVarRainWetRoughness.GetValueOnGameThread()));
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainLightResponse"), FMath::Clamp(
				CVarRainLightResponse.GetValueOnGameThread(), 0.0f, 1.0f));
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainSourceRetain"), FMath::Clamp(
				CVarRainSourceRetain.GetValueOnGameThread(), 0.0f, 1.0f));
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainWetSpecular"), FMath::Clamp(
				CVarRainWetSpecular.GetValueOnGameThread(), 0.0f, 1.0f));
		UKismetMaterialLibrary::SetScalarParameterValue(GetWorld(), EnvironmentParameters,
			TEXT("RainReflectionDebug"), static_cast<float>(FMath::Clamp(
				CVarRainReflectionDebug.GetValueOnGameThread(), 0, 6)));
	}
	const float EnhancedRate = FMath::Lerp(1.0f,
		FMath::Max(0.0f, CVarRainRateScale.GetValueOnGameThread()), Enhancement);
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : RainEmitterStates)
	{
		if (UNiagaraComponent* Component = RainComponents.FindRef(Pair.Key))
		{
			Component->SetVariableFloat(TEXT("User.RateScale"), Pair.Value.RateScale * EnhancedRate);
			Component->SetVariableFloat(TEXT("User.Enhancement"), Enhancement);
			// The Niagara rate expression applies Enhancement once.  Keep the mist control as
			// the full-wet tuning value so intermediate enhancement values remain linear.
			Component->SetVariableFloat(TEXT("User.MistEnhancement"),
				FMath::Max(0.0f, CVarRainMist.GetValueOnGameThread()));
			Component->SetVariableFloat(TEXT("User.LightResponse"), FMath::Max(0.0f,
				CVarRainLightResponse.GetValueOnGameThread()));
		}
	}
}

void AElysiumMapActor::FireWeatherTimer(bool bRainOn)
{
	if (!EntityWorld)
	{
		return;
	}
	EntityWorld->EnqueueInput(bRainOn ? TEXT("rain_on_timer") : TEXT("rain_off_timer"),
		FName(TEXT("FireTimer")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
}

FString AElysiumMapActor::GetWeatherDebugSummary() const
{
	FString Result = FString::Printf(TEXT("wet authored %.3f->%.3f presented %.3f x%.2f override=%d components %d"),
		WetnessTransition.CurrentWetness, WetnessTransition.TargetWetness,
		PresentedWetness, PresentedWetnessScale, bEnvironmentWetnessOverride ? 1 : 0,
		RainComponents.Num());
	if (RainSystem)
	{
		for (const FNiagaraEmitterHandle& Handle : RainSystem->GetEmitterHandles())
		{
			const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (!Data)
			{
				continue;
			}
			for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			{
				const UNiagaraSpriteRendererProperties* Sprite =
					Cast<UNiagaraSpriteRendererProperties>(Renderer);
				Result += Sprite ? FString::Printf(
					TEXT(" | renderer=%s enabled=%d source=%d material=%s position=%s "
						"color=%s size=%s visibility=%s:%u camera_cull=%d:%.1f..%.1f"),
					*Handle.GetName().ToString(), Sprite->GetIsEnabled() ? 1 : 0,
					static_cast<int32>(Sprite->SourceMode), *GetNameSafe(Sprite->Material),
					*Sprite->PositionBinding.GetParamMapBindableVariable().GetName().ToString(),
					*Sprite->ColorBinding.GetParamMapBindableVariable().GetName().ToString(),
					*Sprite->SpriteSizeBinding.GetParamMapBindableVariable().GetName().ToString(),
					*Sprite->RendererVisibilityTagBinding.GetParamMapBindableVariable().GetName().ToString(),
					Sprite->RendererVisibility, Sprite->bEnableCameraDistanceCulling ? 1 : 0,
					Sprite->MinCameraDistance, Sprite->MaxCameraDistance)
					: FString::Printf(TEXT(" | renderer=%s non-sprite"), *Handle.GetName().ToString());
			}
		}
	}
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : RainEmitterStates)
	{
		const UNiagaraComponent* Component = RainComponents.FindRef(Pair.Key);
		FString Materials;
		if (Component)
		{
			TArray<UMaterialInterface*> UsedMaterials;
			Component->GetUsedMaterials(UsedMaterials, false);
			for (const UMaterialInterface* Material : UsedMaterials)
			{
				Materials += Materials.IsEmpty() ? TEXT("") : TEXT(",");
				Materials += GetNameSafe(Material);
			}
			if (const FNiagaraSystemInstanceControllerConstPtr Controller =
					Component->GetSystemInstanceController())
			{
				if (const FNiagaraSystemInstance* Instance = Controller->GetSystemInstance_Unsafe())
				{
					for (const FNiagaraEmitterInstanceRef& EmitterRef : Instance->GetEmitters())
					{
						const FNiagaraEmitterInstance& Emitter = EmitterRef.Get();
						const FNiagaraDataSet& Data = Emitter.GetParticleData();
						const FNiagaraDataSetAccessor<FNiagaraPosition> Accessor(
							Data, FName(TEXT("Position")));
						const FNiagaraDataSetReaderFloat<FNiagaraPosition> Reader =
							Accessor.GetReader(Data);
						const FNiagaraDataSetAccessor<int32> VisibilityAccessor(
							Data, FName(TEXT("VisibilityTag")));
						const FNiagaraDataSetReaderInt32<int32> VisibilityReader =
							VisibilityAccessor.GetReader(Data);
						FNiagaraPosition Min(ForceInit), Max(ForceInit);
						if (Reader.IsValid() && Emitter.GetNumParticles() > 0)
						{
							Reader.GetMinMax(Min, Max);
						}
						Materials += FString::Printf(TEXT(";%s:n=%d,p=%s..%s,visibility=%d"),
							*Emitter.GetEmitterHandle().GetName().ToString(), Emitter.GetNumParticles(),
							*FVector3f(Min).ToString(), *FVector3f(Max).ToString(),
							VisibilityReader.IsValid() && Emitter.GetNumParticles() > 0
								? VisibilityReader.Get(0) : INDEX_NONE);
					}
				}
			}
		}
		Result += FString::Printf(
			TEXT(" | #%d active=%d component=%d visible=%d render=%d rate=%.3f "
				"loc=%s world_bounds=%s materials=%s"),
			Pair.Key, Pair.Value.bActive ? 1 : 0,
			Component && Component->IsActive() ? 1 : 0,
			Component && Component->IsVisible() ? 1 : 0,
			Component && Component->IsRenderStateCreated() ? 1 : 0,
			Pair.Value.RateScale,
			Component ? *Component->GetComponentLocation().ToCompactString() : TEXT("<none>"),
			Component ? *Component->Bounds.GetBox().ToString() : TEXT("<none>"),
			Materials.IsEmpty() ? TEXT("<none>") : *Materials);
	}
	return Result;
}

double AElysiumMapActor::GetRuntimeWaitSeconds() const
{
	if (RuntimePhase == EElysiumMapRuntimePhase::Active
		|| RuntimePhase == EElysiumMapRuntimePhase::Failed)
	{
		return RuntimeWaitDurationSeconds;
	}
	return RuntimeWaitStartSeconds > 0.0
		? FMath::Max(0.0, FPlatformTime::Seconds() - RuntimeWaitStartSeconds)
		: 0.0;
}

FString AElysiumMapActor::GetMissingRuntimePrerequisites() const
{
	return CollectRuntimePrerequisites().Missing();
}

FElysiumMapRuntimePrerequisites AElysiumMapActor::CollectRuntimePrerequisites() const
{
	FElysiumMapRuntimePrerequisites P;
	P.bConstructionComplete = bRuntimeConstructionComplete;
	P.bEntityWorldReady = EntityWorld.Get() != nullptr;
	const UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	P.bAudioCatalogReady = !Audio || Audio->IsReadyForMapActivation();
	P.bMenuBackdrop = bMenuBackdrop;
	const EElysiumCollisionBuildState CollisionState = Collision
		? Collision->GetBuildState() : EElysiumCollisionBuildState::Failed;
	P.bCollisionReady = CollisionState == EElysiumCollisionBuildState::Ready
		|| CollisionState == EElysiumCollisionBuildState::Disabled;
	P.bCollisionFailed = CollisionState == EElysiumCollisionBuildState::Failed;
	P.bNavigationRequired = !bMenuBackdrop
		&& CollisionState != EElysiumCollisionBuildState::Disabled;
	P.bNavigationReady = !P.bNavigationRequired || IsRuntimeNavigationReady();
	P.bNavigationFailed = bNavigationBuildFailed;
	P.bSpawnTransformReady = bSpawnPending;
	P.bPlayerEntityReady = EntityWorld && EntityWorld->PlayerHandle().IsSet();

	if (!bMenuBackdrop)
	{
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		P.bPossessedPawnReady = Pawn && Pawn->GetController() == PC;
		P.bPlayerBodyReady = Pawn && Cast<IElysiumPlayerBody>(Pawn) != nullptr;
		P.bFinalPlacementReady = bSpawnPlaced;
		UPawnMovementComponent* Move = Pawn ? Pawn->GetMovementComponent() : nullptr;
		P.bTickPrerequisitesReady = Move && PrereqController.Get() == PC
			&& PrereqMovement.Get() == Move;
	}
	return P;
}

void AElysiumMapActor::PollRuntimeActivation()
{
	EnsureRuntimeNavigation();
	if (!bMenuBackdrop && bRuntimeConstructionComplete)
	{
		APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
		if (Pawn && Pawn->GetController() == PC && Body && !bSpawnPlaced)
		{
			// Freeze before changing the transform: even if movement's prerequisite runs later in this
			// frame, the newly placed pawn cannot take an unguarded step.
			Body->SetMovementFrozen(true);
			Pawn->SetActorLocation(PendingSpawnLoc, false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation(FRotator(0.f, PendingSpawnYaw, 0.f));
			bSpawnPlaced = true;
			EnsureTickPrerequisites();
			UE_LOG(LogElysium, Log, TEXT("map runtime %s: player placed and frozen at %s"),
				*MapName, *PendingSpawnLoc.ToString());
		}
	}

	const FElysiumMapRuntimePrerequisites P = CollectRuntimePrerequisites();
	FString Failure;
	switch (P.Evaluate(GetRuntimeWaitSeconds(), Failure))
	{
	case EElysiumMapReadinessResult::Ready:
		RuntimePhase = EElysiumMapRuntimePhase::Activating;
		UE_LOG(LogElysium, Log, TEXT("map runtime %s: %s after %.3fs"), *MapName,
			ElysiumMapRuntimePhaseName(RuntimePhase), GetRuntimeWaitSeconds());
		return;
	case EElysiumMapReadinessResult::Failed:
		if (P.bCollisionFailed && Collision && !Collision->GetFailureReason().IsEmpty())
		{
			Failure = Collision->GetFailureReason();
		}
		FailRuntime(Failure);
		return;
	case EElysiumMapReadinessResult::Waiting:
	default:
		return;
	}
}

void AElysiumMapActor::EnsureRuntimeNavigation()
{
	if (bMenuBackdrop || bNavigationBuildRequested || bNavigationBuildFailed || !Collision
		|| Collision->GetBuildState() != EElysiumCollisionBuildState::Ready)
	{
		return;
	}

	const FBox CollisionBounds = Collision->GetWorldBounds();
	UWorld* World = GetWorld();
	UNavigationSystemV1* Navigation = World
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!CollisionBounds.IsValid || !World || !Navigation)
	{
		bNavigationBuildFailed = true;
		UE_LOG(LogElysium, Error, TEXT("runtime navigation %s: no valid collision bounds/navigation system"),
			*MapName);
		return;
	}

	// The nav-bounds actor normally carries an editor-authored brush. Generated maps intentionally
	// carry no nav asset, so a no-collision UBoxComponent contributes the equivalent runtime bounds;
	// UNavigationSystemV1 reads GetComponentsBoundingBox and Recast projects the actual colliders.
	const FVector Center = CollisionBounds.GetCenter();
	FVector Extent = CollisionBounds.GetExtent();
	Extent.X += 500.0f;
	Extent.Y += 500.0f;
	Extent.Z += 300.0f;

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.OverrideLevel = GetLevel();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	NavigationBounds = World->SpawnActor<ANavMeshBoundsVolume>(
		ANavMeshBoundsVolume::StaticClass(), FTransform(FRotator::ZeroRotator, Center), Params);
	if (!NavigationBounds)
	{
		bNavigationBuildFailed = true;
		UE_LOG(LogElysium, Error, TEXT("runtime navigation %s: failed to create bounds"), *MapName);
		return;
	}

	UBoxComponent* BoundsBox = NewObject<UBoxComponent>(NavigationBounds, TEXT("ElysiumNavigationBounds"));
	BoundsBox->SetMobility(EComponentMobility::Static);
	BoundsBox->SetBoxExtent(Extent);
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetCanEverAffectNavigation(false);
	BoundsBox->SetupAttachment(NavigationBounds->GetRootComponent());
	NavigationBounds->AddInstanceComponent(BoundsBox);
	BoundsBox->RegisterComponent();

	// Both colliders cook asynchronously after their components register. Refresh their octree data
	// now that the activation barrier has observed completed BodySetups, then build exactly once.
	Collision->RefreshNavigationData();
	Navigation->OnNavigationBoundsUpdated(NavigationBounds);
	Navigation->Build();
	bNavigationBuildRequested = true;
	UE_LOG(LogElysium, Log, TEXT("runtime navigation %s: Recast build requested over %s"),
		*MapName, *CollisionBounds.ToString());
}

bool AElysiumMapActor::IsRuntimeNavigationReady() const
{
	if (!bNavigationBuildRequested || bNavigationBuildFailed || !NavigationBounds)
	{
		return false;
	}
	UNavigationSystemV1* Navigation = GetWorld()
		? FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()) : nullptr;
	const ARecastNavMesh* Recast = Navigation
		? Cast<ARecastNavMesh>(Navigation->GetMainNavData()) : nullptr;
	return Recast && Recast->GetNumActiveTiles() > 0
		&& !Navigation->IsNavigationBuildInProgress();
}

void AElysiumMapActor::ActivateRuntime()
{
	if (RuntimePhase != EElysiumMapRuntimePhase::Activating)
	{
		return;
	}

	double Now = 0.0;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			Now = GameState->GameClock().GetNow();
		}
	}
	// Characters are constructed with the entity world, before asynchronous collision and Recast
	// exist. Admit their visibility/collision only at the same atomic activation barrier as the
	// player. Idle CharacterMovement stays asleep; the first MoveTo wakes it against the complete graph.
	for (AElysiumNpcBody* Motor : NpcMotors)
	{
		if (IsValid(Motor))
		{
			Motor->SetRuntimeReady(true);
		}
	}

	if (EntityWorld)
	{
		EntityWorld->Activate(Now);
	}
	if (APawn* Pawn = ResolvePlayerPawn())
	{
		ReconcilePlayerBrushTouches(Pawn);
	}
	if (EntityWorld)
	{
		// One frozen-time pass ignites logic_auto, map-entry outputs, initial containment touches,
		// and every zero-delay event they produce before the partial world is ever shown.
		EntityWorld->RunPlayerThink(Now);
		EntityWorld->Tick(Now);
	}
	if (bHasDeferredSchemeFadeIn && SchemeManager)
	{
		SchemeManager->FadeInScheme(GetAudioSubsystem(), DeferredSchemeRel,
			DeferredSchemeAnchor, DeferredSchemeFadeSeconds);
		bHasDeferredSchemeFadeIn = false;
		DeferredSchemeRel.Reset();
	}
	TickAudio(0.0f);

	RuntimeWaitDurationSeconds = GetRuntimeWaitSeconds();
	RuntimePhase = EElysiumMapRuntimePhase::Active;
	bSpawnDone = true;
	if (APawn* Pawn = ResolvePlayerPawn())
	{
		if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn))
		{
			Body->SetMovementFrozen(false);
		}
	}
	PreMoveTickFunction.bTickEvenWhenPaused = false;
	PrimaryActorTick.bTickEvenWhenPaused = false;
	GameplayTickFunction.bTickEvenWhenPaused = false;

	UE_LOG(LogElysium, Log, TEXT("map runtime %s: Active after %.3fs at game time %.3f"),
		*MapName, GetRuntimeWaitSeconds(), Now);
	RuntimeReady.Broadcast(this);
}

void AElysiumMapActor::FailRuntime(const FString& Reason)
{
	if (RuntimePhase == EElysiumMapRuntimePhase::Failed
		|| RuntimePhase == EElysiumMapRuntimePhase::Active)
	{
		return;
	}
	RuntimeFailureReason = Reason;
	RuntimeWaitDurationSeconds = GetRuntimeWaitSeconds();
	RuntimePhase = EElysiumMapRuntimePhase::Failed;
	UE_LOG(LogElysium, Error,
		TEXT("map runtime %s: Failed after %.3fs: %s [missing: %s]"),
		*MapName, GetRuntimeWaitSeconds(), *RuntimeFailureReason,
		*GetMissingRuntimePrerequisites());
	RuntimeFailed.Broadcast(this, RuntimeFailureReason);
}

void AElysiumMapActor::TickAudio(float DeltaSeconds)
{
	UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	if (!Audio)
	{
		return;
	}
	Audio->TickAudio(DeltaSeconds);
	if (SchemeManager)
	{
		FVector ListenerLoc = GetActorLocation();
		if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			FVector VLoc; FRotator VRot;
			PC->GetPlayerViewPoint(VLoc, VRot);
			ListenerLoc = VLoc;
		}
		SchemeManager->Tick(Audio, ListenerLoc, DeltaSeconds);
	}
}

void AElysiumMapActor::PostMoveTick(float DeltaSeconds)
{
	if (RuntimePhase != EElysiumMapRuntimePhase::Active)
	{
		return;
	}

	// Step 8 — everything here reads the frame's final positions.
	//
	// P4.2 — the minimal +use look-cursor: re-pick the aimed usable and fire OnIn/OnOut on the
	// transitions. It traces, so it belongs after physics: before the pawn's move it would pick
	// against last frame's geometry, which reads as a door you cannot use until you stop walking.
	// Its outputs enqueue against the same `now` the pre-move pass advanced to, so they service on
	// the next frame's queue pass exactly like any other zero-delay wire.
	if (EntityWorld)
	{
		EntityWorld->UpdateUseCursor();
	}

	// 11.7 — re-resolve every `Follow` camera shot against this frame's final entity positions. Same
	// reason as the use cursor: a shot framed on where an NPC *was* reads as a camera that lags the
	// subject it is supposed to be locked onto.
	if (CameraDirector)
	{
		CameraDirector->Tick(EntityWorld.Get(), PlayerCamera());
	}

	// The tail of a released frame: a dev step spends one here, and the last one re-holds the world.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			GameState->TimeControl().EndFrame();
		}
	}
}
