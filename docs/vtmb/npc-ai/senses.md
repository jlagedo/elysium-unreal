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
