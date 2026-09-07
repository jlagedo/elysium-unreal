#include "Substrate/ElysiumFeedSchedules.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumPlayer.h"   // FElysiumCombatCharacter, the feeder/victim base
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"

namespace
{
	using ECond = EElysiumNpcCond;
	using ETask = EElysiumTask;

	FElysiumTaskStep FlagStep(EElysiumNpcFlag Flag)
	{
		FElysiumTaskStep Out;
		Out.Task = ETask::SetNpcFlag;
		Out.Flag = Flag;
		return Out;
	}

	FElysiumTaskStep ObliviousStep(bool bOblivious)
	{
		FElysiumTaskStep Out;
		Out.Task = ETask::MakeOblivious;
		// The compiler's own encoding: `TRUE`/`ON` -> 1.0, `FALSE`/`OFF` -> 0.0 (`0x1030e65f`).
		Out.Param = bOblivious ? 1.f : 0.f;
		return Out;
	}

	FElysiumTaskStep ActivityStep(const TCHAR* Activity)
	{
		FElysiumTaskStep Out;
		Out.Task = ETask::SetActivity;
		Out.Activity = Activity;
		return Out;
	}

	FElysiumTaskStep WaitStep(ETask Task, float Seconds)
	{
		FElysiumTaskStep Out;
		Out.Task = Task;
		Out.Param = Seconds;
		return Out;
	}

	void RegisterFeedSchedules()
	{
		// `SCHED_TROIKA_MESMERIZED`, transcribed verbatim from the schedule blob at `vampire.dll`
		// `0x105e6f40`:
		//
		//     Tasks       TASK_MAKE_OBLIVIOUS   TRUE
		//                 TASK_SET_NPC_FLAG     NPCFlag:D_IS_BUSY
		//                 TASK_SET_NPC_FLAG     NPCFlag:DONT_INVESTIGATE
		//                 TASK_SET_NPC_FLAG     NPCFlag:NO_DIALOG
		//                 TASK_SET_ACTIVITY     ACTIVITY:ACT_DISPOSITION_MESMERIZED
		//                 TASK_WAIT             30
		//                 TASK_WAIT_RANDOM      120
		//     Interrupts  COND_LIGHT_DAMAGE  COND_HEAVY_DAMAGE  COND_REPEATED_DAMAGE
		//     Flags       DELAY_INTERRUPTS
		//
		// Nothing here is chosen. This is the first registered program in this runtime whose
		// interrupt mask is a DECODED one rather than an empty posture or a census-shaped guess —
		// every other registered mask carries a CHOSEN mark, and this one must never grow one.
		//
		// The program has no teardown tasks and needs none: `IElysiumScheduleRunner::OnScheduleChange`
		// releases all three flags and the obliviousness when the NEXT schedule is installed
		// (`CAI_BaseNPCTroika::OnScheduleChange`, `0x102a0940`, mask `&= 0xbbf4b97e`). The trance
		// therefore unwinds by being replaced, which is also why the two WAIT steps are the whole
		// duration: 30 seconds flat plus a uniform draw over 0..120, so 30 to 150 seconds.
		FElysiumSchedule Mesmerized;
		Mesmerized.Id = EElysiumScheduleId::Mesmerized;
		Mesmerized.Tasks = {
			ObliviousStep(true),
			FlagStep(EElysiumNpcFlag::D_IS_BUSY),
			FlagStep(EElysiumNpcFlag::DONT_INVESTIGATE),
			FlagStep(EElysiumNpcFlag::NO_DIALOG),
			ActivityStep(TEXT("ACT_DISPOSITION_MESMERIZED")),
			WaitStep(ETask::Wait, 30.f),
			WaitStep(ETask::WaitRandom, 120.f),
		};
		Mesmerized.Interrupts = FElysiumNpcConditions::Of({
			ECond::LightDamage, ECond::HeavyDamage, ECond::RepeatedDamage });
		Mesmerized.bDelayInterrupts = true;
		ElysiumSchedule::Register(MoveTemp(Mesmerized));
	}

	struct FElysiumFeedScheduleRegistrar
	{
		FElysiumFeedScheduleRegistrar() { RegisterFeedSchedules(); }
	};
	const FElysiumFeedScheduleRegistrar GFeedScheduleRegistrar;
}

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

	// The install. `StartNamedSchedule` is retail's own shape for this call and not a convenience:
	// it refuses on a dead NPC, releases whatever claim the running program held, starts the named
	// program through the ordinary kernel, and stamps `NextThink` to now — which is what retail's
	// slot-614 think-timer reset (`0x102c23f0`) does immediately before its `SetSchedule`.
	return Npc->StartNamedSchedule(ElysiumScheduleName(EElysiumScheduleId::Mesmerized),
		TEXT("CBaseCombatCharacter.FeedInterrupt"), Attacker.DebugString());
}
