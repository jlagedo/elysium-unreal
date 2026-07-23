#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumVariant.h"

class FElysiumEntityWorld;
class UElysiumGameStateSubsystem;

// The provenance a field-6 Python call string is evaluated against. VtMB wraps the string as
// `__main__.<source>` and evals it with the firing entity's context bound (`!self`) plus the
// propagated activator (`!activator`) — see python_bridge.md. Phase 1's null host ignores it;
// the 5.2 evaluator (B6) consumes it. `World` is the entity world the firing output belongs to
// (the substrate a bare targetname resolves against); DeliverEvent sets it to the delivering world.
struct FElysiumScriptContext
{
	FElysiumEntityHandle Self;       // the entity that fired the output (`!self`)
	FElysiumEntityHandle Activator;  // the propagated activator (`!activator`)
	FElysiumEntityWorld* World = nullptr;  // the delivering world (targetname/input dispatch), or null
};

// B6 — the seam the M4 scripting evaluator slots in behind. All field-6 Python payloads flow
// through this one interface from day one, so dispatch is complete before the evaluator exists.
// `Eval` is total (never throws): a failed eval returns Void, which is the error-to-false case
// the dialogue/pythoncheck paths rely on (RE3, python_bridge.md).
class IElysiumScriptHost
{
public:
	virtual ~IElysiumScriptHost() = default;

	// Evaluate a call string as `__main__.<Source>`; returns the result (Void on error/none).
	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext& Ctx) = 0;

	// Short identity for logs (e.g. "null").
	virtual const TCHAR* Name() const = 0;
};

// The A/B-off host: logs every field-6 payload to LogElysiumScript and returns Void (0), so the
// Python-carrying outputs (6,851 of the game's outputs fire *only* Python) stay visible in the I/O
// stream while no evaluation runs. It is the map-load default no longer (5.4 promoted the expr host);
// `elysium.script.live 0` swaps this back in to compare against with scripting dark.
class FElysiumNullScriptHost final : public IElysiumScriptHost
{
public:
	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext& Ctx) override;
	virtual const TCHAR* Name() const override { return TEXT("null"); }
};

// The real expression evaluator host (ElysiumExpr) — the map-load default as of 5.4. Field-6 is a
// statement string (an assignment or a bare call — see python_bridge.md's exec call path), so this
// dispatches through ElysiumExpr::Exec, binding the payload's context (`!self`/`!activator`/world)
// and the `G` store from the game-state subsystem. Total: a failed eval yields Void (error-to-false,
// RE3). Also serves logic_pythoncheck (via FElysiumEntityWorld::EvalCondition) and ScheduleTask's
// deferred sources at delivery. `elysium.script.live 0` swaps in the null host to turn all this off.
class FElysiumExprScriptHost final : public IElysiumScriptHost
{
public:
	explicit FElysiumExprScriptHost(UElysiumGameStateSubsystem* InState) : State(InState) {}
	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext& Ctx) override;
	virtual const TCHAR* Name() const override { return TEXT("expr"); }

private:
	UElysiumGameStateSubsystem* State = nullptr;  // owns the `G` store; outlives this host
};

// The embedded-CPython host (P5 5.5 / 9.3) — routes field-6 + pythoncheck through the real
// CPython 2.7 VM (FElysiumPythonVM), so payloads resolve level-script names (`cCelerity`,
// callbacks, `__main__.*`) that ElysiumExpr cannot. Binds the VM's `G` proxy to this subsystem's
// store on construction, so `G.x` reads/writes hit the same C++ bag the expr host uses. Total: a
// failed eval yields Void (error-to-false, RE3). Installed by `elysium.script.cpython 1`; when the
// module is built without CPython (ELYSIUM_WITH_CPYTHON=0) the VM is inert and every eval is Void.
class FElysiumCPythonScriptHost final : public IElysiumScriptHost
{
public:
	explicit FElysiumCPythonScriptHost(UElysiumGameStateSubsystem* InState);
	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext& Ctx) override;
	virtual const TCHAR* Name() const override { return TEXT("cpython"); }

private:
	UElysiumGameStateSubsystem* State = nullptr;
};
