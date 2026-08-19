#include "ElysiumMapActor.h"

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumMovementComponent.h"   // the gait-speed push (CCC7)
#include "ElysiumMoveSolve.h"           // ElysiumMove::U — the one Source-unit conversion
#include "ElysiumPlayer.h"              // FElysiumCombatCharacter — the feed probe's candidate set
#include "ElysiumPlayerBody.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumUseIcons.h"
#include "Audio/ElysiumSoundScheme.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraService.h"
#include "Visual/ElysiumAnimationDriver.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Substrate/ElysiumDisposition.h"   // FElysiumEyeTargetTuning — the gaze layer's content
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Visual/ElysiumNpcBody.h"
#include "Visual/ElysiumLightRig.h"        // the authored light set the light query estimates from
#include "Visual/ElysiumMapVisuals.h"

#include "Components/BoxComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
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
#include "NiagaraFunctionLibrary.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysium, Log, All);

namespace
{
	UElysiumCameraService* LocalCameraService(const UObject* Context)
	{
		UWorld* World = Context ? Context->GetWorld() : nullptr;
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		ULocalPlayer* Player = GI ? GI->GetFirstGamePlayer() : nullptr;
		return Player ? Player->GetSubsystem<UElysiumCameraService>() : nullptr;
	}
}

static TAutoConsoleVariable<float> CVarRainEnhancement(
	TEXT("elysium.RainEnhancement"), 0.0f,
	TEXT("Wetness presentation tuning: 0 is the authored reference; 1 enables the enhanced branch."));
static TAutoConsoleVariable<float> CVarRainRateScale(
	TEXT("elysium.RainRateScale"), 1.0f,
	TEXT("Multiplier on the authored env_particle rate for live Niagara emitters."));
static TAutoConsoleVariable<int32> CVarRainForce(
	TEXT("elysium.RainForce"), 0,
	TEXT("1 = force the follow-rain volume on, ignoring env_particle rate."));
static TAutoConsoleVariable<float> CVarRainStreakWidth(
	TEXT("elysium.RainStreakWidth"), 1.2f, TEXT("Follow-rain streak width in cm."));
static TAutoConsoleVariable<float> CVarRainStreakLength(
	TEXT("elysium.RainStreakLength"), 55.0f, TEXT("Follow-rain streak length in cm."));
static TAutoConsoleVariable<float> CVarRainStreakAlpha(
	TEXT("elysium.RainStreakAlpha"), 0.18f, TEXT("Follow-rain streak opacity."));
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

static FAutoConsoleCommand GElysiumWeatherDump(
	TEXT("elysium.weather.dump"),
	TEXT("Print the live weather presentation summary."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (AElysiumMapActor* Map = ActiveWeatherMap())
		{
			UE_LOG(LogElysium, Log, TEXT("weather %s"), *Map->GetWeatherDebugSummary());
			return;
		}
		UE_LOG(LogElysium, Warning, TEXT("weather dump: no active map"));
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
	if (!bAnimationPreloadReady){ MissingItems.Add(TEXT("map animation residency")); }
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
	if (bConstructionComplete && !bAnimationPreloadReady)
	{
		OutFailure = TEXT("runtime construction did not complete map animation residency");
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
		// A replaced pawn leaves the outgoing component's edge behind, and the gameplay pass would
		// then wait on a tick function that is never going to run again. Drop it before wiring the
		// new one.
		if (UPawnMovementComponent* Previous = PrereqMovement.Get())
		{
			PrimaryActorTick.RemovePrerequisite(Previous, Previous->PrimaryComponentTick);
		}
		Move->PrimaryComponentTick.AddPrerequisite(this, PreMoveTickFunction);
		PrimaryActorTick.AddPrerequisite(Move, Move->PrimaryComponentTick);
		PrereqMovement = Move;
	}

	// 11.4 — tell the body which entity it embodies. Resynced every tick (cheap: one handle
	// assignment) rather than gated on the movement-prerequisite wiring above: a fresh world has no
	// pawn yet when the map builds, and SpawnPlayer can land on a later tick than the one where this
	// pawn's movement component first appears, so a one-shot assignment here can permanently capture
	// an Invalid() handle and starve every RouteBrushTouch of an activator.
	if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(PC->GetPawn()))
	{
		Body->SetPlayerEntity(EntityWorld ? EntityWorld->PlayerHandle() : FElysiumEntityHandle::Invalid());
	}
}

void AElysiumMapActor::BeginPlay()
{
	Super::BeginPlay();
	// Open this map's epoch (S4). The subsystem mints it and tells every application-lifetime
	// subscriber; a bare world with no subsystem leaves it 0, which matches no owner and retires
	// nothing.
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		MapEpoch = Maps->BeginMapEpoch();
	}
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

void AElysiumMapActor::BuildStageWorld()
{
	const double Start = FPlatformTime::Seconds();
	LoadPhases.Reset();
	LoadedMap = MapName;
	Bodies->SetMap(MapName);
	SkyDef = FElysiumSkyDef();

	// Nothing is adopted, built or parsed: there is no baked level, no `.hulls`, no `.ents`. The
	// collision component is left at its Disabled default, which the activation barrier counts as a
	// satisfied input and which also drops the runtime Recast requirement — a stage with no walkable
	// surface has nothing to navigate.
	//
	// The pawn is seated at the world origin rather than on the stage: the stage stands far out at
	// FElysiumGreenRoomRun::StageOrigin, and a player model standing in the same place as the body
	// being reviewed would be in every frame of it.
	PendingSpawnLoc = FVector(0.0f, 0.0f, 100.0f);
	PendingSpawnYaw = 0.0f;
	PendingSpawnSpace = EElysiumPlayerPlacementSpace::CapsuleCenter;
	bSpawnPending = true;

	// An empty entity world, not the absence of one. The green room reaches the map through the same
	// IElysiumEmbodiment seam a map's own NPCs do, the substrate clock is what `SeekCinematicClip`
	// and the audio pass run against, and the activation barrier requires both a world and a player
	// entity in it. All of that holds with zero entity definitions.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			SchemeManager = MakePimpl<FElysiumSoundSchemeManager>();
			SchemeManager->SetMapEpoch(MapEpoch);

			FElysiumWorldServices Services;
			Services.Embodiment = this;
			Services.Audio      = this;
			Services.Travel     = this;
			Services.Presenter  = UElysiumPresentationSubsystem::Get(GetWorld());
			Services.Weather    = this;
			Services.Camera     = LocalCameraService(this);
			EntityWorld = MakePimpl<FElysiumEntityWorld>(this, GameState, Services);
			EntityWorld->Load(FElysiumEntityDefs());
			EntityWorld->SpawnPlayer();
		}
	}

	// No map animations to make resident — the bodies the green room stands up resolve their own
	// clips on demand through the same caches.
	bAnimationPreloadReady = true;

	const double TotalMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	LoadPhases.Add({ TEXT("Total"), TotalMs });
	UE_LOG(LogElysium, Log, TEXT("built the green-room stage world in %.0f ms (no map)"), TotalMs);
}

void AElysiumMapActor::LoadMap()
{
	if (bStageOnly)
	{
		BuildStageWorld();
		return;
	}

	const double Start = FPlatformTime::Seconds();
	bAnimationPreloadReady = false;
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
		PendingSpawnSpace = EElysiumPlayerPlacementSpace::Feet;
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
				SchemeManager->SetMapEpoch(MapEpoch);

				// 11.2 — hand the substrate its outbound seam. This actor is three of the four
				// services; the fourth is the world-scoped presentation subsystem (11.8), which is
				// null only where there is no publisher at all (an editor preview world, a
				// Substrate-tier world with no engine behind it).
				FElysiumWorldServices Services;
				Services.Embodiment = this;
				Services.Audio      = this;
				Services.Travel     = this;
				Services.Presenter  = UElysiumPresentationSubsystem::Get(GetWorld());
				// The weather seam is live. rain_follow_emitter drives one viewer-volume Niagara
				// system; other env_particle definitions still load their baked closures.
				Services.Weather    = this;
				Services.Camera     = LocalCameraService(this);
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

	// The entity world is still dormant here: no logic_auto, trigger, scene, camera or game-clock
	// work can run. Walk every map-authored animation reference, retain the skeleton-bound sequences
	// in Bodies' map-epoch caches, then wait on only those editor compilation jobs. In a packaged
	// build the same walk performs the synchronous loose-asset loads and the finish is a no-op.
	if (EntityWorld)
	{
		EntityWorld->PreloadMapAnimations();
	}
	const int32 ResidentAnimations = FinishAnimationPreload();
	bAnimationPreloadReady = true;
	Phase(TEXT("Animations"));
	UE_LOG(LogElysium, Log, TEXT("map animation residency %s: %d sequence(s) ready before activation"),
		*MapName, ResidentAnimations);

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
	const FElysiumEntityHandle& EntityOwner, const FVector& FeetOrigin, float YawDegrees,
	const FString& Stem, int32 Variant)
{
	if (!Body || bMenuBackdrop || !GetWorld())
	{
		return nullptr;
	}
	if (!EntityOwner.IsSet())
	{
		UE_LOG(LogElysium, Warning, TEXT("failed to build native NPC body at %s: invalid entity owner"),
			*FeetOrigin.ToString());
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

	Motor->SetOwningEntity(this, EntityOwner);
	Motor->InitializeAtFeet(FeetOrigin, YawDegrees);
	Motor->SetRuntimeReady(RuntimePhase == EElysiumMapRuntimePhase::Active);
	Motor->SetModelStem(Stem, Body, Variant);
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
	const FString& Disposition, int32 DispositionLevel, int32 IdleVariant)
{
	return Bodies->RefreshNpcIdle(Body, Stem, Disposition, DispositionLevel, IdleVariant);
}

void AElysiumMapActor::UpdateNpcDisposition(USkeletalMeshComponent* Body,
	const FString& Disposition, int32 DispositionLevel)
{
	Bodies->UpdateNpcDisposition(Body, Disposition, DispositionLevel);
}

bool AElysiumMapActor::ResolveStanceClips(const FString& Stem, const FString& AnimName,
	FElysiumStanceClips& OutClips)
{
	return Bodies->ResolveStanceClips(Stem, AnimName, OutClips);
}

bool AElysiumMapActor::ResolveDisposition(const FString& Disposition, int32 DispositionLevel,
	FElysiumDisposition& OutRow)
{
	return Bodies->ResolveDisposition(Disposition, DispositionLevel, OutRow);
}

bool AElysiumMapActor::IsNpcBodyVisible(USkeletalMeshComponent* Body)
{
	return Bodies->IsNpcBodyVisible(Body);
}

bool AElysiumMapActor::PlayNpcActivity(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& Activity, int32 Variant, bool bLoop, float* OutSeconds)
{
	return Bodies->PlayNpcActivity(Body, Stem, Activity, Variant, bLoop, OutSeconds);
}

bool AElysiumMapActor::ResolveNpcActivityClip(const FString& Stem, const FString& Activity,
	int32 Variant, FString& OutLabel, FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	return Bodies->ResolveNpcActivityClip(Stem, Activity, Variant, OutLabel, OutAnimName,
		OutGroundSpeedCmPerSecond);
}

bool AElysiumMapActor::ResolveNpcSequenceClip(const FString& Stem, const FString& ClipName,
	FString& OutAnimName, float& OutGroundSpeedCmPerSecond)
{
	return Bodies->ResolveNpcSequenceClip(Stem, ClipName, OutAnimName, OutGroundSpeedCmPerSecond);
}

bool AElysiumMapActor::HasNpcClip(const FString& Stem, const FString& ClipName)
{
	return Bodies->HasNpcClip(Stem, ClipName);
}

bool AElysiumMapActor::PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	return Bodies->PlayNpcClip(Body, Stem, ClipName, bLoop, OutSeconds);
}

