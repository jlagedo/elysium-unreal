# V4 — packet S1: the retail settling read (death and corpse; walk and turn)

Reader S1, 2026-10-04, `spec-0002/step-2`. Read-only on code; no build, no run. Marks:
**(L)** read in the listing or the decompilation this session; **(B)** bytes read from the pinned
`Vampire/dlls/vampire.dll` (image base `0x10000000`; the reading instruction proves the type);
**(D)** taken from `docs/vtmb/` and cited, not re-read; **(data)** a pipeline sidecar; **(I)**
inferred, with what it rests on. No query ran over 10 s.

Doc sections written: `npc-ai/lifecycle.md` § "The ordered chain" (steps 5–6 rewritten, the state-7
writers, a pointer in § "A death sounds more than once"); `combat-and-damage.md` § "Corpse
construction and the solid-body policy" (the tail thinks, the pedestrian override, gib and feed
entries); `npc-ai/shape.md` § "CAI_Motor's unnamed bodies …" (the velocity script, pass by pass);
`npc-ai/schedule-kernel.md` (the Troika `RunTask` 0x2e arm, `AI_ClampYaw`);
`animation_and_movers.md` (the two constants); `npc-ai/rdata-cells.md` (five cells).

## Corrections to what the briefs quote

- **"The state-7 fork is the only caller that passes bone −1"** (brief C2 item 1) is false for the
  image, true for NPCs: `0x1012b370` (the `raggib` entity's setup, from `0x1012b320`) passes
  `(force, −1, 0)` and `0x102b5bb0` (17 bytes, no static caller and in no vtable) passes
  `(vec3_origin, −1, 1)`. Neither is an NPC path. (L)
- **`RunTask` 0x2e on a Troika NPC is `0x102aae61`, not the base `0x102889b5`** (README §1, brief
  B2 item 4, packet R1 item 3). The Troika arm does not reset the yaw clock per call. The bound is
  unchanged (item 5). (L)
- **`AI_ClampYaw` is in `vampire.dll`** (`0x102e1d10`), not the engine (R1 "Unrecovered"). (L)
- **`BecomeClientRagdoll` answering false "only zeroes velocity"** (`combat-and-damage.md`): it
  zeroes the collision BOUNDS (`0x101cf390(this, vec3_origin, vec3_origin)`), nothing else. (L)
- **`SUB_StartFadeOut` arms its think at `curtime + 10.0`**, not `+ 0.0`
  (`combat-and-damage.md` :1898; the cell `0x1044fac0` is a double). (L, B)
- **B1's "the script interpolated at the body's place on the path"** is not retail's read: the
  script is rebuilt every `MoveGroundExecute` from the NPC's current origin and current speed, and
  sampled at TIME `m_flMoveInterval` (item 6). (L)
- **The backward pass of the velocity script does not clamp decelerations** (R1 item 1 "forward /
  backward passes limit by constant acceleration"): both passes test `dv > 0` (item 6). (L)

## 1. `CBaseCombatCharacter::CreateCorpse 0x1032c0e0` — verified (L)

Slot 301. Callers: `CBaseCombatCharacter::Event_Killed 0x1032b9b0` (slot dispatch `+0x4b4`), and
`CNPC_VZombie::CreateCorpse 0x103dfbb0` / `CNPC_VPedestrian::CreateCorpse 0x103a38c0` chaining to it.
Arguments `(Vector* force, CTakeDamageInfo* info)`.

In order:

1. `0x1010e530(−|force|·0.5, +|force|·0.5)` into a local (`_DAT_10449270` = 0.5 double). Its result
   is not used by anything the decompilation shows. *Unrecovered: what `0x1010e530` is.*
2. `0x1032c1a0`: when the NPC self-cast `+0x98` is set, `0x10265a90(npc, resolve(npc+0x1a94
   m_hLastEnemy))` — the one-shot `m_OnDeath` latch (`+0x5bd4`). Inert after `Event_Killed`, which
   already fired it with the attacker.
3. `0x1032c1e4`: bone. `0x101c2a30(info) >= 0` → the model's hitbox-set record at that index (the
   bone word of the `0x20`-byte record); negative → `LookupBone("Bip01 Spine2")`. **Always a real
   bone, never −1.**
4. The body, three arms:
   - **player** (`+0xa8 != 0`, `0x1032c239`): `corpse = SpawnStaticCorpse 0x1032be80` (a
     `prop_base` copy of the pose: `CopyAnimationDataFrom`, `SOLID_NONE`, effects `| 0xb0`); then
     `UTIL_Remove` on the 18 `m_hBodyFireParticles` (`+0xff8`). The player's think is untouched.
   - **`HasMiscFlag(0x80000)`** `No_Ragdoll_Death` (`0x1032c2af`): `corpse = SpawnStaticCorpse`;
     slot 66 `Hide`; **`ThinkSet(this, SUB_Remove 0x101c0b10)`**, `m_flNextThink = curtime + 0.5`
     (`_DAT_104454d0`).
   - **otherwise** (`0x1032c298`): `BecomeClientRagdoll(force, bone, 0)`, answer discarded;
     `corpse = slot 137` (`0x1004fc50`, returns `this`).
5. The tail, only when `corpse != NULL` (`0x1032c2e4`). `burn = 0x10207df0(this)` =
   `template[+0x98] Has_Burning_Death || (IsKindred && !template[+0x9d] Disallow_Kindred_Death)`:
   - **`burn` false, `corpse+0xa8 == 0`** (`0x1032c404`): **`ThinkSet(corpse, 0x10009c9b
     SUB_PVSRemove, 0, NULL)`**, `m_flNextThink = curtime + 10.0` (`_DAT_1044e664`).
   - **`burn` true, `corpse+0xa8 == 0`** (`0x1032c30d..0x1032c3fe`): `corpse->BurnModel(this->
     GetSkeletonModelName(), 1)` (slots 243 / 244); **`ThinkSet(corpse, SUB_Remove)`** (`0x1032c32f`),
     `m_flNextThink = curtime + 10.0`; `EmitSound("character/vampire burning death.wav")` through a
     `CPASAttenuationFilter` at 0.8, volume 1.0, pitch 100.
     `BurnModel 0x10090580`: `m_nRenderFX = 0x1b`, effects `| 0xa0`, `m_flEffectStartTime =
     curtime`; with the flag, one `DMGFX_vampire_death` particle per hitbox bone (unless
     `0x100b5190`); with a model name, a `dynamic_prop` of the skeleton (`models/character/npc/
     common/skel…`, male or female by `IsMale`) following the corpse (`SetAimEnt`, move type
     `0xb`), `SUB_Remove` at `curtime + 0.5 + 3.0`; then `UTIL_Remove(m_hAnimFollowModel)`.
   - then, both: `UTIL_Remove(resolve(m_hAnimFollowModel +0x5a8))`, the handle `= −1`,
     `ForceTransmit(this)`, `ForceTransmit(corpse)`.
   The only skips of the think replacement are `corpse == NULL` (a failed `CreateNoSpawn`) and a
   corpse that is a player (`+0xa8`), which no arm produces.

`BecomeClientRagdoll 0x10090180(force, bone, flag)`: the model interface (`*0x1070b250` slot 18 on
the model) answers no ragdoll → **zero the bounds, return false**: no solid flag, no move type, no
think change. Otherwise: `SelectWeightedSequence(ACT_DIERAGDOLL 0x21)` is always asked; **only when
`bone == −1` and the pick is not −1** (`0x1009021a`): `m_nSequence`, `m_flCycle = 0`,
`ResetSequenceInfo`; slot 225 (`+0x384`); with `flag == 0`: `AddSolidFlags(4)`;
`TriggerClientRagdoll(−1, −1, −1, force, bone)` (latches `m_vecForce` / `m_nForceBone` only for
`|F|² > 0` and `bone > 0`); `m_nRenderFX = 0x17`; with `flag == 0`: `SetMoveType(0)`, zero bounds,
`ThinkSet(NULL)`. Return true.

`SUB_PVSRemove 0x102696f0` (thunk `0x10009c9b`): for each player `1..maxClients`: player slot 363
(`+0x5ac`, the view cone, on the corpse) **and** `0x101d1a90(corpse, player)` (the PVS test) **and**
player slot 201 (`+0x324`, `FVisible(corpse, mask 0x2804091, 0, 0)`) → `m_flNextThink = curtime +
10.0`, return. No player passes → `UTIL_Remove(corpse)`.

Two things run AFTER `CreateCorpse` and replace its think again:

- `CAI_BaseNPC::Event_Killed` step 14: slot 552 `ShouldFadeOnDeath` (spawnflag bit 9) true →
  `SUB_StartFadeOut 0x102695d0`: render mode 2 / alpha 255 when the mode was 0, `AddSolidFlags(4)`,
  zero angular velocity, relink, `m_flNextThink = curtime + 10.0` (`1026968d FADD double ptr
  [0x1044fac0]` — **10.0**, not the `0.0` `combat-and-damage.md` had), `ThinkSet(SUB_FadeOut
  0x100152b2)`.
- `CNPC_VPedestrian::CreateCorpse 0x103a38c0`: snapshot the collision mins / maxs (`+0x6660`,
  `+0x666c`), the base, then **`ThinkSet(this, NULL)`** and `SetSolid(SOLID_NONE)`. **A pedestrian's
  corpse has no think at all: this chain never removes it.**

**Confirmed: an ordinary kill never returns to `NPCThink`.** Every NPC arm ends with the NPC's main
think function replaced (`SUB_PVSRemove`, `SUB_Remove`, `SUB_FadeOut` or none), rig or no rig — the
replacement is `CreateCorpse`'s, not `BecomeClientRagdoll`'s. `Event_Killed` still writes
`m_IdealNPCState = 7` and `SetState(7)` afterwards, but no `SelectSchedule` follows. (I): when the
kill lands inside the NPC's own think, that think's tail still runs once and may move
`m_flNextThink` — the next call is the replaced function either way. A rig-less ordinary corpse
keeps animating its last sequence on the client (nothing writes `m_flPlaybackRate`) (I).

## 2. The two `Event_Killed` bodies — verified (L); the arm-by-arm walk in `lifecycle.md` (:2797, :2846) stands

Order of everything observable, one ordinary kill:

1. Troika `0x102bf340`: `info+0x49 == 0` → the crime record `0x102ca2a0(type 1, this, origin +
   16 z, level 3, radius 100000)`.
2. Base `0x10265ad0`: return at once when the running schedule is `NPC_FREEZE 0x3a`; in state 4
   with a live `m_hCine` whose `m_sequenceStarted` is set and `spawnflags & 0x2080 != 0x80` → stash
   the packet and **return** (no sound, no `OnDeath`, no corpse; the Troika tail below still runs);
   else `CancelScript`.
3. slot 511 `StopLoopingSounds`; **the death sound** (slot 488) unless a live grapple partner.
4. **`OnDeath`** (`0x10265a90`, activator the attacker, one-shot).
5. `FL_NPC`: touch cleared, `BecomeDead` (`m_iHealth = max/2`, `m_iMaxHealth = 5`, `m_takedamage =
   2`, `MOVETYPE_TOSS`).
6. `CBaseCombatCharacter::Event_Killed 0x1032b9b0`: **`m_lifeState = 1`**; slot 385 with the active
   weapon; grapple role 1 → the partner's slot 353 `FeedInterrupt`; `RemoveDisciplineVisuals`,
   `RemoveFromPresenceList`, `RemoveFromComfortList`; the engine sound stop; the force (the packet's,
   or `CalcDamageForceVector` when its length is `<= 0`; plus velocity; clamp 50000); **slot 301
   `CreateCorpse`**; `m_iCurFrenzyCount = 0`; the attacker's slot 300.
7. `m_IdealNPCState = 7`, `SetCondition(0x4c)`, `m_hLastDamageEnt`, `m_bCondTookDamage = 1`, squad
   vacate, fade or `SOUND_CARCASS`, `m_IdealNPCState = 7`, **`SetState(7)`**.
8. Troika tail: `ClearHintNode(5.0)`, slot 601, leave the interesting place, stop a dialogue,
   **`MarkAsDead("<name>")`** when named, the closest player's cache.

So: crime record → death sound → `OnDeath` → `m_lifeState 1` → corpse → state 7 → `MarkAsDead`.
The death sound plays **once** on an ordinary kill.

## 3. Who writes state 7, and who reaches the fork `0x1028a8ec` — verified (L) unless marked

`m_NPCState +0x5cc0` has three writers: `SetState 0x1026e340`, `NPCInit 0x1029a0b0`,
`CGenericNPC::Spawn 0x1034a6d0` (D, `order.md`). Sites that write 7 (grep of the whole image for
the ideal-state write and `SetState(7)`):

| site | condition | think afterwards | reaches the fork |
|---|---|---|---|
| `Event_Killed 0x10265ad0` | every completed kill | replaced by `CreateCorpse` | **no** |
| `CineCleanup 0x1027d170`, `m_lifeState == 1` arm | a script releases an actor already dying | `SetState(7)`, `m_lifeState = 2`, health 0, not solid, hull to `mins.z + 2`; `SUB_StartFadeOut`, or with cine spawnflag 8 think / touch / use cleared | no |
| `CineCleanup`, `m_iHealth < 1` (`:0x2b22`) | a script releases an actor whose kill was **deferred** (item 2 step 2) | NPC think kept; ideal 7 + `COND 0x4c` | **yes**, next `MaintainSchedule` |
| `0x1027d0a0`, from `SelectIdealState` case 4 (`0x1026f660`, Troika `0x102ad660`) | state SCRIPT, interrupt `0x5c` / `0x4c` / `0x4d`, `m_lifeState == 1` → ideal 7; else `CancelScript` | kept | yes |
| `SelectIdealState 0x1026f660` case 7 | already 7 | — | sustains |
| `CNPC_VWerewolf::SelectIdealState 0x103d0820` | `!IsAlive` or `COND 0x7a` → ideal 7 + `SetState` | kept | yes (Werewolf only) |
| `CNPC_VMingXiao 0x103945a0` | dead code: overwritten in the same call (D) | — | no |
| `CNPC_VMingXiaoTentacle 0x1039e310` | state or ideal already 7 | kept | sustains |
| `CNPC_VZombie::CreateCorpse 0x103dfbb0`, non-ragdoll arm | zombie killed with `m_bShouldRagdoll` clear: schedule `0x162` forced, `ThinkSet(NPC think)`; `Event_Killed` then sets state 7 | kept | yes, when `0x162` ends (I) |

The fork (`0x1028a8ec`): `BecomeClientRagdoll(vec3_origin, −1, 0)` true → `SCHED_DIE_RAGDOLL 0x2c`
(line `0xf04`), false → `DIE 0x2b` (line `0xf06`). No other code in the image names `0x2b` / `0x2c`
as an immediate. With a rig: the seed (`ACT_DIERAGDOLL`), not solid, **the think cleared by
`BecomeClientRagdoll` itself** — the returned program runs only in the rest of that think; no
`OnDeath`, no `SUB_PVSRemove`. Without a rig: `DIE` → `TASK_DIE` → `Die 0x103392c0` → a full
`Event_Killed` (the sound a second and third time: `TASK_SOUND_DIE`, then `Event_Killed`).

**On the two witness maps (`sm_hub_1`, `sp_tutorial_1`; `npc-kernel/reach/*.tsv` class rows):**
no Werewolf, Ming Xiao, tentacle or zombie. `CCineNPC` is on both (30 / 51), so the one reacher is
**a kill deferred under a started scripted sequence** (and its `0x1027d0a0` sibling). **In the
step-2 arena records: none.** `damage_lethal_death` kills an idle NPC; `verbs_stealth_kill_scripted`
does not defer, because `CAI_BaseNPCTroika::EnterGrappleState 0x102b5c00` cancels a live `m_hCine`
(`CancelScript 0x101a8c30`, then `SetState(ideal)`) before the kill. **Step 2 needs neither the
seed nor `SCHED_DIE` for a green record; both are already ported and stay as arms reached by arm
tests** (the fork `ElysiumNpcBaseSelect.cpp:273`; the seed is C2 item 5).

Also on both maps, and outside the ordinary arm: **`CNPC_VPedestrian`** (28 / 3; the corpse has no
think, item 1) and **`CNPC_VVampire`** (3 / 16; the burning arm when the template says Kindred).

## 4. Feed deaths and explosion deaths — verified (L)

**Feed.** Two sites, both `CBaseCombatCharacter::Die 0x103392c0` (thunk `0x100034f9`):
`FeedInterrupt 0x1033a9e0` (slot 353, on the feeder) — when the victim's stat `0xc` (blood) is
`< 1`: not Kindred → `Die(victim, feeder, f, f)`, Kindred → `TorporBegin 0x103394a0`; `f` is 1 only
when `0x1023bd00()+0x49c == 2` and the feeder's own grapple slot `+0x1540 == 2`; then the grapple
sounds, the victim's slot 355 (`+0x58c`), the handle cleared. And `DecBloodPool 0x10338df0(true)` —
blood at 0 and `IsKine` → `Die(this, m_hClosestPlayer, 0, 0)`, else `TorporBegin`. `Die`: guard
`m_lifeState != 2`; a synthetic packet (attacker = the argument, damage 1.0, the two flag bytes
through `0x101c2a90` / `0x101c2ad0`); stat `0xf := stat 0x11` (the damage counter to its maximum);
**slot 144 `Event_Killed`**, then slot 403 `Event_Dying` (empty, `0x1032bdf0`). So a feed death is
the ordinary chain: no hit bone → `Bip01 Spine2`, the ragdoll arm, `SUB_PVSRemove`. The death sound
is silenced while the grapple partner is live (item 2 step 3). *Not read:* which packet byte each
flag setter writes (`info+0x49` suppresses the crime record; I).

**Explosion.** `CBaseCombatCharacter::OnTakeDamage 0x1032ef60`, alive arm: after slot 390, when
stat `0xf >= stat 0x11`: **slot 144 `Event_Killed` first, always**; then slot 399 (`0x1014fa30`,
`return false` on every NPC class) — false and `(bits & 0x2000) && !(bits & 0x1000)` → slot 402,
else slot 403 `Event_Dying`; a slot 402 answering false also falls to slot 403. Slot 402
`0x102658f0`: slot 394 `CorpseGib 0x10327a90` false → slot 395 `CorpseFade 0x10326040` (playback
rate 0, move type none, `SUB_StartFadeOut`), false; true and slot 398 false → `UTIL_Remove(this)`;
true and true → blood particles, slot 77, `CorpseFade`. An explosion's packet is `DMG_BLAST 0x40`
(D, `effects.md`), not `0x2000`, so **an explosion death is the ordinary chain** with
`CalcDamageForceVector`'s blast direction (inflictor → victim × 1.5; D). *Unrecovered:* whether any
shipped damage source sets bit `0x2000`.

## 5. `TASK_FACE_ENEMY` 0x2e — verified (L)

**`StartTask`, Troika arm `0x102a4417`:** point = `m_hShootTargetOverride (+0x5ba8)`'s
`GetAbsOrigin` when it resolves, else the enemy's LKP (`GetEnemies()->0x102dfed0(enemy)`). Slot 364
(`+0x5b0`) `FInAimCone(point)` true → complete (`0x102a44cb` → `0x102a66d7`). False → **the turn
tail `0x102a44d1`**: `0x102e0b40(motor)` (`m_flLastYawTime motor+0x2c = −1.0`);
`0x102e2020(motor, &point, 0)` — ideal yaw `motor+0x34 = ` the yaw to the point (`0x102e2750`, owner
slot 515), `± 180` when `motor+0x28`, clamped to `motor+0x18 ± motor+0x1c` (`0x102e0a80`) unless
`motor+0x1c == 180.0`; **no update**; slot 572 (`+0x8f0`) `SetTurnActivity`; return, RUNNING. That
is the whole tail.

**`RunTask`, Troika arm `0x102aae61`** (index 4 of table `0x102ac760`; not the base `0x102889b5`):
`m_afMemory (+0x5d8c) & 0x2000` clear → slot 572 `SetTurnActivity`, every call; the same point;
`0x102e20b0(motor, &point, −1.0)` = `SetIdealYawAndUpdate(yaw, −1)`: the ideal yaw as above, then
because the speed is `−1.0` (`_DAT_104492dc`) `0x102e1cf0` re-reads `MaxYawSpeed` into `motor+0x38`,
then `UpdateYaw(−1)`; `FacingIdeal 0x10278c80` (`|DeltaIdealYaw| <= 0.006`, double `0x10499568`) →
`TaskComplete`, else running. **It does not call `0x102e0b40`.**

**`UpdateYaw 0x102e1e20(speed)`:** `−1` → `(int) motor+0x38`. `current` = the owner's local yaw and
`ideal = motor+0x34`, each quantised (`× 182.0444` `0x1044ffe0`, `& 0xffff`, `× 0.0054931640625`).
`m_flLastYawTime < 0` → `= curtime − 0.1` (double `0x104493d0`). `new = AI_ClampYaw(speed × 10.0`
(double `0x1044fac0`)`, current, ideal, curtime − m_flLastYawTime)`; `m_flLastYawTime = curtime`;
`new != current` → `SetLocalAngles`.
**`AI_ClampYaw 0x102e1d10(rate, current, target, dt)`:** equal → target. `step = rate × dt`;
`move = target − current`; `target > current`: `move >= 180` → `− 360`; else `move <= −180` →
`+ 360`; `move > 0`: `min(move, step)`; else `max(move, −step)`; return `current + move` quantised.

**The bound stands.** The first `RunTask` after `StartTask` integrates 0.1 s (the tail's reset);
later calls integrate the real time between thinks; at `MaxYawSpeed` 90 that is 900°/s — 135° in two
calls. 0.5 s for `face_enemy_turn` is right.

**The three constants (B):**

| cell | type, proving instruction | value | what |
|---|---|---:|---|
| `0x104493c0` | float64, `102630f1 FADD double ptr` | **50.0** | acceleration = ideal speed + 50 |
| `0x10449198` | float64, `1026329e FADD double ptr` | **0.2** | the corner dot's bias |
| `0x10457f60` | float32 (`0x102627e0`; `programs.md:1211` reads the same cell) | **150.0** | turn script: degrees per second of segment time |

Also read: `0x104491a8` float64 **0.8** and `0x104493d0` **0.1** (turn script, below), `0x10450aa0`
float32 **4.0** (`SolveQuadratic`'s discriminant), `0x1044f020` float64 **0.001** (R2's
`StudioFrameAdvance` gate, `dt > 0.001`). For `0x10457f60`, `0x104491a8`, `0x10450aa0` and
`0x1044f020` the width is taken from which reading is a sane number (the other is a denormal or
−5e11), not from a re-read instruction: (I) on the type, (B) on the bytes.

## 6. The arrival deceleration: the velocity script `0x102630b0` — verified (L)

**Who calls it.** `CAI_HumanoidMotor` slot 19 `0x10264680` (`MoveGroundExecute`) →
`0x10262590` (`m_scriptMove` count `+0x6030 = 0`, turn script count `+0x6044 = 0`, then
`0x102630b0`, then `0x102627e0`). So **the script is rebuilt on every ground-move execute, from
where the NPC is and how fast it is going now.** Units are Source units and units per second.

**The entry** (`0x38` bytes, 14 floats): `+0x00` `flTime` (the segment's duration), `+0x04`
`flElapsed` (its start time), `+0x08` `flDist` (to the next entry), `+0x0c` `flMaxVelocity`,
`+0x10` `flYaw`, `+0x20` the waypoint pointer, `+0x2c` `vecLocation`.

**Inputs.** `ideal` = owner slot 248 `GetIdealSpeed` (`+0x654`), **50.0 when it answers 0**.
`accel = ideal + 50.0`. Entry 0: location slot 217 `GetAbsOrigin`, yaw `GetLocalAngles().y`, speed
`|m_vecVelocity|` (motor `+0x3c`, 3-D length). Then one entry per waypoint from the path's current
waypoint (`nav+0x30 → +0x24`, next at `waypoint+0x30`).

**Each waypoint's speed.** No next waypoint → **0.0** (the last one; `GetArrivalSpeed` is not
asked). Else `d1 = next − this`, `d2 = this − (the previous entry's location)`, both with z zeroed
and normalised; `s = dot(d1, d2) + 0.2`; `s <= 0` → 0; `s > 1` → 1; speed `= s × ideal`.

**Pass 1, distances and prune** (`0x1026330e`): `flDist[i] = |loc[i+1] − loc[i]|` (3-D). `flDist[i]
< 0.01` (double `0x1044e658`) and `i != 0` → remove entry `i`, retry; else next.

**Pass 2, forward** (`0x102633b8`), `i = 0 .. n−2`: `dv = v[i+1] − v[i]`; `dv > 0` (double
`0x1044fab0`): `t = dv / accel`; when `v[i]·t + 0.5·accel·t² > flDist[i]` and `SolveQuadratic(
0.5·accel, v[i], −flDist[i])` (`0x1013a6f0`, `r1 = (sqrt(b² − 4ac) − b) / 2a`): `v[i+1] = v[i] +
r1·accel`.

**Pass 3, backward** (`0x10263480`), `i = n−1 .. 1`: `dv = v[i] − v[i−1]`; **`dv > 0`** (the same
sign test as pass 2, not the SDK's `dv < 0`): `t = dv / accel`; when `v[i]·t + 0.5·accel·t² >
flDist[i]` (**entry `i`'s** distance) and `SolveQuadratic(0.5·accel, v[i], −flDist[i])`: `v[i−1] =
v[i] − r1·accel`. As written it never fires on a slowing pair, so a deceleration that does not fit
its segment is not corrected here. Retail's; reproduce it.

**Pass 4, cruise points** (`0x10263554`), `i = 0`, while `i < n−1`:
`t1 = (ideal − v[i]) / accel`, `d1 = v[i]·t1 + 0.5·accel·t1²`;
`t2 = (ideal − v[i+1]) / accel`, `d2 = v[i+1]·t2 + 0.5·accel·t2²`.
- `d1 + d2 < flDist[i]`: insert at `i+1` the point `lerp(loc[i], loc[i+1], d1 / flDist[i])` with
  speed `ideal`; insert at `i+2` the point `lerp(loc[i], loc[old i+1], (flDist[i] − d2) /
  flDist[i])` with speed `ideal`; `i += 3`. **No guards** (the SDK's `d > 1.0 && t > 0.1` are
  absent); a zero-length piece is removed by pass 5.
- else, when `|DeltaV(v[i], v[i+1], flDist[i])| < accel` (`0x102e1470` = `(v2² − v1²)·0.5 / d`):
  `r = (accel + v[i]) / (accel + v[i+1])`; `SolveQuadratic((0.5·r² + 0.5)·accel, r·v[i+1] + v[i],
  −flDist[i])` → `t`; `dA = v[i]·t + 0.5·accel·t²`; `dB = t·r·v[i+1] + 0.5·accel·(t·r)²`;
  `peak = v[i] + t·accel`; **`peak < ideal`** → insert at `i+1` the point `lerp(loc[i], loc[i+1],
  dA / (dA + dB))` with speed `peak`, `i += 1`. Then `i += 1`.
- else `i += 1`.

**Pass 5, times** (`0x10263991`): `flElapsed[0] = 0`. For `i = 0 .. n−2`: `flDist[i]` recomputed
(3-D); `v[i] > 0 || v[i+1] > 0` → `flTime[i] = flDist[i] / (0.5·(v[i] + v[i+1]))`, else `1.0`;
`flDist[i] < 0.01 || flTime[i] < 0.01` → remove entry `i+1`, retry; else `flElapsed[i+1] =
flElapsed[i] + flTime[i]`.

**The profile**, for one straight leg from a standing start: accelerate at `ideal + 50` to `ideal`,
cruise, decelerate at `ideal + 50` to 0 at the goal — pass 4's two inserted points. The braking
distance is `d2 = ideal² / (2·(ideal + 50))`: for the female `walk_0` (101.278 cm/s = 53.16 u/s),
13.7 units = 26.1 cm over 0.515 s.

**How `MoveGroundExecute 0x10264680` reads it** (`0x102646c0`): `speed = |m_vecVelocity|`; with more
than one entry, the first `i >= 1` whose `flElapsed[i] > m_flMoveInterval (motor+0x30)`: `a =
m_flMoveInterval / flElapsed[i]` (**the divisor is `flElapsed[i]`, not the segment's own time**),
`speed = (1 − a)·v[i−1] + a·v[i]`; no such entry (the interval outruns the script) → the current
speed stands. The yaw the same way off the turn script. Then slot 18 on the copied move, the
`+0x654` refresh, and `dist = (|m_vecVelocity| + speed) × m_flMoveInterval × 0.5`.

**How the last step lands on the goal** (`0x10264916`): `dist <= move.maxDist (+0x28)` →
`m_flMoveInterval = 0`; else `move.flags (+0x38) & 2` → interval 0, else interval `×= 1 − maxDist /
dist`; and `dist = maxDist`. `m_vecVelocity = move.dir × speed`. `dist > 0` → `0x102e0bd0(origin +
dir × dist, move+0x34, −1.0, 1, 1, …)`, a zero answer dispatching motor slot 10; else the local
navigator's slot 6. `move.maxDist` is the navigator's distance to the waypoint (D,
`navigation-jump-links.md` :1646), so **the body is placed exactly on the waypoint and the unused
interval re-enters the navigator's loop.**

**The arrival tolerance** (D, `navigation-jump-links.md` § "The arrival test and waypoint advance",
navigator slot 16 `0x102ef510`): reached iff `dist <= 0.0625` units (double `0x10451f78`; `0.25`
under `npc_vphysics`, shipped `"0"`), 2-D on a ground move, measured from slot 220 `GetOrigin` to
the head waypoint. It is a constant, not the goal tolerance and not a hull. The goal tolerance
(`path+0x28`: −2 → the hull's 26, −1 → that only when still 0) shortens the PATH, not this test.

**The turn script `0x102627e0`** (for B1 item 5): entry 0 the current yaw; per velocity-script entry
that carries a waypoint: `out` = yaw of (next − this) or, on the last, the navigator's arrival
direction `0x102ee5b0`; `in` = yaw of (this − previous turn entry's location);
`|AngleDiff(out, in)| <= 0.1` → no entry; else yaw `= 0x1013d450(out, in, |diff| × 0.8)` (*the
approach helper's argument order is not read*). Backward, `i = n−1 .. 2`: `limit = flTime[i−1] ×
150.0`; `|AngleDiff(yaw[i−1], yaw[i])| > limit` → `yaw[i−1] = 0x1013d450(yaw[i−1], yaw[i], limit)`.
Then `0x10262c20(i, i+1)` over the entries. *Unrecovered: `0x10262c20`, `0x1013d450`.*

## 7. The female `walk_0`'s 1.06446 s — settled (data): it is the bank's fps

`$ELYSIUM_WORK_ROOT/import/characters/character/shared/female/move_and_ranged.clips.json`, clip
`walk_0`: **37 frames at 33.81999969482422 fps** (float32 33.82) → `36 / 33.82 = 1.064459 s`;
`groundDistanceCm 107.8067` → **101.278 cm/s**. Every other walk cell of that bank, and the male
`walk_0` (37 frames, **30.0** fps, 1.2 s, 164.019 cm, 136.683 cm/s), is at 30.0. The same 37 frames:
the female clip is authored faster, not shorter. No 32-frame reading; H18's 101.278 is right. *Not
done:* the `.mdl`'s own `animdesc.fps` bytes were not re-read; the sidecar value is the decoder's.

## Changes to the plan

- **A0 (seam and records)** — changes:
  1. `damage_lethal_death`: `about`'s last sentence ("the only programs base `SelectSchedule` can
     answer in state 7 are `0x2c` / `0x2b`") is wrong for this record: the think is replaced, so
     **no schedule, task or state line follows the death**. The `never` regex today admits
     `DIE (` and `SCHED_DIE_RAGDOLL (`: drop those two alternatives, or add a `never` of any
     `schedule` after `dies`.
  2. `face_enemy_turn`: the 0.5 s bound stands; cite `RunTask` `0x102aae61` (Troika), not
     `0x102889b5`.
  3. The N13 acceptance (101.278 cm/s for `vampire_hunter_chick`) stands; `about` may state the
     cause: 37 frames at 33.82 fps.
- **B1 (body speed)** — changes:
  1. It no longer waits on R1b for its inputs: the constants are **50.0**, **0.2**, **150.0** (and
     0.8 / 0.1 for the turn script); the arrival tolerance is **0.0625 units, 2-D**.
  2. Item 2's "a forward pass and a backward pass" becomes the five passes of item 6 here, as
     written, including pass 3's `dv > 0` and pass 4's unguarded inserts.
  3. Item 2's "interpolated at the body's place on the path" becomes: rebuild from the body's
     current position and speed each move tick and sample at **time = the tick's interval**, `a =
     interval / flElapsed[i]`; interval past the script's end → the current speed.
  4. Item 4: retail's tolerance is 0.0625 units (0.119 cm at 1.905 cm per unit), and retail lands
     on the waypoint by clamping the step to the remaining distance. `FollowerArrivalFloorCm` 1.0
     is not retail's: keep it only as a named divergence of the crowd follower (K1), stated at the
     line, or replace it — the judge's call, not the coder's.
  5. Item 6's corner test: `ideal × 0.2` at a 90° corner.
- **B2 (move yaw, facing)** — changes: item 4's citation and mechanism: the Troika arm
  `0x102aae61`; `SetTurnActivity` every call unless `m_afMemory & 0x2000`; the point is the shoot
  target override else the enemy's LKP; `MaxYawSpeed` re-read every call; the yaw clock is reset
  only by `StartTask`'s turn tail, so `UpdateYaw` integrates real elapsed time (0.1 s on the first
  call); `AI_ClampYaw` is `0x102e1d10` as walked above. Items 1–3 stand.
- **C2 (pick, disposition, corpse)** — changes:
  1. Item 1's walk is done and confirmed; `lifecycle.md` § "The ordered chain" steps 5–6 are
     rewritten by S1 and the state-7 writers are listed there. C2 keeps only § "A death sounds more
     than once": **once** on an ordinary kill; three times only on the rig-less fork route.
  2. Item 1's "the only caller that passes bone −1": say "the only NPC caller" (`0x1012b370`,
     `0x102b5bb0`).
  3. Item 5 stands. The test's text names retail's reachers: the deferred script death
     (`CineCleanup` `:0x2b22`), `0x1027d0a0`, the Werewolf, the zombie's collapse.
  4. **New, in nobody's files:** `CNPC_VPedestrian::CreateCorpse 0x103a38c0` (think cleared,
     `SOLID_NONE`) is recovered but **not wired to slot 301** (`ElysiumNpcPedestrian.h:47`). Both
     witness maps carry pedestrians: a pedestrian's corpse today takes `SUB_PVSRemove` and retail's
     takes none. One owner must wire it (`ElysiumCombatCharacter.cpp` `CreateCorpse` is the chain's
     home); it is not in C2's file list.
- **D (ragdoll)** — changes:
  1. The contract table's "its removal (`SUB_PVSRemove`, re-checked every 10 s while a player sees
     it)" holds for an ordinary mortal only. Add: **Kindred or `Has_Burning_Death`** → `BurnModel`
     (render fx `0x1b`, the skeleton `dynamic_prop` following the corpse) and `SUB_Remove` at
     +10 s unconditionally — the ragdoll of a `CNPC_VVampire` (on both witness maps) burns and
     goes; **pedestrian** → no removal; **spawnflag bit 9** → `SUB_StartFadeOut`. The integrator's
     trace check expects these per body.
  2. `SUB_PVSRemove`'s keep test is three-fold: view cone, PVS, `FVisible`.
  3. "After the spike" item 4 is confirmed as written. A rig-less model: bounds zeroed, **not**
     made non-solid, think replaced.
  4. "feed and explosion deaths (unread)" in § "Not in V4d": read — both are the ordinary chain
     (item 4); nothing to add to V4d.
- **A1, A2, A3, A4, C1:** none.

## Unrecovered after S1

`0x1010e530` (CreateCorpse's first call); `SUB_FadeOut 0x100152b2`'s body; the flag byte each of
`0x101c2a90` / `0x101c2ad0` writes; whether any shipped damage carries bit `0x2000`; `0x10262c20`
and `0x1013d450` (turn script); `0x102e0bd0`; what `0x102b5bb0` is reached from; the `.mdl` fps
bytes of the female `walk_0` (the sidecar's decoded value is what was read).
