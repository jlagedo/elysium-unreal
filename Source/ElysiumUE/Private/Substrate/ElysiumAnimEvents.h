#pragma once

#include "CoreMinimal.h"

#include "Containers/ArrayView.h"
#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"

// VtMB's sequence-event dispatcher: `CBaseAnimating::DispatchAnimEvents` `0x10091880` and the
// per-layer body `0x10098cd0`, as two bodies over one sequence's words, so the NPC (`PostRun
// 0x1026c7c0`), the player (`CBasePlayer::PostThink 0x1016be10`) and the camera (`0x10071840`) run
// the same listing. Recovered in `docs/vtmb/animation_events.md` § "Who calls the dispatcher, and
// where against the frame advance" and `docs/specs/0002-npc-ai/stories/v4/packets-R2.md` item 1.
//
// The window is `[m_flLastEventCheck, m_flCycle + 0.1 x cycle rate)`: closed at the bottom, open at
// the top, a 0.1 s look-ahead. Records fire in the order the descriptor declares them (one pass over
// the table, never sorted), ids at or above 5000 are never offered to a handler, and what an id
// MEANS belongs to the handler (slot 259) that claims it.

class FElysiumEntity;

// One sequence's words, as `CBaseAnimating::DispatchAnimEvents 0x10091880` reads and writes them.
// Declared by spec 0002 V4a's seam (`stories/v4/README.md` § "Shared names"); the dispatcher over it
// is owned and filled by lane A1, and called by A1 (the NPC) and A4 (the player, the camera).
struct FElysiumSequenceWords
{
	int32 Sequence = 0;              // m_nSequence (the trace and the census name it)
	float Cycle = 0.f;               // m_flCycle, [0,1)
	float CycleRate = 0.f;           // GetSequenceCycleRate × m_flPlaybackRate, per second
	float AnimTime = 0.f;            // m_flAnimTime (a layer: the OWNER's)
	bool  bLoops = false;            // m_bSequenceLoops +0x65d
	bool  bHasDescriptor = true;     // a seqdesc exists (none: no finish, no past-half)
	bool  bDescriptorLoops = false;  // seqdesc.flags & 1 (the wrap clause)
	float LastEventCheck = 0.f;      // in/out: m_flLastEventCheck +0x658 (a layer: layer+0x2c)
	bool  bSequenceFinished = false; // IN/out: m_bSequenceFinished +0x65c (a layer: layer+4, zeroed).
	                                 // The value at entry is retail's `cVar2`, the rising edge's "before".
	bool  bSequencePastHalf = false; // out: m_fSequencePastHalf +0x568 (untouched for a layer)

	// --- Added by V4a lane A1, last and defaulted (the seam's fields above are unchanged) ---

	// Retail writes `+0x65c`, `+0x568` and `+0x658` on the object BEFORE the event loop
	// (`0x10091880`), so a handler that resets the sequence (`ResetSequenceInfo 0x10090950` zeroes
	// `+0x658` and clears `+0x65c`) is not overwritten afterwards. A caller whose handlers can do
	// that sets this: it is called once, after the words are written and before the first event, and
	// the caller then copies nothing back after the return. Unset: copy back after the return.
	TFunction<void(const FElysiumSequenceWords&)> WordsWritten;
	// The clip the census names an unclaimed id under (debug bookkeeping, not a retail word): the
	// owning bank and the label. Empty: the census row reads `seq <Sequence>`.
	FString CensusOwner;
	FString CensusLabel;
};

namespace ElysiumAnimEvents
{
	// `0x10091880` / `0x10098cd0` (V4a lane A1); called by the NPC's slot 258
	// (`ElysiumNpcBaseAnimEvents.cpp`) and by lane A4 (the player, the camera). The caller holds
	// retail's `GetModelPtr() != 0` precondition: with no model retail's body touches nothing.

	// 0x10091880. Fires each event on Handler.HandleAnimEvent (slot 259) in table order and
	// emits the `animevent` trace for Source. Returns true on the finish flag's rising edge
	// (finished now, `Words.bSequenceFinished` false at entry): the caller then makes its own
	// OnSequenceFinished call (0x10091b9a -> 0x10091c80 is a direct call). A caller that set
	// `WordsWritten` tests its own live word instead, as retail does after the loop.
	bool DispatchBase(FElysiumSequenceWords& Words, TConstArrayView<FElysiumAnimEvent> Events,
	                  FElysiumEntity& Source, FElysiumEntity& Handler);
	// 0x10098cd0, one overlay layer: no clamp, no past-half, the finish word zeroed, never set.
	void DispatchLayer(FElysiumSequenceWords& Layer, TConstArrayView<FElysiumAnimEvent> Events,
	                   FElysiumEntity& Source, FElysiumEntity& Handler);

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

	// The COMBAT CHARACTER's own arms of `0x1032e330`, past the weapon forward. Each is a `case` in
	// retail's switch and each is claimed by `FElysiumCombatCharacter::HandleAnimEvent`
	// (`docs/vtmb/animation_events.md` -> "Port status — combat character band"). The two feed ids
	// are spelled again in `ElysiumFeed.h` beside the transaction that consumes them; these are the
	// dispatcher's view of the same numbers.

	// `0xfb4`. `FUN_101e3e70(&DAT_10739a4c, this, options)`: `options` names a DISCIPLINE, the
	// manager resolves it to a bit, the character's own discipline mask at `+0xF34` is tested, and
	// the record's level block runs its hit callback — `DevMsg(3, "Discipline<%s> CallbackHit")` at
	// `0x101e3910`. It is NOT a sound: it is where a cast's own animation commits its effect.
	inline constexpr int32 DisciplineCallbackHit = 4020;

	// `0x1004` / `0x1005` / `0x1006` — the `m_hAnimFollowModel` (`+0x5a8`) ornament slot. 4100
	// formats `"%s.mdl"` from `options`, 4102 formats `"%s_%s.mdl"` with the gender word, and both
	// REMOVE the standing model before they create the next one; 4101 only removes.
	inline constexpr int32 AttachFollowModel         = 4100;
	inline constexpr int32 DetachFollowModel         = 4101;
	inline constexpr int32 AttachFollowModelGendered = 4102;

	// Retail's own two formats, verbatim from `1032e435` / `1032e448`, and the gender words
	// `1032e424`/`1032e42b` select between (`male` at `0x105994a0`, `female` at `0x10599490` — the
	// same pair the `.gender` token substitution at `0x101b3a10` uses).
	//
	// **The option is never extension-stripped.** `models/scenery/misc/wineglass/wineglass.mdl`
	// under 4102 legitimately produces `wineglass.mdl_male.mdl`, and that is the key the catalogue
	// is written under. Lowercased and forward-slashed here because the lookup is — the same fold,
	// and only that fold, as the bake's `ornament_models.model_key` — and because a `.mdl` path is
	// case- and separator-insensitive on the source filesystem. The shipped options are all
	// forward-slashed already; the fold is what keeps the two sides one contract.
	inline FString FormatFollowModelPath(int32 Event, const FString& Options, bool bMale)
	{
		const FString Trimmed = Options.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return FString();
		}
		const FString Formatted = Event == AttachFollowModelGendered
			? FString::Printf(TEXT("%s_%s.mdl"), *Trimmed, bMale ? TEXT("male") : TEXT("female"))
			: FString::Printf(TEXT("%s.mdl"), *Trimmed);
		return Formatted.Replace(TEXT("\\"), TEXT("/")).ToLower();
	}
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
// a dispatcher body above (run from an entity's own think) and every reader is a console verb, a Cog frame or an automation case.
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
