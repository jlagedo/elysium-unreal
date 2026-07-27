#include "ElysiumMapSubsystem.h"

#include "ElysiumContentPaths.h"
#include "ElysiumMapActor.h"
#include "ElysiumProbeRun.h"
#include "ElysiumProfiler.h"
#include "ElysiumShotRun.h"

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

// The game boots into /Game/Elysium (Config/DefaultEngine.ini GameDefaultMap), an empty UWorld it
// sits in until the first Travel. Map travel does not come back through it — each VtMB map is its
// own baked .umap under the /ElysiumBaked mount, and Travel opens that level directly.

void UElysiumMapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

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
	// Under -ElysiumProbe, arm the headless light-attribution probe (one map per launch).
	if (FElysiumProbeRun::IsRequested())
	{
		ProbeRun = MakePimpl<FElysiumProbeRun>(this);
	}
}

void UElysiumMapSubsystem::Deinitialize()
{
	ProfileRun.Reset();
	ShotRun.Reset();
	ProbeRun.Reset();
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

	const FString Level = FElysiumContentPaths::BakedLevel(Map);
	if (!FPackageName::DoesPackageExist(Level))
	{
		UE_LOG(LogElysiumMap, Warning,
			TEXT("no baked level for '%s' (%s) — run: bake.bat %s"), *Map, *Level, *Map);
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
	// An ordinary Travel is always a play world. TravelForMenu re-raises this immediately after,
	// *before* OpenLevel — the game mode reads it during PostLogin (which runs ahead of BeginPlay)
	// to decide whether to spawn a pawn at all.
	bCurrentIsMenuBackdrop = false;

	// Hard travel into the map's own baked level: the engine tears the current UWorld down and runs
	// GC, then the fresh world's game mode spawns the map actor on BeginPlay (SpawnPendingMap). The
	// destination world differs per map, so this runs on cold boot too — unlike the boot level, a
	// baked level cannot be "already open" for a map we have not entered.
	CurrentMap = nullptr;
	UE_LOG(LogElysiumMap, Log, TEXT("hard travel -> %s%s"), *Map,
		Landmark.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" @ %s"), *Landmark));
	UGameplayStatics::OpenLevel(World, FName(*Level));
	return true;
}

void UElysiumMapSubsystem::SpawnPendingMap()
{
	if (!PendingMapLoad.bValid)
	{
		return;
	}
	const FPendingMapLoad P = PendingMapLoad;
	PendingMapLoad = FPendingMapLoad{};
	// Latch the kind of world this is before the map actor builds — AElysiumMapActor::BeginPlay
	// reads it to decide whether to build the gameplay half at all.
	bCurrentIsMenuBackdrop = P.bMenuBackdrop;

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const double Start = FPlatformTime::Seconds();

	// Spawn deferred so MapName is set before BeginPlay builds the map.
	FTransform Xf = FTransform::Identity;
	AElysiumMapActor* NewMap = World->SpawnActorDeferred<AElysiumMapActor>(AElysiumMapActor::StaticClass(), Xf);
	if (!NewMap)
	{
		return;
	}
	NewMap->MapName = P.Map;
	NewMap->FinishSpawning(Xf);
	CurrentMap = NewMap;

	UE_LOG(LogElysiumMap, Log, TEXT("built %s%s%s (%.2fs)"), *P.Map,
		P.Landmark.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" @ %s"), *P.Landmark),
		P.bMenuBackdrop ? TEXT(" [menu backdrop]") : TEXT(""),
		FPlatformTime::Seconds() - Start);
}

bool UElysiumMapSubsystem::TravelForMenu(const FString& Map)
{
	// Travel does all the validation and the OpenLevel; the only difference is the flag the fresh
	// world reads, so set it after Travel has stowed the record.
	if (!Travel(Map))
	{
		return false;
	}
	PendingMapLoad.bMenuBackdrop = true;
	bCurrentIsMenuBackdrop = true;
	return true;
}

void UElysiumMapSubsystem::RequestLandmarkTravel(const FString& Map, const FString& Landmark,
	const FVector& PlayerOffset, float PlayerYaw)
{
	if (Map.IsEmpty())
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
	NextLandmarkSpawn = FLandmarkSpawn{ true, Landmark, PlayerOffset, PlayerYaw, /*bHasYaw*/ true };
	UE_LOG(LogElysiumMap, Log, TEXT("landmark travel -> %s @ %s (offset %s)"),
		*Map, *Landmark, *PlayerOffset.ToString());

	if (!Travel(Map, Landmark))
	{
		// Destination map isn't exported — drop the placement so it can't leak onto a later travel.
		NextLandmarkSpawn = FLandmarkSpawn{};
		UE_LOG(LogElysiumMap, Warning, TEXT("landmark travel to '%s' failed (map not exported)"), *Map);
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

