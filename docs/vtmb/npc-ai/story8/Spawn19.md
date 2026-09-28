# Spawn19 -- the spawn, death and swap bodies (spec 0019 story 8, lane L08)

Walked from the listing (`vtmb_asm`) with the pass-R packet `families-19-29/Spawn19-READING.md` beside
it. Every `rule` body over 64 bytes, arms in retail order. Port: `ElysiumNpcBaseSpawn19.cpp`,
`ElysiumNpcSpawn19.cpp`, `ElysiumNpcSpawn19Species.cpp`. Pass C folds these into `lifecycle.md`
(spawn, death) and `schedule-kernel.md` (the death programs).

## `0x10265ad0` `CAI_BaseNPC::Event_Killed` (slot 144, 759 bytes)

1. `0x10265adf`/`0x10265aee` -- `m_pSchedule` non-null and equal to `0x102cc1f0(0x3a)` (slot 440 then
   slot 446, the `"GetScheduleOfType(): No CASE"` miss answering schedule 1): `NPC_FREEZE` is
   running, return at once. Nothing is written.
2. `0x10265afb` -- `m_NPCState` (`+0x5cc0`) `== 4` SCRIPT and `m_hCine` (`+0x5d74`) resolves
   (`0x10265b15`): a cine whose `m_sequenceStarted` (`+0x5f91`) is set and whose spawnflags
   `& 0x2080 != 0x80` (`0x10265b2f`, `0x10265b51`) takes the whole `CTakeDamageInfo` into
   `m_DeferredDeathInfo` (`+0x1a48..+0x1a92`, two vectors through `0x1003e4b0`) and RETURNS
   (`0x10265c04`) -- the NPC stays alive. Otherwise `CancelScript` (`0x101a8c30`) on the cine, then
   the impulse arm: the ConVar at `DAT_106bbaa4` (`IsCommand() ? 0 : m_nValue`, `0x10265c27`/
   `0x10265c34`) and `m_pPhysicsObject` (`+0x36c`, `0x10265c3e`) both set dispatch slot 39 on the
   physics object with slot 263 `GetGroundSpeedVelocity()`.
3. `0x10265c78` -- slot 511 `StopLoopingSounds`.
4. `0x10265c85..0x10265cb2` -- `m_GrappleRole` (`+0x153c`) `-1`, or `m_GrapplePartner` (`+0x1538`)
   `-1`, stale or null: slot 488 `DeathSound`. A live grapple partner silences it.
5. `0x10265cc8` -- `0x10265a90(attacker)`: behind the `+0x5bd4` latch, `m_OnDeath` (`+0x5e24`)
   fires with the attacker as activator, then the latch is set.
6. `0x10265cd7` -- `GetFlags() & 0x2000`: `m_pfnTouch = NULL` and `BecomeDead` (`0x10265a40`:
   `m_iHealth = m_iMaxHealth / 2`, `m_takedamage = 2`, `m_iMaxHealth = 5`, `SetMoveType(6, 0)`).
7. `0x10265ced` -- `CBaseCombatCharacter::Event_Killed(info)` (`0x1032b9b0`), DIRECT.
8. `0x10265cf2..0x10265d06` -- the ideal-state trace `AI_BaseNPC.cpp:0x217`, `m_IdealNPCState = 7`.
9. `0x10265d18` -- a global object's slot 1 through `DAT_10924a6c`, answer discarded.
10. `0x10265d1f` -- `SetCondition(0x4c LIGHT_DAMAGE)`.
11. `0x10265d29..0x10265d3a` -- `m_hLastDamageEnt` (`+0x5b7c`) = the attacker's handle, or `-1`.
12. `0x10265d46` -- `m_bCondTookDamage` (`+0x5b80`) = 1.
13. `0x10265d4d` -- `0x1028ae60` (vacate the squad slot); `0x10265d5a` `m_pSquad` set:
    `RemoveFromSquad` (`0x103158f0`).
