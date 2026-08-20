#pragma once

#include "CoreMinimal.h"

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
struct FElysiumAnimEvent
{
	float Cycle = 0.f;
	int32 Event = 0;
	int32 Type = 0;
	FString Options;
};

// One channel's place on one clip's timeline, carried across frames.
//
// It rides here with the record rather than with the rule that advances it
// (`Private/Substrate/ElysiumAnimEvents.h`) because `FElysiumAnimating` owns one per polled
// channel, and that class is declared in a public header.
//
// The identity is `(OwnerStem, Label, PlayId)`, and all three are needed: two banks can declare the
// same label, and the same clip re-armed is a new play whose timeline fires again from zero.
// `bArmed` is what separates "the cursor has never seen this play" from "the cursor sits at cycle 0
// of it" — the first frame of a play is the interval `[0, Cycle)`, so a record authored at cycle 0
// fires, and a second frame at the same phase fires nothing.
struct FElysiumAnimEventCursor
{
	FString OwnerStem;
	FString Label;
	uint32 PlayId = 0;
	float LastCycle = 0.f;
	bool bArmed = false;

	void Reset()
	{
		OwnerStem.Reset();
		Label.Reset();
		PlayId = 0;
		LastCycle = 0.f;
		bArmed = false;
	}
};
