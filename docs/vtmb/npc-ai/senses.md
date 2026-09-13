# NPC AI — Senses, sound and memory

Part of the [NPC AI oracle](./README.md). Sections moved verbatim from
`npc-ai-reverse-engineering.md` on 2026-09-13; their headers are the citation keys.

## Perception, sound, memory, and hostility

### Visual and auditory input

Map fields provide per-actor sensory tuning. The exact player-light target surface, visual-range
and cone scaling, closest-player LOS cache, sound-radius reduction, and found/lost transition are
owned by [stealth.md](../stealth.md). `sound_volume_table.txt` classifies sound levels and occlusion:

| Level/example | Radius/value | Occlusion behavior |
|---|---:|---|
| quiet | 180 | occludable |
| normal | 240 | occludable |
| loud | 1200 | non-occluded |
| player stealth | 120 | authored special value |
| feeding | 240 | authored special value |

Named sound categories include footsteps, gunshots, impacts, `NPC_TAKE_DAMAGE`, `NPC_FLEE`,
`NPC_DISCIPLINE_ALERT`, doors, explosions, and physics sounds. A combat incident can therefore
propagate to actors that did not see the original attacker. This matches both the native
`HEAR_COMBAT` interrupt condition and the authored `OnHearCombat` output.

The native object retains last-seen relation-category targets, last-heard stimulus, damage state,
current enemy, last enemy, and occlusion state. Perception is thus a memory-producing subsystem,
not a per-frame yes/no raycast. Losing current line of sight does not necessarily erase the enemy
or force a return to idle.

## The sense pass for a hated player, walked (2026-09-08)

Recovered against `sp_tutorial_1`'s `thug_1` (`npc_VVampire` via `npc_maker`, `vision 540`,
`hearing 1.00`, `npc_perception 3`, `player_reaction "D_HT 5"`). The chain is
`CAI_BaseNPC::GatherConditions` (`0x1026ec30`) → `PerformSensing` (`0x1026e4f0`, gated on
`m_iIsOblivious < 1`) → `CAI_Senses::PerformSensing` (`0x10310710`, gated on `senses+0x80
m_bCanPerformSenses`) → `Look(m_LookDist)` (`0x1030ff10`) then `Listen()` (`0x1030f940`).

**Look.** `CAI_Senses` is built in `0x1027cc10` with **`m_LookDist = 3072.0`** (`+0x10`, datamap,
save-restored, no other writer) and three cadences: **players every 0.15 s** (`+0x68/+0x6c`), NPCs
0.25 s, objects 0.45 s. `LookForPlayers` (`0x1030fff0`) walks client slots with a `3072²`
distance prefilter; `LookForNPCs` (`0x10310130`) walks the global AI list the same way. Per
candidate (`0x1030ffa0`): not already seen (`0x1030fb90`); `0x1030fa00` — alive (slot 158),
not `FL_NOTARGET` (0x8000), not `spawnflags & 1`, `+0x1480` targetable, not hidden
(`0x100b5190`), then **`QuerySeeEntity` slot 468 = `0x102b38b0`**; then **`FInViewCone` slot
363 = `0x102b4540`** and **`FVisible` slot 201 = `0x102b4630`** (trace mask `0x2804091`); then
`0x1030fc50` dispatches **slot 472 (`0x102b3e00`, the attention path)** and appends to the seen
list. `QuerySeeEntity` admits **any player** regardless of relation (`ent+0xa8 != 0`) and
non-players only at `D_HT`/`D_FR`; rejected by `DAT_10924fba` (`npc_ignore_senses`),
`DAT_10924fb9` (`npc_ignore_player`) and the frenzy friend-player.

**Cone.** `CBaseCombatCharacter::FInViewCone` (`0x10326750`) → `FinViewCone3dNew`
(`0x103264d0`; the 2-D variant when the cvar object at `0x10936f74` reads 2). Inputs:
`m_flFieldOfView`, the candidate position (slot 192) and the candidate-side cone scalar (slot 29,
`CHL2_Player` → `m_flStealthVisionCone +0x1c74`). Anything strictly behind (`dot < 0`) is out.
**`CAI_BaseNPCTroika` spawn (`0x10298d30`) sets `m_flFieldOfView = 0.2`** — ±78.5°, ≈157°
total, for every VtMB humanoid. Player 0.5; `CNPC_Crow` −1.0; `CNPC_VMingXiao`/`VTzimisce` −0.5.
UNRECOVERED: the body-offset term inside `0x103264d0` (register-garbled decompile).

**The Troika cone override `0x102b4540` (slot 363), walked.** Every sight admission and the
sound sweep's `SEE_SOUND_SOURCE` stranger arm dispatch the cone test virtually, so on a VtMB NPC
the function that actually runs is this override, not the base:

1. `target == NULL` → false.
2. `DAT_10924fba` (`npc_ignore_senses`) → false.
3. `DAT_10924fb9` (`npc_ignore_player`) and `target+0xa8` (the target is a player) → false.
4. Slot 293 (`FUN_102c5470`, also `GetFollowerBoss`) resolves `m_hFollowerBoss` (`+0x647c`) and
   returns `boss+0x9c` (the cached `CBaseCombatCharacter*` self-pointer). If that equals
   `m_hClosestPlayer` (`+0x628c`), `target+0x98` is non-null, and `*(target->+0x98 + 0x6279)`
   (`m_bInPlayerLOS`) is set → **return true, skipping the cone entirely**.
5. Otherwise the base `CBaseCombatCharacter::FInViewCone` `0x10326750`.

`ai_ignoreplayers` is HL2's name and is **not in the image**. The two bytes are the live state
of ConCommands `npc_ignore_player` / `npc_ignore_senses` (constructors `0x10088c70` /
`0x10088d70`, help `"NPCs will not hear or see the player"` / `"NPCs will not hear or see
anything"`, toggles `0x10088be0` / `0x10088ce0`, `DevMsg` at `0x1054c188`). Default both 0.
The same bytes also gate `QuerySeeEntity` `0x102b38b0`, `QueryHearSound` `0x102b35b0`, and
Troika `FVisible` `0x102b4630`.

Arm 4 is not "accept the player at any angle". `+0x98` is the Troika self-pointer
(`CAI_BaseNPCTroika` ctor `0x1028d3bc` `MOV [ESI+0x98], ESI`; `animation_and_movers.md`); a
player does not write it, so a Look at the player cannot take the arm. The arm is: a follower
of the closest player, looking at an NPC whose `m_bInPlayerLOS` is set, skip the cone.
`thug_1` has no `follower_boss`. `+0x9c` is the CC self-pointer (`PrecacheSoundTable`
`0x1009d460` calls `IsMale` on it; choreo `CAMERAMOVE` uses `tgtEnt+0x9c` the same way).
`+0xa8` is the player self-pointer, the existing "is player" test. `OnLooked` (`0x1026a2c0`)
reads observer `+0x98` through slot `0x928` (`GetBestSeeUnknown`) to skip one seen entity; that
is the same self-pointer, not a separate "lead to follow" object.

**Port (6b).** `FElysiumNpcSenses::IsInViewCone(Npc, Entity)` is slot 363: arms 2–3 as ConVars
`npc_ignore_player` / `npc_ignore_senses` (the spec's ConVar job; the toggle+`DevMsg` command
bodies are not bytecode-observable), arm 4 a named seam that currently answers nothing (no
follower field, no target Troika overlay producer), then the 6a point body. Look and 10a's
stranger arm dispatch the entity overload. The closest-player `bPlayerInCone` cache keeps the
point body: retail `SetPlayerLOS` `0x10291610` does not call `FInViewCone`. The same two
ConVars also gate the inlined QuerySeeEntity, QueryHearSound, and `IsVisible`. Frenzy-friend
(`m_bfNPCFrenziedFlags & 0x800`) remains a 16c seam.

