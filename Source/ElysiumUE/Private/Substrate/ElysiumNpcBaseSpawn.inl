// Story 0019/8 (29e under the strict verdict), family **Spawn19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseSpawn.cpp`, or generated in the slot files for a slot body.
//
// Owns (Spawn19's `rule` rows): 0x10265ad0 CAI_BaseNPC::Event_Killed, 0x10273200
// CAI_BaseNPC::Spawn.

// --- Slot 103, `CAI_BaseNPC::Spawn` (`0x10273200`) ---------------------------------------------
//
// The `CAI_BaseNPC` half of slot 103. No `CAI_BaseNPC`-line class of this port reaches it through
// the vtable -- `CCineNPC` (`0x101a6f10`) and `CAI_TestHull` (`0x102d72f0`) replace the slot without
// chaining -- so its one caller is the Troika body's DIRECT call (`0x10299006`, `TroikaSpawnBody`).
virtual void Spawn() override;

/** `BecomeDead` (`0x10265a40`), the four stores `Event_Killed` makes on a body whose `GetFlags()`
 *  carries `0x2000`: `m_iHealth = m_iMaxHealth / 2` (`+0x210` from `+0x208`), `m_takedamage = 2`
 *  (`+0x1fc`), `m_iMaxHealth = 5`, then slot 93 `SetMoveType(6, 0)` (MOVETYPE_FLYGRAVITY). */
void BecomeDead();

/** `FUN_10265a90` (`0x10265a90`): the one-shot `m_OnDeath` (`+0x5e24`) fire behind the `+0x5bd4`
 *  latch, the attacker as activator and this NPC as caller. The latch is the combat character's
 *  `bDeathReported` (read through `HasReportedDeath` / written through
 *  `SetDeathReportedForRestore`), the word the checklist's `present` verdict names for `+0x5bd4`. */
void FireOnDeathOnce(const FElysiumEntityHandle& Attacker);

/** `m_pSchedule == GetScheduleOfType(TranslateSchedule(Id))` as `Event_Killed` asks it
 *  (`0x10265ae3` -> `0x102cc1f0`): slot 440, the lookup, and the `"GetScheduleOfType(): No CASE"`
 *  miss arm's base schedule 1. Answers the GLOBAL id `Schedule.Current` is compared against. */
int32 Spawn19ScheduleOfType(int32 RetailId);

/** `CSoundEnt::InsertSound(SOUND_CARCASS 0x20, GetAbsOrigin(), 0x180, 30.0, NULL)` -- the call
 *  `0x10265d90` (`0x101babc0`) makes when slot 552 answers false. The bus's raw-request door, as
 *  `ambient_generic`'s `EmitAiSoundEvent` inserts: the explicit volume, the carcass type bit, no
 *  owner, non-occludable. */
void Spawn19InsertCarcassSound();

/** The CBaseCombatCharacter::Spawn (`0x10323a90`) stand-in the base body chains into: `AddToTeam`
 *  on a non-empty `m_sTeamName`, then `CBaseAnimating::Spawn`, which has no kernel counterpart (the
 *  port's body build is `FElysiumAnimating`'s presentation, run by the Troika leaf). */
void Spawn19CombatCharacterSpawn();

/* `m_lifeState` (`+0x200`) is the entity's `LifeState` (`ElysiumEntity.h`), the one word; its
 * main writer, `CBaseCombatCharacter::Event_Killed` `0x1032b9b0` (LIFE_DYING), is still the 29e stub, so
 * slot 158 `IsAlive` and `LifeStateIsDying` keep reading the death latches until it lands (L13). */

// --- Seams (each with the three searches in the L08 report) ---

/** SEAM for `g_pGameRules->FAllowNPCs()` -- `DAT_1070ba0c` slot 74 (`+0x128`), the first gate of
 *  `0x10273200` (`0x10273272`) and of `CNPC_VCamera::NPCInit` (`0x103692f8`). No game-rules object
 *  stands here; answers `bSpawn19GameRulesAllowNpcs`, TRUE by default, the admitting arm (false runs
 *  `UTIL_Remove`). Tests may refuse. */
bool Spawn19GameRulesAllowNpcs() const;
bool bSpawn19GameRulesAllowNpcs = true;

/** SEAM for `CBaseCombatCharacter::Weapon_Create` (`0x1032e120`), which `0x10273306` calls with
 *  `m_spawnEquipment`. No port body creates a weapon by classname for a base-only NPC; the request is
 *  counted and answers null, so `Weapon_Equip` is not reached (retail's own null arm). */
