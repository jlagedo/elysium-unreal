#include "Substrate/ElysiumNpc.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **BaseHelpers** — `CAI_BaseNPC`'s own unnamed layer 0–9 bodies.
//
// 40 rows: the fifteen Troika-line vtable slots whose stubs the generator left for this family, and
// the twenty-five helpers, free functions and branch overrides beside them. The walked prose is
// spread by concern, as the brief asks: `docs/vtmb/npc-ai/shape.md` for the `CAI_BaseActor` branch
// and the geometry helpers, `conditions-and-states.md` for the attack-condition ladder and the
// victim-side reaction slots, `schedule-kernel.md` for the hint validators and the turn ladder,
// `lifecycle.md` for the think-clock forwards.
//
// THE TWO STANDING FACTS OF THIS FAMILY:
//
//   1. **Five rows are `CAI_BaseHumanoid`'s, not the Troika line's.** `classes.md` gives
//      `CAI_BaseHumanoid` no entity classname, so no map stands one and `RetailClass()` never
//      answers it. They are ported because they are rows; they read `CAI_BaseActor`'s own words,
//      declared in the `.inl`, and nothing on the Troika line dispatches through them.
//   2. **Three rows are already carried by the port and are NOT re-stood here.** `0x10272790`
//      (`ShouldMaintainActivity`'s base arm) is family Anim's `FElysiumNpc::ShouldMaintainActivity`;
//      `0x10298910` and `0x102989e0` are `FElysiumNpc::ParseGroupMask` and its two setters. Adding
//      a second copy of a rule the port already reproduces arm for arm is the drift this story
//      exists to end.

namespace
{
	// --- The image constants these bodies read -------------------------------------------------
	//
	// One named constant per `_DAT_` the decompiled C reads, with the address it is read from.
	// `vampire.dll`'s `.rdata` values are NOT in the corpus (it exposes referrers, not bytes), so a
	// value is recovered only where an oracle document pins that address or where the SDK 2013 twin
	// of the same arm states it. Anything else says UNRECOVERED and the arm that reads it says what
	// it does instead.

	// The band both melee-condition bodies share with the two ranged ones.
	constexpr float GDatAttackBandUnits = ElysiumNpcTunables::SixtyFour;
	// `FCOMP double ptr [0x104492d0]` — a DOUBLE, and SDK 2013's `MeleeAttack1Conditions` reads the
	// same arm as `if (flDot < 0.7)`. Recovered from the twin, not from the bytes.
	constexpr double GDatMeleeDotMin = 0.7;                  // _DAT_104492d0, as a double
	constexpr float GDatMelee2TooFarUnits = ElysiumNpcTunables::OneEighty;
	// UNRECOVERED. `MeleeAttack1Conditions`'s outer band must exceed 64 for the `0x60` rung below
	// it to be reachable at all, and nothing pins it. Standing it at +inf makes the `0x09` arm
	// UNREACHABLE, which is a stated refusal rather than a guessed threshold; every other arm of
	// the body is exact.
	constexpr float GDatMelee1TooFarUnits = TNumericLimits<float>::Max();  // _DAT_1044ddb0
	// SDK 2013 `SetDefaultEyeOffset`: `m_vDefaultEyeOffset *= 0.75`.
	constexpr float GDatEyeOffsetFallbackScale = 0.75f;      // _DAT_104629b8
	// `docs/vtmb/npc-ai/conditions-and-states.md` (line 1010) and `entity_io.md` pin
	// `_DAT_10449258 = 3.0f` — the unreachable record's retention.
	constexpr float GDatUnreachableSeconds = 3.0f;           // _DAT_10449258
	constexpr float GDatOne = ElysiumNpcTunables::One;
	constexpr float GDatZero = ElysiumNpcTunables::Zero;
	// The clear-trace fraction both LOS arms compare against: a DOUBLE cell, compared as a float.
	constexpr float GDatClearFraction = static_cast<float>(ElysiumNpcTunables::OneDouble);
	constexpr float GDatNearDistanceUnits = ElysiumNpcTunables::FiveHundredTwelve;
	constexpr float GDatFaceAnimTurnYaw = ElysiumNpcTunables::Forty;
	// Family Facing recovered the Troika turn ladder's own pair, and the top rung of `0x10297a20`
	// reads the SAME two addresses: `_DAT_1049ae3c = -140.0f`, `_DAT_1049ae38 = 140.0f`.
	constexpr float GDatFaceAnimYawLow = -140.0f;            // _DAT_1049ae3c
	constexpr float GDatFaceAnimYawHigh = 140.0f;            // _DAT_1049ae38
	// The retail string of the arm that reads `_DAT_10451ab4` is
	// `"Projection (%.2f) < 0.2"` — the literal is in the message.
	constexpr float GDatProjectionMin = 0.2f;                // _DAT_10451ab4

	// UNRECOVERED literals. Each is named so the arm reads as retail's and the value is the one
	// thing waiting; each site says what the stand-in does.
	constexpr float GDatFollowRunDistanceUnits = 0.f;        // _DAT_1049a17c — slot 571's walk/run
	// NO LONGER UNRECOVERED. Story 29d, family **Anim10** read `_DAT_10497ca0` out of the pinned
	// image while porting `CAI_BaseHumanoid::MaintainEyeDirection` (`0x1025fa50`), whose
	// `1025fda5 FCOMP double ptr [0x10497ca0]` settles both its width and its value: it is a
	// **double** and it reads **-0.5**. So the head-target cone is 120 degrees off the head
	// direction, not the forward half-plane the 0.0 stand-in made it. `0x1025ea00` reads the same
	// cell and its gate is still STRICT, so a target dead behind (dot -1) is still refused.
	constexpr double GDatValidHeadTargetDotMin = -0.5;       // _DAT_10497ca0 — ValidHeadTarget

	// Read 2026-09-21 (`docs/vtmb/npc-ai/rdata-cells.md`) and held by the tunables table since
	// 0019/4; each stood at a 0.0 stand-in before. The height limit is `FCOMP double ptr`: a DOUBLE.
	constexpr double GDatHintHeightDiffUnits = ElysiumNpcTunables::SixtyFourDouble;  // 0x10296c40
	constexpr float GDatFaceAnimYawMid = ElysiumNpcTunables::FaceTurnSecondEdge;   // 0x10297a20's rung 2
	constexpr float GDatFaceAnimRandomScale = ElysiumNpcTunables::AngleQuantum;    // 0x10297a20's draw
	constexpr float GDatDistanceEpsilon = ElysiumNpcTunables::FloatEpsilon;        // the 1/(d+eps) guard
	constexpr float GDatCoverForwardMin = ElysiumNpcTunables::Half;                // 0x10295ed0's 0x283d arm

	// `CAI_Hint::m_nHintType` 0x283d — `0x10295ed0`'s one type-specific extra projection test.
	constexpr int32 GHintTypeCoverForward = 0x283d;

	// The retail `Activity` numbers this family's bodies name. Spelled here because the port has no
	// retail activity table (family Hints' `RestartIdealActivityId` says why).
	constexpr int32 GBaseHelpersActIdle = 1;                // ACT_IDLE — the tail of the face-anim ladder
	constexpr int32 GBaseHelpersActWalk = 9;                // ACT_WALK
	constexpr int32 GBaseHelpersActRun = 0x13;              // ACT_RUN
	constexpr int32 GBaseHelpersActScriptCustomMove = 0x18; // ACT_SCRIPT_CUSTOM_MOVE
	constexpr int32 GBaseHelpersActDisposition = 0xf1;      // ACT_DISPOSITION, slot 588's restart

	// `0x10297a20`'s four turn programs, in ladder order.
	constexpr int32 GFaceAnimAct180 = 0x10ff;
	constexpr int32 GFaceAnimAct90 = 0x10fd;
	constexpr int32 GFaceAnimAct45 = 0x10fa;
	constexpr int32 GFaceAnimActSmall = 0x10f8;

	// `CBaseEntity::GetFlags()` bit 0.
	constexpr uint32 GFlOnGround = 1;

	// `m_iszCustomMove` sits at `+0x5f50` on the cine. There is no port field for it, so
	// `GetScriptCustomMoveActivity` reads this empty key and says so.
	const FString GUnrecoveredCustomMove;
}

// =================================================================================================
// `CAI_BaseHumanoid` / `CAI_BaseActor`'s branch. Five rows; no map stands the class.
// =================================================================================================

