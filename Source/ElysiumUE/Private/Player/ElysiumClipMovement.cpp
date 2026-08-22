#include "ElysiumClipMovement.h"

#include "ElysiumComboChain.h"   // `SelectionMask` — the seven direction bits, in the file's numbering
#include "Visual/ElysiumActionTables.h"

namespace
{
	// File-prefixed because the module builds non-unity adaptively and this translation unit is
	// regularly concatenated with its neighbours.
	const TCHAR* const GClipMovementMeleeAttack       = TEXT("ACT_MELEE_ATTACK");
	const TCHAR* const GClipMovementMeleeAirAttack    = TEXT("ACT_MELEE_AIR_ATTACK");
	const TCHAR* const GClipMovementMeleeAttack2Combo = TEXT("ACT_MELEE_ATTACK_2COMBO");
	const TCHAR* const GClipMovementMeleeAttackHeavy  = TEXT("ACT_MELEE_ATTACK_HEAVY");
	// The one activity `vt+0x674` adds over `vt+0x670`. It is not a row of `AnimDrivenArmFor`, so
	// today it reaches nothing but the movement-lock arm below.
	const TCHAR* const GClipMovementLandHard          = TEXT("ACT_LAND_HARD");
}

EElysiumAnimDrivenArm ElysiumClipMovement::AnimDrivenArmFor(const FString& Activity)
{
	if (Activity.IsEmpty())
	{
		return EElysiumAnimDrivenArm::None;
	}
	if (Activity.Equals(GClipMovementMeleeAttack, ESearchCase::IgnoreCase))
	{
		return EElysiumAnimDrivenArm::UntilHoldCycle;
	}
	if (Activity.Equals(GClipMovementMeleeAirAttack, ESearchCase::IgnoreCase)
		|| Activity.Equals(GClipMovementMeleeAttack2Combo, ESearchCase::IgnoreCase)
		|| Activity.Equals(GClipMovementMeleeAttackHeavy, ESearchCase::IgnoreCase))
	{
		return EElysiumAnimDrivenArm::WholeClip;
	}
	// The block, knockback, vomit and feeding rows of the same table are not implemented here — see
	// `EElysiumAnimDrivenArm`. They read as "not driven", which is what a body outside a melee swing
	// is anyway.
	return EElysiumAnimDrivenArm::None;
}

bool ElysiumClipMovement::IsAnimationDriven(const FElysiumIdealActivityState& State)
{
	const EElysiumAnimDrivenArm Arm = AnimDrivenArmFor(State.Activity);
	if (Arm == EElysiumAnimDrivenArm::None)
	{
		return false;
	}
	// The melee rows' shared guard: a real sequence, and a cycle the clip has not run past. A body
	// whose clip was cut short — a reaction took the channel, a scene claimed the body — reports no
	// sequence here and the lock is gone in the same frame, with nothing to un-latch.
	if (!State.bHasSequence || !(State.Cycle < 1.0f))
	{
		return false;
	}
	switch (Arm)
	{
	case EElysiumAnimDrivenArm::UntilHoldCycle:
		return State.Cycle < State.HoldCycle;
	case EElysiumAnimDrivenArm::WholeClip:
		return true;
	default:
		return false;
	}
}

bool ElysiumClipMovement::IsMovementLocked(const FElysiumIdealActivityState& State)
{
	// `vt+0x674` is `vt+0x670` plus one line, and this is that line. The extra activity carries no
	// cycle or sequence guard of its own in retail — the landing clip owns the body outright — so it
	// is tested before the shared predicate rather than through it.
	if (State.Activity.Equals(GClipMovementLandHard, ESearchCase::IgnoreCase))
	{
		return true;
	}
	return IsAnimationDriven(State);
}