**Admission body `0x102b4760` (slot 594), exactly.** `+0x6081` (outer-band byte, no datamap
name) is cleared per call. `vecMe = EyePosition()`, `vecHim = target EyePosition()`. The range
test runs only when `(m_NPCState != 2 || m_bEnemyWentOccluded(+0x5bc5) != 0) && curtime >=
m_flStealthVisionOverrideTime(+0x6604)`: `d = |vecHim − vecMe|` (true 3-D);
`radius = target->vtable[+0x70]() /*m_flStealthVisionScalar*/ × m_flSeekDistInspection(+0x63b8)`;
`d > radius` → false (strict); `d > 0.7·radius` → `+0x6081 = 1`. Then the concealment test
`0x10146b20(targetCC, this)` (Obfuscate stats `0xe`/`1` > 0, or beyond the per-observer obfuscate
radius from `DAT_10738d10` `+0xac`) → false. **Two bypasses:** a COMBAT-state NPC with a
non-occluded enemy admits every candidate on range out to the 3072 prefilter (the only unbounded
sight path in retail); and `+0x6604` (`m_flStealthVisionOverrideTime`), which spawn
(`0x1029a0b0`) clears and the Troika damage override (`0x102beda0`) extends through
`0x1028e8b0` → `0x1028e940` by five seconds after a surviving hit with an attacker. It bypasses
only the ordinary range test; it does not alter `IRelationType` or itself create an enemy-memory
actor record. `CAI_BaseNPCTroika::FVisible` (`0x102b4630`) = slot 594 →
`HasStatusEffect(Dominate_BrainWipe)` (`0x1033d2f0`) → `CBaseEntity::FVisible` trace.

**`OnLooked` and the memory write.** Troika `0x102b39a0` (slot 469) adds only
`if (HasCondition(NEW_ENEMY 0x54)) m_iEnemySightings++` and chains to `CAI_BaseNPC::OnLooked`
(`0x1026a2c0`): clear the SEE family (table `0x105c979c`, 6); compute the one skipped entity
(`GetBestSeeUnknown` slot 586 → `m_hBestSeeUnknown +0x6088`, or `m_hLastSeeUnknown +0x608c`
under `IGNORE_UNKNOWN`); for each seen entity `rel = IRelationType(ent)`:
- player (`ent+0xa8`) → **`SetCondition(0x5a COND_SEE_PLAYER)`** (`0x5a`, not `0x6f` — `0x6f` is
  `HEAR_PLAYER`), plus the player-side observer publication `0x1017ff40` when `m_bIsBCCTargetable`;
- `rel != D_NU`: `ent == GetEnemy()` → `SEE_ENEMY 0x46`; **`D_HT`**: `p = IRelationPriority(ent)`
  — `p < 0` → `m_hLastSeenDislikeEnt`, `SEE_DISLIKE 0x45`; `0 ≤ p < 11` → `m_hLastSeenHateEnt`,
  **`SEE_HATE 0x43`**; `p ≥ 11` → `m_hLastSeenNemesisEnt`, `SEE_NEMESIS 0x5b`; then
  **`UpdateEnemyMemory(ent, origin)` slot 544**; **`D_FR`** → `m_hLastSeenFearEnt`,
  `UpdateEnemyMemory`, `SEE_FEAR 0x44`; `D_LI`/`D_NU` nothing. (`flags2 & 0x10000` on a `D_HT`
  target diverts into the FEAR arm.) So the shipped `player_reaction "D_HT 99"` rows raise
  `SEE_NEMESIS`, not `SEE_HATE`; `"D_HT 5"` raises `SEE_HATE`. This also closes `COND_SEE_DISLIKE`'s
  disposition: it is `D_HT` with a negative `IRelationPriority`, not a separate `Disposition_t`.

**The attention path `0x102b3e00` (slot 472).** For every candidate that passed cone + trace:
`if (!(flags2 & 0x4000000 NO_UNKNOWN_VISION) && +0x6081 && ShouldInvestigate(ent, false))`: a
player passing `0x101671a0` (visible, or in a grapple with `+0x1540 == 3`, or `FL_DUCKING` and
`!0x101672d0`) → `SetCondition(0x01 SEE_UNKNOWN)`; if `m_hBestSeeUnknown != ent`: set it,
`m_iEnemySightings(+0x60a8)++`; if it equals `m_hLastSeeUnknown` → `+0x60a4
m_iSeeUnknownRepeatSightings++`, clear `IGNORE_UNKNOWN|MADE_INITIAL_RESPONSE`; else fire
**`OnUnknownVisionPlayer`**, `m_hLastSeeUnknown = ent`, `m_vecLastSeeUnknownPos(+0x6090)`,
repeat = 0, `m_flSeeUnknownRunTimer = curtime + RandomFloat(10, 20)`, `m_flSeeUnknownStartTimer
= curtime + RandomFloat(5, 10)`, clear `LOOKED_AT_UNKNOWN|IGNORE_UNKNOWN|MADE_INITIAL_RESPONSE`.
A non-player in the band sets `ATTACK_UNKNOWN` (flags1 `0x800000`). Outside the band:
`m_hBestSeeUnknown` cleared if it was this entity, the three flags cleared. The band arm and the
`SEE_HATE` arm are not exclusive — one candidate can carry `SEE_PLAYER + SEE_HATE + memory` and
`SEE_UNKNOWN` in one pass.

**The numbers for `thug_1`.** `vision 540` is not the sentinel → `+0x63b8 = 540`, perception
inert (it would have given `Inspection_Vision_Distances[3] = 440`). `StealthVisionScalarTable`
spans `0.14 … 1.00`. Effective radius **75.6 … 540 units**; band `0.7×`; cone `0.2` × player cone
scalar `0.50 … 1.00`; LOS mask `0x2804091` eye to eye. At 2380 units no retail sight path admits
the player.

## The enemy memory — `CAI_Memory` (2026-09-08)

`UpdateEnemyMemory` (`CAI_BaseNPC::FUN_102709c0`, slot 544, **no override in any of the 77
classes**): `GetMemory()` (slot 541, `0x10273e10`); squad dedupe on `ent+0x94` (inert for a
player); `IsEluded` (`0x102e0210`: walk `mem+0xc`, match entry `+0x24`, return `+0x35`) → slot
494; then `CAI_Memory::UpdateMemory` `0x102df700(mem, navigator+0x2c, ent, pos, vel)`.

Record (list head **`mem+0xc`**, stride from `0x102df130`):

| off | meaning |
|---|---|
| `+0x00` | last known position |
| `+0x0c` | anchor position (re-latched when moved further than `[0x10497c80]` = 0.0 — always) |
| `+0x18` | last known velocity |
| `+0x24` | **actor EHANDLE** (`-1` for a position-only record) |
| `+0x28` | last-seen `curtime` |
| `+0x2c/+0x30` | nearest nav node ids (`0x102f41b0`), `-1` with no network |
| `+0x34` | position-only byte |
| `+0x35` | **eluded byte — 0 on every create and every refresh** |
| `+0x38` | next |

`CAI_Memory::RefreshMemories` (`0x102df320`) drops an entry only when its handle dies or its
NPC's state (slot 464) is 7 (dead); otherwise it re-copies the target's origin while `curtime <
lastSeen + m_flFreeKnowledgeDuration`. **No time-based expiry**: once sensed, the player is a
permanent `BestEnemy` candidate until eluded or the map ends. `BestEnemy` (`0x102743c0`) walks
this list and nothing else — gates: handle resolves, `!FL_NOTARGET`, `+0x1480`, `!= this`,
`IsAlive`, `IsValidEnemy` (slot 479 = `0x101a6820`, `return 1`, **no override anywhere**),
relation ∈ {D_HT, D_FR}, `!eluded`; ranking reachability (`+0x848`) > `IRelationPriority` >
integer distance, visibility = `senses->DidSeeEntity` (`0x1030fb10`) or `FVisible`.

**Port consequence.** This runtime's `BestEnemy` walked the world entity list gated on the
relationship table (`ElysiumNpcEnemy.cpp`), so a `D_HT` player anywhere on the map was a
candidate the moment an NPC spawned; verified live on `sp_tutorial_1` 2026-09-08 (`thug_1`
committed the player at ~2380 units with 0 sightings and 10 failed LOS checks, and ran to him).
The record store above is the missing subsystem; see `docs/specs/0002-npc-ai/spec.md`.

## Hearing, walked (2026-09-08)

