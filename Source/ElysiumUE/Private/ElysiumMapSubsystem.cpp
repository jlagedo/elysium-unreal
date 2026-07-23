#include "ElysiumMapSubsystem.h"

#include "ElysiumContentPaths.h"
#include "ElysiumMapActor.h"
#include "ElysiumProfiler.h"
#include "ElysiumTextureCache.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMap, Log, All);

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
}

void UElysiumMapSubsystem::Deinitialize()
{
	ProfileRun.Reset();
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

	if (!FPaths::FileExists(FElysiumContentPaths::MapObj(Map)))
	{
		UE_LOG(LogElysiumMap, Warning, TEXT("no exported map '%s' under %s"), *Map, *FElysiumContentPaths::Root());
		return false;
	}

	const double Start = FPlatformTime::Seconds();

	// A direct Travel(map, landmark) (console / debug UI) with no transition already queued places the
	// player AT the destination landmark facing its angles (offset zero, lift onto it). A transition
	// (FlushPendingTravel) has already filled NextLandmarkSpawn with the real offset/yaw, so leave it.
	if (!Landmark.IsEmpty() && !NextLandmarkSpawn.bValid)
	{
		NextLandmarkSpawn = FLandmarkSpawn{ true, Landmark, FVector::ZeroVector, 0.0f, /*bHasYaw*/ false };
	}

	// Unload: the map actor owns everything map-scoped, so destroying it is the unload.
	// The texture cache then drops its strong refs and GC reclaims the memory.
	if (AElysiumMapActor* Old = CurrentMap.Get())
	{
		Old->Destroy();
		CurrentMap = nullptr;
		FElysiumTextureCache::FlushAll();
		GEngine->ForceGarbageCollection(true);
	}

	// Load: spawn deferred so MapName is set before BeginPlay builds the map.
	FTransform Xf = FTransform::Identity;
	AElysiumMapActor* NewMap = World->SpawnActorDeferred<AElysiumMapActor>(AElysiumMapActor::StaticClass(), Xf);
	if (!NewMap)
	{
		return false;
	}
	NewMap->MapName = Map;
	NewMap->FinishSpawning(Xf);
	CurrentMap = NewMap;

	UE_LOG(LogElysiumMap, Log, TEXT("travel -> %s%s (%.2fs total)"), *Map,
		Landmark.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" @ %s"), *Landmark),
		FPlatformTime::Seconds() - Start);
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
	if (PendingTravel.bValid)
	{
		return;   // one transition per frame; the first to fire wins (retail defers to end-of-frame)
	}
	PendingTravel = FPendingTravel{ true, Map, Landmark, PlayerOffset, PlayerYaw };
	UE_LOG(LogElysiumMap, Log, TEXT("landmark travel queued -> %s @ %s (offset %s)"),
		*Map, *Landmark, *PlayerOffset.ToString());

	// Defer to the next engine tick: the caller is inside AElysiumMapActor::Tick -> the entity-world
	// tick, and Travel destroys that actor (and its world) + force-GCs. Running it on a next-tick
	// timer executes the swap after all actor ticks have unwound, so nothing is freed under the stack.
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		World->GetTimerManager().SetTimerForNextTick(this, &UElysiumMapSubsystem::FlushPendingTravel);
	}
}

void UElysiumMapSubsystem::FlushPendingTravel()
{
	if (!PendingTravel.bValid)
	{
		return;
	}
	const FPendingTravel P = PendingTravel;
	PendingTravel = FPendingTravel{};

	// Hand the placement to the map load (dest = destination landmark origin + P.Offset, keep view yaw).
	NextLandmarkSpawn = FLandmarkSpawn{ true, P.Landmark, P.Offset, P.Yaw, /*bHasYaw*/ true };
	if (!Travel(P.Map, P.Landmark))
	{
		// Destination map isn't exported — drop the placement so it can't leak onto a later travel.
		NextLandmarkSpawn = FLandmarkSpawn{};
		UE_LOG(LogElysiumMap, Warning, TEXT("landmark travel to '%s' failed (map not exported)"), *P.Map);
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

FString UElysiumMapSubsystem::PendingTravelDesc() const
{
	if (!PendingTravel.bValid)
	{
		return FString();
	}
	return PendingTravel.Landmark.IsEmpty()
		? PendingTravel.Map
		: FString::Printf(TEXT("%s @ %s"), *PendingTravel.Map, *PendingTravel.Landmark);
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
		if (FPaths::FileExists(FElysiumContentPaths::MapObj(Dir)))
		{
			Names.Add(Dir);
		}
	}
	Names.Sort();
	return Names;
}

FString UElysiumMapSubsystem::ResolveBootMap() const
{
	FString CmdMap;
	if (FParse::Value(FCommandLine::Get(), TEXT("ElysiumMap="), CmdMap) && !CmdMap.IsEmpty())
	{
		return CmdMap;
	}
	return TEXT("sp_tutorial_1");
}
