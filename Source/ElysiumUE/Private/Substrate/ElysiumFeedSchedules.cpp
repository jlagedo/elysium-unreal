#include "Substrate/ElysiumFeedSchedules.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"   // FElysiumCombatCharacter, the feeder/victim base
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleNumbers.h"

// `SCHED_TROIKA_MESMERIZED` (0xfb) is no longer typed here.
//
// It used to be, transcribed by hand off the blob at `0x105e6f40`, and it was the one registered
// program in this runtime whose interrupt mask was DECODED rather than chosen. The corpus now loads
// that same blob's own bytes, so the transcription is gone and what is left is the policy.
//
// The program still has no teardown tasks and still needs none:
// `IElysiumScheduleRunner::OnScheduleChange` releases all three flags and the obliviousness when the
// NEXT schedule is installed (`CAI_BaseNPCTroika::OnScheduleChange`, `0x102a0940`, mask
// `&= 0xbbf4b97e`). The trance unwinds by being replaced, which is why its two WAIT steps are the
// whole duration: 30 seconds flat plus a uniform draw over 0..120.

bool ElysiumFeedSchedules::BeginPostFeedTrance(FElysiumCombatCharacter& Victim,
	FElysiumCombatCharacter& Attacker)
{
	// Retail reaches the branch through the victim's cached NPC downcast (`+0x98`) and does nothing
	// at all when it is null — a non-NPC victim keeps no schedule and takes no trance.
	FElysiumNpc* Npc = Victim.AsNpc();
	if (Npc == nullptr)
	{
		return false;
	}

	// `IRelationType(attacker) != D_HT`. `1` is `D_HT` in retail's `Disposition_t`, confirmed against
	// `CAI_BaseNPCTroika::IRelationType` (`0x10299da0`), which returns `3` (`D_LI`) for its own owner
	// and falls through to the base table otherwise.
	//
	// This runtime answers the base table only. Retail's override also composes a squad/owner term
	// that this substrate has no squad to carry; what it does NOT change is the term that decides
	// this branch in ordinary play — a victim the player has attacked carries a derived `D_HT` row
	// (`ElysiumNpcEnemy`, the damage-memory writer), and that is exactly the case retail's guard
	// exists to catch. The composition is named here rather than faked.
	const FString AttackerClassname = Attacker.Def ? Attacker.Def->Classname : FString();
	const EElysiumRelationship Relation =
		Npc->Relationships.Resolve(Attacker.Handle, AttackerClassname);
	if (Relation == EElysiumRelationship::Hate)
	{
		Npc->RecordScheduleEvent(FString::Printf(
			TEXT("post-feed: no trance, %s is D_HT toward the feeder"), *Npc->DebugString()));
		return false;
	}

	// The install. `FeedInterrupt` `0x1033a9e0` dispatches slot 614 (`ResetThinkTimers`
	// `0x102c23f0`) on the victim's Troika pointer immediately before its `SetSchedule(0xfb)`,
	// so the trance takes hold on this frame; then `StartNamedSchedule`, retail's own shape for
	// the install: it refuses on a dead NPC, releases whatever claim the running program held and
	// starts the named program through the ordinary kernel.
	Npc->ResetThinkTimers(Npc->World ? Npc->World->NowSeconds() : 0.0);
	return Npc->StartScheduleId(ElysiumSched::SCHED_TROIKA_MESMERIZED,
		TEXT("CBaseCombatCharacter.FeedInterrupt"), Attacker.DebugString());
}
