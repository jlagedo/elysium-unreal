// `CAI_BaseNPC`'s bodies of the `Motor` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseMotor.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	constexpr float GJumpGravity = ElysiumNpcTunables::JumpGravity;   // `0x101a6b80`
	// `CheckOnGround` `0x1026e5e0`.
	constexpr float GCheckOnGroundInterval = ElysiumNpcTunables::Half;
	constexpr double GCheckOnGroundSlack = ElysiumNpcTunables::MinusThousandthDouble;
	constexpr float GCheckOnGroundUp = static_cast<float>(ElysiumNpcTunables::TenthDouble);
	constexpr float GCheckOnGroundDown = 4.0f;      // _DAT_10449148
	constexpr float GTraceClearFraction = static_cast<float>(ElysiumNpcTunables::OneDouble);
	constexpr int32 GGroundTraceMask = 0x202400b;
	constexpr int32 GCoverTraceMask = 0x2804091;    // `ValidateNavGoal`'s
	// Conditions this family touches that `EElysiumNpcCond` does not name. Retail's `CAI_BaseNPC`
	// registrar is one dense namespace 0x00..0x76 and 0x73 sits in the unnamed tail of it; 0x7b is
	// above the base band entirely, so it is a species registration. Both are carried as retail's
	// own number.
	constexpr EElysiumNpcCond GCondOnGround = static_cast<EElysiumNpcCond>(0x73);
	constexpr EElysiumNpcCond GCondNavGoalInvalid = static_cast<EElysiumNpcCond>(0x39);
	// `TaskFail`'s reason on the `ValidateNavGoal` failure (`0x10280360`, `vtable+0x700` slot 448).
	constexpr int32 GFailNoCover = 0x1b;
	constexpr float GJumpApexScale = static_cast<float>(ElysiumNpcTunables::JumpApexScale);
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::NavGetType() const
{
	// `FUN_1027d990`: `return m_pNavigator->field_0x18;` — one word, 29 direct callers, the widest
	// read of this family.
	return Navigator.NavType;
}

void FElysiumNpcBase::NavSetType(int32 Type)
{
	// `FUN_1027d9b0` → `0x102eeba0`: `m_pNavigator->field_0x18 = value`.
	Navigator.NavType = Type;
	// The port's navigator IS `IElysiumNpcMotor`, and it carries the same four-value vocabulary
	// (`EElysiumNpcNavType`: Ground 0, Jump 1, Fly 2, Climb 3), so the stored word is pushed on.
	// A value outside the four has no counterpart and is stored only — retail stores anything.
	if (Motor != nullptr && Type >= 0 && Type <= 3)
	{
		Motor->SetNavigationType(static_cast<EElysiumNpcNavType>(Type));
	}
}

void FElysiumNpcBase::NavSnapshotOwnerPointers(int32 Argument)
{
	// `CAI_Navigator::vfunc3` `0x102ecb50`:
	//     this->+0x20 = owner->+0x5d44;   // m_pMotor
	//     this->+0x24 = owner->+0x5d40;   // m_pMoveProbe
	//     this->+0x28 = owner->+0x5d38;   // m_pLocalNavigator
	//     this->+0x2c = param_1;
	// All three owner words are `ELYSIUM_NPC_WORD_CHAIN` rows onto the one motor, so there are no
	// three pointers to snapshot. What survives is that the snapshot happened and what it was taken
	// with.
	Navigator.bSnapshotTaken = true;
	Navigator.SnapshotArgument = Argument;
}

bool FElysiumNpcBase::NavIsGoalActive() const
{
	// `thunk_FUN_102ee680(m_pNavigator)` — SDK `CAI_Navigator::IsGoalActive()`, which
	// `CAI_BaseNPC::IsMoving` (`0x10280300`) is a one-line forward to. The port's mover answers the
	// same question, so this is a wire and not a seam.
	return Motor != nullptr && Motor->SampleNavigation().bActiveGoal;
}

void FElysiumNpcBase::NavStopMoving()
{
	// `thunk_FUN_102ee2c0(m_pNavigator)` — SDK `CAI_Navigator::StopMoving()`.
	if (Motor != nullptr)
	{
		Motor->Stop();
	}
}

