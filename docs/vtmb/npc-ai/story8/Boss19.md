# Story 8 — family Boss19, walked prose (lane L12)

Spec 0019 story 8, pass I. Each section is one `rule` row of `families-19-29/Boss19-READING.md`
(and Damaged19's `0x103c43b0`), walked arm by arm off the decompiled C and the listing. Pass C folds
these into `schedule-kernel.md` / `lifecycle.md`. Port: `ElysiumNpcBoss19.cpp` (Troika line),
`ElysiumNpcMingXiao.cpp`, `ElysiumNpcMingXiaoTentacle.cpp`, `ElysiumNpcHengeyokai.cpp`,
`ElysiumNpcBoss19Species.cpp` (SabbatLeader), `ElysiumNpcDamaged19Species.cpp` (TzimisceRunner).

`0x102ae750` is `SetSchedule(id, force)`: translate (slot 440), resolve (slot 446, fallback 1), refuse
in state 7, and — unless `force` — refuse when slot 158 `IsAlive` is false. The `+0x1b30`/`+0x1b34`
and `+0x1b3c`/`+0x1b40` file/line stamps are recorded in the schedule / mind traces.

## `0x102b52a0` `ResetAiState(bool, bool)` (135 bytes)

`SetEnemy(NULL)` (`0x102b52a5`); `SetLastEnemy(NULL)` (`0x10279b70`, `+0x1a94` = -1, `0x102b52ae`);
`m_afMemory &= 0xf7fc7fff` (`0x102b52c5`); with the first argument, `0x10273760` (`0x102b52cf`); the
enemy store's clear-all `0x102dfc10` on slot 541 `GetEnemies()` with `UTIL_VarArgs("%s(%d) :",
"E:\Vampire\main\dlls\AI_BaseNPCTroika.cpp", 0x5626)` (`0x102b52e3`..`0x102b52f8`); with the second,
the ideal-state stamp at line `0x562a` and `m_IdealNPCState = 1` DIRECTLY (`0x102b5319`) — no
`SetState`, no `OnStateChange`.

**Correction to the checklist:** `0x10273760` is not a hint release. Its whole body is
`InputSetRelationship(this, m_RelationshipString (+0x1584) ?: "", 0)` — the authored relationship
line re-applied. The format string is `"%s(%d) :"` (the packet's correction stands).
`0x102dfc10` pops the list head until empty; for a store that is not squad-shared (`+8 == 0`) with
an owner it calls the owner's slot 56 (`[owner]+0xe0`, `0x102b5120`) with the resolved record entity,
the record's two vectors (`+0xc..+0x20`) and the reason, then frees the record (`0x102df1b0`); a
squad-shared store calls `0x103169a0` instead. The port empties its store and runs slot 56 per popped
record (integration review; the lane dispatched no callback).
**Unrecovered:** which record words the two vectors are (the port hands `LastPosition` / `Anchor`);
the squad arm (no squad-shared store stands).

## `0x102c51a0` `DoPossession(CBaseEntity* caster)` (281 bytes)

Null caster: nothing (`0x102c51aa`). A current enemy that slot 404 `IRelationType` calls `D_HT`, then
the caster the same: `AddMiscFlag(0x800)` each (`0x102c51dd` / `0x102c51f9`). `DisconnectFromSquad`
(`0x102c5200`); `ResetAiState(1, 1)` (`0x102c520b`); slot 304 `GiveBaseFightingItems` (`0x102c5214`);
`m_bfAINPCFlags2 |= 0x80840000` (`0x102c5226`); a player caster: `InputSetRelationship("player D_LI
99")` (`0x102c523f`); `SetFollowerBossName(caster)` (`0x102c4470`, `0x102c5247`);
`SetFollowerType("Combat")` (`0x102c5253`); the ideal-state stamp at line `0x6e42`, `m_IdealNPCState` =
1, `SetState(1)` (`0x102c525c`..`0x102c527a`); `SetTarget(caster)` (`0x102c5282`); `m_hFriendPlayer`
(`+0x60ac`) = caster (`0x102c5292`); slot 614 `ResetThinkTimers` (`0x102c529a`); `m_bfNPCFrenziedFlags`
= `0x3b1c` (`0x102c52a4`); slot 595 `AcquireNearestHatedTarget` last (`0x102c52ae`).
**Unrecovered:** the discipline effect that calls it is not wired in the port.

## `0x102c5310` `DoFrenzy(CBaseEntity* caster)` (260 bytes)

As `DoPossession` up to `ResetAiState(1, 1)` (`0x102c537b`), then slot 595 FIRST (`0x102c5384`) and slot
304 (`0x102c538e`); `m_bfAINPCFlags2 |= 0x80820000` (D_INSANE, `0x102c53a0`); the player line
(`0x102c53b9`); the stamp at line `0x6e7f`, `m_IdealNPCState` = `0xb` HUNT, `SetState(0xb)`
(`0x102c53c2`..`0x102c53e0`); `m_eInvestigateMode` = `m_eInvestigateModeCombat` = 6 (`0x102c53ec` /
`0x102c53f2`); `m_hFriendPlayer` = caster (`0x102c53ff`); `m_bfNPCFrenziedFlags` = `0x9fbd`
(`0x102c5405`). No `SetTarget`, no follower boss, no slot 614.

## `0x10395c70` `MingXiaoEnterDeath` (79 bytes)

Stamp `NPC_VMingXiao.cpp:0x916` (`0x10395c73`); `0x10398870` (`m_iTentacleID != -1`, a proxy) picks
`0x16e`, else `0x16d` (`0x10395c92`); `SetSchedule(id, TRUE)` — forced past `IsAlive` (`0x10395ca0`);
`m_lifeState` = 1 (`0x10395ca5`), `m_bPlayedDeathAnim` (`+0x6744`) = 1 (`0x10395caf`), `m_bInvincible`
(`+0x63d8`) = 1 (`0x10395cb6`). `0x10395ce0` is the latch in front of it.
**Unrecovered:** `m_lifeState` has no port word; the new `LifeStateRetail` is written and nothing
reads it (the port's `IsAlive` reads the death latches).

## `0x1039e970` `MingXiaoTentacleEnterDeath` (171 bytes)

Switch on `m_ePhase` (`+0x6670`, `0x1039e97c`): 2 answers `0x16a` at line `0x590`; 3 asks
`0x10295460(0x1d, false)` — a sequence answers `0x16a` at `0x598`, none `0x16c` at `0x59d`
(`0x1039e9a3`); everything else first runs `0x1039f310` then answers `0x16b` at `0x589`. Install forced
(`0x1039e9fc`); `m_lifeState` = 1, `+0x6699` = 1, `m_bInvincible` = 1 (`0x1039ea01`..`0x1039ea12`).
`0x1039f310` emits `table[RandomInt(0, 0)]` of the ONE-entry table `0x106477cc`
(`character/monster/ming xiao/tentacle_flopping_loop.wav`) on channel 2 at volume 1.0, attenuation 0.8,
pitch 100, with flags `SND_STOP` (`PUSH 0x4`, `0x1039f399`): it STOPS the flop loop that `0x1039f1a0`
starts with flags 0 (integration review; the packet and the lane read it as playing a death sound). **Unrecovered:** the port has no retail-id sequence table (`0x10295460` seam answers -1).

## `0x103aa3b0` `CNPC_VSabbatLeader::StartTransformation` (146 bytes)

`m_bActivated` (`+0x66b8`) = 1 FIRST (`0x103aa409`) — slot 461's gate; `AddClassRelationship(1 player,
D_HT, 10)` (`0x103aa410`, `0x10013cf5` -> `0x10332aa0`, which overwrites the class row at any
priority); stamp `NPC_VSabbatLeader.cpp:0x549`; `SetSchedule(0x163, false)` (`0x103aa432`). The body
is the datamap INPUT `StartTransformation` itself (`RET 0x4`, the inputdata argument unread); the port
binds the input to it.

## `0x10397410` `MingXiaoSpawnTentacle(index)` (981 bytes)

1. `+0x6714` = index (`0x10397424`); stamp line `0xcac`; `SetSchedule(0x16c, false)` (`0x1039743e`).
2. Throwable mode 1/2: the throw cleanup `0x10398fd0`; 3/4: only when index == `m_eThrowingTentacle`
   (`0x1039745f`); else nothing.
3. The burst emitter at the limb's bone (`0x10398680` names it, `0x102c42a0`, `0x10397480`); the bone
   position (`0x10398630`: `LookupBone`, bone 0 on a miss); my angles (slot 219); the hull `0x11`
   extents.
4. The search (`0x10397523`..`0x1039762c`): `IsAreaClear(spot, 0x2400b, mins, maxs)` first at the bone,
   then the compass `(+r,+r) (+r,0) (+r,-r) (0,-r) (-r,-r) (-r,0) (-r,+r) (0,+r)` with r from 20.0
   growing by 20.0 per ring; Z never changes; `DevMsg("ERROR:  FAILED TO FIND SPAWN LOC FOR TENTACLE.")`
   on every try past 31; never gives up.
5. `0x10310cf0`: the maker entity named `TentacleGenerator` (`0x10390d00` binds the static reference)
   makes one NPC (`MakeNPC(1)`) and places it at the spot; none: return NULL.
6. Its angles := mine (slot 218); its velocity := (normalize(bone - my origin) * 100 + (RandomFloat(-10,
   10), RandomFloat(-10, 10), RandomFloat(5, 20))) * 0.1 (`0x103976b4`..`0x10397766`); slot 614; its
   `m_iTentacleID` (`+0x6660`) = index; `ShareEnemyWithAlly` (`0x1039777e`); its `m_hMingXiao`
   (`+0x665c`) = me; `m_rhSeveredTentacles[index]` = it; mask `|= 1 << index`; connected count `-= 1`;
   `BodyGroup`; `m_flIdealRange` (`+0x6748`) = `0x103986b0` (`0x103977d4`).

`0x103986b0`: 400 for each of limbs 0/1 and 300 for each of 2/3 summed, divided by TWICE the count;
300 when none. **Corrections to the packet:** the random jitter is on the VELOCITY, not the spawn
spot; the two sub-object writes are `+0x6660` and `+0x665c` (`[0x17]+0x330`/`+0x32c` at a
`0x450`-byte stride). **Unrecovered:** bone positions (seam: origin); the port's `IsAreaClear` takes
the NPC's own extents, not the `0x11` hull.

## `0x10397e90` / `0x10397f00` — the six-handle sweeps (69 bytes each)

`m_rhSeveredTentacles[6]` (`+0x66a8`) -> each live tentacle's `0x1039ea60`; `m_rhProxies[6]`
(`+0x668c`) -> each live proxy's `0x10395ce0`. Six hard-coded; nothing cleared.

## `0x10395750` `MingXiaoApplyTentacleDamage(index, info, weapon)` (696 bytes)

1. A weapon whose slot 360 word has `0x18000`: `m_flDamage` = tuning `+0x28` * magnitude
   (`0x10395766`..`0x103957a4`).
2. index <= -1 (`0x103957ae`): `-2` with at most 2 limbs (`0x10395926` / `0x10395932`) stamps line
   `0x8a5`, `SetSchedule(0x16b, false)` and runs the throw cleanup for modes 1..4, damage untouched;
   anything else rescales: `m_flDamage = (int)max((int)magnitude / tuning +0x24, 1.0)`.
3. A connected limb (`0x103957c0`): `m_rflHitPoints[i] -= magnitude`; at or below 0.0 (ordered,
   `0x103957f5`) the limb is lost (`0x10397410`); the damage emitter at the packet position with my
   angles; not invincible: `0x1023e4b0(this, (int)magnitude)`; `m_flDamage` = 0; `0x101c2b10(info, 1)`.
4. A severed limb: the rescale.

The tuning cells are the `Ming_Xiao_Info/General` rows of `Rules.txt` (`0x101e6310`): `+0x24`
"NohitDamageDivide" (default 4.0, `0x101e73d7`), `+0x28` "MeleeDamageScalar" (2.0, `0x101e73ee`);
`0x103952b0`'s `+0x2c` is "MeleeTentacleHitPercent" (int, 20, `0x101e7405`); `0x10397930`'s `+0x4` is
"TentacleHPRegrown" (160.0) and `+0x0` "TentacleHPInitial" (200.0). The weapon's slot 360 word is
read as the weapon record's family (melee -> `0x18000`), as `0x10395650`'s port reads it.
**Unrecovered:** `0x1023e4b0`'s consumer (a 25-slot global queue; recorded); the packet's `+0x1c`
position and `+0x4a` byte (no field); the initial `m_rflHitPoints` writer (`Spawn` `0x103927a0`,
lane L08) — until it lands the words start at 0.

## `0x103c43b0` `CNPC_VTzimisceRunner::vfunc330` (42 bytes, Damaged19)

`shooter = info->+0x94 ? info->+0x94->+0x9c : 0` (`0x103c43b6`..`0x103c43c0`); overwrite the two stack
arguments with `(shooter, 0x79)` and tail-jump slot 320 `PlayerKnockbackReaction` (`0x103c43d4`).
The float is never read; there is no jump table.
