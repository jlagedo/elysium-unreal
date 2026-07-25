#include "ElysiumCardRun.h"

#include "ElysiumCardBake.h"
#include "ElysiumContentPaths.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"

#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCards, Log, All);

namespace
{
	// Frames to let a map build settle before reading its bake items. The items are filled
	// synchronously during LoadMap, so this only covers the spawn hold; a handful of frames is
	// plenty and keeps the run honest about being a real map load.
	constexpr int32 BootSettleFrames = 5;
}

bool FElysiumCardRun::IsRequested()
{
#if ELYSIUM_WITH_CARDGEN
	return FParse::Param(FCommandLine::Get(), TEXT("ElysiumCards"));
#else
	if (FParse::Param(FCommandLine::Get(), TEXT("ElysiumCards")))
	{
		UE_LOG(LogElysiumCards, Warning,
			TEXT("-ElysiumCards ignored: the Lumen card builder is editor-only (Embree). "
			     "Run cards.bat against an editor-target build."));
	}
	return false;
#endif
}

void FElysiumCardRun::ResolveMapList(TArray<FString>& OutMaps)
{
	OutMaps.Reset();

	FString Single;
	if (FParse::Value(FCommandLine::Get(), TEXT("ElysiumMap="), Single) && !Single.IsEmpty())
	{
		OutMaps.Add(Single);
		return;
	}

	// Every exported map: a directory under tools/out holding <name>/<name>.obj. The leading
	// `_` folders (_shots, _profile, _tests) never match that shape, so they drop out on their own.
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(FElysiumContentPaths::Root() / TEXT("*")), false, true);
	for (const FString& Dir : Dirs)
	{
		if (FPaths::FileExists(FElysiumContentPaths::MapObj(Dir)))
		{
			OutMaps.Add(Dir);
		}
	}
	OutMaps.Sort();
}

FElysiumCardRun::FElysiumCardRun(UElysiumMapSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
	ResolveMapList(Maps);

	// The bake produces cards for the chunked-world path, so it has to run on that path. With
	// elysium.LumenCards off the world is one PMC, which has no card representation at all and
	// nothing to fit — so force it on for the run rather than silently baking an empty sidecar.
	if (IConsoleVariable* Cards = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.LumenCards")))
	{
		if (Cards->GetInt() == 0)
		{
			UE_LOG(LogElysiumCards, Warning, TEXT("-ElysiumCards forces elysium.LumenCards on for this run."));
			Cards->Set(1, ECVF_SetByCode);
		}
	}

	ElysiumCardBake::SetBaking(true);

	UE_LOG(LogElysiumCards, Log, TEXT("card bake armed: %d map(s)."), Maps.Num());

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FElysiumCardRun::Tick));
}

FElysiumCardRun::~FElysiumCardRun()
{
	ElysiumCardBake::SetBaking(false);
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
}

void FElysiumCardRun::BakeCurrent()
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const AElysiumMapActor* MapActor = Sub ? Sub->GetCurrentMap() : nullptr;
	const FString Map = Sub ? Sub->GetCurrentMapName() : FString();

	FResult& Result = Results.AddDefaulted_GetRef();
	Result.Map = Map;

	if (MapActor == nullptr || MapActor->GetCards() == nullptr)
	{
		UE_LOG(LogElysiumCards, Warning, TEXT("%s: no built map actor; skipped."), *Map);
		return;
	}

	const FElysiumCardContext* Context = MapActor->GetCards();
	const TArray<FElysiumCardBakeItem>& Items = Context->BakeItems;
	if (Items.Num() == 0)
	{
		UE_LOG(LogElysiumCards, Warning,
			TEXT("%s: nothing to bake (no chunked world and no prop models)."), *Map);
		return;
	}