int32 FElysiumNpcBase::NavGoalState() const
{
	// `thunk_FUN_102ee620(m_pNavigator)` — the navigator's route/goal-state word, which
	// `ValidateNavGoal` requires to be exactly 6. **SEAM**: `IElysiumNpcMotor` keeps no goal type,
	// so this answers -1, which is "not that state" and is the arm retail takes for every other
	// value.
	return INDEX_NONE;
}

bool FElysiumNpcBase::NavLinkActivity(int32& OutActivity) const
{
	// `thunk_FUN_102ee6a0` (the pending link is valid) and `thunk_FUN_102ee510` (its cached
	// activity), the pair `FUN_1027a6c0` reads. **SEAM**, and the link object's retail identity is
	// **unrecovered** — the ledger itemises nothing past the `m_pNavigator` chain redirect.
	(void)OutActivity;
	return false;
}

bool FElysiumNpcBase::MotorApplyIntervalMovement(const FVector& DeltaUnits, float YawDelta)
{
	// `thunk_FUN_102e0bd0(m_pMotor, delta, yaw, …)` — the apply half of `AutoMovement`.
	// **SEAM, and a named modernization**: Unreal's animation instance extracts and applies root
	// motion itself, which is the visual-only half this port adopts freely. What the substrate owes
	// retail is the GATE and the ORDER above it, and those are ported; the apply is recorded here
	// and moves nothing.
	(void)DeltaUnits;
	(void)YawDelta;
	++MotorSeams.IntervalMovementApplied;
	return false;
}

bool FElysiumNpcBase::AnimIntervalMovement(float Interval, FVector& OutDeltaUnits,
	float& OutYawDelta) const
{
	// `CBaseAnimating::GetIntervalMovement(m_flAnimTime - m_flPrevAnimTime, …)`. **SEAM**: the
	// animating tier publishes no interval movement to the kernel.
	(void)Interval;
	OutDeltaUnits = FVector::ZeroVector;
	OutYawDelta = 0.f;
	return false;
}

bool FElysiumNpcBase::KernelHullTrace(const FVector& StartUnits, const FVector& EndUnits,
	const FVector& HullMins, const FVector& HullMaxs, int32 Mask, FKernelHullTrace& OutTrace) const
{
	// `thunk_FUN_1026e940` and `(*DAT_1070b254)->TraceRay`, the engine hull trace the three probe
	// bodies of this family run. **SEAM**: nothing traces a hull for the kernel here. `OutTrace`
	// keeps its defaults — fraction 1.0, no entity — which is retail's own CLEAR result, and every
	// caller below branches on the `false` return rather than on the defaults.
	(void)StartUnits;
	(void)HullMins;
	(void)HullMaxs;
	(void)Mask;
	// A clear trace ends where it was aimed; `endpos` is the one word of the clear answer a caller
	// can read back (`CNPC_VWerewolf::CheckStuck`'s `SetAbsOrigin(tr.endpos)`).
	OutTrace.EndPosUnits = EndUnits;
	++MotorSeams.HullTraces;
	return false;
}

bool FElysiumNpcBase::RetailHullExtents(int32 Hull, EElysiumHullExtents Which, FVector& OutMinsUnits,
	FVector& OutMaxsUnits) const
{
	// The shared hull table, replayed from the image's own static initialisers. A hull id outside
	// the table keeps the zero box and the false return every caller's failure arm was written
	// against, so an unrecovered id still refuses rather than boxing a point.
	const ElysiumRetailHulls::FRow* Row = ElysiumRetailHulls::Find(Hull);
	if (Row == nullptr)
	{
		OutMinsUnits = FVector::ZeroVector;
		OutMaxsUnits = FVector::ZeroVector;
		return false;
	}
	const bool bSmall = Which == EElysiumHullExtents::Small;
	OutMinsUnits = bSmall ? Row->SmallMins : Row->Mins;
	OutMaxsUnits = bSmall ? Row->SmallMaxs : Row->Maxs;
	return true;
}

