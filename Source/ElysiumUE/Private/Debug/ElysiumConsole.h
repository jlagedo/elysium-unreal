#pragma once

#include "CoreMinimal.h"

// VtMB's console surface -- the fifth scripting surface (docs/vtmb/python_bridge.md). It is a table of
// **aliases** and **cvars** parsed from `out/cfg/*.cfg` (Valve console syntax), plus the execute
// path a `ccmd` attribute-set drives: a script runs `c.patchtype = ""` and the console executes
// the command `patchtype`. Resolution, per command word -- the precedence is stated here once and
// asserted by `Elysium.Substrate.Console` (roadmap 11.6):
//
//   * a **registered command** (FElysiumCommands, the VtMB bindable-verb inventory) -> run it,
//     `+`/`-` edges included. Commands outrank aliases, matching Source's own Cmd_ExecuteString,
//     which is why a user alias cannot shadow `+forward`;
//   * an **alias** -> recursively execute its expansion (`;`-separated commands, aliases nest);
//   * a known **cvar** with args -> set it; with no args -> a no-op (retail prints the value);
//   * otherwise -> **fall through to Python** (the sink evals the line in `__main__`).
//
// The Unofficial Patch's Basic/Plus switch rides on exactly this: `user.cfg` carries
// `alias patchtype "setPlus()"`, so `c.patchtype = ""` -> alias -> `setPlus()` -> Python fallthrough
// -> the level-script function. `setPlus`/`setBasic` are named nowhere else in the install.
//
// Plain C++, no Python/UObject dependency, so it is unit-testable and the ccmd/cvar PyObjects
// (ElysiumPythonEntity) forward into it. It is owned by FElysiumPythonVM, which supplies the sink.
class FElysiumConsole
{
public:
	// The Python fallthrough: an unresolved command line is exec'd in `__main__`. Returns true if
	// it WAS Python (parsed + defined, whether or not its body then raised); false if it is neither
	// a known alias/cvar nor valid Python -- an engine cvar/command we do not model (e.g.
	// `rope_shake 0`, `+speed`), which the console then drops with a Verbose log.
	using FPythonSink = TFunction<bool(const FString& /*CommandLine*/)>;
	void SetPythonSink(FPythonSink InSink) { PythonSink = MoveTemp(InSink); }

	// Parse default.cfg, config.cfg, autoexec.cfg, user.cfg (in that order; later shadows earlier)
	// from `CfgDir`. Missing files are skipped. Clears the tables first, so it is safe to re-seed.
	void LoadFromCfgDir(const FString& CfgDir);

	// Parse cfg text (one directive per line) into the alias/cvar tables, additively. Used by
	// LoadFromCfgDir; exposed for unit tests.
	void ParseText(const FString& CfgText);

	// Load CfgDir the first time this is called and never again. The command bus reaches the console
	// from outside the Python VM (`elysium.cmd`, a bound key), and that path must resolve the
	// patch's aliases whether or not the VM ever came up.
	void EnsureSeeded(const FString& CfgDir);

	// Execute one console command line (a `ccmd` attribute-set forms `name` + optional " " + value).
	// Splits on top-level `;`, resolves each part (alias / cvar / Python fallthrough).
	void Execute(const FString& CommandLine);

	// Declare a cvar the *engine* registers rather than a cfg file: the camera surface (11.7) and
	// anything else compiled code owns. A declared name is a **known** cvar, so `cam_idealdist 50`
	// resolves as a cvar set rather than falling through to Python, and it reads back its default when
	// no cfg on disk carries it. Declarations survive a re-seed; a cfg value shadows one.
	void DeclareCvar(const FString& Name, const FString& Default);

	// The `cvar` object surface. Get returns the stored value, the declared default, or empty on a
	// miss (never raises).
	FString GetCvar(const FString& Name) const;
	void SetCvar(const FString& Name, const FString& Value);

	// Introspection (Cog / tests).
	bool HasAlias(const FString& Name) const { return Aliases.Contains(Name.ToLower()); }
	const FString* FindAlias(const FString& Name) const { return Aliases.Find(Name.ToLower()); }
	int32 NumAliases() const { return Aliases.Num(); }
	int32 NumCvars() const { return Cvars.Num(); }

private:
	void ParseLine(const FString& Line);
	// Execute a single `;`-free statement; Depth guards runaway alias recursion.
	void ExecuteStatement(const FString& Statement, int32 Depth);

	// Split a console line into words, honouring double-quoted runs (which may hold spaces and `;`).
	static void Tokenize(const FString& Line, TArray<FString>& OutTokens);
	// Split a command string on `;` that are NOT inside double quotes.
	static void SplitStatements(const FString& Line, TArray<FString>& OutParts);

	// True for a name the store knows: a cfg-parsed value or an engine-declared one.
	bool IsKnownCvar(const FString& LowerName) const;

	TMap<FString, FString> Aliases;      // name (lowercased) -> command expansion
	TMap<FString, FString> Cvars;        // name (lowercased) -> value
	TMap<FString, FString> CvarDefaults; // name (lowercased) -> engine-registered default
	FPythonSink PythonSink;
	bool bSeeded = false;
};
