#pragma once

#include "CoreMinimal.h"
#include "Debug/ElysiumConsole.h"
#include "ElysiumVariant.h"

class UElysiumGameStateSubsystem;
struct FElysiumScriptContext;

// Process-global embedded CPython 2.7 VM (P5 5.5 decision -> 9.3 foundation).
//
// VtMB's real VM is stock CPython 2.1 (vampire_python21.dll, 653 exports); the loose level scripts
// ($ELYSIUM_EXPORT_ROOT/scripts, 16.5k lines) run 1:1 on 2.7 -- verified against all 36 scripts: no string
// exceptions (removed in 2.6), no `from __future__`, classic division and old-style classes are the
// default on both. So we embed the maintained qnox/python-2.7 2.7.18 build rather than reimplement
// the language (ElysiumExpr is an expression evaluator; these scripts have 1,119 defs, 45 classes,
// try/except, imports, exec -- past what any hand-rolled subset can carry).
//
// This owns: the vendored PythonHome + Py_Initialize/Finalize and the `vampire` C-module -- `G`
// proxied onto UElysiumGameStateSubsystem (attribute AND mapping protocol, since VtMB's `G[k]` is
// its `G.k`), plus the entity/native object surface in ElysiumPythonEntity (the Entity and Player
// types and the 11 module globals) -- and the Python bootstrap that star-imports that module into
// `__main__`, which is the bus every level script reaches the engine through.
//
// Win64 only. When ELYSIUM_WITH_CPYTHON==0 every method is inert (EnsureStarted fails, Eval returns
// Void) and the existing null / expr script hosts stand in unchanged.
//
// Threading: single interpreter, game-thread only. The GIL is held by the game thread from
// Py_Initialize on; nothing here releases it, so no PyGILState dance is needed for the PoC.
class FElysiumPythonVM
{
public:
	static FElysiumPythonVM& Get();

	// True when the module was compiled with the vendored CPython SDK present.
	static bool IsAvailable();

	bool IsStarted() const { return bStarted; }

	// Load the DLL, point Python at the vendored PythonHome, Py_Initialize, register `vampire`,
	// run the bootstrap. Idempotent. Returns false (with OutError) if unavailable or init fails.
	bool EnsureStarted(FString& OutError);

	// Bind the G store that vampire.G reads/writes (the current game instance's subsystem). The
	// CPython script host sets this when installed; the console verbs fall back to the live world.
	void SetGameState(UElysiumGameStateSubsystem* InState);
	UElysiumGameStateSubsystem* GameState() const;

	// Run a statement block in a scratch namespace (stdout/stderr captured to LogElysiumPy).
	// Returns false + OutError on exception.
	bool RunSimpleString(const FString& Code, FString& OutError);

	// Evaluate a field-6 / pythoncheck payload against the loaded level-script namespace (or
	// __main__ when none is loaded). An expression yields its marshaled value; a statement yields
	// Void on success. Any exception -> Void (error-to-false, RE3) with OutError set. Never throws.
	FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext& Ctx, FString& OutError);

	// Import a level script (absolute .py path) as a module after injecting the vampire builtins
	// into __main__. On success OutModuleName is the module name and it becomes the eval namespace.
	bool LoadLevelScript(const FString& AbsPath, FString& OutModuleName, FString& OutError);

	// Call a top-level callback (e.g. "OnMasqueradeEnd") in the loaded level script.
	bool FireCallback(const FString& FuncName, FString& OutError);

	// --- Console bridge (9.3b) -----------------------------------------------------------
	// The alias/cvar store the `vampire.ccmd` / `vampire.cvar` objects drive. Seeded from
	// out/cfg at EnsureStarted; the ccmd attribute-set path calls Console().Execute(...).
	FElysiumConsole& Console() { return ConsoleStore; }
	// The console->Python fallthrough sink: exec a command line in __main__. Returns true when it
	// was Python (parsed with all names defined -- even if the body then raised, which PyErr_Prints
	// error-to-false), false on NameError/SyntaxError (an engine cvar/command we do not model).
	bool ExecConsoleLine(const FString& Line);

	// --- Cog / debug introspection -------------------------------------------------------
	FString GetVersion() const;
	TArray<FString> GetSysPath() const;
	FString GetLoadedModule() const { return LoadedModule; }
	// Top-level function names in the loaded module matching Filter (case-insensitive substring;
	// empty = all). Used by the Cog window to list the On* callbacks.
	TArray<FString> GetModuleCallables(const FString& Filter) const;

private:
	FElysiumPythonVM() = default;

	bool bStarted = false;
	void* DllHandle = nullptr;
	FString LoadedModule;
	FElysiumConsole ConsoleStore;
	TWeakObjectPtr<UElysiumGameStateSubsystem> GameStateWeak;
};