**Insertion.** `CSoundEnt::InsertSound` `0x101bac90` (thunk `0x1000bca8`) `(iType, origin,
iVolume, flDuration, bOccludable, pOwner)`. Records are stride `0x2c` at `CSoundEnt
(DAT_1072c464) + 0x464`: `+0x00` owner EHANDLE, `+0x04` type bitmask, `+0x08` volume/radius
(**int**), `+0x0c` insertion `curtime`, `+0x10` expire, `+0x14` occludable, `+0x18/+0x1c` links,
`+0x20` origin. Types (DevMsg switch `0x101badab`): `1 COMBAT`, `2 WORLD`, `4 PLAYER`,
`8 DANGER`, `0x10 BULLET_IMPACT`, `0x20 CARCASS`, `0x40` unnamed, `0x80 GARBAGE`,
`0x100 THUMPER`, `0x200 BUGBAIT`, `0x400 PHYSICS_DANGER`, `0x800 FLINCH`. A player-owned insert
at the "loud" row (`VolumeLevels[3]`, 1200) also calls the masquerade hook
`0x10227a30(&DAT_10750cb4, player, 6)`. **Player footsteps never call `InsertSound`**: the
player's one reserved `CSound` is rewritten every think by `UpdatePlayerSound` (`0x1016b480`;
`footsteps.md` §2.5) — type 4, radius from `sound_volume_table.txt` (sneak 180, walk/run 240,
jump 240, land soft 180, land hard 240), `m_flTime = curtime`, so the freshness gate below
always passes. The only type-4 `InsertSound` is the **door** (`0x100ee560`: `DOOR_STEALTH` 250 /
`DOOR_NORMAL` 500, duration 2.0, owner = the activator, skipped on `spawnflags & 0x1000`).

