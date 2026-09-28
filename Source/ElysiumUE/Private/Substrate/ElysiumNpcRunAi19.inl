// Story 0019/8 (29e under the strict verdict), family **RunAi19** -- `CAI_BaseNPCTroika`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcRunAi19.cpp`, or generated in the slot files for a slot body.
//
// Owns (RunAi19's `rule` rows): 0x1028fd80 CAI_BaseNPCTroika::RunAlternateAI, 0x1028fcc0
// CAI_BaseNPCTroika::RunAI.

/** `CAI_BaseNPCTroika::RunAlternateAI` (`0x1028fd80`, `RET 4`), which `NPCThink` (`0x10292de0`) asks
 *  before slot 432 with the same `bReduced` byte and runs `RunAI` only on a FALSE answer. Head arm:
 *  a live `m_GrapplePartner` (`+0x1538`) with `m_GrappleRole` (`+0x153c`) == 1 (the victim) runs
 *  `AutoMovement` for nine ideal activities and answers TRUE either way; otherwise `m_eAlternateAI`
 *  (`+0x644c`) 1..4 hands the pass to its door-transaction arm, anything else answers FALSE.
 *
 *  Retail casing on purpose: `RunAlternateAi(double)` (`ElysiumNpc.cpp`) is the port's twin that the
 *  loop rewire deletes; this is the retail body. */
bool RunAlternateAI(bool bReduced);

/** `FUN_1028fc90`, the per-pass stealth-surface reset `RunAI` (`0x1028fcc0`) runs first:
 *  `m_flStealthHearingDist (+0x63cc) = 0`, `m_flStealthVisionScalar (+0x63c4) = 1.0`,
 *  `m_flStealthVisionCone (+0x63c8) = 1.0`, in that store order. A discipline's modifier therefore
 *  survives exactly one AI pass. */
void RunAi19ResetStealthSurface();

/** `FUN_10290200` (`RET 4`, argument unused), `m_eAlternateAI` mode 2 -- the door opened, wait out
 *  the expiry: `MaintainActivity`; before `m_flAlternateAIExpireTimer` (`+0x6450`) nothing more; at
 *  or after it, a still-live `m_hOpeningDoor` (`+0x5d24`) fails the task with 0xe and clears the
 *  three transaction words, while a gone door re-plans (`0x102ee2e0` -> `0x102bf7e0`, then slot 528
 *  `ValidateNavGoal`) and clears the mode only. Answers TRUE on every path. */
bool RunAi19AlternateAiMode2(double Now);

/** `FUN_102902e0` (`RET 4`, argument unused), `m_eAlternateAI` mode 3 -- the door blocked:
 *  `MaintainActivity`; at or after `m_flAlternateAIExpireTimer`, `TaskFail(0xe)` and clear the three
 *  transaction words. Answers TRUE on every path. */
bool RunAi19AlternateAiMode3(double Now);

// --- The forward-obstruction reaction three species' slot 432 share ---------------------------
//
// `CNPC_VTzimisce` `0x103bdef0`, `CNPC_VGargoyle` `0x10378b80` and `CNPC_VHengeyokai` `0x10380120`
// each carry their own copy of one sweep and two reactions (Tzimisce `0x103c0160` / `0x103c0860` /
// `0x103c05e0`, Gargoyle `0x103796a0` / `0x10379e80` / the tail of `0x10379b40`, Hengeyokai
// `0x10380fc0` / `0x103816e0` / `0x10381460`); they differ only in the ConVars they read and in
// the step-height re-trace the Gargoyle and Hengeyokai sweeps add. One body each here, with the
// species' own constants passed in.

/** The forward hull sweep (`RET 4`, one out-int): from slot 220 `GetOrigin` raised by 4.0
 *  (`_DAT_10450aa0`) along slot 221's forward (`AngleVectors 0x10139610`) scaled by the species'
 *  lookahead ConVar, `m_Collision`'s box, mask `0x202400b`, `CTraceFilterSimpleTwoEnt(this,
 *  GetIgnoreCollisionEntity(), m_CollisionGroup +0x368)`. A clear trace, the world entity, or a hit
 *  slot 69 `NavIgnoreCollision` refuses answers NULL and leaves `OutKind` unwritten. Without the
 *  re-trace (Tzimisce) a blocker writes kind 0. With it (Gargoyle, Hengeyokai) the same sweep
 *  raised by slot 522 `StepHeight` on both ends decides: clear, world, or a hit slot 69 refuses ->
 *  kind 1 (step up), else kind 0. Answers the FIRST trace's entity. */
FElysiumEntity* RunAi19ObstructionSweep(float LookaheadUnits, bool bRetraceAtStepHeight, int32& OutKind);

/** SEAM for the `tr.m_pEnt == GetWorldEntity()` test (`0x1023bd00` answers `DAT_107532e8`, written
 *  by `CWorld::Precache`). The port stands no world ENTITY (world geometry is Unreal's), so nothing
 *  is it: answers false. A null hit is read as the world (crash guard: retail's `m_pEnt` is never
 *  null on a hit). */
bool RunAi19IsWorldEntity(const FElysiumEntity* Entity) const;

/** The step-up (`0x103c0860` / `0x10379e80` / `0x103816e0`, 74 bytes each, `RET 4`, the blocker
 *  unused): slot 62 `SetOrigin(GetOrigin() + (0, 0, StepHeight()))`, slot 522 in Source units. */
void RunAi19ObstructionStepUp();

/** The physics push (`0x103c05e0` / `0x10381460` / `0x10379b40`'s tail): a null blocker or one with
 *  no `m_pPhysicsObject` (`+0x36c`) is left alone; else this body's own slot 199 velocity rotated
 *  -90 degrees (`(v.y, -v.x, v.z)`, not normalised), reversed on all three components when a
 *  world-only line from the blocker's origin along it hits anything, scaled by `Scalar`, `ZPush`
 *  added to z, and handed to the blocker's `IPhysicsObject` slot 41 (`AddVelocity(&v, &zero)`). */
void RunAi19ObstructionPush(FElysiumEntity* Blocker, float Scalar, float ZPush);

/** SEAM for the blocker's `m_pPhysicsObject` (`+0x36c`). No port word stands for a generic entity's
 *  physics object; an NPC blocker answers its own `bHasPhysicsObject` (family Senses10's stand-in),
 *  anything else false -- retail's own no-object arm. */
bool RunAi19BlockerHasPhysicsObject(const FElysiumEntity* Blocker) const;

/** SEAM for `IPhysicsObject` slot 41 (`+0xa4`, `AddVelocity(&velocity, &angular)`, inferred from
 *  the SDK pattern of `CBaseCombatWeapon`'s drop `0x10252a90`) on the blocker. No kernel physics
 *  impulse stands; the SOURCE-unit velocity is recorded and nothing moves. */
void RunAi19BlockerAddVelocity(FElysiumEntity* Blocker, const FVector& VelocityUnits);
int32 RunAi19BlockerPushes = 0;
FVector RunAi19LastBlockerPushUnits = FVector::ZeroVector;
