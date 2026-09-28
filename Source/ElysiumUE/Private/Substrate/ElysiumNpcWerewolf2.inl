// Story 0019/8 (29e under the strict verdict), family **Werewolf19** -- `CAI_BaseNPCTroika`'s
// helper declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcWerewolf19.cpp`, or generated in the slot files for a slot body.
//
// Owns (Werewolf19's `rule` rows): 0x102c44e0 SetFollowerBoss, 0x103cac20 FUN_103cac20, 0x102c4430
// FUN_102c4430, 0x10397380 FUN_10397380.

// --- Story 8, lane L12: the Troika-line helpers of Werewolf19 -----------------------------------
//
// No slot holds these. Bodies in `ElysiumNpcWerewolf19.cpp`; walked prose in
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Story 8, family Werewolf19".

/** `0x102c44e0` `SetFollowerBoss(const char*)`: slot 559 `FindNamedEntity` resolves the name into
 *  `m_hFollowerBoss` (`+0x647c`); a miss, a dead handle, this NPC itself, or a connected squad member
 *  refuses (the squad arm with retail's `Error`), and the acceptance runs `ResetAiState(false, false)`
 *  (`0x102b52a0`) and ORs `0x3008` into `m_bfNPCFrenziedFlags` (`+0x5b84`). Answers acceptance. */
bool SetFollowerBoss(const FString& BossName);

/** `0x102c4430` `SetFollowerBossName(const char*)`: `SetFollowerBoss(name)` then
 *  `m_sFollowerBoss` (`+0x6478`) := the name, NULL (empty) for an empty string. The string overload
 *  of family Squad's `SetFollowerBossName(const FElysiumEntity*)` (`0x102c4470`), exactly as retail
 *  overloads it. */
void SetFollowerBossName(const FString& BossName);

/** `0x10397380` — hand this NPC's slot-167 enemy to `Ally`: `Ally`'s entity relationship to it
 *  (`D_HT`, priority 5), `Ally`'s slot 596 with it, `Ally`'s `COND_NEW_ENEMY` (`0x54`), then — only
 *  when THIS NPC is in a connected squad — `SquadNewEnemy`. The checklist's name. */
void ShareEnemyWithAlly(FElysiumNpc* Ally);