14. `0x10265d66`/`0x10265d70` -- slot 552 `ShouldFadeOnDeath` (spawnflag bit 9): true
    `SUB_StartFadeOut` (`0x102695d0`); false `InsertSound(SOUND_CARCASS 0x20, origin, 0x180, 30.0,
    NULL)` (`0x101babc0`).
15. `0x10265d9c..0x10265dba` -- the trace `AI_BaseNPC.cpp:0x241`, `m_IdealNPCState = 7` again,
    `SetState(7)` (`0x1026e340`).

Reads `+0x5c38`, `+0x5cc0`, `+0x5d74`, cine `+0x5f91`/`+0x204`, `+0x36c`, `+0x153c`, `+0x1538`,
`+0x5da4`, info `+0x2c`. Writes `+0x1a48..+0x1a92`, `+0x1ec`, `+0x1b3c/+0x1b40` (absent), `+0x5cc4`,
`+0x5b7c`, `+0x5b80`, and through its callees `+0x5bd4`, `+0x210`, `+0x1fc`, `+0x208`, `+0x5cc0`.

**Unrecovered:** the ConVar at `DAT_106bbaa4` (the SDK names it `npc_vphysics`; this image's
name and default are not read); the object behind `DAT_10924a6c` and its slot 1; `m_DeferredDeathInfo`'s
scalar words have no slot on the port's `FElysiumDmg`.

## `0x102bf340` `CAI_BaseNPCTroika::Event_Killed` (slot 144, 261 bytes)

1. `0x102bf34d`/`0x102bf354` -- `0x101c2af0(info)` answers `info+0x49`; zero broadcasts the death
   act through `0x102ca2a0` on `DAT_109253f8`: type 1 (criminal), source this, the slot-217 origin
   with Z `+ 16.0` (`_DAT_10451ad0`), level 3, radius `100000.0`, offender NULL, victim this, 1.
   The store caps at 64 records (`+0xa04 < 0x40`).
2. `0x102bf3a8` -- `CAI_BaseNPC::Event_Killed` (above), DIRECT.
3. `0x102bf3b4` -- `ClearHintNode(5.0)`.
4. `0x102bf3be` -- slot 601 with `this`, unconditional.
5. `0x102bf3cd` -- `0x102b53d0(0, "Leaving interesting place (Event_Killed)")`.
6. `0x102bf3d4`/`0x102bf3df` -- `IsInDialog()` true: `0x102c0bb0` (stop the dialogue).
7. `0x102bf3ec..0x102bf3fa` -- `m_iName` set: `PyRun_SimpleString(va("MarkAsDead(\"%s\")", name))`.
8. `0x102bf40c..0x102bf438` -- `m_hClosestPlayer` (`+0x628c`) live: `0x101828b0(this, 0x47c34ff3)`
   on that player -- the closest-NPC cache offered this NPC at `99999.8984375`.

**Unrecovered:** the port's damage packet carries no `+0x49` byte; the act record's radius, victim
and trailing flag have no port field; the Python import behind `[0x109f36fc]` is outside the corpus
(its identity as `PyRun_SimpleString` is from the verdict).

## `0x10273200` `CAI_BaseNPC::Spawn` (slot 103, 312 bytes)

1. `0x10273272`/`0x1027327a` -- `g_pGameRules` (`DAT_1070ba0c`) slot 74 (`FAllowNPCs`) false:
   `UTIL_Remove(this)` and return.
2. `0x1027329b` -- only with `m_pBaseNPCTroika` (`+0x98`) NULL: `CapabilitiesGet() & 0x200000`
   (`0x102732ac`), `m_spawnEquipment` (`+0x5dec`) non-null (`0x102732b6`), not the 2-byte `"0"`
   (`0x102732d1`) and not the 15-byte `"item_w_unarmed"` (`0x102732f2`): `Weapon_Create` and, on a
   weapon, slot 383 `Weapon_Equip(weapon, false)`.
3. `0x10273320` -- slot 223 `CreateVPhysics`.
4. `0x10273328` -- `CBaseCombatCharacter::Spawn` (`0x10323a90`: `AddToTeam` on a non-empty
   `m_sTeamName`, then `CBaseAnimating::Spawn`).

