#include "Debug/ElysiumArenaRun.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaBuilder.h"
#include "Debug/ElysiumArenaStage.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumArenaRun, Log, All);

// Qualified rather than anonymous: a unity build concatenates translation units.
namespace ElysiumArenaRunDetail
{
	// How long the boot is given to stand the host up (the stage world, or the map's whole load),
	// wall seconds. Past it the run is a harness error rather than a hang.
	constexpr double HostWaitSeconds = 180.0;
	// How long Recast is given over the arena, wall seconds. A character staged before the graph
	// answers cannot path, which would read as an AI defect.
	constexpr double NavigationWaitSeconds = 30.0;
	// Frames between the host reporting ready and the first thing this run does to it -- the settle
	// every self-driving harness takes, kept short because every record rebuilds its own world anyway.
	constexpr int32 SettleFrames = 10;

	const TCHAR* const IndexFile = TEXT("index.json");
}

bool FElysiumArenaRun::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("ElysiumArena"));
}

FElysiumArenaRun::FElysiumArenaRun(UElysiumMapSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FParse::Value(CommandLine, TEXT("ElysiumMap="), MapName);
	FParse::Value(CommandLine, TEXT("ArenaHz="), Hz);
	Hz = FMath::Clamp(Hz, 10, 1000);
	FString List;
	// Not stopping on a separator: the list IS comma-separated.
	if (FParse::Value(CommandLine, TEXT("ArenaScenarios="), List, /*bShouldStopOnSeparator=*/false))
	{
		List.ParseIntoArray(Requested, TEXT(","), /*InCullEmpty=*/true);
		for (FString& Name : Requested)
		{
			Name.TrimStartAndEndInline();
		}
		Requested.RemoveAll([](const FString& Name) { return Name.IsEmpty(); });
	}
	if (!FParse::Value(CommandLine, TEXT("ArenaOut="), OutDir, /*bShouldStopOnSeparator=*/false) || OutDir.IsEmpty())
	{
		OutDir = FElysiumContentPaths::SavedDebugDir(TEXT("_arena"));
	}
	IFileManager::Get().MakeDirectory(*OutDir, /*Tree=*/true);
	ClearStaleOutput();
	SelectRecords();
	if (Records.IsEmpty() && HarnessError.IsEmpty())
	{
		HarnessError = Requested.IsEmpty()
			? FString::Printf(TEXT("no record of host %s under %s"), *HostName(), *ElysiumArenaScenario::ScenarioRoot())
			: FString::Printf(TEXT("none of %s is a record of host %s"), *FString::Join(Requested, TEXT(", ")),
				*HostName());
	}

	UE_LOG(LogElysiumArenaRun, Log, TEXT("headless arena run armed: host %s, %d Hz, %d record(s), report %s"),
		*HostName(), Hz, Records.Num(), *OutDir);
	PhaseStartWall = FPlatformTime::Seconds();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FElysiumArenaRun::Tick));
}

FElysiumArenaRun::~FElysiumArenaRun()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
	// The runner first: it clears its sink on a world that is still standing.
	Runner.Reset();
	// Stood with no entity world, so there are no anchor or node handles to kill -- only the room.
	ElysiumArena::Teardown(nullptr, ArenaStanding);
}

FString FElysiumArenaRun::HostName() const
{
	return IsArenaHost() ? FString(TEXT("arena")) : FString::Printf(TEXT("map:%s"), *MapName);
}

UWorld* FElysiumArenaRun::GetWorld() const
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const UGameInstance* GI = Sub != nullptr ? Sub->GetGameInstance() : nullptr;
	return GI != nullptr ? GI->GetWorld() : nullptr;
}

FElysiumEntityWorld* FElysiumArenaRun::GetEntityWorld() const
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const AElysiumMapActor* Map = Sub != nullptr ? Sub->GetCurrentMap() : nullptr;
	return Map != nullptr ? Map->GetEntityWorld() : nullptr;
}

void FElysiumArenaRun::ClearStaleOutput() const
{
	// This run's own files only: a previous run's trace left beside this run's index would read as
	// one of its scenarios.
	IFileManager& Files = IFileManager::Get();
	TArray<FString> Stale;
	Files.FindFiles(Stale, *(OutDir / TEXT("*.trace.tsv")), /*Files=*/true, /*Directories=*/false);
	for (const FString& Name : Stale)
	{
		Files.Delete(*(OutDir / Name), /*RequireExists=*/false, /*EvenReadOnly=*/true);
	}
	Files.Delete(*(OutDir / ElysiumArenaRunDetail::IndexFile), /*RequireExists=*/false, /*EvenReadOnly=*/true);
}

