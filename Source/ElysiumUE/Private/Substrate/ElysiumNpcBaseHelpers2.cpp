// `CAI_BaseNPC`'s bodies of the `Helpers2` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseHelpers2.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcTroikaHelpers2Shared.h"
#include "Substrate/ElysiumSchedule.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `_DAT_10450564` — `CAI_Motor#4`'s deceleration scale (100.0), applied to BOTH the interval
	// bound and the velocity it issues.
	constexpr float TroikaMotorDecelScale = ElysiumNpcTunables::Hundred;
	// `_DAT_10450aa4` — how much of the remaining distance `CAI_Motor#4` draws off the interval per
	// call (0.01).
	constexpr float TroikaMotorDecelDrain = ElysiumNpcTunables::Hundredth;
	// `_DAT_1044e658` — the distance at or below which `CAI_Motor#4` treats the goal as reached and
	// draws nothing off the interval: the DOUBLE 0.01 (`102e1050 FCOMP double ptr`).
	constexpr double TroikaMotorArrivedDistanceUnits = ElysiumNpcTunables::HundredthDouble;
	// `_DAT_1044ffdc` — the scale `CAI_Motor#18` applies to `ftol(yaw * 65536/360) & 0xffff`, the
	// 16-bit angle quantum 360 / 65536.
	constexpr float TroikaMotorYawQuantum = ElysiumNpcTunables::AngleQuantum;
	// `_DAT_1044ffe0` — its inverse, 65536 / 360, which the yaw is multiplied by BEFORE the `ftol`
	// (`102e1a41` / `102e1afc FMUL float ptr`).
	constexpr float TroikaMotorYawQuantumInverse = ElysiumNpcTunables::AngleQuantumInverse;
	// The literal activity ids the three `CAI_Motor` bodies force through the owner's vtable
	// `+0x4d8` (slot 342, `ForcePreTranslatedSequenceAndActivity`).
	constexpr int32 TroikaMotorInitActivity = 0x33;     // CAI_Motor#3
	constexpr int32 TroikaMotorStopFaceActivity = 0x2c; // CAI_Motor#6
	constexpr int32 TroikaMotorFullStopActivity = 0x30; // CAI_Motor#8
	// `CAI_Motor#3`'s `SetSolid(2)` and its `m_flGravity` (+0x3ec) zero, and the `(0, 5, 0)` it
	// dispatches through the owner's `+0x340` (slot 208).
	constexpr int32 TroikaMotorInitSolid = 2;
	constexpr int32 TroikaMotorInitSlot208Arg = 5;
	// `CAI_StandoffBehavior::vfunc3`'s capability bit and its sequence activity, both literals.
	constexpr uint32 TroikaStandoffRangedCapabilityBit = 0x8000000u;
	constexpr int32 TroikaStandoffHeaviestSequenceActivity = 8;
	constexpr int32 TroikaStandoffDisciplineCastCounterRequired = 2;
	// `CAI_StandoffBehavior::vfunc20` / `vfunc21`'s behaviour-local schedule id, and `vfunc5`'s
	// literal `2`.
	constexpr int32 TroikaStandoffLocalScheduleId = 0x17;
	constexpr int32 TroikaStandoffOwnerWord0x1fcValue = 2;
}

// --- Moved from `ElysiumNpcTroikaHelpers2.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::HintYaw(int32 HintNode, float& OutYaw) const
{
	// `thunk_FUN_102d12e0(hint)` — the hint's own facing yaw. **SEAM**, false.
	(void)HintNode;
	(void)OutYaw;
	return false;
}

FVector FElysiumNpcBase::HintAttackExtentsUnits() const
{
	// Slot 16 (vtable `+0x40`) — the attack-extent margin. **SEAM**: the port already carries the
	// margin itself (`FElysiumEntity::SetAttackExtents`, `CBaseEntity::SetAttackExtents 0x1009af40`),
	// and nothing produces a per-hint one, so this answers the zero margin.
	return FVector::ZeroVector;
}

int32 FElysiumNpcBase::NavCurrentLinkActivity() const
{
	// `thunk_FUN_102ee3f0(m_pNavigator)`. **SEAM**: family Motor records the same absent link
	// object (`NavLinkActivity`). `-1` — and `m_IdealActivity` is never `-1` on a live body, so the
	// compare fails and the activity is not replayed, which is the arm that changes nothing.
	return INDEX_NONE;
}

