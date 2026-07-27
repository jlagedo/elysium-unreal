#include "ElysiumGameStateSubsystem.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumExpr.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayer.h"
#include "Scripting/ElysiumPythonVM.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumState, Log, All);

// --- The player: live entity first, record second (11.4) -------------------------------------

FElysiumPlayer* UElysiumGameStateSubsystem::PlayerEntity() const
{
	FElysiumEntityWorld* World = CurrentEntityWorld();
	return World ? World->FindPlayer() : nullptr;
}

const FElysiumSheet& UElysiumGameStateSubsystem::PlayerSheet() const
{
	const FElysiumPlayer* Player = PlayerEntity();
	return Player ? Player->Sheet : Record.Sheet;
}

FElysiumSheet& UElysiumGameStateSubsystem::PlayerSheet()
{
	FElysiumPlayer* Player = PlayerEntity();
	return Player ? Player->Sheet : Record.Sheet;
}

UElysiumRulebookSubsystem* UElysiumGameStateSubsystem::Rulebook() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UElysiumRulebookSubsystem>() : nullptr;
}

const FElysiumStatTable* UElysiumGameStateSubsystem::Stats() const
{
	UElysiumRulebookSubsystem* Rules = Rulebook();
	if (!Rules)
	{
		return nullptr;
	}
	// The accessor loads on first touch and remembers a failure, so a broken export costs one
	// warning rather than one per read. An empty table is returned, never null — hence IsValid.
	const FElysiumStatTable& Table = Rules->Stats();
	return Table.IsValid() ? &Table : nullptr;
}

void UElysiumGameStateSubsystem::NotifyPlayerKilled()
{
	UE_LOG(LogElysiumState, Display, TEXT("the player died — ending the run"));
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameFlowSubsystem* Flow = GI->GetSubsystem<UElysiumGameFlowSubsystem>())
		{
			Flow->TriggerGameOver(EElysiumGameOverReason::Killed);
		}
	}
}

void UElysiumGameStateSubsystem::NotifyMasqueradeBreach()
{
	UE_LOG(LogElysiumState, Display, TEXT("the masquerade broke — ending the run"));
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameFlowSubsystem* Flow = GI->GetSubsystem<UElysiumGameFlowSubsystem>())
		{
			Flow->TriggerGameOver(EElysiumGameOverReason::MasqueradeBreach);
		}
	}
}

void UElysiumGameStateSubsystem::BeginNewGame(int32 Clan, bool bMale)
{
	// A fresh run starts from a clean bag: travel keeps `G` alive (R8), so without this a second
	// New Game would inherit the previous run's beat counter and latches.
	ClearAllGlobals();
	Quests.Reset();
	Snapshots.Reset();
	Visited.Reset();

	// S8 — every game-visible draw comes from an owned, seeded stream whose state is in the save
	// (`save-architecture.md` §8). A run takes one session seed; the five streams derive from it.
	ElysiumRng::SeedAll(static_cast<int32>(FPlatformTime::Cycles()));

	Record.Reset();
	// The sheet starts from `stats.txt`'s authored defaults, then takes the two identity slots the
	// front end chose. Chargen (9.4f) replaces this with the full spend.
	if (const FElysiumStatTable* Table = Stats())
	{
		Record.Sheet.SeedFrom(*Table);
	}
	Record.Sheet.SetClan(FElysiumSheet::IsValidClan(Clan) ? Clan : 2);
	Record.Sheet.SetMale(bMale);
	// A live player entity would otherwise keep the previous run's numbers until the next map
	// build; New Game is destructive by design, so re-seed it from the fresh record now.
	if (FElysiumPlayer* Player = PlayerEntity())
	{
		Player->Hydrate(Record);
	}

	SetGlobalInt(TEXT("Story_State"), -4);
	SetGlobalInt(TEXT("Tut_Jack"), 0);
	SetGlobalInt(TEXT("Tut_Patch"), 0);
	SetGlobalInt(TEXT("Linux_Wine"), 1);

	UE_LOG(LogElysiumState, Display,
		TEXT("new game: clan %d (%s), %s — Story_State=-4, Tut_Jack=0, Tut_Patch=0, Linux_Wine=1"),
		Record.Sheet.Clan(), FElysiumSheet::ClanName(Record.Sheet.Clan()),
		Record.Sheet.IsMale() ? TEXT("male") : TEXT("female"));
}

