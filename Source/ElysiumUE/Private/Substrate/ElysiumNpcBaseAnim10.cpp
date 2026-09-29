// `CAI_BaseNPC`'s bodies of the `Anim10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseAnim10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAnim10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumReactions.h"
#include "Visual/ElysiumActionTables.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	//
	// Spelled once, as every other family in this band spells them: the port stands no retail
	// activity table (family Hints' `RestartIdealActivityId` records why).
	constexpr int32 GAnim10ActReset = 0;            // ACT_RESET
	constexpr int32 GAnim10ActTransition = 2;       // ACT_TRANSITION — `SetActivity`'s second refusal
}

// --- Moved from `ElysiumNpcAnim10.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::FindTransitionSequence(int32 From, int32 To) const
{
	// `CBaseAnimating::FindTransitionSequence(from, to, &dir)`. SEAM: family Anim recorded that this
	// substrate stands no studio header and no sequence index at all, so there is no transition
	// graph to walk.
	//
	// **It answers `To`, the DESTINATION, and that is retail's own no-transition answer**, not a
	// convenience: the SDK body walks the studio transition table and returns the destination
	// sequence unchanged when no transition clip stands between the two. That is the arm
	// `AdvanceToIdealActivity` reads as `transition == m_nIdealSequence`, which commits the ideal
	// through `SetActivityAndSequence` DIRECTLY.
	//
	// Answering the `-1` sentinel instead would have been wrong twice over. It is retail's
	// "invalid sequence" value, not its "no transition" one; and it takes the arm that dispatches
	// slot 310 with the ideal activity — which `CAI_BaseNPC::SetActivity` then REFUSES, because
	// `m_Activity` is 2 `ACT_TRANSITION` on every path that reaches this body. A body would be
	// stranded in the transition activity for good. The `-1` arm is ported above and is what a
	// studio header with a genuinely invalid sequence pair would reach.
	(void)From;
	return To;
}

void FElysiumNpcBase::WeaponSetActivity(int32 Activity, float Duration)
{
	// `CBaseCombatCharacter::Weapon_SetActivity(activity, duration)`. SEAM: the weapon's activity
	// lives on the item's own clip set here and no per-weapon activity word stands on the NPC.
	FWeaponActivityRequest Request;
	Request.Activity = Activity;
	Request.Duration = Duration;
	WeaponActivityRequests.Add(Request);
}

void FElysiumNpcBase::SetViewOffset(const FVector& OffsetUnits)
{
	// `thunk_FUN_1009f380(this, &offset)` — `CBaseEntity::SetViewOffset`. SEAM: no writable
	// `m_vecViewOffset` word; the write is recorded so the per-commit dispatch of slot 533 is
	// assertable.
	ViewOffsetWrites.Add(OffsetUnits);
}

