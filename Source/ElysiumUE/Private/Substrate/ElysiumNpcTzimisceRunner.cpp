#include "Substrate/ElysiumNpcTzimisceRunner.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcAnim10Shared.h"
#include "Substrate/ElysiumNpcLifecycle19Shared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSpeciesMisc10_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Visual/ElysiumActionTables.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	constexpr int32 GAnim10ActDisposition = 0xf1;   // ACT_DISPOSITION
	// The Tzimisce runner's four variants, and the head claw's one.
	constexpr int32 GAnim10ActTzIdle2 = 0x1134;    // 4404
	constexpr int32 GAnim10ActTzFidget2 = 0x1135;  // 4405
	constexpr int32 GAnim10ActTzRun2 = 0x1137;     // 4407
	//
	// Four contiguous tables at `0x1065d680` / `…688` / `…690` / `…6a0`. The port's footstep table
	// (`ElysiumFootsteps.cpp`) already carries the same 2+2 left/right split and the same four
	// breaths.
	const TCHAR* const GRunnerStepsA[] = {
		TEXT("character/monster/TC_Runner/foot_steps_1.wav"),
		TEXT("character/monster/TC_Runner/foot_steps_2.wav"),
	};
	const TCHAR* const GRunnerStepsB[] = {
		TEXT("character/monster/TC_Runner/foot_steps_3.wav"),
		TEXT("character/monster/TC_Runner/foot_steps_4.wav"),
	};
	const TCHAR* const GRunnerBreaths[] = {
		TEXT("character/monster/TC_Runner/Breath1.wav"),
		TEXT("character/monster/TC_Runner/Breath2.wav"),
		TEXT("character/monster/TC_Runner/Breath3.wav"),
		TEXT("character/monster/TC_Runner/Breath4.wav"),
	};
	const TCHAR* const GRunnerExerts[] = {
		TEXT("character/monster/TC_Runner/Exert_Heavy_1.wav"),
		TEXT("character/monster/TC_Runner/Exert_Heavy_2.wav"),
		TEXT("character/monster/TC_Runner/Exert_Heavy_3.wav"),
	};
	const TCHAR* const GRunnerWeapon = TEXT("item_w_tzimisce3_claw");
}

const FElysiumNpcClass* FElysiumNpcTzimisceRunner::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 588: `0x103c3fd0`, `RestartIdealActivity(1)` with no `IsActivityFinished` gate.
void FElysiumNpcTzimisceRunner::Slot588()
{
	// `0x103c3fd0`, eight bytes: `RestartIdealActivity(1)` and nothing else.
	//
	// The base override (`0x10293e50`) puts `IsActivityFinished()` in front of the same call. This
	// one does not, so a runner restarts its ideal activity on EVERY call of slot 588 — mid-clip
	// included — where every other class waits for the clip to end. That is the whole of the
	// species difference.
	//
	// `RestartIdealActivityId` is family **Hints**' seam: this runtime resolves activities by name
	// and carries no retail-id table, so the id is recorded and reaches nothing. The DECISION —
	// that the restart is unconditional — is this body's and is what the suite asserts.
	RestartIdealActivityId(1);
}

// The melee quartet, slots 599-602: `0x103c3960`, `0x103c39e0`, `0x103c3a70`, `0x103c3ab0`. Every recovered dispatch
// site of 599 pushes `GetEnemy()` (family TroikaHelpers' `Slot599`), which the body is handed.
bool FElysiumNpcTzimisceRunner::Slot599(int32 Arg)
{
	(void)Arg;
	return FUN_103c3960(static_cast<const FElysiumNpc*>(this)->GetEnemy());
}