void UElysiumGameStateSubsystem::EndSession()
{
	ClearAllGlobals();
	Quests.Reset();
	Snapshots.Reset();
	Visited.Reset();
	// Order matters: quit-to-menu asks for the travel first and the engine tears the old world down
	// at the end of the frame, so without this the dying map's player would dehydrate back into the
	// record cleared below.
	if (FElysiumEntityWorld* World = CurrentEntityWorld())
	{
		World->ForgetPlayer();
	}
	Record.Reset();
	// The clock is session time (`curtime`), so it rewinds with the run. ResetClock also clears any
	// hold, scale and armed dev step, and re-stamps the engine side.
	TimeCtl.ResetClock();
	UE_LOG(LogElysiumState, Display,
		TEXT("session ended — G, quests, the map snapshots, the player record and the clock cleared"));
}

// --- The per-map snapshots (11.9) -------------------------------------------------------------

const FElysiumMapSnapshot* UElysiumGameStateSubsystem::FindMapSnapshot(const FString& Map) const
{
	return Snapshots.Find(Map);
}

void UElysiumGameStateSubsystem::StoreMapSnapshot(FElysiumMapSnapshot&& Snapshot)
{
	if (!Snapshot.IsValid())
	{
		return;
	}
	const FString Key = Snapshot.MapName;
	Visited.AddUnique(Key);   // first-visit order; re-freezing a map does not move it
	Snapshots.Add(Key, MoveTemp(Snapshot));
}

void UElysiumGameStateSubsystem::SetMapSnapshots(TMap<FString, FElysiumMapSnapshot>&& In)
{
	Snapshots = MoveTemp(In);
}

void UElysiumGameStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// S1 — the time facade reaches the engine (pause, dilation) through the game instance's
	// current world, resolved per call so travel never leaves it holding a dead one.
	TimeCtl.Bind(GetGameInstance());

	// P5 5.4 / P9 9.3 — real evaluation is the map-load default: field-6 payloads, logic_pythoncheck
	// gates, and ScheduleTask deferred sources all run live, through the embedded CPython VM when it
	// is available (so level-script names resolve) and the expression evaluator otherwise.
	// `elysium.script.live 0` swaps in the null host — the whole surface goes dark together.
	ScriptHostPtr = MakePreferredScriptHost();

	// `elysium.g <name> [value]` — inspect/poke the G store by hand. No args dumps every
	// set flag; one arg reads (miss -> 0); a second arg writes (integer if it parses, else
	// string; the literal `none` deletes the key, matching G's None-deletes semantics).
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.g"),
		TEXT("elysium.g [name [value]] — read/write a G global flag (no args: dump all)"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				TArray<FString> Keys = GlobalKeys();
				Keys.Sort();
				UE_LOG(LogElysiumState, Display, TEXT("G: %d flags"), Keys.Num());
				for (const FString& Key : Keys)
				{
					UE_LOG(LogElysiumState, Display, TEXT("  %s = %s"), *Key, *GetGlobal(Key).Describe());
				}
				return;
			}

			const FString& Key = Args[0];
			if (Args.Num() == 1)
			{
				UE_LOG(LogElysiumState, Display, TEXT("G.%s = %s"), *Key, *GetGlobal(Key).Describe());
				return;
			}

			const FString& Raw = Args[1];
			if (Raw.Equals(TEXT("none"), ESearchCase::IgnoreCase))
			{
				ClearGlobal(Key);
			}
			else if (Raw.IsNumeric())
			{
				SetGlobalInt(Key, FCString::Atoi(*Raw));
			}
			else
			{
				SetGlobal(Key, FElysiumVariant::String(Raw));
			}
			UE_LOG(LogElysiumState, Display, TEXT("G.%s = %s"), *Key, *GetGlobal(Key).Describe());
		}),
		ECVF_Cheat));

	// `elysium.eval <expr>` — evaluate one expression against the current map + G store and print the
	// value (error-to-false: a bad expression logs the reason and reads Void). Read-only sugar over
	// the P5 5.2 evaluator; the args are re-joined so quoting is optional.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.eval"),
		TEXT("elysium.eval <expression> — evaluate a script expression (e.g. G.Tut_Elev, 1+2, ent.field)"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const FString Src = FString::Join(Args, TEXT(" ")).TrimStartAndEnd();
			if (Src.IsEmpty()) { UE_LOG(LogElysiumState, Display, TEXT("usage: elysium.eval <expression>")); return; }
			FString Err;
			const FElysiumVariant R = EvalScript(Src, Err);
			if (Err.IsEmpty()) { UE_LOG(LogElysiumState, Display, TEXT("eval %s = %s"), *Src, *R.Describe()); }
			else { UE_LOG(LogElysiumState, Warning, TEXT("eval %s -> error: %s"), *Src, *Err); }
		}),
		ECVF_Cheat));

	// `elysium.exec <stmt>` — run one or more ';'-separated statements (assignment / bare call), e.g.
	// `elysium.exec G.Tut_Elev = 1`. This is the field-6 shape; useful to poke story state by hand.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.exec"),
		TEXT("elysium.exec <statement> — run a script statement (e.g. G.Tut_Elev = 1; door1.Open())"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const FString Src = FString::Join(Args, TEXT(" ")).TrimStartAndEnd();
			if (Src.IsEmpty()) { UE_LOG(LogElysiumState, Display, TEXT("usage: elysium.exec <statement>")); return; }
			FString Err;
			const FElysiumVariant R = EvalScript(Src, Err);
			if (Err.IsEmpty()) { UE_LOG(LogElysiumState, Display, TEXT("exec %s -> %s"), *Src, *R.Describe()); }
			else { UE_LOG(LogElysiumState, Warning, TEXT("exec %s -> error: %s"), *Src, *Err); }
		}),
		ECVF_Cheat));

	// `elysium.script.live [0|1]` — arm/disarm live script evaluation (no arg reports state). On by
	// default (5.4): field-6 payloads, logic_pythoncheck gates, and ScheduleTask sources run live. `0`
	// swaps in the null host (logs + Void) so the whole scripting surface goes dark together, for A/B.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.script.live"),
		TEXT("elysium.script.live [0|1] — toggle live script evaluation (default on; 0 = null host for A/B)"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumState, Display, TEXT("elysium.script.live = %d (host: %s)"),
					IsLiveScriptEval() ? 1 : 0, ScriptHostPtr ? ScriptHostPtr->Name() : TEXT("none"));
				return;
			}
			SetLiveScriptEval(FCString::Atoi(*Args[0]) != 0);
			UE_LOG(LogElysiumState, Display, TEXT("elysium.script.live = %d (host: %s)"),
				IsLiveScriptEval() ? 1 : 0, ScriptHostPtr ? ScriptHostPtr->Name() : TEXT("none"));
		}),
		ECVF_Cheat));

	// `elysium.script.cpython [0|1]` — swap the field-6/pythoncheck host between the embedded
	// CPython 2.7 VM (1, the default where available) and the ElysiumExpr evaluator (0), for A/B.
	// Either way the current map's level script is re-imported into the new host. No arg reports
	// the current host.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.script.cpython"),
		TEXT("elysium.script.cpython [0|1] — route scripting through embedded CPython 2.7 (1) or ElysiumExpr (0)"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumState, Display, TEXT("script host: %s (cpython available: %d)"),
					ScriptHostPtr ? ScriptHostPtr->Name() : TEXT("none"), FElysiumPythonVM::IsAvailable() ? 1 : 0);
				return;
			}
			if (FCString::Atoi(*Args[0]) != 0)
			{
				SetScriptHost(MakeUnique<FElysiumCPythonScriptHost>(this));
			}
			else
			{
				SetScriptHost(MakeUnique<FElysiumExprScriptHost>(this));
			}
			UE_LOG(LogElysiumState, Display, TEXT("script host -> %s (level script '%s': %s)"),
				ScriptHostPtr ? ScriptHostPtr->Name() : TEXT("none"), *LevelScriptModule,
				bLevelScriptLoaded ? TEXT("loaded") : *LevelScriptLoadError);
		}),
		ECVF_Cheat));

	// --- S1: the time facade, reachable by name ------------------------------------------
	// All three drive FElysiumTimeControl, so each moves the clock and engine time together —
	// thinks, the event queue, movers and ScheduleTask on one side; actor ticks, physics,
	// animation and the camera blend on the other.

	// `elysium.timescale [scale]` — one knob for game time. 0.25 slows movers, the queue,
	// animation and the camera blend by the same factor, because there is only one factor.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.timescale"),
		TEXT("elysium.timescale [scale] — read/set game time scale (clock + engine dilation together)"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() > 0)
			{
				TimeCtl.SetScale(FCString::Atod(*Args[0]));
			}
			UE_LOG(LogElysiumState, Display, TEXT("timescale %.4fx%s (now %.2f s)"),
				TimeCtl.GetScale(), TimeCtl.IsPaused() ? TEXT("  [paused]") : TEXT(""), Clock.GetNow());
		}),
		ECVF_Cheat));

	// `elysium.pause [0|1]` — hold the world with both halves (no arg toggles).
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.pause"),
		TEXT("elysium.pause [0|1] — hold/release the world (clock + engine pause; no arg toggles)"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			const bool bPause = (Args.Num() > 0) ? (FCString::Atoi(*Args[0]) != 0) : !TimeCtl.IsPaused();
			TimeCtl.SetPaused(bPause);
			UE_LOG(LogElysiumState, Display, TEXT("world %s (now %.2f s)"),
				bPause ? TEXT("held") : TEXT("running"), Clock.GetNow());
		}),
		ECVF_Cheat));

	// `elysium.step [n]` — release n frames while held, then hold again.
	ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.step"),
		TEXT("elysium.step [n] — release n frames (default 1) while the world is held"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (!TimeCtl.IsPaused())
			{
				UE_LOG(LogElysiumState, Display, TEXT("elysium.step: the world is not held (elysium.pause 1 first)"));
				return;
			}
			const int32 Frames = (Args.Num() > 0) ? FMath::Max(1, FCString::Atoi(*Args[0])) : 1;
			TimeCtl.StepFrames(Frames);
			UE_LOG(LogElysiumState, Display, TEXT("stepping %d frame(s)"), Frames);
		}),
		ECVF_Cheat));
}

