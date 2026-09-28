// Story 0019/8 (29e under the strict verdict), family **Misc19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseMisc19.cpp`, or generated in the slot files for a slot body.
//
// Owns (Misc19's `rule` rows): 0x10279dd0 CAI_BaseNPC::ChooseEnemy, 0x1026cdc0
// CAI_BaseNPC::EnterGrappleState, 0x1026cec0 CAI_BaseNPC::FUN_1026cec0, 0x10274e30
// CAI_BaseNPC::HandleAnimEvent.

/** `CAI_BaseNPC::EnterGrappleState` (`0x1026cdc0`), slot 379's base body, 71 bytes: `0x1026d130`
 *  (`SetEnemy(NULL)`, `DisconnectFromSquad`, `++m_iIsOblivious`), `m_OnGrappleBegin` (`+0x5bd8`)
 *  with the partner as activator, then `CBaseCombatCharacter::EnterGrappleState` (`0x10329760`)
 *  whose answer it leaves in `AL`. No grapple-type gate anywhere. `CPayphone#379` (`0x101aade0`)
 *  calls it directly; the Troika fill `0x102b5c00` calls it after its queued-burn refusal. */
virtual bool EnterGrappleState(const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role,
	EElysiumGrappleType Type, int32 Position = INDEX_NONE, bool bHolster = true) override;

/** `0x10279b70`, `SetEnemy`'s last-enemy helper (retail name unrecovered): `m_hLastEnemy`
 *  (`+0x1a94`, `BaseMemory.LastEnemy`) := the entity's handle, or `-1` for null. Its callers are
 *  `SetEnemy` `0x10279a50` and the two slot-598 forget routes (`0x102b4fe0`, `0x10372dd0`). */
void SetLastEnemy(FElysiumEntity* Entity);

/** SEAM for `thunk_FUN_101e3d70(&DAT_10739a4c, this)` as `SetEnemy` (`0x10279b0c`) runs it on every
 *  non-null enemy write: the discipline manager's break-on-notice sweep (`RemoveEffect`
 *  `0x101e3af0` on every discipline whose record carries a non-zero byte at `+0x32`). The same
 *  sweep's other site is `SelectIdealState` case 3 (`SelectIdealStateDisciplineStripCalls`,
 *  `ElysiumNpcBaseState19.inl`); the break flag is unrecovered and `FElysiumDisciplines` has no
 *  "strip the notice-breaking effects" accessor, so it is counted and strips nothing. */
int32 SetEnemyDisciplineStripCalls = 0;

/** `0x102707d0` (retail name unrecovered), 36 bytes: an entity whose `+0x98` Troika sub-object
 *  (`AsNpc()`) answers slot 138 `Classify() == 3` is replaced by that sub-object's slot 97
 *  `GetOwnerEntity()` — a summoned body hands the hate to its summoner; everything else, null
 *  included, passes through. Callers in this family: `ChooseEnemy` `0x10279ffc`, slot 596
 *  `0x102b4f6e`, `CNPC_VCop#596` `0x10372c5e`. */
static FElysiumEntity* SummonerRedirect(FElysiumEntity* Entity);

/** `CAI_Enemies::ClearMemory` (`0x102dfaa0`) on slot 541 `GetEnemies()`: walk the record list
 *  (`+0xc`, next `+0x38`), unlink and free the first record whose handle (`+0x24`) resolves to the
 *  entity (`FElysiumNpcEnemyMemory::ClearMemory`). The notify through the list's `+0x0` vtable
 *  `+0xe0` or `0x103169a0` (by the byte at `+8`) has no port target and stays absent; the calls
 *  are counted for the tests. The `"%s(%d) :"` reason string retail formats is debug text. */
void ClearEnemyMemoryRecord(FElysiumEntity* Entity);
int32 ClearEnemyMemoryRecordCalls = 0;
FElysiumEntityHandle LastClearedEnemyMemoryRecord;

/** Slot 448 `TaskFail` with retail's text code (`MakeFailCode(const char*)`: the string pointer IS
 *  the code, so it matches no numbered fail code). The port's `TaskFail(int32)` takes a number;
 *  this stores `TextTaskFailCode` (outside every numbered code) and keeps the text beside it. Used by
 *  the two `0x7f8` NPC_PICKUP arms (`0x10275384`, `0x1029b3b3` and their siblings). */
void TaskFailText(const TCHAR* Text);
static constexpr int32 TextTaskFailCode = 0x7fffffff;
FString LastTaskFailText;

/** `CAI_BaseNPC::HandleAnimEvent` (`0x10274e30`), slot 259's base body: the two-band switch (the
 *  `0x3e8..0x3fe` script band and the `0x7d1..0x805` band) and the default route to
 *  `CBaseCombatCharacter::HandleAnimEvent` / `Weapon_HandleAnimEvent`. Every id the switch names
 *  answers claimed, its guard failures included (retail returns without the base). */
virtual bool HandleAnimEvent(const struct FElysiumAnimEvent& Event) override;

/** `CBaseCombatCharacter::Weapon_HandleAnimEvent` (`0x1032e210`): a live `m_hActiveWeapon` gets the
 *  event through its `Operator_HandleAnimEvent` (`+0x5c8`). The port's combat character inlines the
 *  same forward for 3000..3999; the base's default route needs it for 3000..0xfa2. */
bool WeaponHandleAnimEventMisc19(const struct FElysiumAnimEvent& Event);

/** SEAM for `CBaseEntity::EmitSound(const char* soundscript)` (`0x101b0c10`): `IElysiumAudio`
 *  resolves no game_sounds script, so the names the body asks for are recorded, in order. */
void EmitSoundScriptMisc19(const FString& SoundScript);
TArray<FString> EmittedSoundScripts;

/** `0x102d09b0` (`CAI_Hint`'s anim-event output, retail name unrecovered): `0 < n < 9` fires the
 *  hint's `OnAnimEvent<n>` (`+0x4e4 + 0x18n`) with this NPC as activator. `HintNode` is the port's
 *  hint reference (the entity index). Callers: the base body's 0x3eb arm (`0x10274fd0`) and
 *  `CNPC_VWerewolf::HandleAnimEvent`'s (`0x103d89d6`). Answers whether it fired. */
bool FireHintAnimEvent(int32 HintNode, int32 N);

/** SEAM for `CBaseCombatWeapon::OnPickedUp` (`0x10252c10`, weapon slot 341), which `0x7f8` runs on
 *  the weapon before `Weapon_Equip`: its `m_OnNPCPickup` output, weapon slot 225 and the solid-flag
 *  work have no port. Counted. */
int32 WeaponOnPickedUpCalls = 0;

/** SEAM for the weapon-MODEL sequence arms `0x7fa`/`0x7fb` (`LookupSequence` `0x1008f7b0` on the
 *  active weapon, then `0x10260a50` `ResetSequence` on it) and `0x7fc`'s weapon `LookupActivity`
 *  (`0x1008f6c0`): the port animates no weapon model, so a lookup answers -1 (retail's refusal) and
 *  the request is recorded. */
int32 WeaponModelSequenceRequests = 0;