// 0x1025e780 `CAI_BaseHumanoid::vfunc277`, the slot-277 `SetViewtarget` override
void FElysiumNpc::FUN_1025e780(const FVector& ViewTarget)
{
	// The whole 19-byte body: clear bit 0 of `m_fLatchedPositions` (`+0x5f4c`), then chain the base
	// `CBaseFlex::SetViewtarget` (`0x100b5b00`, the body the generated slot-277 virtual carries).
	LatchedPositions &= ~1;
	SetViewtarget(ViewTarget);
}

// 0x1025f1a0 `CAI_BaseActor::HasActiveLookTargets`, `CAI_BaseHumanoid#586`
bool FElysiumNpc::HasActiveLookTargets() const
{
	// `return *(int *)(this + 0x5f94) != 0;` — the look-queue `CUtlVector`'s COUNT, whose vector
	// base is `+0x5f88` with 0x24-byte elements. Family Facing already carries that list as
	// `LookTargets` (`AddLookTargetHumanoid`, slots 535/536), so this asks it.
	return LookTargets.Num() != 0;
}

// 0x1025ea00 `CAI_BaseActor::ValidHeadTarget(const Vector&)`, `CAI_BaseHumanoid#588`
bool FElysiumNpc::ValidHeadTargetBaseActor(const FVector& LookTargetPosCm) const
{
	// Arm for arm:
	//   vFacing = HeadDirection3D()            (vtable +0x5c4)
	//   dir     = lookTargetPos - EyePosition() (vtable +0x304), normalised
	//   if (dot(vFacing, dir) < _DAT_10497ca0)            return false
	//   return ABS(lookTargetPos.z - eye.z) < _DAT_104492d0
	//
	// Retail's dot gate is `>` and not `>=`: the decompiler renders it as
	// `if (fVar1 >= fVar2 && (fVar1 == fVar2) == 0)`, which admits only a strictly greater dot.
	const FVector Eye = EyePosition();
	FVector Dir = LookTargetPosCm - Eye;
	// `PTR_thunk_FUN_10137220` is `VectorNormalize`; a zero-length direction leaves the vector
	// alone in retail, which lands on a zero dot.
	Dir.Normalize();
	// SEAM: `HeadDirection3D` (slot 369, vtable `+0x5c4`) has no port body — the gaze cascade
	// publishes a look POINT, not a head basis. `ViewForward` is the observer's own forward and is
	// the nearest recovered axis; the head's own deflection from it is UNRECOVERED.
	const FVector Facing = FElysiumNpcSenses::ViewForward(*this);
	const double Dot = FVector::DotProduct(Facing, Dir);
	// `_DAT_10497ca0` = **-0.5**, a double, recovered 2026-09-14 by story 29d (family Anim10) out of
	// the pinned image at `0x1025fa50`'s `FCOMP double ptr` — the gate admits anything within 120
	// degrees of the head direction, and still refuses a target dead behind.
	if (!(Dot > GDatValidHeadTargetDotMin))
	{
		return false;
	}
	// The height limit reads the same address as the melee dot minimum and the decompiler flags the
	// overlap; the value is UNRECOVERED as a height and the port carries the double it pins.
	const double HeightCm = static_cast<double>(GDatMeleeDotMin) * ElysiumMove::U;
	return FMath::Abs(LookTargetPosCm.Z - Eye.Z) < HeightCm;
}

// 0x10260540 `CAI_BaseActor::SelectRandomExpressionForState(NPC_STATE)`, `CAI_BaseHumanoid#589`
const FString* FElysiumNpc::SelectRandomExpressionForState(int32 NpcState) const
{
	// `if (m_iszExpressionOverride == NULL || state == 7) { switch … } else return override`.
	// State 7 is `NPC_STATE_DEAD`: a dead body takes the per-state table even with an override set.
	if (!ExpressionOverride.IsEmpty() && NpcState != 7)
	{
		return &ExpressionOverride;
	}
	// The switch, exactly as the decompiled C reads it. NOTE the pairing: retail answers `+0x5fb0`
	// for state 2 and `+0x5fac` for state 3, which is the REVERSE of SDK 2013's alert/combat field
	// order — the bodies are what this follows.
	const FString* Answer = nullptr;
	switch (NpcState)
	{
		case 1:   Answer = &IdleExpression;   break;   // NPC_STATE_IDLE   -> +0x5fa8
		case 2:   Answer = &AlertExpression;  break;   // NPC_STATE_ALERT  -> +0x5fb0
		case 3:   Answer = &CombatExpression; break;   // NPC_STATE_COMBAT -> +0x5fac
		case 5:
		case 7:   Answer = &DeathExpression;  break;   // PLAYDEAD / DEAD  -> +0x5fb4
		default:  return nullptr;
	}
	// A state whose word is the `string_t` null answers NULL and falls out of the switch; a word
	// that is set answers the string, and retail's `STRING()` maps a null one to `DAT_106b8540`,
	// the shared empty string. Both distinctions are kept by answering a pointer.
	return Answer->IsEmpty() ? nullptr : Answer;
}

// 0x10260670 `CAI_BaseActor::SetExpression(const char*)`
void FElysiumNpc::SetExpression(const FString& SceneName)
{
	// 1. a null or empty name clears and returns;
	// 2. a name equal to `m_iszExpressionScene` under `__strcmpi` is a no-op;
	// 3. otherwise `m_iszExpressionScene = NULL`, `InstancedScriptedScene(this, name)`
	//    (`thunk_FUN_10084b40`) into `m_hExpressionSceneEnt` (`+0x5fa0`), and the pooled string is
	//    stored ONLY when the returned handle resolves to a live entity.
	if (SceneName.IsEmpty())
	{
		ClearExpression();
		return;
	}
	if (!ExpressionScene.IsEmpty() && ExpressionScene.Equals(SceneName, ESearchCase::IgnoreCase))
	{
		return;
	}
	ExpressionScene.Reset();
	// SEAM for `InstancedScriptedScene` (`0x10084b40`): this runtime has no instanced-scene spawner
	// for an expression `.vcd`, so the handle stays unset and step 3's cache is never taken — which
	// is retail's own dead-handle arm.
	ExpressionSceneEnt = FElysiumEntityHandle();
	if (World != nullptr && ExpressionSceneEnt.IsSet()
		&& World->Resolve(ExpressionSceneEnt) != nullptr)
	{
		ExpressionScene = SceneName;
	}
}

// 0x10260750 `CAI_BaseActor::ClearExpression`
void FElysiumNpc::ClearExpression()
{
	// The whole 11-byte body. SDK 2013's `ClearExpression` also removes the actor from its scene;
	// retail's does NOT — it writes the one word and returns, and the scene entity at `+0x5fa0` is
	// deliberately left standing.
	ExpressionScene.Reset();
}

// =================================================================================================
// `CAI_BaseNPC`'s own helpers.
// =================================================================================================

// slot 555 0x1026d9a0 `int MeleeAttack1Conditions(float, float)`
int32 FElysiumNpc::MeleeAttack1Conditions(float Dot, float Dist)
{
	// Arm for arm, and NOTE that `GetEnemy()` (slot 167, vtable `+0x29c`) is dispatched THREE
	// times: once up front for the combat-character cache, once as a null gate after the dot, and
	// once more for the ground-flag read. Retail re-reads it each time.
	const FElysiumEntity* Enemy = (World != nullptr && Senses.Memory.Enemy.IsSet())
		? World->Resolve(Senses.Memory.Enemy) : nullptr;
	// `enemy->+0x9c` is `CBaseEntity`'s self-downcast cache: non-null exactly for a combat
	// character.
	const FElysiumCombatCharacter* EnemyCombatant =
		Enemy != nullptr ? Enemy->AsCombatCharacter() : nullptr;

	// `_DAT_1044ddb0` is UNRECOVERED, so this arm is stated unreachable rather than guessed. The
	// rung IS recovered: past the outer band the answer is `COND_TOO_FAR_FOR_MELEE`.
	if (Dist > GDatMelee1TooFarUnits)
	{
		return static_cast<int32>(EElysiumNpcCond::TooFarForMelee);   // 9
	}
	if (Dist > GDatAttackBandUnits)
	{
		return static_cast<int32>(EElysiumNpcCond::TooFarToAttack);   // 0x60
	}
	if (static_cast<double>(Dot) < GDatMeleeDotMin)
	{
		return static_cast<int32>(EElysiumNpcCond::None);
	}
	if (Enemy == nullptr)
	{
		return static_cast<int32>(EElysiumNpcCond::None);
	}
	if (EnemyCombatant != nullptr)
	{
		// Slot 327 (vtable `+0x51c`) on the ENEMY's combat character; the base body `0x10345460`
		// is `return 1`. A non-NPC combat character (the player) has no port body for the slot,
		// which is the same answer.
		const FElysiumNpc* EnemyNpc = Enemy->AsNpc();
		if (EnemyNpc != nullptr && !const_cast<FElysiumNpc*>(EnemyNpc)->Slot327())
		{
			return static_cast<int32>(EElysiumNpcCond::None);
		}
	}
	// `GetFlags() & FL_ONGROUND` — the answer is `COND_CAN_MELEE_ATTACK1` only for a grounded
	// enemy, and 0 otherwise. Retail computes it branchlessly (`-(flags & 1) & 0x51`).
	return (Enemy->Flags & GFlOnGround) != 0
		? static_cast<int32>(EElysiumNpcCond::CanMeleeAttack1) : 0;
}