bool FElysiumNpcBase::RetailCollisionExtents(const FElysiumEntity& Entity, FVector& OutMinsUnits,
	FVector& OutMaxsUnits)
{
	// `m_Collision` (+0x270) slots +4 / +8 — `OBBMins()` / `OBBMaxs()`. **SEAM**: `FElysiumEntity`
	// carries no collision extents; the bodies below refuse rather than box a point.
	(void)Entity;
	OutMinsUnits = FVector::ZeroVector;
	OutMaxsUnits = FVector::ZeroVector;
	return false;
}

uint32 FElysiumNpcBase::ActiveWeaponCapabilityWord() const
{
	// The active weapon's vtable +0x5a0 (slot 360, retail body `0x1014f930`). **SEAM**: no such
	// word on `FElysiumWeapon`; answering 0 closes `ShouldMoveAndShoot`'s Troika gate.
	return 0;
}

float FElysiumNpcBase::MotorMinStoppingDistanceUnits() const
{
	// `CAI_Motor#16` `0x102e1300`, which lives on `IElysiumNpcMotor` itself
	// (`MinStoppingDistanceUnits`). With no motor at all the interface's own floor is the answer.
	return Motor != nullptr ? Motor->MinStoppingDistanceUnits() : 10.0f;
}

bool FElysiumNpcBase::IsIgnoreCollisionEntityTail(const FElysiumEntity* Other) const
{
	// `CBaseAnimating::IsIgnoreCollisionEntity` `0x1008be20`: resolve `m_hIgnoreCollisionEntity`
	// (+0x055c) and compare it against the candidate. Nothing writes the handle in this runtime yet,
	// so the tail answers "not that entity" for everything.
	if (Other == nullptr || !IgnoreCollisionEntity.IsSet() || World == nullptr)
	{
		return false;
	}
	return World->Resolve(IgnoreCollisionEntity) == Other;
}

float FElysiumNpcBase::StepHeight() const
{
	// slot 522. `CAI_BaseNPC::StepHeight` `0x101a6b40` returns `_DAT_10453b94` = 18.0 and IS the
	// body slot 522 carries on the Troika line. `CAI_TestHull::StepHeight` `0x102d72b0` (40.0) and
	// three species override it on their own classes.
	return NpcKernelMotorShared::GStepHeightBase;
}

float FElysiumNpcBase::GetMaxJumpSpeed() const
{
	// slot 523. `CAI_BaseNPC::GetMaxJumpSpeed` `0x101a6b60` returns `_DAT_10453b94` = 18.0 — the
	// SAME cell slot 522's `0x101a6b40` reads. The Troika (`0x101aa670`) and
	// `CAI_TestHull` (`0x102d72d0`) override it on their own classes. Until story 5 fold A1 this was a
	// generated stub answering 0.
	return ElysiumNpcTunables::StepHeightBase;
}

float FElysiumNpcBase::GetJumpGravity() const
{
	// slot 524. `CAI_BaseNPC::GetJumpGravity` `0x101a6b80` returns `_DAT_10477ce8` = 350.0, and no
	// class in the family overrides it. Zero dispatch sites in the closure, but the slot is filled,
	// so the body is not dead.
	return GJumpGravity;
}

bool FElysiumNpcBase::IsJumpLegal(FVector& StartUnits, FVector& ApexUnits, FVector& EndUnits) const
{
	// slot 521. `CAI_BaseNPC::IsJumpLegal` `0x10280880` forwards to the geometry helper with
	// 80.0 / 250.0 / 160.0; `CAI_TestHull::IsJumpLegal` `0x102d7760` is its own class's override
	// with 1024 / 1024 / 1024 (`FElysiumNpcTestHull`).
	return IsJumpLegalGeometry(StartUnits, ApexUnits, EndUnits, NpcKernelMotorShared::GJumpLegalRise,
		NpcKernelMotorShared::GJumpLegalDrop, NpcKernelMotorShared::GJumpLegalDistance);
}

float FElysiumNpcBase::MaxYawSpeedBase()
{
	// `CAI_BaseNPC::MaxYawSpeed` `0x10280bb0` — one constant, `_DAT_1049949c` = 45.0, the same
	// number every other ladder in the family falls through to.
	return NpcKernelMotorShared::GYawDefault;
}

bool FElysiumNpcBase::IsMoving()
{
	// slot 153. `CAI_BaseNPC::FUN_10280300` `0x10280300` is a one-line forward to
	// `thunk_FUN_102ee680(m_pNavigator)`.
	return NavIsGoalActive();
}