void FElysiumArenaRun::SelectRecords()
{
	TArray<FElysiumArenaScenario> All;
	TArray<FString> Errors;
	ElysiumArenaScenario::LoadAll(All, Errors);
	for (const FString& Error : Errors)
	{
		UE_LOG(LogElysiumArenaRun, Error, TEXT("record refused: %s"), *Error);
	}
	for (FElysiumArenaScenario& Record : All)
	{
		const bool bThisHost = IsArenaHost() ? Record.IsArenaStage()
			: Record.StageMap.Equals(MapName, ESearchCase::IgnoreCase);
		const bool bAsked = Requested.IsEmpty() || Requested.ContainsByPredicate([&Record](const FString& Name)
			{ return Name.Equals(Record.Name, ESearchCase::IgnoreCase); });
		if (bThisHost && bAsked)
		{
			Records.Add(MoveTemp(Record));
		}
	}
	// In the order asked, so the report reads in the launcher's order.
	if (!Requested.IsEmpty())
	{
		Records.Sort([this](const FElysiumArenaScenario& A, const FElysiumArenaScenario& B)
		{
			const int32 IndexA = Requested.IndexOfByPredicate([&A](const FString& N) { return N.Equals(A.Name, ESearchCase::IgnoreCase); });
			const int32 IndexB = Requested.IndexOfByPredicate([&B](const FString& N) { return N.Equals(B.Name, ESearchCase::IgnoreCase); });
			return IndexA < IndexB;
		});
	}

	// What was asked for and cannot run is a result, not a log line: the index is what a reader reads.
	for (const FString& Name : Requested)
	{
		if (!Records.ContainsByPredicate([&Name](const FElysiumArenaScenario& Record)
			{ return Record.Name.Equals(Name, ESearchCase::IgnoreCase); }))
		{
			AddErrorResult(Name, FString::Printf(TEXT("no record of host %s is named '%s'%s"), *HostName(), *Name,
				Errors.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%d record file(s) did not parse: %s)"),
					Errors.Num(), *FString::Join(Errors, TEXT(" | ")))));
		}
	}
	if (Requested.IsEmpty())
	{
		for (const FString& Error : Errors)
		{
			// `<file>: <field>: <message>`; the drive letter's colon is not followed by a space.
			const int32 Split = Error.Find(TEXT(": "));
			const FString File = Split > 0 ? Error.Left(Split) : Error;
			AddErrorResult(FPaths::GetBaseFilename(File), Error);
		}
	}
}

void FElysiumArenaRun::AddErrorResult(const FString& Name, const FString& Error)
{
	FElysiumArenaScenarioResult& Result = Results.AddDefaulted_GetRef();
	Result.Name = Name;
	Result.Result = TEXT("error");
	Result.Error = Error;
	UE_LOG(LogElysiumArenaRun, Error, TEXT("%s"), *Result.Summary());
}

bool FElysiumArenaRun::IsHostReady()
{
	UElysiumMapSubsystem* Sub = Subsystem.Get();
	if (Sub == nullptr)
	{
		return false;
	}
	const AElysiumMapActor* Map = Sub->GetCurrentMap();
	if (!IsArenaHost())
	{
		return Map != nullptr && !Map->IsStageOnly() && Map->MapName.Equals(MapName, ESearchCase::IgnoreCase)
			&& Map->IsSpawnDone() && Map->IsRuntimeActive();
	}
	if (Sub->IsStageWorld() && Map != nullptr && Map->IsStageOnly() && Map->IsSpawnDone()
		&& Map->IsRuntimeActive())
	{
		return true;
	}
	// The boot plan stood something else (the front end, or a New Game): enter the stage world through
	// the door `elysium.gr` takes, minus the lab -- the cast harness's world, which `-nullrhi` allows.
	// Only once the boot has settled, so this does not race the plan it replaces.
	if (!bStageRequested && !Sub->HasPendingMapLoad()
		&& (Sub->IsMenuBackdrop() || (Map != nullptr && Map->IsSpawnDone())))
	{
		bStageRequested = true;
		UGameInstance* GI = Sub->GetGameInstance();
		UElysiumGameFlowSubsystem* Flow = GI != nullptr ? GI->GetSubsystem<UElysiumGameFlowSubsystem>() : nullptr;
		FString Error;
		if (Flow == nullptr || !Flow->EnterStageWorld(Error))
		{
			FailHarness(FString::Printf(TEXT("could not enter the stage world: %s"),
				Flow == nullptr ? TEXT("no game flow subsystem") : *Error));
			return false;
		}
		UE_LOG(LogElysiumArenaRun, Log, TEXT("entering the stage world for the arena host"));
	}
	return false;
}