int32 FElysiumNpcBase::StandoffScheduleForLocalId(int32 LocalId) const
{
	// `thunk_FUN_102cc1f0(this, localId)` — `CAI_Behavior::GetSchedule(localId)`. **SEAM**: there is
	// no behaviour-local id space on this substrate. `None`, so the compare against a RUNNING
	// program fails and neither `vfunc20` nor `vfunc21` clears its condition.
	(void)LocalId;
	return ElysiumScheduleId::None;
}

uint32 FElysiumNpcBase::StandoffOwnerCapabilityWord() const
{
	// Slot 513 (vtable `+0x804`) on the owning NPC. **SEAM**, `0` — which closes `vfunc3`'s gate
	// and, because the gate is inside the `+0x19` test, still CLEARS `bStandoffRangedCache`.
	return 0u;
}

int32 FElysiumNpcBase::SelectHeaviestSequence(int32 Activity, int32 CurrentSequence) const
{
	// `CBaseAnimating::SelectHeaviestSequence(owner, 8, -1)`. **SEAM**: the animating tier publishes
	// no weighted sequence set to the kernel (family **Facing** records the same gap for
	// `SelectWeightedSequence`). `INDEX_NONE`, which is retail's `< 0` refusal.
	(void)Activity;
	(void)CurrentSequence;
	return INDEX_NONE;
}

int32 FElysiumNpcBase::DisciplineCastCounter() const
{
	// `owner->m_iDisciplineCastCounter`, which `vfunc3` requires to be exactly 2. **SEAM**: no such
	// counter on this leaf; `0`.
	return 0;
}

void FElysiumNpcBase::FUN_102e0ea0()
{
	// `CAI_Motor#3` `0x102e0ea0`, in retail's order:
	//     owner->vtable[+0x4d8](0x33);          // slot 342, force activity 0x33 — FIRST
	//     MotorStateReset(this);                // thunk_FUN_102e2840
	//     SetSolid(owner, 2);                   // thunk_FUN_100dc480(owner->m_Collision, 2)
	//     owner->m_flGravity (+0x3ec) = 0;
	//     owner->vtable[+0x340](0, 5, 0);       // slot 208
	//
	// The activity dispatch is BEFORE the state reset, which the 29c walk has the other way round.
	++TroikaMotor.ForcedActivities;
	TroikaMotor.LastForcedActivity = TroikaMotorInitActivity;
	++TroikaMotor.StateResets;
	++TroikaMotor.SolidSets;
	(void)TroikaMotorInitSolid;
	(void)TroikaMotorInitSlot208Arg;
	++TroikaMotor.OwnerSlot208Dispatches;
}

bool FElysiumNpcBase::FUN_102e0f90(const FVector& GoalUnits, float Yaw)
{
	// `CAI_Motor#4` `0x102e0f90`:
	//     dir = goal - owner->slot220();                                 // vtable +0x370
	//     d = VectorNormalize(dir);                                      // 102e0fde, in place
	//     SetAbsVelocity(dir * _DAT_10450564);                           // the UNIT direction
	//     if (d < m_flMoveInterval * _DAT_10450564) {                    // +0x30
	//         if (d <= _DAT_1044e658) d = 0;
	//         m_flMoveInterval -= d * _DAT_10450aa4;
	//         owner->vtable[+0xf8](goal);
	//         return true;
	//     }
	//     m_flMoveInterval = 0;
	//     ReissueMove(this, yaw, -1.0);                                  // thunk_FUN_102e1c10
	//     return false;
	//
	// The velocity is issued on BOTH arms — it is written before the branch — and the `d <= arrived`
	// clamp means a body inside the arrival radius decelerates by nothing at all rather than by a
	// tiny amount. Both are retail's. SOURCE units.
	//
	// `slot220` (vtable `+0x370`) is the position accessor family **Hints** records as unidentified
	// in the census and answers with the entity's own origin; that same reading is used here rather
	// than a second one.
	const FVector PositionUnits = HintComparePosition(this) / ElysiumMove::U;
	const FVector Delta = GoalUnits - PositionUnits;
	const float Distance = static_cast<float>(Delta.Size());

	++TroikaMotor.VelocitySets;
	// `VectorNormalize` leaves a zero vector zero and answers 0.
	const FVector Direction = Distance > 0.f ? Delta / static_cast<double>(Distance) : FVector::ZeroVector;
	TroikaMotor.LastVelocityUnits = Direction * TroikaMotorDecelScale;

	if (Distance < TroikaMotor.MoveInterval * TroikaMotorDecelScale)
	{
		const float Drain =
			static_cast<double>(Distance) <= TroikaMotorArrivedDistanceUnits ? 0.f : Distance;
		TroikaMotor.MoveInterval -= Drain * TroikaMotorDecelDrain;
		++TroikaMotor.OwnerMoveDispatches;
		return true;
	}
	TroikaMotor.MoveInterval = 0.f;
	++TroikaMotor.MoveReissues;
	TroikaMotor.LastReissueYaw = Yaw;
	TroikaMotor.LastReissueSpeed = -1.0f;
	return false;
}

