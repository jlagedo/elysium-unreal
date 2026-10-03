#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaScenario.h"
#include "Debug/ElysiumArenaStage.h"
#include "Delegates/IDelegateInstance.h"   // FDelegateHandle, the activation binding
#include "ElysiumEntityHandle.h"
#include "Internationalization/Regex.h"     // a `"regex": true` matcher, compiled once
#include "Templates/UniquePtr.h"

class AElysiumMapActor;
class FElysiumEntityWorld;
class FJsonObject;
struct FElysiumAiTraceEvent;

// What one run concluded, in the shape `seam.md`'s `index.json` scenario entry takes.
struct FElysiumArenaScenarioResult
{
	FString Name;
	// pass | fail | expected-fail | unexpected-pass | error
	FString Result;
	FString KnownRed;
	// A harness error: the record could not be staged, an action is unsupported in this host, the
	// stage never activated. Empty otherwise.
	FString Error;
	// Anything else a reader needs (an `expect_fail` record that met everything).
	FString Note;

	// The first thing that did not hold, for any run that did not hold everything (an `expect_fail`
	// record's included): which section, which entry, what it asked for, and the deadline it missed.
	bool bHasFirstUnmet = false;
	FString UnmetSection;          // expect | never | probe
	int32 UnmetIndex = INDEX_NONE;
	FString UnmetWho;
	FString UnmetKind;
	FString UnmetMatch;
	double UnmetDeadline = 0.0;
	FString UnmetReason;

	double GameSeconds = 0.0;      // scenario time when the run ended
	double WallSeconds = 0.0;
	int32 Events = 0;
	FString Trace;                 // `<name>.trace.tsv`, relative to the report directory

	TSharedRef<FJsonObject> ToJson() const;
	// One line: name, result, and the first unmet entry or the error.
	FString Summary() const;
	// Whether this result fails a suite (`fail`, `unexpected-pass`, `error`).
	bool IsVerdictFailure() const;
};

// The scenario runner: given a world that is staged (`ElysiumArenaStage::Stage`), it installs the AI
// trace sink (`FElysiumEntityWorld::SetAiTraceSink`) on that world, records every event, fires the
// record's script, evaluates its expectations, `never` entries and probes on each tick, and ends at
// the first failure, when everything is met, or at the record's duration.
//
// It holds no lab type: both hosts hand it the world, the map actor and the arena spec
// (`ElysiumArenaStage::FHost`) and tick it.
//
// **The sink only records.** It is called synchronously from inside kernel code; everything that acts
// -- the script, the verdict -- happens on the runner's own tick, never inside the call.
//
// **Expectations are ordered.** Expectation `k` is met by the first event not already consumed whose
// scenario time is at or after expectation `k-1`'s match (zero for the first) and at or before its own
// deadline, with that `who`, `kind` and `match`. An event in the same instant as the previous match
// counts: one think may claim a hint and then install the schedule that wants it.
class FElysiumArenaScenarioRunner
{
public:
	// Where scenario time zero is. A staged arena world: its `Activate` (the map actor's ready
	// broadcast, `AElysiumMapActor::ActivateRuntime`). A map host, which is not rebuilt: now.
	enum class EZero : uint8 { StageActivation, Now };

	FElysiumArenaScenarioRunner(const FElysiumArenaScenario& InRecord, const ElysiumArenaStage::FHost& InHost);
	~FElysiumArenaScenarioRunner();

	FElysiumArenaScenarioRunner(const FElysiumArenaScenarioRunner&) = delete;
	FElysiumArenaScenarioRunner& operator=(const FElysiumArenaScenarioRunner&) = delete;

	// The lab logs each expectation as it is met or missed; the headless host logs the verdict only.
	void SetLogProgress(bool bInLogProgress) { bLogProgress = bInLogProgress; }

	// Install the sink on the host map's CURRENT entity world -- call it right after the rebuild, since
	// a world's teardown clears its sink -- and arm the clock. False with a reason.
	bool Start(EZero Zero, FString& OutError);
	// One step. False once the run is over (`GetResult` is then final).
	bool Tick();
	bool IsDone() const { return bDone; }
	// End the run as a harness error.
	void Abort(const FString& Error);