bool AElysiumMapActor::PreloadNpcClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName)
{
	return Bodies && Bodies->PreloadNpcClip(Body, Stem, ClipName);
}

bool AElysiumMapActor::PreloadNpcClipForModel(const FString& Stem, bool bPlayerMaterial,
	const FString& ClipName)
{
	return Bodies && Bodies->PreloadNpcClipForModel(Stem, bPlayerMaterial, ClipName);
}

bool AElysiumMapActor::PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName,
	bool bLoop, float* OutSeconds)
{
	// The anim-set model + the actor's bonerename root name a bank the offline split produced.
	UGameInstance* GI = GetGameInstance();
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
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

bool AElysiumMapActor::PreloadCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	const FString Bank = Anims ? Anims->GetIndex().CinematicBank(AnimSetModel, BoneRoot) : FString();
	return Bodies && !Bank.IsEmpty()
		&& Bodies->PreloadCinematicClip(Body, Stem, Bank, ClipName);
}

bool AElysiumMapActor::PreloadCinematicClipForModel(const FString& Stem, bool bPlayerMaterial,
	const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	const FString Bank = Anims ? Anims->GetIndex().CinematicBank(AnimSetModel, BoneRoot) : FString();
	return Bodies && !Bank.IsEmpty()
		&& Bodies->PreloadCinematicClipForModel(Stem, bPlayerMaterial, Bank, ClipName);
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

void AElysiumMapActor::ReleaseCinematicClaim(USkeletalMeshComponent* Body)
{
	if (Bodies)
	{
		Bodies->ReleaseCinematicClaim(Body);
	}
}

bool AElysiumMapActor::GetCinematicClipPosition(USkeletalMeshComponent* Body, float& OutSeconds) const
{
	return Bodies && Bodies->GetCinematicClipPosition(Body, OutSeconds);
}

bool AElysiumMapActor::ResyncCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds)
{
	return Bodies && Bodies->ResyncCinematicClip(Body, PositionSeconds);
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

bool AElysiumMapActor::PlayAttachedEffect(USkeletalMeshComponent* Body,
	const FString& Definition, FName Attachment)
{
	auto WarnOnce = [this, &Definition](const FString& Key, const FString& Message)
	{
		const FString Failure = TEXT("oneshot|") + Key;
		if (!ReportedEmitterFailures.Contains(Failure))
		{
			ReportedEmitterFailures.Add(Failure);
			UE_LOG(LogElysium, Warning, TEXT("%s"), *Message);
		}
	};
	if (!Body || Definition.IsEmpty() || Attachment.IsNone())
	{
		WarnOnce(TEXT("invalid|") + Definition,
			FString::Printf(TEXT("cannot play attached effect '%s' on map '%s': invalid body or attachment"),
				*Definition, *MapName));
		return false;
	}
	if (!Body->DoesSocketExist(Attachment))
	{
		WarnOnce(TEXT("socket|") + Definition + TEXT("|") + Attachment.ToString(),
			FString::Printf(TEXT("cannot play attached effect '%s' on %s: baked socket '%s' is absent"),
				*Definition, *Body->GetName(), *Attachment.ToString()));
		return false;
	}
	const FString Path = FElysiumContentPaths::BakedParticleSystem(MapName, Definition);
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Path);
	if (!System)
	{
		WarnOnce(TEXT("system|") + Definition,
			FString::Printf(TEXT("cannot play attached effect '%s' on map '%s': Niagara system '%s' is absent"),
				*Definition, *MapName, *Path));
		return false;
	}
	UNiagaraComponent* Spawned = UNiagaraFunctionLibrary::SpawnSystemAttached(
		System, Body, Attachment, FVector::ZeroVector, FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget, /*bAutoDestroy*/ true, /*bAutoActivate*/ true,
		ENCPoolMethod::None, /*bPreCullCheck*/ false);
	if (!Spawned)
	{
		WarnOnce(TEXT("spawn|") + Definition,
			FString::Printf(TEXT("cannot spawn attached effect '%s' on %s.%s"),
				*Definition, *Body->GetName(), *Attachment.ToString()));
		return false;
	}
	return true;
}

bool AElysiumMapActor::GetPhonemeFilter(USkeletalMeshComponent* Body, float& OutMin,
	float& OutMax) const
{
	return Bodies ? Bodies->GetPhonemeFilter(Body, OutMin, OutMax) : false;
}

bool AElysiumMapActor::SetViewTarget(USkeletalMeshComponent* Body, const FVector& WorldTarget)
{
	return Bodies ? Bodies->SetViewTarget(Body, WorldTarget) : false;
}

bool AElysiumMapActor::GetHeadFrame(USkeletalMeshComponent* Body, FVector& OutPosition,
	FVector& OutForward) const
{
	return Bodies ? Bodies->GetHeadFrame(Body, OutPosition, OutForward) : false;
}

FString AElysiumMapActor::AnimatedPropStemForModel(const FString& ModelPath) const
{
	return Bodies ? Bodies->AnimatedPropStemForModel(ModelPath) : FString();
}

FElysiumPlacedModelBody AElysiumMapActor::BuildPlacedModelBody(
	const FElysiumPlacedModelRequest& Request)
{
	return Bodies ? Bodies->BuildPlacedModelBody(Request) : FElysiumPlacedModelBody{};
}

bool AElysiumMapActor::HasPlacedModelCatalogue() const
{
	return Bodies && Bodies->HasPlacedModelCatalogue();
}

USkeletalMeshComponent* AElysiumMapActor::BuildAnimatedPropVisual(const FString& Stem,
	const FVector& Location, const FQuat& Rotation, float UniformScale, int32 PlacementToken)
{
	return Bodies ? Bodies->BuildAnimatedPropVisual(
		Stem, Location, Rotation, UniformScale, PlacementToken) : nullptr;
}

bool AElysiumMapActor::PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
	const FString& ClipName, bool bLoop, float* OutSeconds)
{
	return Bodies && Bodies->PlayAnimatedPropClip(Body, Stem, ClipName, bLoop, OutSeconds);
}

int32 AElysiumMapActor::PreloadAnimatedPropClips(USkeletalMeshComponent* Body,
	const FString& Stem)
{
	return Bodies ? Bodies->PreloadAnimatedPropClips(Body, Stem) : 0;
}

int32 AElysiumMapActor::FinishAnimationPreload()
{
	return Bodies ? Bodies->FinishAnimationPreload() : 0;
}

FString AElysiumMapActor::AnimatedPropRestClip(const FString& Stem, int32 PlacementToken) const
{
	return Bodies ? Bodies->AnimatedPropRestClip(Stem, PlacementToken) : FString();
}