void FElysiumNpcBase::SetActivityAndSequence(int32 Activity, int32 Sequence, int32 TranslatedActivityIn,
	int32 WeaponActivity)
{
	// `CAI_BaseNPC::SetActivityAndSequence` `0x10272490`, 243 bytes. The commit, and every one of its
	// six effects is ordered against the others.

	// 1. `*(int *)((int)this + 0xff4) = param_3` — the TRANSLATED activity is written FIRST, before
	//    anything below can read it. Both the `EyeOffset` dispatch and the listener test below read
	//    the NEW value.
	TranslatedActivity = TranslatedActivityIn;

	if (Sequence < 0)
	{
		// 2. A negative sequence takes `ResetSequence(0)` and SKIPS the cycle, the duration and the
		//    weapon activity entirely. The rest of the body still runs.
		CommitForcedSequence(0);
	}
	else
	{
		// 3. The cycle word is zeroed UNLESS the sequence equals `m_nSequence` with `+0x65d` set, OR
		//    the old activity and the new one are BOTH in {ACT_WALK, ACT_RUN} — retail's walk/run
		//    cycle carry, which is what keeps a footfall in phase across a gait change.
		const bool bSameSequenceLooped =
			static_cast<float>(Sequence) == static_cast<float>(SequenceNumber) && bSequenceLoopedOnce;
		const bool bOldIsGait = ActivityNumber == NpcKernelAnim10Shared::GAnim10ActWalk || ActivityNumber == NpcKernelAnim10Shared::GAnim10ActRun;
		const bool bNewIsGait = Activity == NpcKernelAnim10Shared::GAnim10ActWalk || Activity == NpcKernelAnim10Shared::GAnim10ActRun;
		if (!bSameSequenceLooped && !(bOldIsGait && bNewIsGait))
		{
			SequenceCycle = 0.f;   // +0x06f8 m_flCycle
		}
		// 4. `ResetSequence(seq)`, `SequenceDuration(seq)`, then `Weapon_SetActivity(act, duration)`.
		CommitForcedSequence(Sequence);
		const float Duration = SequenceDurationOf(Sequence);
		WeaponSetActivity(WeaponActivity, Duration);
	}

	// 5. slot 533 `EyeOffset(activity, m_TranslatedActivity)` feeds `SetViewOffset` — for EVERY
	//    request, the negative-sequence one included.
	SetViewOffset(EyeOffset(Activity, TranslatedActivity));

	// 6a. slot 465 `OnChangeActivity(activity)` fires only when `m_Activity` DIFFERS from the new
	//     activity. Read BEFORE step 7 overwrites it.
	if (ActivityNumber != Activity)
	{
		OnChangeActivity(Activity);
	}
	// 6b. The activity-change listener `thunk_FUN_101f6010(&DAT_1073dd58, this, m_TranslatedActivity,
	//     m_Activity)` fires on a DIFFERENT comparison — `m_Activity` against the TRANSLATED
	//     activity, not against the requested one — and only while `m_bKeepSound` is clear. That is
	//     the whole reason `m_bKeepSound` exists: the Troika random pick raises it so the listener is
	//     not told about the intermediate activity.
	if (ActivityNumber != TranslatedActivity && !bKeepSound)
	{
		FActivityChangeNotice Notice;
		Notice.NewTranslatedActivity = TranslatedActivity;
		Notice.OldActivity = ActivityNumber;
		ActivityChangeNotices.Add(Notice);
	}

	// 7. `*(int *)((int)this + 0xfec) = param_1`, then the navigator.
	ActivityNumber = Activity;
	// `thunk_FUN_102e1cf0` on `m_pMotor` (`0x10272569 MOV ECX,[ESI+0x5d44]` / `0x10272575`; the
	// declaration's `m_pNavigator` is a misreading): motor `+0x38` := `MaxYawSpeed`, read now that
	// the new activity stands (the ladder switches on it). Counted as before.
	MotorStoreMaxYawSpeed();
	++NavigatorActivityNotices;
}

void FElysiumNpcBase::SetActivity(int32 Activity)
{
	// `CAI_BaseNPC::SetActivity` `0x102725d0`, 95 bytes — slot 310's BASE body, a distinct retail
	// function beside the Troika override `0x10295750` that owns the slot. Ported under its own name,
	// the convention wave 1's `FElysiumNpcBase::Precache` set.
	//
	//     if ((m_Activity != act) && (act == 0 || m_Activity != 2)) { ... }
	//
	// Two refusals: a request that equals what is already playing is a NO-OP (which is why
	// `RestartIdealActivity` has to clear `m_Activity` first), and a body in `ACT_TRANSITION` (2)
	// refuses everything except `ACT_RESET` (0).
	if (ActivityNumber == Activity)
	{
		return;
	}
	if (Activity != GAnim10ActReset && ActivityNumber == GAnim10ActTransition)
	{
		return;
	}
	IdealActivityNumber = Activity;   // +0x0ff0 m_IdealActivity
	// `thunk_FUN_10272130(this, act, &m_nIdealSequence, &m_IdealTranslatedActivity,
	// &m_IdealWeaponActivity)` — family Anim's `ResolveActivityToSequence`.
	ResolveActivityToSequence(Activity, IdealSequence, IdealTranslatedActivity, IdealWeaponActivity);
	// `thunk_FUN_10272490(this, m_IdealActivity, m_nIdealSequence, m_IdealTranslatedActivity,
	// m_IdealWeaponActivity)` — note it re-reads `m_IdealActivity` rather than using the argument.
	SetActivityAndSequence(IdealActivityNumber, IdealSequence, IdealTranslatedActivity,
		IdealWeaponActivity);
}

