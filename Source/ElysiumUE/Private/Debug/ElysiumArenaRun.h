#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Debug/ElysiumArenaSpec.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaScenario.h"
#include "Debug/ElysiumArenaScenarioRunner.h"
#include "Templates/UniquePtr.h"

class FElysiumEntityWorld;
class UElysiumMapSubsystem;
class UWorld;

// The headless arena run (`-ElysiumArena`): the scenario records of `Arena/scenarios/` as a suite,
// one boot per host, nothing rendered. The launch contract is `docs/specs/0002-npc-ai/stories/wave2/
// seam.md` § "The launch contract"; `uv run elysium arena` is its launcher.
//
// **Two hosts.** Without `-ElysiumMap` it is the ARENA host: it enters the stage world (the cast
// harness's precedent, `FElysiumCastRun`), stands the arena once (`ElysiumArena::Stand`, collision and
// a Recast graph, no entity world: every record's rows come through its own rebuild), waits for the
// graph (bounded), then per record of `"stage": "arena"`: stages it (`ElysiumArenaStage::Stage` ->
// `RebuildStageWorld`), runs it (`FElysiumArenaScenarioRunner`, zero at the stage's `Activate`) and
// writes its trace. With `-ElysiumMap=<map>` it is that MAP's host: the boot loads the map, and each
// record of `"stage": "map:<map>"` runs against the map's own entities; the map is not rebuilt between
// records, so the launcher boots a map record alone unless it says `"shares_map": true`.
//
// Then `<out>/index.json` and a forced `RequestExitWithStatus`: 0 when no result is `fail`,
// `unexpected-pass` or `error`, 1 otherwise or on a harness error (no record matched, the stage never
// became ready). The index is the verdict a reader trusts; the exit code is a courtesy, for the reason
// `FElysiumCastRun::Fail` states.
//
// **Bodies pose while nothing renders.** Under `-nullrhi` no component is ever "recently rendered",
// so every NPC body's skeletal component is put on `AlwaysTickPoseAndRefreshBones` with update-rate
// optimisation off, every frame of a run -- the compose run's rule (`ElysiumComposeRun.cpp`). The
// kernel's own sequence clock (`StudioFrameAdvance`, `m_bSequenceFinished`) advances on world time and
// the clip length the play reported, and needs no render; what needs the pose to tick is everything
// read off the anim instance: the clip phase the animation-event walk polls
// (`UElysiumEntityBodies::GetBodyClipPhase`) and the bone transforms an attachment reads.
class FElysiumArenaRun
{
public:
	static bool IsRequested();

	explicit FElysiumArenaRun(UElysiumMapSubsystem* InSubsystem);
	~FElysiumArenaRun();

	FElysiumArenaRun(const FElysiumArenaRun&) = delete;
	FElysiumArenaRun& operator=(const FElysiumArenaRun&) = delete;

private:
	enum class EPhase : uint8
	{
		WaitForHost,        // the stage world (arena) or the map (map host) to be built and active
		WaitForNavigation,  // the arena's Recast graph
		NextRecord,
		Running,
		Done,
	};

	bool Tick(float DeltaSeconds);

	bool IsArenaHost() const { return MapName.IsEmpty(); }
	FString HostName() const;
	UWorld* GetWorld() const;
	FElysiumEntityWorld* GetEntityWorld() const;

	// Pick this host's records (and the requested names) and turn every unmatched name or unreadable
	// file into an `error` result, so the index says what was asked for and could not run.
	void SelectRecords();
	void ClearStaleOutput() const;
	// The host is ready to stage into. False while waiting; a harness error past the bound.
	bool IsHostReady();
	bool StandArena();
	void BeginNextRecord();
	void EndRecord();
	// Every bodied entity of the run's world poses whether or not it is drawn.
	void PoseBodiesHeadless() const;
	void AddErrorResult(const FString& Name, const FString& Error);
	void FailHarness(const FString& Error);
	void Finish();

	TWeakObjectPtr<UElysiumMapSubsystem> Subsystem;
	FTSTicker::FDelegateHandle TickHandle;

	EPhase Phase = EPhase::WaitForHost;
	FString MapName;
	FString OutDir;
	int32 Hz = 60;
	TArray<FString> Requested;

	TArray<FElysiumArenaScenario> Records;
	int32 RecordIndex = INDEX_NONE;
	TUniquePtr<FElysiumArenaScenarioRunner> Runner;
	TArray<FElysiumArenaScenarioResult> Results;

	ElysiumArena::FSpec Arena;
	ElysiumArena::FStanding ArenaStanding;

	bool bStageRequested = false;
	int32 SettleFrames = 0;
	double PhaseStartWall = 0.0;
	FString HarnessError;
};

#endif // !UE_BUILD_SHIPPING