void UElysiumGameStateSubsystem::Deinitialize()
{
	for (IConsoleObject* Obj : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Obj);
	}
	ConsoleObjects.Empty();

	Globals.Empty();
	Quests.Empty();
	// Unbind before the rewind: the game instance is going away, so the facade must not reach
	// for a world to re-stamp pause/dilation onto.
	TimeCtl.Bind(nullptr);
	TimeCtl.ResetClock();
	EvalHistory.Empty();
	NativeCallHistory.Empty();
	NativeCallCounts.Empty();
	ScriptHostPtr.Reset();

	Super::Deinitialize();
}

void UElysiumGameStateSubsystem::SetScriptHost(TUniquePtr<IElysiumScriptHost> InHost)
{
	// M4 swaps the null host for the real evaluator; ignore a null argument so ScriptHost()
	// always dereferences a live host.
	if (!InHost)
	{
		return;
	}
	ScriptHostPtr = MoveTemp(InHost);

	// A new host starts with an empty namespace, so re-import the current map's level script into
	// it. Without this, toggling `elysium.script.cpython` mid-map would leave the CPython host
	// evaluating against a bare __main__ and every level constant would read as NameError.
	if (!LevelScriptModule.IsEmpty())
	{
		LoadLevelScript(LevelScriptModule);
	}
}

TUniquePtr<IElysiumScriptHost> UElysiumGameStateSubsystem::MakePreferredScriptHost()
{
	if (FElysiumCPythonScriptHost::IsAvailable())
	{
		TUniquePtr<FElysiumCPythonScriptHost> Py = MakeUnique<FElysiumCPythonScriptHost>(this);
		if (Py->IsUsable())
		{
			return Py;
		}
		// The interpreter did not come up (missing python27.dll, bad PythonHome). Its evals would
		// all be Void, which reads as error-to-false everywhere — quieter and far more misleading
		// than falling back to the evaluator that does work.
		UE_LOG(LogElysiumState, Warning,
			TEXT("CPython VM failed to start — falling back to the ElysiumExpr host"));
	}
	return MakeUnique<FElysiumExprScriptHost>(this);
}

bool UElysiumGameStateSubsystem::LoadLevelScript(const FString& Module)
{
	LevelScriptModule = Module.TrimStartAndEnd();
	bLevelScriptLoaded = false;
	LevelScriptLoadError.Reset();

	if (LevelScriptModule.IsEmpty())
	{
		return false;   // the map's worldspawn names no level script; nothing to import
	}
	if (!ScriptHostPtr)
	{
		LevelScriptLoadError = TEXT("no script host");
		return false;
	}

	bLevelScriptLoaded = ScriptHostPtr->LoadLevelScript(LevelScriptModule, LevelScriptLoadError);
	if (bLevelScriptLoaded)
	{
		UE_LOG(LogElysiumState, Display, TEXT("level script '%s' loaded into host '%s'"),
			*LevelScriptModule, ScriptHostPtr->Name());
	}
	else
	{
		UE_LOG(LogElysiumState, Warning, TEXT("level script '%s' not loaded: %s"),
			*LevelScriptModule, *LevelScriptLoadError);
	}
	return bLevelScriptLoaded;
}

FElysiumVariant UElysiumGameStateSubsystem::GetGlobal(const FString& Key) const
{
	if (const FElysiumVariant* Found = Globals.Find(Key))
	{
		return *Found;
	}
	return FElysiumVariant::Int(0); // default-on-miss = 0 (confirmed, python_bridge.md)
}

