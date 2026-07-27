#include "ElysiumScriptHost.h"

#include "ElysiumContentPaths.h"
#include "ElysiumExpr.h"
#include "ElysiumGameStateSubsystem.h"
#include "Scripting/ElysiumPythonVM.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumScript, Log, All);

FElysiumVariant FElysiumNullScriptHost::Eval(const FString& Source, const FElysiumScriptContext& /*Ctx*/,
	FString* OutError)
{
	// Scripting is switched off (`elysium.script.live 0`). Make the call observable and return Void
	// so the caller's truthiness test reads false (error-to-false, RE3), exactly as a failed retail
	// eval would. Not an error condition — nothing was attempted.
	UE_LOG(LogElysiumScript, Verbose, TEXT("[null] eval __main__.%s"), *Source);
	if (OutError) { OutError->Reset(); }
	return FElysiumVariant::Void();
}

FElysiumVariant FElysiumExprScriptHost::Eval(const FString& Source, const FElysiumScriptContext& Ctx,
	FString* OutError)
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
	if (OutError) { *OutError = Env.bError ? Env.Error : FString(); }
	return Result;
}

bool FElysiumCPythonScriptHost::IsAvailable()
{
	return FElysiumPythonVM::IsAvailable();
}

FElysiumCPythonScriptHost::FElysiumCPythonScriptHost(UElysiumGameStateSubsystem* InState)
	: State(InState)
{
	// Point the VM's `G` proxy at this subsystem's store, then bring the interpreter up so the
	// first field-6 delivery doesn't pay the init cost (and any init failure logs here, not later).
	FElysiumPythonVM::Get().SetGameState(InState);
	FString Err;
	bVmStarted = FElysiumPythonVM::Get().EnsureStarted(Err);
	if (!bVmStarted)
	{
		UE_LOG(LogElysiumScript, Warning, TEXT("[cpython] VM unavailable: %s (evals return Void)"), *Err);
	}
}

bool FElysiumCPythonScriptHost::LoadLevelScript(const FString& Module, FString& OutError)
{
	// scripts/<module>/<module>.py — the layout UE_extract_scripts.py mirrors. Missing is reported,
	// not fatal: several maps name a levelscript whose file the install does not ship.
	const FString Abs = FPaths::ConvertRelativePathToFull(FElysiumContentPaths::ScriptModuleFile(Module));
	if (!IFileManager::Get().FileExists(*Abs))
	{
		OutError = FString::Printf(TEXT("no such script: %s"), *Abs);
		return false;
	}
	FString ModName;
	return FElysiumPythonVM::Get().LoadLevelScript(Abs, ModName, OutError);
}

FElysiumVariant FElysiumCPythonScriptHost::Eval(const FString& Source, const FElysiumScriptContext& Ctx,
	FString* OutError)
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
	if (OutError) { *OutError = Err; }
	return Result;
}
