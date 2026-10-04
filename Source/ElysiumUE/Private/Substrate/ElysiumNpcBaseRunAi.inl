// Story 0019/8 (29e under the strict verdict), family **RunAi19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseRunAi.cpp`, or generated in the slot files for a slot body.
//
// Owns (RunAi19's `rule` rows): 0x1026f110 CAI_BaseNPC::RunAI.

/** `m_hDialogPartner` (`+0x0fe8`) resolving to a live entity -- the gate `RunAI` (`0x1026f1f0` ..
 *  `0x1026f219`) skips `GatherConditions` on. The word is `CBaseCombatCharacter`'s
 *  (`GetDialogPartner()`, written by `SetDialogPartner 0x10107050`), resolved by
 *  `FElysiumNpc::HasLiveDialogPartner` (family Anim). On an NPC only `StartTalking 0x102c0270` sets
 *  it, so a base-only NPC -- never a conversation's owner -- reads retail's invalid-handle arm. */
bool RunAi19DialogPartnerLive() const;

/** SEAM for `CBaseEntity::AddTimedOverlay` (`0x1009f3c0`), the debug-overlay text `RunAI` pushes at
 *  `0x1026f1e1` under `developer != 0` for an NPC whose navigator is off the network. No debug-overlay
 *  service stands on the kernel; the text and the duration are recorded, nothing is drawn. */
void RunAi19AddTimedOverlay(const TCHAR* Text, float DurationSeconds);
FString RunAi19LastOverlayText;
float RunAi19LastOverlaySeconds = 0.f;
int32 RunAi19OverlayCalls = 0;

/** `m_bGroundSpeedFromIntervalMovement` (`CBaseAnimating +0x05ac`, layout row), which
 *  `CNPC_VZombie`'s slot 432 (`0x103df9b1`) sets to 1 on every exit. No port word stood for it (the
 *  shape map carries no `+0x05ac` row) and no port reader consumes it: the animating tier's
 *  ground-speed source is Unreal's. Carried so the write is the retail one. */
bool bGroundSpeedFromIntervalMovement = false;   // +0x05ac (layout)

/** `FUN_102ee220` with its answer: `m_pPath->SetGoalPos(pos)` (`0x1030b950`) then the route build
 *  `0x102f1dc0(this, 0, 0)` whose byte it returns (`0x102ee236`, `RET 4`). The port's surface for
 *  the call is Family RunTask19's `NavUpdateGoalPos` (a recording SEAM: the mover exposes no goal
 *  re-aim), which is called; its answer is the admitting TRUE -- the caller's no-failure arm --
 *  because no re-aimed route stands to refuse. */
bool RunAi19NavUpdateGoalPos(const FVector& GoalCm);
