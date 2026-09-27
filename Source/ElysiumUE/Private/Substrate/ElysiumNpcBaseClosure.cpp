// `CAI_BaseNPC`'s bodies of the `Closure` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseClosure.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumCameraOverride.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Visual/ElysiumActionTables.h"
#include "Visual/ElysiumEyeRig.h"

// --- Moved from `ElysiumNpcClosure.cpp` (story 5 step 5) ---

void FElysiumNpcBase::Slot355()
{
	// `CAI_BaseNPC::FUN_1026cf90`, 143 bytes, all 77 classes. Reading it with the census's names for
	// the three words it touches — `+0x1538 m_GrapplePartner` (EHANDLE), `+0x153c m_GrappleRole`
	// (int), `+0x1540 m_GrappleType` (int) — it is:
	//
	//     ent = m_GrapplePartner.Get();                       // null when the handle is stale
	//     if (!(ent && m_GrappleRole != -1 && m_GrappleType == 8))
	//         m_OnFedUponEnd.FireOutput(ent, this, 0);        // +0x5c20, activator = the partner
	//     FUN_10007ea0(this);
	//
	// The output is fired on THIS entity — the fed-upon one — with the feeder as activator, which is
	// exactly the identity the port fires it with. The `m_GrappleType == 8` arm is the one grapple
	// type that ends without a callback.
	//
	// The port already runs this, in `FElysiumCombatCharacter::CompleteFeedTransaction`
	// (`Substrate/ElysiumFeed.cpp`): `Victim->FireOutput(GOnFedUponEnd, Handle)` on the same pair,
	// in the same direction. This slot forwards there rather than firing the output a second time
	// from a second place — one producer of `OnFedUponEnd` is the whole point, because a map that
	// wires it counts the fires.
	//
	// `CompleteFeedTransaction` is idempotent and re-entry-guarded (`FeedState.bInterrupting`, then
	// `IsPaired()`), so a dispatch with no live transaction performs nothing and fires nothing —
	// which is retail's behaviour for a stale `m_GrapplePartner` too, since the handle resolves to
	// null and the null-activator fire reaches no wire. `bKeepReleaseTail` is false: retail's
	// teardown at this slot keeps no release pose, and `true` is reserved for the anim-event 4006
	// exit that the event handler, not this slot, takes.
	CompleteFeedTransaction(/*bKeepReleaseTail*/ false);
}

const TCHAR* FElysiumNpcBase::GetStateName(EElysiumNpcState State)
{
	// `0x1027e740` is a bare forward to `0x1027e660`, which is the NPC_STATE name table: None,
	// Idle, Combat, Alert, Script, Playdead, Prone, ?, Fleeing, Retreating, Cowering, Hunting,
	// Dialog, Oblivious, CriminalSuspicion, and `__UNKNOWN__` for anything past the end.
	//
	// -> `LexToString(EElysiumNpcState)` (`Public/ElysiumNpcMindTypes.h`), the same table. The port's
	// state vocabulary is the SUBSET of retail's whose transitions have landed (`ElysiumNpcMind.h`
	// says so), and its default answers `"unknown"` where retail answers `__UNKNOWN__` — the same
	// role for the same reason. A state retail names that this enum does not carry cannot be asked
	// for here, because there is no value to ask with.
	return LexToString(State);
}

int32 FElysiumNpcBase::SelectFailSchedule(int32 FailedSchedule, int32 FailedTask, int32 TaskFailCode)
{
	// `CAI_BaseNPC::SelectFailSchedule`, and the whole of it:
	//
	//     int s = m_failSchedule;                 // +0x5c54
	//     if (s == 0) s = 0x43;                   // SCHED_FAIL
	//     return s;
	//
	// **All three arguments are ignored by the retail body.** They are read, on the base line, by
	// nothing: no override in the closure consults them either. They are kept in the signature
	// because they are the slot's, and a species that ever wanted them must be reachable.
	//
	// 29c's target is `ElysiumSchedule.cpp:FailScheduleFor`, which carries this rule as its first
	// arm. That function is FILE-LOCAL (an anonymous namespace in `ElysiumSchedule.cpp`) and cannot
	// be forwarded to; it also does MORE than this slot — it folds in the running program's declared
	// `FailSchedule` and runs `TranslateSchedule` over the answer, which in retail is the CALLER's
	// work (`0x10281730`), not slot 439's. So the slot answers its own two lines, off the same word
	// (`FElysiumScheduleState::FailScheduleOverride`, the shape map's binding for `+0x5c54`) that
	// `FailScheduleFor` reads first.
	(void)FailedSchedule;
	(void)FailedTask;
	(void)TaskFailCode;
	const int32 Override = Schedule.FailScheduleOverride;
	return Override != ElysiumScheduleId::None
		? Override
		: ElysiumSched::FAIL;
}