bool FElysiumNpcTzimisceRunner::Slot600(FElysiumEntity* Enemy)
{
	// `0x103c39e0`, `CNPC_VTzimisceRunner`'s slot 600 — `0x103c1a60` with the `m_hPotentialEnemy`
	// cache in front of it, written on both arms exactly as its slot 599 does:
	//
	//     (*DAT_10924edc)->vfunc1();
	//     m_hPotentialEnemy = param_1 ? param_1->GetRefEHandle() : INVALID;
	//     ... the head claw's body, verbatim ...
	//
	// Note the ORDER: the event fires BEFORE the cache here, where slot 599 caches first and fires
	// the event only on the accepting arm.
	++MeleeEventFires;
	RunnerPotentialEnemy = Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle();
	if (!bInMelee)
	{
		if (MeleeCoordinatorAdmits600())
		{
			bInMelee = true;
			return true;
		}
	}
	bInMelee = false;
	return false;
}

void FElysiumNpcTzimisceRunner::Slot601(FElysiumEntity* Enemy)
{
	// `0x103c3a70`, `CNPC_VTzimisceRunner`'s slot 601 — `0x103c1ad0` with one extra write, and it
	// is not the potential-enemy CACHE the runner's 599 and 600 do but its CLEAR:
	//     (*DAT_10924edc)->vfunc1();
	//     m_hPotentialEnemy = 0xffffffff;                    // +0x6678, INVALID_EHANDLE
	//     m_bInMelee = 0;
	//     ReleaseMeleeSlot(m_pAttackCoordinator, this);
	//
	// So the runner's three melee slots are a matched set: 599 and 600 latch whoever was offered,
	// 601 forgets them. The argument is ignored on this arm, unlike the other two.
	(void)Enemy;
	++MeleeEventFires;
	RunnerPotentialEnemy = FElysiumEntityHandle();
	bInMelee = false;
	++MeleeCoordinatorReleases;
}

bool FElysiumNpcTzimisceRunner::Slot602()
{
	// `0x103c3ab0`, `CNPC_VTzimisceRunner`'s slot 602 — byte-identical to `0x103c1b10`, verified
	// against the decompiled C of both. Called rather than restated.
	return FUN_103c1b10();
}

// Slot 130: `0x103c3c40`, which calls the Troika body first.
// `0x103c3c40`
void FElysiumNpcTzimisceRunner::OnRestore(bool bFromLoad)
{
	TroikaOnRestore(bFromLoad);
	SetAttackExtents(FVector(RunnerAttackExtentX, RunnerAttackExtentY, RunnerAttackExtentZ)
		* ElysiumMove::U);
	++Flag2Removals;                                                     // RemoveFlag2(4)
}

// Slot 104: `0x103c31e0`.
// 0x103c31e0
void FElysiumNpcTzimisceRunner::Precache()
{
	// `CNPC_VTzimisceRunner::Precache` `0x103c31e0` — the Troika body, then four tables (2, 2, 4, 3)
	// and the claw. No field writes.
	TroikaPrecache();
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GRunnerStepsA, UE_ARRAY_COUNT(GRunnerStepsA));
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GRunnerStepsB, UE_ARRAY_COUNT(GRunnerStepsB));
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GRunnerBreaths, UE_ARRAY_COUNT(GRunnerBreaths));
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GRunnerExerts, UE_ARRAY_COUNT(GRunnerExerts));
	NpcKernelPrecache10Shared::Precache10Other(*this, GRunnerWeapon);
}

// Slot 310: `0x103c3d80`, which calls the Troika body `0x10295750` directly.
/** `CNPC_VTzimisceRunner::SetActivity` (`0x103c3d80`). A five-entry request remap in front of the
 *  Troika body, gated on the form byte `+0x6672` being non-zero and tested in retail's order
 *  1, 9, 0x13, 3, 0xf1. With the byte clear every request forwards unchanged. */