void FElysiumNpcBase::FUN_102e1180(const FVector& GoalUnits)
{
	// `CAI_Motor#6` `0x102e1180`:
	//     SetAbsVelocity(goal);                          // thunk_FUN_102e2690(this, param_1)
	//     owner->vtable[+0x340](0);                      // slot 208
	//     owner->vtable[+0x4d8](0x2c);                   // slot 342, force activity 0x2c
	//     ReissueMove(this, UTIL_VecToYaw(goal), -1.0);  // thunk_FUN_101d2c70, thunk_FUN_102e1c10
	//
	// Note it hands the GOAL VECTOR straight to `SetAbsVelocity`, not a scaled direction — that is
	// what the decompiled C says and it is reproduced.
	++TroikaMotor.VelocitySets;
	TroikaMotor.LastVelocityUnits = GoalUnits;
	++TroikaMotor.OwnerSlot208Dispatches;
	++TroikaMotor.ForcedActivities;
	TroikaMotor.LastForcedActivity = TroikaMotorStopFaceActivity;
	++TroikaMotor.MoveReissues;
	// `UTIL_VecToYaw(v)` — the planar heading, degrees.
	TroikaMotor.LastReissueYaw = static_cast<float>(
		FMath::RadiansToDegrees(FMath::Atan2(GoalUnits.Y, GoalUnits.X)));
	TroikaMotor.LastReissueSpeed = -1.0f;
}

void FElysiumNpcBase::FUN_102e1270()
{
	// `CAI_Motor#8` `0x102e1270` (also `CAI_HumanoidMotor#8`), the whole body:
	//     SetAbsVelocity(0, 0, 0);                       // thunk_FUN_102e2690 on a zeroed local
	//     owner->vtable[+0x4d8](0x30);                   // slot 342, force activity 0x30
	//
	// The retail motor's full stop, and there is no reissued move on this one.
	++TroikaMotor.VelocitySets;
	TroikaMotor.LastVelocityUnits = FVector::ZeroVector;
	++TroikaMotor.ForcedActivities;
	TroikaMotor.LastForcedActivity = TroikaMotorFullStopActivity;
}

FVector FElysiumNpcBase::BlendFacingQueue(TArrayView<const FFacingQueueEntry> Entries,
	const FVector& SelfUnits)
{
	// `CAI_Motor#15` `0x102e2180`'s second loop, as a pure function:
	//     acc = (0,0,0);
	//     for (e : entries) {
	//         w = Weight(e);
	//         VectorNormalize(Target(e) - GetAbsOrigin());     // computed and DISCARDED
	//         acc = (Target(e) - GetAbsOrigin()) * w + acc * (1 - w);
	//         VectorNormalize(acc);
	//     }
	//
	// The discarded normalise is retail's: the registers the blend multiplies are loaded BEFORE the
	// call, so the raw delta is what is blended, and only the accumulator is normalised. `1 - w` is
	// `_DAT_104454c0 - w`, the shared `1.0`.
	FVector Accumulator = FVector::ZeroVector;
	for (const FFacingQueueEntry& Entry : Entries)
	{
		const FVector Delta = Entry.TargetUnits - SelfUnits;
		const float Inverse = NpcKernelTroikaHelpers2Shared::TroikaSharedOne - Entry.Weight;
		Accumulator = Delta * Entry.Weight + Accumulator * Inverse;
		Accumulator.Normalize();
	}
	return Accumulator;
}

