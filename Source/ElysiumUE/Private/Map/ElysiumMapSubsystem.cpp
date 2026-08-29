#include "ElysiumMapSubsystem.h"

#include "ElysiumContentPaths.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumPlayerBody.h"
#include "Debug/ElysiumGreenRoomConsole.h"
#include "Debug/ElysiumCastRun.h"
#include "Debug/ElysiumComposeRun.h"
#include "Debug/ElysiumGreenRoomRun.h"
#include "Debug/ElysiumMoveRun.h"
#include "Debug/ElysiumProbeRun.h"
#include "Debug/ElysiumProfiler.h"
#include "Debug/ElysiumShotRun.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMap, Log, All);

// The game boots into /Game/ElysiumGenerated/Boot (Config/DefaultEngine.ini GameDefaultMap), an
// empty UWorld it sits in until the first Travel. Map travel does not come back through it — each
// VtMB map is its own baked .umap under the /ElysiumBaked mount, and Travel opens that level
// directly.

void UElysiumMapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if !UE_BUILD_SHIPPING
	// The green room's own verb set. Registered for the whole session rather than with the lab: they
	// must be callable before one is armed in order to report that none is.
	GreenRoomConsole = MakePimpl<FElysiumGreenRoomConsole>(this);
#endif

	// Engine-console mirrors of the dev-console commands, handy for -ExecCmds automation.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.map"),
		TEXT("elysium.map <name>|next [landmark] — travel to an exported VtMB map (optional landmark = "
			"spawn at that info_landmark instead of info_player_start; the P4.6 direct-entry path)"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumMap, Display, TEXT("current map: %s"), *GetCurrentMapName());
				return;
			}
			const FString Target = (Args[0] == TEXT("next")) ? NextMapName() : Args[0];
			const FString Landmark = (Args.Num() > 1) ? Args[1] : FString();

			UGameInstance* GI = GetGameInstance();
			UElysiumGameStateSubsystem* GameState = GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
			if (GameState && !FElysiumSheet::IsValidClan(GameState->PlayerRecord().Sheet.Clan()))
			{
				if (ExportedMaps().Contains(Target))
				{
					if (UElysiumGameFlowSubsystem* Flow = GI->GetSubsystem<UElysiumGameFlowSubsystem>())
					{
						Flow->SeedNewGameState(ElysiumStory::MakeMockCharacterRequest(FString()));
					}
				}
			}

			Travel(Target, Landmark);
		}),
		ECVF_Cheat));

	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.maps"),
		TEXT("elysium.maps — list exported VtMB maps"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			for (const FString& Name : ExportedMaps())
			{
				UE_LOG(LogElysiumMap, Display, TEXT("  %s%s"), *Name,
					Name == GetCurrentMapName() ? TEXT("   <- current") : TEXT(""));
			}
		}),
		ECVF_Cheat));

	// elysium.reload — re-Travel the current map: the recook-free hot loop paired with the
	// pipeline (edit exporter -> re-export -> reload in the running game). No-op with no map.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.reload"),
		TEXT("elysium.reload — reload the current map (export->reload hot loop)"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]() { Reload(); }),
		ECVF_Cheat));

	// Log the current camera as a paste-ready GProfileCams row, so a new profiling vantage
	// (with exact pitch, which the HUD omits) can be captured by flying there and running this.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.campos"),
		TEXT("elysium.campos — log current camera as a GProfileCams row (for the headless profiler)"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			if (!PC)
			{
				return;
			}
			FVector Loc; FRotator Rot;
			PC->GetPlayerViewPoint(Loc, Rot);
			UE_LOG(LogElysiumMap, Display,
				TEXT("{ TEXT(\"%s\"), TEXT(\"camN\"), false, FVector(%.0ff, %.0ff, %.0ff), FRotator(%.1ff, %.1ff, %.1ff) },"),
				*GetCurrentMapName(), Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll);
		}),
		ECVF_Cheat));

	// The same capture for a movement course's start. `elysium.campos` logs the *camera*, and a
	// course start is the **body's feet** — the pawn's own origin is its box centre, so a coordinate
	// read off either the view or the actor is 36 units out. Fly to a staircase, run this, paste.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.playerpos"),
		TEXT("elysium.playerpos — log the player's feet as a sited-course row (for the move harness)"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!Pawn)
			{
				return;
			}
			const IElysiumPlayerBody* Body = Cast<const IElysiumPlayerBody>(Pawn);
			const float HalfHeight = Body ? Body->GetBodyHalfHeight() : 0.0f;
			const FVector Feet = Pawn->GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);
			UE_LOG(LogElysiumMap, Display,
				TEXT("elysium.playerpos: FVector(%.1ff, %.1ff, %.1ff), %.1ff  // %s, feet"),
				Feet.X, Feet.Y, Feet.Z, PC->GetControlRotation().Yaw, *GetCurrentMapName());
		}),
		ECVF_Cheat));

	// Under -ElysiumProfile, arm the headless profiling harness. It self-drives once the
	// boot map settles, captures each configured vantage, writes a summary, and exits.
	if (FElysiumProfileRun::IsRequested())
	{
		ProfileRun = MakePimpl<FElysiumProfileRun>(this);
	}
	// Under -ElysiumShots, arm the headless screenshot-regression harness (same vantages).
	if (FElysiumShotRun::IsRequested())
	{
		ShotRun = MakePimpl<FElysiumShotRun>(this);
	}
	// Under -ElysiumGreenRoom, audition one body/bank pair (or the opening ensemble) on an
	// isolated rendered stage before an aggregate theatre run is allowed.
	if (FElysiumGreenRoomRun::IsRequested())
	{
		GreenRoomRun = MakePimpl<FElysiumGreenRoomRun>(this);
	}