void FElysiumNpcTzimisceRunner::SetActivity(int32 Activity)
{
	// `CNPC_VTzimisceRunner::SetActivity` `0x103c3d80`, 110 bytes. A five-entry request remap in
	// front of the Troika body, gated on the form byte `+0x6672` being non-zero, tested in retail's
	// own order — 1, 9, 0x13, 3, 0xf1 — and NOT in numeric order.
	int32 Request = Activity;
	if (bTzimisceRunnerForm)
	{
		if (Activity == NpcKernelAnim10Shared::GAnim10ActIdle)
		{
			Request = GAnim10ActTzIdle2;
		}
		else if (Activity == NpcKernelAnim10Shared::GAnim10ActWalk)
		{
			Request = NpcKernelAnim10Shared::GAnim10ActTzWalk2;
		}
		else if (Activity == NpcKernelAnim10Shared::GAnim10ActRun)
		{
			Request = GAnim10ActTzRun2;
		}
		else if (Activity == NpcKernelAnim10Shared::GAnim10ActFidget)
		{
			Request = GAnim10ActTzFidget2;
		}
		else if (Activity == GAnim10ActDisposition)
		{
			Request = GAnim10ActTzIdle2;
		}
	}
	TroikaSetActivity(Request);
}

// Slot 375: `0x103c3e10`, which calls the Troika body `0x10295590` directly.
/** `CNPC_VTzimisceRunner::NPC_EarlyTranslateActivity` (`0x103c3e10`). Chains the Troika body FIRST
 *  and only then remaps the TRANSLATED activity under a non-zero `+0x6672`, so it is a POST-PASS on
 *  the base's answer and not a replacement: 1 and `0xf1` become `0x1134`, 3 `0x1135`, 9 `0x1136`,
 *  `0x13` `0x1137`. */
int32 FElysiumNpcTzimisceRunner::NPC_EarlyTranslateActivity(int32 Activity)
{
	// `CNPC_VTzimisceRunner::NPC_EarlyTranslateActivity` `0x103c3e10`, 82 bytes. It chains the Troika
	// body FIRST and only then remaps the TRANSLATED activity, so it is a POST-PASS on the base's
	// answer and not a replacement — which is exactly why its slot-310 twin `0x103c3d80` remaps the
	// REQUEST instead and the two look alike but are not.
	int32 Translated = TroikaNpcEarlyTranslateActivity(Activity);
	if (bTzimisceRunnerForm)
	{
		switch (Translated)
		{
		case NpcKernelAnim10Shared::GAnim10ActIdle:
		case GAnim10ActDisposition:
			Translated = GAnim10ActTzIdle2;
			break;
		case NpcKernelAnim10Shared::GAnim10ActFidget:
			return GAnim10ActTzFidget2;
		case NpcKernelAnim10Shared::GAnim10ActWalk:
			return NpcKernelAnim10Shared::GAnim10ActTzWalk2;
		case NpcKernelAnim10Shared::GAnim10ActRun:
			return GAnim10ActTzRun2;
		default:
			break;
		}
	}
	return Translated;
}

// Slot 604: `0x103c4430`, which replaces the Troika body wholesale; its argument is read by no arm.
int32 FElysiumNpcTzimisceRunner::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	return SelectScheduleMeleeCombatChangLine(false);
}

// Slot 440: `0x103c3560`.
// `0x103c3560`
// `0x103c3560`, `CNPC_VTzimisceRunner::TranslateSchedule`, the body of `FElysiumNpcTzimisceRunner::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcTzimisceRunner::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber == 0xc7) { return 0xc8; }
	if (ScheduleNumber == 0xe7) { return 0x156; }
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 337: `0x103c3cb0`.
int32 FElysiumNpcTzimisceRunner::GetUsedHullBits()
{
	// The Troika body `0x1029a050` called directly, its 1 ORed with this class's bit.
	// `OR AH,0x20` in the listing; the decompiled C drops the bit.
	return FElysiumNpc::GetUsedHullBits() | 0x2000;
}

