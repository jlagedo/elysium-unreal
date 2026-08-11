#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

// The per-wire accounting instrument (`docs/architecture/gameplay-systems-architecture.md` §7,
// `docs/vtmb/sp_tutorial_1-event-surface.md` §14). Acceptance has to classify every authored wire,
// and "the map ran" is not an answer: a wire that never fired, one whose target does not exist, one
// whose receiver has no such input and one that delivered into a system with no visible side effect
// all look identical from the outside. This is the runtime half — a tally per authored output row,
// plus the identity that lets a delivery be attributed back to the row that produced it. The offline
// join against the research inventory is a separate deliverable and reads the dumped report.

// One authored wire's identity: the entity that owns the row, the output key it hangs off, and the
// row's ordinal in that entity's def `Outputs` array. The ordinal is the def-array position rather
// than a per-output counter, because that is the index `OutputTimesRemaining` is aligned with and
// the one FireOutput already has in hand; the report also states the per-output ordinal, which is
// how an offline inventory enumerates rows.
//
// `Output` is redundant against (SourceIndex, Row) while the def array is the one this world loaded,
// and it is carried anyway: a queued record survives into a save, and a self-describing identity is
// readable there without resolving a def. Comparison folds case through FName, matching how every
// other output-name comparison in the substrate works.
struct FElysiumWireRef
{
	int32 SourceIndex = INDEX_NONE;   // the firing entity's handle index (its position in EntityList)
	FName Output;                     // the On*/Out* key the row fires on
	int32 Row = INDEX_NONE;           // the row's ordinal in the source def's Outputs array

	bool IsSet() const { return SourceIndex != INDEX_NONE && Row != INDEX_NONE; }

	bool operator==(const FElysiumWireRef& Other) const
	{
		return SourceIndex == Other.SourceIndex && Row == Other.Row && Output == Other.Output;
	}
	bool operator!=(const FElysiumWireRef& Other) const { return !(*this == Other); }
};

inline uint32 GetTypeHash(const FElysiumWireRef& Wire)
{
	return HashCombine(HashCombine(GetTypeHash(Wire.SourceIndex), GetTypeHash(Wire.Row)),
		GetTypeHash(Wire.Output));
}

// What one wire did this session. Counts, not opinions: the classification the acceptance report
// draws is a function of these six numbers, so a change in what "delivered" means is a change to
// the report and not to the instrument.
//
// A single fire can produce many deliveries — a targetname is non-unique and a trailing-`*` pattern
// fans out (51 shipped outputs aim at `patrol_cop_*` alone) — so Delivered is a delivery count and
// is expected to exceed Fired on a fan-out wire. Deliveries never resolve twice for one event: a
// due event reaches exactly one of Delivered / UnknownInput per target, and UnknownTarget instead
// when the whole resolve came back empty.
struct FElysiumWireTally
{
	// The row was enqueued: it matched the fired output name and its `times` gate let it through.
	int32 Fired = 0;
	// The row matched but `times` was already spent, so nothing was queued. No sink is notified on
	// this path — no event exists to notify about — which is why the counter is the only witness.
	int32 TimesExhausted = 0;
	// A due delivery from this wire found a live target with this input and the thunk ran.
	int32 Delivered = 0;
	// A due delivery from this wire resolved to no live entity (dead wire — retail data has these).
	int32 UnknownTarget = 0;
	// A due delivery from this wire reached a live target whose class chain wires no such input.
	int32 UnknownInput = 0;
	// This wire's field-6 payload was handed to the script host. A row that carries Python and never
	// forwards it either never fired or ran with no host installed.
	int32 PythonForwarded = 0;

	// Nothing ever reached this row — neither a fire nor a refusal. The never-fired set is the point
	// of the whole report, so it gets a name rather than being re-derived at each reader.
	bool IsUntouched() const { return Fired == 0 && TimesExhausted == 0; }
};

// One row of the dumped report: the authored wire joined to its tally. Built from the def array, so
// every authored wire appears whether or not it ever did anything.
struct FElysiumWireReportRow
{
	FElysiumWireRef Wire;

	FString SourceName;      // the source entity's authored targetname (may be empty)
	FString SourceClass;     // the source entity's classname
	FString Target;          // the row's target expression, verbatim (`!self`, `patrol_cop_*`, ...)
	FString Input;           // the input name on the target
	FString Param;           // the authored parameter string
	FString Python;          // field-6 source, empty when the row carries none
	float Delay = 0.0f;
	int32 AuthoredTimes = -1;   // the def's `times` as authored (-1 = unlimited)

	// The row's ordinal among the rows of THIS output name, in authoring order. `Wire.Row` is the
	// def-array position; this is what an offline enumeration of "the third OnTrigger row" means.
	int32 OutputRow = INDEX_NONE;

