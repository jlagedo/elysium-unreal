// Story 0019/8 (29e under the strict verdict), family **Damage19** -- `CAI_BaseNPCTroika`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcDamage3.cpp`, or generated in the slot files for a slot body.
//
// Owns (Damage19's `rule` rows): 0x1029fa50 CAI_BaseNPCTroika::FUN_1029fa50, 0x1029fcf0
// CAI_BaseNPCTroika::PlayerDefenderBlockReaction, 0x102a01b0
// CAI_BaseNPCTroika::PlayerKnockbackReaction, 0x102beda0 CAI_BaseNPCTroika::OnTakeDamage.

// --- Words -----------------------------------------------------------------------------------

/** `+0x660c m_LastTakeDamageInfo` — the 0x4c-byte `CTakeDamageInfo` the Troika's slot-390 body
 *  (`0x102beda0`) caches verbatim at `0x102bedab..0x102bee48` before anything else runs. The port's
 *  packet carries the words `FElysiumTakeDamageInfo` declares (`+0x00`, `+0x2c`, `+0x30`, `+0x38`,
 *  `+0x40`); the force/position vectors (`+0x04..+0x24`), `+0x28`, `+0x34`, `+0x3c`, `+0x44` and the
 *  three bytes `+0x48..+0x4a` have no port word and are not cached. `+0x40` is what
 *  `RetailLastDamageInfoWord40` (`0x101c2a30`, family Debug10) reads. */
FElysiumTakeDamageInfo LastTakeDamageInfo;

// `CBaseCombatCharacter`'s three Presence words, which the three species `UpdatePresenceEffect`
// bodies (slot 313: `0x1037a5b0`, `0x10381b10`, `0x103ab270`) zero. No port word stood for any of
// them (the kernel shape lists them under `CAI_BaseNPCTroika` with no shape-map row).
int32 FriendPresenceEffect = 0;      // +0x0e80 m_iFriendPresenceEffect (datamap)
float EnemyPresencePercent = 0.f;    // +0x0e84 m_flEnemeyPresencePercent (datamap, retail spelling)
int32 EnemyPresenceEffect = 0;       // +0x0e88 m_iEnemyPresenceEffect (datamap)

// --- Seams -----------------------------------------------------------------------------------

/** SEAM for `CBaseCombatCharacter::AddExpressionForEvent(0)` (`0x101072b0`), the facial expression
 *  the Troika's slot-390 body asks for on a positive hit (`0x102bee8e`). This runtime names its
 *  expressions (`FElysiumAnimating::RefreshDispositionExpression`) and has no event-indexed table,
 *  so the request is counted and the event id kept. */
int32 AddExpressionForEventCalls = 0;
int32 LastExpressionEvent = INDEX_NONE;
void AddExpressionForEvent(int32 Event);

/** SEAM for `FUN_102daf50` (`m_pInterestingPlace`'s "has a death activity": `place+0x548` type row,
 *  then `0x102dd610`, the type row's byte at `+0x19c`). `FElysiumInterestingPlaceType` carries no
 *  death-activity word (the `interestingplacetypelist.txt` key behind `+0x19c` / `+0xd0` is
 *  unrecovered), so a place with a resolved type answers false, which is retail's own answer for a
 *  type row whose byte is 0. */
bool InterestingPlaceHasDeathActivity(const FElysiumInterestingPlace& Place) const;

/** SEAM for `FUN_102daf20` (`place+0x548` then `0x102dd580`, the type row's death-activity name at
 *  `+0xd0`/`+0xd4`). The empty string `&DAT_106b8540` is retail's own answer for a null type row;
 *  with no port word for the name it is the answer for every row. */
FString InterestingPlaceDeathActivityName(const FElysiumInterestingPlace& Place) const;

/** `CBaseCombatCharacter::IsMeleeSwingInRange` (`0x10345760`) on `Swinger`: the 2-D Source-unit
 *  distance from `Swinger`'s `GetAbsOrigin()` to `PointCm` is at most `_DAT_1049e048` (100.0) plus
 *  the swinger's current sequence descriptor's float at `+0x2d0`. The comparison is `FCOMP` +
 *  `AND EAX,0x100` / `JNZ` at `0x10345824..0x10345837`: in range when `100 + reach >= distance`. */
static bool MeleeSwingInRange(const FElysiumEntity& Swinger, const FVector& PointCm);

/** SEAM for the studio sequence descriptor's `+0x2d0` float (`GetSeqDesc(m_nSequence)` at
 *  `0x10345813`), the per-sequence swing reach `IsMeleeSwingInRange` adds to its 100-unit pad. The
 *  kernel reads no studio header (family Anim records the same gap), so the reach term answers 0.0
 *  and the test is the 100-unit pad alone. */
static float SequenceSwingReachUnits(const FElysiumEntity& Swinger);

/** `CBaseCombatCharacter::IsHoldingMeleeWeapon` (`0x10345d00`): a null active weapon answers false,
 *  otherwise the weapon's slot-360 capability word (`+0x5a0`) tested against `0x18000`. That test
 *  is `ElysiumNpcCond::WeaponCapability` == `Melee` (`ElysiumNpcConditions.h`,
 *  `MeleeCapabilityBits`), the record-based reading this runtime already makes of the same word. */
static bool HoldingMeleeWeapon(const FElysiumCombatCharacter& Character);

/** `FUN_103498b0`, the defender half of the melee-reaction classifier, on the
 *  `melee_dice_roll_result` retail's slot 318 is handed: `__ftol(word1 - word3 - word2)` compared
 *  against the four `rules.txt` `Melee_Reactions` cells `_DAT_10739fa0..fac` (runtime-filled),
 *  answering 0..4. The port's record is `FElysiumMeleeRoll` (`Margin()` is that difference) and the
 *  classifier is `ElysiumWeapons::ClassifyDefender`, whose five bands are retail's 0..4 plus one;
 *  an unloaded table (`Unclassified`) answers -1, which takes slot 318's not-3 arm. */
int32 DefenderBlockReactionClass(const FElysiumMeleeRoll& Roll) const;

/** `FUN_10344da0(activity)`: `0x8a < activity && activity < 0x94`, the knockback-push activity
 *  window slot 320 tests. */
static bool IsKnockbackPushActivity(int32 Activity);

/** SEAM for `FUN_101e3ff0(&DAT_10739a4c, this)`, the Presence-effect dismissal the three species
 *  slot-313 bodies open with: lazily cache the two discipline-10 masks (`DAT_10739478` /
 *  `DAT_10739484` behind `DAT_10739a25` bits 0 and 1), and when this character's effect word
 *  (`+0xeb4`) intersects the first, walk up to five bits of the second and `RemoveEffect(this, bit,
 *  0)`. The port's discipline domain (`ElysiumDisciplines`) keeps no per-target effect word and no
 *  global mask table, so the call is counted and removes nothing. */
int32 DismissPresenceEffectCalls = 0;
void DismissPresenceEffect();
