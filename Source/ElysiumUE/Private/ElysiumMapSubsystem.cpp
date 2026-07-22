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

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMap, Log, All);

void UElysiumMapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Engine-console mirrors of the dev-console commands, handy for -ExecCmds automation.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.map"),
		TEXT("elysium.map <name>|next — travel to an exported VtMB map"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumMap, Display, TEXT("current map: %s"), *GetCurrentMapName());
				return;
			}
			const FString Target = (Args[0] == TEXT("next")) ? NextMapName() : Args[0];
			Travel(Target);
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