FVector FElysiumNpcBase::FUN_102e2180(int32& OutSurvivors)
{
	// `CAI_Motor#15` `0x102e2180`, both loops:
	//     for (i = 0; i < m_facingQueueCount; ) {                        // +0x3c
	//         if (!Live(queue[i])) { RemoveAt(queue, i, 1); --count; }   // 0x102d8b50, 0x102e2b10
	//         else ++i;
	//     }
	//     <the blend above>
	//
	// The compaction does NOT advance `i` when it drops an entry, so two expired entries in a row
	// are both removed — that is the correct in-place compaction and it is what retail wrote.
	//
	// **SEAM**: family **Facing**'s standing fact is that this mover keeps no facing queue
	// (`FacingTargetRequests` records what would have gone into one). The queue is therefore empty,
	// the compaction removes nothing, and the blend answers the zero vector — retail's own answer
	// for an empty queue.
	OutSurvivors = TroikaMotor.FacingQueueCount;
	TArray<FFacingQueueEntry> Live;
	return BlendFacingQueue(Live, Origin / ElysiumMove::U);
}

float FElysiumNpcBase::FUN_102e2580() const
{
	// `CAI_Motor#17` `0x102e2580`, the whole body:
	//     s = BaseSpeed(this);                            // thunk_FUN_102e12c0
	//     if (s < this->vtable[+0x40]()) s = this->vtable[+0x40]();
	//     f = HullSpeedFloor(this->field_0x8);            // thunk_FUN_102d61b0
	//     return f <= s ? s : f;
	//
	// **Both bounds are FLOORS.** 29c's walk reads the first as an upper bound; the listing raises
	// `s` to it when `s` is below, which is a maximum and not a clamp. So the body is
	// `max(max(base, motorFloor), hullFloor)` and the motor's `+0x40` cannot slow a body down.
	// SOURCE units.
	// `thunk_FUN_102e12c0(this)` — the base speed. **SEAM**, `0.0`; the same seam `CAI_Navigator#17`
	// asks for its own `out[9]`.
	float Speed = 0.f;
	if (Speed < TroikaMotor.SpeedCeilingUnits)
	{
		Speed = TroikaMotor.SpeedCeilingUnits;
	}
	return TroikaMotor.HullSpeedFloorUnits <= Speed ? Speed : TroikaMotor.HullSpeedFloorUnits;
}

