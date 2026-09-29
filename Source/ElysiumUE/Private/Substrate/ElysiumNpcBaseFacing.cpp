// `CAI_BaseNPC`'s bodies of the `Facing` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseFacing.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"

// --- Moved from `ElysiumNpcFacing.cpp` (story 5 step 5) ---

float FElysiumNpcBase::MotorDeltaIdealYaw() const
{
	// `CAI_Motor::DeltaIdealYaw` `0x102e1f90`:
	//     float cur = UTIL_AngleMod( GetOuter()->GetAngles().y );
	//     if (cur == m_IdealYaw) return 0.0f;                     // _DAT_104454c4
	//     return UTIL_AngleDiff( m_IdealYaw, cur );
	// with `UTIL_AngleMod(a) = 0.0054931640625f * (int(a * 65536/360) & 65535)` — the 16-bit
	// quantisation, whose leading constant is `_DAT_1044ffdc`.
	//
	// Story 8 wave 2 (L13): both inputs stand -- `m_IdealYaw` is `MotorIdealYaw` (motor `+0x34`,
	// kept at the travel yaw by the navigator step while a route runs, `NavigatorMoveStep`) and the
	// body's yaw is the entity's retail-frame `Angles.Y`, synced off the mover every frame.
	const float Current = StartTaskAngleMod(static_cast<float>(Angles.Y));
	if (Current == MotorIdealYaw)                                        // _DAT_104454c4 arm
	{
		return 0.f;
	}
	return NpcKernelFacingShared::FacingRetailAngleDiff(MotorIdealYaw, Current);   // 0x1013d580
}

void FElysiumNpcBase::MotorAddFacingTarget(const FFacingTargetRequest& Request)
{
	// `CAI_Motor` slots 12/13/14 — `0x102e2150`, `0x102e2120`, `0x102e20f0`: a 32-byte forward each
	// into `m_facingQueue` (motor+0x54) through `0x102d8f20` / `0x102d8e50` / `0x102d8cf0` (read
	// 2026-09-29, 0019/6). All three end in `0x102d9040`, which appends the record at the tail:
	// `+0x14` curtime, `+0x18` curtime + duration, `+0x1c` ramp / duration, `+0x20` importance, the
	// arguments in the order (importance, duration, ramp). Before appending each removes the FIRST
	// record already holding the target: the ENTITY adder (`0x102d8cf0`, kind 0) matches the same
	// resolved entity ONLY when that record's `+0x1c` ramp is 0; the POSITION adder (`0x102d8e50`,
	// kind 1) matches exact component equality; the BOTH adder (`0x102d8f20`, kind 2) matches the
	// same entity with no ramp test. The two entity adders also keep the LARGER importance when the
	// record they replace was added this very frame (`+0x14 == curtime`). A stale handle never matches.
	FacingTargetRequests.Add(Request);
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FFacingQueueEntry Entry;
	Entry.Kind = Request.MotorSlot == 14 ? FacingQueueKindEntity
		: Request.MotorSlot == 13 ? FacingQueueKindPosition : FacingQueueKindBoth;
	Entry.Target = Entry.Kind == FacingQueueKindPosition ? FElysiumEntityHandle() : Request.Target;
	Entry.PositionCm = Request.Position;
	Entry.StartSeconds = Now;                                            // +0x14 curtime
	Entry.EndSeconds = Now + Request.Duration;                           // +0x18 curtime + duration
	Entry.Ramp = Request.Ramp / Request.Duration;                        // +0x1c, divided unguarded
	Entry.Interest = Request.Importance;                                 // +0x20
	const FElysiumEntity* const Entity =
		Entry.Kind != FacingQueueKindPosition && World != nullptr && Entry.Target.IsSet()
		? World->Resolve(Entry.Target) : nullptr;
	for (int32 Index = 0; Index < FacingQueue.Num(); ++Index)
	{
		const FFacingQueueEntry& Held = FacingQueue[Index];
		const bool bSame = Entry.Kind == FacingQueueKindPosition
			? Held.PositionCm.X == Entry.PositionCm.X && Held.PositionCm.Y == Entry.PositionCm.Y
				&& Held.PositionCm.Z == Entry.PositionCm.Z
			: Entity != nullptr && Held.Target.IsSet() && World->Resolve(Held.Target) == Entity
				&& (Entry.Kind != FacingQueueKindEntity || Held.Ramp == 0.f);   // 0x102d8cf0: `+0x1c == 0.0f`
		if (bSame)
		{
			if (Entry.Kind != FacingQueueKindPosition && Held.StartSeconds == Now
				&& Entry.Interest <= Held.Interest)                          // same frame: the larger importance
			{
				Entry.Interest = Held.Interest;
			}
			FacingQueue.RemoveAt(Index);                                 // the first match only
			break;
		}
	}
	FacingQueue.Add(Entry);
}