**Unrecovered:** nothing in the body; `CBaseAnimating::Spawn`'s own content is outside the family.

## `0x10298d30` `CAI_BaseNPCTroika::Spawn` (slot 103, 1336 bytes)

1. `0x10298d3d` -- `m_bInitialized` (`+0x62e9`) = 1.
2. `0x10298d59..0x10298d93` -- a non-empty `m_statTemplate` (`+0x10e4`) goes to the template
   manager (`0x10206c30` on `DAT_1074f028`, which applies it through `0x10206aa0`).
3. `0x10298d9c` -- slot 104 `Precache`.
4. `0x10298db1`/`0x10298db6` -- `AddToTeam(m_sTeamName)` on a non-empty string.
5. `0x10298dc4`/`0x10298dd4` -- slot 105 `SetModel(STRING(GetModelName()))`.
6. `0x10298dde`/`0x10298de8` -- `m_bloodColor = 0xf7`, `m_flFieldOfView = 0.2`.
7. `0x10298df2`/`0x10298dfe`/`0x10298e07` -- `CapabilitiesAdd` `1`, `0x800000`, `8`.
8. `0x10298e0c..0x10298e30` -- `m_HackedGunPos = (0, 0, 55.0)`, `m_pInterestingPlace = 0`,
   `m_bInterestingPlaceArrived = 0`.
9. `0x10298e8d`/`0x10298efe`/`0x10298f70`/`0x10298fd7` -- on `m_Collision`: `SetSolidFlags(0)`,
   `AddSolidFlags(w | 1)`, `AddSolidFlags(w | 0x40)`, `SetSolid(SOLID_BBOX)`, each in its own
   scope-trace frame.
10. `0x10298fed`/`0x10298ff6`/`0x10298ffc` -- slot 93 `SetMoveType(4, 0)`, `SetHullSizeNormal(false)`,
    `Relink`.
11. `0x10299006` -- `CAI_BaseNPC::Spawn` (above).
12. `0x10299017..0x1029904d` -- `m_vecInitialPosition` = slot 217, `m_qaInitialAngles` = slot 219.
13. `0x10299057`/`0x1029905f` -- slot 420 `NPCInit`, `ApplyDisciplineSpawnFlags` (`0x1033df80`:
    spawnflags bits 5, 6, 12..15 OR `1, 2, 4, 8, 0x10, 0x20` into `+0x0eac`).
14. `0x1029907c..0x102990ed` -- each `m_iPL…Level` (`+0x6348..+0x6358`) below 1: `DevMsg("Warning:
    Invalid m_iPL… ('pl_…' in world craft)")` and 6.
15. `0x10299120` -- a positive occluded sum: chase = 100, and wait / cover / walk / flank become the
    CUMULATIVE `x * 100 / sum` ladder (truncating `IDIV`).
16. `0x10299194`/`0x102991d4` -- chase not 100: `Warning("Occluded target reaction percentages for
    %s (%f, %f, %f) do not add up to 100%")` with the debug name and the initial position; chase
    still `<= 0` resets the ladder to 10 / 40 / 50 / 70 / 100.
17. `0x10299216`/`0x1029922d` -- `0x10298910(m_sInterestingPlaceGroups)`, `0x102989e0(m_sHintGroups)`.
18. `0x10299244`/`0x1029924f` -- `m_iCombatStartActivity = 0x1029f340(m_sCombatStartActivity)`.
19. `0x10299255` -- `0x10207e60(this)`: `PrecacheModel(GetCharTemplate(this)->+0x78, 0)`.
20. `0x1029925e` -- `AddFlag2(4)`.

**Unrecovered:** the template column `+0x78` `0x10207e60` precaches; the meaning of `m_fFlags2` bit 4.

## Species `Spawn` bodies over 64 bytes (slot 103)

