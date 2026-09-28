// Story 0019/8 (29e under the strict verdict), family **Script19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseScript19.cpp`, or generated in the slot files for a slot body.
//
// Owns (Script19's `rule` rows): 0x1027d0a0 FUN_1027d0a0 (the checklist names it on
// `FElysiumNpc`; its every read is a `CAI_BaseNPC` word, so it lands on the base).

/** `0x1027d0a0` (`ExitScriptedSequence`, name coined by the checklist; no retail symbol). Reached from
 *  `SelectIdealState`'s SCRIPT case (`0x1026f660` / Troika `0x102ad660`) when the running script is
 *  interrupted. A DYING NPC (`m_lifeState == LIFE_DYING`) gets `m_IdealNPCState := 7 DEAD` with the
 *  selector trace line `0x2a70` and answers FALSE, leaving `m_hCine` installed; any other NPC runs
 *  `CancelScript` (`0x101a8c30`) on a live `m_hCine` and answers TRUE, the no-script case included. */
bool ExitScriptedSequence();

/** `m_lifeState (+0x200) == LIFE_DYING (1)`, read off the word (`AnimEventLifeStateWord`), with the
 *  entity not yet removed. */
bool LifeStateIsDying() const;

/** The selector-trace source line `0x1027d0a0` stamps into `+0x1b40` (`AI_BaseNPC.cpp`). */
static constexpr int32 ExitScriptDyingLine = 0x2a70;