bool AElysiumMapActor::FindAnimatedPropClip(const FString& Stem, const FString& ClipName,
	bool& bOutLoops) const
{
	bOutLoops = false;
	return Bodies && Bodies->FindAnimatedPropClip(Stem, ClipName, bOutLoops);
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

EElysiumItemGroundModelState AElysiumMapActor::ItemGroundModelState(const FString& ModelPath)
{
	return Bodies ? Bodies->ItemGroundModelState(ModelPath)
		: EElysiumItemGroundModelState::Unavailable;
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
	Visual->SetRelativeRotation(ElysiumSkeletalBasis::RelativeToParentFacing());

	// The mesh animates from the body sample the mover publishes at its tick tail (CCC1), so it has
	// to tick after the mover. `ACharacter` installs this prerequisite itself in
	// PostInitializeComponents — which covers every NPC and the capsule A/B body — but `AElysiumPawn`
	// is a plain `APawn` whose visual is built at runtime, so a skeletal mesh sharing the mover's
	// tick group would otherwise be ordered by registration, i.e. not at all.
	if (UPawnMovementComponent* Move = Pawn->GetMovementComponent())
	{
		Visual->AddTickPrerequisiteComponent(Move);
	}

	// Built hidden, and shown by the first draw policy the camera publishes. The alternative — show
	// it here and let the camera put it away a frame later — is a one-frame flash of the bind pose
	// every time a body is built or a model swapped.
	Visual->SetVisibility(true);
	Visual->SetComponentTickEnabled(true);
	Visual->SetHiddenInGame(true);
	Body->SetPlayerVisual(Visual);
	// The stem the animation driver resolves its catalog from. Kept here rather than re-derived from
	// the entity record each frame, because this is the one place that knows which model was built.
	PlayerVisualStem = Stem;
	if (PlayerAnimDriver.IsValid())
	{
		PlayerAnimDriver->Reset();
	}
	return Visual;
}

void AElysiumMapActor::TickPlayerAnimation(float DeltaSeconds)
{
	APawn* Pawn = ResolvePlayerPawn();
	IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
	if (Body == nullptr)
	{
		// A backdrop map seats no pawn. The driver simply does not advance; its record keeps saying
		// what it last said rather than reporting a body that is not there.
		return;
	}

	if (!PlayerAnimDriver.IsValid())
	{
		PlayerAnimDriver = MakePimpl<FElysiumAnimationDriver>();
		PlayerAnimDriver->Source = EElysiumAnimSource::Player;
	}

	PlayerAnimDriver->Stem = PlayerVisualStem;
	if (EntityWorld)
	{
		PlayerAnimDriver->Character = EntityWorld->PlayerHandle();
		// The variant is the body's own index, so the same character resolves the same idle every load
		// — repeatability is what makes a weighted pick assertable at all.
		PlayerAnimDriver->Variant = FMath::Max(0, PlayerAnimDriver->Character.Index);
		// What the drawn weapon does to the gait. Refreshed here rather than on the equip, because
		// this is the pass that already reads the character and the driver keys its own re-resolve on
		// the value changing — a swap therefore costs one frame of latency and no per-equip wiring.
		PlayerAnimDriver->SetTranslationContext(EntityWorld->FindPlayer());
	}

	USkeletalMeshComponent* Visual = Body->GetPlayerVisual();
	UElysiumAnimSubsystem* Anims = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	UElysiumBipedAnimInstance* Graph = Visual
		? Cast<UElysiumBipedAnimInstance>(Visual->GetAnimInstance()) : nullptr;

	// **What the graph said about the clip the latch is about to advance past.** Read BEFORE the
	// tick, because the report describes the request that was published last frame and the latch is
	// about to decide whether that request is over.
	//
	// The generation gate is the whole safety of it: a report stamped with a generation the driver
	// has already moved past describes a clip that is no longer playing, and letting it answer would
	// end the request that replaced it. A stale or absent report reads `Unknown`, which is exactly
	// the timer fallback a body with no graph gets.
	// The one-shot answer is `ElysiumAnimGraph::OneShotStateFor`'s, so the two collapses it exists to
	// prevent — a missing clip read as unanswerable, and a stale report ending the request that
	// replaced it — are asserted in the Substrate tier rather than living here.
	static const FElysiumOneShotReport NoReport;
	const FElysiumOneShotReport& Report = Graph ? Graph->GetOneShotReport() : NoReport;
	const EElysiumOneShotState OneShot = ElysiumAnimGraph::OneShotStateFor(
		/*bHasAsset*/ PlayerAnimDriver->Assets.Sequence != nullptr
			|| PlayerAnimDriver->Assets.Space != nullptr,
		/*bGenerationMatches*/ Graph != nullptr
			&& Report.Generation == PlayerAnimDriver->Selection.Generation,
		Report.bInOneShotState, Report.bComplete);

	PlayerAnimDriver->Tick(DeltaSeconds, Body->GetLocomotionSample(), Anims,
		Visual ? Visual->GetSkeletalMeshAsset() : nullptr, OneShot);

	// **The speed authority's push** (CCC7). The mover runs in the pre-physics pass and this driver
	// in the post-move one, so the mover cannot ask for a table — it has to be handed one, and the
	// tables are a property of the body rather than of the frame, so handing one over on change is
	// the whole of it. The generation gate is what keeps it from being a per-frame struct copy.
	//
	// The driver has already rebuilt its own gait reference off these tables, so the classifier's
	// walk/run threshold and the mover's commanded speed cannot come from two different numbers.
	if (PlayerAnimDriver->GaitGeneration != PushedGaitGeneration)
	{
		PushedGaitGeneration = PlayerAnimDriver->GaitGeneration;
		if (UElysiumMovementComponent* Move = Pawn->FindComponentByClass<UElysiumMovementComponent>())
		{
			Move->SetGaitSpeeds(PlayerAnimDriver->GaitSpeeds);
		}
	}
	// Hand the settled record to the graph (CCC5). The push is here rather than a pull from the
	// instance because the driver lives on this actor behind a pimpl while the visual is a component
	// of the pawn: an instance reaching for it would invert the layering and carry a null branch for
	// every map that seats no pawn. A cast body, or a player body whose graph package is missing,
	// simply is not a biped instance and is skipped.
	if (Graph)
	{
		Graph->PublishSelection(PlayerAnimDriver->Selection, PlayerAnimDriver->Assets);
	}
}

const FElysiumAnimationSelection& AElysiumMapActor::GetPlayerAnimSelection() const
{
	// A shared empty record rather than a null pointer: every reader — the recorder, Cog, the MCP
	// surface — wants a row, and a default record's outcome already says there is no vocabulary.
	static const FElysiumAnimationSelection Empty;
	return PlayerAnimDriver.IsValid() ? PlayerAnimDriver->Selection : Empty;
}

const FElysiumLocomotionSample& AElysiumMapActor::GetPlayerAnimSample() const
{
	// Same shape and same reason as the record above: a shared empty sample rather than a pointer,
	// and the two are always read together.
	static const FElysiumLocomotionSample Empty;
	return PlayerAnimDriver.IsValid() ? PlayerAnimDriver->Sample : Empty;
}

uint32 AElysiumMapActor::SubmitPlayerAnimRequest(const FElysiumAnimationRequest& Request)
{
	if (!PlayerAnimDriver.IsValid())
	{
		// The driver is normally built by the first player anim pass; a claim arriving ahead of it
		// — a scene that opens on the load frame — builds the same driver rather than being dropped.
		PlayerAnimDriver = MakePimpl<FElysiumAnimationDriver>();
		PlayerAnimDriver->Source = EElysiumAnimSource::Player;
	}
	return PlayerAnimDriver->SubmitRequest(Request);
}

bool AElysiumMapActor::ReleasePlayerAnimRequest(uint32 Handle)
{
	return PlayerAnimDriver.IsValid() && PlayerAnimDriver->ReleaseRequest(Handle);
}

bool AElysiumMapActor::IsPlayerVisual(const USkeletalMeshComponent* Body) const
{
	if (Body == nullptr)
	{
		return false;
	}
	APawn* Pawn = ResolvePlayerPawn();
	IElysiumPlayerBody* PlayerBody = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
	return PlayerBody != nullptr && PlayerBody->GetPlayerVisual() == Body;
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
	PlayerVisualStem.Reset();
	if (PlayerAnimDriver.IsValid())
	{
		PlayerAnimDriver->Reset();
	}
	// The body is gone, so its authored speeds go with it — a mover left holding them would steer the
	// next body by the last one's gait.
	PushedGaitGeneration = 0;
	if (UElysiumMovementComponent* Move = Pawn->FindComponentByClass<UElysiumMovementComponent>())
	{
		Move->ClearGaitSpeeds();
	}
}

void AElysiumMapActor::SetPlayerBodyEntityHidden(bool bInHidden)
{
	APawn* Pawn = ResolvePlayerPawn();
	if (IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr)
	{
		// Forwarded, not applied. The pawn holds both gates and is the only writer of the flags, so a
		// dormancy change and a mode switch cannot land on the component in either order and disagree.
		Body->SetBodyEntityHidden(bInHidden);
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

bool AElysiumMapActor::GetPlayerUseOrigin(FVector& OutLocation) const
{
	const APawn* Pawn = ResolvePlayerPawn();
	const IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn);
	const UElysiumCameraComponent* Camera = Body ? Body->GetCameraComponent() : nullptr;
	if (!Camera)
	{
		return false;
	}
	OutLocation = Camera->GetComponentLocation();
	return true;
}

bool AElysiumMapActor::GetPlayerCapsuleTransform(FVector& OutCapsuleCenter, FRotator& OutViewRotation) const
{
	const APawn* Pawn = ResolvePlayerPawn();
	if (!Pawn)
	{
		return false;
	}
	OutCapsuleCenter = Pawn->GetActorLocation();
	const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	OutViewRotation = PC ? PC->GetControlRotation() : Pawn->GetActorRotation();
	return true;
}

bool AElysiumMapActor::GetPlayerFeetTransform(FVector& OutFeetOrigin, FRotator& OutViewRotation) const
{
	if (!GetPlayerCapsuleTransform(OutFeetOrigin, OutViewRotation))
	{
		return false;
	}
	if (const APawn* Pawn = ResolvePlayerPawn())
	{
		if (const IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Pawn))
		{
			OutFeetOrigin.Z -= Body->GetBodyHalfHeight();
		}
	}
	return true;
}

void AElysiumMapActor::TeleportPlayer(const FVector& FeetOrigin, const FRotator& ViewRotation)
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
	{
		TGuardValue<bool> SuppressIngress(bSuppressPlayerTouchIngress, true);
		bPlayerTouchReconcilePending = true;
		Pawn->SetActorLocationAndRotation(Dest, FRotator(0.0f, ViewRotation.Yaw, 0.0f),
			false, nullptr, ETeleportType::TeleportPhysics);
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			PC->SetControlRotation(ViewRotation);
		}
	}
}