void FElysiumNpcBase::FUN_102e19e0(const FVector& GoalUnits)
{
	// `CAI_Motor#18` `0x102e19e0`:
	//     if (owner->vtable[+0x838](goal, m_flMoveInterval)) return;     // slot 526, the clip test
	//     UTIL_VecToYaw(goal + 0xc);                                     // computed, DISCARDED
	//     seq = owner->m_nSequence;                                      // +0x6f0
	//     thunk_FUN_102e2790(this, seq);
	//     if (!HasPoseParameter(this, seq, "move_yaw")) {                 // thunk_FUN_102e2820
	//         ReissueMove(this, (ftol(yaw * _DAT_1044ffe0) & 0xffff) * _DAT_1044ffdc, -1.0);
	//         return;
	//     }
	//     this->vtable[+0x3c](&avg);                                     // motor slot 15 — the
	//                                                                    // facing-queue average
	//     VectorNormalize(avg);
	//     yaw = UTIL_VecToYaw(avg);
	//     ReissueMove(this, (ftol(yaw * _DAT_1044ffe0) & 0xffff) * _DAT_1044ffdc, -1.0);
	//     diff = UTIL_AngleDiff(yaw, owner->slot221()[1]);               // 0x1013d580
	//     if (owner->field_0x98) owner->field_0x98->m_flDesiredMoveYaw = -diff;   // +0x63ec
	//     else SetPoseParameter(this, "move_yaw", -diff);                // thunk_FUN_102e27d0
	//
	// `vtable[+0x3c]` on the MOTOR is slot 15, which IS `FUN_102e2180` above — the steering asks the
	// facing-queue average for its heading rather than the goal. That is the recovered link between
	// the two bodies and it is a call here, not a copy.
	//
	// The named condition key is `"move_yaw"`, and the two writes are the same number by two routes:
	// an NPC-backed motor writes the NPC's own word, a bare one writes the pose parameter.
	if (TroikaMotor.bSteerClipped)
	{
		return;
	}
	const int32 Sequence = SequenceNumber;
	if (!TroikaMotor.bHasMoveYawPoseParam)
	{
		++TroikaMotor.MoveReissues;
		TroikaMotor.LastReissueYaw = static_cast<float>(FMath::TruncToInt(static_cast<float>(
			FMath::RadiansToDegrees(FMath::Atan2(GoalUnits.Y, GoalUnits.X)))
			* TroikaMotorYawQuantumInverse) & 0xffff) * TroikaMotorYawQuantum;
		TroikaMotor.LastReissueSpeed = -1.0f;
		return;
	}
	(void)Sequence;

	int32 Survivors = 0;
	FVector Average = FUN_102e2180(Survivors);
	Average.Normalize();
	const float Yaw = static_cast<float>(
		FMath::RadiansToDegrees(FMath::Atan2(Average.Y, Average.X)));
	++TroikaMotor.MoveReissues;
	TroikaMotor.LastReissueYaw =
		static_cast<float>(FMath::TruncToInt(Yaw * TroikaMotorYawQuantumInverse) & 0xffff)
			* TroikaMotorYawQuantum;
	TroikaMotor.LastReissueSpeed = -1.0f;

	// `UTIL_AngleDiff(yaw, myAngles.yaw)` — the wrapped difference family **Bosses** already
	// transcribes for the same `0x1013d580`.
	const float Diff = static_cast<float>(FMath::FindDeltaAngleDegrees(
		Angles.Y, static_cast<double>(Yaw)));
	// `owner->field_0x98` is the owner's `m_pBaseNPCTroika` (`AsNpc()`): a Troika owner takes the
	// yaw in `m_flDesiredMoveYaw` (`+0x63ec`), any other owner in the `move_yaw` pose parameter.
	// One write either way, counted once.
	if (FElysiumNpc* const Troika = AsNpc())
	{
		Troika->ScheduleHost.DesiredMoveYaw = -Diff;
	}
	else
	{
		SetPoseParameterByName(TEXT("move_yaw"), -Diff);
	}
	++TroikaMotor.PoseParamWrites;
	TroikaMotor.LastPoseParamYaw = -Diff;
}

void FElysiumNpcBase::NavStopAndMarkDirty()
{
	// `thunk_FUN_102eeb70(this)` then `this->field_0x1c = 1`:
	//     nav->+0x54 = -1;
	//     nav->+0x58 = -1.0;
	//     nav->+0x60 = -1.0;
	//     ClearRoute(nav->+0x28);                                        // thunk_FUN_102ddc40
	//     nav->+0x1c = 1;
	//
	// `+0x1c` is family **Motor**'s `Navigator.bNavFailed` — the same latch `OnNavFailed`
	// (`0x102eeae0`) sets — so this writes THAT member rather than a second copy of the word.
	NavigatorWord0x54 = -1;
	NavigatorWord0x58 = -1.0f;
	NavigatorWord0x60 = -1.0f;
	++NavigatorRouteClears;
	Navigator.bNavFailed = true;
}

void FElysiumNpcBase::FUN_102eea70()
{
	// `CAI_Navigator#7` `0x102eea70`.
	NavStopAndMarkDirty();
}

void FElysiumNpcBase::FUN_102eeac0()
{
	// `CAI_Navigator#11` `0x102eeac0` — a BYTE-IDENTICAL body at a distinct slot. Two retail entry
	// points, one behaviour, so one method and two forwards.
	NavStopAndMarkDirty();
}

FElysiumNpcBase::FNavPathSample FElysiumNpcBase::NavPathSample() const
{
	// `m_pPath`'s four reads: the current point (`0x10012805`), the straight-line test
	// (`0x1030bd50`), the navigator radius (`0x102ecc40`) and the next waypoint's kind against its
	// owner's (`path+0x24` -> `+0x30` -> `+0x2c`). **SEAM**: family Motor's standing fact — no path
	// object here. Everything false and zero.
	return FNavPathSample();
}

