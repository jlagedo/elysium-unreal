#pragma once

#include "CoreMinimal.h"
#include "ElysiumUserCmd.h"

// S7 — one command registry (roadmap 11.6, `runtime-architecture.md` §8.2). VtMB has no action
// abstraction: **an action is a console command string**, and the Unofficial Patch's whole
// vocabulary is aliases over those strings (`f` → `vm_feed` → `checkFeed()`), so a key bound to a
// compiled verb and a key bound to a user alias have to be indistinguishable. That is only true if
// the compiled verbs have names, which is what this is — the inventory `controls.md` documents,
// declared once, so a key, a gamepad button, a level script, a `.dlg` action, `-ExecCmds` and an
// MCP tool all fire the same verb the same way.
//
// Plain C++, no UObject: the registry, its precedence and its ±-pair semantics are asserted with no
// world and no input device (`Elysium.Substrate.Commands`).

enum class EElysiumCmdKind : uint8
{
	// A verb that happens once when it is invoked (`togglecamera`, `slot3`, `snapshot`).
	Once,
	// VtMB's `+cmd` / `-cmd` pair: a press and a release, latched for as long as the key is held.
	ButtonPair,
};

// Where a verb comes from, for the options screen's grouping (10.6/8.10) and the dump verb.
enum class EElysiumCmdGroup : uint8
{
	Movement,
	Combat,
	Camera,
	Interface,
	System,
	Cheat,
};

const TCHAR* LexToString(EElysiumCmdKind Kind);
const TCHAR* LexToString(EElysiumCmdGroup Group);

// One invocation, as a handler sees it.
struct FElysiumCommandCall
{
	// The canonical verb, `+`/`-` already stripped ("use", "speed", "vhotkey").
	FName Name;

	// Everything after the first word, unquoted ("#3" for `vhotkey #3`, "quick" for `save quick`).
	FString Args;

	// ButtonPair: true for `+cmd`, false for `-cmd`. Always true for a Once verb.
	bool bPressed = true;

	EElysiumCmdKind Kind = EElysiumCmdKind::Once;
};

using FElysiumCommandHandler = TFunction<void(const FElysiumCommandCall&)>;

// A declared verb. The declarations are static data (the VtMB inventory); the *implementation* is
// installed separately and comes and goes with whatever owns it.
struct FElysiumCommandDef
{
	FName Name;
	EElysiumCmdKind Kind = EElysiumCmdKind::Once;
	EElysiumCmdGroup Group = EElysiumCmdGroup::System;

	// The FElysiumUserCmd bit a ButtonPair verb latches, or 0 for a pair the user command does not
	// carry. Latching is intrinsic to the verb, not to whoever implements it: `+forward` moves the
	// player because it is `+forward`, with no handler in sight.
	uint64 Button = 0;

	// Human text for the options screen and the dump. For a verb nothing implements yet, this names
	// the task that owns it, so the dump reads as a work list rather than as a wall of "stub".
	const TCHAR* Help = nullptr;
};

// Identity for an installed implementation; zero is "nothing installed".
struct FElysiumCommandBinding
{
	int32 Id = 0;
	FName Name;

	bool IsValid() const { return Id != 0; }
	void Reset() { Id = 0; Name = NAME_None; }
};

class FElysiumCommands
{
public:
	// Process-wide, because the verb inventory is: it is the game's vocabulary, not a world's. The
	// first call declares the VtMB inventory.
	static FElysiumCommands& Get();

	// --- Declaration -----------------------------------------------------------------------
	// Declare a verb. A name declared twice keeps the first declaration (the static inventory wins
	// over anything that tries to add a verb behind its back).
	void Declare(const FElysiumCommandDef& Def);

	const FElysiumCommandDef* Find(FName Name) const;
	bool IsDeclared(FName Name) const { return Find(Name) != nullptr; }
	const TArray<FElysiumCommandDef>& All() const { return Defs; }

	// --- Implementation --------------------------------------------------------------------
	// Install the implementation of a declared verb. Implementations stack, so a system that owns a
	// verb for a while (a cutscene, a screen) can take it and give it back; the most recently
	// installed one runs. Binding an undeclared name fails loudly rather than inventing a verb.
	FElysiumCommandBinding Bind(FName Name, FElysiumCommandHandler Handler);
	// Release an implementation and clear the handle. A stale handle is a no-op.
	bool Unbind(FElysiumCommandBinding& Binding);
	bool IsBound(FName Name) const;

	// --- Execution -------------------------------------------------------------------------
	// Run one `;`-free statement. Returns **false when the first word is not a declared verb**,
	// which is the console's cue to try an alias — the registry never guesses.
	bool Execute(const FString& Statement);

	// The same, pre-split: used by the input router, which already knows the verb and the edge.
	bool Invoke(FName Name, bool bPressed, const FString& Args = FString());

	// --- The user command ------------------------------------------------------------------
	// Where a ButtonPair verb's latch lands. The local player's router owns the builder and installs
	// itself here; with no sink the button half of a `+cmd` is simply dropped, which is what a
	// headless logic world and the front end both want.
	void SetUserCmdSink(FElysiumUserCmdBuilder* Sink) { UserCmdSink = Sink; }
	FElysiumUserCmdBuilder* GetUserCmdSink() const { return UserCmdSink; }

	// --- Introspection ---------------------------------------------------------------------
	// How many times each verb has run this session — the coverage report behind `elysium.commands`,
	// the same shape the script-natives table uses.
	int32 CallCount(FName Name) const;
	// One line per verb: name, kind, group, whether it is implemented, call count, help.
	FString Describe(const FString& Filter) const;

	// Test seam: drop every installed implementation and every call count, keeping the declarations.
	void ResetImplementations();

private:
	FElysiumCommands() = default;

	struct FImpl
	{
		int32 Id = 0;
		FElysiumCommandHandler Handler;
	};

	struct FEntry
	{
		FElysiumCommandDef Def;
		TArray<FImpl> Impls;     // last is the live one
		int32 Calls = 0;
	};

	FEntry* FindEntry(FName Name);
	const FEntry* FindEntry(FName Name) const;

	// Resolve one console word to a verb + edge. `+use`/`-use` strip to `use` only when `use` is a
	// declared **pair**; a bare word resolves to itself. Anything else fails.
	bool Resolve(const FString& Word, FName& OutName, bool& bOutPressed) const;

	TArray<FElysiumCommandDef> Defs;      // declaration order = report order
	TMap<FName, int32> Index;             // lowercased name -> Entries index
	TArray<FEntry> Entries;

	FElysiumUserCmdBuilder* UserCmdSink = nullptr;
	int32 NextBindingId = 1;
};

namespace ElysiumCommands
{
	// The VtMB bindable-verb inventory (`controls.md` § "What is bindable"), declared into the
	// registry. Called once by FElysiumCommands::Get(); exposed so a test can assert the table
	// itself. `vphysicshand` is deliberately absent — it is bound by both shipped `default.cfg`s and
	// exists in no binary and no script (`decisions.md` 2026-07-25).
	void DeclareVtmbInventory(FElysiumCommands& Registry);

	// Case-folded canonical form of a verb name.
	FName Canonical(const FString& Word);
}