bool FElysiumNpcBase::MotorFacingEntryExpired(const FFacingQueueEntry& Entry, double Now) const
{
	// `0x102d8b50`: the end stamp is past, or an entity entry's handle is dead.
	if (Entry.EndSeconds < Now)
	{
		return true;
	}
	return Entry.Kind == FacingQueueKindEntity
		&& (World == nullptr || !Entry.Target.IsSet() || World->Resolve(Entry.Target) == nullptr);
}

float FElysiumNpcBase::MotorFacingEntryInterest(const FFacingQueueEntry& Entry, double Now) const
{
	// `0x102d8bc0`, read as the humanoid `MaintainEyeDirection` pass 5's inline copy (a dead row): zero-weighted strictly outside
	// `[0, 1]`; the 1.0 of `1 - t` is the DOUBLE `0x10449280`; the cubic's 3.0 is `_DAT_10449258`
	// there (`InterestCubicThree`, cells-fix3.tsv). Whether `0x102d8bc0` reads its 2.0 from a cell or
	// doubles `f` in the FPU is **unrecovered**; `f + f` is the same number.
	const float T = static_cast<float>((Now - Entry.StartSeconds) / (Entry.EndSeconds - Entry.StartSeconds));
	if (T < ElysiumNpcTunables::Zero || T > ElysiumNpcTunables::One)
	{
		return ElysiumNpcTunables::Zero;
	}
	float F = ElysiumNpcTunables::One;
	if (T < Entry.Ramp)
	{
		F = T / Entry.Ramp;
	}
	else if (T > static_cast<float>(ElysiumNpcTunables::OneDouble - Entry.Ramp))
	{
		F = static_cast<float>((ElysiumNpcTunables::OneDouble - T) / Entry.Ramp);
	}
	const float Cubic = static_cast<float>(ElysiumNpcTunables::InterestCubicThree) * F * F - (F + F) * F * F;
	return Cubic * Entry.Interest;
}

FVector FElysiumNpcBase::MotorFacingEntryPosition(FFacingQueueEntry& Entry)
{
	if (Entry.Kind == FacingQueueKindEntity && World != nullptr && Entry.Target.IsSet())
	{
		if (const FElysiumEntity* const Entity = World->Resolve(Entry.Target))
		{
			Entry.PositionCm = Entity->EyePosition();                    // the cached +0x08, refreshed
		}
	}
	return Entry.PositionCm;
}

FVector FElysiumNpcBase::MotorFacingQueueBlend(double& OutRangeCm)
{
	// `CAI_Motor` slot 15 `0x102e2180` (shape.md "CAI_Motor's unnamed bodies"). First the queue is
	// compacted in place: an expired entry (`0x102d8b50` through `0x102e2b10`) is removed WITHOUT
	// advancing the cursor, so two adjacent dead entries both go.
	OutRangeCm = 0.0;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	for (int32 Index = 0; Index < FacingQueue.Num();)
	{
		if (MotorFacingEntryExpired(FacingQueue[Index], Now))
		{
			FacingQueue.RemoveAt(Index);
		}
		else
		{
			++Index;
		}
	}
	// Then the survivors, in order: `acc = (target - self) * w + acc * (1 - w)` with `w` from
	// `0x102d8bc0` and the 1.0 the float `_DAT_104454c0`, the accumulator normalised after each. The
	// per-entry delta is normalised too and the result DISCARDED -- the registers the blend multiplies
	// were loaded before that call -- so the delta enters RAW, in retail units: a far target pulls
	// harder than a near one of the same weight. Reproduced; the discarded normalise is not run.
	FVector Acc = FVector::ZeroVector;
	for (FFacingQueueEntry& Entry : FacingQueue)
	{
		const float W = MotorFacingEntryInterest(Entry, Now);
		const FVector Point = MotorFacingEntryPosition(Entry);
		const FVector DeltaUnits = (Point - Origin) / ElysiumMove::U;
		Acc = DeltaUnits * W + Acc * (ElysiumNpcTunables::One - W);
		Acc = Acc.GetSafeNormal();                                       // VectorNormalize: zero stays zero
		if (W != ElysiumNpcTunables::Zero)
		{
			OutRangeCm = FMath::Max(OutRangeCm, FVector::Dist(Point, Origin));
		}
	}
	return Acc;
}