// slot 556 0x1026da90 `int MeleeAttack2Conditions(float, float)`
int32 FElysiumNpc::MeleeAttack2Conditions(float Dot, float Dist)
{
	// The sibling, and the three differences are the whole of it: a DIFFERENT outer band
	// (`_DAT_1044c3a8`, 180 units), NO second `GetEnemy()` null gate, and NO ground test — a
	// passing body answers `COND_CAN_MELEE_ATTACK2` outright.
	const FElysiumEntity* Enemy = (World != nullptr && Senses.Memory.Enemy.IsSet())
		? World->Resolve(Senses.Memory.Enemy) : nullptr;
	const FElysiumCombatCharacter* EnemyCombatant =
		Enemy != nullptr ? Enemy->AsCombatCharacter() : nullptr;

	if (Dist > GDatMelee2TooFarUnits)
	{
		return static_cast<int32>(EElysiumNpcCond::TooFarForMelee);   // 9
	}
	if (Dist > GDatAttackBandUnits)
	{
		return static_cast<int32>(EElysiumNpcCond::TooFarToAttack);   // 0x60
	}
	if (static_cast<double>(Dot) < GDatMeleeDotMin)
	{
		return static_cast<int32>(EElysiumNpcCond::None);
	}
	if (EnemyCombatant != nullptr)
	{
		const FElysiumNpc* EnemyNpc = Enemy->AsNpc();
		if (EnemyNpc != nullptr && !const_cast<FElysiumNpc*>(EnemyNpc)->Slot327())
		{
			return static_cast<int32>(EElysiumNpcCond::None);
		}
	}
	return static_cast<int32>(EElysiumNpcCond::CanMeleeAttack2);      // 0x52
}

// 0x102729d0 `CAI_BaseNPC::GetNavTargetEntity`
FElysiumEntity* FElysiumNpc::GetNavTargetEntity() const
{
	// `GetGoalType()` (`thunk_FUN_102ee620(m_pNavigator)`), re-read for EVERY arm:
	//   2 `GOALTYPE_ENEMY`     -> m_hEnemy      (+0x5ce0)
	//   1 `GOALTYPE_TARGETENT` -> m_hTargetEnt  (+0x5ce4)
	//   7 `GOALTYPE_COVER`     -> the handle behind `(this+0x98)->vtable+0x928`
	//   anything else          -> NULL
	// and every arm then resolves the handle through the global entity table, answering NULL for a
	// stale one. `NavGoalState()` is family Motor's seam for the goal-type read and answers -1, so
	// the default arm is what this takes today.
	if (World == nullptr)
	{
		return nullptr;
	}
	FElysiumEntityWorld* MutableWorld = const_cast<FElysiumEntityWorld*>(World);
	const int32 GoalType = NavGoalState();
	if (GoalType == 2)
	{
		return MutableWorld->Resolve(Senses.Memory.Enemy);
	}
	if (GoalType == 1)
	{
		return MutableWorld->Resolve(TargetEnt);
	}
	if (GoalType == 7)
	{
		// SEAM: `+0x98` is `CBaseEntity`'s self-downcast cache and `+0x928` the cover-goal query on
		// it. No port object answers it; the arm resolves nothing, which is retail's stale-handle
		// answer.
		return nullptr;
	}
	return nullptr;
}

// 0x10274080 `CAI_BaseNPC::RememberUnreachable`
void FElysiumNpc::RememberUnreachable(FElysiumEntity* Entity)
{
	// The scan is BACKWARD, from `m_UnreachableEnts.Count() - 1`, and stops at the FIRST match; a
	// hit refreshes the expiry and falls through to the position write without touching the handle.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const double Expiry = Now + static_cast<double>(GDatUnreachableSeconds);
	for (int32 Index = UnreachableEnts.Num() - 1; Index >= 0; --Index)
	{
		FElysiumEntity* Recorded = (World != nullptr)
			? const_cast<FElysiumEntityWorld*>(World)->Resolve(UnreachableEnts[Index].Entity)
			: nullptr;
		if (Recorded == Entity)
		{
			UnreachableEnts[Index].ExpiresAt = Expiry;
			// Retail writes the position for a null `param_1` too — and faults doing it. The port
			// refuses instead (NAMED DIVERGENCE: a crash is not a behaviour to reproduce).
			if (Entity != nullptr)
			{
				UnreachableEnts[Index].PositionCm = Entity->Origin;
			}
			return;
		}
	}
	// The append. Retail grows the vector, writes `-1` into the new record's handle word TWICE
	// (once by the grow helper, once by the null arm) and then overwrites it with the entity's own
	// `GetRefEHandle()` (`vtable +0x4`) when there is one.
	FUnreachableEntity& Record = UnreachableEnts.AddDefaulted_GetRef();
	if (Entity != nullptr)
	{
		Record.Entity = Entity->Handle;
		Record.PositionCm = Entity->Origin;   // `GetAbsOrigin()`, slot 217 / vtable +0x364
	}
	Record.ExpiresAt = Expiry;
}

// slot 515 0x10274b30 `float CalcIdealYaw(const Vector&)`
float FElysiumNpc::CalcIdealYaw(const FVector& TargetPos)
{
	// The navigator's state word (`thunk_FUN_102ee3f0(m_pNavigator)`) picks how the delta is built,
	// and the answer is always `VecToYaw(delta)` (`thunk_FUN_101d2c70`), which reads X and Y only:
	//
	//   0x37 -> ( -p.y - origin.x , p.x - origin.y )
	//   0x38 -> (  p.y - origin.x , p.x - origin.y )
	//   else -> (  p.x - origin.x , p.y - origin.y )
	//
	// The Z term of the two special arms is built from an UNINITIALISED stack slot in retail; it is
	// harmless because `VecToYaw` never reads Z, and the port simply does not build one.
	//
	// `GetOrigin()` is slot 220 (vtable `+0x370`), the raw `m_vecOrigin`, not `GetAbsOrigin`.
	// `NavGoalState()` is family Motor's seam and answers -1, so the default arm is what runs.
	const int32 NavState = NavGoalState();
	double Dx = 0.0;
	double Dy = 0.0;
	if (NavState == 0x37)
	{
		Dx = -TargetPos.Y - Origin.X;
		Dy = TargetPos.X - Origin.Y;
	}
	else if (NavState == 0x38)
	{
		Dx = TargetPos.Y - Origin.X;
		Dy = TargetPos.X - Origin.Y;
	}
	else
	{
		Dx = TargetPos.X - Origin.X;
		Dy = TargetPos.Y - Origin.Y;
	}
	// `VecToYaw`: `atan2(y, x)` in degrees, and zero for a zero-length 2-D vector.
	if (Dx == 0.0 && Dy == 0.0)
	{
		return 0.f;
	}
	return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Dy, Dx)));
}

// 0x10274ca0 `CAI_BaseNPC::SetDefaultEyeOffset`
void FElysiumNpc::SetDefaultEyeOffset()
{
	// `GetEyePosition(GetModelPtr(), m_vDefaultEyeOffset)`; if the result is the unset sentinel
	// `vec3_origin` (`DAT_1070d1b0/b4/b8`), DevMsg and fall back to
	// `(WorldAlignMins() + WorldAlignMaxs()) * 0.75`; then `SetViewOffset(m_vDefaultEyeOffset)`.
	//
	// Retail DevMsgs UNCONDITIONALLY where SDK 2013 gates the message on `Classify() != CLASS_NONE`;
	// the gate is not in the body and is not reproduced.
	//
	// SEAM: no `.qc` eye-offset channel reaches this runtime — the character bake publishes an eye
	// point through the visual layer, not a model-space offset word — so the read always lands on
	// the sentinel and the fallback arm is the one every call takes. `+0x5d60` is `_CHAIN` in the
	// shape map ("no stored view offset; the eye point is the chain's virtual `EyePosition()`"), so
	// there is nothing to write either; what is recovered and stated here is the WARNING and the
	// formula.
	FVector EyeOffset = FVector::ZeroVector;   // GetEyePosition answers the sentinel
	if (EyeOffset.IsNearlyZero(0.0))
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("WARNING: %s has no eye offset in .qc!"),
			Class != nullptr ? *Class->ClassName.ToString() : TEXT(""));
		FVector MinsUnits = FVector::ZeroVector;
		FVector MaxsUnits = FVector::ZeroVector;
		// `m_Collision`'s vtable `+4` / `+8` — family Motor's seam for the same pair, which answers
		// false and leaves both at zero.
		RetailCollisionExtents(*this, MinsUnits, MaxsUnits);
		EyeOffset = (MinsUnits + MaxsUnits) * GDatEyeOffsetFallbackScale;
	}
	// `thunk_FUN_1009f380` is `SetViewOffset`. The chain carries no view-offset word (`+0x5d60`
	// `_CHAIN`), so the write has nowhere to land and is recorded rather than made.
	(void)EyeOffset;
}