#if ELYSIUM_WITH_CARDGEN
	// Stamped into the sidecar header: the cell size identifies which chunking the world keys
	// belong to, and the card cap is there so a re-bake at a different cap is self-describing.
	const IConsoleVariable* CellVar = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.LumenCardCellCm"));
	const IConsoleVariable* MaxVar = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.LumenCardMax"));
	const float CellCm = CellVar ? CellVar->GetFloat() : 0.f;
	const int32 MaxCards = MaxVar ? MaxVar->GetInt() : 12;

	const double Start = FPlatformTime::Seconds();
	Result.Written = ElysiumCardBake::BakeAndWrite(
		FElysiumContentPaths::MapCards(Map), CellCm, MaxCards, Items, Result.Failed);
	Result.Seconds = FPlatformTime::Seconds() - Start;
#endif
}

void FElysiumCardRun::Advance()
{
	++MapIndex;
	FrameInPhase = 0;

	if (!Maps.IsValidIndex(MapIndex))
	{
		Finish();
		return;
	}

	UElysiumMapSubsystem* Sub = Subsystem.Get();
	if (Sub == nullptr || !Sub->Travel(Maps[MapIndex]))
	{
		UE_LOG(LogElysiumCards, Warning, TEXT("could not travel to %s; stopping."), *Maps[MapIndex]);
		Finish();
		return;
	}
	Phase = EPhase::Travel;
}

void FElysiumCardRun::Finish()
{
	Phase = EPhase::Done;

	int32 TotalWritten = 0;
	int32 TotalFailed = 0;
	double TotalSeconds = 0.0;
	UE_LOG(LogElysiumCards, Log, TEXT("==== card bake summary ===="));
	for (const FResult& R : Results)
	{
		UE_LOG(LogElysiumCards, Log, TEXT("  %-24s %5d baked  %4d failed  %6.1f s"),
			*R.Map, R.Written, R.Failed, R.Seconds);
		TotalWritten += R.Written;
		TotalFailed += R.Failed;
		TotalSeconds += R.Seconds;
	}
	UE_LOG(LogElysiumCards, Log, TEXT("  %d map(s), %d meshes baked, %d failed, %.1f s fitting."),
		Results.Num(), TotalWritten, TotalFailed, TotalSeconds);

	FPlatformMisc::RequestExit(false);
}

bool FElysiumCardRun::Tick(float /*DeltaSeconds*/)
{
	UElysiumMapSubsystem* Sub = Subsystem.Get();
	const AElysiumMapActor* MapActor = Sub ? Sub->GetCurrentMap() : nullptr;

	switch (Phase)
	{
	case EPhase::WaitReady:
	{
		if (Maps.Num() == 0)
		{
			UE_LOG(LogElysiumCards, Warning, TEXT("no exported maps under %s; nothing to bake."),
				*FElysiumContentPaths::Root());
			Finish();
			break;
		}
		// The boot map is whatever -ElysiumMap= (or New Game) landed on; the run list is walked
		// from wherever that is, so index 0 is the map already loading.
		if (MapActor != nullptr && MapActor->IsSpawnDone() && ++FrameInPhase >= BootSettleFrames)
		{
			// The boot map may not be Maps[0] (a bare `play`-style boot enters the story map), so
			// align the index to what actually loaded rather than assuming.
			const int32 Found = Maps.Find(Sub->GetCurrentMapName());
			MapIndex = Found != INDEX_NONE ? Found : 0;
			Phase = EPhase::Bake;
		}
		break;
	}

	case EPhase::Bake:
	{
		BakeCurrent();
		Advance();
		break;
	}

	case EPhase::Travel:
	{
		// Hard travel tears the world down and rebuilds it; wait for the new map actor to finish
		// its spawn hold, exactly as the WaitReady phase does.
		if (MapActor != nullptr && MapActor->IsSpawnDone()
			&& Sub->GetCurrentMapName() == Maps[MapIndex]
			&& ++FrameInPhase >= BootSettleFrames)
		{
			FrameInPhase = 0;
			Phase = EPhase::Bake;
		}
		break;
	}

	case EPhase::Done:
	default:
		break;
	}

	return true;   // keep ticking; Finish() requests the exit
}
