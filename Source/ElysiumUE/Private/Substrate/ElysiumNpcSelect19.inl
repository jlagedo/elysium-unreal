// Story 0019/8 (29e under the strict verdict), family **Select19** -- `CAI_BaseNPCTroika`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcSelect19.cpp`, or generated in the slot files for a slot body.
//
// Owns (Select19's `rule` rows): 0x102ae920 CAI_BaseNPCTroika::PreSelectSchedule, 0x102af660
// CAI_BaseNPCTroika::SelectSchedule.

// --- The Troika selector ---------------------------------------------------------------------------

/** `CAI_BaseNPCTroika::SelectSchedule` (`0x102af660`), slot 438's Troika body. Named as State19 names
 *  `TroikaSelectIdealState`: the slot's virtual is `SpeciesSelectSchedule` (the census name), whose
 *  Troika definition answers this; every species body that retail chains to the Troika body
 *  through a DIRECT thunk (`0x1000f68d` and kin) calls this, not the virtual. */
int32 TroikaSelectSchedule();

// --- Unnamed Troika selector helpers this family's bodies call (no verdict row of their own) --------

/** `0x102b8a60` — the see-unknown ladder `SelectSchedule` case 3, `CNPC_VAnimal` and `CNPC_VTzimisce`
 *  ask first: `IGNORE_UNKNOWN` as an interrupt while not investigating → `0x60`; any of
 *  `SEE_UNKNOWN` / `UNKNOWN_ADVANCING` (interrupts) or `INVESTIGATE_SIGHT` → alert level 3 and
 *  `0x59` / `0x5a` / `0x5c` / `0x5e`; `LOST_UNKNOWN` → `0x5d` (`0x5f`); `LOOKED_AT_UNKNOWN` → `0x5c`;
 *  else 0. Retail name UNRECOVERED. */
int32 SelectUnknownAlertSchedule();

/** `0x102b9060` — the sound ladder case 3 and the same two species ask last. Retail name UNRECOVERED. */
int32 SelectSoundAlertSchedule();

/** `0x102b8d20(this, HatedAnswer, FearedAnswer)` — `SEE_SOUND_SOURCE`'s third-party arm: the NPC that
 *  made the committed sound (`m_hBestSoundSource`, `+0x5b78`), its enemy, and this NPC's disposition
 *  toward that enemy. `D_HT` answers `HatedAnswer` after writing `m_vSavePosition`; `D_FR` answers
 *  `FearedAnswer`; anything else re-arms `m_flNextInvestigateSoundTime` at `curtime + 20.0` and answers
 *  0. The listing's `RET 0x8` and `MOV EAX,[ESP+0x2c]`/`[ESP+0x30]` are what make the two arguments
 *  the answers — the decompiler printed them as `unaff_retaddr`. Retail name UNRECOVERED. */
int32 SelectSoundSourceSchedule(int32 HatedAnswer, int32 FearedAnswer);

/** `m_pSchedule == GetScheduleOfType(RetailId)` (`0x102cc1f0`, which translates through slot 440
 *  before the lookup) — the "is this program already running" test the selectors make. */
bool SelectRunningScheduleIs(int32 RetailId);

/** The active weapon's slot `+0x5a0` capability word, 0 with no active weapon — what every selector's
 *  `GetActiveWeapon()->vfunc(+0x5a0)` pair reads. The port's typed answer for that word is
 *  `ElysiumNpcCond::WeaponCapability` (melee `0x18000`, ranged `0x2000`); read through it here. */
uint32 SelectActiveWeaponWord() const;

/** `m_hClosestPlayer`-style resolution to the port's one player: the handle resolved, and only when it
 *  IS the world's player (retail passes the resolved `CBasePlayer*` as `this` to the incident bodies). */
FElysiumPlayer* SelectResolvePlayer(const FElysiumEntityHandle& Handle) const;

// --- Seams (no port surface for the retail input; each answers retail's admitting "nothing") --------

/** SEAM for `m_sppPatrolPath.m_pPath` (`+0x6590`, a pooled `CAI_PatrolPath`) and its `m_iSchedule`
 *  (`+0x4`), read by `SelectSchedule` case 1. The port stands no patrol-path object (story 10g, the
 *  `ThinkPatrol` executor is the port's route walker and has no schedule id), so this answers
 *  "no path object": the case-1 patrol arm is skipped, as retail skips it for an NPC with no path. */
bool SelectPatrolPathObject(int32& OutScheduleRetail) const;