**Listen** (`CAI_Senses::Listen` `0x1030f940`): `mask = GetSoundInterests()` (slot 473; VVampire
`0x103846e0`); for each active sound with `type & mask` and `CanHearSound` (`0x1030f7b0`) link it;
`OnListened` (slot 470); `senses+0x84 = curtime`. `CanHearSound`, asm-exact: owner hidden
(`0x10014da8`) or not targetable (`+0x1480 == 0`) → reject; owner == me → reject; **`sound.time
<= senses+0x84` (not inserted since my last Listen) → reject**; `d = |origin − EarPosition()|`
(slot 196, 3-D); **`radius = HearingSensitivity() × (int)volume`** — slot 476 `0x101aa5f0` returns
`+0x63c0` verbatim, so `hearing` is a **multiplier on the sound's radius**, never on distance;
`AdjustSoundDistForStealth` (`0x1009d850`, in place, any owner with the stealth surface,
`StealthHearingDistTable` 0…80 units); occlusion `0x102703f0` when occludable; `d > radius` →
reject; then `QueryHearSound` (slot 467, Troika `0x102b35b0`): `DAT_10924fba`
(`npc_ignore_senses`); `DAT_10924fb9` (`npc_ignore_player`) for a player owner; frenzy friend;
owner == self; the same concealment test as sight
(`0x10146b20`) — **Obfuscate silences as well as hides**; **type 4 from a player →
`CStealthKillRules::InDeafZone(&DAT_1072c540, player, this)` ⇒ false** (the deaf arc suppresses
the player's movement sound outright); `flags1 & 0x20400` (COWERING bit 10, SLEEPING bit 17)
→ a second test at **`HearingSensitivity × volume × 0.25`** (`[0x1044bef8]`).

**`OnListened`** (`CAI_BaseNPC` `0x1026a5e0`): clear the HEAR family (table `0x105c97b4`, 10),
zero the six-dword heard-type field at `+0x5ca8`, map type → condition: `1→0x6d HEAR_COMBAT`,
`2→0x6e HEAR_WORLD`, `4→0x6f HEAR_PLAYER`, `8→0x6a HEAR_DANGER`, `0x10→0x70 HEAR_BULLET_IMPACT`,
`0x100→0x6b HEAR_THUMPER`, `0x200→0x6c HEAR_BUGBAIT`, `0x400→0x71 HEAR_PHYSICS_DANGER`,
`0x800→0x72 HEAR_FLINCH`, others DevMsg and dropped; `0x101b9920` false → `0x5e SMELL`. **The
condition is not set immediately** — it is queued on `m_DelayedConditionList` with delay slot 471
(`0x1026a8a0` = `RandomFloat(0.2, 0.9)`; `HEAR_FLINCH` `RandomFloat(0, 0.5)`), then `0x102cc760`
promotes what is due. Outputs: `OnHearWorld` on `0x6e`, **`OnHearPlayer` on `0x6f`**,
`OnHearCombat` on `0x6d || 0x70 || 0x6a`. The Troika override `0x102b39e0` then snapshots one
`CSound` per heard type into **seven** records — `m_LastSoundWorld +0x61e4`, `PhysicsDanger
+0x6134`, `Danger +0x6108`, `Player +0x61b8`, `BulletImpact +0x618c`, `Combat +0x6160`, and
**`Flinch +0x6210`** (a seventh, missing from the sweep section above) — and calls
`0x1028e8b0(this, owner, 1.0)` for `HEAR_COMBAT`/`HEAR_BULLET_IMPACT` only (forwarded to
`0x1028e940` only when the owner is or shares my enemy).

**Hearing cannot acquire an enemy.** Nothing in the hear path writes `CAI_Memory`.
`HEAR_PLAYER` yields the delayed condition, the `m_LastSoundPlayer` copy (on the NPC, not in
memory), `OnHearPlayer`, the idle→alert promotion in `CAI_BaseNPC::SelectIdealState`
(`0x1026f660` case 1, no `m_bNoAlertState` test), and via the sound sweep + `ShouldInvestigate`
`COND_INVESTIGATE_SOUND 0x25`. An NPC turns hostile from a noise only by walking to it and then
seeing the player there.

**`CommitBestSound` `0x102b4090`** (the sweep→task seam): copies the winning record into
`m_BestSound +0x60b0`, `+0x5b78 = owner`, mirrors to `+0x60dc`. Priority, first match:
`HEAR_COMBAT` (+0x6160) > `HEAR_BULLET_IMPACT` (+0x618c) > **`HEAR_FLINCH` (+0x6210)** >
`HEAR_PLAYER` (+0x61b8) > `HEAR_DANGER` (+0x6108) > `HEAR_PHYSICS_DANGER` (+0x6134) >
`HEAR_WORLD` (+0x61e4). `CAI_BaseNPCTroika::GetBestSound` (slot 474, `0x102b4520`) returns
`&m_BestSound` unconditionally, so the "no best sound → `TaskFail`" arms of
`TASK_ALERT_LOOK_AT_BEST_SOUND` / `TASK_GET_PATH_TO_BESTSOUND` are dead on every Troika NPC.

**NOT PORTED: the two records are one.** `CommitBestSound` writes the winner to `m_BestSound`
(`+0x60b0`) and then mirrors it to `m_InvestigateSound` (`+0x60dc`); the runtime carries a single
`FElysiumNpcMemory::BestSound`. They are distinguishable in retail — `+0x60b0` is the volatile
task-facing record `GetBestSound` (slot 474) hands out and every commit overwrites, while `+0x60dc`
is the sticky saved decision copy that `ShouldInvestigate`, `GetSchedule` (`0x102ae920`),
`0x102993c0` and `0x10299700` read, and which `FUN_102b9060`'s `HEAR_WORLD` arm writes WITHOUT
going through `CommitBestSound` (so that arm updates `+0x60dc` and leaves `+0x5b78` stale). None of
those readers exists yet, so the collapse is not observable today; the story that builds the sound
selectors must split the field before wiring `FUN_102b9060`.

**Port R6 state boundary (2026-09-08).** `FElysiumNpcSenses` keeps actual `Look` candidates
separate from the two-second closest-player/PVS/LOS cache: 3072-unit prefilter, then player
0.15 s, NPC 0.25 s, object 0.45 s cadence, full 3-D apex/cone/scalar test, admission, and trace.
Only an actual D_HT/D_FR observation writes `FElysiumNpcEnemyMemory`; cached player LOS never
replays that write. The senses record also retains the slot-472 unknown-vision handles/position,
repeat/timers and the seven raw sound snapshots. `m_flStealthVisionOverrideTime` is saved as the
live five-second range-only deadline. BrainWipe/Obfuscate ownership remains an explicit target
status seam until its existing discipline state has a stable substrate accessor; no guessed flag is
used as a substitute.

### R6 integration corrections from raw bodies (2026-09-08)

- `FinViewCone3dNew` disassembly `0x103265af` rejects a negative front-plane dot before the
  apex offset; `0x1032669c` multiplies the normalized viewing cosine by the **target scalar**,
  then compares against the observer's FOV. A smaller scalar narrows the cone. The unnamed
  `0x10937a8c` ConVar's default remains unrecovered; `ViewConeBodyOffsetCm` is its explicit
  zero-answer seam, not a claim that retail's default is zero.
- `FVisible` `0x102b4630` calls `HasStatusEffect(Dominate_BrainWipe)` on **this observer**.
  `0x10146b20(target, observer)` returns permission to perceive: no active cloak, observer
  active stat `0xe` or `1`, or the ready observer detection record admitting the distance.
  Those observer stats do not cause concealment; the earlier summary inverted this branch.
  The port reads the active sheet and tracked BrainWipe effect. The cloak `+0x14dc` and
  detection record `+0x97/+0xac` are explicit fields whose effect producers remain in 0006.
- `OnLooked` `0x1026a2c0` skips the current best unknown (or last unknown under
  `IGNORE_UNKNOWN`) before the player and relationship arms. All three D_HT priority branches
  write enemy memory. `D_CALM` is on the **observer**; its D_HT diversion reaches the D_FR arm
  whose second D_CALM test suppresses that arm as well.
- `OnListened` `0x1026a5e0` queues the exact raw type's condition, calls delayed promotion,
  then fires `OnHearWorld`/`OnHearPlayer`/`OnHearCombat` with **self as activator**. Both the
  condition and output are delayed. A combined raw type is not expanded into multiple sounds.
  `0x102cc6c0` has eight pending entries; the capacity check precedes duplicate lookup.
  `0x102cc590` keeps the earlier deadline when another sound of that type arrives.
- `GetClosestSound` `0x103105d0` prefers the current enemy's sound, otherwise the nearest
  sound of that type in the current Listen. Retained snapshots are separate from delayed
  conditions. `CommitBestSound` chooses only among conditions currently raised, preventing an
  old combat snapshot from permanently outranking fresh footsteps.
- Sound interests (slot 473): Troika/humanoids/cop `0x81f` (`0x102b4070`, `0x103846e0`,
  `0x10387180`); animals `0x1f` (`0x1035f540`); cameras zero (`0x103692a0`); pedestrians
  `0x81d` (`0x103a28f0`); zombies `0x17` (`0x103df260`).
- The V2 `vdata/system/stealthkillrules.glb` source member SHA-256
  `6cf156a97285ca81f4015a01aab4b4038bf29774aa0ba9cf9b226443cc03e4ef` authors arc keys **1..10**,
  a 95-unit maximum distance and a 2.5 maximum hearing scalar. Those override loader defaults
  70 and 3.0. `InDeafZone` `0x101be710` suppresses the player's type-4 sound only when eligible,
  strictly inside the rear arc and **farther than** the minimum approach depth.

## `ambient_generic` as an AI sound source (2026-09-08)

Datamap: `+0x450 m_radius` (`radius`, audio only), `+0x4cc m_nSoundEvent` (**`sound_event`**),
`+0x4d0 m_nSoundEventLevel` (**`sound_event_level`**), `+0x4d4 m_iszSoundEventOwner`
(`sound_event_owner`). The one consumer is the Use/Toggle handler `0x101ad470`, in the branch
that starts playback, after `EmitSound`:
`if (m_nSoundEvent != 0)`: resolve the owner (`0x101ad9a0`); a null owner is legal only for
`CARCASS 0x20` / `FLINCH 0x800`, otherwise `Warning("… invalid NPC sound event …")` and nothing is
inserted; `lvl` outside `1..3` warns and clamps; `dur = GetSoundDuration(wav)` floored at 1.0;
**`InsertSound(m_nSoundEvent /*raw type bitmask*/, owner origin, VolumeLevels[lvl], dur,
bOccludable = 0, owner)`** — **once per activation, never per think**. Level 1 = 180, 2 = 240,
3 = 1200 (`0x1072bccc + lvl*4`). `radius` plays no part.

**Every `ambient_generic` in `sp_tutorial_1` carries `sound_event "0"`** — `sound_combat_1..4`,
`sound_jackflash`, `sound_window_break`, `sound_fire_2`, `sound_thugs_w_guns`, `sound_howl_2` —
so the tutorial's "gunfire diversion" (`trig_diversion → logic_gunfire`) is **audio only**: it
inserts no AI sound and no NPC hears it. `sound_event_level 2` is dead data on all of them.

## Sense and investigate leftovers, closed (2026-09-08)

Method note: the field ledger and name-based grep miss accesses the decompiler renders as a
dword index (`param_1[0x16e1]` for `+0x5b84`, `[0x188f]` for `+0x623c`); cross-check with a grep
on `0x<offset/4>]`.

- **`m_eAlertLevel` (+0x63f4)** is `FIELD_INTEGER`, `FTYPEDESC_SAVE` (flags `0x2`; `0x4` = KEY,
  calibrated on `m_iIsOblivious`/`m_bNoAlertState`). Writers: the slot-420 NPCInit
  (`0x1029a0b0`, VCamera `0x103692c0`) → 0, `FUN_102b8980` → 1/2/3, `FUN_102b8a60` and
  `FUN_102b9060` → 3. Slot 420 is dispatched only from `Spawn` bodies (and `CNPC_VPedestrian`'s
  level-reset respawn). **Nothing resets it: the turn → step → walk ladder runs once per life and
  survives saves**; every later `HEAR_PLAYER`/`HEAR_DANGER` goes straight to `0x51`/`0x52`.
- **`debug_allow_npc_hunting`** (object `0x10924478`, ctor `0x1028c840`, default **`"0"`**, flags
  0, help "Set this to 1 to allow NPCs to do their 'scouring the area' hunting state") is the
  ConVar behind `DAT_1092447c`, read only by `CNPC_VHuman::SelectIdealState` (`0x103851e0`).
  **In retail no `CNPC_VHuman` enters the HUNT state from `SelectIdealState`**; the case-0xb ladder
  and `SCHED_TROIKA_HUNT_*` are reachable only by `aiscripted_schedule`/`ChangeSchedule` or with
  the cvar flipped. `debug_allow_move_facing` (`0x10924f70`, ctor `0x1028c720`, default `"1"`,
  "If this is on, NPCs will move facing the NPC when they run for cover") is `DAT_10924f74`;
  readers `0x10278cb0/d20/d90` (facing forwarders), `NPCThink`, `0x102b93c0`,
  `CNPC_VAndreiBlood::PreTranslate_Human`. `0x10278d90` is slot 517 on every NPC class: under the
  cvar it forwards its five arguments to `CAI_Motor` slot 12 (`m_pMotor +0x30`), the motor's
  facing target; `NPCThink` calls it with `(enemy, the memory's last-known position, 1.0, 0.8, 0)`
  when `flags2` carries `MOVE_FACE_ENEMY 0x400`.
- **`m_bfNPCFrenziedFlags` (+0x5b84)**: `FIELD_INTEGER`, saved, no external name, **no name
  table anywhere** (only `NPCFlag:` `0x1030cbd0` and `MiscFlag:` `0x1030d850` parsers exist) —
  UNRECOVERED authored names. Writers: base init `0x10273390` → 0 (VCamera/VNewscaster/
  VPlayerController likewise); `CNPC_VFrenzyShadow::vfunc420` → `0x5ddf`; `CNPC_VScurrying::
  Spawn` `0x103ac430` → `|= 0x10000`; `VTzimisceHeadClaw`/`VTzimisceRunner` Spawn → `|= 0x80`;
  `SetFollowerBoss` `0x102c44e0` → `|= 0x3008`; `DoPossession` arm → `= 0x3b1c`; `DoFrenzy` arm →
  `= 0x9fbd`. Bit meanings from their readers: `0x8` always-PVS/LOS bookkeeping (`0x10290b60`,
  `0x10291230`, `SetPlayerLOS`); **`0x10` "does not witness"** — first gate of the player-law
  sweep `0x1028efc0` and a reject rung of slot 587; `0x80` suppresses the unknown-attacker
  response (`0x102ae920` ×3); `0x800` the frenzy friend-player reject in `QuerySeeEntity`/
  `QueryHearSound`; `0x2000` ally banter permission (one reader `0x102b7cf0`: `SEE_ENEMY`,
  cooldown `+0x65a4`, `!D_INSANE`, `!stay_entrenched` → `0x8c`/`0x8d`, re-arm 10–20 s);
  **`0x10000` no-flinch** — set only by `CNPC_VScurrying::Spawn`, read only by `FUN_102b9060`.
  Further readers: `0x2` (`FUN_102b5900`, `0x10385d70`), `0x400` (`FUN_102b2570`), `0x4000`
  (`CWeaponMelee::RequestActivity` `0x103e9e00`). The bits have no name table anywhere in the
  image (settled; the `Frenzied` string at `0x1061ab20` belongs to an unreferenced 10-entry mood
  table at `0x10619f24`: `Diablerist_, Kindred_, Afraid, Angry, Calm, Confused, Frenzied,
  Innocent, Obfuscated, Suspicious`, consumer unrecovered).
- **The misc-flag name table** (`0x10619ec8`, 22 pointers; name→index `0x1030d390` for the
  `MiscFlag:` parser `0x1030d850`, name→mask `0x1033cb00`, mask→name `0x1033cb50`), bit order:
  `Unconscious, D_Targeted, Allow_Fort_Soak, Allow_Thaum_Exp, Gave_Fighting_Wpns,
  Allow_Discipline_Fx, Update_Auto_Leveling, Picked_Up_Item, Obf_Bumped_Object,
  Has_Special_Dmg_Mod, Has_Special_Hit_Mod, **Was_Hateful (0x800)**, Double_Humanity_Mods,
  Feed_Bonus_Opp_Gender, Feed_Bonus_Tramps, Increased_Rat_Feed, Cannot_Rat_Feed,
  Forced_BloodShield, No_Resist_Feeding, No_Ragdoll_Death, Gain_Stealth_Atk_Bonus, Fired_Gun`.
  So the possession/frenzy arms' `AddMiscFlag(0x800)` mark the target `Was_Hateful`, read by
  `0x1033d580` ("counts as an ordinary killable human").
- **`CAI_InterestingPlace+0x57d`**: written only by the constructor `0x102d99d0` (= 0); never set
  to 1. The predicate arm is dead; only `m_bEnabled` disables a node. `+0x57e` likewise.
- **`COND_SHOULD_INTERACT`**: `FUN_102a0cb0` has zero references in the image (full `E8`/`E9`/
  absolute-dword scan; only its own uncalled link thunk `0x10013039`). `m_flNextPedInteractTime`
  is zeroed at spawn and read by nobody; `UpdatePedestrianInfo` clears `0x10` every navigating
  think. **`SCHED_TROIKA_INTERACT` (0x106) is unreachable in retail**, like `LOITER` (0x105).
- **`m_flNextInvestigateSoundTime` (+0x623c)**, `FIELD_TIME`, saved. Writers: the slot-420 init
  → 0; `SelectSchedule` at `0x102afb8d` (hunt) and `0x102b0349` (the `INVESTIGATE_SOUND`
  interrupt arm that also clears `COWER_PATH 0x200`, commits the best sound and returns `0x48`),
  `FUN_102b9060`, `CNPC_VPedestrian::vfunc438` (`0x103a29f0`), `CNPC_VTzimisce::vfunc438`
  (`0x103bb7c0`) — all `curtime + 2.0`; `FUN_102b8d20` → `curtime + 20.0`. One reader:
  the sound sweep `0x102b1cd0`, gating its whole body. **`DAT_10452dc4 = 2.0f`,
  `DAT_1044eb0c = 20.0f`**; also `[0x1049a160] = −1.0f` (the best-sound path tolerance
  override), `[0x1044bef8] = 0.25f`.
- **`+0x60dc` is `m_InvestigateSound`** (`FIELD_EMBEDDED`, saved), the `CSound` immediately after
  `m_BestSound` (`+0x60b0 + 0x2c`); `+0x60e0` its `m_iType`. Written by `CommitBestSound` and by
  `FUN_102b9060`'s `HEAR_WORLD` arm; read by `ShouldInvestigate` `0x102b3270` (the type test for
  1/0x10), `GetSchedule` `0x102ae920`, `0x102993c0`, `0x10299700`. Two copies because
  `m_BestSound` is the volatile task-facing record `GetBestSound` hands out and re-commits, while
  `m_InvestigateSound` is the sticky decision-layer copy that outlives it across saves.