// slot 529 0x10280330 `bool IsCurTaskContinuousMove()`
bool FElysiumNpc::IsCurTaskContinuousMove()
{
	// `GetCurTask()` (`0x1028a150`), then: NO task answers TRUE, and a task answers true only for
	// ids 0x6e, 0x0b and 0x72. Everything else is false. Retail spells it as a single negated
	// conjunction and the port keeps the shape.
	if (!Schedule.IsRunning())
	{
		return true;
	}
	int32 Task = INDEX_NONE;
	if (!CurrentRetailTaskNumber(Task))
	{
		// A running task the seam cannot number. Retail's answer for a non-null task that is not
		// one of the three is FALSE, and that is what an unnumbered task gets here.
		return false;
	}
	return Task == 0x6e || Task == 0x0b || Task == 0x72;
}

bool FElysiumNpc::CurrentRetailTaskNumber(int32& OutTaskNumber) const
{
	// SEAM for `GetCurTask()->iTask` (`0x1028a150`, the `Task_t` at `+0x00`). This runtime's task
	// vocabulary is `EElysiumTask`, a 30-odd identity subset of retail's 441-entry library with no
	// registered numbers — `ElysiumSchedule.h` names the retail id in a COMMENT beside a few tasks
	// and nowhere in the data. So a running task cannot be numbered and this answers false.
	OutTaskNumber = INDEX_NONE;
	return false;
}

// 0x10289fe0 `CAI_BaseNPC::GetScriptCustomMoveActivity`
int32 FElysiumNpc::GetScriptCustomMoveActivity() const
{
	// `ACT_WALK` unless `m_hCine` (`+0x5d74`) resolves AND its `m_iszCustomMove` (`+0x5f50`) is
	// set; then `LookupActivity(name)`, and on a miss `LookupSequence(name)` -> `ACT_SCRIPT_CUSTOM_
	// MOVE` when the sequence exists, `ACT_WALK` when it does not. Retail re-resolves the handle at
	// every one of the four reads.
	if (World == nullptr || !ScriptOwner.IsSet()
		|| const_cast<FElysiumEntityWorld*>(World)->Resolve(ScriptOwner) == nullptr)
	{
		return GBaseHelpersActWalk;
	}
	// SEAM: `m_iszCustomMove` (`+0x5f50` on the cine) has no port field — `FElysiumAiScriptedSchedule`
	// carries the beat's named clips, not the scripted-sequence custom move — so the key reads
	// empty and the body takes its own `ACT_WALK` arm.
	const FString& CustomMove = GUnrecoveredCustomMove;
	if (CustomMove.IsEmpty())
	{
		return GBaseHelpersActWalk;
	}
	const int32 Activity = ActivityIdForName(CustomMove);   // `LookupActivity`
	if (Activity != INDEX_NONE)
	{
		return Activity;
	}
	// `LookupSequence(name)`: a name that is not an activity may still be a raw sequence, and the
	// answer is then `ACT_SCRIPT_CUSTOM_MOVE`. SEAM — this runtime resolves clips by name through
	// the clip identity and carries no retail sequence index, so the lookup answers "not found"
	// and the body takes its `ACT_WALK` tail. That is retail's own arm for an unknown name.
	return GBaseHelpersActWalk;
}

// 0x1028ebc0 — can I see this point? Retail name unrecovered; no caller in the corpus.
bool FElysiumNpc::FUN_1028ebc0(const FVector& PointCm) const
{
	// Three gates, in order, and the whole 674-byte body is nothing else:
	//   1. `CBaseCombatCharacter::FInViewCone(point, m_flFieldOfView /*+0x1574*/)` — `0x103268e0`,
	//      which picks the 2-D or the 3-D cone off a ConVar;
	//   2. `|EyePosition() - point|² <= m_flVisionDistance² (+0x63b8)`; retail computes the
	//      "beyond" predicate and takes the FAIL path on true;
	//   3. an engine ray from the eye to the point, passing only at `fraction == 1.0`
	//      (`_DAT_10449280`). The ConVar-gated pass between the trace and the compare
	//      (`thunk_FUN_10143d80` / `thunk_FUN_10142e90`) is the debug-overlay draw and changes
	//      nothing the trace answered.
	const FVector Eye = EyePosition();
	// `m_flFieldOfView` (+0x1574) is the observer's own cone threshold, which this port's cone test
	// reads internally; the target cone scalar is retail's 1.0 literal.
	if (!FElysiumNpcSenses::IsInViewCone(*this, PointCm, 1.0f))
	{
		return false;
	}
	const double VisionCm = static_cast<double>(Senses.Perception.VisionDistanceCm);
	if (FVector::DistSquared(Eye, PointCm) > VisionCm * VisionCm)
	{
		return false;
	}
	// Family Motor's `KernelHullTrace` is the kernel-tier seam for `(*DAT_1070b254)->TraceRay`. It
	// answers false with `Fraction == 1.0`, which IS retail's clear line — so this gate passes
	// today and the refusal above it is what decides.
	FKernelHullTrace Trace;
	KernelHullTrace(Eye / ElysiumMove::U, PointCm / ElysiumMove::U, FVector::ZeroVector,
		FVector::ZeroVector, 0, Trace);
	return Trace.Fraction >= GDatClearFraction;
}

// 0x102906a0 / 0x102906c0 / 0x10290700 — `IsThinkDue` (`0x10290660`) over three named clocks
bool FElysiumNpc::IsUpdateThinkDue() const
{
	// `IsThinkDue(stamp)` is `(stamp - curtime) <= frametime`: the decompiler renders the FPU
	// compare as `(a < ft) != (a == ft)`, which is `a <= ft`. `ElysiumNpcThink::IsDue` is the port's
	// own spelling of it and the four think clocks already run off it.
	return World != nullptr
		&& ElysiumNpcThink::IsDue(ScheduleHost.NextUpdate, World->NowSeconds(),
			World->FrameSeconds());
}

bool FElysiumNpc::IsNormalThinkDue() const
{
	return World != nullptr
		&& ElysiumNpcThink::IsDue(ScheduleHost.NextNormal, World->NowSeconds(),
			World->FrameSeconds());
}

bool FElysiumNpc::IsAiThinkDue() const
{
	return World != nullptr
		&& ElysiumNpcThink::IsDue(ScheduleHost.NextAI, World->NowSeconds(), World->FrameSeconds());
}

// slot 527 0x10293e80 `bool IsUnusableNode(CAI_Node*)`
bool FElysiumNpc::IsUnusableNode(void* Node)
{
	// `node->+0xa0` is the node's `CAI_Hint*`. A node with NO hint is usable; a node with one is
	// unusable exactly when `0x102d1540` says the hint is NOT available to me. Retail:
	//
	//   uVar1 = 0;
	//   if (node->hint) { uVar1 = IsHintAvailable(hint, this); if (!uVar1) return 1; }
	//   return 0;
	//
	// SEAM: this substrate carries nodes as bare indices and stands no `CAI_Node`, so the `+0xa0`
	// read answers "no hint" and every node is usable. `Node` is retail's word, kept as `void*` by
	// the generated signature.
	if (Node == nullptr)
	{
		return false;
	}
	// The hint index behind `node+0xa0`; there is no node store, so it is always absent.
	constexpr int32 NodeHint = INDEX_NONE;
	if (NodeHint == INDEX_NONE)
	{
		return false;
	}
	return !IsHintAvailableToMe(NodeHint);
}

// 0x1029f610 — the navigator path probe. Retail name unrecovered.
bool FElysiumNpc::FUN_1029f610(const FElysiumEntity* GoalEntity) const
{
	// `if (goal && goal->+0x04) return thunk_FUN_10307ac0(goal->+0x04, m_pNavigator->+0x2c);`
	// else false. `+0x2c` on the navigator is its current path object, and `0x10307ac0` asks
	// whether that path is routed through the given entity.
	//
	// SEAM: `IElysiumNpcMotor` keeps no readable path (family Motor's `NavGoalPosition` stands the
	// same absence), so the query answers false — which is retail's own "not on this path" arm.
	if (GoalEntity == nullptr)
	{
		return false;
	}
	return false;   // UNRECOVERED input: the navigator's path object at `m_pNavigator+0x2c`
}

