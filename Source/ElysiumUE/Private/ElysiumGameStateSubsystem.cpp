#include "ElysiumGameStateSubsystem.h"

#include "ElysiumExpr.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPythonVM.h"

#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumState, Log, All);

void UElysiumGameStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// P5 5.4 — the real expression evaluator is the map-load default: field-6 payloads,
	// logic_pythoncheck gates, and ScheduleTask deferred sources all run live. `elysium.script.live 0`
	// swaps in the null host (logs + Void) for A/B — the whole scripting surface goes dark together.
	ScriptHostPtr = MakeUnique<FElysiumExprScriptHost>(this);

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
			const FElysiumVariant R = EvalScript(Src, /*bExec*/ false, Err);
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
			const FElysiumVariant R = EvalScript(Src, /*bExec*/ true, Err);
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
	// CPython 2.7 VM (1) and the ElysiumExpr evaluator (0, the default). The CPython host resolves
	// level-script names the expr host cannot (`cCelerity`, callbacks); load a script first with
	// `elysium.py.load`. No arg reports the current host.
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
			UE_LOG(LogElysiumState, Display, TEXT("script host -> %s"),
				ScriptHostPtr ? ScriptHostPtr->Name() : TEXT("none"));
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
	Clock.Reset();
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
	if (InHost)
	{
		ScriptHostPtr = MoveTemp(InHost);
	}
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
		SetScriptHost(MakeUnique<FElysiumExprScriptHost>(this));
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

FElysiumVariant UElysiumGameStateSubsystem::EvalScript(const FString& Source, bool bExec, FString& OutError)
{
	ElysiumExpr::FEnv Env;
	Env.State = this;
	Env.Ctx.World = CurrentEntityWorld();   // Self/Activator stay Invalid for a hand-run eval
	const FElysiumVariant Result = bExec ? ElysiumExpr::Exec(Source, Env) : ElysiumExpr::Eval(Source, Env);
	OutError = Env.bError ? Env.Error : FString();
	RecordEval(Source, Result, Env.bError, Env.Error);
	return Result;
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
