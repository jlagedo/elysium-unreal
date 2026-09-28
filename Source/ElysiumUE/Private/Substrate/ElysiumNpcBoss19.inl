// Story 0019/8 (29e under the strict verdict), family **Boss19** -- `CAI_BaseNPCTroika`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcBoss19.cpp`, or generated in the slot files for a slot body.
//
// Owns (Boss19's `rule` rows): 0x102b52a0 FUN_102b52a0, 0x102c51a0 DoPossession, 0x102c5310
// DoFrenzy, 0x103830e0 FUN_103830e0, 0x10395c70 FUN_10395c70, 0x1039e970 FUN_1039e970, 0x10397410
// FUN_10397410, 0x10397e90 FUN_10397e90, 0x10397f00 FUN_10397f00, 0x10395750 FUN_10395750.

// --- Story 8, lane L12: the Troika-line helpers of Boss19 ----------------------------------------
//
// No slot holds these. Bodies in `ElysiumNpcBoss19.cpp`; walked prose in
// `docs/vtmb/npc-ai/authored-control.md` § "Story 8, family Boss19, the discipline helpers".
// The two bools of `ResetAiState` are retail's two `char`
// arguments, kept as the codebase's other retail helpers keep theirs (`SetSchedule`, `SetMoveHint`).

/** `0x102b52a0` — the AI teardown `DoPossession`, `DoFrenzy` and `SetFollowerBoss` share: drop the
 *  enemy and the last enemy, strip `m_afMemory` (`+0x5d8c`) with `0xf7fc7fff`, re-apply the authored
 *  relationship line when `bReapplyRelationships` (`0x10273760`), clear the enemy store (`0x102dfc10`),
 *  and, when `bSetIdleIdeal`, write `m_IdealNPCState` = 1 directly. The checklist's name.
 *
 *  CORRECTION to the checklist: `0x10273760` is not a hint release — its whole body is
 *  `InputSetRelationship(this, m_RelationshipString (+0x1584) ?: "", 0)`. */
void ResetAiState(bool bReapplyRelationships, bool bSetIdleIdeal);

/** `0x102c51a0` `DoPossession(CBaseEntity* caster)`. A null caster is a no-op. */
void DoPossession(FElysiumEntity* Caster);

/** `0x102c5310` `DoFrenzy(CBaseEntity* caster)`. A null caster is a no-op. */
void DoFrenzy(FElysiumEntity* Caster);

/** SEAM for `m_RelationshipString` (`+0x1584`), the authored `Relationship` keyvalue
 *  `0x10273760` re-applies. The port parses the line through `InputSetRelationship` at spawn and keeps
 *  no string member (`ElysiumNpcKernelBindings.cpp` UNBOUND row); this answers the authored key from
 *  the entity's own def, which is the string retail stored, or empty. */
FString AuthoredRelationshipString() const;

/** `0x10273760` — `InputSetRelationship(this, m_RelationshipString (+0x1584) ?: "", 0)`: re-apply the
 *  authored relationship line. Reached from `ResetAiState` and from `NPCInitThink` `0x10273aa0`. */
void ReapplyRelationshipString();

/** `0x10273aa0` — the think `NPCInit` installs for the map's first second (`ThinkSet(NPCInitThink)`,
 *  `curtime + 0.1`): `0x10273760`, slot 422 `StartNPC`, then a tail jump into slot 421
 *  `PostNPCInit`. `StartNPC` re-installs the ordinary think (`LAB_1000f4e8`, `CallNPCThink` ->
 *  slot 431), so `NPCThink` runs from the NEXT think on. */
void NpcInitThink();

/** `0x102dfc10` over slot 541 `GetEnemies()`: the `CAI_Enemies` clear-all. Retail walks the record
 *  list, hands each record and the reason to the owner's forget callback (slot 56; the squad's
 *  `0x103169a0` is unreached, no squad-shared store stands) and frees it. The store is emptied
 *  (keeping `FreeKnowledgeDuration`, which the clear does not touch), then slot 56 runs per popped
 *  record in list order; the count and reason are recorded. */
void ClearEnemyMemoryStore(const FString& Reason);
/** How many records the last `ClearEnemyMemoryStore` dropped, and the reason it was handed. */
int32 EnemyStoreClearedRecords = INDEX_NONE;
FString EnemyStoreClearReason;

/** `m_lifeState` (`+0x200`), which the Boss19 death entries write LIFE_DYING (1) into, is family
 *  the entity's `LifeState` (`ElysiumEntity.h`): one word (StartTask19 integration). */