	// The source entity was synthesized at runtime (npc_maker.Spawn, a scripted create) rather than
	// parsed from the map, so it is not part of the authored surface acceptance measures.
	bool bRuntimeSource = false;

	FElysiumWireTally Tally;

	bool HasPython() const { return !Python.IsEmpty(); }
	bool HasTarget() const { return !Target.IsEmpty(); }
};

// The single label acceptance reads a wire by. The counts are the record; this is the answer to
// "what went wrong with THIS wire", picked most-diagnostic-first, because a wire that both delivered
// and hit a dead target is a broken wire and not a working one.
//
// NeverFired and Exhausted are the two ways a wire produced nothing: nothing ever asked for it,
// versus its `times` budget was already spent when something did. Pending is a wire that fired and
// whose delivery has not resolved — normally a delayed row still sitting in the queue, and a
// standing finding if it never clears.
enum class EElysiumWireOutcome : uint8
{
	NeverFired,
	Exhausted,
	UnknownTarget,
	UnknownInput,
	Delivered,
	PythonOnly,
	Pending,
};

inline EElysiumWireOutcome ElysiumWireOutcome(const FElysiumWireReportRow& Row)
{
	const FElysiumWireTally& T = Row.Tally;
	if (T.Fired == 0)
	{
		return T.TimesExhausted > 0 ? EElysiumWireOutcome::Exhausted : EElysiumWireOutcome::NeverFired;
	}
	if (T.UnknownTarget > 0) { return EElysiumWireOutcome::UnknownTarget; }
	if (T.UnknownInput > 0)  { return EElysiumWireOutcome::UnknownInput; }
	if (T.Delivered > 0)     { return EElysiumWireOutcome::Delivered; }
	if (!Row.HasTarget() && T.PythonForwarded > 0) { return EElysiumWireOutcome::PythonOnly; }
	return EElysiumWireOutcome::Pending;
}

// The label as it appears in the dumped JSON and in the console line. Stable strings — the offline
// joiner keys on them.
inline const TCHAR* ElysiumWireOutcomeName(EElysiumWireOutcome Outcome)
{
	switch (Outcome)
	{
	case EElysiumWireOutcome::NeverFired:    return TEXT("never_fired");
	case EElysiumWireOutcome::Exhausted:     return TEXT("exhausted");
	case EElysiumWireOutcome::UnknownTarget: return TEXT("unknown_target");
	case EElysiumWireOutcome::UnknownInput:  return TEXT("unknown_input");
	case EElysiumWireOutcome::Delivered:     return TEXT("delivered");
	case EElysiumWireOutcome::PythonOnly:    return TEXT("python_only");
	default:                                 return TEXT("pending");
	}
}

// The one-line answer over a whole report. Authored and runtime-spawned rows are counted apart:
// acceptance is a statement about the map's authored surface, and an npc_maker's synthesized def is
// not part of it.
struct FElysiumWireSummary
{
	int32 Authored = 0;          // authored wires in the map (the denominator)
	int32 RuntimeRows = 0;       // wires on runtime-synthesized defs, reported but not measured
	int32 Fired = 0;             // authored wires that produced at least one queued delivery
	int32 FullyDelivered = 0;    // fired with every outcome resolved and no unknowns
	int32 NeverFired = 0;        // authored wires nothing ever reached
	int32 Exhausted = 0;         // reached only after `times` was spent
	int32 UnknownTarget = 0;     // fired into a name this map has no live entity for
	int32 UnknownInput = 0;      // reached a live entity whose class wires no such input
	int32 Pending = 0;           // fired, nothing resolved yet
	int32 PythonRows = 0;        // authored wires carrying a field-6 payload
	int32 PythonForwarded = 0;   // ...of which the payload actually reached a script host
};

inline FElysiumWireSummary ElysiumWireSummarize(TConstArrayView<FElysiumWireReportRow> Rows)
{
	FElysiumWireSummary S;
	for (const FElysiumWireReportRow& Row : Rows)
	{
		if (Row.bRuntimeSource)
		{
			++S.RuntimeRows;
			continue;
		}
		++S.Authored;
		if (Row.HasPython())
		{
			++S.PythonRows;
			if (Row.Tally.PythonForwarded > 0) { ++S.PythonForwarded; }
		}
		if (Row.Tally.Fired > 0) { ++S.Fired; }

		switch (ElysiumWireOutcome(Row))
		{
		case EElysiumWireOutcome::NeverFired:    ++S.NeverFired; break;
		case EElysiumWireOutcome::Exhausted:     ++S.Exhausted; break;
		case EElysiumWireOutcome::UnknownTarget: ++S.UnknownTarget; break;
		case EElysiumWireOutcome::UnknownInput:  ++S.UnknownInput; break;
		case EElysiumWireOutcome::Pending:       ++S.Pending; break;
		default:                                 ++S.FullyDelivered; break;
		}
	}
	return S;
}