// Slot 546: `0x103c2a60`, the class's own schedule id space.
const TCHAR* FElysiumNpcTzimisceRunner::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093d224`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VTzimisceRunner"), TEXT("0x103c2a60"), TEXT("0x1093d224") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 259: `0x103c32c0`, the footstep body of `docs/vtmb/footsteps.md` §1.7; an id it does not
// claim is a direct call into the base body.
bool FElysiumNpcTzimisceRunner::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return SpeciesFootstepAnimEvent(TEXT("npc_VTzimisceRunner"), Event);
}

// `ENpcPredicate::FormBit`: the runner's slot-375 body reads its own form byte `+0x6672`.
bool FElysiumNpcTzimisceRunner::AnimFormBit() const
{
	return bTzimisceRunnerForm;
}

// Slot 400: `0x103c3060`, `CNPC_VTzimisceRunner::vfunc400` — `return 1;`.
bool FElysiumNpcTzimisceRunner::AllowsKnockbackBypass()
{
	return true;
}

// --- Moved from `ElysiumNpcAnim10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcLifecycle19.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSpecies.cpp` (story 5 step 4) ---

bool FElysiumNpcTzimisceRunner::FUN_103c3960(FElysiumEntity* Enemy)
{
	// `0x103c3960`, `CNPC_VTzimisceRunner`'s slot 599:
	//
	//     m_hPotentialEnemy = param_1 ? param_1->GetRefEHandle() : INVALID;    // +0x6678
	//     if (RequestMeleeSlot(m_pAttackCoordinator, this)) {                  // 0x1025db70
	//         (*DAT_10924edc)->vfunc1();
	//         m_bInMelee = 1;
	//         return true;
	//     }
	//     m_bInMelee = 0;
	//     return false;
	//
	// TWO differences from `CNPC_VTzimisceHeadClaw`'s, and both are load-bearing:
	//   * the argument is CACHED first — this is the one slot-599 body in the family that reads its
	//     parameter at all, and it writes `m_hPotentialEnemy` on BOTH arms, refusal included;
	//   * the melee-must-leave timer is **not armed**. A runner that enters melee therefore has no
	//     deadline to leave it, which is a real divergence from the Troika line and from the head
	//     claw beside it, not a transcription slip — the immediates for `RandomFloat` are simply not
	//     in the body.
	RunnerPotentialEnemy = Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle();
	if (MeleeCoordinatorAdmits599())
	{
		++MeleeEventFires;
		bInMelee = true;
		return true;
	}
	bInMelee = false;
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 588 — `CNPC_VTzimisceRunner::vfunc588` `0x103c3fd0`.
// -------------------------------------------------------------------------------------------------

// --- Moved from `ElysiumNpcSpeciesMisc10_2.cpp` (story 5 step 4) ---

float FElysiumNpcTzimisceRunner::RunnerHullEngineToken() const
{
	// SEAM for `DAT_1070b22c` vtable `+0x1dc` called with 0. The retail quantity is unrecovered; the
	// ONE property both bodies read is that it changes between frames and not within one, so the
	// substrate clock stands in and the substitution is named here.
	return static_cast<float>(NpcKernelSpeciesMisc10_2Shared::SpeciesMisc10_2Now(*this));
}

void FElysiumNpcTzimisceRunner::TzimisceRunnerNotifyChangeSizeSmall()
{
	// `103c3cd3`: `SetHullSizeSmall(force = 1)` — family Motor10's `0x10273180`.
	SetHullSizeSmall(/*bForce=*/true);
	// `103c3cdb`: the form byte `+0x6672`, which family Anim10's `PreTranslate_TzimisceRunner`
	// (`0x103c3e10`) selects its four TZ activity variants on.
	bTzimisceRunnerForm = true;
	// `103c3ce2`: `m_bWantsLargeHull` (+0x5f2c) = 0.
	bWantsLargeHull = false;
	// `103c3cf6`: `+0x6674` = the engine token, the value slot 336 reads back.
	RunnerHullToken = RunnerHullEngineToken();
}

void FElysiumNpcTzimisceRunner::TzimisceRunnerNotifyChangeSizeNormal()
{
	// `103c3d1e`: re-read the engine token and compare against the one slot 335 cached. ONLY when
	// the two DIFFER does the restore happen — an unchanged token leaves the runner small and the
	// form byte set, so the restore is edge-triggered on the engine value, not on a request.
	if (RunnerHullEngineToken() == RunnerHullToken)
	{
		return;
	}
	// `103c3d4a`: `SetHullSizeNormal(force = 1)`, clear the form byte, set `m_bWantsLargeHull`.
	SetHullSizeNormal(/*bForce=*/true);
	bTzimisceRunnerForm = false;
	bWantsLargeHull = true;
}

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 4) ---

