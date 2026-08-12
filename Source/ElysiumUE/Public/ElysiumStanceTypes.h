#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

struct FElysiumDisposition;

// VtMB's disposition stance machine, as a pure decision.
//
// A standing NPC does not take its idle from the weighted sequence choice. `ACT_DISPOSITION` has
// its own resolver bypass and its own selection algorithm compiled into the DLL, tuned per
// disposition by `vdata/System/DispositionTable.txt`. The recovered behaviour this transcribes —
// the addresses, the name convention and the precache fallbacks — is
// `docs/vtmb/animation_and_movers.md` -> "The disposition stance machine".
//
// Nothing here touches the engine: the clip names arrive resolved, the tuning arrives as a row,
// and the randomness arrives as a stream. That is what lets the whole machine be asserted in the
// Substrate tier against literals (`gameplay-systems-architecture.md` K10).
namespace ElysiumStance
{
	//: VtMB authors exactly three stances per disposition and indexes them directly. It is not a
	//: weighted pick and not a count of what the model happens to carry.
	inline constexpr int32 Count = 3;
}

// One model's stance clips for one disposition, with the fallbacks already applied.
//
// Retail resolves these at model precache and bakes the misses in, so a body that authored no
// `Fidget_2` has `fidget[2] == idle[2]` from then on rather than being asked again per roll. This
// mirrors that: whoever fills it applies the fallback ladder once, and every reader downstream
// sees a table with no holes.
struct FElysiumStanceClips
{
	// `Stance_<Anim>_Idle_<n+1>`. A missing entry falls back to `Idle[0]`.
	FString Idle[ElysiumStance::Count];
	// `Stance_<Anim>_Fidget_<n+1>`. A missing entry falls back to `Idle[n]` — which is also the
	// test the selector uses to decide a fidget is available at all.
	FString Fidget[ElysiumStance::Count];
	// `Stance_<Anim>_Trans_<from+1>_<to+1>`. A missing entry falls back to `Idle[to]`, so a stance
	// change with no authored blend snaps to the destination instead of failing.
	FString Trans[ElysiumStance::Count][ElysiumStance::Count];

	bool IsValid() const { return !Idle[0].IsEmpty(); }
};

// The per-character stance state. Retail carries the first two on the NPC as save-only datamap
// fields (`m_CurrStance` +0x64c8, `m_flStanceTime` +0x64e4) and never resets either on a schedule,
// state, dialogue or disposition change; the two flags are the "I am mid-fidget / mid-transition"
// latches the next selection consumes to settle back onto the idle.
struct FElysiumStanceState
{
	int32 Current = 0;
	// Seconds on the substrate clock, stamped only by a stance change.
	float LastChangeTime = 0.f;
	bool bInFidget = false;
	bool bInChange = false;
};

// What one selection decided.
struct FElysiumStanceChoice
{
	FString Clip;
	// A transition is a one-shot; an idle and a fidget loop until the next selection.
	bool bLoop = true;
	bool bChangedStance = false;

	bool IsSet() const { return !Clip.IsEmpty(); }
};

namespace ElysiumStance
{
	/**
	 * Choose the clip a standing character shows now, advancing `State`.
	 *
	 * Called once per idle-clip completion, which is the cadence retail runs it at: the idle task's
	 * per-tick body re-requests `ACT_DISPOSITION` only when the current sequence has finished, so
	 * nothing re-enters the selector mid-clip.
	 *
	 * `bTalking` is "a dialogue line is playing on this character", not "a dialogue is open" — the
	 * distinction is the file's own, and it selects which threshold/chance pair applies.
	 *
	 * `Now` is the substrate clock. The stance-change floor is measured against
	 * `State.LastChangeTime`, so a character that has just changed cannot change again until the
	 * disposition's threshold has passed however many clips complete in between.
	 */
	FElysiumStanceChoice Select(const FElysiumStanceClips& Clips, const FElysiumDisposition& Tuning,
		FElysiumStanceState& State, bool bTalking, double Now, FRandomStream& Rng);

	/**
	 * The stance change itself, exposed because the dialogue-pause driver runs it directly.
	 *
	 * Retail's `ChangeStance` picks uniformly among the two stances that are *not* current, plays
	 * the authored transition into the new one, and stamps the clock. A model with only one usable
	 * stance has nothing to move to and keeps the one it has.
	 */
	FElysiumStanceChoice ChangeStance(const FElysiumStanceClips& Clips, FElysiumStanceState& State,
		double Now, FRandomStream& Rng);

	/**
	 * Apply retail's precache fallback ladder to a partially-filled table, in place.
	 *
	 * Split out from whoever gathered the labels so the rule is stated once and can be asserted
	 * without an export: a missing idle becomes `Idle[0]`, a missing fidget becomes the idle at the
	 * same index, and a missing transition becomes the destination idle.
	 */
	void ApplyPrecacheFallbacks(FElysiumStanceClips& Clips);
}
