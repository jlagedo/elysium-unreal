#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumVariant.h"

class FElysiumEntityWorld;
class UElysiumSessionSubsystem;

// The provenance a field-6 Python call string is evaluated against. VtMB wraps the string as
// `__main__.<source>` and evals it with the firing entity's context bound (`!self`) plus the
// propagated activator (`!activator`) — see `docs/vtmb/python_bridge.md`. The null host ignores it;
// the evaluator consumes it. `World` is the entity world the firing output belongs to
// (the substrate a bare targetname resolves against); DeliverEvent sets it to the delivering world.
struct FElysiumScriptContext
{
	FElysiumEntityHandle Self;       // the entity that fired the output (`!self`)
	FElysiumEntityHandle Activator;  // the propagated activator (`!activator`)
	FElysiumEntityWorld* World = nullptr;  // the delivering world (targetname/input dispatch), or null
};

// The seam the scripting evaluator slots in behind. All field-6 Python payloads flow
// through this one interface. `Eval` is total (never throws): a failed eval returns Void,
// which is the error-to-false case the dialogue/pythoncheck paths rely on
// (`docs/vtmb/python_bridge.md`).
class IElysiumScriptHost
{
public:
	virtual ~IElysiumScriptHost() = default;

	// Evaluate a call string as `__main__.<Source>`; returns the result (Void on error/none).
	// OutError, when given, receives the failure reason — empty on success. The return value alone
	// cannot distinguish "evaluated to None" from "raised", and the debug layer needs to.
	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext& Ctx,
		FString* OutError = nullptr) = 0;

	// Import the map's level-script module (worldspawn.levelscript) so subsequent evals resolve its
	// names. Only a host with a real interpreter can honour this; the others report false with a
	// reason, which is not an error — a map's field-6 payloads still evaluate, just without level
	// constants. Called at map load and again whenever a host is installed.
	virtual bool LoadLevelScript(const FString& Module, FString& OutError)
	{
		OutError = FString::Printf(TEXT("host '%s' cannot import level scripts"), Name());
		return false;
	}

	// The map-epoch boundary, forwarded by UElysiumSessionSubsystem because a host is plain
	// C++ and cannot subscribe to a UObject delegate itself. A host that resolves level scripts
	// against per-map interpreter state releases it here. Hosts that carry none do nothing.
	virtual void OnMapEpochRetired() {}

	// Short identity for logs (e.g. "null").
	virtual const TCHAR* Name() const = 0;
};

// The dark host: logs every field-6 payload to LogElysiumScript and returns Void (0), so the
// Python-carrying outputs (6,851 of the game's outputs fire *only* Python) stay visible in the I/O
// stream while no evaluation runs. `elysium.script.live 0` swaps this in to turn scripting off.
class FElysiumNullScriptHost final : public IElysiumScriptHost
{
public:
	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext& Ctx,
		FString* OutError = nullptr) override;
	virtual const TCHAR* Name() const override { return TEXT("null"); }
};

// The real expression evaluator host (ElysiumExpr). Field-6 is a statement string (an assignment
// or a bare call — see `docs/vtmb/python_bridge.md`'s exec call path), so this dispatches through
// ElysiumExpr::Exec, binding the payload's context (`!self`/`!activator`/world) and the `G` store
// from the game-state subsystem. Total: a failed eval yields Void (error-to-false). Also serves
// logic_pythoncheck (via FElysiumEntityWorld::EvalCondition) and ScheduleTask's deferred sources
// at delivery. `elysium.script.live 0` swaps in the null host to turn all this off.
class FElysiumExprScriptHost final : public IElysiumScriptHost
{
public:
	explicit FElysiumExprScriptHost(UElysiumSessionSubsystem* InState) : State(InState) {}
	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext& Ctx,
		FString* OutError = nullptr) override;
	virtual const TCHAR* Name() const override { return TEXT("expr"); }

private:
	UElysiumSessionSubsystem* State = nullptr;  // owns the `G` store; outlives this host
};

// The embedded-CPython host — routes field-6 + pythoncheck through the real CPython 2.7 VM
// (FElysiumPythonVM), so payloads resolve level-script names (`cCelerity`, callbacks,
// `__main__.*`) that ElysiumExpr cannot. Binds the VM's `G` proxy to this subsystem's store on
// construction, so `G.x` reads/writes hit the same C++ bag the expr host uses. Total: a failed
// eval yields Void (error-to-false). Installed by `elysium.script.cpython 1`; when the module is
// built without CPython (ELYSIUM_WITH_CPYTHON=0) the VM is inert and every eval is Void.
class FElysiumCPythonScriptHost final : public IElysiumScriptHost
{
public:
	explicit FElysiumCPythonScriptHost(UElysiumSessionSubsystem* InState);
	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext& Ctx,
		FString* OutError = nullptr) override;
	virtual bool LoadLevelScript(const FString& Module, FString& OutError) override;
	virtual void OnMapEpochRetired() override;
	virtual const TCHAR* Name() const override { return TEXT("cpython"); }

	// True when the embedded VM actually came up. A failed init leaves every eval Void, which is
	// indistinguishable from error-to-false — so the subsystem checks this before making this host
	// the map-load default and falls back to the expr host instead of silently going dark.
	bool IsUsable() const { return bVmStarted; }

	static bool IsAvailable();  // module built with the vendored CPython SDK

private:
	UElysiumSessionSubsystem* State = nullptr;
	bool bVmStarted = false;
};