void* FElysiumNpcBase::GetScheduleOfType(int32 ScheduleNumber)
{
	// `CAI_BaseNPC::GetScheduleOfType`:
	//
	//     idSpace = GetClassScheduleIdSpace();                        // vtable +0x910, slot 580
	//     if (*idSpace == -1) { Warning("ERROR: %s missing schedule!", GetClassname());
	//                           return g_ScheduleTable.Get(1); }      // SCHED_IDLE_STAND
	//     if (n < 1000000000 || n == -1) n = idSpace->Translate(n);   // 0x102ea2d0, local -> global
	//     return g_ScheduleTable.Get(n);                              // thunk_FUN_1030f300
	//
	// The id-space indirection is REAL now, and it is the third line above: a number below 1e9 --
	// and -1 -- goes through this class's schedule space (`ResolveScheduleId`, slot 580) before the
	// table lookup. It used to have no operand, because every program was registered in one flat
	// namespace under the port's own numbering, and a fold from a retail number to one of 29 typed
	// identities stood where the translation belongs.
	//
	// **The miss answers null, not `IDLE_STAND`.** Retail's miss returns schedule 1 and Warnings;
	// the port's equivalent of that whole arm is `ElysiumSchedule::Start`, which records the miss
	// and then installs `IDLE_STAND` — the right place for it, because the substitution is
	// `SetSchedule`'s decision and not the lookup's. A lookup that answered `IDLE_STAND` for every
	// unloaded number would make an unported program indistinguishable from an idle one.
	if (ScheduleNumber == ElysiumScheduleId::None)
	{
		return nullptr;
	}
	return const_cast<void*>(static_cast<const void*>(
		ElysiumScheduleFor(ResolveScheduleId(ScheduleNumber))));
}

EElysiumNpcState FElysiumNpcBase::GetState()
{
	// `0x101a6720`: `return m_NPCState` (`+0x5cc0`). The shape map binds `+0x5cc0` to
	// `FElysiumNpcMind::CurrentState`, which is PRIVATE so that every write goes through
	// `RequestState` and the admission cannot be sidestepped; `FElysiumNpcMind::State()` is its
	// read accessor and returns the word verbatim.
	return Mind.State();
}

bool FElysiumNpcBase::ShouldChooseNewEnemy()
{
	// `CAI_BaseNPC::ShouldChooseNewEnemy`, read arm by arm:
	//
	//     if (m_bfAINPCFlags2 & 0x10000) return true;      // the unnamed declining bit, +0x14bc
	//     if (!GetEnemy()) return true;                    // vtable +0x29c
	//     if (!GetEnemy()->IsAlive()) return true;         // enemy vtable +0x278
	//     if (GetEnemies()->IsEluded(GetEnemy())) return true;
	//     return HasCondition(0x43) || HasCondition(0x45)  // SEE_HATE, SEE_DISLIKE
	//         || HasCondition(0x5b) || HasCondition(0x58);  // SEE_NEMESIS, ENEMY_DEAD
	//
	// -> `ElysiumNpcEnemy::ShouldChooseNewEnemy(Npc, Cond)`, which is the same five tests in the same
	// order and is cited by this address in its own header (`Substrate/ElysiumNpcEnemy.h`). That
	// header also records the two readings a caller would otherwise get wrong: `SEE_FEAR` is
	// deliberately NOT in the list (a fear relation can be chosen but cannot trigger a choice), and
	// the `0x10000` bit is not reproduced because it is unnamed and has no recovered writer, so
	// gating on it would silently disable selection.
	//
	// The conditions come from `Cognition.Conditions`, the set the gather pass rebuilt — retail's
	// `HasCondition` reads the live condition bitfield, which is that same set.
	return ElysiumNpcEnemy::ShouldChooseNewEnemy(*this, Cognition.Conditions);
}

void FElysiumNpcBase::GatherAttackConditions(FElysiumEntity* Enemy, float DistanceUnits)
{
	// `CAI_BaseNPC::GatherAttackConditions`, the SDK body, in its recovered order: the
	// `WAITING_ATTACK_TIME` (0x2f) raise off the weapon's `m_flNextAttack` deadline, the melee and
	// ranged capability calls that answer a condition number each (`0x4f` gets the extra
	// facing/body-target re-test), the blocked-by-friend pair (`m_flWeaponBlockedByFriendTimer`
	// `+0x5b88` and `m_flExtendedBlockedByFriendTimer` `+0x5b8c`, `0x2e` raised once the extended
	// one lapses), and finally the schedule-selection set — clear 8, 0x5f, 0x60, 9 and raise 99, or
	// raise 99 and clear 0x50, 0x4f, 0x52, 0x51 — in that priority order.
	//
	// -> `ElysiumNpcCond::GatherAttackConditions(Npc, Now, Out)`
	// (`Substrate/ElysiumNpcConditions.cpp`), which reproduces it, names its unbuilt melee-selector
	// arms as seams in place, and additionally carries the ONE species override of this slot
	// (`CNPC_VWerewolf`, `0x103d02b0`, a suppression of the melee pair).
	//
	// **The two arguments are the port's own state, not the port's input.** Retail is handed the
	// enemy and its distance by `GatherEnemyConditions`; the port's gather reads the COMMITTED enemy
	// off `BaseMemory.Enemy` and measures the distance itself, so that a headless case can drive
	// the pass without staging a caller. They are accepted and ignored, and a dispatch that passes a
	// DIFFERENT entity than the committed enemy still gathers for the committed one — which is what
	// retail does too, because its caller only ever passes `GetEnemy()`.
	(void)Enemy;
	// Story 29d, family **SpeciesMisc10**: `CNPC_VBach#561` (`0x10363db0`) ADDS the shield, teleport
	// and weapon-switch block IN FRONT of this body and changes nothing it gathers; it is
	// `FElysiumNpcBach`'s override (story 5 step 3), which then calls this body directly.
	(void)DistanceUnits;
	// The gather pass's own clock when the pass dispatched this slot (`GatherPassNow`, set by
	// `ElysiumNpcEnemy::GatherConditions` around the call), else the world's `curtime`.
	const double Now = GatherPassNow >= 0.0 ? GatherPassNow
		: (World != nullptr ? World->NowSeconds() : 0.0);
	ElysiumNpcCond::GatherAttackConditions(*this, Now, Cognition.Conditions);
}