bool FElysiumArenaRun::StandArena()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}
	Arena = ElysiumArena::Build();
	FString Error;
	// **No entity world.** Every record brings the room's anchors and cover nodes through its own
	// rebuild (`ElysiumArenaStage::Stage`); rows stood into the boot's entity world now would die with
	// it at the first rebuild and leave stale handles in `ArenaStanding`.
	if (!ElysiumArena::Stand(World, /*EntityWorld=*/nullptr, Arena, ElysiumArena::DefaultOrigin(),
		/*bWithMeshes=*/false, ArenaStanding, Error))
	{
		FailHarness(FString::Printf(TEXT("could not stand the arena: %s"), *Error));
		return false;
	}
	UE_LOG(LogElysiumArenaRun, Log, TEXT("arena stood: %d solid(s), navigation building"), Arena.Solids.Num());
	return true;
}

void FElysiumArenaRun::BeginNextRecord()
{
	Runner.Reset();
	++RecordIndex;
	if (!Records.IsValidIndex(RecordIndex))
	{
		Finish();
		return;
	}
	const FElysiumArenaScenario& Record = Records[RecordIndex];
	UElysiumMapSubsystem* Sub = Subsystem.Get();

	ElysiumArenaStage::FHost Host;
	Host.World = GetWorld();
	Host.Map = Sub != nullptr ? Sub->GetCurrentMap() : nullptr;
	Host.bArena = IsArenaHost();
	if (Host.bArena)
	{
		Host.Spec = Arena;
		Host.Origin = ElysiumArena::DefaultOrigin();
	}

	FString Summary;
	FString Error;
	if (!ElysiumArenaStage::Stage(Record, Host, Summary, Error))
	{
		AddErrorResult(Record.Name, FString::Printf(TEXT("staging: %s"), *Error));
		return;
	}
	UE_LOG(LogElysiumArenaRun, Log, TEXT("%s"), *Summary);

	// After the rebuild: the sink goes on the world the rebuild just made (a teardown clears it).
	Runner = MakeUnique<FElysiumArenaScenarioRunner>(Record, Host);
	if (!Runner->Start(Host.bArena ? FElysiumArenaScenarioRunner::EZero::StageActivation
		: FElysiumArenaScenarioRunner::EZero::Now, Error))
	{
		Runner.Reset();
		AddErrorResult(Record.Name, Error);
		return;
	}
	Phase = EPhase::Running;
}

void FElysiumArenaRun::EndRecord()
{
	if (Runner.IsValid())
	{
		FString Error;
		if (!Runner->WriteTrace(OutDir / Runner->GetResult().Trace, Error))
		{
			UE_LOG(LogElysiumArenaRun, Error, TEXT("%s"), *Error);
		}
		Results.Add(Runner->GetResult());
		Runner.Reset();
	}
	Phase = EPhase::NextRecord;
}

void FElysiumArenaRun::PoseBodiesHeadless() const
{
	const FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		return;
	}
	for (const TUniquePtr<FElysiumEntity>& Entity : World->Entities())
	{
		USkeletalMeshComponent* Body = Entity.IsValid() ? Entity->GetSkeletalBody() : nullptr;
		if (Body == nullptr)
		{
			continue;
		}
		// The rendered game ticks and refreshes a drawn body every frame; under `-nullrhi` nothing is
		// drawn, so this is what keeps the clip phase and the bones a headless run reads moving.
		if (Body->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
		Body->bEnableUpdateRateOptimizations = false;
	}
}

void FElysiumArenaRun::FailHarness(const FString& Error)
{
	if (HarnessError.IsEmpty())
	{
		HarnessError = Error;
	}
	UE_LOG(LogElysiumArenaRun, Error, TEXT("arena run: %s"), *Error);
	Finish();
}