bool FElysiumNpcBase::BaseEntityIsMoving(const FElysiumEntity& Entity)
{
	// slot 153's OTHER body. `CAISound::FUN_10026e70` `0x10026e70`, whole:
	//
	//     if (DAT_1070d1b0 == this->m_vecVelocity[0] &&
	//         DAT_1070d1b4 == this->m_vecVelocity[1] &&
	//         DAT_1070d1b8 == this->m_vecVelocity[2]) return 0;
	//     return 1;
	//
	// `DAT_1070d1b0` is `vec3_origin` — family Geometry's standing fact, 371 readers and one writer
	// (the static initialiser `0x101370b0`). The comparison is component-wise and EXACT, not a
	// tolerance, so this is written out rather than as `!Velocity.IsNearlyZero()`: a near-zero
	// velocity answers MOVING in retail and must answer moving here.
	return !(Entity.Velocity.X == 0.0 && Entity.Velocity.Y == 0.0 && Entity.Velocity.Z == 0.0);
}

bool FElysiumNpcBase::OverrideMove(float Interval)
{
	// slot 525. The census holds three species bodies and this leaf dispatches none of them:
	//   * `CNPC_VManBat` `0x1038b120` (`rule`) — the flight step at navigator state 2. UNPORTED.
	//   * `CNPC_VVampireBoss` `0x103c5fe0` and its seven heirs (`present`) — `m_bJumping != 0`
	//     (`+0x6498`, `bJumping`). The word is carried; this arm does NOT read it yet, so a jumping
	//     boss's move is not suppressed here. A named divergence, not a step-5 change.
	//   * `CNPC_Crow` `0x10357ba0` — on a class no map stands; no port arm (0019 story 5 step 1).
	(void)Interval;
	// `CAI_BaseNPC::OverrideMove` `0x1027da90` — a scope-trace push/pop around an unconditional
	// false. The base DECLINES, and that is what every species override is measured against.
	return false;
}

bool FElysiumNpcBase::ValidateNavGoal()
{
	// slot 528. `CAI_BaseNPC::FUN_10280360` `0x10280360` — retail's `IsCoverPosition` check on the
	// goal the navigator is holding:
	//
	//     if (GetNavigator()->GetGoalType() != 6) return true;
	//     if (!GetEnemy()) return true;
	//     Vector goal = GetNavigator()->GetGoalPos();  goal.z = FUN_102f9c70(goal);
	//     Vector eye  = goal + GetViewOffset();        // vtable +0x854
	//     Ray_t  ray  = { eye -> GetEnemy()->EyePosition() (vtable +0x304) };
	//     TraceRay(ray, 0x2804091, filter(this, 0), &tr);
	//     if (tr.fraction == 1.0f) {                   // NOTHING blocks -> this is not cover
	//         if (!ConditionInterruptsCurrentSchedule(0x39)) {
	//             m_failText/-Line = "…AI_BaseNPC…", 0xbc;
	//             TaskFail(0x1b);                      // slot 448, already ported
	//             return false;
	//         }
	//         SetCondition(0x39);
	//     }
	//     return true;
	//
	// **SEAM**: `NavGoalState()` answers -1, so the gate never opens and the body answers true —
	// which is retail's own answer for every goal type but 6. The rest is written out so the arm is
	// here the day the mover carries a goal type.
	if (NavGoalState() != 6)
	{
		return true;
	}
	FElysiumEntity* Enemy = World != nullptr && BaseMemory.Enemy.IsSet()
		? World->Resolve(BaseMemory.Enemy)
		: nullptr;
	if (Enemy == nullptr)
	{
		return true;
	}
	FVector GoalUnits = FVector::ZeroVector;
	if (!NavGoalPosition(GoalUnits))
	{
		return true;
	}
	FKernelHullTrace Trace;
	if (!KernelHullTrace(GoalUnits, NpcKernelMotorShared::SourceOf(Enemy->Origin), FVector::ZeroVector,
		FVector::ZeroVector, GCoverTraceMask, Trace))
	{
		return true;
	}
	if (Trace.Fraction == GTraceClearFraction)
	{
		if (!ElysiumSchedule::MaskHasCondition(Schedule, *this, GCondNavGoalInvalid))
		{
			TaskFail(GFailNoCover);
			return false;
		}
		Cognition.Conditions.Set(GCondNavGoalInvalid);
	}
	return true;
}

