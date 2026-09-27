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
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Visual/ElysiumActionTables.h"
#include "Visual/ElysiumEyeRig.h"

// --- Moved from `ElysiumNpcClosure.cpp` (story 5 step 5) ---

float FElysiumNpcBase::Slot48()
{
	// `0x10026810` -> `ElysiumCameraOverride::DefaultRollDegrees`. `IElysiumCameraOverrideSource`'s
	// own `GetCameraRoll()` default is the same constant, written FROM this body.
	return ElysiumCameraOverride::DefaultRollDegrees;
}

float FElysiumNpcBase::Slot49()
{
	// `0x10026830` -> `ElysiumCameraOverride::DefaultFieldOfView`.
	return ElysiumCameraOverride::DefaultFieldOfView;
}

void FElysiumNpcBase::SetOrigin(float X, float Y, float Z)
{
	// `0x10026a10`, and the whole of it: build a `Vector` on the stack from the three floats and
	// dispatch `vtable +0xf8` — slot 62, `SetOrigin(const Vector&)`. A compiler-generated forwarding
	// thunk shared unmodified by about 82 classes, not retail logic, which is why 29c's verdict is
	// `mechanism` and names `RTTI:VirtualThunk`.
	//
	// It forwards VIRTUALLY here too, exactly as retail does: slot 62 (`0x100b2be0`) is another
	// story's row and is still a generated stub, so this reaches whatever that slot eventually
	// answers rather than a copy of it. Reproducing the dispatch is the point — a species that
	// overrides slot 62 must be reached through slot 63 as well.
	SetOrigin(FVector(X, Y, Z));
}

void* FElysiumNpcBase::GetPredDescMap()
{
	// `0x10321670` -> `&datamap_CBaseCombatCharacter_10619d10`. REFUSAL: no datamap; the port's
	// save mechanism is `FElysiumSaveArchive`, a per-type `Serialize`, not a descriptor table.
	++ClosureRefusals.PredDescMap;
	return nullptr;
}

bool FElysiumNpcBase::Slot88()
{
	// `0x10026b50` is a bare tail jump into `0x10146700` on the eight-byte tracker at `this+0x1b0`
	// (interval word `+0x4`, countdown `+0x6`, flag bytes `+0x0`/`+0x1`/`+0x2`). `0x10146700` runs
	// the countdown down by the frame delta, re-arms it and raises `+0x2` when it lapses, and
	// answers "a change is pending". Slot 89 (`0x10026b70`) zeroes `+0x1`/`+0x2`, and
	// `SetOrigin` (`0x100b2be0`) sets `+0x1b1`. The tracker's retail NAME is **unrecovered** — no
	// datamap names it and no corpus body declares it.
	//
	// **REFUSAL.** Roughly 80 unrelated non-NPC classes (props, triggers, items) fill this slot with
	// the same body, which is what says it is engine plumbing rather than an NPC rule; it is the
	// networked-edict dirty flag, and this substrate has no replication to dirty. No port member
	// stands `+0x1b0` — the shape map has no row for it, and no other family declared one.
	//
	// `false` is not a placeholder: it is what `0x10146700` itself answers for a ZERO-INITIALISED
	// tracker. With `+0x4` zero the countdown arm never runs, `+0x0` is clear, and the function
	// takes its `(sVar1 == 0)` exit and returns 0. An NPC that never dirtied has no change pending.
	++ClosureRefusals.ChangeTracker;
	return false;
}

void FElysiumNpcBase::Physics_TraceEntity(FElysiumEntity* Entity, const FVector& StartCm,
	const FVector& EndCm, uint32 Mask, void* OutTrace)
{
	// `0x100ab450`, 121 bytes of which 100 are the crash-report breadcrumb push and pop: the body
	// writes `"Physics_TraceEntity"` into the scope-trace stack, calls `thunk_FUN_101cd110` with all
	// five arguments unchanged, and pops. `0x101cd110` issues the trace through the engine trace
	// service's own vtable (`DAT_1070b254 + 0x14`). There is nothing else in the body, which is 29c's
	// `mechanism` verdict and why it names `UWorld::LineTraceSingleByChannel`.
	//
	// **REFUSAL, and it is the out parameter that forces it.** The port HAS a trace seam — the
	// embodiment's `QueryLineOfSight` — but retail's fifth argument is a `trace_t*`, a Source
	// structure (fraction, endpos, plane, surface, hit entity, hitbox, physics bone) that this
	// substrate stands no counterpart for, so the generator could only type it `void*`. Filling a
	// buffer whose layout is not the caller's would be worse than filling none, and answering only
	// the boolean half would silently drop the fraction and the endpos that every retail consumer
	// of this slot reads. So the trace is NOT issued and `OutTrace` is left exactly as the caller
	// handed it in — which the case asserts by passing a sentinel-filled buffer.
	//
	// What it takes to close: a port `trace_t` and the world trace behind it. Then this becomes a
	// forward, and the `Mask` (Source's `MASK_*` content flags) becomes a channel choice.
	(void)Entity;
	(void)OutTrace;
	++ClosureRefusals.PhysicsTraceEntity;
	ClosureRefusals.TraceStartCm = StartCm;
	ClosureRefusals.TraceEndCm = EndCm;
	ClosureRefusals.TraceMask = Mask;
}