- `0x10368b70` `CNPC_VCamera::Spawn` (589): `CapabilitiesAdd(0x4000000)`; slot 104; slot 105 with
  slot 9; `m_bloodColor = 0xf7`, `m_fEffects = 0`, `m_iHealth = 1`, `m_flFieldOfView = 0.2`,
  `m_NPCState = 0`, `m_HackedGunPos = 0`, `m_flNextListenTime = 0`, `m_pInterestingPlace = 0`
  (`0x10368bb1..0x10368bfb`); `SetSolidFlags(0)`, `SetSolid(SOLID_NONE)`; `SetMoveType(4, 0)`;
  `SetHullSizeNormal(false)`; `Relink`; slot 420; `InitPerceptionDistances`;
  `ApplyDisciplineSpawnFlags`; `m_hEyeLookTarget = -1`, `m_RelativeEyeTarget = 0`; the five
  police-level repairs (identical strings); slot 66 `Hide`; `AddFlag2(0x10)`. No Troika and no base
  spawn.
- `0x101aa9c0` `CPayphone::Spawn` (357): the Troika spawn; `m_flNextThink = curtime + 0.1`;
  `SetMoveType(0, 0)`; `SetSolid(SOLID_BBOX)`; `m_iHealth = 80000`; `m_takedamage = 0`;
  `AddFlag(0x10000)`; `m_nSequence = 0`; `ResetSequenceInfo`; `m_flCycle = 0`;
  `AddSolidFlags(w | FSOLID_NOT_SOLID)`; `RemoveFlag2(4)`; `AddFlag2(0x10)`.
- `0x103927a0` `CNPC_VMingXiao::Spawn` (436): caps `0x4000000`, `0x200000`; the Troika spawn;
  `AddMiscFlag(0x80000)`; FOV `-0.5`; `m_hParentMingZhao = -1`, `m_iTentacleID = -1`,
  `m_bHasTransformed = 0`, `m_iSeveredTentacleMask = 0`, `m_flProxyReadyTimer = 0`;
  `m_flSpitAttackTimer = 0x10397f70() + curtime`; six slots (`0x1039281c..0x10392850`): severed
  `-1`, registered 0, proxy `-1`, attack timer 0, hit points from the tuning record's first float,
  regrow timer `FLT_MAX`; `m_iConnectedTentacleCount = 6`; `0x10398800` (bodygroup);
  `m_hMeleeWeapon` from `GetBestMeleeWeapon`; `m_hRangedWeapon` from slot 309; slot 388
  `Weapon_Switch(ranged, 0)`; `SetAbsoluteAttackExtents(120, 120, 92)`; `m_bNeverMeleeOpponent = 1`;
  throwable mode 0, coordinate tentacle 0, `m_bPlayedDeathAnim = 0`,
  `m_flIdealRange = 0x103986b0()`, charge-ready 0, blocked-by-friend 0.
- `0x1039c380` `CNPC_VMingXiaoTentacle::Spawn` (134): caps `0x4000000`, `0x200000`; the Troika
  spawn; `AddMiscFlag(0x80000)`; `m_ePhase = 0`, `m_bInvincible = 1` (`+0x63d8`), the four timers 0,
  `m_bIgnoreCollision = 1`, `m_bHitGroundSound = 0`, `m_bPlayedDeathAnim = 0`,
  `m_flIgnoreCollisionTimer = curtime + 5.0`; `RemoveFlag2(4)`.
- `0x103b9060` `CNPC_VTzimisce::Spawn` (132): the Troika spawn FIRST; FOV `-0.5`; the head-forward
  basis `+0x1074` rotated `(x, y, z) -> (-y, x, z)`; `SetAbsoluteAttackExtents(60, 60, 100)`;
  `RemoveFlag2(4)`; `m_bInMelee = 1`.
- `0x103c1b90` `CNPC_VTzimisceHeadClaw::Spawn` (180) and `0x103c3b30` `CNPC_VTzimisceRunner::Spawn`
  (193): BEFORE the Troika spawn, `m_statTemplate` (`TzimisceCreation2` / `TzimisceCreation3`),
  `m_sPlayerReaction = "D_HT 10"`, occluded 0 / 0 / 0 / 0 / 100. After it: the head claw adds
  `0x4000000` and `0x200000`, the runner only `0x4000000`; `m_bfNPCFrenziedFlags |= 0x80`; the
  runner clears `+0x6672`, sets `m_bAllowsInterpenetratingAttacks` and `m_hPotentialEnemy = -1`;
  extents `(45, 45, 100)` / `(50, 50, 82)`; `RemoveFlag2(4)`.