bool FElysiumNpcBase::AutoMovement()
{
	// `CAI_BaseNPC::AutoMovement` `0x10280a50`:
	//     vtable[1000/4 = 250]();                                   // first, unconditionally
	//     GetIntervalMovement(m_flAnimTime - m_flPrevAnimTime, …);
	//     if (GetMoveType() != 4) return false;                     // vtable +0x178, slot 94
	//     if (GetFlags() & 0x400) return false;                     // FL_FROZEN
	//     return thunk_FUN_102e0bd0(m_pMotor, delta, thunk_FUN_102729d0(this), yaw, …) == 1;
	//
	// **The gate is the retail contract.** `ElysiumNpc.cpp` already records that this runtime cedes
	// root-motion EXTRACTION to Unreal's animation instance (a visual-only modernization); what it
	// may not cede is WHEN the extraction is allowed to move the body, and that is `GetMoveType()`
	// being 4 with `0x400` clear, after slot 250 has run. Both are ported.
	// Slot 250 is `StudioFrameAdvance(float)` (`0x10098bb0`); retail passes no argument, so the
	// frame advance runs on the animating tier's own clock.
	StudioFrameAdvance(0.f);
	FVector DeltaUnits = FVector::ZeroVector;
	float YawDelta = 0.f;
	// `m_flAnimTime - m_flPrevAnimTime` (+0x174 - +0x170): the animating tier's own interval, which
	// is a CHAIN concern here, so the seam is asked for the interval as well as the delta.
	AnimIntervalMovement(0.f, DeltaUnits, YawDelta);
	if (GetMoveType() != 4)
	{
		return false;
	}
	if ((Flags & 0x400) != 0)
	{
		return false;
	}
	return MotorApplyIntervalMovement(DeltaUnits, YawDelta);
}

void FElysiumNpcBase::PostRun()
{
	// `CAI_BaseNPC::PostRun` `0x1026c7c0`. Everything but two lines is VProf scaffolding; the
	// retail content is the PAIRING and its ORDER:
	//     float dt = thunk_FUN_1026c540(this);        // the elapsed animation interval
	//     vtable[0x408/4 = 258](dt, this);            // DispatchAnimEvents, `0x10098c80`
	//     CBaseCombatCharacter::Weapon_FrameUpdate(dt);   // with the SAME number
	// The port's comment at `ElysiumNpc.cpp:895` discussed this ordering; this is the body.
	//
	// **SEAM**: `thunk_FUN_1026c540`'s interval is the animating tier's, which this substrate does
	// not publish to the kernel, so the pair runs with 0.0 and the ORDER is what is ported.
	const float Interval = 0.f;
	DispatchAnimEvents(Interval, this);
	MotorSeams.PostRunInterval = Interval;
	++MotorSeams.PostRunWeaponUpdates;
}