#if !UE_BUILD_SHIPPING
	// Under -ElysiumProbe, arm the headless light-attribution probe (one map per launch).
	if (FElysiumProbeRun::IsRequested())
	{
		ProbeRun = MakePimpl<FElysiumProbeRun>(this);
	}
	// Under -ElysiumMove, arm the headless movement-regression run: a fixed command stream
	// per course, sampled against real geometry.
	if (FElysiumMoveRun::IsRequested())
	{
		MoveRun = MakePimpl<FElysiumMoveRun>(this);
	}
	// Under -ElysiumCast, arm the headless cast-locomotion run: the same body trace from the other
	// producer, over the arena in the stage world.
	if (FElysiumCastRun::IsRequested())
	{
		CastRun = MakePimpl<FElysiumCastRun>(this);
	}
	// Under -ElysiumCompose, arm the headless composed-pose run: a driven body on a real map, and
	// the pose its graph produced written down per frame.
	if (FElysiumComposeRun::IsRequested())
	{
		ComposeRun = MakePimpl<FElysiumComposeRun>(this);
	}
#endif
}

bool UElysiumMapSubsystem::EnterGreenRoom(FString& OutError)
{
	// A green room armed from the command line — a lab or a capture run — already owns the harness,
	// and the stage world is simply the world it runs in. Only a session that has none gets one armed
	// here, which is the `elysium.gr` case. Arming happens before anything is torn down so its
	// failure modes (no RHI) are reported while the current map is still standing. The run is owned
	// by this GI-scoped subsystem and re-reads the current map every tick, so it survives the travel
	// below and picks the stage actor up when it is ready.
	//
	// It is separate from the travel because the stage world has a second caller with no interest in
	// the lab at all: the movement gym builds its own geometry into the same empty level and runs
	// under `-nullrhi`, where arming the lab would refuse outright.
	if (!GreenRoomRun.IsValid() && EnsureGreenRoomLab(OutError) == nullptr)
	{
		return false;
	}
	return EnterStageWorld(OutError);
}

bool UElysiumMapSubsystem::EnterStageWorld(FString& OutError)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		OutError = TEXT("no current world");
		return false;
	}
	if (const AElysiumMapActor* Map = CurrentMap.Get())
	{
		if (Map->IsStageOnly())
		{
			return true;   // already standing in one; whatever armed it has just re-armed over it
		}
	}

	// The stage wants the same empty shell the front end stands in, and it can be built in place
	// there — but only if this world already seated a pawn. The front end refuses one (the game mode
	// returns no pawn class while IsMenuBackdrop), and nothing but a fresh world spawns one, so
	// entering from the menu re-opens the shell rather than building into a world with no pawn for
	// the activation barrier to wait on.
	const FString ShellPackage = FElysiumContentPaths::BootMount();
	const bool bInShell =
		World->GetOutermost()->GetName().Equals(ShellPackage, ESearchCase::IgnoreCase);
	const bool bWorldHasPawn = !bCurrentIsMenuBackdrop;

	PendingMapLoad = FPendingMapLoad{ true, FString(), FString(), false, /*bStageOnly*/ true };
	bCurrentIsMenuBackdrop = false;
	// None of the three one-shots means anything without a map to resolve it against, and leaving any
	// of them armed would leak it onto whatever map is entered after the green room. The stage
	// consumes none of them itself: it parses no `.ents`, which is where the consume points sit.
	NextLandmarkSpawn = FLandmarkSpawn{};
	NextRestorePlacement = FRestorePlacement{};
	bFreshMapState = false;

	if (bInShell && bWorldHasPawn)
	{
		UE_LOG(LogElysiumMap, Log, TEXT("stage: building the stage world in place"));
		return SpawnPendingMap();
	}

	CurrentMap = nullptr;
	UE_LOG(LogElysiumMap, Log, TEXT("stage: %s for the stage world"),
		bInShell ? TEXT("reopening the shell") : TEXT("leaving the map"));
