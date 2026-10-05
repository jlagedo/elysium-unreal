// Story 0019/8 (29e under the strict verdict), family **Select19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseSelect.cpp`, or generated in the slot files for a slot body.
//
// Owns (Select19's `rule` rows): 0x1028a380 CAI_BaseNPC::SelectSchedule.

/** How many times `CAI_BaseNPC::SelectSchedule` case 7 asked `BecomeClientRagdoll` (`0x10090180`). */
int32 SelectRagdollRequests = 0;

/** `CAI_BaseNPC::SelectSchedule` (`0x1028a380`), slot 438's base body: the per-state ladder the
 *  Troika switch's `default:` and every Troika state it does not own tail-jump into
 *  (`0x102b0be3 JMP 0x10001d57`). Not virtual: slot 438's dispatch on the Troika line is
 *  `FElysiumNpc::SpeciesSelectSchedule`, and a base-only NPC reaches this through
 *  `SelectNewScheduleRetail`. */
int32 BaseSelectSchedule();

/** `0x1028a260`, the selector pair `GetNewSchedule` (`0x102814d0`) and the two re-entries of the
 *  selectors themselves call: `+0x1b2c = 0`, slot 437 `PreSelectSchedule`, and — only when that
 *  answered 0 — slot 438 `SelectSchedule` as a tail jump. No verdict row of its own (38 bytes, no
 *  stamps); ported here because three Select19 bodies re-enter through it. */
int32 SelectNewScheduleRetail();

/** `0x1028a8ec..0x1028a92b`: execute the zero-force, explicit bone -1, flag 0 transaction;
 *  source-rig success selects 0x2c, refusal selects 0x2b. Ordinary corpse think never reaches it. */
bool SelectBecomeClientRagdoll();