void AElysiumMapActor::RouteBrushTouch(const FElysiumEntityHandle& Brush,
	const FElysiumEntityHandle& Activator, bool bBegin)
{
	if (bSuppressPlayerTouchIngress)
	{
		return;
	}
	if (EntityWorld)
	{
		EntityWorld->RouteBrushTouch(Brush, Activator, bBegin);
	}
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
	TGuardValue<bool> SuppressIngress(bSuppressPlayerTouchIngress, true);
	if (USceneComponent* Root = Pawn->GetRootComponent())
	{
		Root->ClearSkipUpdateOverlaps();
	}
	Pawn->UpdateOverlaps(/*bDoNotifies*/ true);

	TArray<FElysiumEntityHandle> CurrentBrushes;
	TInlineComponentArray<UElysiumBrushComponent*> BrushComponents(this);
	for (UElysiumBrushComponent* Brush : BrushComponents)
	{
		if (Brush && Brush->GetSolidity() == EElysiumBrushSolidity::Trigger
			&& Brush->IsOverlappingActor(Pawn))
		{
			CurrentBrushes.Add(Brush->GetOwningEntity());
		}
	}
	for (const FTouchAnchorRecord& Record : TouchAnchors)
	{
		UPrimitiveComponent* Component = Record.Component.Get();
		if (Record.bEnabled && Component && Component->IsOverlappingActor(Pawn))
		{
			CurrentBrushes.Add(Record.Owner);
		}
	}
	EntityWorld->ReconcilePlayerTouches(CurrentBrushes);
	bPlayerTouchReconcilePending = false;
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

void AElysiumMapActor::RegisterUseAnchor(UPrimitiveComponent* Source,
	const FElysiumEntityHandle& OwnerHandle)
{
	if (!Source || !OwnerHandle.IsSet())
	{
		return;
	}
	for (const FUseAnchorRecord& Record : UseAnchors)
	{
		if (Record.Component.Get() == Source && Record.Owner == OwnerHandle)
		{
			return;
		}
	}

	UPrimitiveComponent* Anchor = Source;
	if (!Source->IsA<UElysiumBrushComponent>())
	{
		// A visual prop remains non-solid. Give only its rendered bounds a query body: this is a
		// target surface, not a proximity/action volume, and it follows the source component through
		// elevator attachment and animation transforms.
		const FBoxSphereBounds LocalBounds = Source->CalcBounds(FTransform::Identity);
		UBoxComponent* Proxy = NewObject<UBoxComponent>(this);
		Proxy->SetCanEverAffectNavigation(false);
		Proxy->SetMobility(EComponentMobility::Movable);
		Proxy->InitBoxExtent(LocalBounds.BoxExtent.ComponentMax(FVector(2.0f)));
		Proxy->SetupAttachment(Source);
		Proxy->SetRelativeLocation(LocalBounds.Origin);
		Proxy->SetRelativeRotation(FRotator::ZeroRotator);
		Proxy->SetCollisionObjectType(ECC_WorldDynamic);
		Proxy->SetCollisionResponseToAllChannels(ECR_Ignore);
		Proxy->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Block);
		Proxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Proxy->SetGenerateOverlapEvents(false);
		Proxy->RegisterComponent();
		AddInstanceComponent(Proxy);
		OwnedUseAnchorComponents.Add(Proxy);
		Anchor = Proxy;
	}

	FUseAnchorRecord& Record = UseAnchors.AddDefaulted_GetRef();
	Record.Component = Anchor;
	Record.Visual = Source;
	Record.Owner = OwnerHandle;
}

UPrimitiveComponent* AElysiumMapActor::FindUseVisual(
	const FElysiumEntityHandle& OwnerHandle) const
{
	const FUseAnchorRecord* Record = UseAnchors.FindByPredicate(
		[OwnerHandle](const FUseAnchorRecord& Candidate)
		{
			return Candidate.Owner == OwnerHandle && Candidate.bEnabled
				&& Candidate.Visual.IsValid();
		});
	return Record ? Record->Visual.Get() : nullptr;
}

void AElysiumMapActor::SetUseAnchorEnabled(const FElysiumEntityHandle& OwnerHandle, bool bEnabled)
{
	for (FUseAnchorRecord& Record : UseAnchors)
	{
		if (Record.Owner != OwnerHandle)
		{
			continue;
		}
		Record.bEnabled = bEnabled;
		UPrimitiveComponent* Component = Record.Component.Get();
		if (!Component)
		{
			continue;
		}
		if (OwnedUseAnchorComponents.Contains(Component))
		{
			Component->SetCollisionEnabled(
				bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
			continue;
		}
		// Brush slabs keep pawn/world collision. A disabled use owner (a knobbed door) must not
		// remain on ElysiumUse or the slab eats the exact ray and occludes its own knobs.
		Component->SetCollisionResponseToChannel(
			ELYSIUM_USE_CHANNEL, bEnabled ? ECR_Block : ECR_Ignore);
	}
}

void AElysiumMapActor::ClearUseAnchors()
{
	UseAnchors.Reset();
	for (UPrimitiveComponent* Component : OwnedUseAnchorComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	OwnedUseAnchorComponents.Reset();
}

void AElysiumMapActor::RegisterTouchAnchor(UPrimitiveComponent* Source,
	const FElysiumEntityHandle& OwnerHandle)
{
	if (!Source || !OwnerHandle.IsSet())
	{
		return;
	}
	const FBoxSphereBounds LocalBounds = Source->CalcBounds(FTransform::Identity);
	UBoxComponent* Proxy = NewObject<UBoxComponent>(this);
	Proxy->SetCanEverAffectNavigation(false);
	Proxy->SetMobility(EComponentMobility::Movable);
	Proxy->InitBoxExtent(LocalBounds.BoxExtent.ComponentMax(FVector(4.0f)));
	Proxy->SetupAttachment(Source);
	Proxy->SetRelativeLocation(LocalBounds.Origin);
	Proxy->SetRelativeRotation(FRotator::ZeroRotator);
	Proxy->SetCollisionObjectType(ECC_WorldDynamic);
	Proxy->SetCollisionResponseToAllChannels(ECR_Ignore);
	Proxy->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Proxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Proxy->SetGenerateOverlapEvents(true);
	Proxy->OnComponentBeginOverlap.AddDynamic(this, &AElysiumMapActor::HandleTouchAnchorBegin);
	Proxy->RegisterComponent();
	AddInstanceComponent(Proxy);
	OwnedTouchAnchorComponents.Add(Proxy);

	FTouchAnchorRecord& Record = TouchAnchors.AddDefaulted_GetRef();
	Record.Component = Proxy;
	Record.Owner = OwnerHandle;
}

void AElysiumMapActor::SetTouchAnchorEnabled(const FElysiumEntityHandle& OwnerHandle, bool bEnabled)
{
	for (FTouchAnchorRecord& Record : TouchAnchors)
	{
		if (Record.Owner != OwnerHandle)
		{
			continue;
		}
		Record.bEnabled = bEnabled;
		if (UPrimitiveComponent* Component = Record.Component.Get())
		{
			Component->SetCollisionEnabled(
				bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		}
	}
}

void AElysiumMapActor::ClearTouchAnchors()
{
	TouchAnchors.Reset();
	for (UPrimitiveComponent* Component : OwnedTouchAnchorComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	OwnedTouchAnchorComponents.Reset();
}

void AElysiumMapActor::HandleTouchAnchorBegin(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (!EntityWorld || OtherActor != ResolvePlayerPawn())
	{
		return;
	}
	const FTouchAnchorRecord* Record = TouchAnchors.FindByPredicate(
		[OverlappedComponent](const FTouchAnchorRecord& Candidate)
		{
			return Candidate.bEnabled && Candidate.Component.Get() == OverlappedComponent;
		});
	if (Record)
	{
		EntityWorld->RouteEntityTouch(
			Record->Owner, EntityWorld->PlayerHandle(), /*bBegin*/ true);
	}
}

FElysiumUseQueryResult AElysiumMapActor::QueryPlayerUse(
	const FElysiumEntityHandle& /*CurrentFocus*/) const
{
	// vampire.dll 0x10167470 → FindEntityFOV 0x10341c30. Eye + look (the boom is view-only).
	// 80u look-ray wins if the hit is a use anchor; else an 80u sphere ranked by view-dot with
	// AABB-closest-to-eye rescue against cos(player_use_arc=30°); else a 160u fallback ray.
	// Solvers are Unreal's: LineTraceSingleByChannel, OverlapMultiByObjectType, GetClosestPointTo.
	constexpr float UseReachCm = 80.0f * ElysiumMove::U;
	constexpr float FallbackReachCm = 160.0f * ElysiumMove::U;
	constexpr float MinDot = 0.86602540378f; // cos(30°)

	FElysiumUseQueryResult Result;
	UWorld* World = GetWorld();
	const APawn* Pawn = ResolvePlayerPawn();
	FVector Eye;
	if (!World || !Pawn || !GetPlayerUseOrigin(Eye))
	{
		return Result;
	}
	const FVector Forward = Pawn->GetViewRotation().Vector().GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return Result;
	}

	FCollisionQueryParams Params(FName(TEXT("ElysiumPlayerUse")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(Pawn);

	auto FindAnchor = [this](const UPrimitiveComponent* Component) -> const FUseAnchorRecord*
	{
		return UseAnchors.FindByPredicate([Component](const FUseAnchorRecord& Record)
		{
			return Record.bEnabled && Record.Component.Get() == Component;
		});
	};

	auto MakeCandidate = [](const FUseAnchorRecord& Record, const FVector& Point,
		EElysiumUseSelection Selection, float EyeDistance, float Dot) -> FElysiumUseCandidate
	{
		FElysiumUseCandidate Out;
		Out.Owner = Record.Owner;
		Out.AnchorPoint = Point;
		Out.Selection = Selection;
		Out.BodyDistance = EyeDistance;
		Out.CameraDistance = EyeDistance;
		Out.CameraDepth = EyeDistance;
		Out.AimError = 1.0f - Dot;
		return Out;
	};

	auto TraceUse = [World, &Eye, &Forward, &Params](float Length, FHitResult& Hit) -> bool
	{
		return World->LineTraceSingleByChannel(
			Hit, Eye, Eye + Forward * Length, ELYSIUM_USE_CHANNEL, Params);
	};

	auto VisibleTo = [World, &Eye, &Params, &FindAnchor](const FVector& Point,
		const FElysiumEntityHandle& Target) -> bool
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Eye, Point, ELYSIUM_USE_CHANNEL, Params))
		{
			return true;
		}
		const FUseAnchorRecord* Blocking = FindAnchor(Hit.GetComponent());
		return Blocking && Blocking->Owner == Target;
	};

	FHitResult RayHit;
	if (TraceUse(UseReachCm, RayHit))
	{
		if (const FUseAnchorRecord* Anchor = FindAnchor(RayHit.GetComponent()))
		{
			Result.Candidates.Add(MakeCandidate(*Anchor, RayHit.ImpactPoint,
				EElysiumUseSelection::Exact, FVector::Distance(Eye, RayHit.ImpactPoint), 1.0f));
			return Result;
		}
	}

	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Eye, FQuat::Identity, Objects,
		FCollisionShape::MakeSphere(UseReachCm), Params);

	const FUseAnchorRecord* BestAnchor = nullptr;
	FVector BestPoint = FVector::ZeroVector;
	float BestDot = MinDot;
	bool bSawOccluded = false;
	TSet<FElysiumEntityHandle> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const FUseAnchorRecord* Anchor = FindAnchor(Overlap.GetComponent());
		if (!Anchor || Seen.Contains(Anchor->Owner))
		{
			continue;
		}
		Seen.Add(Anchor->Owner);
		const UPrimitiveComponent* Component = Anchor->Component.Get();
		if (!Component)
		{
			continue;
		}
		const FBox Bounds = Component->Bounds.GetBox();
		const FVector Closest = Bounds.GetClosestPointTo(Eye);
		if (FVector::DistSquared(Eye, Closest) > FMath::Square(UseReachCm))
		{
			continue;
		}

		auto Consider = [&](const FVector& Point)
		{
			const FVector Delta = Point - Eye;
			const float Dist = Delta.Size();
			if (Dist <= KINDA_SMALL_NUMBER)
			{
				return;
			}
			const float Dot = FVector::DotProduct(Delta / Dist, Forward);
			if (Dot <= 0.0f || Dot <= BestDot)
			{
				return;
			}
			if (!VisibleTo(Point, Anchor->Owner))
			{
				bSawOccluded = true;
				return;
			}
			BestDot = Dot;
			BestAnchor = Anchor;
			BestPoint = Point;
		};

		Consider(Bounds.GetCenter());
		Consider(Closest);
	}

	if (BestAnchor)
	{
		Result.Candidates.Add(MakeCandidate(*BestAnchor, BestPoint,
			EElysiumUseSelection::Assisted, FVector::Distance(Eye, BestPoint), BestDot));
		return Result;
	}

	if (TraceUse(FallbackReachCm, RayHit))
	{
		if (const FUseAnchorRecord* Anchor = FindAnchor(RayHit.GetComponent()))
		{
			Result.Candidates.Add(MakeCandidate(*Anchor, RayHit.ImpactPoint,
				EElysiumUseSelection::Exact, FVector::Distance(Eye, RayHit.ImpactPoint), 1.0f));
			return Result;
		}
	}

	if (bSawOccluded)
	{
		Result.MissOutcome = EElysiumUseOutcome::Occluded;
	}
	return Result;
}