#if WITH_EDITOR
	// Same editor-game world lifetime normalization Travel performs; see the comment there.
	World->ClearFlags(RF_Standalone);
#endif
	UGameplayStatics::OpenLevel(World, FName(*ShellPackage));
	return true;
}

void UElysiumMapSubsystem::RetireGreenRoomLab(const TCHAR* Reason)
{
	if (!GreenRoomRun.IsValid() || !GreenRoomRun->IsLab())
	{
		return;
	}
	// The destructor is what puts the game HUD back and releases the bodies, so this is the whole of
	// it — but it must run before the new world's first frame, not on some later teardown.
	GreenRoomRun.Reset();
	UE_LOG(LogElysiumMap, Log, TEXT("green room: lab retired (%s)"), Reason);
}

FElysiumGreenRoomRun* UElysiumMapSubsystem::EnsureGreenRoomLab(FString& OutError)
{
	if (GreenRoomRun.IsValid())
	{
		// A capture run owns the camera, the stage and the process's exit code, and driving it from
		// a window mid-flight would corrupt the very captures it exists to produce.
		if (!GreenRoomRun->IsLab())
		{
			OutError = TEXT("a green-room capture run is already in flight in this session");
			return nullptr;
		}
		return GreenRoomRun.Get();
	}
	if (GUsingNullRHI)
	{
		OutError = TEXT("the green room needs a real RHI");
		return nullptr;
	}
	GreenRoomRun = MakePimpl<FElysiumGreenRoomRun>(this, /*bForceLab=*/true);
	return GreenRoomRun.Get();
}

void UElysiumMapSubsystem::Deinitialize()
{
	ProfileRun.Reset();
	ShotRun.Reset();
	// Before the lab: a verb that resolves it must not outlive it.
	GreenRoomConsole.Reset();
	GreenRoomRun.Reset();
	ProbeRun.Reset();
	MoveRun.Reset();
	CastRun.Reset();
	for (IConsoleObject* Obj : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Obj);
	}
	ConsoleObjects.Empty();
	Super::Deinitialize();
}

bool UElysiumMapSubsystem::Travel(const FString& Map, const FString& Landmark)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	if (!FElysiumContentPaths::IsConfigured())
	{
		UE_LOG(LogElysiumMap, Error,
			TEXT("export root is not configured; pass -ElysiumContentRoot=... or set ")
			TEXT("ELYSIUM_EXPORT_ROOT / ELYSIUM_WORK_ROOT"));
		return false;
	}

	const FString Level = FElysiumContentPaths::BakedLevel(Map);
	if (!FPackageName::DoesPackageExist(Level))
	{
		UE_LOG(LogElysiumMap, Warning,
			TEXT("no baked level for '%s' (%s) — run: uv run elysium export map %s --force"),
			*Map, *Level, *Map);
		return false;
	}
	// The sidecars the runtime still reads (.ents, .hulls, .ropes, .spawn) live beside the export,
	// so a baked level with no export would build a world with no entities at all.
	if (!FPaths::FileExists(FElysiumContentPaths::MapObj(Map)))
	{
		UE_LOG(LogElysiumMap, Warning, TEXT("no exported map '%s' under %s"), *Map, *FElysiumContentPaths::Root());
		return false;
	}

	// A direct Travel(map, landmark) (console / debug UI) with no transition already queued places the
	// player AT the destination landmark facing its angles (offset zero, lift onto it). A transition
	// (RequestLandmarkTravel) has already filled NextLandmarkSpawn with the real offset/yaw, so leave it.
	if (!Landmark.IsEmpty() && !NextLandmarkSpawn.bValid)
	{
		NextLandmarkSpawn = FLandmarkSpawn{ true, Landmark, FVector::ZeroVector, 0.0f, /*bHasYaw*/ false };
	}

	// Stow the target for the world that builds it. This subsystem is GI-scoped, so PendingMapLoad
	// (and NextLandmarkSpawn) survive the OpenLevel below.
	PendingMapLoad = FPendingMapLoad{ true, Map, Landmark };
	// An ordinary Travel is always a play world. EnterFrontEnd sets the shell flag before its own
	// OpenLevel — the game mode reads it during PostLogin (which runs ahead of BeginPlay) to decide
	// whether to spawn a pawn at all.
	bCurrentIsMenuBackdrop = false;

	// Hard travel into the map's own baked level: the engine tears the current UWorld down and runs
	// GC, then the fresh world's game mode spawns the map actor on BeginPlay (SpawnPendingMap). The
	// destination world differs per map, so this runs on cold boot too — unlike the boot level, a
	// baked level cannot be "already open" for a map we have not entered.
	CurrentMap = nullptr;
	UE_LOG(LogElysiumMap, Log, TEXT("hard travel -> %s%s"), *Map,
		Landmark.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" @ %s"), *Landmark));