void FElysiumNpcBase::MotorHandFacingTarget()
{
	// Retail's consumer is `MoveFacing` (motor slot 18, closed at CMC), which takes slot 15's heading
	// while the sequence carries `move_yaw`. The port's body faces a point, so the blend is handed as
	// one: the NPC's origin along the blended bearing, at the farthest contributing entry's range (a
	// single entry lands on its own point). Port presentation only; the bearing is retail's.
	double RangeCm = 0.0;
	const FVector Dir = MotorFacingQueueBlend(RangeCm);
	TOptional<FVector> Point;
	if (!Dir.IsZero())
	{
		Point = Origin + Dir * RangeCm;
	}
	if (Motor != nullptr && Point != FacingTargetHanded)
	{
		Motor->SetFacingTarget(Point);
	}
	FacingTargetHanded = Point;
}

void FElysiumNpcBase::SetIdealActivityNumber(int32 Activity)
{
	// `CAI_BaseNPC::SetIdealActivity` `0x10272650`, the half this family is measured by:
	//     if (act == ACT_INVALID) { vtable[0x4d8](); return; }       // slot 342's reset arm
	//     m_IdealActivity = act;                                     // +0x0ff0
	//     ResetIdeal(act, &m_nIdealSequence, &m_IdealTranslatedActivity, &m_IdealWeaponActivity);
	// `0x10272130`'s translation chain is story 29d's; the store is reproduced and the three ideal
	// words beside it are left for it.
	IdealActivityNumber = Activity;
}

// --- Slot 277 `SetViewtarget` -------------------------------------------------------------------

void FElysiumNpcBase::AddFacingTarget(const FVector& Position, float Importance, float Duration,
	float Ramp)
{
	// `0x10278d20`, slot 518 — the position overload, motor slot 13 (`0x102e2120`).
	if (!FacingTargetsEnabled())
	{
		return;
	}
	FFacingTargetRequest Request;
	Request.MotorSlot = 13;
	Request.Position = Position;
	Request.Importance = Importance;
	Request.Duration = Duration;
	Request.Ramp = Ramp;
	MotorAddFacingTarget(Request);
}

void FElysiumNpcBase::AddFacingTarget(FElysiumEntity* FaceEntity, const FVector& Position,
	float Importance, float Duration, float Ramp)
{
	// `0x10278d90`, slot 517 — the entity-plus-offset overload, motor slot 12 (`0x102e2150`).
	if (!FacingTargetsEnabled())
	{
		return;
	}
	FFacingTargetRequest Request;
	Request.MotorSlot = 12;
	Request.Target = FaceEntity != nullptr ? FaceEntity->Handle : FElysiumEntityHandle();
	Request.Position = Position;
	Request.Importance = Importance;
	Request.Duration = Duration;
	Request.Ramp = Ramp;
	MotorAddFacingTarget(Request);
}

// --- Slot 539 `SetAim` --------------------------------------------------------------------------

void FElysiumNpcBase::SetAim(const FVector& AimDirection)
{
	// `0x1026b480`, 79 bytes:
	//     QAngle angles; VectorAngles( aim, angles );          // 0x10139970
	//     SetPoseParameter( "aim_pitch", angles.x );           // vtable +0x564, slot 345
	//     SetPoseParameter( "aim_yaw",   0.0f );
	// The yaw argument is a hard zero in the listing (`uVar1 = 0` pushed as the float), not the
	// computed yaw: retail aims the pitch pose and pins the yaw pose at neutral.
	const FVector AimAngles = NpcKernelFacingShared::FacingRetailVectorAngles(AimDirection);
	SetPoseParameterByName(TEXT("aim_pitch"), static_cast<float>(AimAngles.X));
	SetPoseParameterByName(TEXT("aim_yaw"), 0.f);
}

// --- Slot 537 `SetHeadDirection` ----------------------------------------------------------------