FElysiumEntityHandle AElysiumMapActor::QueryFeedTarget() const
{
	// `CBasePlayer::Replenish`'s direct victim search, reproduced in shape
	// (`docs/vtmb/feeding.md` § "Target acquisition and acceptance"): a hull trace from the body-eye
	// use origin toward the control-relative offset (32 forward, 0 right, -32 vertical) with extents
	// (-8,-8,-8)..(8,8,8). The retail figures are Source units and are converted once, here, by the
	// same `ElysiumMove::U` every other recovered distance in this runtime goes through.
	//
	// The recovered mask is `0x0201400b`. Source content masks are not portable to Unreal's channel
	// set, so the semantics are adapted rather than the number: candidacy is restricted to live
	// characters (the mask's player/NPC bits), and occlusion is tested on `ELYSIUM_USE_CHANNEL`
	// (its solid-world bits) — the same channel `+use` reaches the world through, and the one the
	// map's brush bodies and the walkable surface already answer on.
	// The standing hull a bodiless candidate is measured by: VtMB's own 32x32x72-unit character box.
	// Only reachable with `elysium.NpcBodies 0` or a failed model, where a rendered bound does not
	// exist; a standing body is measured by its own rendered bounds like every `+use` candidate is.
	constexpr float StandHalfWidthUnits = 16.0f;
	constexpr float StandHeightUnits = 72.0f;

	FVector UseOrigin;
	FVector IgnoredCapsuleCenter;
	FRotator ControlRotation;
	if (!EntityWorld || !GetPlayerUseOrigin(UseOrigin)
		|| !GetPlayerCapsuleTransform(IgnoredCapsuleCenter, ControlRotation))
	{
		return FElysiumEntityHandle::Invalid();
	}
	const ElysiumFeedTargeting::FProbe Probe =
		ElysiumFeedTargeting::MakeProbe(UseOrigin, ControlRotation);

	UWorld* World = GetWorld();
	FCollisionQueryParams Params(FName(TEXT("ElysiumFeedTarget")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(ResolvePlayerPawn());

	const FElysiumEntityHandle PlayerHandle = EntityWorld->PlayerHandle();
	FElysiumEntityHandle Best = FElysiumEntityHandle::Invalid();
	double BestDistanceSq = TNumericLimits<double>::Max();

	for (const TUniquePtr<FElysiumEntity>& EntPtr : EntityWorld->Entities())
	{
		FElysiumEntity* Ent = EntPtr.Get();
		if (!Ent || Ent->IsInert() || Ent->Handle == PlayerHandle || !Ent->AsCombatCharacter())
		{
			continue;
		}
		FBox Candidate(ForceInit);
		const USkeletalMeshComponent* CandidateBody = Ent->GetSkeletalBody();
		if (CandidateBody)
		{
			Candidate = CandidateBody->Bounds.GetBox();
		}
		else
		{
			const FVector Half(StandHalfWidthUnits * ElysiumMove::U,
				StandHalfWidthUnits * ElysiumMove::U, 0.0f);
			Candidate = FBox(Ent->Origin - Half,
				Ent->Origin + Half + FVector(0.0f, 0.0f, StandHeightUnits * ElysiumMove::U));
		}
		// Sweeping a box along a segment against an AABB is exactly a segment test against the AABB
		// grown by the hull's extents, so the recovered 16-cube is applied without a physics query.
		const FBox Swept = Candidate.ExpandBy(Probe.HullExtent);
		if (!FMath::LineBoxIntersection(Swept, Probe.Start, Probe.End, Probe.End - Probe.Start))
		{
			continue;
		}
		const FVector Chest = Candidate.GetCenter();
		if (World)
		{
			FHitResult Blocked;
			if (World->LineTraceSingleByChannel(Blocked, Probe.Start, Chest,
				ELYSIUM_USE_CHANNEL, Params)
				&& !ElysiumFeedTargeting::HitBelongsToCandidate(
					Blocked.GetComponent(), CandidateBody))
			{
				continue;   // a wall between the mouth and the neck
			}
		}
		const double DistanceSq = FVector::DistSquared(Probe.Start, Chest);
		if (DistanceSq < BestDistanceSq)
		{
			BestDistanceSq = DistanceSq;
			Best = Ent->Handle;
		}
	}
	return Best;
}

bool AElysiumMapActor::QueryLineOfSight(const FVector& FromCm, const FVector& ToCm) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		// No collision world to ask. The interface's stated headless answer is "clear", and this is
		// the same case reached through a live-but-unbuilt actor.
		return true;
	}
	// Degenerate segments are trivially clear, and a zero-length trace is not a query worth paying
	// for during a per-NPC sense pass.
	if (FromCm.Equals(ToCm))
	{
		return true;
	}
	// `ELYSIUM_USE_CHANNEL` is this project's solid-world channel: the walkable `.hulls` collider is
	// material-less and would be reported instead of the wall on the visibility channel, which is
	// exactly why `+use` has its own. Complex tracing is off — the brush bodies are convex hulls and
	// the query runs per NPC per sense pass.
	FCollisionQueryParams Params(FName(TEXT("ElysiumLineOfSight")), /*bTraceComplex*/ false);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, FromCm, ToCm, ELYSIUM_USE_CHANNEL, Params);
}

float AElysiumMapActor::QueryLightAtPoint(const FVector& PointCm) const
{
	// How many contributing sources are folded in before the answer is called good enough. A point
	// standing in more than this many overlapping authored radii is already saturated, so the cap
	// bounds the cost without changing the verdict. Nothing here allocates.
	constexpr int32 MaxContributors = 24;
	// The single-source intensity that reads as fully lit. `UElysiumLightRig::MaxBrightness` is the
	// calibrated ceiling one source is clipped to, so a point sitting at the centre of one
	// full-strength light is 1.0 and everything dimmer is a fraction of it.
	constexpr float MinReferenceIntensity = 0.01f;
	// `UElysiumLightRig`'s own source-type numbering: 3 is the sun/skylight directional.
	constexpr int32 SunSourceType = 3;

	const UElysiumMapVisuals* MapVisuals = GetVisuals();
	const UElysiumLightRig* Rig = MapVisuals ? MapVisuals->GetLightRig() : nullptr;
	if (Rig == nullptr)
	{
		return 1.0f;   // the stated headless answer: no rig, no darkness to claim
	}

	const float Reference = FMath::Max(Rig->MaxBrightness, MinReferenceIntensity);
	const TArray<UElysiumLightRig::FLightSource>& Sources = Rig->Sources();
	float Total = 0.0f;
	int32 Contributors = 0;
	for (int32 Index = 0; Index < Sources.Num() && Contributors < MaxContributors; ++Index)
	{
		const UElysiumLightRig::FLightSource& Source = Sources[Index];
		// A sky source lights the 3D-skybox miniature and never the playable world; the sun (type 3)
		// is a directional term with no position, and folding it in untraced would read every
		// interior as fully lit — the occlusion half of this query's stated divergence.
		//
		// A source switched off by hand in the Lights window is genuinely dark and is skipped. The
		// MASTER visibility toggle deliberately is not consulted: `elysium.lights 0` is a debug view
		// and must not change what an NPC perceives.
		if (Source.bSky || Source.Type == SunSourceType || Rig->IsSourceDisabled(Index))
		{
			continue;
		}
		const ULightComponent* Light = Source.Light.Get();
		if (Light == nullptr)
		{
			continue;
		}
		const float Reach = Source.RadiusCm > 1.0f ? Source.RadiusCm : Rig->FallbackRadiusCm;
		const float Distance = static_cast<float>(
			FVector::Dist(Light->GetComponentLocation(), PointCm));
		if (Reach <= 0.0f || Distance >= Reach)
		{
			continue;
		}
		// The rig's own falloff shape, not inverse-square: VtMB's authored light is nearly flat
		// inside its radius and stops at it, which is what `FalloffExponent` (1.0 today) encodes.
		const float Attenuation = FMath::Pow(1.0f - (Distance / Reach),
			FMath::Max(Rig->FalloffExponent, UE_KINDA_SMALL_NUMBER));
		Total += Source.BaseIntensity * Attenuation;
		++Contributors;
	}
	return FMath::Clamp(Total / Reference, 0.0f, 1.0f);
}