- **`m_bfAINPCFlags` bit 21 = `LOOKED_AT_UNKNOWN` (0x200000), bit 25 = `FINISHED_IGNORE_UNKNOWN`
  (0x2000000)**, both set/cleared only by `TASK_SET/CLEAR_NPC_FLAG` from `0x59`/`0x62`/`0x63` and
  `0x60`/`0x61`. Bit 21 is read only by `FUN_102b8a60`; **bit 25 has zero readers** — bookkeeping
  like `MADE_OBLIVIOUS`, cleared by `OnScheduleChange`'s `&= 0xd7ffffff`.
- **`+0x6081`**: no datamap entry, not saved. Writer `0x102b4760` (`= 1` when `d > 0.7 ×
  radius`, `DAT_10457f54 = 0.7f`); readers `0x102b3e00` and the two slot-472 overrides
  `CNPC_VCop::vfunc472` (`0x10371ae0`), `CNPC_VHunter::vfunc472` (`0x103887d0`).
- **Slots.** 434 = `PrescheduleThink()` (`0x101a6560`, empty; proved by `CNPC_VSabbatLeader::
  PrescheduleThink` `0x103a7650`; called from `RunAI`; Troika does not override it). 436 =
  `OnStartSchedule(int)` (`0x101a6580`, empty; `MaintainSchedule` calls it on `m_iCurTask == 0`
  with the local id). 445 = `StartTaskOverlay()` (`0x10288710`, the move-and-shoot overlay;
  twin `RunTaskOverlay` `0x10289c90`, non-virtual). 447 = `GetLocalScheduleId(int)`
  (`0x101a6620`, `GetClassScheduleIdSpace()->ScheduleGlobalToLocal`). None of the four is
  overridden in the Troika hierarchy. `MaintainSchedule` = `0x102817c0` (VProf
  `"CAI_BaseNPC::RunAI::MaintainSchedule"`, `StartTask` slot 442, `RunTask` slot 444).
  **Slot 587** (`0x1028ef20`; overrides on `CAI_BaseHumanoid`, `VAnimal`, `VMingXiao`,
  `CNPCMaker`, `VNewscaster`, `VPlaceholder`): `IsKindred()` → false; `m_iDialog != 0` → false;
  `m_iIsOblivious > 0` → false; `frenzied & 0x10` → false; `IsBusyWithDiscipline()` → false;
  else `(pl_supernatural_flee < 3) || (pl_supernatural_attack < 3)`. Dispatched from
  `CAI_BaseHumanoid::vfunc333` and the Nosferatu player think `0x10181be0` (template
  `Player_Nosferatu`, 512-unit sphere, nearest passer → SendProp **`m_idxNosferatuRadarNPC`**
  at player `+0x1ED0`, consumed by `hud/Context_Icons/Nosferatu_Warning`). Its authored name is
  UNRECOVERED; `CanWitnessSupernatural()` stays the project name.

## The sound and scent memory readers — `0x102b4520`, `0x1026aef0`, `0x1026af30`

Slots 474 `GetBestSound` and 475 `GetBestScent` are what a schedule, a condition sweep or a
species body asks for "the sound I am acting on". They are NOT the same kind of answer.

`CAI_BaseNPC::GetBestSound` (`0x1026aef0`, 38 bytes) is the base reading: `m_pSenses`
(`+0x5cdc`)`->GetClosestSound(bScent = 0)` — `PUSH 0x0` at `1026aef7` — with a
`"Warning: NULL Return from GetBestSound\n"` dev message when the list is empty. It walks the live
retained sound list and picks the nearest audible member, so it answers about the current pass.

`CAI_BaseNPCTroika::GetBestSound` (`0x102b4520`) replaces it with **seven bytes**:
`return &this->m_BestSound;` (`+0x60b0`). Every shipped NPC therefore reads the **committed**
record — the one `CommitBestSound` wrote one sweep earlier — and not the senses object's live
answer. The address, not a copy: a caller mutating it mutates the NPC's memory.

`CAI_BaseNPC::GetBestScent` (`0x1026af30`, 38 bytes) is `GetBestSound`'s base body byte for byte
with `PUSH 0x1` at `1026af37` and its own `"Warning: NULL Return from GetBestScent\n"` string, and
**the Troika line does not override it** (`CAI_BaseHumanoid#475`, `CAI_BaseNPC#475`,
`CAI_BaseNPCTroika#475`, +74 more all hold `0x1026af30`), so this IS the dispatched body on every
`npc_V*`. The scent channel and the sound channel are the same retained list read with a different
filter flag.

**Not built:** the port has no "closest of the live list" accessor — `FElysiumNpcSenses` retains one
record per CSound family and commits a winner — and no producer emits a scent at all, so both base
bodies answer null and warn, which is retail's own answer on a map with no scents.

## The point-visibility test and `CAI_BaseActor::ValidHeadTarget` — `0x1028ebc0`, `0x1025ea00` (2026-09-13)

