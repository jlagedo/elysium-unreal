#pragma once

#include "CoreMinimal.h"
#include "ElysiumAnimEvent.generated.h"

// One record on a sequence's own timeline, read from the same descriptor as the grids and the
// bindings (`docs/vtmb/animation_and_movers.md` → "Sequence events and native dispatch").
//
// `Cycle` is normalized over the sequence, so it is a *phase* and not a time: the dispatcher fires
// a record when the interval the sequence advanced through contains it, which means a looping
// sequence visits the wrapped interval too. `Event` is the numeric dispatch id the handler
// switches on, and `Options` is the record's 64-byte payload — the whole argument a handler gets,
// spelled however the id's own family reads it (an integer, a bodygroup name, an `ACT_*` literal).
//
// The record carries no side effect of its own. What an id means belongs to the handler that
// claims it, which is why nothing here interprets `Event` or parses `Options`.
//
// It lives in `Public/` rather than beside the parser because it crosses the outbound service seam:
// `IElysiumEmbodiment::GetNpcEventTimeline` hands one model's timeline down to the substrate, the
// same way `Public/ElysiumStanceTypes.h` carries the stance set.
USTRUCT()
struct ELYSIUMUE_API FElysiumAnimEvent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float Cycle = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 Event = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 Type = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FString Options;
};