#if WITH_EDITOR
	// UWorld::PostLoad marks map assets RF_Standalone whenever GIsEditor is true, including an
	// UnrealEditor.exe -game process. UEngine::LoadMap removes the outgoing world from the root set
	// but, unlike UWorld::DestroyWorld, does not clear that asset flag. The old world then has no
	// reference chain yet survives GC, and the next hard travel is fatal under world-leak checking.
	// Normalize the outgoing editor-game world to the lifetime LoadMap expects before deferring the
	// travel. Packaged game worlds never acquire the flag.
	World->ClearFlags(RF_Standalone);
#endif

	UGameplayStatics::OpenLevel(World, FName(*Level));
	return true;
}

bool UElysiumMapSubsystem::SpawnPendingMap()
{
	if (!PendingMapLoad.bValid)
	{
		return false;
	}
	const FPendingMapLoad P = PendingMapLoad;
	PendingMapLoad = FPendingMapLoad{};
	// Latch the kind of world this is before the map actor builds — AElysiumMapActor::BeginPlay
	// reads it to decide whether to build the gameplay half at all.
	bCurrentIsMenuBackdrop = P.bMenuBackdrop;
	bCurrentIsStageOnly = P.bStageOnly;
	if (!P.bStageOnly)
	{
		// Travelling to a real map is one of the two ways out of the green room (`elysium.map <name>`
		// is the documented one); the lab does not follow the player into it.
		RetireGreenRoomLab(TEXT("left the stage world for a map"));
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		const FString Reason = FString::Printf(TEXT("cannot spawn runtime map '%s': no current world"), *P.Map);
		UE_LOG(LogElysiumMap, Error, TEXT("%s"), *Reason);
		CurrentMapFailed.Broadcast(nullptr, Reason);
		return false;
	}

	const double Start = FPlatformTime::Seconds();

	// Spawn deferred so MapName is set before BeginPlay builds the map.
	FTransform Xf = FTransform::Identity;
	AElysiumMapActor* NewMap = World->SpawnActorDeferred<AElysiumMapActor>(AElysiumMapActor::StaticClass(), Xf);
	if (!NewMap)
	{
		const FString Reason = FString::Printf(TEXT("cannot spawn runtime map actor for '%s'"), *P.Map);
		UE_LOG(LogElysiumMap, Error, TEXT("%s"), *Reason);
		CurrentMapFailed.Broadcast(nullptr, Reason);
		return false;
	}
	NewMap->MapName = P.Map;
	NewMap->bStageOnly = P.bStageOnly;
	// CurrentMap and the delegates are installed before FinishSpawning invokes BeginPlay. Readiness
	// normally completes on a later tick, but this ordering also makes a synchronous construction
	// failure unambiguously belong to the current actor.
	CurrentMap = NewMap;
	NewMap->OnRuntimeReady().AddUObject(this, &UElysiumMapSubsystem::HandleRuntimeReady);
	NewMap->OnRuntimeFailed().AddUObject(this, &UElysiumMapSubsystem::HandleRuntimeFailed);
	NewMap->FinishSpawning(Xf);

	UE_LOG(LogElysiumMap, Log, TEXT("built %s%s%s (%.2fs)"),
		P.bStageOnly ? TEXT("the stage world") : *P.Map,
		P.Landmark.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" @ %s"), *P.Landmark),
		P.bMenuBackdrop ? TEXT(" [menu backdrop]") : TEXT(""),
		FPlatformTime::Seconds() - Start);
	return true;
}

