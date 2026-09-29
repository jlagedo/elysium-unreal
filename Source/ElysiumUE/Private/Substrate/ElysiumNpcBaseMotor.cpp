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

	// `CAI_Navigator::Move` `0x102eff40` and the pass it dispatches (R3). The failure codes are the
	// table `0x1060fcc4` = `{0, 0x0d, 0x0c, 0x0e, 0x0f}`.
	constexpr float GMoveStepMaxInterval = 1.0f;                  // 0x10449280 (double 1.0)
	constexpr int32 GMoveStepFailNoGoal = 0x0d;                   // table[1], 0x102f0081
	constexpr int32 GMoveStepFailNoRoute = 0x0c;                  // table[2], 0x102f00bc / 0x102f0180
	constexpr int32 GMoveStepFailDoor = 0x0e;                     // table[3], 0x102f08e7
	constexpr int32 GMoveStepClimbNavType = 3;                    // nav+0x18 == 3, 0x102f0198
	constexpr int32 GMoveStepMaxPasses = 0x10;                    // `INC EBP; CMP EBP,0x10; JG`
	constexpr float GMoveStepStaleSeconds = 4.0f;                 // `PUSH 0x40800000`, 0x102f016e
	// `0x102ef510`: the waypoint arrival radius, 0.0625 units (`0x10451f78`); 0.25 (`0x10449260`) under
	// ConVar `npc_vphysics`, whose shipped value `"0"` the port keeps without reading.
	constexpr float GMoveStepArrivalUnits = 0.0625f;
	constexpr float GMoveStepGoalSlackUnits = 0.1f;               // 0x104491b4, 0x102ef760
	constexpr double GMoveStepTimeSlack = ElysiumNpcTunables::MinusThousandthDouble; // 0x10497530
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
	// The port's "a route is being followed" fact, which stands for `IsGoalActive` `0x102ee6a0`
	// (`nav+0x30 != 0 && path+0x24 != 0`, a head waypoint exists) -- NOT for `0x102ee680`, which is
	// `IsGoalSet` (`path+0x5c != 0`, `NavigatorIsGoalSet`), and which `CAI_BaseNPC::IsMoving`
	// (`0x10280300`, slot 153) forwards to. The retail word is `Navigator.IsGoalActive()`; it is set
	// when the goal's request is accepted and cleared by the reset, but the pop at arrival is the
	// navigator's move step (0018 story 5 lane I), so until that lands the mover's own sample is the
	// one that goes false when the walk ends, and this answers it.
	return Motor != nullptr && Motor->SampleNavigation().bActiveGoal;
}

bool FElysiumNpcBase::NavigatorIsGoalSet() const
{
	return Navigator.IsGoalSet();                                        // 0x102ee680 path+0x5c != 0
}

bool FElysiumNpcBase::NavigatorIsPaused() const
{
	return Navigator.IsPaused();                                         // 0x102ee2e0 path+0x10 m_bPaused
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
	// `thunk_FUN_102ee620(m_pNavigator)` -- `GetGoalType()`, `path+0x5c`: 0 none, 1 target, 2 enemy,
	// 3 path corner, 4 location, 6 cover, 7 best-unknown, 8 pedestrian place, 9 animal place.
	// `ValidateNavGoal` requires exactly 6. No goal: 0 (the path constructor's and the reset's).
	return Navigator.GetGoalType();
}