void FElysiumNpcBase::SetHeadDirection(FVector& LookTarget, float Interval)
{
	// `CAI_BaseNPC::SetHeadDirection` `0x1026af70`, the Troika line's fill of slot 537. Read from
	// the listing, because the decompiler lost the pitch half's register aliasing.
	//
	// Constants, all out of retail `.rdata`: `_DAT_104454c4 = 0.0f`, `_DAT_1047049c = 0.8f`,
	// `_DAT_1049954c = 0.2f`, `_DAT_104493d0 = 0.1` (a **double**, subtracted as one),
	// `_DAT_10450568 = 360.0f`, `_DAT_10446758 = 57.29578f` (radians to degrees).
	if ((CapabilityWord & 0x1000) == 0)   // bits_CAP_TURN_HEAD; `CapabilitiesGet` 0x1026db30
	{
		return;
	}

	// The yaw half, from GetOrigin (slot 220) and GetAngles (slot 221).
	const float BodyYaw = static_cast<float>(Angles.Y);
	const FVector YawDelta = LookTarget - Origin;
	const float TargetYaw = NpcKernelFacingShared::FacingRetailAngleDiff(NpcKernelFacingShared::RetailYawOf(YawDelta, BodyYaw), BodyYaw);
	if (Interval > 0.0f)
	{
		// A do/while: an interval at or under one step still integrates once.
		float Value = HeadYaw;
		float Remaining = Interval;
		do
		{
			Value = Value * ElysiumNpcTunables::HeadFilterKeep + TargetYaw * ElysiumNpcTunables::HeadFilterBlend;
			Remaining -= static_cast<float>(ElysiumNpcTunables::TenthDouble);   // retail subtracts the double cell (0.1)
		} while (Remaining > 0.0f);
		HeadYaw = Value;
	}
	if (HeadYaw > ElysiumNpcTunables::HeadAngleRunawayLimit)
	{
		// Retail's lone guard, and it is one-sided: a filter that ran away negative is left alone.
		HeadYaw = 0.f;
	}
	// `SetBoneController(0, m_flHeadYaw)` `0x10095cb0` returns the value CLAMPED to the controller's
	// authored range and retail assigns it back. `Studio_SetController` returns its argument
	// unchanged when the model declares no controller at that index, and no shipped VtMB model
	// declares one — the same fact `FElysiumCombatCharacter::FilterHeadTurn` records — so the
	// assignment is the identity and the value never reaches a skeleton, so it is left out rather
	// than written as a self-assignment.

	// The pitch half, from EyePosition (slot 193) and the FULL 3-D distance to the target.
	const FVector Eye = EyePosition();
	const FVector PitchDelta = LookTarget - Eye;
	const float Distance = static_cast<float>(PitchDelta.Size());
	const float TargetPitch = -FMath::RadiansToDegrees(
		FMath::Atan(static_cast<float>(PitchDelta.Z) / Distance));
	if (Interval > 0.0f)
	{
		float Value = HeadPitch;
		float Remaining = Interval;
		do
		{
			Value = Value * ElysiumNpcTunables::HeadFilterKeep + TargetPitch * ElysiumNpcTunables::HeadFilterBlend;
			Remaining -= static_cast<float>(ElysiumNpcTunables::TenthDouble);   // retail subtracts the double cell (0.1)
		} while (Remaining > 0.0f);
		HeadPitch = Value;
	}
	if (HeadPitch > ElysiumNpcTunables::HeadAngleRunawayLimit)
	{
		HeadPitch = 0.f;
	}
	// `SetBoneController(1, m_flHeadPitch)` — and retail does NOT assign this one back, unlike the
	// yaw. Reproduced as the asymmetry it is.
}

FVector FElysiumNpcBase::EyeDirection2D()
{
	// `0x1026b210`, 20 bytes: a tail jump through this object's own vtable at +0x5c8, which is
	// slot 370 — `HeadDirection2D`. The base tier has no independent eye aim, and the forward IS the
	// whole behaviour.
	return HeadDirection2D();
}

FVector FElysiumNpcBase::EyeDirection3D()
{
	// `0x1026b240`, 20 bytes: the same forward through +0x5cc, slot 371 — `HeadDirection3D`.
	return HeadDirection3D();
}