uint64 UElysiumMapSubsystem::BeginMapEpoch()
{
	const uint64 Epoch = MapEpoch.Begin();
	MapEpochBegin.Broadcast(Epoch);
	return Epoch;
}

void UElysiumMapSubsystem::RetireMapEpoch(uint64 Epoch)
{
	// The guard lives on the epoch value, so a late retire from an outgoing world cannot free the
	// incoming map's state. Subscribers are told only about a retire that actually happened.
	if (!MapEpoch.ShouldRetire(Epoch))
	{
		UE_LOG(LogElysiumMap, Verbose, TEXT("ignored stale map-epoch retire %llu (current %llu)"),
			Epoch, MapEpoch.Current());
		return;
	}
	MapEpoch.Retire(Epoch);
	MapEpochRetired.Broadcast(Epoch);
}

void UElysiumMapSubsystem::HandleRuntimeReady(AElysiumMapActor* Map)
{
	if (!Map || CurrentMap.Get() != Map)
	{
		UE_LOG(LogElysiumMap, Verbose, TEXT("ignored stale map-ready callback from %s"),
			*GetNameSafe(Map));
		return;
	}
	CurrentMapReady.Broadcast(Map);
}

void UElysiumMapSubsystem::HandleRuntimeFailed(AElysiumMapActor* Map, const FString& Reason)
{
	if (!Map || CurrentMap.Get() != Map)
	{
		UE_LOG(LogElysiumMap, Verbose, TEXT("ignored stale map-failed callback from %s: %s"),
			*GetNameSafe(Map), *Reason);
		return;
	}
	CurrentMapFailed.Broadcast(Map, Reason);
}

bool UElysiumMapSubsystem::EnterFrontEnd(bool& bOutTravelStarted)
{
	bOutTravelStarted = false;
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	PendingMapLoad = FPendingMapLoad{};
	bCurrentIsMenuBackdrop = true;
	bCurrentIsStageOnly = false;
	// The front end resolves none of the three one-shots either, so none may survive it.
	NextLandmarkSpawn = FLandmarkSpawn{};
	NextRestorePlacement = FRestorePlacement{};
	bFreshMapState = false;
	// The other way out of the green room: quit to the menu. The shell has no pawn and no map, so a
	// lab left armed here would drive a controller the front end is trying to point at its own
	// character stage.
	RetireGreenRoomLab(TEXT("returned to the front end"));

	const FString ShellPackage = FElysiumContentPaths::BootMount();
	if (World->GetOutermost()->GetName().Equals(ShellPackage, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogElysiumMap, Log, TEXT("front end: using the empty boot world in place"));
		return true;
	}

	CurrentMap = nullptr;
#if WITH_EDITOR
	World->ClearFlags(RF_Standalone);
#endif
	bOutTravelStarted = true;
	UE_LOG(LogElysiumMap, Log, TEXT("front end: leaving the map for the empty boot world"));
	UGameplayStatics::OpenLevel(World, FName(*ShellPackage));
	return true;
}

void UElysiumMapSubsystem::RequestLandmarkTravel(const FString& Map, const FString& Landmark,
	const FVector& PlayerOffset, float PlayerYaw)
{
	FString DestMap = Map;
	FString DestLandmark = Landmark;
	FVector DestOffset = PlayerOffset;
	bool bHasYaw = true;
	if (ElysiumStory::ResolveIntroSkip(UElysiumGameFlowSubsystem::ShouldSkipIntro(),
		DestMap, DestLandmark, DestOffset, bHasYaw))
	{
		UE_LOG(LogElysiumMap, Log,
			TEXT("intro skip: %s @ %s -> %s @ %s (dropping source offset/yaw)"),
			*Map, *Landmark, *DestMap, *DestLandmark);
	}
	const FString SourceMap = GetCurrentMapName();
	if (ElysiumStory::ResolveTheatreExitPlacement(SourceMap, DestMap, DestLandmark,
		DestOffset, bHasYaw))
	{
		UE_LOG(LogElysiumMap, Log,
			TEXT("theatre exit: %s @ %s -> %s @ %s (direct landmark placement)"),
			*SourceMap, *Landmark, *DestMap, *DestLandmark);
	}

	if (DestMap.IsEmpty())
	{
		UE_LOG(LogElysiumMap, Warning, TEXT("landmark travel requested with empty map name"));
		return;
	}
	if (PendingMapLoad.bValid)
	{
		return;   // one transition per frame; the first to fire wins (retail defers to end-of-frame)
	}

	// Fill the placement the destination map will consume (dest = landmark origin + offset, keep the
	// player's view yaw). Travel below won't overwrite it (its direct-entry fallback only fires when
	// NextLandmarkSpawn is empty), then OpenLevels — safe from inside the tick (teardown is deferred).
	NextLandmarkSpawn = FLandmarkSpawn{
		true, DestLandmark, DestOffset, PlayerYaw, bHasYaw
	};
	UE_LOG(LogElysiumMap, Log, TEXT("landmark travel -> %s @ %s (offset %s)"),
		*DestMap, *DestLandmark, *DestOffset.ToString());

	if (!Travel(DestMap, DestLandmark))
	{
		// Destination map isn't exported — drop the placement so it can't leak onto a later travel.
		NextLandmarkSpawn = FLandmarkSpawn{};
		UE_LOG(LogElysiumMap, Warning,
			TEXT("landmark travel to '%s' failed (map not exported)"), *DestMap);
	}
}