bool AElysiumMapActor::IsPlayerSneaking() const
{
	// The body's own settled posture, read off the same locomotion record the animation graph is
	// steered by — so "sneaking" is one fact with one producer rather than a second definition
	// living in the substrate. `Lowering` counts with `Ducked`: the duck is engaged the moment the
	// ramp starts, which is what the movement solve's own gait tables already branch on.
	const APawn* Pawn = ResolvePlayerPawn();
	const IElysiumPlayerBody* Body = Pawn ? Cast<IElysiumPlayerBody>(Pawn) : nullptr;
	if (Body == nullptr)
	{
		return false;   // no body, no posture — the stated non-stealth answer
	}
	const EElysiumStance Stance = Body->GetLocomotionSample().Stance;
	return Stance == EElysiumStance::Ducked || Stance == EElysiumStance::Lowering;
}

float AElysiumMapActor::ResolveNpcMakerGroundZ(const FVector& MakerOriginCm,
	float TraceDepthCm) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return MakerOriginCm.Z;
	}
	const FVector End = MakerOriginCm - FVector::UpVector * TraceDepthCm;
	FCollisionQueryParams Params(FName(TEXT("ElysiumNpcMakerGround")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(ResolvePlayerPawn());
	FHitResult Hit;
	return World->LineTraceSingleByChannel(Hit, MakerOriginCm, End, ELYSIUM_USE_CHANNEL, Params)
		? Hit.ImpactPoint.Z : End.Z;
}

bool AElysiumMapActor::IsNpcMakerVisibleFromPlayer(const FVector& MakerOriginCm) const
{
	FVector ViewLocation;
	FRotator ViewRotation;
	UWorld* World = GetWorld();
	if (!World || !GetPlayerViewPoint(ViewLocation, ViewRotation))
	{
		return false;
	}
	FCollisionQueryParams Params(FName(TEXT("ElysiumNpcMakerVisible")), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(ResolvePlayerPawn());
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(
		Hit, ViewLocation, MakerOriginCm, ELYSIUM_USE_CHANNEL, Params);
}

bool AElysiumMapActor::IsNpcMakerInPlayerViewCone(const FVector& MakerOriginCm) const
{
	FVector ViewLocation;
	FRotator ViewRotation;
	if (!GetPlayerViewPoint(ViewLocation, ViewRotation))
	{
		return false;
	}
	const FVector Local = ViewRotation.UnrotateVector(MakerOriginCm - ViewLocation);
	if (Local.X <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	float HorizontalFov = 90.0f;
	float Aspect = 16.0f / 9.0f;
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				HorizontalFov = PC->PlayerCameraManager->GetFOVAngle();
			}
			int32 SizeX = 0;
			int32 SizeY = 0;
			PC->GetViewportSize(SizeX, SizeY);
			if (SizeX > 0 && SizeY > 0)
			{
				Aspect = static_cast<float>(SizeX) / static_cast<float>(SizeY);
			}
		}
	}
	const float TanHalfHorizontal = FMath::Tan(FMath::DegreesToRadians(HorizontalFov * 0.5f));
	const float TanHalfVertical = TanHalfHorizontal / FMath::Max(Aspect, UE_KINDA_SMALL_NUMBER);
	return FMath::Abs(Local.Y / Local.X) <= TanHalfHorizontal
		&& FMath::Abs(Local.Z / Local.X) <= TanHalfVertical;
}

bool AElysiumMapActor::IsNpcMakerSpawnAreaOccupied(const FVector& GroundOriginCm,
	float HalfExtentCm) const
{
	// Native enumerates solid entities through a 2D 68-unit square at the cached ground. A thin
	// plane is sufficient here: any standing body that owns that ground point crosses it.
	const FBox SpawnArea(
		GroundOriginCm - FVector(HalfExtentCm, HalfExtentCm, 1.0f),
		GroundOriginCm + FVector(HalfExtentCm, HalfExtentCm, 1.0f));
	if (const APawn* Pawn = ResolvePlayerPawn())
	{
		if (SpawnArea.Intersect(Pawn->GetComponentsBoundingBox(/*bNonColliding*/ false)))
		{
			return true;
		}
	}
	if (!EntityWorld)
	{
		return false;
	}
	for (const TUniquePtr<FElysiumEntity>& EntPtr : EntityWorld->Entities())
	{
		const FElysiumEntity* Ent = EntPtr.Get();
		if (!Ent || Ent->IsInert() || Ent->Handle == EntityWorld->PlayerHandle())
		{
			continue;
		}
		FBox Bounds(ForceInit);
		if (const USkeletalMeshComponent* Skeletal = Ent->GetSkeletalBody())
		{
			if (Skeletal->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
			{
				Bounds = Skeletal->Bounds.GetBox();
			}
		}
		else if (Ent->Body && Ent->Body->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			Bounds = Ent->Body->Bounds.GetBox();
		}
		else if (Ent->AsCombatCharacter())
		{
			Bounds = FBox(
				Ent->Origin - FVector(ElysiumMove::HullHalfWidth, ElysiumMove::HullHalfWidth, 0.0f),
				Ent->Origin + FVector(ElysiumMove::HullHalfWidth, ElysiumMove::HullHalfWidth,
					ElysiumMove::StandHeight));
		}
		if (Bounds.IsValid && SpawnArea.Intersect(Bounds))
		{
			return true;
		}
	}
	return false;
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

void AElysiumMapActor::SetEquippedCameraClass(int32 CameraClass)
{
	if (UElysiumCameraComponent* Camera = PlayerCamera())
	{
		Camera->SetEquippedCameraClass(CameraClass);
	}
}

FElysiumVoiceHandle AElysiumMapActor::Submit(FElysiumAudioRequest Request)
{
	Request.Owner.MapEpoch = MapEpoch;
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
	AudioOwner.MapEpoch = MapEpoch;
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

	// Authored Source origins are feet. The readiness poll adds the active body's exact half-height
	// once the pawn exists; keeping the logical placement in feet avoids a magic 100 cm lift.
	OutLocation = Origin;
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

	// New feet = destination landmark origin + the Source-feet offset captured at the source.
	PendingSpawnLoc = Lm->Def->Origin + Offset;
	if (bHasYaw)
	{
		PendingSpawnYaw = Yaw;   // preserve the player's view yaw across the transition
	}
	else
	{
		PendingSpawnYaw = -Lm->Angles.Y;   // face the landmark's angles (Source yaw negated to Unreal)
	}
	PendingSpawnSpace = EElysiumPlayerPlacementSpace::Feet;
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
	PendingSpawnSpace = EElysiumPlayerPlacementSpace::CapsuleCenter; // legacy save payload contract
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

	// The substrate owns plain C++ entities, but its teardown reaches map-owned UObjects through the
	// embodiment seam (interaction anchors, bodies and scripted cameras). Run that teardown while
	// EndPlay still guarantees those objects have valid UObject indices; waiting for this actor's C++
	// destructor is too late because world cleanup may already have reclaimed its components.
	EntityWorld.Reset();

	// The scheme manager is this actor's own, so its voices are stopped here rather than at the
	// epoch boundary below — it will not exist to be asked once this actor is gone.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumAudioSubsystem* Audio = GI->GetSubsystem<UElysiumAudioSubsystem>())
		{
			if (SchemeManager)
			{
				SchemeManager->StopAll(Audio);
			}
		}
	}

	// Close this map's epoch (S4). Every application-lifetime object holding state on this map's
	// behalf — voices, camera requests, the character stage's actors, debug NPC bodies, the level
	// script's path entry — frees it from this one broadcast. It happens here, and not in this
	// actor's destructor, because the world is still standing: a subscriber may destroy actors and
	// components rather than merely dropping references to them.
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		Maps->RetireMapEpoch(MapEpoch);
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
				// A runtime teleport performed during the prior event pass moved the body immediately but
				// deliberately left its touch links dirty. Movement has now had its retail opportunity;
				// settle the final containment before thinks and queued output delivery.
				if (bPlayerTouchReconcilePending && FElysiumEntityWorld::IsTriggerResolutionEnabled())
				{
					ReconcilePlayerBrushTouches(ResolvePlayerPawn());
				}
				EntityWorld->Tick(GameState->GameClock().GetNow());
			}

			TickAudio(DeltaSeconds);
			TickWeatherPresentation();
		}
	}
}

void AElysiumMapActor::ApplyWetness(const FElysiumWeatherTransition& Transition)
{
	WetnessTransition = Transition;
	ApplyWeatherTuning();
}

namespace
{
bool IsFollowRainDefinition(const FString& Definition)
{
	return Definition.Equals(TEXT("rain_follow_emitter"), ESearchCase::IgnoreCase);
}
}