void FElysiumNpcBase::CheckOnGround()
{
	// `CAI_BaseNPC::CheckOnGround` `0x1026e5e0`, arm for arm.
	//
	//     if (HasCondition(0x73)) {
	//         if (!(GetFlags() & 1) && GetNavType() == 0) return;   // still airborne on the ground
	//         ClearCondition(0x73);                                  // 0x10269b50
	//         return;
	//     }
	//     if (GetNavType() != 0) return;                             // FUN_1027d990
	//     if (GetMoveType() == 7) return;
	//     if (curtime - m_flCheckOnGroundTime <= -0.001) return;     // _DAT_10497530
	//     m_flCheckOnGroundTime = curtime + 0.5;                     // _DAT_104454d0
	//     start = GetAbsOrigin() + (0,0,0.1);  end = GetAbsOrigin() - (0,0,4.0);
	//     TraceHull(start, end, OBBMins, OBBMaxs, 0x202400b, filter, &tr);
	//     if (tr.fraction == 1.0) { SetCondition(0x73); SetGroundEntity(NULL); return; }
	//     if (tr.m_pEnt && tr.m_pEnt != GetGroundEntity()) SetGroundEntity(tr.m_pEnt);
	//
	// `m_flCheckOnGroundTime` is the ONE bound word of this body (`FElysiumNpcBase::CheckOnGroundTime`);
	// the trace is a seam, so the two ground writes are unreachable today and the deadline is still
	// stamped, exactly as retail stamps it before tracing.
	if (Cognition.Conditions.Has(GCondOnGround))
	{
		if ((Flags & 1) == 0 && NavGetType() == 0)
		{
			return;
		}
		Cognition.Conditions.Clear(GCondOnGround);
		return;
	}
	if (NavGetType() != 0)
	{
		return;
	}
	if (GetMoveType() == 7)
	{
		return;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now - CheckOnGroundTime <= GCheckOnGroundSlack)
	{
		return;
	}
	CheckOnGroundTime = Now + GCheckOnGroundInterval;

	const FVector OriginUnits = NpcKernelMotorShared::SourceOf(Origin);
	const FVector StartUnits(OriginUnits.X, OriginUnits.Y, OriginUnits.Z + GCheckOnGroundUp);
	const FVector EndUnits(OriginUnits.X, OriginUnits.Y, OriginUnits.Z - GCheckOnGroundDown);
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	RetailCollisionExtents(*this, Mins, Maxs);
	FKernelHullTrace Trace;
	if (!KernelHullTrace(StartUnits, EndUnits, Mins, Maxs, GGroundTraceMask, Trace))
	{
		return;
	}
	if (Trace.Fraction == GTraceClearFraction)
	{
		Cognition.Conditions.Set(GCondOnGround);
		SetGroundEntity(nullptr);
		return;
	}
	if (Trace.HitEntity.IsSet() && World != nullptr)
	{
		FElysiumEntity* Hit = World->Resolve(Trace.HitEntity);
		if (Hit != nullptr && Hit != GetGroundEntity())
		{
			SetGroundEntity(Hit);
		}
	}
}

bool FElysiumNpcBase::OnObstructingDoorBase(float& InOutMoveGoalMaxDistance, int32 DoorState,
	float DistClear, EObstructingDoorResult& OutResult) const
{
	// `CAI_BaseNPC::FUN_1027dc80` `0x1027dc80`, the BASE branch of slot 531
	// `OnObstructingDoor(AILocalMoveGoal_t*, CBaseDoor*, float distClear, AIMoveResult_t*)`:
	//     if (moveGoal->+0x28 < distClear) return false;      // the door is further than the goal
	//     int state = door->+0x4f8;
	//     if (state != 1 && state != 3) return false;         // only opening / closing obstruct
	//     if (distClear < 0.1) { *result = -1; return true; } // _DAT_104493d0
	//     moveGoal->+0x28 = distClear;
	//     *result = 0;
	//     return true;
	if (InOutMoveGoalMaxDistance < DistClear)
	{
		return false;
	}
	if (DoorState != 1 && DoorState != 3)
	{
		return false;
	}
	if (DistClear < NpcKernelMotorShared::GJumpLegalSlack)
	{
		OutResult = EObstructingDoorResult::Illegal;
		return true;
	}
	InOutMoveGoalMaxDistance = DistClear;
	OutResult = EObstructingDoorResult::Ok;
	return true;
}

int32 FElysiumNpcBase::ResolveLinkActivity() const
{
	// `FUN_1027a6c0` `0x1027a6c0`:
	//     if (thunk_FUN_102ee6a0(m_pNavigator)) {
	//         int a = thunk_FUN_102ee510(m_pNavigator);
	//         if (a != -1) return a;
	//     }
	//     return 1;                                  // ACT_IDLE
	int32 Activity = INDEX_NONE;
	if (NavLinkActivity(Activity) && Activity != INDEX_NONE)
	{
		return Activity;
	}
	return NpcKernelMotorShared::GActIdle;
}