- `0x103caa30` `CNPC_VWerewolf::Spawn` (217): `m_statTemplate = "Werewolf"` BEFORE the Troika
  spawn; `AddFlag(0x2000)`; `m_bIsBCCTargetable = 1`; `+0x66a9 = 0`, `+0x66ac = 0`,
  `m_DoorState = 0`; `m_Collision` partition update (`0x100ddd90`); `RemoveFlag2(4)`;
  `AddFlag2(0x10)`.
- `0x10374000` `CNPC_VDog::Spawn` (90): `RandomFloat(0, 1)` first, `+0x6688 = 0`,
  `+0x6674 = curtime + that`; caps `0x4000000`, `0x200000`, `0x8000`; `CNPC_VAnimal::Spawn`.
- `0x103df170` `CNPC_VZombie::Spawn` (183): caps `0x4000000`, `0x200000`, `0x8000`;
  `m_altEquipment = m_spawnEquipment = NULL`; `m_flNextFleeSoundTime = 0`; the five police levels
  `999999`; the encoded witnessed level (`0x1042fde0(0)`) with the two tag bytes read from
  UNINITIALISED stack (a retail defect); `m_iPLSupernaturalLevelWitnessed = 0`;
  `CNPC_VAnimal::Spawn` LAST.
- `0x1035cc20` `CNPC_VAndreiBlood::Spawn` (176): `CapabilitiesAdd(0x200000)`; `CNPC_VVampire::Spawn`;
  runner and kill counts 0, activated / dead / trigger-unhide 0, `m_fTeleportWaitStartTime = curtime`,
  `m_bForceTeleport = 1`, hit counter 0; `0x1035e950` rolls `m_iHitMax`.
- `0x10360c50` `CNPC_VAsianVampire::Spawn` (109), `0x1036afc0` `CNPC_VChangBros::Spawn` (109),
  `0x103ae630` `CNPC_VSheriffMan::Spawn` (109): one `CapabilitiesAdd` (`0x201000`, `0x209000`,
  `0x201000`) then `CNPC_VVampire::Spawn`, in a scope-trace frame.
- `0x10363850` `CNPC_VBach::Spawn` (184): `CapabilitiesAdd(0x200000)`, `CapabilitiesRemove(1)`,
  `CNPC_VVampire::Spawn`, then twenty-two seeds all 0 except `m_bBachInStartingPosition` (`+0x66a1`)
  = 1 -- including `m_flWaitFinished` (`+0x5db4`) and the three words of `m_vecLastOccludeOrigin`.
- `0x1037b040` `CNPC_VGhoulCroucher::Spawn` (267): caps `0x4000000`, `0x200000`, `0x8000`;
  `m_altEquipment = m_spawnEquipment = NULL`; the template by `m_bSpawnBurning` (`+0x6665`) FIRST
  (`MalkMansionStalkerBurning`), then `m_bSpawnDisturbed` (`+0x6664`, `MalkMansionStalker`), else
  `MalkMansionCroucher`; `CNPC_VHumanCombatant::Spawn`; disturbed again: `+0x6666 = +0x6667 = 1` and
  the stalker model, else both 0 and the female model; `m_nUnawareType = RandomInt(0, 3)`; the five
  police levels `999999`.
- `0x1037fa00` `CNPC_VHengeyokai::Spawn` (70): `CNPC_VVampire::Spawn`; extents `(50, 50, 100)`;
  `RemoveFlag2(4)`; `AddFlag2(0x20)`.
- `0x103a6c80` `CNPC_VSabbatLeader::Spawn` (129): `CapabilitiesAdd(0x209000)`,
  `m_statTemplate = "VampireSabbatLeader"`, `CNPC_VVampire::Spawn`.