bool UElysiumMapSubsystem::ConsumeLandmarkSpawn(FString& OutLandmark, FVector& OutOffset,
	float& OutYaw, bool& bOutHasYaw)
{
	if (!NextLandmarkSpawn.bValid)
	{
		return false;
	}
	OutLandmark = NextLandmarkSpawn.Landmark;
	OutOffset   = NextLandmarkSpawn.Offset;
	OutYaw      = NextLandmarkSpawn.Yaw;
	bOutHasYaw  = NextLandmarkSpawn.bHasYaw;
	NextLandmarkSpawn = FLandmarkSpawn{};
	return true;
}

void UElysiumMapSubsystem::RequestRestorePlacement(const FVector& Origin, float Yaw)
{
	NextRestorePlacement.bValid = true;
	NextRestorePlacement.Origin = Origin;
	NextRestorePlacement.Yaw = Yaw;
}

bool UElysiumMapSubsystem::ConsumeRestorePlacement(FVector& OutOrigin, float& OutYaw)
{
	if (!NextRestorePlacement.bValid)
	{
		return false;
	}
	OutOrigin = NextRestorePlacement.Origin;
	OutYaw    = NextRestorePlacement.Yaw;
	NextRestorePlacement = FRestorePlacement{};
	return true;
}

bool UElysiumMapSubsystem::ConsumeFreshMapState()
{
	const bool bWas = bFreshMapState;
	bFreshMapState = false;
	return bWas;
}

FString UElysiumMapSubsystem::PendingTravelDesc() const
{
	if (!PendingMapLoad.bValid)
	{
		return FString();
	}
	return PendingMapLoad.Landmark.IsEmpty()
		? PendingMapLoad.Map
		: FString::Printf(TEXT("%s @ %s"), *PendingMapLoad.Map, *PendingMapLoad.Landmark);
}

bool UElysiumMapSubsystem::Reload()
{
	const FString Current = GetCurrentMapName();
	if (Current.IsEmpty())
	{
		UE_LOG(LogElysiumMap, Warning, TEXT("elysium.reload: no map loaded"));
		return false;
	}
	return Travel(Current);
}

FString UElysiumMapSubsystem::NextMapName() const
{
	const TArray<FString> Maps = ExportedMaps();
	if (Maps.Num() == 0)
	{
		return FString();
	}
	const int32 Cur = Maps.IndexOfByKey(GetCurrentMapName());
	return Maps[(Cur + 1) % Maps.Num()];
}

FString UElysiumMapSubsystem::GetCurrentMapName() const
{
	const AElysiumMapActor* Map = CurrentMap.Get();
	return Map ? Map->LoadedMap : FString();
}

TArray<FString> UElysiumMapSubsystem::ExportedMaps() const
{
	TArray<FString> Names;
	if (!FElysiumContentPaths::IsConfigured())
	{
		return Names;
	}
	IFileManager& FM = IFileManager::Get();
	TArray<FString> Dirs;
	FM.FindFiles(Dirs, *(FElysiumContentPaths::Root() / TEXT("*")), false, true);
	for (const FString& Dir : Dirs)
	{
		// Both halves are required to enter a map: the baked level carries the look, the export
		// carries the sidecars the runtime still reads. An export with no bake is listed nowhere,
		// because Travel would refuse it.
		if (FPaths::FileExists(FElysiumContentPaths::MapObj(Dir))
			&& FPackageName::DoesPackageExist(FElysiumContentPaths::BakedLevel(Dir)))
		{
			Names.Add(Dir);
		}
	}
	Names.Sort();
	return Names;
}