/** How many times the patrol arm ran `0x1029f650` (the interest-hint draw) and `0x1029f5d0` (the
 *  path release after `"has no schedule"`). */
int32 SelectPatrolPathDraws = 0;
int32 SelectPatrolPathReleases = 0;

/** SEAM for `CSoundEnt::InsertSound(8 SOUND_DANGER, player eye (+0x364), DAT_1072bc88, 10.0,
 *  DAT_1072bcc2, this)` (`0x1000bca8`), the flee case's scream at the witnessed player. The port's
 *  sound bus takes named `SoundTypes` categories and no row maps retail type 8 with those two
 *  unrecovered globals, so the insertion is counted and emits nothing. */
int32 SelectFleeDangerSounds = 0;

/** SEAM for `0x101e3ee0(&DAT_10739a4c, this)` — the discipline manager's record-byte-`+0x34` sweep
 *  (`InterruptSchedule` `0x101e3a30`, the `OnInterruptSchedule` HitInfo) PreSelectSchedule runs while
 *  `SCHED_TROIKA_D_MESMERIZE` is installed. `FElysiumDisciplines` carries no per-effect `+0x34` byte
 *  (its only schedule-interrupt entry, `NotifyScheduleChanged`, is the unconditional 0x102a0940 arm),
 *  so the sweep is counted and interrupts nothing. */
int32 PreSelectMesmerizeSweeps = 0;

/** How many times PreSelectSchedule's squad arm reached `SquadNewEnemy` (`0x103161a0`). No squad object
 *  stands here (`ConnectedSquad()` is null), so the arm is unreachable today; counted when reached. */
int32 PreSelectSquadNewEnemyCalls = 0;

// --- ConVars the Select19 bodies read, absent from `ElysiumNpcKernelTunables.h` ----------------------
//
// Retail reads each as `!cv->vtable[+0x04]() && cv->m_nValue (+0x2c)`. The generated tunables table
// (`gen_kernel_tunables`) does not carry these twelve and this lane may not edit it; they are stood
// here with their object address and shipped default, read out of each `ConVar::Create` call
// (`DAT_10539978` = "1", `DAT_105399a0` = "0"), and are settable for a test or the console.
enum class ESelect19ConVar : uint8
{
	DebugPlayerOnHead,            // `debug_player_on_head` "3", object `0x10924508`, ctor `0x1028bc20`
	MingXiaoCharge,               // `ming_xiao_charge` "1", `0x1093bb58`, ctor `0x10390c70`
	AsianVampForceJumpUp,         // `asianvamp_force_jump_up` "0", `0x1093a590`, ctor `0x10360280`
	ChangBrosForceUnitedAttack,   // `changbros_force_united_attack` "0", `0x1093aa28`, ctor `0x1036acd0`
	ChangBrosForceTeleport,       // `changbros_force_teleport` "0", `0x1093a980`, ctor `0x1036ad60`
	ChangBrosForceLedgeAttack,    // `changbros_force_ledge_attack` "0", `0x1093a9c8`, ctor `0x1036adf0`
	AndreiForcePlayerCollision,   // `andrei_force_player_collision` "0", `0x1093c2b0`, ctor `0x103a5a70`
	AndreiForceJumpAttack,        // `andrei_force_jump_attack` "0", `0x1093c390`, ctor `0x103a59e0`
	AndreiForceChargeAttack,      // `andrei_force_charge_attack` "0", `0x1093c2f8`, ctor `0x103a58c0`
	SheriffForceTeleport,         // `sheriff_force_teleport` "0", `0x1093c588`, ctor `0x103ad9c0`
	WerewolfForceTeleport,        // `werewolf_force_teleport` "0", `0x1093f9a0`, ctor `0x103c8590`
	TzimiscePounce,               // `tzimisce_pounce` "1", `0x1093cce0`, ctor `0x103b6300`
	Count
};

/** `m_nValue` of one of the twelve (the shipped default until `SetSelect19ConVar`). */
static int32 Select19ConVarInt(ESelect19ConVar ConVar);
/** `ConVar::SetValue` for a test, the console, or a body that writes one (`CNPC_VSabbatLeader`). */
static void SetSelect19ConVar(ESelect19ConVar ConVar, int32 Value);
/** Every one back to its shipped default. */
static void ResetSelect19ConVars();
/** The retail gate, `!IsCommand() && m_nValue != 0` — a ConVar is never a command, so the value. */
static bool Select19ConVarEnabled(ESelect19ConVar ConVar);