FElysiumNpcBase::FNavMoveInfo FElysiumNpcBase::FUN_102eee40() const
{
	// `CAI_Navigator#17` `0x102eee40`, field by field:
	//     out[12] = m_navType;                                           // +0x18
	//     out[0..2] = CurrentPathPoint(m_pPath);
	//     out[3] = out[0] - myPos.x;  out[4] = out[1] - myPos.y;         // slot 220 (+0x370)
	//     if (m_navType == 0) { out[5] = 0; out[10] = Length2D(out+3); }
	//     else                { out[5] = out[2] - myPos.z; out[10] = VectorNormalize(out+3); }
	//     out[6..8] = out[3..5];
	//     out[9]  = BaseSpeed(nav->+0x20);                               // thunk_FUN_102e12c0
	//     out[11] = nav->+0x20->[+0x30] * out[9];
	//     out[13] = NavRadius(this);                                     // 0x102ecc40
	//     if (out[10] < out[11]) out[11] = out[10];
	//     if (IsStraightLine(m_pPath)) { out[14] |= 1; out[15] = m_pPath; return; }
	//     wp = m_pPath->+0x24; next = wp->+0x30;
	//     if (next && next->+0x2c != wp->+0x2c) out[14] |= 4;
	//     out[15] = m_pPath;
	//
	// **`out[10]` is computed BEFORE `out[11]`, and `out[3..5]` is normalised on the 3-D arm** — so
	// `out[6..8]` is the NORMALISED direction at nav type != 0 and the RAW delta at nav type 0.
	// That asymmetry is retail's and is reproduced.
	//
	// The goal tolerance is the MIN of the computed limit and the distance, which is why a body that
	// is nearly there cannot overshoot its own tolerance.
	FNavMoveInfo Info;
	Info.NavType = NavGetType();

	const FNavPathSample Path = NavPathSample();
	if (!Path.bValid)
	{
		// No path: retail would have dereferenced `m_pPath`. The block comes back zeroed with
		// `bHasPath` clear, which is this runtime's stated refusal.
		return Info;
	}

	const FVector MyUnits = HintComparePosition(this) / ElysiumMove::U;
	Info.TargetUnits = Path.PointUnits;
	Info.DeltaUnits.X = Info.TargetUnits.X - MyUnits.X;
	Info.DeltaUnits.Y = Info.TargetUnits.Y - MyUnits.Y;
	if (Info.NavType == 0)
	{
		Info.DeltaUnits.Z = 0.0;
		Info.DistanceUnits = static_cast<float>(
			FMath::Sqrt(Info.DeltaUnits.X * Info.DeltaUnits.X
				+ Info.DeltaUnits.Y * Info.DeltaUnits.Y));
	}
	else
	{
		Info.DeltaUnits.Z = Info.TargetUnits.Z - MyUnits.Z;
		Info.DistanceUnits = static_cast<float>(Info.DeltaUnits.Size());
		Info.DeltaUnits.Normalize();
	}
	Info.DirUnits = Info.DeltaUnits;
	// `thunk_FUN_102e12c0(nav->+0x20)` — the motor's base speed, the same seam `CAI_Motor#17` asks.
	Info.BaseSpeedUnits = 0.f;
	// `nav->+0x20->[+0x30]` is `CAI_Motor::m_flMoveInterval`, which this family carries.
	Info.GoalToleranceUnits = TroikaMotor.MoveInterval * Info.BaseSpeedUnits;
	Info.Radius = Path.RadiusUnits;
	if (Info.DistanceUnits < Info.GoalToleranceUnits)
	{
		Info.GoalToleranceUnits = Info.DistanceUnits;
	}
	if (Path.bStraightLine)
	{
		Info.Flags |= 1u;
	}
	else if (Path.bNextWaypointKindDiffers)
	{
		Info.Flags |= 4u;
	}
	Info.bHasPath = true;
	return Info;
}

