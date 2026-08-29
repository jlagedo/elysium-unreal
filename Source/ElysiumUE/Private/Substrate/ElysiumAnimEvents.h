#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"

// VtMB's sequence-event dispatcher, as a pure rule — the `ElysiumDice` shape beside the substrate's
// other decision leaves. No world, no clock, no engine service, no UObject: a cursor and this
// frame's phase in, the records that fired out.
//
// The rule is `CBaseAnimating::DispatchAnimEvents` `0x10091880`, recovered in
// `docs/vtmb/animation_and_movers.md` → "Sequence events and native dispatch". The engine stores
// the last checked cycle on the animating object at `+0x658`, scans the sequence's 76-byte records
// and fires every one whose cycle satisfies `last_cycle <= event.cycle < current_cycle`; a looping
// sequence that passed 1.0 also visits the wrapped interval, exactly once. Records that share a
// cycle fire in the order the file declares them, which is why `FElysiumBlendTable::Events` is an
// array and is never sorted.
//
// What an id MEANS belongs to whoever claims it. Nothing here reads `Event`, parses `Options` or
// filters on the server band: `Advance` answers which records the interval contained, and the
// caller decides what to do with each.

// `FElysiumAnimEventCursor`, the state this rule advances, is declared beside the record it walks
// in `Public/ElysiumAnimEvent.h`: `FElysiumAnimating` owns one per polled channel, and that class
// is declared in a public header.

namespace ElysiumAnimEvents
{
	// The server dispatch band. `DispatchAnimEvents` hands `HandleAnimEvent` only ids BELOW this;
	// everything at or above it is a client-side id the server dispatcher never routes.
	inline constexpr int32 ServerDispatchCeiling = 5000;

	// The WEAPON band. `CBaseCombatCharacter::HandleAnimEvent` (`0x1032e330`, the body six classes
	// share) acts on none of 3000..3999 itself: it hands every id in that range to the active
	// weapon's virtual `Operator_HandleAnimEvent` `+0x5c8` and answers with what the weapon said.
	// The band is the CHARACTER's routing rule; which ids inside it mean anything belongs to the
	// weapon family that receives them (`docs/vtmb/animation_and_movers.md` → "Sequence events and
	// native dispatch").
	inline constexpr int32 WeaponBandFirst = 3000;
	inline constexpr int32 WeaponBandLast  = 3999;

	inline bool IsWeaponBand(int32 Event)
	{
		return Event >= WeaponBandFirst && Event <= WeaponBandLast;
	}

	// Advance one cursor by one frame and collect what the interval contained, in file order.
	//
	// `Timeline` may be null or empty — most sequences declare no timeline at all, which is an
	// absence rather than a fault. The cursor still advances, so a clip that gains a timeline
	// mid-play cannot fire a backlog.
	//
	// `OutFired` is RESET before the walk. The pointers it carries alias `Timeline`'s storage and
	// are valid only as long as the caller's timeline is.
	void Advance(const TArray<FElysiumAnimEvent>* Timeline, const FElysiumClipPhase& Phase,
		FElysiumAnimEventCursor& InOut, TArray<const FElysiumAnimEvent*>& OutFired);
}

// The census of event ids nothing has claimed yet.
//
// **The census IS the observability for an unclaimed id.** An id with no handler is not a failure —
// it is work that has not landed — and a warning per occurrence would fire dozens of times a second
// on a walking cast and bury every real defect in the log. So an unclaimed record is counted here
// and read back with `elysium.animevents`, which is exactly the shape `elysium.stubs` gives an
// unimplemented surface.
//
// The tally is process-wide, like the stub tally: it is a work list across a session rather than
// state belonging to one map, so it must survive map travel. Hanging it on `FElysiumEntityWorld`
// would throw it away on every load screen, and hanging it on a GameInstance subsystem would put an
// engine object inside a substrate rule the tests run with no world at all.
//
// **Game thread only, and unsynchronised**, like the stub tally for the same reason: every writer is
// the world's own event pass and every reader is a console verb, a Cog frame or an automation case.
// A caller off the game thread needs its own funnel rather than a lock here.
namespace ElysiumAnimEventCensus
{
	// One row: an id, the model that owns the clip, and the label it fired from.
	struct FRow
	{
		int32 Event = 0;
		FString OwnerStem;
		FString Label;
		// The 64-byte payload of the first record counted under this key, kept so a reader can see
		// what a handler would be given. Later records with the same key do not overwrite it.
		FString Options;
		int32 Count = 0;
		// True when the id sits at or above `ServerDispatchCeiling`, so it never reached a handler at
		// all — a different fact from "a handler saw it and refused it", and the two must not read
		// the same on the work list.
		bool bAboveServerBand = false;
	};

	// Count one unclaimed record. Keyed on (id, stem, label), which is what makes the tally a work
	// list: one row per thing to implement, however many bodies fire it.
	//
	// True when this call CREATED the row — the first time that id has been seen on that clip. It is
	// the once-per-key gate the dispatcher's Verbose line rides on, so the log names each unclaimed
	// id once instead of once per occurrence, and the row count is the same fact a test can read.
	bool Record(const FElysiumAnimEvent& Event, const FString& OwnerStem, const FString& Label,
		bool bAboveServerBand);

	// Every row, most-fired first. Ties break on the id so two reads of one state agree.
	void Collect(TArray<FRow>& Out);
	void Clear();
	int32 Num();
}