void AElysiumMapActor::ApplyEmitter(const FElysiumWeatherEmitterState& Emitter)
{
	// One viewer-volume system for every rain_follow_emitter. Two hub entities share it.
	if (IsFollowRainDefinition(Emitter.ParticleDefinition))
	{
		if (!Emitter.bActive)
		{
			RainEmitterStates.Remove(Emitter.Entity.Index);
			RefreshFollowRain();
			return;
		}
		RainEmitterStates.Add(Emitter.Entity.Index, Emitter);
		RefreshFollowRain();
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
			const FString Failure = FString::Printf(TEXT("system|%d|%s"),
				Emitter.Entity.Index, *Emitter.ParticleDefinition.ToLower());
			if (!ReportedEmitterFailures.Contains(Failure))
			{
				ReportedEmitterFailures.Add(Failure);
				UE_LOG(LogElysium, Warning,
					TEXT("particle emitter %d on map '%s' cannot load definition '%s' from '%s'"),
					Emitter.Entity.Index, *MapName, *Emitter.ParticleDefinition, *Path);
			}
			return;
		}
		Component = NewObject<UNiagaraComponent>(this);
		if (!Component)
		{
			const FString Failure = FString::Printf(TEXT("component|%d"), Emitter.Entity.Index);
			if (!ReportedEmitterFailures.Contains(Failure))
			{
				ReportedEmitterFailures.Add(Failure);
				UE_LOG(LogElysium, Warning,
					TEXT("particle emitter %d ('%s') could not create a Niagara component on map '%s'"),
					Emitter.Entity.Index, *Emitter.ParticleDefinition, *MapName);
			}
			return;
		}
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

// `attach_type` 1 is `tree`: preserve the authored parent offset while following the named root.
// `attach_type` 2 is `point`: snap to a named bone (or the body root when the bone is absent).
// Parents resolve here rather than at PostSpawn because cinematic receivers may be made after map
// activation.
void AElysiumMapActor::AttachEmitter(
	const FElysiumWeatherEmitterState& Emitter, UNiagaraComponent* Component)
{
	if (!Component)
	{
		return;
	}
	auto WarnAttachmentOnce = [this, &Emitter](const TCHAR* Kind, const FString& Message)
	{
		const FString Failure = FString::Printf(TEXT("attach|%s|%d"), Kind, Emitter.Entity.Index);
		if (!ReportedEmitterFailures.Contains(Failure))
		{
			ReportedEmitterFailures.Add(Failure);
			UE_LOG(LogElysium, Warning, TEXT("%s"), *Message);
		}
	};
	USceneComponent* ParentBody = nullptr;
	FElysiumEntity* ParentEntity = nullptr;
	const bool bWantsParent = Emitter.AttachType == 1 || Emitter.AttachType == 2;
	if (bWantsParent && !Emitter.ParentName.IsEmpty() && EntityWorld)
	{
		ParentEntity = EntityWorld->FindByName(Emitter.ParentName);
		if (ParentEntity)
		{
			ParentBody = ParentEntity->GetAttachBody();
		}
		if (!ParentBody)
		{
			const FString Failure = FString::Printf(TEXT("parent|%d|%s"),
				Emitter.Entity.Index, *Emitter.ParentName.ToLower());
			if (!ReportedEmitterFailures.Contains(Failure))
			{
				ReportedEmitterFailures.Add(Failure);
				UE_LOG(LogElysium, Warning,
					TEXT("particle emitter %d ('%s') cannot attach to parent '%s' on map '%s'; using map root"),
					Emitter.Entity.Index, *Emitter.ParticleDefinition, *Emitter.ParentName, *MapName);
			}
		}
	}
	if (!ParentBody)
	{
		if (!Component->AttachToComponent(GetRootComponent(),
			FAttachmentTransformRules::KeepRelativeTransform))
		{
			WarnAttachmentOnce(TEXT("root"), FString::Printf(
				TEXT("particle emitter %d ('%s') could not attach to the map root"),
				Emitter.Entity.Index, *Emitter.ParticleDefinition));
		}
		Component->SetRelativeLocation(Emitter.LocationCm);
		return;
	}
	if (Emitter.AttachType == 1)
	{
		// Retail parents these at map setup, before the cinematic moves its actor. Components are
		// created lazily here, so reconstruct that same authored offset against the parent's live
		// position before establishing the persistent component attachment.
		FVector WorldLocation = Emitter.LocationCm;
		if (ParentEntity && ParentEntity->Def)
		{
			WorldLocation += ParentEntity->Origin - ParentEntity->Def->Origin;
		}
		if (!Component->AttachToComponent(ParentBody, FAttachmentTransformRules::KeepWorldTransform))
		{
			WarnAttachmentOnce(TEXT("tree"), FString::Printf(
				TEXT("particle emitter %d ('%s') could not tree-attach to parent '%s'"),
				Emitter.Entity.Index, *Emitter.ParticleDefinition, *Emitter.ParentName));
		}
		Component->SetWorldLocation(WorldLocation);
		return;
	}
	// VtMB bone names carry spaces (`Bip01 Neck`) and survive the glTF export unchanged, so the
	// authored name is used verbatim. A body that has not got the bone falls back to its root.
	const FName Bone(*Emitter.AttachBone);
	const bool bHasBone = !Emitter.AttachBone.IsEmpty()
		&& ParentBody->DoesSocketExist(Bone);
	if (!Component->AttachToComponent(ParentBody,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale, bHasBone ? Bone : NAME_None))
	{
		WarnAttachmentOnce(TEXT("point"), FString::Printf(
			TEXT("particle emitter %d ('%s') could not point-attach to parent '%s'"),
			Emitter.Entity.Index, *Emitter.ParticleDefinition, *Emitter.ParentName));
	}
	Component->SetRelativeLocation(FVector::ZeroVector);
	if (!bHasBone && !Emitter.AttachBone.IsEmpty())
	{
		const FString Failure = FString::Printf(TEXT("bone|%d|%s|%s"), Emitter.Entity.Index,
			*Emitter.ParentName.ToLower(), *Emitter.AttachBone.ToLower());
		if (!ReportedEmitterFailures.Contains(Failure))
		{
			ReportedEmitterFailures.Add(Failure);
			UE_LOG(LogElysium, Warning,
				TEXT("particle emitter %d ('%s') cannot find bone '%s' on parent '%s'; using body root"),
				Emitter.Entity.Index, *Emitter.ParticleDefinition,
				*Emitter.AttachBone, *Emitter.ParentName);
		}
	}
}

void AElysiumMapActor::RemoveEmitter(const FElysiumEntityHandle& Entity)
{
	RainEmitterStates.Remove(Entity.Index);
	if (TObjectPtr<UNiagaraComponent> Component; RainComponents.RemoveAndCopyValue(Entity.Index, Component))
	{
		if (Component) { Component->DestroyComponent(); }
	}
	RefreshFollowRain();
}

void AElysiumMapActor::TickWeatherPresentation()
{
	if (GPendingWeatherTimer.IsSet())
	{
		const bool bRainOn = GPendingWeatherTimer.GetValue();
		GPendingWeatherTimer.Reset();
		FireWeatherTimer(bRainOn);
	}
	RefreshFollowRain();
	UpdateFollowRainLocation();
	ApplyWeatherTuning();
}

void AElysiumMapActor::UpdateFollowRainLocation()
{
	if (!RainFollowComponent)
	{
		return;
	}
	FVector Location;
	FRotator Rotation;
	if (GetPlayerViewPoint(Location, Rotation))
	{
		RainFollowComponent->SetWorldLocation(Location);
		RainFollowComponent->SetVariablePosition(TEXT("User.SpawnCenter"), Location);
	}
}

void AElysiumMapActor::RefreshFollowRain()
{
	float Rate = 0.0f;
	float Bounds = 0.0f;
	bool bAny = false;
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : RainEmitterStates)
	{
		if (!IsFollowRainDefinition(Pair.Value.ParticleDefinition))
		{
			continue;
		}
		bAny = true;
		Rate = FMath::Max(Rate, Pair.Value.RateScale);
		Bounds = FMath::Max(Bounds, Pair.Value.BoundsCm);
	}
	if (CVarRainForce.GetValueOnGameThread() != 0)
	{
		bAny = true;
		Rate = FMath::Max(Rate, 1.0f);
	}
	const float ParticleRate = FMath::Max(0.0f, CVarRainRateScale.GetValueOnGameThread());
	Rate *= ParticleRate;
	if (!bAny || Rate <= KINDA_SMALL_NUMBER)
	{
		if (RainFollowComponent)
		{
			RainFollowComponent->Deactivate();
		}
		return;
	}
	if (!RainFollowComponent)
	{
		if (!RainSystem)
		{
			RainSystem = LoadObject<UNiagaraSystem>(nullptr,
				TEXT("/Game/VtMB/Particles/NS_ElysiumRain.NS_ElysiumRain"));
		}
		if (!RainSystem)
		{
			const FString Failure = TEXT("follow|system");
			if (!ReportedEmitterFailures.Contains(Failure))
			{
				ReportedEmitterFailures.Add(Failure);
				UE_LOG(LogElysium, Warning,
					TEXT("rain_follow_emitter on map '%s' cannot load /Game/VtMB/Particles/NS_ElysiumRain"),
					*MapName);
			}
			return;
		}
		RainFollowComponent = NewObject<UNiagaraComponent>(this);
		if (!RainFollowComponent)
		{
			const FString Failure = TEXT("follow|component");
			if (!ReportedEmitterFailures.Contains(Failure))
			{
				ReportedEmitterFailures.Add(Failure);
				UE_LOG(LogElysium, Warning,
					TEXT("rain_follow_emitter on map '%s' could not create a Niagara component"),
					*MapName);
			}
			return;
		}
		RainFollowComponent->SetAsset(RainSystem);
		RainFollowComponent->SetAutoActivate(false);
		RainFollowComponent->SetupAttachment(GetRootComponent());
		RainFollowComponent->RegisterComponent();
		AddInstanceComponent(RainFollowComponent);
		UE_LOG(LogElysium, Log,
			TEXT("follow rain created on '%s' system=%s"),
			*MapName, *RainSystem->GetPathName());
	}
	if (Bounds <= KINDA_SMALL_NUMBER)
	{
		Bounds = 1200.0f;
	}
	RainFollowComponent->SetVariableFloat(TEXT("User.RateScale"), Rate);
	RainFollowComponent->SetVariableFloat(TEXT("User.BoundsCm"), Bounds);
	RainFollowComponent->SetVariableFloat(TEXT("User.LightResponse"), 1.0f);
	RainFollowComponent->SetVariableFloat(TEXT("User.StreakWidth"),
		FMath::Max(0.2f, CVarRainStreakWidth.GetValueOnGameThread()));
	RainFollowComponent->SetVariableFloat(TEXT("User.StreakLength"),
		FMath::Max(4.0f, CVarRainStreakLength.GetValueOnGameThread()));
	RainFollowComponent->SetVariableFloat(TEXT("User.StreakAlpha"),
		FMath::Clamp(CVarRainStreakAlpha.GetValueOnGameThread(), 0.0f, 1.0f));
	UpdateFollowRainLocation();
	if (!RainFollowComponent->IsActive())
	{
		RainFollowComponent->Activate();
	}
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
	const float ParticleRate = FMath::Max(0.0f, CVarRainRateScale.GetValueOnGameThread());
	const float LightResponse = FMath::Clamp(
		CVarRainLightResponse.GetValueOnGameThread() * 4.0f, 0.0f, 2.0f);
	for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : RainEmitterStates)
	{
		if (UNiagaraComponent* Component = RainComponents.FindRef(Pair.Key))
		{
			Component->SetVariableFloat(TEXT("User.RateScale"), Pair.Value.RateScale * ParticleRate);
			Component->SetVariableFloat(TEXT("User.LightResponse"), LightResponse);
		}
	}
}