bool FElysiumNpcBase::StandoffVfunc3()
{
	// `0x102c7410`, arm by arm:
	//     if (this[0x19] == 0) return false;
	//     if (owner->vtable[+0x804]() & 0x8000000) {                     // slot 513
	//         if (SelectHeaviestSequence(owner, 8, -1) >= 0) {
	//             if (owner->m_iDisciplineCastCounter == 2
	//                 && GetActiveWeapon(owner) != NULL) return true;
	//             return false;                                          // and +0x19 is KEPT
	//         }
	//     }
	//     this[0x19] = 0;                                                // both other refusals
	//     return false;
	//
	// **The clear is NOT on every refusal.** A body that has the capability bit and a sequence but
	// fails the counter or the weapon KEEPS `+0x19`; only "no capability" and "no sequence" clear
	// it. That is the whole shape of the body and it is easy to read the other way round.
	if (!bStandoffRangedCache)
	{
		return false;
	}
	if ((StandoffOwnerCapabilityWord() & TroikaStandoffRangedCapabilityBit) != 0)
	{
		if (SelectHeaviestSequence(TroikaStandoffHeaviestSequenceActivity, INDEX_NONE) >= 0)
		{
			const bool bHasWeapon = World != nullptr && Inventory.ActiveWeapon.IsSet()
				&& World->Resolve(Inventory.ActiveWeapon) != nullptr;
			return DisciplineCastCounter() == TroikaStandoffDisciplineCastCounterRequired && bHasWeapon;
		}
	}
	bStandoffRangedCache = false;
	return false;
}

void FElysiumNpcBase::StandoffVfunc5()
{
	// `0x102c7530`, the whole body:
	//     if (owner->m_pHintNode != NULL) {                              // +0x5ddc
	//         if (IsHintUnusable(hint)) {                                // 0x102d14c0
	//             if (thunk_FUN_102d1450(hint, owner)) ReleaseHint(hint, 0.0);   // 0x102d1420
	//         }
	//     }
	//     owner->m_pHintNode = NULL;
	//     owner->m_flDistTooFar = this->+0x50;                           // +0x5de4
	//     owner->+0x1fc = 2;
	//
	// **The release is gated on `IsHintUnusable` being TRUE**, which reads backwards — retail
	// releases a hint it has already decided is unusable. Reproduced as written. The hint is cleared
	// UNCONDITIONALLY afterwards, so a usable hint is dropped without being released, which is the
	// leak the schedule kernel's own release path exists to avoid.
	//
	// `IsHintUnusable` and `ReleaseHintNode` are family **Hints**' bodies and are called rather than
	// restated; `thunk_FUN_102d1450` (does this NPC own the hint) is `BaseScheduleHost.bOwnsHint`.
	if (BaseScheduleHost.HintNode != INDEX_NONE)
	{
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		if (IsHintUnusable(BaseScheduleHost.HintNode, Now) && BaseScheduleHost.bOwnsHint)
		{
			ReleaseHintNode(BaseScheduleHost.HintNode, 0.f);
		}
	}
	BaseScheduleHost.HintNode = INDEX_NONE;
	DistTooFar = StandoffDistTooFar;
	Field_0x01fc = TroikaStandoffOwnerWord0x1fcValue;
}

void FElysiumNpcBase::StandoffClearNewEnemyOnLocalSchedule()
{
	// `0x102c7960` / `0x102c79a0`, the whole body:
	//     if (owner->m_pSchedule != NULL                                 // +0x5c38
	//         && owner->m_pSchedule == GetSchedule(this, 0x17))          // 0x102cc1f0
	//         ClearCondition(owner, COND_NEW_ENEMY 0x54);                // 0x10269f30
	//
	// `StandoffScheduleForLocalId` is a seam answering `None`, so a running program never matches
	// and the condition is never cleared. That is the recovered refusal: the behaviour-local id
	// space has no source here.
	if (Schedule.Current == ElysiumScheduleId::None)
	{
		return;
	}
	if (Schedule.Current != StandoffScheduleForLocalId(TroikaStandoffLocalScheduleId))
	{
		return;
	}
	Cognition.Conditions.Clear(EElysiumNpcCond::NewEnemy);
}

void FElysiumNpcBase::StandoffVfunc20()
{
	// `CAI_StandoffBehavior#20` `0x102c7960`.
	StandoffClearNewEnemyOnLocalSchedule();
}

void FElysiumNpcBase::StandoffVfunc21()
{
	// `CAI_StandoffBehavior#21` `0x102c79a0` — a BYTE-IDENTICAL body at a distinct slot, the same
	// pair the navigator's 7/11 are.
	StandoffClearNewEnemyOnLocalSchedule();
}