// 0x1029f650 — the patrol node's interesting-place draw
bool FElysiumNpc::FUN_1029f650(int32 PatrolNode)
{
	// `m_bPatrolPathUseHint = 0; record = 0x1029f6c0(node); if (record && Random(0,99) <
	// record->m_iIPPercent (+0x46c)) m_bPatrolPathUseHint = 1; return m_bPatrolPathUseHint;`
	//
	// The reset happens FIRST and unconditionally, so a node with no record clears a standing flag.
	ScheduleHost.bPatrolPathUseHint = false;
	const int32 Record = PatrolNodeInterestRecord(PatrolNode);
	if (Record != INDEX_NONE)
	{
		// `(*DAT_1070b244)->+8` is the engine's `RandomInt(0, 99)`.
		const int32 Roll = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99);
		if (Roll < PatrolNodeInterestPercent(Record))
		{
			ScheduleHost.bPatrolPathUseHint = true;
		}
	}
	return ScheduleHost.bPatrolPathUseHint;
}

// 0x1029f730 — the cached read side of that draw
int32 FElysiumNpc::FUN_1029f730(int32 PatrolNode)
{
	// `if (!m_bPatrolPathUseHint) return 0; if (!cache) cache = 0x1029f6c0(node); return cache;`
	// The cache word is `+0x659c`, which 29b reserved as `ScheduleHost.Unknown659c` and both Troika
	// teardown virtuals clear.
	if (!ScheduleHost.bPatrolPathUseHint)
	{
		return 0;
	}
	if (ScheduleHost.Unknown659c == 0)
	{
		const int32 Record = PatrolNodeInterestRecord(PatrolNode);
		ScheduleHost.Unknown659c = Record == INDEX_NONE ? 0u : static_cast<uint32>(Record);
	}
	return static_cast<int32>(ScheduleHost.Unknown659c);
}

// =================================================================================================
// The three hint validators.
// =================================================================================================

bool FElysiumNpc::CoverHintStillValid(const FHintWords& Hint, const FVector& CoverObjectCm,
	const FVector& MyOriginCm, bool bIsCurrentHint) const
{
	// `0x10295ed0`'s rule, without the two seams (the cover-object resolve and `0x102968f0`).
	//
	//   dist = Length2D(coverObject - hint)
	//   current hint -> reject outside [m_flTargetDistMin - 64, m_flTargetDistMax + 64]
	//   otherwise    -> reject outside [m_flTargetDistMin,      m_flTargetDistMax]
	//   proj = dot( normalize2D(coverObject - hint), AngleVectors2D(hint yaw) )
	//   reject unless proj > m_flTargetAngleRangeDot     (STRICTLY greater; retail's compare is
	//                                                     `(a<b) == (a==b)`, true only for a > b)
	//   current hint -> ACCEPT here
	//   otherwise    -> |hint - me| must be <= 512 (`_DAT_10483aac`), and for hint type 0x283d the
	//                   forward projection of (hint - me) on the hint facing must exceed
	//                   `_DAT_104454d0`, and then `0x102968f0` decides.
	if (!Hint.bValid || Hint.Disabled != 0)
	{
		return false;
	}
	const FVector DeltaUnits = (CoverObjectCm - Hint.OriginCm) / ElysiumMove::U;
	const double Dist = FMath::Sqrt(DeltaUnits.X * DeltaUnits.X + DeltaUnits.Y * DeltaUnits.Y);
	const double Tolerance = bIsCurrentHint ? static_cast<double>(GDatAttackBandUnits) : 0.0;
	if (Dist < static_cast<double>(Hint.TargetDistMin) - Tolerance
		|| Dist > static_cast<double>(Hint.TargetDistMax) + Tolerance)
	{
		return false;
	}
	// `_DAT_104454c0 / (dist + _DAT_1046a51c)` is the 1/length normalise; the epsilon is what keeps
	// a coincident pair finite.
	const double Scale =
		static_cast<double>(GDatOne) / (Dist + static_cast<double>(GDatDistanceEpsilon));
	const double Yaw = FMath::DegreesToRadians(Hint.Angles.Y);   // `0x102d12e0`, the hint's yaw
	const double FaceX = FMath::Cos(Yaw);
	const double FaceY = FMath::Sin(Yaw);
	const double Projection = DeltaUnits.X * Scale * FaceX + FaceY * DeltaUnits.Y * Scale;
	if (!(Projection > static_cast<double>(Hint.TargetAngleRangeDot)))
	{
		return false;
	}
	if (bIsCurrentHint)
	{
		return true;
	}
	const FVector ToHintUnits = (Hint.OriginCm - MyOriginCm) / ElysiumMove::U;
	if (ToHintUnits.Size() > static_cast<double>(GDatNearDistanceUnits))
	{
		return false;
	}
	if (Hint.HintType == GHintTypeCoverForward)
	{
		const double ForwardProjection = ToHintUnits.X * FaceX + ToHintUnits.Y * FaceY;
		// `_DAT_104454d0`, the pooled 0.5f, as a projection floor.
		if (ForwardProjection <= static_cast<double>(GDatCoverForwardMin))
		{
			return false;
		}
	}
	return true;
}

bool FElysiumNpc::FUN_10295ed0(int32 HintNode) const
{
	// The entry point. `m_hHintCoverObject` (`+0x6448`) must resolve, and the tail is the hint LOS
	// check `0x102968f0`.
	FHintWords Hint;
	if (!HintWords(HintNode, Hint))
	{
		return false;
	}
	FElysiumEntity* CoverObject = (World != nullptr)
		? const_cast<FElysiumEntityWorld*>(World)->Resolve(ScheduleHost.HintCoverObject) : nullptr;
	if (CoverObject == nullptr)
	{
		return false;
	}
	const bool bIsCurrentHint = HintNode == ScheduleHost.HintNode;
	if (!CoverHintStillValid(Hint, CoverObject->Origin, Origin, bIsCurrentHint))
	{
		return false;
	}
	return bIsCurrentHint || HintLosCheck(HintNode, CoverObject);
}

FElysiumNpc::EHintRejectReason FElysiumNpc::CoverHintRejectReason(const FHintWords& Hint,
	const FVector& CoverObjectCm, bool bIsCurrentHint) const
{
	// `0x102961a0`'s rule — the verbose twin. It shares `0x10295ed0`'s band and projection and
	// differs in three ways: a `target_name` gate in FRONT of everything, a single shared
	// "Distance (%d) < %d or > %d" reason for both band policies, and an INLINE LOS ray (which the
	// entry point runs) instead of `0x102968f0`.
	if (!Hint.bValid)
	{
		return EHintRejectReason::NoHint;
	}
	if (Hint.Disabled != 0)
	{
		return EHintRejectReason::Disabled;
	}
	// `m_strTargetName` (`+0x468`) against MY `m_iName` (`+0x26c`), case-insensitive. An empty
	// target name admits everyone; a set one admits only the NPC it names.
	if (!Hint.TargetName.IsEmpty() && !Hint.TargetName.Equals(TargetName, ESearchCase::IgnoreCase))
	{
		return EHintRejectReason::TargetNameMismatch;   // "Target name mismatch (%s)"
	}
	const FVector DeltaUnits = (CoverObjectCm - Hint.OriginCm) / ElysiumMove::U;
	const double Dist = FMath::Sqrt(DeltaUnits.X * DeltaUnits.X + DeltaUnits.Y * DeltaUnits.Y);
	const double Tolerance = bIsCurrentHint ? static_cast<double>(GDatAttackBandUnits) : 0.0;
	if (Dist < static_cast<double>(Hint.TargetDistMin) - Tolerance
		|| Dist > static_cast<double>(Hint.TargetDistMax) + Tolerance)
	{
		return EHintRejectReason::DistanceOutOfBand;    // "Distance (%d) < %d or > %d"
	}
	const double Scale =
		static_cast<double>(GDatOne) / (Dist + static_cast<double>(GDatDistanceEpsilon));
	const double Yaw = FMath::DegreesToRadians(Hint.Angles.Y);
	const double Projection =
		DeltaUnits.X * Scale * FMath::Cos(Yaw) + FMath::Sin(Yaw) * DeltaUnits.Y * Scale;
	if (!(Projection > static_cast<double>(Hint.TargetAngleRangeDot)))
	{
		// "Enemy outside of good range (%.2f) <= %.2f"
		return EHintRejectReason::OutsideGoodRange;
	}
	return EHintRejectReason::None;
}

