// Story 0019/8 (29e under the strict verdict), family **Spawn19** -- `CAI_BaseNPCTroika`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcSpawn19.cpp`, or generated in the slot files for a slot body.
//
// Owns (Spawn19's `rule` rows): 0x102bf340 CAI_BaseNPCTroika::Event_Killed, 0x10298d30
// CAI_BaseNPCTroika::Spawn.

/** `CAI_BaseNPCTroika::Spawn` (`0x10298d30`), slot 103's Troika body, arm by arm; `FElysiumNpc::Spawn`
 *  (`ElysiumNpc.cpp`) is this. `NPCInit` runs HERE (`0x10299057`), not in `FElysiumNpc::Activate`, and
 *  the skeletal body is stood by its slot-105 `SetModel` (`0x10298dd4`). */
void TroikaSpawnBody();

/** `CBaseCombatCharacter::ApplyDisciplineSpawnFlags` (`0x1033df80`): spawnflags bits 5, 6, 12, 13, 14
 *  and 15 OR `1, 2, 4, 8, 0x10, 0x20` into `m_iDisciplineContextTgtFlags` (`+0x0eac`). */
void ApplyDisciplineSpawnFlags();

/** `CBaseEntity::SetAbsoluteAttackExtents` (`0x1009b060`): slot 15 `SetAttackExtents` with the
 *  absolute extents MINUS half the collision box (`m_Collision` slots 8 and 4, maxs and mins,
 *  scaled by `_DAT_104454d0` = 0.5). The box is the one `SetHullSizeNormal` last sized
 *  (`LastSetSizeMinsUnits` / `LastSetSizeMaxsUnits`). `AbsoluteUnits` is SOURCE units. */
void SetAbsoluteAttackExtents(const FVector& AbsoluteUnits);

/** Slot 105 `SetModel` over an `FString`, the `char*` the retail call pushes: an empty name is the
 *  `DAT_106b8540` empty string retail substitutes for a null pointer. */
void Spawn19SetModel(const FString& ModelName);

// --- Words the family needed that no earlier family declared (three searches each in the report) --

/** `m_bloodColor` (`+0x1570`, `CBaseCombatCharacter`). Troika `Spawn` and `CNPC_VCamera::Spawn`
 *  write `0xf7`; slot 145 `BloodColor` is the generated reader stub. */
int32 BloodColorWord = 0;
/** `m_HackedGunPos` (`+0x1578`, `CBaseCombatCharacter`), SOURCE units. Troika `Spawn` writes
 *  `(0, 0, 55.0)`, the camera zeroes it. */
FVector HackedGunPosUnits = FVector::ZeroVector;
/** `m_iDisciplineContextTgtFlags` (`+0x0eac`, `CBaseCombatCharacter`) -- `ApplyDisciplineSpawnFlags`'
 *  one write. No port reader yet. */
int32 DisciplineContextTgtFlags = 0;
/** `CBaseEntity::Relink` (`0x101cf600`) calls from the spawn bodies -- no spatial partition stands. */
int32 Spawn19RelinkCalls = 0;
/** `CCollisionProperty::UpdatePartition` (`0x100ddd90` on `m_Collision` `+0x270`) calls -- the same
 *  absent spatial partition. */
int32 Spawn19CollisionPartitionUpdates = 0;
/** `m_bNeverMeleeOpponent` (`+0x1482`, `CBaseCombatCharacter`). */
bool bNeverMeleeOpponent = false;
/** `m_bAllowsInterpenetratingAttacks` (`+0x0fe0`, `CBaseCombatCharacter`). */
bool bAllowsInterpenetratingAttacks = false;
/** `m_vecHeadLocalForward` (`+0x1074`, `CAI_BaseNPCTroika`), the kernel's copy of the head-forward
 *  basis (the visual binding's `HeadLocalForward` is the presentation half). */
FVector HeadLocalForward = FVector::ZeroVector;
/** `m_nRenderFX` (`+0x0168`, `CBaseEntity`). */
int32 RenderFxWord = 0;
/** `m_clrRender`'s alpha byte (`+0x01a3`, `CBaseEntity`); Source's default render colour is opaque. */
uint8 RenderAlphaByte = 255;
/** `m_hProteanTransformOther` (`+0x155c`, `CBaseCombatCharacter`), the protean swap's partner the
 *  boss writes LAST (`0x103c6304`). */
