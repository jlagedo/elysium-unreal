#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumVariant.h"

// The provenance a field-6 Python call string is evaluated against. VtMB wraps the string as
// `__main__.<source>` and evals it with the firing entity's context bound (`!self`) plus the
// propagated activator (`!activator`) — see python_bridge.md. Phase 1's null host ignores it;
// M4's real evaluator (B6) consumes it.
struct FElysiumScriptContext
{
	FElysiumEntityHandle Self;       // the entity that fired the output (`!self`)
	FElysiumEntityHandle Activator;  // the propagated activator (`!activator`)
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

// Phase 1 host: logs every field-6 payload to LogElysiumScript and returns Void (0). It makes
// the Python-carrying outputs (6,851 of the game's outputs fire *only* Python) visible in the
// I/O stream immediately, with no evaluator. Replaced wholesale by the M4 host via
// UElysiumGameStateSubsystem::SetScriptHost.
class FElysiumNullScriptHost final : public IElysiumScriptHost
{
public:
	virtual FElysiumVariant Eval(const FString& Source, const FElysiumScriptContext& Ctx) override;
	virtual const TCHAR* Name() const override { return TEXT("null"); }
};