FElysiumNpc::EHintRejectReason FElysiumNpc::FUN_102961a0(int32 HintNode) const
{
	FHintWords Hint;
	if (!HintWords(HintNode, Hint))
	{
		return EHintRejectReason::NoHint;
	}
	FElysiumEntity* CoverObject = (World != nullptr)
		? const_cast<FElysiumEntityWorld*>(World)->Resolve(ScheduleHost.HintCoverObject) : nullptr;
	// Retail runs the `target_name` and `m_iDisabled` gates BEFORE it resolves the cover object, so
	// a disabled hint answers `Disabled` (no string, the top arm) and a name mismatch answers its
	// own reason even with no cover object standing.
	if (!Hint.bValid || Hint.Disabled != 0)
	{
		return EHintRejectReason::Disabled;
	}
	if (!Hint.TargetName.IsEmpty() && !Hint.TargetName.Equals(TargetName, ESearchCase::IgnoreCase))
	{
		return EHintRejectReason::TargetNameMismatch;
	}
	if (CoverObject == nullptr)
	{
		return EHintRejectReason::NoCoverObject;        // "No cover object"
	}
	const bool bIsCurrentHint = HintNode == ScheduleHost.HintNode;
	const EHintRejectReason Reason = CoverHintRejectReason(Hint, CoverObject->Origin,
		bIsCurrentHint);
	if (Reason != EHintRejectReason::None)
	{
		return Reason;
	}
	if (bIsCurrentHint)
	{
		return EHintRejectReason::None;
	}
	// The inline ray: from my origin raised by `m_Collision->OBBMaxs().z` to a point on the hint
	// (`0x102d1180`), and it passes only at `fraction >= 1.0` with neither `allsolid` nor
	// `startsolid`.
	FVector EndCm = FVector::ZeroVector;
	if (!HintLosEndpoint(HintNode, EndCm))
	{
		// The endpoint seam refused, so the ray cannot be cast. Retail's failing arm is
		// "Failed LOS check (%s)" and this is where the port stands until a hint store lands.
		return EHintRejectReason::FailedLos;
	}
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);
	const FVector StartUnits = Origin / ElysiumMove::U + FVector(0.0, 0.0, MaxsUnits.Z);
	FKernelHullTrace Trace;
	KernelHullTrace(StartUnits, EndCm / ElysiumMove::U, FVector::ZeroVector, FVector::ZeroVector, 0,
		Trace);
	return Trace.Fraction >= GDatClearFraction ? EHintRejectReason::None
											   : EHintRejectReason::FailedLos;
}

FElysiumNpc::EHintRejectReason FElysiumNpc::AttackHintRejectReason(const FHintWords& Hint,
	const FVector& EnemyCm, const FVector& MyOriginCm, bool bIsCurrentHint, bool bHasActiveWeapon,
	float GoodRangeDot, float BadRangeDot) const
{
	// `0x10296c40`'s rule, in retail's exact order.
	if (!Hint.bValid || Hint.Disabled != 0)
	{
		return EHintRejectReason::Disabled;             // "Disabled"
	}
	// THE FIRST ARM IS A PASS, not a fail: my own hint while `m_bStayEntrenched` stands accepts
	// unconditionally and skips every test below. Retail's second half of the same arm — a NULL
	// enemy — is decided by the entry point, which has the handle.
	if (bIsCurrentHint && bStayEntrenched)
	{
		return EHintRejectReason::None;
	}
	if (!bHasActiveWeapon)
	{
		return EHintRejectReason::NoActiveWeapon;       // "No active weapon"
	}
	const double HeightDiffUnits =
		FMath::Abs(Hint.OriginCm.Z - MyOriginCm.Z) / static_cast<double>(ElysiumMove::U);
	if (HeightDiffUnits > GDatHintHeightDiffUnits)
	{
		return EHintRejectReason::HeightDiff;          // "Height diff (%d) > %d"
	}
	const FVector DeltaUnits = (EnemyCm - Hint.OriginCm) / ElysiumMove::U;
	const double Dist = FMath::Sqrt(DeltaUnits.X * DeltaUnits.X + DeltaUnits.Y * DeltaUnits.Y);
	if (Dist < static_cast<double>(Hint.TargetDistMin))
	{
		return EHintRejectReason::DistanceBelowMin;     // "Distance (%d) < %d"
	}
	// `m_bStayEntrenched` SKIPS the upper bound entirely. The bound itself is two terms ORed: the
	// active weapon's own maximum range (`+0x8c0`) and the hint's `m_flTargetDistMax`.
	if (!bStayEntrenched)
	{
		float WeaponRangeUnits = 0.f;
		const bool bHasRange = ActiveWeaponMaxRangeUnits(WeaponRangeUnits);
		// SEAM: no port weapon record carries a range, so only the hint term is evaluated. Stated
		// rather than papered over — a body past the weapon's reach but inside the hint's band is
		// accepted here and rejected in retail.
		if ((bHasRange && static_cast<double>(WeaponRangeUnits) < Dist)
			|| static_cast<double>(Hint.TargetDistMax) < Dist)
		{
			return EHintRejectReason::DistanceAboveMax; // "Distance (%d) > %d or %d"
		}
	}
	if (!bIsCurrentHint)
	{
		// `dot( normalize2D(me - enemy), normalize2D(hint - enemy) )` — am I already on the enemy's
		// side of the hint?
		FVector ToMe = (MyOriginCm - EnemyCm) / ElysiumMove::U;
		FVector ToHint = (Hint.OriginCm - EnemyCm) / ElysiumMove::U;
		ToMe.Z = 0.0;
		ToHint.Z = 0.0;
		ToMe.Normalize();
		ToHint.Normalize();
		if (FVector::DotProduct(ToHint, ToMe) < static_cast<double>(GDatProjectionMin))
		{
			return EHintRejectReason::Projection;       // "Projection (%.2f) < 0.2"
		}
	}
	const double Scale =
		static_cast<double>(GDatOne) / (Dist + static_cast<double>(GDatDistanceEpsilon));
	const double Yaw = FMath::DegreesToRadians(Hint.Angles.Y);
	const double Facing =
		DeltaUnits.X * Scale * FMath::Cos(Yaw) + FMath::Sin(Yaw) * DeltaUnits.Y * Scale;
	// The two BAND arms, and they are not symmetric: the good-range gate is `<=` (retail's
	// `(a < p3) != (a == p3)`) and the bad-range gate is `>=`.
	if (Facing <= static_cast<double>(GoodRangeDot))
	{
		return EHintRejectReason::OutsideGoodRange;     // "Enemy outside of good range (%.2f) <= …"
	}
	if (Facing >= static_cast<double>(BadRangeDot))
	{
		return EHintRejectReason::InsideBadRange;       // "Enemy inside of bad range (%.2f) >= …"
	}
	return EHintRejectReason::None;
}

FElysiumNpc::EHintRejectReason FElysiumNpc::FUN_10296c40(int32 HintNode,
	const FElysiumEntity* Enemy, float GoodRangeDot, float BadRangeDot) const
{
	FHintWords Hint;
	if (!HintWords(HintNode, Hint))
	{
		return EHintRejectReason::Disabled;   // retail's top arm covers a null hint too
	}
	const bool bIsCurrentHint = HintNode == ScheduleHost.HintNode;
	if ((bIsCurrentHint && bStayEntrenched) || Enemy == nullptr)
	{
		return EHintRejectReason::None;
	}
	const bool bHasActiveWeapon = World != nullptr && Inventory.ActiveWeapon.IsSet()
		&& const_cast<FElysiumEntityWorld*>(World)->Resolve(Inventory.ActiveWeapon) != nullptr;
	const EHintRejectReason Reason = AttackHintRejectReason(Hint, Enemy->Origin, Origin,
		bIsCurrentHint, bHasActiveWeapon, GoodRangeDot, BadRangeDot);
	if (Reason != EHintRejectReason::None)
	{
		return Reason;
	}
	// The LAST gate, and only under `m_bForceCoverLOSCheck` (`+0x6408`).
	if (ScheduleHost.bForceCoverLosCheck && !HintLosCheck(HintNode, Enemy))
	{
		return EHintRejectReason::FailedLos;            // "Failed hint LOS"
	}
	return EHintRejectReason::None;
}

// =================================================================================================
// The face-anim turn ladder.
// =================================================================================================