bool ElysiumClipMovement::RefusesReselection(const FElysiumIdealActivityState& State,
	const FString& Candidate)
{
	const ElysiumActionTables::FPlayerActionTuning& Tuning = ElysiumActionTables::PlayerTuning();
	if (Tuning.MeleeHoldIdeal == nullptr || Candidate.IsEmpty())
	{
		return false;
	}
	// The recovered check names ONE ideal activity, and it is the swing whose lock releases before
	// its clip does. The other three melee rows are driven for their whole clip, so they never reach
	// a frame where a reselection could be offered at all.
	if (!State.Activity.Equals(Tuning.MeleeHoldIdeal, ESearchCase::IgnoreCase))
	{
		return false;
	}
	if (!State.bHasSequence || !(State.Cycle < 1.0f))
	{
		return false;   // the swing is over; nothing is refused
	}
	for (int32 Index = 0; Index < Tuning.MeleeHoldRefuseCount; ++Index)
	{
		const TCHAR* const Refused = Tuning.MeleeHoldRefuses[Index];
		if (Refused != nullptr && Candidate.Equals(Refused, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool ElysiumClipMovement::StopsMeleeTailMotion(const FElysiumIdealActivityState& State,
	int32 HeldSelectionMask)
{
	const ElysiumActionTables::FPlayerActionTuning& Tuning = ElysiumActionTables::PlayerTuning();
	if (Tuning.MeleeHoldIdeal == nullptr)
	{
		return false;
	}
	// The recovered check names ONE ideal activity — the same `ACT_MELEE_ATTACK` the reselection
	// guard is keyed on, and the only melee row that HAS a release before its clip ends. The three
	// whole-clip rows are outside it by construction, not by omission.
	if (!State.Activity.Equals(Tuning.MeleeHoldIdeal, ESearchCase::IgnoreCase))
	{
		return false;
	}
	// No forced sequence standing on the channel: the claim is up but the pose layer is not on its
	// clip — the arming frame of every swing and of every chain link, a body whose baked clip is
	// missing, and a headless body that has no pose layer at all. There is no cycle to be past, so
	// there is no tail. Retail cannot reach this state (it stores `m_IdealActivity` and
	// `m_nSequence` in one apply); this runtime composes the two from different producers, so the
	// guard is implementation-necessary — the same one `RefusesReselection` carries.
	if (!State.bHasSequence)
	{
		return false;
	}
	if (IsAnimationDriven(State))
	{
		return false;   // still busy: the cycle has not reached the sequence's `w_hold` yet
	}
	// The seven direction bits and nothing else. The attack bits, `+use`, duck and the camera verbs
	// are invisible here exactly as they are to attack selection, so a player holding only attack
	// still stops.
	return (HeldSelectionMask & ElysiumCombo::SelectionMask) == 0;
}

void ElysiumClipMovement::PositionAtFrame(const FElysiumClipMovementPath& Path, float Frame,
	FVector& OutPositionCm, float& OutYawDegrees)
{
	OutPositionCm = FVector::ZeroVector;
	OutYawDegrees = 0.0f;

	float PrevFrame = 0.0f;
	for (const FElysiumMovementRecord& Record : Path.Records)
	{
		const float EndFrame = static_cast<float>(Record.EndFrame);
		if (EndFrame < Frame)
		{
			// Wholly behind the wanted frame: this block's cumulative position stands, and the walk
			// continues from its end.
			PrevFrame = EndFrame;
			OutPositionCm = Record.PositionCm;
			OutYawDegrees = Record.YawDegrees;
			continue;
		}
		// The block the frame falls inside. `f` is the fraction through it, and `d` is the ease's
		// own integral over that fraction — `v0` and `v1` are lengths, so `d` is a distance along
		// the block's direction and never a speed.
		const float Span = EndFrame - PrevFrame;
		const float F = Span > 0.0f ? FMath::Clamp((Frame - PrevFrame) / Span, 0.0f, 1.0f) : 0.0f;
		const float D = Record.V0Cm * F + 0.5f * (Record.V1Cm - Record.V0Cm) * F * F;
		OutPositionCm += D * Record.Direction;
		OutYawDegrees = OutYawDegrees * (1.0f - F) + Record.YawDegrees * F;
		return;
	}
	// Past the last record: the whole authored path has been walked, and the loop above already left
	// the final cumulative position in place.
}

bool ElysiumClipMovement::SampleDelta(const FElysiumClipMovementPath& Path, int32 FrameCount,
	float CycleFrom, float CycleTo, FVector& OutDeltaCm)
{
	OutDeltaCm = FVector::ZeroVector;
	if (Path.IsEmpty() || FrameCount < 2)
	{
		// `Studio_AnimMovement`'s only false: `nummovements == 0`. Nothing refills the discarded
		// command and the body is held where it stands by ordinary friction. `FrameCount < 2` is the
		// same absence expressed by a clip a cycle cannot be mapped onto at all.
		return false;
	}
	if (!(CycleFrom < CycleTo))
	{
		// **A window that does not advance is TRUE with a zero delta, not false.** A zero-width
		// window — a clip whose length rounded to nothing, a zero-delta substep, the tail of a clip
		// already clamped to 1.0 — still substitutes: `WalkMove` assigns `Velocity = 0` and the body
		// stops dead. Answering false instead leaves `bSubstitutedMove` clear and the body coasts
		// down under friction, which is a different motion the player can see.
		return true;
	}
	// The cycle maps onto `numframes - 1`: the cells are the range's endpoints, so cycle 1.0 is the
	// last frame rather than one past it.
	const float Span = static_cast<float>(FrameCount - 1);
	FVector FromPosition = FVector::ZeroVector;
	FVector ToPosition = FVector::ZeroVector;
	float FromYaw = 0.0f;
	float ToYaw = 0.0f;
	PositionAtFrame(Path, CycleFrom * Span, FromPosition, FromYaw);
	PositionAtFrame(Path, CycleTo * Span, ToPosition, ToYaw);

	// The window's displacement, rotated back into the frame the window OPENED in — retail applies
	// `-angFrom` before anything consumes the delta. Every shipped record states a yaw of exactly
	// zero (`docs/vtmb/animation_and_movers.md` carries the census), so this rotation is the identity
	// on all of VtMB's own content; it is applied rather than dropped because that zero is a fact
	// about the corpus, not about the format.
	const FVector Local = ToPosition - FromPosition;
	OutDeltaCm = FMath::IsNearlyZero(FromYaw)
		? Local
		: FRotator(0.0f, -FromYaw, 0.0f).RotateVector(Local);
	return true;
}