- `0x103a4510` `CNPC_VPlayerController::Spawn` (69, landed before this story):
  `CNPC_VVampire::Spawn`; `AddClassRelationship(1, 3, 0)`; `m_bForceFrequentThink = 1`;
  `SetName("playercontroller")`; `AddFlag2(0x10)`.

**Unrecovered:** `0x103986b0` (Ming Xiao's ideal range), `GetBestMeleeWeapon`'s NPC pick, the Ming Xiao
tuning record `0x10739d08`'s fields, the names of the Dog's `+0x6688` / `+0x6674`, the Scurrying's
`+0x668c`, the Werewolf's `+0x66a9` / `+0x66ac`, the Bach byte `+0x66a4`.

## Species `Event_Killed` bodies over 64 bytes (slot 144)

- `0x1038e8c0` `CNPC_VManBat::Event_Killed` (159): `0x1038f020(1)` (release the slowed victim),
  `0x1038c170(2)` (fly mode), `0x1038f660` (drop the carried body), `0x1038fd40` (turn the scared
  minions); the screech-cone handle `+0x66a4` resolving: `UTIL_Remove` it and write `-1`; then the
  Troika body.
- `0x10395ba0` `CNPC_VMingXiao::Event_Killed` (156): `m_bPlayedDeathAnim` (`+0x6744`) clear:
  `m_lifeState = 1` and `0x10395c70` (the death animation, which sets the latch) -- the base is NOT
  reached. Set: a live `m_hParentMingZhao` (`+0x6670`) runs `0x10397a50(parent, this)`; the ConVar
  `ming_xiao_grub_death` (default `"1"`, read `IsCommand() ? 0 : m_nValue` through the object's
  `+4`) and `0x10398870` answering head (`m_iTentacleID == -1`) run `0x10397e90` then `0x10397f00`;
  then the Troika body.
- `0x1039e900` `CNPC_VMingXiaoTentacle::Event_Killed` (65): `m_bPlayedDeathAnim` (`+0x6699`) clear:
  `m_lifeState = 1` and `0x1039e970` (the death schedule), return. Set: `0x1039ede0` resolves the
  head and `0x103979d0(head, this)`; then the Troika body.

**Unrecovered:** `0x1038c170`, `0x1038f660` and `0x1038fd40` have no port body; `0x103979d0`, the
head's own handling of a dying tentacle.

## `0x103dfbb0` `CNPC_VZombie::CreateCorpse` (slot 301, 363 bytes)

1. `0x103dfbd7..0x103dfc86` -- COPY first: `m_vecDeathForceVector` (`+0x6680`) = the force, the whole
   `0x13`-dword `CTakeDamageInfo` into `m_DeathDamageInfo` (`+0x668c..+0x66d6`) through a stack copy.
2. `0x103dfc89` -- `SelectWeightedSequence(0x21, -1)`, asked once.
3. `0x103dfc96`/`0x103dfc9b` -- `m_bShouldGib` (`+0x66e0`) clear AND a sequence: `m_bShouldRagdoll`
   (`+0x6675`) set runs `CBaseCombatCharacter::CreateCorpse` with the SAVED words (`0x103dfcab`);
   clear stamps `NPC_VZombie.cpp:0x2eb`, installs `0x162` with force (`0x103dfcd5`),
   `ThinkSet(LAB_1000f4e8, 0)` and slot 614 -- no corpse at all.
4. `0x103dfd02`/`0x103dfd09` -- gibbing, or no sequence: slot 394 `CorpseGib`, `UTIL_Remove(this)`.

**Unrecovered:** nothing in the body; the port's sequence seam answers -1, so only the gib arm is
reachable today.

## `0x103c75f0` `CNPC_VVampireBoss::InputTransformModel` (slot 617, 135 bytes)

1. `0x103c763f` -- `m_pszMonsterClassname` (`+0x6694`) = `"npc_VVampireBoss"`, UNCONDITIONALLY.
2. `0x103c7652`/`0x103c7659` -- `m_MorphModelName` (`+0x667c`, null read as `""`) non-empty:
   `m_pMonsterModelName` (`+0x6680`) = it and `0x102ae750(0x159, false)`.

**Unrecovered:** nothing.

## `0x103c60a0` `CNPC_VVampireBoss::TransformationStart` (slot 618, 632 bytes)

1. `0x103c60fa`/`0x103c610a` -- the entity factory `0x10136580` on `m_pszMonsterClassname`; its `+0x98`
   (a miss leaves NULL and retail faults at `0x103c6110`).
2. `0x103c611b` -- new `m_spawnflags |= 4`; `0x103c612e` slot 62 with our slot 217; `0x103c6143`
   slot 64 with our slot 221; `0x103c614e` slot 202 (owner = us); `0x103c6157`
   `CopyAnimationDataFrom(us)`; `0x103c615e` new `m_flSeekDistBase = 4096.0`.
3. `0x103c6171` -- slot 105 `SetModel(m_pMonsterModelName)`; `0x103c6178` `DispatchSpawn`.
4. `0x103c618d`/`0x103c6193` -- new `m_fEffects |= 0x60`, slot 614; `0x103c61a4` slot 105 AGAIN;
   `0x103c61bb` `m_fEffects &= ~0x60`, alpha 1, `m_nRenderFX = 0x1f`, `m_nRenderMode = 2`.
5. `0x103c61e0` -- new `m_hProteanTransformOther` (`+0x155c`) = us; `0x103c61fe`/`0x103c6204`
   `m_flProteanTransformStartTime` (`+0x1560`) = curtime on BOTH.
6. `0x103c620a..0x103c6234` -- new `m_NPCState = m_IdealNPCState = 5`, `m_bIsBCCTargetable = 1`,
   traces `npc_VVampireBoss.cpp:0x205` / `0x206`; `0x103c623e` `0x102ae750(0x158, false)` ON THE
   NEW BODY.
7. `0x103c6243..0x103c62c4` -- the five police levels and the five occluded percents copied across.
8. `0x103c62ca`/`0x103c62e1` -- `__RTDynamicCast(new, CAI_BaseNPCTroika)` hit: new `+0x66b0`
   (`m_hTransformPartner`) = us.
9. `0x103c62e7`/`0x103c62f1`/`0x103c6304` -- OUR `m_nRenderFX = 0x1e`, `m_nRenderMode = 2`, and
   `m_hProteanTransformOther` = the new body, LAST.

**Unrecovered:** nothing in the body; the port's `CopyAnimationDataFrom` carries only the words the
port has (model, skin, sequence, cycle, anim time, effects).

## `0x103ab310` `CNPC_VSabbatLeader::TransformationStart` (slot 618, 183 bytes)

`0x103ab36a` slot 105 `SetModel(".../andrei/andrei_no_mouth.mdl")`; `0x103ab377`
`SetIdealActivity(0x113e)`; `0x103ab38b` `m_fEffects |= 0x10`; `m_nRenderFX = 0`,
`m_nRenderMode = 0`, `m_eHull = 0`, `m_eDefaultHull = 0` (`0x103ab391..0x103ab3a3`);
`0x103ab3a9` `SetHullSizeNormal(true)`; `0x103ab3af` `Relink`; `0x103ab3b9`
`CNPC_VVampireBoss::TransformationStart`, DIRECT. The boss body's render writes land last.

**Unrecovered:** nothing.

## `0x1037c1c0` `CNPC_VGhoulCroucher::ScriptHide` (slot 77, 232 bytes)

`0x1037c229` `CAI_BaseNPCTroika::ScriptHide` (`0x102c1ce0`); then `m_hBurningParticle` (`+0x6670`)
resolving (`0x1037c237..0x1037c280`, the handle read twice): slot 77 on the particle (`0x1037c286`).
A failed second resolve dispatches slot 77 through a null receiver (`0x1037c299`), a retail fault. The
handle is not cleared.

**Unrecovered:** why the null-receiver arm was left in; the Troika `ScriptHide` body is family
Damaged19's unported row.