FElysiumNpcBase::FTurnActivityPick FElysiumNpcBase::TurnActivityBaseLadder(float YawDelta,
	TFunctionRef<bool(int32)> HasSequence)
{
	// `CAI_BaseNPC::SetTurnActivity` `0x10289d10`. Five rungs against the motor's yaw delta, in
	// retail's order; the first whose activity the body authors wins, and the ladder falls to
	// `ACT_IDLE` (1) when none does. The first three tag `m_afMemory |= 0x2000`; the last two do
	// not, and that asymmetry is the base line's alone — the Troika ladder tags every pick.
	//
	// `_DAT_1049a198 = -80.0f`, `_DAT_1049a194 = -100.0f`, `_DAT_104454c8 = 80.0f`,
	// `_DAT_10450564 = 100.0f`, `_DAT_1049a188 = 160.0` (a **double**), `_DAT_1049a180 = -45.0f`,
	// `_DAT_1049949c = 45.0f`.
	if (YawDelta < ElysiumNpcTunables::MinusEighty && ElysiumNpcTunables::MinusHundred <= YawDelta && HasSequence(0xa2))
	{
		return FTurnActivityPick{ 0xa2, true };   // ACT_90_RIGHT
	}
	if (ElysiumNpcTunables::Eighty <= YawDelta && YawDelta < ElysiumNpcTunables::Hundred && HasSequence(0xa1))
	{
		return FTurnActivityPick{ 0xa1, true };   // ACT_90_LEFT
	}
	if (static_cast<float>(ElysiumNpcTunables::OneSixtyDouble) <= FMath::Abs(YawDelta) && HasSequence(0x9d))
	{
		return FTurnActivityPick{ 0x9d, true };   // ACT_180_LEFT
	}
	if (YawDelta < ElysiumNpcTunables::MinusFortyFive && HasSequence(0x3c))
	{
		return FTurnActivityPick{ 0x3c, false };  // ACT_TURN_RIGHT
	}
	if (ElysiumNpcTunables::FortyFive <= YawDelta && HasSequence(0x3b))
	{
		return FTurnActivityPick{ 0x3b, false };  // ACT_TURN_LEFT
	}
	return FTurnActivityPick{ 1, false };         // ACT_IDLE
}

void FElysiumNpcBase::ClearFacingTarget()
{
	// `CAI_Motor` slot 7 `0x102e11f0`, 95 bytes:
	//     UpdateYaw( -1 );                           // thunk 0x102e1e20
	//     Vector v; 0x102e26b0( v );                 // fills three floats (the velocity)
	//     GetOuter()->vtable[0x4d8]( v[1] > 0.0f ? 0x2d : 0x2e );   // slot 342, _DAT_104454c4
	//     m_flMoveInterval (+0x30) = 0;
	// Its footprint writes `+0x30` and reads `+0x04` only (functions.md): it never reads the facing
	// queue at `+0x54`, so it cancels no entry. It is the navigator's in-flight jump step --
	// `MoveJump` (the navigator's jump arm, closed at UNavigationSystem) calls it, `ACT_LEAP_ASCEND 0x2d` rising, else `ACT_LEAP_DESCEND 0x2e`
	// (conditions-and-states.md, "The burning trio"); shape.md's "cancels the queue's current entry"
	// is the older guess (0019/6 fix 3). So the queue is left whole: an entry leaves it only by
	// expiring (slot 15's compaction) or by a re-add of its target. The jump step has no port caller
	// (`MoveJump` is not ported) and is not run here.
}

bool FElysiumNpcBase::FacingIdeal() const
{
	// `0x10278c80`, 32 bytes, read from the listing because the decompiler turned the FPU compare
	// into a strict `<`:
	//     FABS( m_pMotor->DeltaIdealYaw() );  FCOMP double [0x10499568];  TEST AH,0x41;  JP …
	// less-OR-EQUAL returns true. `_DAT_10499568` is a **double** — `0.006` — not the float the
	// overlapping symbol at that address reads as. This is the SDK's own `FacingIdeal`, tolerance
	// included.
	return FMath::Abs(MotorDeltaIdealYaw()) <= static_cast<float>(ElysiumNpcTunables::FacingIdealTolerance);   // a double in retail; the float compare keeps the equal bit
}

// --- Moved from `ElysiumNpcFacing.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::FacingTargetsEnabled() const
{
	// The retail cvar at `0x10924f74`, read as `!vtable[1]() && m_nValue != 0`:
	// `debug_allow_move_facing`, shipped "1" — the facing-target family is ON.
	return ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::DebugAllowMoveFacing) != 0;
}