FElysiumNpc::FFaceAnimPick FElysiumNpc::FaceAnimLadder(float YawDelta,
	TFunctionRef<bool(int32)> HasSequence)
{
	// `0x10297a20`, rung by rung. Each rung is GATED on the body actually authoring the activity
	// (`SelectWeightedSequence(act) != -1`); a body that does not falls through to the next rung,
	// and the tail is `ACT_IDLE` with `m_eFaceAnim = 0`.
	if ((YawDelta < GDatFaceAnimYawLow || YawDelta > GDatFaceAnimYawHigh)
		&& HasSequence(GFaceAnimAct180))
	{
		return FFaceAnimPick{ GFaceAnimAct180, 8, true };
	}
	if (YawDelta <= GDatFaceAnimYawMid && HasSequence(GFaceAnimAct90))
	{
		// A `<=` against the NEGATIVE band edge -40, which the ladder's descent requires.
		return FFaceAnimPick{ GFaceAnimAct90, 6, true };
	}
	if (YawDelta >= GDatFaceAnimTurnYaw && HasSequence(GFaceAnimAct45))
	{
		return FFaceAnimPick{ GFaceAnimAct45, 3, true };
	}
	if (HasSequence(GFaceAnimActSmall))
	{
		return FFaceAnimPick{ GFaceAnimActSmall, 1, false };
	}
	return FFaceAnimPick{ GBaseHelpersActIdle, 0, false };
}

void FElysiumNpc::FUN_10297a20()
{
	// The body around the ladder: the motor's yaw delta in, the pick's activity made ideal
	// (`thunk_FUN_10272650`, family Facing's `SetIdealActivityNumber`), and TWO words written —
	// `m_eFaceAnim` (`+0x63e4`) and `m_flFaceYawDiff` (`+0x63e8`). The three upper rungs write a
	// DRAWN duration (`__ftol` of a random, masked to 16 bits and scaled by `_DAT_1044ffdc`); the
	// two lower ones copy the yaw delta itself.
	const float YawDelta = MotorDeltaIdealYaw();   // `CAI_Motor::DeltaIdealYaw` 0x102e1f90
	const FFaceAnimPick Pick = FaceAnimLadder(YawDelta,
		[this](int32 Activity) { return SelectWeightedSequenceForActivity(Activity) != -1; });
	FaceAnim = Pick.FaceAnim;
	if (Pick.bRandomDuration)
	{
		// The 16-bit draw scaled by `_DAT_1044ffdc` = 360 / 65536.
		const int32 Draw =
			ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 0xffff) & 0xffff;
		FaceYawDiff = static_cast<float>(Draw) * GDatFaceAnimRandomScale;
	}
	else
	{
		FaceYawDiff = YawDelta;
	}
	SetIdealActivityNumber(Pick.Activity);
}

// =================================================================================================
// The victim-side reaction slots, 21 / 22 / 23 / 27 / 317.
// =================================================================================================
//
// Four bodies, one shape. `thunk_FUN_102bf5d0` is the detected-attack notice family Squad ported as
// `AlertNearbyAlly`; `thunk_FUN_10269a20` is `SetCondition`, NOT a clear (29c's walk read it as one
// and it is corrected here); the bare `(*DAT_10924a6c)->vtable+4` call beside every `SetCondition`
// is the AI-debug ConVar the condition setter consults and NOT a game event (the same walk read it
// as one). Condition 10 is `COND_BEING_ATTACKED` (`0x0a`) and condition 12 `COND_SHOULD_DODGE`
// (`0x0c`) in the registry `ElysiumNpcConditions.h` carries.

// slot 21 0x1029f800 `void vfunc21(CBaseEntity*)`
void FElysiumNpc::Slot21(FElysiumEntity* Attacker)
{
	// The vtable dispatch first. `CNPC_VMingXiaoTentacle` overrides slots 21, 22 and 23 AGAIN under
	// the Troika line (`0x1039e800` / `0x1039e830` / `0x1039e860`, family **Species**) and forwards
	// each to its head instead: a tentacle raises no condition of its own, which is what the base
	// arm below would have done.
	if (SpeciesSlot21(Attacker))
	{
		return;
	}
	AlertNearbyAlly(Attacker);
	// `m_iHitBuildupCount` (`+0x6064`) — the shape map binds it to the combat character, where the
	// port already raises it on a landed hit (`FElysiumCombatCharacter::RaiseHitBuildup`).
	RaiseHitBuildup();
	Cognition.Conditions.Set(EElysiumNpcCond::BeingAttacked);
}

// slot 22 0x1029f850 `void vfunc22(CBaseEntity*)`
void FElysiumNpc::Slot22(FElysiumEntity* Attacker)
{
	// The tentacle's own slot 22 first (`0x1039e830`, family **Species**).
	if (SpeciesSlot22(Attacker))
	{
		return;
	}
	// Slot 21 without the hit-buildup increment. `CBasePlayer::Replenish` (`0x10168320`) dispatches
	// this on a feed target with the player as the argument.
	AlertNearbyAlly(Attacker);
	Cognition.Conditions.Set(EElysiumNpcCond::BeingAttacked);
}

// slot 23 0x1029f890 `void vfunc23(CBaseEntity*)`
void FElysiumNpc::Slot23(FElysiumEntity* Attacker)
{
	// The tentacle's own slot 23 first (`0x1039e860`, family **Species**).
	if (SpeciesSlot23(Attacker))
	{
		return;
	}
	// Byte-identical to slot 22. `signatures.md`: no dispatch site exists in the decompiled corpus,
	// so what distinguishes the three is UNRECOVERED — they are three slots carrying one body.
	AlertNearbyAlly(Attacker);
	Cognition.Conditions.Set(EElysiumNpcCond::BeingAttacked);
}

// slot 25 0x100265b0 `void vfunc25(CBaseEntity*)`
void FElysiumNpc::Slot25(FElysiumEntity* Victim)
{
	// Here, and not in the generated file, because slots 25 and 26 need the same species prologue
	// the four victim-side slots above need and are the same concern: `CNPC_VZombie` replaces both
	// (`0x103e12c0` / `0x103e12f0`, family **Species**) with one `m_OnAttackedVictim` fire and NO
	// base forward. The verdict overlay's row is `hand:FElysiumNpc::Slot25` for that reason alone.
	if (SpeciesSlot25(Victim))
	{
		return;
	}
	// `0x100265b0`, the Troika line's own body: ONE byte, `ret`. The overlay's reading was
	// `default:void` and the body is still exactly that — no member is written and nothing is
	// tallied, because retail writes nothing either.
}

// slot 26 0x100265d0 `void vfunc26(CBaseEntity*)`
void FElysiumNpc::Slot26(FElysiumEntity* Victim)
{
	// The zombie's second copy of the same output fire (`0x103e12f0`).
	if (SpeciesSlot26(Victim))
	{
		return;
	}
	// `0x100265d0`, empty on the Troika line exactly as slot 25 is.
}

// slot 27 0x1029f8f0 `void vfunc27(CBaseEntity*)`
void FElysiumNpc::Slot27(FElysiumEntity* Attacker)
{
	// Slot 22 plus a fourth step: slot 600 (vtable `+0x960`) on MYSELF with the attacker as the
	// argument — the melee-coordinator slot request. Order matters: the notice and the condition
	// land first, the request last.
	AlertNearbyAlly(Attacker);
	Cognition.Conditions.Set(EElysiumNpcCond::BeingAttacked);
	Slot600(Attacker);
}

// slot 317 0x1029fb70 `bool vfunc317(CBaseEntity*)`
bool FElysiumNpc::Slot317(FElysiumEntity*)
{
	// The argument is never read — `signatures.md` calls the slot unsettled for exactly that
	// reason. The body:
	//   SetCondition(COND_BEING_ATTACKED);
	//   if (ConditionInterruptsCurrentSchedule(COND_SHOULD_DODGE)) {
	//       SetCondition(COND_SHOULD_DODGE); return true;
	//   }
	//   return false;
	//
	// So the dodge bit is raised ONLY when the running program's interrupt mask lists it, and the
	// return says whether it was. `ElysiumSchedule::MaskHasCondition` is the port's `0x10269c70`.
	Cognition.Conditions.Set(EElysiumNpcCond::BeingAttacked);
	if (!ElysiumSchedule::MaskHasCondition(Schedule, *this, EElysiumNpcCond::ShouldDodge))
	{
		return false;
	}
	Cognition.Conditions.Set(EElysiumNpcCond::ShouldDodge);
	return true;
}

// =================================================================================================
// The remaining slots.
// =================================================================================================

