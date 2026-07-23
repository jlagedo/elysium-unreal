#include "ElysiumScriptHost.h"

#include "ElysiumExpr.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPythonVM.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumScript, Log, All);

FElysiumVariant FElysiumNullScriptHost::Eval(const FString& Source, const FElysiumScriptContext& /*Ctx*/)
{
	// No evaluator yet (M4/B6). Make the call observable and return Void so the caller's
	// truthiness test reads false (error-to-false, RE3), exactly as a failed retail eval would.
	UE_LOG(LogElysiumScript, Verbose, TEXT("[null] eval __main__.%s"), *Source);
	return FElysiumVariant::Void();
}

FElysiumVariant FElysiumExprScriptHost::Eval(const FString& Source, const FElysiumScriptContext& Ctx)
{
	// Field-6 is a statement string (an assignment or a bare call), so run it through Exec. Bind the
	// delivery's provenance + world (from Ctx) and the `G` store (from the subsystem). Record the
	// outcome for the debug layer, then return the value (Void on error -> error-to-false).
	ElysiumExpr::FEnv Env;
	Env.State = State;
	Env.Ctx = Ctx;
	const FElysiumVariant Result = ElysiumExpr::Exec(Source, Env);
	if (State)
	{
		State->RecordEval(Source, Result, Env.bError, Env.Error);
	}
	return Result;
}

FElysiumCPythonScriptHost::FElysiumCPythonScriptHost(UElysiumGameStateSubsystem* InState)
	: State(InState)
{
	// Point the VM's `G` proxy at this subsystem's store, then bring the interpreter up so the
	// first field-6 delivery doesn't pay the init cost (and any init failure logs here, not later).
	FElysiumPythonVM::Get().SetGameState(InState);
	FString Err;
	if (!FElysiumPythonVM::Get().EnsureStarted(Err))
	{
		UE_LOG(LogElysiumScript, Warning, TEXT("[cpython] VM unavailable: %s (evals return Void)"), *Err);
	}
}

FElysiumVariant FElysiumCPythonScriptHost::Eval(const FString& Source, const FElysiumScriptContext& Ctx)
{
	// Run the payload through the real CPython VM. It resolves against the loaded level-script
	// namespace (level constants, __main__ builtins) — the whole point of the embed. Record the
	// outcome for the debug layer; Void on any error (error-to-false, RE3).
	FString Err;
	const FElysiumVariant Result = FElysiumPythonVM::Get().Eval(Source, Ctx, Err);
	if (State)
	{
		State->RecordEval(Source, Result, /*bError*/ !Err.IsEmpty(), Err);
	}
	return Result;
}