`0x1028ebc0` is 674 bytes and three gates. It takes a struct whose `+0x08..+0x10` is a position,
calls `EyePosition()` (slot 193, vtable `+0x304`), and asks
`CBaseCombatCharacter::FInViewCone` (`0x103268e0`, which picks `FinViewCone2d` or `FinViewCone3dNew`
off the ConVar `DAT_10936f74` and passes `m_flFieldOfView` at `+0x1574`). If the cone admits the
point, the squared eye-to-point distance is compared against `m_flVisionDistance` (`+0x63b8`)
squared — retail computes the *beyond* predicate and takes the failing path when it is true. Inside
the band it builds a ray from the eye to the point, traces it, runs a ConVar-gated debug-overlay
pass (`thunk_FUN_10143d80` / `thunk_FUN_10142e90`) that changes nothing the trace answered, and
reports pass only when the fraction equals `_DAT_10449280` (**1.0**). So: in my cone, within my
vision distance, and with a clear line.

`0x1025ea00` fills `CAI_BaseHumanoid#588` and is `CAI_BaseActor::ValidHeadTarget(const Vector&)`,
which `PickLookTarget` (`0x1025f1c0`) applies to a candidate's eye point before adding a look
target. It dots `HeadDirection3D()` (slot 369, vtable `+0x5c4`) with the normalised direction from
`EyePosition()` to the argument, requires the dot **strictly** above `_DAT_10497ca0`, and then
requires the absolute height difference below `_DAT_104492d0`. Both results are packed into the
return byte. It is NOT the Troika line's slot 588 (`0x10293e50`, the `ACT_DISPOSITION` restart), and
it is NOT `CBaseCombatCharacter`'s own 149-byte `ValidHeadTarget` at `0x10325da0` that
`PickLookTarget` dispatches through vtable `+0x930`.

**Unrecovered:** `0x1028ebc0` has **no caller** in the decompiled corpus and fills no vtable slot, so
what asks it is not recovered; `_DAT_10497ca0` (`ValidHeadTarget`'s dot floor) is unpinned, and
`_DAT_104492d0` is read here as a height where the melee ladder reads it as a dot, which the
decompiler flags as an overlap.

### The detected-attack notice `0x102bf560`

_Recovered 2026-09-13, story 29c-1._

`m_bIgnoreDetectedAttack` (`+0x65f5`) refuses the whole body. Otherwise the candidate is put through
the type-3 redirect `0x102707d0` — an entity whose `+0x98` combat-character pointer answers `3` at
vtable `+0x228` is replaced by its vtable `+0x184`, and everything else passes through — and the
result's `GetRefEHandle()` lands in `m_hDetectedAttacker` (`+0x65c0`), or `0xffffffff` for a null.
`m_flDetectedAttackTime` (`+0x65c4`) is then stamped with `curtime + _DAT_10454110` on **both** arms,
so a refused notice still opens the window.

**Unrecovered:** what a type-3 entity is. `_DAT_10454110` is **5.0f** — it is at `.rdata` file
offset `0x454110`, not past `.data`'s raw size, and story 29c-1 family Senses read it there while
porting `OnDoorBlocked` (`0x1027de00`), which reads the same cell.

## `CAI_BaseActor::ValidEyeTarget` — `0x1025e920` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Senses._

`CAI_BaseHumanoid#587`, 164 bytes, and the sibling of `ValidHeadTarget` (`0x1025ea00`, `#588`)
above. It is **not** the Troika line's slot 587 — that index carries `CanWitnessSupernatural`
(`0x1028ef20`) past `CAI_BaseActor`, and the two branches declare unrelated virtuals there.

The listing: `HeadDirection3D()` (slot 371, vtable `+0x5cc`) into a local, `EyePosition()` (slot 193,
`+0x304`) into another, `d = point - eye` component by component (`1025e948`–`1025e960`), a 3-D
`VectorNormalize` of `d` through `PTR_thunk_FUN_10137220`, and `dot(d, head)` compared with
`FCOMP double ptr [0x10449270]` — **0.5 as a double**, a 60-degree half-angle, strictly exceeded.
Unlike slot 364 below there is no Z zeroing: this cone is three-dimensional.

## The two aim cones — `0x10326bd0`, `0x10326ae0` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Senses._

Slot 364, `bool FInAimCone(const Vector&)` (`0x10326bd0`, 283 bytes): `d = target - GetAbsOrigin()`
(slot 217, `+0x364`), then **`d.z = 0` before the normalise** (`10326c83  MOV [ESP+0xc],0x0`), then
`VectorNormalize(&d)`, then `dot(d, EyeDirection2D())` (slot 372, `+0x5d0`) against
`FCOMP double ptr [0x1049e0c8]` = **0.994**, strictly exceeded. `_DAT_1049e0c8` has exactly one
reader in the image and is a double; read as a float it is 2.18e-25 and the cone admits everything.
0.994 is a **6.28-degree** half-angle — an order of magnitude tighter than the view cone's `0.2`
(`FinViewCone3dNew`, `0x103264d0`), which is what separates "I can see you" from "my weapon is on
you". The dot's third term is `eyeDir.z * 0` and is computed anyway. The planar normalise is the
correction this pass makes to the earlier one-line reading, and it is what decides a target directly
above or below the shooter.

Slot 365, `bool FInAimCone(CBaseEntity*)` (`0x10326ae0`, 181 bytes), is three dispatches past the
scope trace: `eye = EyePosition()`, `aim = target->BodyTarget(eye, true, false)` (slot 197, `+0x314`
— the two literal booleans are pushed at `10326b0c`/`10326b0e`, before the eye call, which is why
the decompiler attached them to the wrong callee), then `FInAimCone(aim)` through this object's own
vtable `+0x5b0`. The cone is measured to the target's **body target**, not its origin.

## The enemy accessors and the `CAI_Enemies` store — `0x101a67e0`, `0x102b5360`, `0x10027020`, `0x10273e10`, `0x10273e40` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Senses._

Slot 167 `CBaseEntity* GetEnemy() const` (`0x101a67e0`) resolves `m_hEnemy` (`+0x5ce0`) through the
handle table at `PTR_DAT_10566458` — index `& 0x1fff`, serial `>> 0xd` — and answers 0 on a stale
handle. 126 real callers, the most-called body in this family.

Slot 168, the mutable overload, has **two lines**. The base (`0x10027020`, 18 classes) is two
instructions, `MOV EAX,[ECX]` / `JMP [EAX+0x29c]` — a tail jump to slot 167 and nothing else. The
Troika line (`0x102b5360`, 64 classes) adds one arm: when slot 167 answers null **and**
`m_bfNPCStateFlags` (`+0x5b64`) bit 6 is set, it resolves `m_hLastEnemy` (`+0x1a94`) instead. That
byte is a pure function of `m_NPCState` (`0x1026e3e0`), and bit 6 (`0x40`) belongs to retail states
`0xb` (HUNT) and `0xe` alone — so the last-enemy fallback is reachable only from a hunt state.

Slot 541 `GetEnemies()` (`0x10273e10`) answers `m_pMemory` (`+0x5d88`) when
`m_iSquadDisconnected < 1` and the single global `DAT_109203f0` otherwise. The test is `< 1`, not
`== 0` — the counter is incremented on disconnect and decremented on reconnect, so a negative value
reads as connected — and it is the COUNTER that decides, never `m_pSquad`. Slot 543 `RemoveMemory()`
(`0x10273e40`) frees `m_pMemory` only when `m_pSquad` (`+0x5da4`) is null: an NPC in a squad does not
own the store it was handed.

## `OnListened`, the Troika half — `0x102b39e0` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Senses._

740 bytes, and four steps. It first chains `CAI_BaseNPC::OnListened` (`0x1026a5e0`), which clears
`m_HeardConditions` (`+0x5ca8`), walks the sense list mapping each raw `CSound` type to its
condition — `1`→`0x6d` COMBAT, `2`→`0x6e` WORLD, `4`→`0x6f` PLAYER, `8`→`0x6a` DANGER,
`0x10`→`0x70` BULLET_IMPACT, `0x100`→`0x6b`, `0x200`→`0x6c`, `0x400`→`0x71` PHYSICS_DANGER,
`0x800`→`0x72` FLINCH, anything else `0x5e` with a `DevMsg` — queues each on
`m_DelayedSoundConditionList` with slot 471's `GetReactionDelay()` (`RandomFloat(0, 0.5)` for
FLINCH), promotes the expired ones, and fires `OnHearWorld`, `OnHearPlayer` and `OnHearCombat`.

