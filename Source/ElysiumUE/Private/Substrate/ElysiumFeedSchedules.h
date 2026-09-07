#pragma once

#include "CoreMinimal.h"

class FElysiumCombatCharacter;

// The post-feed trance: `SCHED_TROIKA_MESMERIZED` (0xfb) and the one policy that installs it.
//
// Retail's `CBaseCombatCharacter::FeedInterrupt` (`0x1033a9e0`) ends a feed by reading the victim's
// remaining `BloodPool` (stat index 0xc). Below one it selects the death outcome; at one or more it
// takes the branch this file owns:
//
//     npc = victim->MyNPCPointer();                       // the cached downcast at `+0x98`
//     if (npc && npc->IRelationType(attacker) != D_HT) {  // virtual slot 404, D_HT == 1
//         npc->ResetThinkTimers();                        // slot 614, `0x102c23f0`
//         npc->SetSchedule(SCHED_TROIKA_MESMERIZED, false);
//     }
//
// so a victim that already hates its attacker skips the trance and goes straight back to fighting,
// and everyone else stands mesmerized for 30 + rand(0..120) seconds. That guard is the whole reason
// feeding on a hostile mid-combat looks different from feeding on a civilian.
//
// This is the ONLY producer in the shipped game. There is no other `SetSchedule(0xfb)` site in
// `vampire.dll`, and no script, `disciplinetgt` record or vdata file in the install names the
// string — which is why the program is registered here, beside its policy, rather than with the
// combat families.
namespace ElysiumFeedSchedules
{
	/**
	 * The surviving-victim branch of feed teardown.
	 *
	 * `Attacker` is the feeder. Returns whether the trance was installed; false covers a victim that
	 * is not an NPC, a dead one, and the recovered `D_HT` refusal — the caller does not need to tell
	 * them apart, but the trace row does and says which.
	 *
	 * Depletion is the CALLER's branch, not this one: retail decides death before it gets here, and
	 * folding that test in would put two outcomes behind one call.
	 */
	bool BeginPostFeedTrance(FElysiumCombatCharacter& Victim, FElysiumCombatCharacter& Attacker);
}