void FElysiumNpcBase::MakeTracer(const FVector& StartCm, void* Trace, int32 TracerType)
{
	// `0x10267260`, 186 bytes and 497 classes deep — the stock SDK body. It builds a `CPASFilter`
	// around `param_1` (the tracer's start), and when `param_3 == 1` (`TRACER_LINE`) fires the
	// bullet-tracer temp entity from that point to the trace's `endpos` (`param_2 + 0xc`) with this
	// entity's index as the attachment owner, then unwinds the filter's heap. Every other tracer
	// type builds the filter and fires nothing. A pure visual effect: no condition, no state, no
	// stamp, and nothing downstream reads anything it writes.
	//
	// **REFUSAL, for the same two reasons as slot 102.** `param_2` is a `trace_t&` this substrate
	// has no type for, so the endpos the tracer would be drawn TO cannot be read; and the PAS filter
	// is Source's potentially-audible-set broadcast, whose counterpart here is Niagara plus the
	// engine's own relevance, not a recipient list. 29c's `mechanism` target
	// (`UGameplayStatics::SpawnEmitterAtLocation`) is the right eventual home and the effect is
	// visual-only, so adopting it changes no event order — but it needs the endpos first.
	(void)Trace;
	++ClosureRefusals.MakeTracer;
	ClosureRefusals.TracerStartCm = StartCm;
	ClosureRefusals.TracerType = TracerType;
}

FVector FElysiumNpcBase::WorldSpaceCenter()
{
	// `0x10027160` -> `ElysiumCameraShots::SurroundingBounds`, the port's one bounds accessor.
	return ElysiumCameraShots::SurroundingBounds(*this).GetCenter();
}

void* FElysiumNpcBase::WorldSpaceCenter() const
{
	// `0x100b4c30`, the `const Vector&` overload of the SAME body. The value is slot 192's; what is
	// different is that retail hands out an ADDRESS, into its rotating temp-vector ring. The port
	// caches into `WorldSpaceCentreCacheCm` instead — a named modernization stated in full at the
	// member's declaration in `Substrate/ElysiumNpcClosure.inl`.
	WorldSpaceCentreCacheCm = ElysiumCameraShots::SurroundingBounds(*this).GetCenter();
	return &WorldSpaceCentreCacheCm;
}

void FElysiumNpcBase::VPhysicsDestroyObject()
{
	// `0x100b5040`, 47 bytes, 497 classes: when `m_pPhysicsObject` (`+0x36c`) is set, unregister it
	// from the physics-object list (`thunk_FUN_1002ee60`), destroy it (`thunk_FUN_10158520`) and
	// null the pointer. Stock SDK teardown with no VtMB-specific rule.
	//
	// **REFUSAL.** There is no `m_pPhysicsObject` here. This substrate stands no rigid body: the
	// shape map has no row for `+0x36c`, the collision surface it would tear down is Unreal's own
	// (`UPrimitiveComponent::DestroyPhysicsState`, which the engine calls on component destruction
	// without being asked), and no port system holds a physics handle to release. With the pointer
	// null retail's body is `return;` — so answering nothing is not merely the port's answer, it is
	// retail's for an NPC that never got a physics object, which is every NPC that was never
	// ragdolled.
	++ClosureRefusals.VPhysicsDestroyObject;
}

float FElysiumNpcBase::SetPoseParameter(int32 Index, float Value, bool bWrap)
{
	// `CBaseCombatCharacter::SetPoseParameter`, 413 bytes, 85 classes — the stock SDK LOOPING
	// pose-parameter setter. It walks the two-entry registry at `m_flSet_PoseParameters`, and if the
	// asked-for index is one of them it stores the value and, when `bWrap` is set and the model
	// resolves, wraps it using that pose parameter's own bounds (`+0x8`, `+0xc`, `+0x10` off the
	// `mstudioposeparamdesc_t`) and the fixed SDK wrap fraction `_DAT_10449270`. An index that is
	// NOT in the registry falls through the loop to `CBaseAnimating::SetPoseParameter02` — slot 260,
	// `0x10091fe0` — which is the ordinary non-looping setter.
	//
	// **The fall-through is the arm this runtime can take, and it is retail's own.** The registry is
	// filled from the model's studio header, this substrate's animating tier stands no
	// `studiohdr_t` and therefore no pose-parameter descriptors, so no index is ever a registered
	// looping parameter and every call takes the miss. That is a refusal of the WRAP, not of the
	// write: the value still goes where retail sends it on a miss.
	(void)bWrap;
	++ClosureRefusals.LoopingPoseParameter;
	return SetPoseParameter02(Index, Value);
}

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

bool FElysiumNpcBase::FInViewCone(const FVector& PointCm)
{
	// `0x10326a20` -> `FElysiumNpcSenses::IsInViewCone(Npc, point)`, the base 3-D apex test. The
	// scope-trace push and pop around it are the crash-report breadcrumb stack and have no
	// observable effect. The target cone scalar defaults to 1.0 because a POINT carries no stealth
	// surface — retail's point overload does not read one either.
	return FElysiumNpcSenses::IsInViewCone(*this, PointCm);
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
