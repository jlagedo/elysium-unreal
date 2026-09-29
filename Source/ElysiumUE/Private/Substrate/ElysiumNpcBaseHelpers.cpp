// `CAI_BaseNPC`'s bodies of the `Helpers` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseHelpers.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStanceTypes.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelBaseHelpersShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Substrate/ElysiumNpcTroikaHelpersShared.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
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
	// UNRECOVERED literals. Each is named so the arm reads as retail's and the value is the one
	// thing waiting; each site says what the stand-in does.
	constexpr float GDatFollowRunDistanceUnits = 190.f;      // _DAT_1049a17c = 0x433e0000 (190.0f, .rdata; no writer) — slot 571's walk/run
	constexpr int32 GBaseHelpersActWalk = 9;                // ACT_WALK
	constexpr int32 GBaseHelpersActRun = 0x13;              // ACT_RUN
	// `CBaseEntity::GetFlags()` bit 0.
	constexpr uint32 GFlOnGround = 1;
}

// --- Moved from `ElysiumNpcKernelBaseHelpers.cpp` (story 5 step 5) ---

// slot 555 0x1026d9a0 `int MeleeAttack1Conditions(float, float)`
int32 FElysiumNpcBase::MeleeAttack1Conditions(float Dot, float Dist)
{
	// Arm for arm, and NOTE that `GetEnemy()` (slot 167, vtable `+0x29c`) is dispatched THREE
	// times: once up front for the combat-character cache, once as a null gate after the dot, and
	// once more for the ground-flag read. Retail re-reads it each time.
	const FElysiumEntity* Enemy = (World != nullptr && BaseMemory.Enemy.IsSet())
		? World->Resolve(BaseMemory.Enemy) : nullptr;
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
	if (Dist > NpcKernelBaseHelpersShared::GDatAttackBandUnits)
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
int32 FElysiumNpcBase::MeleeAttack2Conditions(float Dot, float Dist)
{
	// The sibling, and the three differences are the whole of it: a DIFFERENT outer band
	// (`_DAT_1044c3a8`, 180 units), NO second `GetEnemy()` null gate, and NO ground test — a
	// passing body answers `COND_CAN_MELEE_ATTACK2` outright.
	const FElysiumEntity* Enemy = (World != nullptr && BaseMemory.Enemy.IsSet())
		? World->Resolve(BaseMemory.Enemy) : nullptr;
	const FElysiumCombatCharacter* EnemyCombatant =
		Enemy != nullptr ? Enemy->AsCombatCharacter() : nullptr;

	if (Dist > GDatMelee2TooFarUnits)
	{
		return static_cast<int32>(EElysiumNpcCond::TooFarForMelee);   // 9
	}
	if (Dist > NpcKernelBaseHelpersShared::GDatAttackBandUnits)
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
FElysiumEntity* FElysiumNpcBase::GetNavTargetEntity() const
{
	// `GetGoalType()` (`thunk_FUN_102ee620(m_pNavigator)`), re-read for EVERY arm:
	//   2 `GOALTYPE_ENEMY`     -> m_hEnemy      (+0x5ce0)
	//   1 `GOALTYPE_TARGETENT` -> m_hTargetEnt  (+0x5ce4)
	//   7 `GOALTYPE_COVER`     -> the handle behind `(this+0x98)->vtable+0x928`
	//   anything else          -> NULL
	// and every arm then resolves the handle through the global entity table, answering NULL for a
	// stale one. `NavGoalState()` is the navigator's goal type (`path+0x5c`, 0 with no goal), so an
	// unset goal takes the default arm.
	if (World == nullptr)
	{
		return nullptr;
	}
	FElysiumEntityWorld* MutableWorld = const_cast<FElysiumEntityWorld*>(World);
	const int32 GoalType = NavGoalState();
	if (GoalType == 2)
	{
		return MutableWorld->Resolve(BaseMemory.Enemy);
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
void FElysiumNpcBase::RememberUnreachable(FElysiumEntity* Entity)
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
float FElysiumNpcBase::CalcIdealYaw(const FVector& TargetPos)
{
	// The path's MOVEMENT ACTIVITY (`thunk_FUN_102ee3f0(m_pNavigator)`, `path+0x2c`, 1 by default and after
	// a reset) picks how the delta is built,
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
	//
	// The arithmetic is retail's in SOURCE axes (this world's Y negated; the answer is a RETAIL yaw,
	// the `motor+0x34` convention), then `VecToYaw` (`0x1000612c` -> `0x101d2c70`, `[0, 360)`).
	// (Story 8 L05 integration: the body answered an Unreal-convention yaw in `(-180, 180]`; nothing
	// called it until RunTask19's `0x102e20b0` / `0x102e2750` route did.)
	const int32 NavState = Navigator.GetMovementActivity();
	const double Px = TargetPos.X;
	const double Py = -TargetPos.Y;
	const double Ox = Origin.X;
	const double Oy = -Origin.Y;
	double Dx = 0.0;
	double Dy = 0.0;
	if (NavState == 0x37)                                  // 10274b41
	{
		Dx = -Py - Ox;                                     // 10274b53 FCHS / 10274b67
		Dy = Px - Oy;                                      // 10274b71
	}
	else if (NavState == 0x38)                             // 10274b8c
	{
		Dx = Py - Ox;                                      // 10274bb0
		Dy = Px - Oy;                                      // 10274bba
	}
	else
	{
		Dx = Px - Ox;                                      // 10274c08
		Dy = Py - Oy;                                      // 10274c11
	}
	// Back into this world's axes for the shared `VecToYaw` (which negates Y again).
	return NpcKernelFacingShared::RetailVecToYaw(FVector(Dx, -Dy, 0.0));   // 10274c3f
}

// 0x10274ca0 `CAI_BaseNPC::SetDefaultEyeOffset`
void FElysiumNpcBase::SetDefaultEyeOffset()
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
bool FElysiumNpcBase::IsCurTaskContinuousMove()
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

bool FElysiumNpcBase::CurrentRetailTaskNumber(int32& OutTaskNumber) const
{
	// `GetCurTask()->iTask` (`0x1028a150`: the running schedule's `Task_t` at `m_iScheduleIndex`,
	// its `+0x00`). The runner's steps carry retail's own global task ids (`FElysiumScheduleStep::
	// TaskId`, the registrar `0x10316ff0`'s numbers), so the running step answers it. No schedule, or an
	// index past its task list, is retail's NULL task.
	OutTaskNumber = INDEX_NONE;
	if (!Schedule.IsRunning())
	{
		return false;
	}
	const FElysiumScheduleProgram* Program = ElysiumScheduleFor(Schedule.Current);
	if (Program == nullptr || !Program->Tasks.IsValidIndex(Schedule.TaskIndex))
	{
		return false;
	}
	// The step's id is GLOBAL (`ElysiumScheduleText.h`); retail's `iTask` is class-local, the
	// translation slot 450 carries.
	OutTaskNumber = GlobalToLocalId(IdSpace(EElysiumIdCategory::Task), Program->Tasks[Schedule.TaskIndex].TaskId);
	return true;
}

// 0x10289fe0 `CAI_BaseNPC::GetScriptCustomMoveActivity`
int32 FElysiumNpcBase::GetScriptCustomMoveActivity() const
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
	// `m_iszCustomMove` (`+0x5f50`) on the director `m_hCine` resolves to (story 5 fold A3).
	const FElysiumScriptedSequence* Cine = ResolveCine();
	const FString CustomMove = Cine != nullptr ? Cine->CustomMove : FString();
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

// slot 542 0x10273dd0 `void vfunc542()`
void FElysiumNpcBase::Slot542()
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
int32 FElysiumNpcBase::Slot571(float Distance)
{
	// The whole 30-byte body: `ACT_WALK` below `_DAT_1049a17c`, `ACT_RUN` at or beyond it. Its one
	// caller is `CAI_BaseNPC::RunTask` (`0x10288780`) task 0x0b, which passes the distance to the
	// move target and makes the answer both the movement and the ideal activity.
	//
	// `_DAT_1049a17c` is the `.rdata` float 190.0 (`0x433e0000`, read from the image; its three
	// referrers only read it). The distance is in SOURCE units (the 0x0b arm's `Length2D`).
	// `10289ce4 FCOMP` / `10289cec TEST AH,0x5` / `10289cf4 JNP`: ACT_WALK (9) only when C0 alone is
	// set (ordered `Distance < 190.0`); at or beyond it, and on an unordered (NaN) distance, ACT_RUN.
	return Distance < GDatFollowRunDistanceUnits ? GBaseHelpersActWalk : GBaseHelpersActRun;
}

int32 FElysiumNpcBase::RetailPathingHull() const
{
	// `+0x156c`. Carried on the Troika line (`FElysiumNpc::PathingHullKind`); a base-only NPC has no
	// class that writes a pathing word of its own, so it paths on its standing hull, as
	// `StartTaskSetGoal`'s tolerance arm already reads it.
	const FElysiumNpc* Troika = AsNpc();
	return Troika != nullptr ? Troika->PathingHullKind : HullKind;
}

bool FElysiumNpcBase::HintPositionCm(int32 HintNode, FVector& OutPointCm) const
{
	// `CAI_Hint::GetPosition` -- `0x102d1180(hint, npc, &out)`, the whole body:
	//
	//     if (hint->m_nNodeID (+0x5e4) == -1) out = hint->GetAbsOrigin();       // vtable +0x364
	//     else out = 0x102f46d0(DAT_1093407c, &tmp, npc, hint->m_nNodeID);      // the network
	//
	// and `0x102f46d0` answers `vec3_origin` for no node array, a null NPC or an id outside
	// `-1 < id <= count`, else `CAI_Node::GetPosition(node, npc+0x156c)` -- the PATHING hull, read off
	// the NPC it was handed (`FElysiumPlaceSet::NetworkNodePositionCm`). Every arm writes the out
	// vector; `this` is never null here, so the null-NPC arm is not reachable. False only when
	// `HintNode` names no live hint (retail holds the `CAI_Hint*`, never an index to a dead one).
	FHintWords Words;
	if (!HintWords(HintNode, Words))
	{
		return false;
	}
	if (Words.NodeId == INDEX_NONE)                                            // 0x102d1187 CMP -1
	{
		OutPointCm = Words.OriginCm;                                           // vtable +0x364
		return true;
	}
	OutPointCm = World != nullptr
		? World->Places().NetworkNodePositionCm(Words.NodeId, RetailPathingHull())   // 0x102d11a8
		: FVector::ZeroVector;
	return true;
}

bool FElysiumNpcBase::IsHintAvailableToMe(int32 HintNode) const
{
	// SEAM for `0x102d1540`. Retail: the hint's `m_hHintOwner` (`+0x5e0`) is me -> true; otherwise
	// `curtime < m_flNextUseTime` (`+0x5ec`) -> false; otherwise a LIVE owner handle -> false;
	// else true. Family Hints ports the same three words as `IsHintUnusable`, from the other side.
	// With no hint store there is no owner, which is retail's free answer.
	(void)HintNode;
	return true;
}

// --- Moved from `ElysiumNpcTroikaHelpers.cpp` (story 5 step 5) ---

// --- Moved from `ElysiumNpcTroikaHelpers.cpp` (story 5 step 5) ---

