#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumScriptHost.h"   // FElysiumScriptContext
#include "ElysiumVariant.h"

class FElysiumEntityWorld;
class UElysiumGameStateSubsystem;

// Forward-declared so this header stays clear of Python.h, which every .cpp that touches the
// C-API must include LAST, after all Unreal headers (Private/ThirdParty/ElysiumPython.h).
// Repeating CPython's own typedef of the same incomplete type is legal and keeps the signatures
// honest without pulling the header in.
struct _object;
typedef struct _object PyObject;

// 9.3/B2 — the `vampire` module's OBJECT surface for the embedded CPython VM.
//
// This is the datamap reflection layer described in python_bridge.md, ported onto the P1 class
// registry. `Entity.__getattr__` resolves a name against the entity's class-chain tables — an
// INPUT name manufactures a bound callable that fires through the real chokepoint, a FIELD name
// marshals the live value — and `__setattr__` mirrors it, writing keyable fields and falling
// through to the instance `__dict__` otherwise (entities are property bags on both sides). One
// namespace, no second dispatch: `ent.ScriptHide()` in a script and the I/O wire
// `OnTrigger -> ScriptHide` are the same lookup.
//
// The Character/player object is separate because there is no player entity yet (P8/P9): it is
// backed by UElysiumGameStateSubsystem's FElysiumPlayerSheet, and its methods go through the
// shared ElysiumScriptNatives surface — the same stubs, defaults, and call counters the expr
// host reports, so an A/B host swap changes nothing but the interpreter.
namespace ElysiumPy
{
#if defined(ELYSIUM_WITH_CPYTHON) && ELYSIUM_WITH_CPYTHON

	// --- Marshalling (python_bridge.md "Divergence": VtMB's own fieldtype_t rules) -------------
	PyObject* VariantToPy(const FElysiumVariant& V);   // new reference; Void -> None
	FElysiumVariant PyToVariant(PyObject* O);          // borrowed; unknown types -> their repr()
	// Pull the pending exception into a "Type: message" string and clear it. Never throws.
	FString FetchPyError();

	// --- Types + module globals ---------------------------------------------------------------
	// Ready the Entity / Player / bound-method types and add the 11 `vampire` module globals
	// (python_bridge.md's module table) to an already-created module. Idempotent per process.
	bool InstallEntityBindings(PyObject* Module, FString& OutError);

	// A new `vampire.Entity` for a handle — or a new reference to None when the handle is unset,
	// which is FindEntityByName's own documented miss result ("Returns None if not found").
	PyObject* NewEntity(const FElysiumEntityHandle& Handle);

	// --- Evaluation provenance ------------------------------------------------------------------
	// FElysiumPythonVM pushes one of these around every eval/callback, so a script's entity
	// lookups resolve against the world that is delivering, and the deferred work it schedules
	// (ScheduleTask/ChangeMap) is attributed to the firing entity as `!self`.
	struct FScopedContext
	{
		explicit FScopedContext(const FElysiumScriptContext& Ctx);
		~FScopedContext();

		FScopedContext(const FScopedContext&) = delete;
		FScopedContext& operator=(const FScopedContext&) = delete;

	private:
		FElysiumScriptContext Saved;
	};

	// The world names resolve against: the running eval's delivering world, else the current
	// map's (so a console-run eval works between deliveries). Null between maps.
	FElysiumEntityWorld* CurrentWorld();
	const FElysiumScriptContext& CurrentContext();

#endif // ELYSIUM_WITH_CPYTHON
}