FElysiumEntityHandle ProteanTransformOther;
/** `m_flProteanTransformStartTime` (`+0x1560`, `CAI_BaseNPCTroika`): the vampire boss stamps it on
 *  both bodies (`0x103c61fe` / `0x103c6204`); `TASK 0x14b` reads it on the boss
 *  (`0x103c63c0`), Hengeyokai (`0x10383470`) and Ming Xiao (`0x1039aa20`). NPCInit leaves it 0. */
double ProteanTransformStartTime = 0.0;

/** The five police-level repairs `CAI_BaseNPCTroika::Spawn` (`0x10299064..0x102990ed`) and
 *  `CNPC_VCamera::Spawn` (`0x10368d15..0x10368d9e`) share, verbatim: each `m_iPL…Level` below 1 is
 *  `DevMsg`'d and forced to 6. */
void RepairPoliceLevels();


// --- `Event_Killed` (`0x102bf340`) seams and counters -------------------------------------------

/** SEAM for `FUN_101c2af0(info)` -- `return info->byte[+0x49]`, the `CTakeDamageInfo` flag the port's
 *  `FElysiumTakeDamageInfo` does not carry. Answers FALSE, so the death stimulus is broadcast -- the
 *  arm every ordinary kill takes. */
bool Spawn19DamageInfoSuppressesStimulus(const FElysiumTakeDamageInfo* Info) const;
/** How many death stimuli `Event_Killed` published -- read by the tests. */
int32 Spawn19DeathStimulusBroadcasts = 0;
/** How many `MarkAsDead` lines `Event_Killed` ran -- read by the tests. */
int32 Spawn19MarkAsDeadLines = 0;
FString Spawn19LastMarkAsDeadLine;

// --- Retail numbers ------------------------------------------------------------------------------

/** `0x10298e18` -- `m_HackedGunPos.z = 55.0` (`0x425c0000`). */
static constexpr float Spawn19HackedGunHeightUnits = 55.f;
/** `0x10298dde` -- `m_bloodColor = 0xf7`. */
static constexpr int32 Spawn19BloodColor = 0xf7;
/** `0x10298de8` -- `m_flFieldOfView = 0.2` (`0x3e4ccccd`). */
static constexpr float Spawn19TroikaFieldOfView = 0.2f;
/** `0x10299075 MOV EBP,0x6` -- the value an invalid `m_iPL…Level` is forced to. */
static constexpr int32 Spawn19PlLevelRepair = 6;
/** `0x10299115 MOV EBX,0x64` -- 100, the ladder's top. */
static constexpr int32 Spawn19OccludedHundred = 100;
/** `0x102991d6..0x102991fe` -- the default ladder 10 / 40 / 50 / 70 / 100. */
static constexpr int32 Spawn19OccludedDefaultWait = 10;
static constexpr int32 Spawn19OccludedDefaultCover = 40;
static constexpr int32 Spawn19OccludedDefaultWalk = 50;
static constexpr int32 Spawn19OccludedDefaultFlank = 70;
/** `0x10298dda` / `0x10298df7` / `0x10298e03` -- the three capability pushes. */
static constexpr int32 Spawn19TroikaCapA = 0x1;
static constexpr int32 Spawn19TroikaCapB = 0x800000;
static constexpr int32 Spawn19TroikaCapC = 0x8;
/** `0x10298efb OR AL,0x1` / `0x10298f6a OR ECX,0x40` / `0x10298f93 PUSH 0x2` -- the solid words. */
static constexpr uint32 Spawn19SolidFlagA = 0x1;
static constexpr uint32 Spawn19SolidFlagB = 0x40;
static constexpr int32 Spawn19SolidBbox = 2;
/** `0x10298fe2 PUSH 0x4` -- `SetMoveType(MOVETYPE_STEP 4, 0)`. */
static constexpr int32 Spawn19MoveTypeStep = 4;
/** `0x1029925a PUSH 0x4` -- `AddFlag2(4)`. */
static constexpr uint32 Spawn19TroikaFlags2 = 0x4;
/** `0x102bf37f FADD [0x10451ad0]` -- 16.0, the stimulus' raise, SOURCE units. */
static constexpr float Spawn19DeathStimulusRaiseUnits = 16.f;
/** `0x102bf372 PUSH 0x3` -- the stimulus' level. */
static constexpr int32 Spawn19DeathStimulusLevel = 3;
/** `0x102bf3ad PUSH 0x40a00000` -- `ClearHintNode(5.0)`. */
static constexpr float Spawn19HintReuseSeconds = ElysiumNpcTunables::Five;
/** `0x102bf430 PUSH 0x47c34ff3` -- 99999.8984375, the closest-NPC offer distance. */
static constexpr float Spawn19ClosestNpcOffer = 99999.8984375f;