bool AElysiumMapActor::IsFollowRainActive() const
{
	return RainFollowComponent && RainFollowComponent->IsActive();
}

FVector AElysiumMapActor::GetFollowRainLocation() const
{
	return RainFollowComponent ? RainFollowComponent->GetComponentLocation() : FVector::ZeroVector;
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
	FString Result = FString::Printf(
		TEXT("wet authored %.3f->%.3f presented %.3f x%.2f override=%d components %d follow=%d force=%d"),
		WetnessTransition.CurrentWetness, WetnessTransition.TargetWetness,
		PresentedWetness, PresentedWetnessScale, bEnvironmentWetnessOverride ? 1 : 0,
		RainComponents.Num(),
		RainFollowComponent && RainFollowComponent->IsActive() ? 1 : 0,
		CVarRainForce.GetValueOnGameThread());
	if (RainFollowComponent)
	{
		Result += FString::Printf(TEXT(" follow_loc=%s"),
			*RainFollowComponent->GetComponentLocation().ToCompactString());
	}
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
	P.bAnimationPreloadReady = bAnimationPreloadReady;
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
			const FVector Placement = ElysiumPlayerPlacement::ToCapsuleCenter(
				PendingSpawnLoc, PendingSpawnSpace, Body->GetBodyHalfHeight());
			Pawn->SetActorLocation(Placement, false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation(FRotator(0.f, PendingSpawnYaw, 0.f));
			bSpawnPlaced = true;
			EnsureTickPrerequisites();
			UE_LOG(LogElysium, Log, TEXT("map runtime %s: player placed and frozen at %s"),
				*MapName, *Placement.ToString());
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

	const FTransform BoundsTransform(FRotator::ZeroRotator, Center);
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.OverrideLevel = GetLevel();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	NavigationBounds = World->SpawnActor<ANavMeshBoundsVolume>(
		ANavMeshBoundsVolume::StaticClass(), BoundsTransform, Params);
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
	NavigationBounds->FinishSpawning(BoundsTransform);
	if (!BoundsBox->IsRegistered())
	{
		BoundsBox->RegisterComponent();
	}

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
			// A stage world has no walkable surface at all, so releasing the pawn drops it out of the
			// level for as long as the green room is open. It stays frozen where the spawn seated it;
			// the view belongs to the camera shot stack, which does not consult the pawn's feet.
			Body->SetMovementFrozen(bStageOnly);
		}
	}
	PreMoveTickFunction.bTickEvenWhenPaused = false;
	PrimaryActorTick.bTickEvenWhenPaused = false;
	GameplayTickFunction.bTickEvenWhenPaused = false;

	// Everything this map places is standing by now — the adopted baked actors and the NPC bodies
	// the entity world built — so this is the one point where the level's material bindings can be
	// read whole. A body spawned after activation is not covered; the audit is a load-time listing,
	// not a live watch.
	if (Visuals)
	{
		Visuals->AuditMaterials(MapName);
	}

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

void AElysiumMapActor::TickGaze(float DeltaSeconds)
{
	if (EntityWorld == nullptr || Bodies == nullptr)
	{
		return;
	}
	const UGameInstance* GI = GetGameInstance();
	UElysiumRulebookSubsystem* Rules = GI
		? const_cast<UGameInstance*>(GI)->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;
	const float Now = EntityWorld->NowSeconds();

	// `DialogPOV` is a property of the shot in effect, not of any one character, so it resolves once
	// per frame. It is the dominant look-at surface in the game: 51 of the 66 shipped shot files set
	// it, against 40 authored `LookAtEntity*` wires in the whole of VtMB.
	FVector DialogPovValue = FVector::ZeroVector;
	const FVector* DialogPovPoint = nullptr;
	if (EntityWorld->GetDialogueCameraGaze(DialogPovValue))
	{
		DialogPovPoint = &DialogPovValue;
	}
	else if (CameraDirector && CameraDirector->WantsDialogPOV())
	{
		if (const UElysiumCameraComponent* Cam = PlayerCamera())
		{
			DialogPovValue = Cam->GetComponentLocation();
			DialogPovPoint = &DialogPovValue;
		}
	}

	for (const TUniquePtr<FElysiumEntity>& Entity : EntityWorld->Entities())
	{
		FElysiumEntity* Raw = Entity.Get();
		if (Raw == nullptr || Raw->IsInert())
		{
			continue;
		}
		FElysiumCombatCharacter* Character = Raw->AsCombatCharacter();
		USkeletalMeshComponent* Body = Raw->GetSkeletalBody();
		if (Character == nullptr || Body == nullptr)
		{
			continue;
		}
		// Retail runs the whole eye path per *drawn* model, so an unseen character neither aims nor
		// fidgets. Skipping here rather than inside the eye pass also keeps the cascade's own cost
		// off the frame, which is the half that walks the entity list.
		if (!Body->WasRecentlyRendered(0.2f))
		{
			continue;
		}
		// The head frame the cone and the fidget grid are measured in, with retail's own fallback to
		// EyePosition()/EyeAngles() when the model has no head bone.
		FVector HeadPos = FVector::ZeroVector;
		FVector HeadForward = FVector::ZeroVector;
		if (!Bodies->GetHeadFrame(Body, HeadPos, HeadForward))
		{
			HeadPos = Character->EyePosition();
			HeadForward = FRotator(0.f, Character->Angles.Y, 0.f).Vector();
		}

		// Every rate and interval is content. A character whose disposition does not resolve gets
		// the table's own `Neutral`, which is what the table says it should.
		FElysiumEyeTargetTuning Tuning;
		if (Rules != nullptr)
		{
			if (const FElysiumDisposition* Row = Rules->Dispositions().Resolve(
				Character->Disposition, Character->DispositionLevel))
			{
				Tuning = Row->EyeTarget;
			}
		}
		const FVector Smoothed =
			Character->TickGaze(Now, DeltaSeconds, HeadPos, HeadForward, Tuning, DialogPovPoint);
		Bodies->SetViewTarget(Body, Smoothed);
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
	// Settle camera/body interaction focus and consume this frame's queued command edges. The query
	// belongs after physics: before the pawn's move it would target last frame's geometry, which
	// reads as a door you cannot use until you stop walking. Focus outputs enqueue against the same
	// `now` advanced by the pre-move pass and service on the next frame like any zero-delay wire.
	if (EntityWorld)
	{
		EntityWorld->UpdatePlayerInteraction();
		// The feed request is acquired against the same settled frame the use focus is, and for the
		// same reason: retail's victim search is a trace off the player's final view position.
		EntityWorld->UpdatePlayerFeed();
	}

	// 11.7 — re-resolve every `Follow` camera shot against this frame's final entity positions. Same
	// reason as the use cursor: a shot framed on where an NPC *was* reads as a camera that lags the
	// subject it is supposed to be locked onto.
	if (CameraDirector)
	{
		CameraDirector->Tick(EntityWorld.Get(), PlayerCamera());
	}
	if (EntityWorld)
	{
		EntityWorld->RefreshDialogueCamera();
	}

	// 12.4 — decide where each character is looking, then rebuild each eye's basis against this
	// frame's settled pose and publish it to the material. Both halves are here for the same reason
	// as the two above, and for one of their own: the head-bone transform the cascade measures its
	// cone in, and the bone transforms the eye pass reads, are only stable once the frame's parallel
	// animation evaluation has completed, which at this tick group it has.
	//
	// The gaze runs from this pass rather than from NextThink deliberately: NextThink is a
	// single-slot scheduler already shared with patrol, ambient and scripted move on FElysiumNpc,
	// and a per-frame gaze update would fight ThinkPatrol's 0.05 s cadence for the slot.
	TickGaze(DeltaSeconds);
	if (Bodies)
	{
		Bodies->TickEyes(DeltaSeconds);
	}

	// CCC4 — the frame's animation selection, taken from the body sample the mover published at its
	// tick tail. It belongs in this pass for the reason the three above do: the sample is settled only
	// after the last stepper substep, and a selection read before that is a selection made from a
	// half-integrated frame (`docs/architecture/animation-architecture.md` section 3.2).
	TickPlayerAnimation(DeltaSeconds);

	// The tail of a released frame: a dev step spends one here, and the last one re-holds the world.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameStateSubsystem* GameState = GI->GetSubsystem<UElysiumGameStateSubsystem>())
		{
			GameState->TimeControl().EndFrame();
		}
	}
}