// slot 19 0x1028dfb0 `void TraceMessageBare(const char*) const`
void FElysiumNpc::TraceMessageBare(const TCHAR* Message) const
{
	// A null message does nothing at all. Otherwise the global dev byte `DAT_10920534` picks the
	// channel: set copies up to 0x200 bytes onto the NPC's own trace ring (`thunk_FUN_1027ee20`),
	// clear prints straight through `DevMsg`. No member is written either way.
	//
	// SEAM: `DAT_10920534` is retail's verbose-trace toggle and this runtime has no such console
	// byte; the ring itself is `ELYSIUM_NPC_WORD_ABSENT(0x1b4e)` ("retail's 16 KB in-memory AI
	// debug ring; this runtime logs through its own channels"). So the toggle reads CLEAR and the
	// `DevMsg` arm is the one every call takes, onto `LogElysiumNpcEnt` — which is that channel.
	if (Message == nullptr)
	{
		return;
	}
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s"), Message);
}

// slot 542 0x10273dd0 `void vfunc542()`
void FElysiumNpc::Slot542()
{
	// `delete m_pEnemies (+0x5d88); m_pEnemies = m_pSquad (+0x5da4) + 8;` — the NPC gives up its
	// private `AI_Enemies` and points at the squad's embedded one. Retail does NOT null-check
	// `m_pSquad`: with no squad it writes the literal 8 into the pointer, which the next enemy read
	// dereferences.
	//
	// Family Squad already stands this ownership move as `RepointEnemyMemoryToSquad` and calls
	// `Slot542()` from `InitSquad`; this is the slot's body and it forwards there rather than
	// standing a second copy. The port carries ONE enemy memory per NPC and no squad memory to
	// point it at, so the move changes nothing and the null-squad fault is not reproduced (NAMED
	// DIVERGENCE: a crash is not a behaviour to reproduce).
	RepointEnemyMemoryToSquad(const_cast<void*>(ConnectedSquad()));
}

// slot 571 0x10289ce0 `Activity vfunc571(float)`
int32 FElysiumNpc::Slot571(float Distance)
{
	// The whole 30-byte body: `ACT_WALK` below `_DAT_1049a17c`, `ACT_RUN` at or beyond it. Its one
	// caller is `CAI_BaseNPC::RunTask` (`0x10288780`) task 0x0b, which passes the distance to the
	// move target and makes the answer both the movement and the ideal activity.
	//
	// `_DAT_1049a17c` is UNRECOVERED; at the 0.0 stand-in every non-negative distance answers
	// `ACT_RUN`, which is the arm the task takes for any real separation.
	return Distance >= GDatFollowRunDistanceUnits ? GBaseHelpersActRun : GBaseHelpersActWalk;
}

// slot 588 0x10293e50 `void vfunc588()`
void FElysiumNpc::Slot588()
{
	// The vtable dispatch first: `CNPC_VTzimisceRunner` `0x103c3fd0` (family **Species**) is this
	// body with the `IsActivityFinished()` gate REMOVED, so a runner restarts mid-clip.
	if (SpeciesSlot588())
	{
		return;
	}

	// `if (IsActivityFinished()) RestartIdealActivity(ACT_DISPOSITION);` — slot 251 (`+0x3ec`) and
	// `0x10289ee0`. Dispatched three times from `CAI_BaseNPCTroika::RunTask` (`0x102aacf0`).
	//
	// NOT `CAI_BaseHumanoid`'s slot 588: that is `0x1025ea00`, `CAI_BaseActor::ValidHeadTarget`, a
	// one-word body of another table, ported above under its own name.
	if (IsActivityFinished())
	{
		RestartIdealActivityId(GBaseHelpersActDisposition);
	}
}

// slot 497 0x102947e0 `void vfunc497()`
void FElysiumNpc::Slot497()
{
	// The vtable dispatch first: `CNPC_VCamera` `0x103681d0` (and `CNPC_VCameraSecurity` under it)
	// replaces this slot with ONE BYTE, a bare `ret` — family **Species**' row. The point of that row
	// is precisely that the once-only concept cache below does not run for a camera.
	if (SpeciesSlot497())
	{
		return;
	}

	// A ONCE-ONLY global cache, not per-NPC state: bit 0 of `DAT_109249c4` guards it, and the body
	// linear-scans `DAT_1073dc40[0 .. DAT_1073dc3c)` comparing each entry's `+0x04` name
	// case-insensitively against the fixed string at `DAT_105d8ccc`, caching the matching entry's
	// `+0x00` id (or -1) into `_DAT_109247dc`.
	//
	// `signatures.md` reads that string as the PLACEHOLDER `"???"` and says the body "plays
	// nothing" — the concept it caches is a placeholder, so the cached id is never used to speak.
	//
	// SEAM: the global table `DAT_1073dc40` is the response-system concept list, which this runtime
	// does not carry. The once-latch is reproduced (it is the observable half — a second call does
	// nothing) and the scan answers "not found", which writes -1.
	static bool bConceptCached = false;   // DAT_109249c4 & 1
	if (bConceptCached)
	{
		return;
	}
	bConceptCached = true;
	// `_DAT_109247dc = -1`: no concept table to scan.
}

// 0x1027e0f0 `CAI_BaseNPC::FUN_1027e0f0` — the BASE line's slot-532 body
bool FElysiumNpc::FUN_1027e0f0()
{
	// `m_hOpeningDoor = -1; m_bOpeningDoorWait = 0; return 1;` — the argument is ignored. Retail
	// leaves `AL = 1` and every dispatch site drops it, which is why `signatures.md` types the slot
	// `void`.
	//
	// The generated `Slot532(int32)` carries the TROIKA override `0x10290570` (layer 11, story 29d),
	// which switches on its argument to end the alternate-AI door wait and then chains HERE. This
	// is that chain target and is named by address because the retail name is unrecovered.
	OpeningDoor = FElysiumEntityHandle();
	bOpeningDoorWait = false;
	return true;
}

// =================================================================================================
// The seams.
// =================================================================================================

bool FElysiumNpc::ActiveWeaponMaxRangeUnits(float& OutRangeUnits) const
{
	// SEAM for `GetActiveWeapon()->+0x8c0`. No port weapon record carries a maximum range; the
	// item table has damage, ammo and wield rules and no reach. Answers false and the one caller
	// (`AttackHintRejectReason`) drops that term and says so.
	OutRangeUnits = 0.f;
	return false;
}

int32 FElysiumNpc::PatrolNodeInterestRecord(int32 PatrolNode) const
{
	// SEAM for `0x1029f6c0` — the patrol node's interesting-place record. Family Hints stands the
	// same absence from the name side (`PatrolNodeInterestRecordName`) and family Hints'
	// `ResolvePatrolInterestPlace` (`0x1029f780`) caches the resolve at `+0x6300`. There is no
	// patrol-node graph here, so the lookup answers nothing.
	(void)PatrolNode;
	return INDEX_NONE;
}

int32 FElysiumNpc::PatrolNodeInterestPercent(int32 Record) const
{
	// SEAM for that record's `+0x46c m_iIPPercent` — the same word `FHintWords::IpPercent` names on
	// a hint. A `Random(0,99)` never comes in under 0, so the draw never fires.
	(void)Record;
	return 0;
}

bool FElysiumNpc::HintLosCheck(int32 HintNode, const FElysiumEntity* Against) const
{
	// SEAM for `0x102968f0`. Retail traces from the NPC's shooting position to the hint's own LOS
	// point against the target and answers a bool. With no hint store the trace has no endpoint;
	// this answers TRUE, which is the PASS arm — the same posture family Motor's `KernelHullTrace`
	// takes (a seam that cannot trace reports a clear line).
	(void)HintNode;
	(void)Against;
	return true;
}

bool FElysiumNpc::HintLosEndpoint(int32 HintNode, FVector& OutPointCm) const
{
	// SEAM for `0x102d1180(hint, npc, &out)` — the point on a hint `0x102961a0` rays to.
	(void)HintNode;
	(void)OutPointCm;
	return false;
}

bool FElysiumNpc::IsHintDebugNpc() const
{
	// SEAM for `DAT_10925444`, the `ai_debug_npc` handle. No console selection exists here, so no
	// NPC is the debug NPC and no reason string is ever formatted — retail's answer for every NPC
	// but one.
	return false;
}

bool FElysiumNpc::IsHintAvailableToMe(int32 HintNode) const
{
	// SEAM for `0x102d1540`. Retail: the hint's `m_hHintOwner` (`+0x5e0`) is me -> true; otherwise
	// `curtime < m_flNextUseTime` (`+0x5ec`) -> false; otherwise a LIVE owner handle -> false;
	// else true. Family Hints ports the same three words as `IsHintUnusable`, from the other side.
	// With no hint store there is no owner, which is retail's free answer.
	(void)HintNode;
	return true;
}