void FElysiumArenaRun::Finish()
{
	if (Phase == EPhase::Done)
	{
		return;
	}
	Phase = EPhase::Done;
	Runner.Reset();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("host"), HostName());
	Root->SetNumberField(TEXT("hz"), Hz);
	TArray<TSharedPtr<FJsonValue>> Scenarios;
	bool bFailed = !HarnessError.IsEmpty() || Results.IsEmpty();
	for (const FElysiumArenaScenarioResult& Result : Results)
	{
		Scenarios.Add(MakeShared<FJsonValueObject>(Result.ToJson()));
		bFailed |= Result.IsVerdictFailure();
	}
	Root->SetArrayField(TEXT("scenarios"), Scenarios);
	if (!HarnessError.IsEmpty())
	{
		Root->SetStringField(TEXT("error"), HarnessError);
	}

	FString Text;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
	const FString Path = OutDir / ElysiumArenaRunDetail::IndexFile;
	if (!FJsonSerializer::Serialize(Root, Writer)
		|| !FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogElysiumArenaRun, Error, TEXT("arena run could not write %s"), *Path);
		bFailed = true;
	}

	for (const FElysiumArenaScenarioResult& Result : Results)
	{
		UE_LOG(LogElysiumArenaRun, Display, TEXT("  %s"), *Result.Summary());
	}
	UE_LOG(LogElysiumArenaRun, Display, TEXT("arena run complete: host %s, %d result(s), %s; report %s"),
		*HostName(), Results.Num(), bFailed ? TEXT("FAILED") : TEXT("passed"), *Path);
	// Forced, as the automation commandline's `Quit` is: every trace was written when its record
	// ended (`EndRecord`) and the index just above, so the engine's orderly teardown (1.4 s of every
	// headless boot, measured 2026-10-03) writes nothing a reader needs. The forced path flushes the
	// log before it terminates.
	FPlatformMisc::RequestExitWithStatus(/*Force=*/true, /*ReturnCode=*/bFailed ? 1 : 0);
}

bool FElysiumArenaRun::Tick(float /*DeltaSeconds*/)
{
	if (Phase == EPhase::Done)
	{
		return false;
	}
	if (!HarnessError.IsEmpty())
	{
		Finish();
		return false;
	}

	switch (Phase)
	{
	case EPhase::WaitForHost:
		if (!IsHostReady())
		{
			if (Phase != EPhase::Done
				&& FPlatformTime::Seconds() - PhaseStartWall > ElysiumArenaRunDetail::HostWaitSeconds)
			{
				FailHarness(FString::Printf(TEXT("host %s never became ready in %.0f s"), *HostName(),
					ElysiumArenaRunDetail::HostWaitSeconds));
			}
			SettleFrames = 0;
			break;
		}
		if (++SettleFrames < ElysiumArenaRunDetail::SettleFrames)
		{
			break;
		}
		if (IsArenaHost())
		{
			if (!StandArena())
			{
				break;
			}
			PhaseStartWall = FPlatformTime::Seconds();
			Phase = EPhase::WaitForNavigation;
		}
		else
		{
			Phase = EPhase::NextRecord;
		}
		break;

	case EPhase::WaitForNavigation:
		if (ElysiumArena::IsNavigationReady(GetWorld()))
		{
			UE_LOG(LogElysiumArenaRun, Log, TEXT("arena navigation ready after %.1f s"),
				FPlatformTime::Seconds() - PhaseStartWall);
			Phase = EPhase::NextRecord;
		}
		else if (FPlatformTime::Seconds() - PhaseStartWall > ElysiumArenaRunDetail::NavigationWaitSeconds)
		{
			FailHarness(FString::Printf(TEXT("Recast never finished building over the arena in %.0f s"),
				ElysiumArenaRunDetail::NavigationWaitSeconds));
		}
		break;

	case EPhase::NextRecord:
		BeginNextRecord();
		break;

	case EPhase::Running:
		PoseBodiesHeadless();
		if (!Runner.IsValid() || !Runner->Tick())
		{
			EndRecord();
		}
		break;

	default:
		break;
	}
	return Phase != EPhase::Done;
}

#endif // !UE_BUILD_SHIPPING
