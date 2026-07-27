#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumScriptHost.h"   // FElysiumScriptContext (Self / Activator / World)
#include "ElysiumVariant.h"

class FElysiumEntityWorld;
class UElysiumGameStateSubsystem;

// The engine `vampire`-module surface, shared by every scripting host (python_bridge.md
// "Binding"): the 11 module globals and the 24 Character methods `vampire.dll` binds, their
// backing (or their stub default), and the debug-layer call log.
//
// It lives here rather than inside one host because BOTH hosts dispatch it — the embedded
// CPython VM (the map-load default) and ElysiumExpr (the A/B fallback) — and a name that
// stubs differently depending on which host is installed would make the A/B comparison lie.
// One table, one set of stub defaults, one native-call counter set behind the Cog Scripting
// window, so 9.3's demand-driven "fill the stubs the running content actually hits" reads the
// same numbers whichever host produced them.
//
// The four `Find*` globals are NOT here: each host returns them in its own object model (a
// `vampire.Entity` instance for CPython, a handle-carrying FVal for the evaluator), so only
// their *record* goes through this module.
namespace ElysiumScriptNatives
{
	// One row per bound engine name, rendered by the Scripting Cog window (name, kind, backing
	// status) so the native surface is inspectable without reading source.
	struct FNativeBinding
	{
		const TCHAR* Name;
		bool bMethod;         // true = Character method (dispatched off an object); false = module global
		const TCHAR* Status;  // short backing note
	};
	TArrayView<const FNativeBinding> NativeBindings();

	bool IsNativeGlobal(const FString& Name);
	bool IsCharacterMethod(const FString& Name);

	// --- OneOfSet (9.7d) -------------------------------------------------------------------------
	// VtMB's 1-based one-of-N selector, the gate behind 589 `.dlg` rows (`docs/script_api.md`):
	// `(roll % count) == which - 1`. The corpus authors it in sets — N sibling rows carrying the same
	// choice text, row i gated on `OneOfSet(i, N)` — so exactly one row passes only when every gate in
	// the set sees ONE roll. `OneOfSetRoll` is that value: one draw per engine frame, which is the
	// granularity a turn's gates are gathered at (`FElysiumDlgConversation::EnterNpcLine` evaluates a
	// whole choice list in one burst). VtMB reads its roll off an engine counter whose identity is
	// still open; what the corpus pins down is the stability, not the source.
	int32 OneOfSetRoll();
	// Pin the roll (>= 0) or restore the per-frame draw (< 0). `elysium.script.oneofset` is the console
	// face of the same switch; tests use it to make the selector deterministic.
	void SetOneOfSetRoll(int32 PinnedRoll);
	// The selector over the current roll. `count <= 0` reads false rather than dividing by zero, and a
	// `which` outside 1..count reads false on its own, since the modulo lands in [0, count-1].
	bool OneOfSet(int32 Which, int32 Count);

	// Case-insensitive stat/skill name normalisation (python_bridge.md: retail casing is
	// inconsistent — `Humanity`/`humanity`, `F_Seduction`, ...). An unknown name passes through.
	FString CanonicalStatName(const FString& Raw);

	// Comma-joined Describe() of call arguments, for a native-call log line.
	FString DescribeArgs(TArrayView<const FElysiumVariant> Args);

	// Log a native call + push it onto the game state's native-call ring and per-name counter.
	// `bStub` flags a pure-logging call (no backing system yet). The `Find*` globals call this
	// directly, since their dispatch stays with each host.
	void Record(UElysiumGameStateSubsystem* State, FName Name, const FString& Display,
		const FElysiumVariant& Result, bool bStub);

	// A Character method dispatched off the PC (`Self` unset) or an NPC entity handle. SetQuest /
	// GetQuestState route to the real quest map; everything else — including a name outside the
	// known 24, which retail's forgiving surface also accepts — logs a stub and returns its
	// default. Records the call either way.
	FElysiumVariant CallCharacterMethod(UElysiumGameStateSubsystem* State, FElysiumEntityWorld* World,
		const FElysiumEntityHandle& Self, FName Method, TArrayView<const FElysiumVariant> Args);

	// The module globals whose result is a plain value: ScheduleTask and ChangeMap (both real — they go
	// through the event queue), OneOfSet and IsClan/IsPCMalk (real — the selector and the player sheet's
	// clan), and SquadSeesPlayer, the one with no backing here — no shipped script calls it.
	// CreateEntityNoSpawn/CallEntitySpawn return/take an Entity object, so — like the Find*
	// globals — the CPython host implements them directly (ElysiumPythonEntity) and only the expr
	// fallback lands here as a stub. `Ctx` supplies the provenance the deferred work is attributed to.
	// Records the call. A name this does not know logs as a stub.
	FElysiumVariant CallSimpleGlobal(UElysiumGameStateSubsystem* State, FElysiumEntityWorld* World,
		const FElysiumScriptContext& Ctx, FName Name, TArrayView<const FElysiumVariant> Args);
}