The Troika half then snapshots **seven** records, each gated on its own `m_HeardConditions` bit read
directly out of the word (`+0x5cb4 >> 0xe` is bit 110 = `0x6e`, `>> 0x11` is `0x71`, `>> 10` is
`0x6a`, `>> 0xf` is `0x6f`, `>> 0xd` is `0x6d`; `+0x5cb6 & 1` is bit 112 = `0x70`; FLINCH `0x72`
goes through the bit helper `0x102c66b0`). Each snapshot copies whatever
`CAI_Senses::GetClosestSound` (`0x103105d0`) answers for that raw type into the matching
`m_LastSound*` record — `m_LastSoundWorld` `+0x61e4`, `PhysicsDanger` `+0x6134`, `Danger` `+0x6108`,
`Player` `+0x61b8`, `BulletImpact` `+0x618c`, `Combat` `+0x6160`, `Flinch` `+0x6210`.

`GetClosestSound` itself asks its owner for `GetEnemy()` (slot 167) and `EarPosition()` (slot 196,
vtable `+0x310`) once, then walks the list: a record whose owner IS the enemy returns immediately
and ends the walk, and otherwise the nearest to the **ear** by squared distance wins, seeded at
`0x4d800000`.

The tail reads `m_Conditions` (`+0x5c5c`) rather than `m_HeardConditions` — `HasCondition`
(`0x10269aa0`) is indexed off `+0x5c5c` — and for `HEAR_COMBAT` (`0x6d`) and `HEAR_BULLET_IMPACT`
(`0x70`) feeds the corresponding record's resolved owner into `0x1028e8b0` with strength `1.0`, the
stealth-vision override extension. So a sound still inside its reaction delay snapshots its record
but does not extend the override.

## Slot 196 `EarPosition` — `0x100b4c00` (2026-09-13)

20 bytes: a call through this object's own vtable `+0x304` (slot 193, `EyePosition`) for the side
effect of filling the out-vector, and the same pointer returned. 82 classes fill slot 196 and every
one of them with this body — **the ear is the eye** everywhere in the family.

## The species sense overrides — `0x103cb810`, `0x103ddaf0`, `0x103ddaa0`, `0x1036a030` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Senses._

Three of these four are **not senses at all, they are the two debug ConVars**. `DAT_10924fba`
(`npc_ignore_senses`) and `DAT_10924fb9` (`npc_ignore_player`) form a shared three-test gate — a null
candidate fails, `npc_ignore_senses` set fails, `npc_ignore_player` set and the candidate carrying a
non-null `m_pPlayer` (`+0x00a8`) fails — and past it:

- `CNPC_VWerewolf::FVisible` (`0x103cb810`, `#201`) returns **true unconditionally**. No range, no
  cone, no trace, no `m_pSenses`. The 700-byte base slot-201 body it replaces is exactly the checks
  it drops. On the vetoed arm it zeroes the blocker out-parameter; on a null candidate it does not.
- `CNPC_VYukie::FInViewCone` (`0x103ddaa0`, `#363`) returns `1`. Yukie has no view cone.
- `CNPC_VYukie::FVisible` (`0x103ddaf0`, `#201`) chains the base check through vtable `+0x948`
  (slot 594, `0x102b4760`) instead of answering true, and zeroes the blocker on **both** veto arms.

`CNPC_VCameraSecurity::vfunc468` (`0x1036a030`, `#468`, `QuerySeeEntity`) is 18 bytes and the whole
body is `return *(int*)(candidate + 0xa8) != 0` — `CBaseEntity::m_pPlayer`, the self-downcast cache
the `CBasePlayer` constructor fills. A security camera's sense admission is "is this the player",
and it **replaces** the base Troika `QuerySeeEntity` (`0x102b38b0`) rather than adding to it: there
is no call in the eighteen bytes. Its own base `CNPC_VCamera` fills slot 468 with nothing of its own.

## `PassesFindEntityFOVTrace`, the two species bodies — `0x101aaf80`, `0x103a4bb0` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Senses._

`CPayphone#45` (`0x101aaf80`, 165 bytes) runs **no cone and no trace**. It sums the per-component
absolute differences of the two entities' `EyePosition()`s — a MANHATTAN distance — compares it with
`_DAT_1047a3b0` = **85.0** Source units, and under that calls `0x10240250` with the two `m_Collision`
objects' `+0x4`/`+0x8` accessors. `0x10240250` is six `FCOMP` pairs, `otherMax >= myMin` and
`otherMin <= myMax` per axis: an **AABB overlap**, not a field-of-view test. The slot's
`Vector, Vector, int` tail is ignored entirely; only the entity argument is read.

`CNPC_ProneDialog#45` (`0x103a4bb0`, 306 bytes) is almost all `Ray_t` construction: start from the
first `Vector` argument, delta to the second, `m_IsRay = (delta.LengthSquared() != _DAT_104454c4)`,
`m_IsSwept = 1`, every other field zero, then `enginetrace->TraceRay(&ray, mask, this, &tr)` through
`(*DAT_1070b254 + 8)`. The answer is true only when `tr.m_pEnt == this`, or when `tr.m_pEnt` is null
**and** `tr.fraction == _DAT_10449280` (**1.0**). "The ray reaches me, or reaches nothing at all."

## The witness records — `0x1028ea60`, `0x1028eb30` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Senses._

Two setters, one per law channel, and they are **not symmetrical**.

`0x1028ea60` writes the criminal record: `+0x6360` and `+0x6361` (below), then the witnessed level
scrambled into `+0x6364`, then the three-float location at `+0x6380`, then the offender's
`GetRefEHandle()` at `+0x638c` or `0xffffffff` for a null entity. `m_iPLCriminalLevelWitnessed`
(`+0x635c`) is a `custom` datamap type — a `CSecureType` whose payload is the scrambled word — and
the pair is recovered in both directions: the write is
`h = 0x1042fde0(level); stored = (((h & 0x068d8635) ^ 0x0ae8746f) + 0x0ffa91d8) & 0x197279ca ^ h ^ 0xa641cacd`,
and `CNPC_VPedestrian::vfunc461` (`0x103a2e30`) reads it back with the same shape but
`^ 0x0ce9f66a`, followed by `0x1042fe90`. The two XOR immediates genuinely differ; both are in their
listings.

`+0x6360` and `+0x6361` are a **retail defect**. `1028ea72` reads `[ESP+0x8]` and `1028eaa9` reads
`[ESP+0x5]`; both are inside the eight bytes `SUB ESP,0x8` allocated at entry and nothing in the
body ever writes them. They are uninitialised stack, and the two bytes the record carries are
whatever the previous frame left there.

`0x1028eb30` writes the supernatural record and its level is **plain**: `+0x6368` takes the argument
directly, the location goes to `+0x6374`, the offender to `+0x6390` (or `-1`), and
`m_bPLSupernaturalActFleeOnly` (`+0x6394`) is written on **both** arms.

## The occlusion edge and the effective look distance — `0x10270180`, `0x1029c970`, `0x1026a2a0` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Senses._

`0x10270180` (205 bytes) is the state machine behind `m_bEnemyWentOccluded` (`+0x5bc5`) and
`m_vecEnemyWentOccluded` (`+0x5bc8`). Three arms on `(enemy, hasLos)`, none of which falls through:

- **null enemy** — copy `DAT_1070d1b0..b8` (`vec3_origin`, read as three dwords) into `+0x5bc8` and
  store the **LOS byte itself** into `+0x5bc5`. Retail writes `param_2` here, not `0`, so a null
  enemy with the byte set leaves the flag raised.
- **`hasLos == 0`** — snapshot the enemy's current `GetAbsOrigin()` (slot 217) into `+0x5bc8` and
  CLEAR the flag: "I have just lost sight; remember where it was".
- **`hasLos != 0` and the flag is clear** — set the flag once the enemy has moved more than
  `_DAT_104563b0` = **4096.0** from that snapshot. The comparison is against a **squared** distance,
  so the edge trips at 64 Source units of drift. A flag already set is never re-tested.

`0x1029c970` (83 bytes) is the effective look distance, and it is not `m_flVisionDistance` on every
pass. The default is `m_flVisionDistance` (`+0x63b8`); two independent overrides replace it with
`m_pSenses->m_LookDist` — `curtime < m_flStealthVisionOverrideTime` (`+0x6604`), or
`m_NPCState == 2` (COMBAT) **with `m_bEnemyWentOccluded` clear** — and the answer is then clamped up
to `_DAT_104454c4` = 0.0. The second arm reads the occlusion **edge** (`+0x5bc5`), not the
ten-failure debounce.