void UElysiumGameStateSubsystem::SetGlobal(const FString& Key, const FElysiumVariant& Value)
{
	// Assigning None (Void here) deletes the key — tp_setattr semantics.
	if (Value.IsVoid())
	{
		Globals.Remove(Key);
		return;
	}
	Globals.Add(Key, Value);
}

bool UElysiumGameStateSubsystem::HasGlobal(const FString& Key) const
{
	return Globals.Contains(Key);
}

void UElysiumGameStateSubsystem::ClearGlobal(const FString& Key)
{
	Globals.Remove(Key);
}

void UElysiumGameStateSubsystem::ClearAllGlobals()
{
	Globals.Empty();
}

TArray<FString> UElysiumGameStateSubsystem::GlobalKeys() const
{
	TArray<FString> Keys;
	Globals.GetKeys(Keys);
	return Keys;
}

int32 UElysiumGameStateSubsystem::GetQuestState(const FString& Quest) const
{
	const int32* Found = Quests.Find(Quest);
	return Found ? *Found : 0;
}

void UElysiumGameStateSubsystem::SetQuestState(const FString& Quest, int32 State)
{
	Quests.Add(Quest, State);
}

bool UElysiumGameStateSubsystem::HasQuest(const FString& Quest) const
{
	return Quests.Contains(Quest);
}

void UElysiumGameStateSubsystem::SetLiveScriptEval(bool bEnable)
{
	if (bEnable == IsLiveScriptEval())
	{
		return;
	}
	if (bEnable)
	{
		SetScriptHost(MakePreferredScriptHost());
	}
	else
	{
		SetScriptHost(MakeUnique<FElysiumNullScriptHost>());
	}
}

bool UElysiumGameStateSubsystem::IsLiveScriptEval() const
{
	// Live = any real evaluator (expr or cpython); only the null host is "dark".
	return ScriptHostPtr && FCString::Strcmp(ScriptHostPtr->Name(), TEXT("null")) != 0;
}

FElysiumEntityWorld* UElysiumGameStateSubsystem::CurrentEntityWorld() const
{
	const UGameInstance* GI = GetGameInstance();
	const UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	const AElysiumMapActor* Map = Maps ? Maps->GetCurrentMap() : nullptr;
	return Map ? Map->GetEntityWorld() : nullptr;
}

FElysiumVariant UElysiumGameStateSubsystem::EvalScript(const FString& Source, FString& OutError)
{
	// Through the installed host, so a hand-run eval sees exactly what a field-6 payload sees —
	// same evaluator, same namespace (the loaded level script), same error-to-false. Routing this
	// straight to ElysiumExpr would report NameError for the level constants the game resolves.
	// Both the expression and statement shapes go down one path: the CPython host tries
	// Py_eval_input then Py_file_input, and ElysiumExpr::Exec returns its last statement's value.
	OutError.Reset();
	if (!ScriptHostPtr)
	{
		OutError = TEXT("no script host");
		return FElysiumVariant::Void();
	}
	FElysiumScriptContext Ctx;
	Ctx.World = CurrentEntityWorld();   // Self/Activator stay Invalid for a hand-run eval
	return ScriptHostPtr->Eval(Source, Ctx, &OutError);   // the host records into the eval ring
}

void UElysiumGameStateSubsystem::RecordEval(const FString& Source, const FElysiumVariant& Result,
	bool bError, const FString& Error)
{
	FElysiumEvalRecord Rec;
	Rec.Time = Clock.GetNow();
	Rec.Source = Source;
	Rec.Result = bError ? Error : Result.Describe();
	Rec.bError = bError;
	EvalHistory.Add(MoveTemp(Rec));
	if (EvalHistory.Num() > EvalHistoryMax)
	{
		EvalHistory.RemoveAt(0, EvalHistory.Num() - EvalHistoryMax, EAllowShrinking::No);
	}
}

void UElysiumGameStateSubsystem::RecordNativeCall(const FString& Call, const FElysiumVariant& Result,
	bool bStub, FName Name)
{
	NativeCallCounts.FindOrAdd(Name)++;

	FElysiumNativeCallRecord Rec;
	Rec.Time = Clock.GetNow();
	Rec.Call = Call;
	Rec.Result = Result.Describe();
	Rec.bStub = bStub;
	NativeCallHistory.Add(MoveTemp(Rec));
	if (NativeCallHistory.Num() > NativeCallHistoryMax)
	{
		NativeCallHistory.RemoveAt(0, NativeCallHistory.Num() - NativeCallHistoryMax, EAllowShrinking::No);
	}
}