void FElysiumNpcBase::NavOnNavFailed(int32 FailReason)
{
	// `CAI_Navigator::OnNavFailed` `0x102eeae0` (`CAI_Navigator#10`, and `CAI_Navigator#9`
	// `0x102eeb50` is a tail-jump into it):
	//     thunk_FUN_102eeb70(this);                             // the navigator's own reset
	//     owner->+0x1b44 = "E:\Vampire\main\dlls\ai_navigato…";  owner->+0x1b48 = 0x406;
	//     owner->vtable[0x700/4 = 448](reason);                 // TaskFail
	//     SetIdealActivity(owner, FUN_1027a6c0(owner));         // 0x10272650
	//     this->+0x1c = 1;
	//
	// The file/line pair is `ELYSIUM_NPC_WORD_ABSENT(0x1b44)` — the named failure reason and the
	// schedule trace rows carry that account here. `SetIdealActivity` is the Facing family's
	// `SetIdealActivityNumber`, reused rather than duplicated.
	TaskFail(FailReason);
	SetIdealActivityNumber(ResolveLinkActivity());
	Navigator.bNavFailed = true;
}

void FElysiumNpcBase::NavigatorMoveStep()
{
	if (Motor == nullptr || !NavigatorGoalIsActive())                        // 0x102eff7c 0x102ee2e0
	{
		return;
	}
	const EElysiumNpcMoveStatus Status = SampleMotorIntoEntity();
	// Retail's `MoveExecute` keeps the motor's ideal yaw (`+0x34`) at the travel yaw while it walks;
	// this runtime's body orients to its movement, so the travel yaw is the body's own yaw, taken
	// through `UTIL_AngleMod` as the motor stores it (named divergence: the mover's, not the path's).
	MotorIdealYaw = StartTaskAngleMod(static_cast<float>(Angles.Y));
	if (Status == EElysiumNpcMoveStatus::Reached)
	{
		// `OnNavComplete` (`0x102eea90`): `0x102eeb70` (the goal words and the path's reset -- the
		// mover's own clear inside `TaskMovementComplete`), then the owner's `TaskMovementComplete`.
		TaskMovementComplete();                                              // 0x102eccc0 -> 0x10273ec0
		Navigator.bNavFailed = true;                                         // +0x1c = 1
	}
	else if (Status == EElysiumNpcMoveStatus::Failed || Status == EElysiumNpcMoveStatus::Unavailable)
	{
		NavOnNavFailed(0xc);                                                 // 0x102f0180 slot 10 (FAIL_NO_ROUTE, 1)
	}
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::NavGoalPosition(FVector& OutGoalUnits) const
{
	// `thunk_FUN_102ee140(m_pNavigator)` — the navigator's goal point. **SEAM**: the mover keeps no
	// readable goal; the caller is left with its own untouched vector.
	(void)OutGoalUnits;
	return false;
}

bool FElysiumNpcBase::IsJumpLegalGeometry(const FVector& StartUnits, const FVector& ApexUnits,
	const FVector& EndUnits, float MaxRise, float MaxDrop, float MaxDistance)
{
	// `FUN_10280790(start, apex, end, maxRise, maxDrop, maxDistance)`, arm for arm. Every threshold
	// carries the same `_DAT_104493d0 = 0.1` slack except the apex, which is scaled by
	// `_DAT_10460020 = 1.25` instead.
	const float Rise = static_cast<float>(EndUnits.Z - StartUnits.Z);
	if (MaxRise + NpcKernelMotorShared::GJumpLegalSlack < Rise)
	{
		return false;
	}
	const float Drop = static_cast<float>(StartUnits.Z - EndUnits.Z);
	if (MaxDrop + NpcKernelMotorShared::GJumpLegalSlack < Drop)
	{
		return false;
	}
	const float ApexRise = static_cast<float>(ApexUnits.Z - StartUnits.Z);
	if (MaxRise * GJumpApexScale < ApexRise)
	{
		return false;
	}
	// The distance reuses `Drop` for the Z term — squared, so the sign does not matter, and the
	// listing computes it exactly this way.
	const float Distance = FMath::Sqrt(
		static_cast<float>((StartUnits.Y - EndUnits.Y) * (StartUnits.Y - EndUnits.Y))
		+ Drop * Drop
		+ static_cast<float>((StartUnits.X - EndUnits.X) * (StartUnits.X - EndUnits.X)));
	return !(MaxDistance + NpcKernelMotorShared::GJumpLegalSlack < Distance);
}