`0x1026a2a0` (16 bytes) is `SetDistLook`: `*(m_pSenses + 0x10) = value`. `CAI_Senses+0x10` is
`m_LookDist` — `vtmb_fields CAI_Senses` declares exactly two fields and this is one of them — so the
inner word `0x1029c970` reads is the same one this writes, and neither is unrecovered.

## The door-blocked notice — `0x1027de00` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Senses._

336 bytes, and it is **not** an enemy setter. Its two callers are `0x10298840` (the alternate-AI door
transaction, when `0x100eec70` refuses the NPC/door pair) and `0x1027dfb0` (the hit-by-door handler,
when the door that struck this NPC is the one it was opening); it writes `m_hBlockedDoor`
(`+0x5d28`); and `npc-kernel/signatures.md`'s slot-532 row already names the reason word it
dispatches. Seven arms, in order:

1. `IsAlive()` (slot 158, vtable `+0x278`) and a non-null door, or return.
2. If the door is the resolved `m_hOpeningDoor` (`+0x5d24`), dispatch slot 532 with **2** — the
   door-blocked reason, beside `1` (fully open, `0x1027dd10`), `4` (`0x1027dfb0`) and `8`
   (`OnScheduleChange`).
3. `door+0x644 & 0x10` skips the whole retry block. The word's retail name is **unrecovered**.
4. `door+0x644 & 0x40` selects `_DAT_10454110` = **5.0 s** over `_DAT_1044eb0c` = **20.0 s**. Under
   the navigator guard `0x102ee6a0` (a network with a node list), `0x102f1fa0` stamps the door's nav
   node unreachable for that long; unconditionally, `0x100f0e30` MAX-writes `curtime + seconds` into
   `door+0x640`.
5. When `m_iSquadDisconnected < 1` and `m_pSquad` (`+0x5da4`) is live, `GetSquadFocus`
   (`0x103166b0`) is compared with the door and `SetSquadFocus` (`0x10316660`) called only when they
   differ — which also stamps `squad+0x74 = curtime + _DAT_10463584` (**15.0**).
6. `m_hBlockedDoor = door->GetRefEHandle()`.
7. `this+0x98` is `m_pBaseNPCTroika`, `CBaseEntity`'s **self**-downcast cache — non-null exactly when
   this entity is a `CAI_BaseNPCTroika`, not a pointer to another NPC. When it is live and
   `m_eAlternateAI` (`+0x644c`) is 1 or 2, the mode advances to **3** and
   `m_flAlternateAIExpireTimer` (`+0x6450`) is armed with `curtime + _DAT_104454c0` (**1.0 s**).

## `CNPC_VWerewolf::ShouldPursueEnemy` and `CNPC_VYukie`'s melee exit — `0x103cf5f0`, `0x103dda10` (2026-09-13)

_Recovered 2026-09-13, story 29c-1, family Senses._

`0x103cf5f0` (292 bytes with the scope trace): unless `+0x66e8 & 4` is set — the werewolf's hint-gate
bit word — it computes `elapsed = curtime - +0x66ec` (the `ScriptUnhide` stamp) clamped up to
`_DAT_104454c4` = 0.0, and then requires **both** of two gates to fail before it gives up:
`DAT_1093f8ec`'s radius against `elapsed`, and `DAT_1093d574`'s against `m_flPlayerDist`
(`+0x6264`). Each global is read the way this band reads every ConVar — `vfunc1() ? 0.0 : +0x28` —
and a single passing gate falls through to `return true`.

`0x103dda10` (`CNPC_VYukie#602`, 103 bytes) replaces the Troika melee-leave decision with two arms on
slot 308 `HasUsableRangedWeapon()` (vtable `+0x4d0`): with no ranged weapon, leave when
`2 * meleeRange * _DAT_1044f02c` (**1.5**) is `<=` `m_flEnemyDist` (`+0x6268`); with one, leave when
`m_flMeleeMustLeaveTimer` (`+0x6074`) has expired. Retail spells the first as
`(a < b) != (a == b)`, the FPU flag pair for `a <= b`. The frenzy gate, the follower-boss gate and
both attack-coordinator terms the Troika body (`0x102b5900`) spends its first half on are simply
absent.

**Unrecovered:** `DAT_1093f8ec` and `DAT_1093d574` (names and defaults; both objects live in
uninitialised `.data`), `CBaseDoor+0x640`/`+0x644`'s retail names, and the hint-type vocabulary
behind `CAI_Hint::IsViewable`'s literal `13` (`0x102d1320`: a set `m_iDisabled` refuses — the
returned `m_iDisabled & 0xffffff00` has a zero low byte whatever the field holds — and otherwise the
answer is `m_nHintType == 0xd`).

## Story 29c-1, family Closure — the eye maintainer and the point view cone — `0x102bff20`, `0x10326a20` (2026-09-13)

Two of family Closure's bodies read senses; the rest of the family is in `shape.md` and
`lifecycle.md`. Ported in `Substrate/ElysiumNpcKernelClosure.cpp`, tested by
`Elysium.Substrate.NpcKernelClosure.MaintainEyeDirection` and `.ViewCone`.

### Slot 333 `CAI_BaseNPCTroika::MaintainEyeDirection` — `0x102bff20`

182 bytes, filled by 63 classes — every shipped `CNPC_V*`. Four arms in this order.

**1. The blink cadence, and its gate is one test around the whole thing.**
`if (m_flPlayerDist < _DAT_10483aac && (m_blinkTimer -= dt) < _DAT_104454c4)` then dispatch
`vtable +0x450` (`CBaseFlex::Blink`) and reseed `m_blinkTimer = RandomFloat(m_flMinBlink,
m_flMaxBlink)`. The decrement is INSIDE the distance test, so a body far from the player neither
blinks nor runs its countdown down — the timer is frozen, not merely unread, which is why walking up
to a distant NPC does not fire a backlog of blinks. `m_flPlayerDist` is `+0x6264`, `m_blinkTimer` is
`+0x6570`, `m_flMinBlink` / `m_flMaxBlink` are `+0x64d8` / `+0x64dc`.

**2. The dialogue re-scan stamp.** When `m_hDialogPartner` (`+0x0fe8`) resolves to a live entity,
`+0x5d6c` (`m_flNextEyeLookTime`) is written `curtime + _DAT_10452dc4` (2.0) — **every think**, not
once, so the autonomous eye scan is suppressed for the whole conversation and the gaze cascade's
fall-through becomes terminal. This is the wrapper's only write.

**3.** `thunk_FUN_102c0010`, the disposition fidget driver, which rewrites `m_flEyeIntegRate`
(`+0x0e3c`) from the disposition table before the base body integrates with it.

**4.** `CAI_BaseNPC::MaintainEyeDirection` (`0x1026b810`), unchanged — the selection cascade itself.

**Unrecovered:** the value of `_DAT_10483aac`, the image's single "the player is close enough to
matter" radius. It has eighteen readers (`SetPlayerLOS` `0x10291610`, `SelectSchedule` `0x102af660`,
`CNPC_Crow::vfunc481` `0x10357680`, `CNPC_VPedestrian::vfunc461` `0x103a2e30` and this one among
them) and no writer, and no read of the image has settled its value;
`ElysiumEyes::BlinkPlayerDistance` carries a stated placeholder of 1024 Source units.

### Slot 362 `CAI_BaseNPC::FInViewCone(const Vector&)` — `0x10326a20`

134 bytes, 71 classes, and 100 of them are the crash-report breadcrumb: the body pushes
`"CBaseCombatCharacter::FInViewCone"` and the entity's `m_iName` onto the scope-trace stack, calls
`thunk_FUN_103268e0(this, point, m_flFieldOfView)`, and pops. `0x103268e0` is a ConVar gate that
selects between `FinViewCone2d` (`0x103261f0`) and `FinViewCone3dNew` (`0x103264d0`); the 3-D branch
is the body `FElysiumNpcSenses::IsInViewCone` already ports. The point overload reads no stealth cone
scalar — a point carries no stealth surface — which is why the port's cone scalar is an argument with
a default of 1.0 rather than a read.

This is NOT slot 363. That one is `CAI_BaseNPCTroika::FInViewCone(CBaseEntity*)` (`0x102b4540`), the
Troika override, which adds a null guard, the `npc_ignore_senses` / `npc_ignore_player` ConVars and
the follower any-angle bypass before it reaches the base body at the target's EYE.

**Unrecovered:** the name and default of the 2-D/3-D ConVar behind `0x103268e0`. The port takes the
3-D branch unconditionally.