FElysiumEntity* Spawn19WeaponCreate(const FString& ClassName);
int32 Spawn19WeaponCreateRequests = 0;
FString Spawn19LastWeaponCreate;

/** `m_sTeamName` +0x10ac: C3's real combat-character word, read at Spawn's existing join site. */
FString Spawn19TeamName() const;

/** `CBaseCombatCharacter::AddToTeam` (`0x103239a0`): the shared registry join. */
void Spawn19AddToTeam(const FString& Name);
int32 Spawn19AddToTeamCalls = 0;

/** SEAM for the death impulse gate of `0x10265ad0` (`0x10265c1a..0x10265c3e`): the ConVar object at
 *  `DAT_106bbaa4` (SDK `npc_vphysics`; its console name and default are unrecovered in this image)
 *  read as `IsCommand() ? 0 : m_nValue`, AND `m_pPhysicsObject` (`+0x36c`). This runtime stands no
 *  `IPhysicsObject` (the physics object is `Chaos`'s, 0019/6), so the gate answers FALSE and the
 *  slot-39 impulse is not dispatched. */
bool Spawn19DeathVPhysicsArmed() const;

/** `SUB_StartFadeOut` 0x102695d0: mode0 -> 2/alpha255, solid4, angular zero, Relink,
 *  next think +10 and SUB_FadeOut. Shares StartFadeOut's body with TASK_DIE. */
void Spawn19StartFadeOut();
int32 Spawn19FadeOutStarts = 0;

/** How many carcass sounds `Spawn19InsertCarcassSound` inserted -- read by the tests. */
int32 Spawn19CarcassSoundInserts = 0;
/** How many times the `CBaseCombatCharacter::Spawn` stand-in ran -- read by the tests. */
int32 Spawn19CombatCharacterSpawns = 0;

// --- Retail numbers the two bodies read -------------------------------------------------------

/** `0x10265ae1 PUSH 0x3a` -- `NPC_FREEZE`, the base's registered local id `0x3a`
 *  (`docs/vtmb/npc-ai/schedule-kernel.md`). `ElysiumScheduleNumbers.h` carries no row for it yet
 *  (listed for the integrator). */
static constexpr int32 Spawn19SchedNpcFreeze = 0x3a;
/** `0x102cc229` -- the miss arm's base schedule 1 (`IDLE_STAND`). */
static constexpr int32 Spawn19SchedMissFallback = 1;
/** `0x10265b47 AND EAX,0x2080` / `0x10265b4c CMP EAX,0x80` -- the cine spawnflag test. */
static constexpr int32 Spawn19CineDeferMask = 0x2080;
static constexpr int32 Spawn19CineDeferNoDefer = 0x80;
/** `0x10265cd4 TEST AH,0x20` -- `GetFlags() & 0x2000`. */
static constexpr int32 Spawn19BecomeDeadFlag = 0x2000;
/** `0x10265d82 PUSH 0x180` -- the carcass sound's volume, SOURCE units. */
static constexpr float Spawn19CarcassVolumeUnits = 384.f;
/** `0x10265d7d PUSH 0x41f00000` -- its duration, 30.0 s. */
static constexpr double Spawn19CarcassDurationSeconds = 30.0;
/** `0x10265cfc` / `0x10265da6` -- the two `AI_BaseNPC.cpp` lines stamped at the two DEAD writes. */
static constexpr int32 Spawn19KilledLineA = 0x217;
static constexpr int32 Spawn19KilledLineB = 0x241;
/** `0x10265d06 MOV [ESI+0x5cc4],0x7` -- `NPC_STATE_DEAD`. */
static constexpr int32 Spawn19StateDead = 7;
/** `0x10265af4 CMP [ESI+0x5cc0],0x4` -- `NPC_STATE_SCRIPT`. */
static constexpr int32 Spawn19StateScript = 4;
/** `BecomeDead` `0x10265a40`: `m_takedamage = 2`, `m_iMaxHealth = 5`, `SetMoveType(6, 0)`. */
static constexpr int32 Spawn19BecomeDeadTakeDamage = 2;
static constexpr int32 Spawn19BecomeDeadMaxHealth = 5;
static constexpr int32 Spawn19MoveTypeFlyGravity = 6;
/** `0x102732a6 TEST EAX,0x200000` -- `bits_CAP_USE_WEAPONS`. */
static constexpr int32 Spawn19CapUseWeapons = 0x200000;