	const FElysiumArenaScenarioResult& GetResult() const { return Result; }
	const FElysiumArenaScenario& GetRecord() const { return Record; }
	// Every event of the run: time, entity name, kind, text, tab-separated, one per line.
	bool WriteTrace(const FString& Path, FString& OutError) const;

private:
	struct FEvent
	{
		double Time = 0.0;                 // the world clock, raw
		FElysiumEntityHandle Entity;
		FString Name;
		FName Kind;
		FString Text;
	};

	struct FMatcher
	{
		const FElysiumArenaMatch* Spec = nullptr;   // into `Record`, which never changes after construction
		TUniquePtr<FRegexPattern> Pattern;
	};

	// One way the run can fail, so the earliest of several found in one tick is the one reported.
	struct FFailure
	{
		bool bSet = false;
		double Time = 0.0;
		FString Section;
		int32 Index = INDEX_NONE;
		FString Who;
		FString Kind;
		FString Match;
		FString Reason;
	};

	// The sink's body: append, nothing else.
	void RecordEvent(const FElysiumAiTraceEvent& Event);
	// `AElysiumMapActor::OnRuntimeReady`: the stage's `Activate` has run; zero is now.
	void OnStageActivated(AElysiumMapActor* Map);

	// The world the sink was installed on, if it is still the map's current one; null once it has been
	// torn down (whose teardown cleared the sink) or the run has detached.
	FElysiumEntityWorld* LiveWorld() const;
	// Clear the sink and the activation binding. Idempotent.
	void Detach();

	double ScenarioTime(const FEvent& Event) const { return Event.Time - ZeroWorld; }
	bool Matches(const FMatcher& Matcher, const FEvent& Event) const;
	double ExpectDeadline(int32 Index) const;

	bool ApplyPlayerAtZero(FElysiumEntityWorld& World);
	void MatchExpectations();
	void FindNeverViolation(FFailure& Out);
	void FindExpectMiss(double Now, FFailure& Out) const;
	bool FireDueActions(double Now, FElysiumEntityWorld& World);
	bool RunAction(int32 Index, FElysiumEntityWorld& World, FString& OutError);
	void TickPlayerWalk();
	bool CurrentPlayerFeet(FVector& OutFeet) const;
	void ReadDueProbes(double Now, FFailure& Out);
	bool ReadProbe(const FElysiumArenaProbeSpec& Probe, FString& OutRead, FString& OutError) const;
	bool IsComplete(double Now) const;

	// The verdict. `Failure` unset is a pass.
	void Finish(double Now, const FFailure& Failure);
	void FinishWithEndProbes(double Now);

	FElysiumArenaScenario Record;
	ElysiumArenaStage::FHost Host;
	bool bLogProgress = false;

	bool bStarted = false;
	bool bDone = false;
	FElysiumEntityWorld* InstalledWorld = nullptr;   // compared, never dereferenced unless current
	uint32 InstalledEpoch = 0;
	TWeakObjectPtr<AElysiumMapActor> ReadyMap;
	FDelegateHandle ReadyHandle;

	bool bZeroKnown = false;
	bool bZeroApplied = false;
	double ZeroWorld = 0.0;
	double StartWall = 0.0;
	double LastNow = 0.0;

	TArray<FEvent> Events;
	TArray<FMatcher> ExpectMatchers;
	TArray<FMatcher> NeverMatchers;

	int32 NextExpect = 0;
	int32 ExpectScan = 0;
	double PrevMatchTime = 0.0;
	TArray<double> MatchTimes;      // per expectation; negative while unmet
	TSet<int32> Consumed;
	int32 NeverScan = 0;

	TArray<bool> ActionFired;
	TArray<bool> ProbeRead;

	// `player_walk`: the destination the input replay is steering the player toward.
	bool bWalking = false;
	FVector WalkFeet = FVector::ZeroVector;

	FElysiumArenaScenarioResult Result;
};

#endif // !UE_BUILD_SHIPPING
