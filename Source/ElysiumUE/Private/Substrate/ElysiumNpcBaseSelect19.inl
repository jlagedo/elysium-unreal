// Story 0019/8 (29e under the strict verdict), family **Select19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseSelect19.cpp`, or generated in the slot files for a slot body.
//
// Owns (Select19's `rule` rows): 0x1028a380 CAI_BaseNPC::SelectSchedule.

/** `+0x1b2c` `m_SelectScheduleTrace.m_iSelector` — the selector id every slot-437 / 438 body writes
 *  first (1 base, 2 Troika, a class tag for a species body; `0x1028a260` writes 0). The agreed
 *  visual-only modernization of pass I: the selector id is carried as a real word for the debugger
 *  to render; the `__FILE__` / `__LINE__` pair beside it (`+0x1b30` / `+0x1b34`) stays ABSENT and
 *  each retail exit's line is recorded through `RecordScheduleEvent` instead (`SelectTrace`). */
int32 SelectScheduleSelector = 0;

/** How many times `CAI_BaseNPC::SelectSchedule` case 7 asked `BecomeClientRagdoll` (`0x10090180`). */
int32 SelectRagdollRequests = 0;

/** `m_SelectScheduleTrace`'s file/line pair as a trace row, then `Schedule` back. `File` is the
 *  retail source file the exit stamps (`AI_BaseNPC.cpp`, `AI_BaseNPCTroika.cpp`, `NPC_V*.cpp`),
 *  `Line` its `__LINE__`. Returns `Schedule` so an exit reads `return SelectTrace(...)`. */
int32 SelectTrace(const TCHAR* File, int32 Line, int32 Schedule);

/** `CAI_BaseNPC::SelectSchedule` (`0x1028a380`), slot 438's base body: the per-state ladder the
 *  Troika switch's `default:` and every Troika state it does not own tail-jump into
 *  (`0x10015596` / `0x10015ad2`). Not virtual: slot 438's dispatch on the Troika line is
 *  `FElysiumNpc::SpeciesSelectSchedule`, and a base-only NPC reaches this through
 *  `SelectNewScheduleRetail`. */
int32 BaseSelectSchedule();

/** `0x1028a260`, the selector pair `GetNewSchedule` (`0x102814d0`) and the two re-entries of the
 *  selectors themselves call: `+0x1b2c = 0`, slot 437 `PreSelectSchedule`, and — only when that
 *  answered 0 — slot 438 `SelectSchedule` as a tail jump. No verdict row of its own (38 bytes, no
 *  stamps); ported here because three Select19 bodies re-enter through it. */
int32 SelectNewScheduleRetail();

/** SEAM for `CBaseAnimating::BecomeClientRagdoll(&DAT_1070d1b0, -1, 0)` (`0x10090180`), the
 *  base selector's state-7 test. This substrate's ragdoll handoff is `CompleteDeathHandoff`, run
 *  by the death transaction and not by a selector, and the character bake writes no physics asset
 *  (`FElysiumNpcBase::CompleteDeathHandoff`), so the client ragdoll never forms: answers false,
 *  which is the arm that selects `DIE` 0x2b — the schedule `FElysiumNpc::OnKilled` already starts. */
bool SelectBecomeClientRagdoll();