void FElysiumNpcBase::AdvanceToIdealActivity()
{
	// `AdvanceToIdealActivity` `0x102726a0`. `param_1[0x1bc]` is `+0x6f0 m_nSequence`,
	// `param_1[0x1733]` is `+0x5ccc m_nIdealSequence`, `param_1[0x3fc]` is `+0x0ff0
	// m_IdealActivity`, `param_1[0x1734]`/`[0x1735]` the ideal translated/weapon pair, and
	// `+0x4d8` is slot 310.
	const int32 Transition = FindTransitionSequence(SequenceNumber, IdealSequence);
	if (Transition == INDEX_NONE)
	{
		// `if (fVar1 == -NAN)` — retail's own "no transition" sentinel. Dispatch slot 310 with the
		// ideal activity and stop. This is the arm the port always takes: `FindTransitionSequence` is
		// a seam over a studio graph this substrate does not stand.
		SetActivity(IdealActivityNumber);
		return;
	}
	if (Transition != IdealSequence)
	{
		// A real transition clip: commit ACTIVITY 2 (`ACT_TRANSITION`) with it, re-resolving the
		// translated/weapon pair from the transition sequence's OWN activity when it has one. Retail
		// seeds both locals with 2 before the resolve, so a transition whose sequence carries no
		// activity commits `(2, seq, 2, 2)`.
		int32 TranslatedForTransition = GAnim10ActTransition;
		int32 WeaponForTransition = GAnim10ActTransition;
		int32 SequenceForTransition = 0;
		const int32 TransitionActivity = SequenceActivityOf(Transition);
		if (TransitionActivity != INDEX_NONE)
		{
			ResolveActivityToSequence(TransitionActivity, SequenceForTransition,
				TranslatedForTransition, WeaponForTransition);
		}
		SetActivityAndSequence(GAnim10ActTransition, Transition, TranslatedForTransition,
			WeaponForTransition);
		return;
	}
	// The transition IS the ideal sequence: commit the ideal outright.
	SetActivityAndSequence(IdealActivityNumber, IdealSequence, IdealTranslatedActivity,
		IdealWeaponActivity);
}

void FElysiumNpcBase::BaseMaintainActivity()
{
	// `CAI_BaseNPC::MaintainActivity` `0x102727d0`, 230 bytes. The scope-trace push and pop around it
	// are retail's debug stack and are not reproduced; what is left is the gate and the two arms.
	//
	// NOT a vtable slot (`vtmb_func 0x102727d0`: `__thiscall`, no dispatch site), so it takes its own
	// name. Family SaveRestore10 named it as the one call `m_bForceMaintainActivity` spans.
	if (!ShouldMaintainActivity())   // slot 466, vtable +0x748
	{
		return;
	}
	if (ActivityNumber == IdealActivityNumber && SequenceNumber == IdealSequence)
	{
		// Nothing to maintain. Both terms are ORed in retail, so either mismatch is enough.
		return;
	}
	if (ActivityNumber == GAnim10ActTransition)
	{
		// The special arm: a body in `ACT_TRANSITION` WAITS for `m_bSequenceFinished` and then
		// advances, and does NOT re-resolve the ideal. That is what lets a transition clip play out.
		if (bSequenceFinished)
		{
			AdvanceToIdealActivity();
		}
		return;
	}
	// Every other activity re-resolves the ideal FIRST and then advances.
	ResolveActivityToSequence(IdealActivityNumber, IdealSequence, IdealTranslatedActivity,
		IdealWeaponActivity);
	AdvanceToIdealActivity();
}