bool FElysiumNpcBase::NavLinkActivity(int32& OutActivity) const
{
	// The pair `FUN_1027a6c0` reads off `m_pNavigator` (+0x5d34): `0x102ee6a0` is
	// `CAI_Navigator::IsGoalActive` (`m_pPath` +0x30 and its current waypoint +0x24 both set,
	// `NavigatorGoalIsActive`), and `0x102ee510` answers the path's movement activity
	// (`0x1030b520(m_pPath)`), which this runtime carries as the navigator's
	// `MovementActivity` (written by `SetGoal`'s activity word, `0x102ee250`; 1 after a reset).
	if (!NavigatorGoalIsActive())                                        // 0x102ee6a0
	{
		return false;
	}
	OutActivity = Navigator.GetMovementActivity();                       // 0x102ee510 path+0x2c
	return true;
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
	// `thunk_FUN_102ee680(m_pNavigator)`, which is `IsGoalSet` (`NavigatorIsGoalSet`, goal type
	// != 0), not `IsGoalActive`. The port answers the mover's "a route is being followed" fact
	// instead: retail's `TaskMovementComplete` ends a walk with `ClearGoal` (`0x102ee270`, the path
	// reset), which the port does not yet route to `NavClearRoute`, so the goal type outlives the
	// walk here and would answer "moving" for a body that has arrived.
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
	// `NavGoalState()` is the navigator's goal type (`path+0x5c`), so the gate opens for a cover
	// goal (`SetGoal` type 6) and answers true for every other type, as retail does.
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
	// `NavGoalPosition` answers port axes in Source units; the trace below is in Source axes (Y
	// negated), the same frame `SourceOf(Enemy->Origin)` is in.
	GoalUnits.Y = -GoalUnits.Y;
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

float FElysiumNpcBase::PostRun()
{
	// `CAI_BaseNPC::PostRun` `0x1026c7c0`. Everything but three lines is VProf scaffolding:
	//     float dt = RunAnimation();                  // `0x1026c8c4 CALL 0x1000ed63` -> 0x1026c540
	//     vtable[0x408/4 = 258](dt, this);            // DispatchAnimEvents, `0x1026c8d8`
	//     CBaseCombatCharacter::Weapon_FrameUpdate(dt);   // `0x1026c8e0`, with the SAME number
	// and the interval is the answer the think hands to `PerformMovement`.
	const float Interval = RunAnimation();                                      // 0x1026c8c4
	DispatchAnimEvents(Interval, this);                                          // 0x1026c8d8 slot 258
	MotorSeams.PostRunInterval = Interval;
	++MotorSeams.PostRunWeaponUpdates;                                           // 0x1026c8e0 Weapon_FrameUpdate
	return Interval;
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
	// `SetIdealActivityNumber`, reused rather than duplicated. The path is NOT cleared: the head
	// waypoint and the goal type stand for the schedule's own reaction to the failure.
	NavResetBlockerMemory();                                                 // 0x102eeb70
	TaskFail(FailReason);                                                    // slot 448 (+0x700)
	SetIdealActivityNumber(ResolveLinkActivity());                           // 0x100097d2(npc, 0x1000b285(npc))
	Navigator.bNavFailed = true;                                             // +0x1c = 1
	Navigator.LastOutcome = FElysiumNpcNavOutcome();
	Navigator.LastOutcome.Kind = FailReason == GMoveStepFailDoor
		? EElysiumNpcNavOutcomeKind::Door : EElysiumNpcNavOutcomeKind::Failed;
	Navigator.LastOutcome.FailCode = FailReason;
}

void FElysiumNpcBase::NavResetBlockerMemory()
{
	// `0x102eeb70`: `nav+0x54 = -1`, `nav+0x58 = nav+0x60 = -1.0f`, then `0x1000b550` (the local
	// navigator's reset; the port's steering is the crowd's, so the call is recorded). `nav+0x51`
	// is not this reset's word.
	Navigator.BlockerEntity = FElysiumEntityHandle::Invalid();
	Navigator.BlockerHoldUntil = -1.0;
	Navigator.BlockerForgetAt = -1.0;
	++NavMoveStep.LocalNavResets;                                            // 0x1000b550, SEAM
}

void FElysiumNpcBase::NavOnNavComplete()
{
	// `CAI_Navigator::OnNavComplete` `0x102eea90` (slot 8): the reset `0x102eeb70`, the owner's
	// `TaskMovementComplete` through `0x102eccc0`, `nav+0x1c = 1`.
	NavResetBlockerMemory();                                                 // 0x102eeb70
	// `TaskMovementComplete` advances the goal waypoint (`0x102f0400`, the last corner's `InPass`) and
	// ends with `ClearGoal` (`0x10273f46` -> `0x102ee270`), so the goal type (`path+0x5c`) and the head
	// waypoint never outlive an arrival.
	TaskMovementComplete();                                                  // 0x102eccc0 -> 0x10273ec0
	Navigator.bNavFailed = true;                                             // +0x1c = 1
	Navigator.LastOutcome = FElysiumNpcNavOutcome();
	Navigator.LastOutcome.Kind = EElysiumNpcNavOutcomeKind::Arrived;
}

FElysiumEntityHandle FElysiumNpcBase::NavMoveTarget() const
{
	// `0x102ecc40`, the move goal's `+0x34` (slot 17's word `[0xd]`, the body `FUN_102eee40` also
	// transcribes): goal type 2 / 1 / 7 -> `GetNavTargetEntity` (`0x102729d0`), else `path+0x30`.
	const int32 GoalType = Navigator.GetGoalType();
	const FElysiumEntity* TargetEntity = nullptr;
	if (GoalType == 2 || GoalType == 1 || GoalType == 7)
	{
		TargetEntity = GetNavTargetEntity();
	}
	else if (World != nullptr)
	{
		TargetEntity = static_cast<const FElysiumEntityWorld*>(World)->Resolve(Navigator.GetTarget());
	}
	return TargetEntity != nullptr ? TargetEntity->Handle : FElysiumEntityHandle::Invalid();
}

bool FElysiumNpcBase::NavIsNpcBlocker(const FElysiumEntityHandle& Blocker) const
{
	// `0x102e2d70`: `-3` iff `blocker[+0x94]` (the cached NPC pointer) is non-null; else `-1` for an
	// ordinary entity, `-2` for the world (R2 §5). The body names only NPC obstructions, so anything
	// else it reports reaches the pass unnamed.
	if (!Blocker.IsSet() || World == nullptr)
	{
		return false;
	}
	const FElysiumEntity* Entity = static_cast<const FElysiumEntityWorld*>(World)->Resolve(Blocker);
	return Entity != nullptr && Entity->AsNpcBase() != nullptr;
}

bool FElysiumNpcBase::NavBlockerHold(const FElysiumEntityHandle& Blocker)
{
	// `0x102ef3e0(nav, trace)` (thunk `0x1000856c`), instruction by instruction (R3):
	//     blocker = trace+0x1c -> +0x94;  none -> return nav+0x51;
	//     if (resolve(nav+0x54) != blocker || curtime - nav+0x60 > -0.001) goto arm;
	//     if (curtime - nav+0x58 <= -0.001) { nav+0x51 = 1; return true; }       // hold
	//     return nav+0x51;
	//   arm (0x102ef49a):
	//     nav+0x51 = 1; nav+0x54 = blocker; nav+0x58 = curtime + nav+0x5c; nav+0x60 = curtime + nav+0x64;
	//     return true;
	if (!NavIsNpcBlocker(Blocker))
	{
		return Navigator.bBlockerHold;
	}
	const FElysiumEntityWorld* ConstWorld = World;
	const double Now = ConstWorld->NowSeconds();
	// `nav+0x54` is resolved through the entity list (`>> 0xd` serial, `& 0x1fff` index): a handle that
	// no longer names a live entity is "not the blocker".
	const FElysiumEntity* Remembered = ConstWorld->Resolve(Navigator.BlockerEntity);
	const FElysiumEntity* Current = ConstWorld->Resolve(Blocker);
	if (Remembered != Current || Now - Navigator.BlockerForgetAt > GMoveStepTimeSlack)
	{
		Navigator.bBlockerHold = true;                                       // nav+0x51
		Navigator.BlockerEntity = Blocker;                                   // nav+0x54
		Navigator.BlockerHoldUntil = Now + Navigator.BlockerHoldSeconds;     // nav+0x58 = curtime + nav+0x5c
		Navigator.BlockerForgetAt = Now + Navigator.BlockerWindowSeconds;    // nav+0x60 = curtime + nav+0x64
		++NavMoveStep.BlockerHoldArms;
		return true;
	}
	if (Now - Navigator.BlockerHoldUntil <= GMoveStepTimeSlack)
	{
		Navigator.bBlockerHold = true;
		return true;
	}
	return Navigator.bBlockerHold;
}

bool FElysiumNpcBase::NavFollowSameDirectionMover(const FElysiumEntityHandle& Blocker)
{
	// `0x102efde0`, reached from S4 `0x102ef0e0` when motor slot 16 (`0x102e1300`) exceeds the
	// clearance: a moving NPC going the same way is followed (result 0, `maxDist = distClear`, flag 2).
	// SEAM answering no: its constants and the S4 gate distance are unrecovered (R3), so the blocked
	// result stands, which is S4's own `distClear < 1.0` arm.
	(void)Blocker;
	++NavMoveStep.MoverFollowTests;
	return false;
}

bool FElysiumNpcBase::NavBlockedStepCompletes()
{
	// `0x102ef760` (sink slot 5, `OnMoveBlocked`, `this = nav+0x10`). The owner's movement sink
	// (`[npc+0x19b0]`, `CAI_DefMovementSink`) is asked first through its slot 5 (`+0x14`): a true
	// answer returns with the result as the sink left it. `CAI_DefMovementSink`'s slot 5 is
	// `0x101a63c0`, `XOR AL,AL; RET 4`, and no port class replaces that secondary table
	// (`CNPC_VZombie 0x103de330` is unread), so the sink answers false.
	constexpr bool bMovementSinkHandled = false;                             // 0x102ef777 CALL [EAX+0x14]
	if (bMovementSinkHandled)                                                // 0x102ef77a / 0x102ef77c JZ
	{
		return false;
	}
	// The stopped activity, unconditionally: `SetIdealActivity(0x1027a6c0())`.
	SetIdealActivity(ResolveLinkActivity());                                 // 0x102ef78a / 0x102ef793
	// `dist(GetOrigin() (slot 220), 0x1030ba30 raw goal)` -- 2-D on ground nav, 3-D otherwise --
	// against `0x102ee1a0` `path+0x28` + 0.1, strictly (`FCOMPP; TEST AH,5; JP`: equal or NaN fails).
	const float U = ElysiumMove::U;
	const FVector Delta = (Navigator.GoalPosCm - Origin) / U;
	const double DistUnits = Navigator.GetNavType() == 0 ? Delta.Size2D() : Delta.Size();
	const double ToleranceUnits = Navigator.GetGoalTolerance() / U + GMoveStepGoalSlackUnits;
	if (DistUnits < ToleranceUnits)
	{
		NavOnNavComplete();                                                  // nav slot 8, *result = 0
		return true;
	}
	return false;
}

void FElysiumNpcBase::NavMarkStaleLink(float Seconds)
{
	// `0x102f1fa0(nav, Seconds, NULL)`: gated inside on `nav+0x50 m_fRememberStaleNodes`, a path, a
	// head, `path+0x44 != -1` and the head's node `wp+0x10 != -1`, it marks the link in `nav+0x2c`
	// (`link+0x64 |= 1`, `link+0x68 = curtime + Seconds`, `link+0 = -1`). SEAM: the port routes on
	// Unreal's navmesh and keeps no link table to mark, so the call is recorded and marks nothing.
	(void)Seconds;
	++NavMoveStep.StaleMarkCalls;
}

bool FElysiumNpcBase::NavSimplifyPathDoorRefused()
{
	// `SimplifyPath(nav, 0)` `0x102f13d0` from the `MoveNormal` gate `0x102efd50` (every 0.5 s,
	// `nav+0x38`): its forward pass runs the door probe `0x102f06e0`, whose NPC slot 531 answer with a
	// non-zero word raises `OnNavFailed(0x0e)` (`0x102f08e7`) and the door notice `0x1027de00`. SEAM
	// answering "no door refused": the door policy behind slot 531 is 0018/7's.
	++NavMoveStep.SimplifyPasses;
	return false;
}

FElysiumNpcBase::FNavStepFacts FElysiumNpcBase::NavSampleStep()
{
	FNavStepFacts Step;
	// The facts first: `Sample` below consumes a terminal status (it stops the body), and the facts
	// are what survive that.
	FElysiumNpcMoveFacts Facts;
	const bool bFacts = Motor->SampleMoveFacts(Facts);
	// `GetOrigin` (slot 220) is the entity record; the body is its source.
	const EElysiumNpcMoveStatus Status = SampleMotorIntoEntity();
	if (bFacts && (Facts.bRequestAlive || Facts.bRequestEnded))
	{
		// `0x102ef510`: the head waypoint against the constant radius, 2-D when `nav+0x18 == 0`,
		// 3-D otherwise; not reached iff `tol < dist` (NaN: not reached). The body's request goes to
		// the head waypoint, so its remaining distance is that test's. NAMED MODERNIZATION: the
		// follower's own Success end (its arrival floor, `AlreadyAtGoal`) also counts as reached --
		// Unreal's path follower lands a body where retail's clamped step did.
		const float U = ElysiumMove::U;
		const double Dist2D = Facts.RemainingDistance2DCm / U;
		const double DistUnits = Navigator.GetNavType() == 0
			? Dist2D
			: FMath::Sqrt(Dist2D * Dist2D + FMath::Square(Facts.RemainingDzCm / U));
		const bool bSucceeded = Facts.bRequestEnded && Facts.ResultCode == EElysiumNpcMoveResultCode::Success;
		Step.bWaypointReached = DistUnits <= GMoveStepArrivalUnits || bSucceeded;
		Step.bGaveUp = Facts.bRequestEnded && !bSucceeded;
		Step.Blocker = Facts.BlockingEntity;
		return Step;
	}
	// A motor that reports no facts (a double, a headless world): its own verdict, read as the facts
	// it stands for -- arrived, or given up with no obstruction named.
	Step.bWaypointReached = Status == EElysiumNpcMoveStatus::Reached;
	Step.bGaveUp = Status == EElysiumNpcMoveStatus::Failed || Status == EElysiumNpcMoveStatus::Unavailable;
	return Step;
}

FElysiumNpcBase::ENavMoveResult FElysiumNpcBase::NavMoveNormalPass(const FNavStepFacts& Step)
{
	// `MoveNormal` `0x102efaa0`. Route types 0 and 2 dispatch here; the port's waypoint move types
	// (jump 1, climb 3) are the body's own traversal, so every pass is this one.
	//
	// The gate `0x102efd50`: the route-type / nav-type checks read the head waypoint's move type
	// (`+0x2c`), which the follower does not report, so they are not ported; then `SimplifyPath(nav,
	// 0)` (result ignored) and `nav+0x51 = 0`. Quirk kept: a door refusal inside the simplify pass
	// raises `OnNavFailed(0x0e)` and the pass goes on (`MoveNormal` does not re-test `nav+0x1c`).
	if (NavSimplifyPathDoorRefused())
	{
		NavOnNavFailed(GMoveStepFailDoor);                                   // 0x102f08e7
	}
	Navigator.bBlockerHold = false;                                          // nav+0x51 = 0

	// Navigator slot 16 `0x102ef510`, before any step is built (`102efb2f`).
	if (Step.bWaypointReached)
	{
		if (Navigator.CurWaypointIsGoal())                                   // 0x102ee660 -> 0x1030bd50
		{
			NavOnNavComplete();                                              // *result = 0
			return ENavMoveResult::Ok;
		}
		// `0x102f0400`, then `*result = 1` whatever it did. A failure it raised (`nav+0x1c` set) ends
		// the loop at its top; it must not be turned into a completion below.
		const bool bHeadStands = NavAdvancePath();
		if (bHeadStands || Navigator.bNavFailed)
		{
			return ENavMoveResult::ChangeType;
		}
		// No head stands after the advance. Retail's pop (`0x1030ba90`) never empties the list: with
		// no next waypoint it prints "ERROR: Force end of route without goal" and flags the last one
		// as the goal, and the loop's next pass completes on it -- the same position, so the same
		// arrival. The port reaches that completion here.
		NavOnNavComplete();
		return ENavMoveResult::Ok;
	}

	// Not reached: `MoveNormal` reads the ideal speed and sets the path's activity (slot 310) before
	// building the step. The port's landed form of that write is the ideal activity from
	// `ResolveLinkActivity` (`0x1027a6c0`: the path's movement activity while a head stands), which
	// is what walks the body in its WALK/RUN clip.
	SetIdealActivity(ResolveLinkActivity());
	// Retail's `MoveExecute` keeps the motor's ideal yaw (`+0x34`) at the travel yaw while it walks;
	// this runtime's body orients to its movement, so the travel yaw is the body's own yaw, taken
	// through `UTIL_AngleMod` as the motor stores it (named divergence: the mover's, not the path's).
	MotorIdealYaw = StartTaskAngleMod(static_cast<float>(Angles.Y));

	// Motor code 4 (`0x102e0bd0`, checked first): the obstruction is the move goal's own target
	// (`goal+0x34`) -> S7 `0x102ef6d0` runs `OnNavComplete`, `*result = 0`. The probe's own
	// `0x102e5d80` already clears a block by that target.
	const FElysiumEntityHandle MoveTargetHandle = NavMoveTarget();
	if (Step.Blocker.IsSet() && MoveTargetHandle.IsSet() && Step.Blocker == MoveTargetHandle)
	{
		NavOnNavComplete();
		return ENavMoveResult::Ok;
	}

	if (!Step.bGaveUp)
	{
		// The step walked (`MoveEnact` -> the motor, `motor+0x30` spent). An NPC named in the way
		// while the request still stands is being steered round: NAMED MODERNIZATION -- Unreal's crowd
		// avoidance stands for the local navigator's steer (`localnav` slot 6 `0x102de110`) and S2's
		// `PrependLocalAvoidance 0x102ede30` (result 1, the detour walked). Retail's order is kept
		// (steer and detour first; the 0.25 s hold only once they fail, i.e. once the follower gives
		// the request up) and so is the outcome: keep walking, nothing fails, no hold is armed.
		return ENavMoveResult::Ok;
	}

	// The body gave the request up. The obstruction's class decides the status (`0x102e2d70`).
	ENavMoveResult Result = ENavMoveResult::BlockedWorld;                    // motor code 3: -1/-2/-4 -> -2
	if (NavIsNpcBlocker(Step.Blocker))
	{
		// S3 `0x102ef350` / S7 on motor code 2: the hold. True -> walk up to the clearance, spend the
		// interval (`goal+0x38 |= 2`), `*result = 0`: nothing fails during the hold.
		if (NavBlockerHold(Step.Blocker))
		{
			return ENavMoveResult::Ok;
		}
		// S4 `0x102ef0e0`: follow a same-direction mover (0), else the NPC status stands.
		if (NavFollowSameDirectionMover(Step.Blocker))
		{
			return ENavMoveResult::Ok;
		}
		Result = ENavMoveResult::BlockedNpc;                                 // motor code 2: -3
	}
	// Every negative result leaving `MoveEnact` passes `0x102ef760` first.
	if (NavBlockedStepCompletes())
	{
		return ENavMoveResult::Ok;
	}
	return Result;
}

void FElysiumNpcBase::NavigatorMoveStep()
{
	// `CAI_Navigator::Move` `0x102eff40` (R3 "Entry gate and order of tests"). The interval is the
	// one `PerformMovement` (`0x1026c120`) was handed; `102eff57`: clamped to 1.0.
	float Interval = MotorSeams.PerformMovementInterval;
	if (Interval > GMoveStepMaxInterval)
	{
		Interval = GMoveStepMaxInterval;
	}
	// `102effab`: `path+0x10 m_bPaused` -> return. No stop, no fail, `nav+0x1c` untouched.
	if (Navigator.IsPaused())                                                // 0x102ee2e0
	{
		return;
	}
	// `102effc2`: NPC slot 525 `OverrideMove(interval)` true -> return. The base `0x1027da90` declines;
	// the ManBat's flight (`0x1038b120`) is the species body that answers true.
	if (OverrideMove(Interval))
	{
		return;
	}
	// `102effd3` / `102f0198`: `m_bShouldMove == 0` -> no move this think and no failure.
	if (!BaseScheduleHost.bShouldMove)                                       // npc+0x1a40
	{
		if (Navigator.GetNavType() == GMoveStepClimbNavType)
		{
			++NavMoveStep.ClimbMotorResets;
			MotorResetToDefault();                                           // motor slot 5 0x102e1110
			NavSetType(0);                                                   // 0x102eeba0
		}
		else
		{
			// `nav+0x18 != -1` -> motor slot 10 `0x102e1440`, the velocity zeroed for this think with
			// the route kept. SEAM: the body integrates on its own tick and `IElysiumNpcMotor` has no
			// velocity stop that keeps the request (`Stop` drops it), so the call is recorded.
			++NavMoveStep.VelocityStops;
		}
		return;
	}
	// `102effe1..102f0069`: the hull / frame stamps on five components and `0x1000f240(path)` with its
	// result dropped -- words the port's navigator does not keep. `102f007b`: `motor+0x30 = interval`,
	// the budget the pass loop below stands for.
	if (!Navigator.IsGoalSet())                                              // 102f0081 path+0x5c == 0
	{
		++NavMoveStep.NoRouteWarnings;
		EmitDevMsg(TEXT("AIError: Move requested with no route!\n"),
			TEXT("AIError: Move requested with no route!\n"));              // retail's Warning()
		NavOnNavFailed(GMoveStepFailNoGoal);
		return;
	}
	if (!Navigator.IsGoalActive())                                           // 102f00bc head waypoint == 0
	{
		NavOnNavFailed(GMoveStepFailNoRoute);                                // no warning, no stale mark
		return;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now < BaseScheduleHost.MoveWaitFinished)                             // 102f00cd 0x10280a20 npc+0x5cf0
	{
		return;
	}
	if (Motor == nullptr)
	{
		return;                                                              // port: no body to sample
	}

	// The loop (`102f00e9..102f0165`): result seeded 1, `nav+0x1c = 0`, pass counter 0.
	Navigator.bNavFailed = false;
	NavMoveStep.Passes = 0;
	ENavMoveResult Result = ENavMoveResult::ChangeType;
	FElysiumEntityHandle LastBlocker;
	bool bBudgetSpent = false;
	for (;;)
	{
		// Loop top: the latch exits (a failure result goes on to the tail); a spent budget exits.
		if (Navigator.bNavFailed || bBudgetSpent)
		{
			if (static_cast<int32>(Result) >= 0)
			{
				return;
			}
			break;
		}
		const FNavStepFacts Step = NavSampleStep();
		LastBlocker = Step.Blocker;
		Result = NavMoveNormalPass(Step);
		// `INC EBP; CMP EBP,0x10; JG`: the 17th dispatch fails whatever it answered.
		if (++NavMoveStep.Passes > GMoveStepMaxPasses)
		{
			++NavMoveStep.PassCapErrors;
			EmitDevMsg(TEXT("ERROR: AI navigation not terminating. Possibly bad cyclical solving?"),
				TEXT("ERROR: AI navigation not terminating. Possibly bad cyclical solving?"));
			NavMarkStaleLink(GMoveStepStaleSeconds);                         // 102f016e
			NavOnNavFailed(GMoveStepFailNoRoute);                            // 102f0180
			return;
		}
		if (static_cast<int32>(Result) < 0)
		{
			break;
		}
		// 0 spends the think's budget (the motor zeroes `motor+0x30`, or the step walked it); 1 re-enters
		// with what is left.
		bBudgetSpent = Result == ENavMoveResult::Ok;
	}
	// The failure tail `102f0169`: `CMP EAX,-3; JZ` skips the stale mark.
	if (Result != ENavMoveResult::BlockedNpc)
	{
		NavMarkStaleLink(GMoveStepStaleSeconds);                             // 0x102f1fa0(nav, 4.0, NULL)
	}
	NavOnNavFailed(GMoveStepFailNoRoute);                                    // 102f0180 nav slot 10
	if (Result == ENavMoveResult::BlockedNpc)
	{
		Navigator.LastOutcome.Kind = EElysiumNpcNavOutcomeKind::NpcBlocked;
		Navigator.LastOutcome.Blocker = LastBlocker;
	}
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::NavGoalPosition(FVector& OutGoalUnits) const
{
	// `thunk_FUN_102ee140(m_pNavigator)` -- `ActualGoalPosition`: `path+0x4c` minus `path+0x34`. Retail
	// has no "none" answer (the reset leaves `(0,0,0)`), so this always writes and answers true; the
	// bool is the port's old seam shape, kept for the callers that seed a fallback. Source units, in
	// the port's axes (no Y reflection): the frame `Origin / U` is in.
	OutGoalUnits = Navigator.GetGoalPos() / ElysiumMove::U;
	return true;
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
