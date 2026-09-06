# Footsteps — the NPC step, the player step clock, the landing, the hearing stimulus

Three unrelated producers make a footfall in VtMB, and only one of them is an animation event.

1. **NPCs** step from animation events `2050`–`2053`, through one server function that reads the
   NPC's own cached `surfacedata_t` and an authored volume/distance pair.
2. **The player** steps from a **millisecond clock on the player entity** driven by the movement
   code every move — animation events `2050`–`2053` are *swallowed* by `CBasePlayer`.
3. **The AI hearing stimulus** for player movement is neither: it is a **per-think refresh of the
   player's one permanently reserved `CSound` slot**, whose radius is looked up from
   `sound_volume_table.txt` by locomotion category. No footstep ever *inserts* a sound.

Addresses are `vampire.dll` (image base `0x10000000`) unless the line says `engine.dll`
(base `0x20000000`). Float and double constants were read out of the shipped images at the
addresses given.

Companion documents: [animation_events.md](animation_events.md) (the `2050`–`2053` records and the
dispatchers), [source_movement.md](source_movement.md) (the move loop the clock rides on),
[surface_properties.md](surface_properties.md) (`surfacedata_t` and the step pools),
[stealth.md](stealth.md) and [npc-ai-reverse-engineering.md](npc-ai-reverse-engineering.md)
(who listens to the sound bus), [water.md](water.md) (the water-level classification).

---

## 1. NPC footsteps

### 1.1 Dispatch

`CAI_BaseNPC::HandleAnimEvent` (`0x10274e30`) maps the four codes onto one function with a mode:

| event | meaning | call |
|---:|---|---|
| `2050` (`0x802`) | walk footfall, left | `0x1026d460(this, 0)` — "normal" |
| `2051` (`0x803`) | walk footfall, right | `0x1026d460(this, 0)` — "normal" |
| `2052` (`0x804`) | run footfall, left | `0x1026d460(this, 1)` — "heavy" |
| `2053` (`0x805`) | run footfall, right | `0x1026d460(this, 1)` — "heavy" |

**The left/right in the event name is discarded.** The mode is the only thing carried, and the foot
is re-chosen by a coin flip inside (§1.6). The walk/run split is the *volume* split, not the foot
split.

### 1.2 `0x1026d460` — the chain, in order (`__thiscall`, `RET 0x4`)

| # | step | asm |
|---:|---|---|
| 1 | `pPlayer = UTIL_GetLocalPlayer()`; if non-null, three predicates each return silently | `1026d467`…`1026d499` |
| 2 | resolve the NPC's stat template | `1026d4a0` `FUN_10001de8(0x1074f028, this)` |
| 3 | pick `volume` + `dist` from the template or from cvars | `1026d4aa`…`1026d58a` |
| 4 | `if (!g_physprops) return;` | `1026d58a`, global `0x10723944` |
| 5 | `if (!this->m_pSurfaceData /* +0x5b90 */) return;` | `1026d597` |
| 6 | `soundlevel = SoundLevelFromDistance(dist)` | `1026d5ab` → `0x10004db3` → `0x10228350` |
| 7 | build a `CPASAttenuationFilter(GetAbsOrigin(), attenuation)` | `1026d5b3`…`1026d621` |
| 8 | `attenuation = soundlevel > 50 ? 20/(soundlevel−50) : 4.0` — **integer division** | `1026d5e1`…`1026d609` |
| 9 | coin flip → `stepleft` (`+0x2c`) or `stepright` (`+0x2e`) | `1026d626`…`1026d653` |
| 10 | `name = physprops->GetString(sample)`; silent if null or empty | `1026d653`…`1026d666` |
| 11 | emit | `1026d668`…`1026d6a6` |

**The player gate (step 1).** Any one of these three, evaluated on the *local player*, aborts the
step before anything else happens:

| thunk | target | reads | meaning |
|---|---|---|---|
| `0x10014371` | `0x1017d630` | player `+0x19c0` | the scripted **camera view entity** is set |
| `0x10014ad3` | — | player `+0x19cc` | the **camera target** is set |
| `0x1000306c` | — | player `+0x19b4` with `+0x1ec4 > 0` | a **dialogue / control entity** owns the player |

So: while a cutscene camera or a conversation is up, **no NPC in the level makes a footfall**. The
gate is global, not per-NPC, and it is checked before the template lookup.

### 1.3 Where the volume and the distance come from

`footstep_npc_use_templates` (`0x109203f8`, default `1`) selects the source:

| mode | `use_templates != 0` — the NPC's `npctemplate*.txt` record | `use_templates == 0` — cvars |
|---|---|---|
| 0 "normal" | `template+0x24` `NormalFootfallVol`, `template+0x28` `NormalFootfallDist` | `footstep_normal_vol` (`0x10920440`, **0.5**), `footstep_normal_dist` (`0x10920280`, **256**) |
| 1 "heavy" | `template+0x2c` `HeavyFootfallVol`, `template+0x30` `HeavyFootfallDist` | `footstep_heavy_vol` (`0x10920160`, **0.85**), `footstep_heavy_dist` (`0x109204a8`, **512**) |

Template loader `0x101d3f10`, keys `NormalFootfallVol` / `NormalFootfallDist` /
`HeavyFootfallVol` / `HeavyFootfallDist` (strings at `0x105a0848` / `0x105a081c` and neighbours),
loader defaults `0.45 / 256 / 0.85 / 512`, resolved through the template parent chain. The shipped
records author `0.45 / 300 / 0.85 / 600` where they author them at all.

**The volume is passed to the emit unmodified.** There is no duck scale, no distance scale, no
material scale on the NPC path — verified at `1026d66e` (`MOV EDX,[ESP+0xc]`, the same slot written
by the template/cvar selection at `1026d4d6` / `1026d51c` / `1026d561`) and `1026d68d` (`PUSH EDX`).

### 1.4 Distance → soundlevel → attenuation

`0x10228350` (reached through thunk `0x10004db3`):

```c
int SoundLevelFromDistance(float dist)
{
    if (dist <= 0.0f) return 0;                       // 0x104454c4 = 0.0
    return (int)( 20.0 * log10(dist * (1.0/36.0)) + 40.0 );
}                     // 0x104704a8 = 20.0   0x1047aa18 = 0.0277777778   0x10462950 = 40.0
```

and then, inline at `1026d5e1`:

```c
float attenuation = (soundlevel > 50) ? (float)(20 / (soundlevel - 50))   // INTEGER divide
                                      : 4.0f;                            // 0x10449148 = 4.0
```

That second expression is Source's `SNDLVL_TO_ATTN`, except that VtMB does the division in
**integers**, so the attenuation is a staircase: level 51 → 20, 52 → 10, 53 → 6, 54 → 5, 55 → 5,
56…58 → 4, 59…60 → 3, 61…65 → 2, 66…70 → 1, ≥71 → 0.

Worked values for the shipped authored distances:

| source | dist | soundlevel | attenuation |
|---|---:|---:|---:|
| shipped `NormalFootfallDist` | 300 | 58 | 2 |
| shipped `HeavyFootfallDist` | 600 | 64 | 1 |
| cvar `footstep_normal_dist` | 256 | 57 | 2 |
| cvar `footstep_heavy_dist` | 512 | 63 | 1 |
| — | ≤ 36 | ≤ 40 | 4.0 |

**The attenuation is not what makes the sound quiet.** It is only handed to
`CPASAttenuationFilter` (`0x1019d4b0`), which culls recipients farther than
`2000.0 / attenuation` (`0x104492ac` = 2000.0) — and that whole loop is skipped when
`gpGlobals->maxClients == 1`. In single-player VtMB the attenuation value therefore does
**nothing at all**; the falloff the player hears is entirely the *soundlevel* (§3).

### 1.5 The surface

`this->m_pSurfaceData` at **`+0x5b90`** on `CAI_BaseNPC`. Null → silent, and it is null until the
NPC has moved.

- written per move by `CAI_Navigator::MoveEnact` (`0x102ef870`);
- cleared by `0x10273390` (`CAI_BaseNPC::NPCInit` — health, life state, the first sequence, the
  think) and `0x1027bf50` (`CAI_BaseNPC` vtable slot 130, the save-restore arm that re-resolves the
  schedule by name and resets the navigator). **Nothing clears it at the end of a leg**: an
  arrival, a freeze or a teleport leaves the last floor cached until the next `MoveEnact`.

The two sound indices live in the `surfacedata_t`: `+0x2c` = `stepleft`, `+0x2e` = `stepright`
(`unsigned short` soundscript indices), `+0x70` = the **game-material character** (§2.1).

### 1.6 The emit

```c
if (RandomInt(0, 1))  sample = m_pSurfaceData->stepleft;   // +0x2c
else                  sample = m_pSurfaceData->stepright;  // +0x2e
const char *name = physprops->GetString(sample);           // g_physprops 0x10723944, vfunc +0x18
if (!name || !*name) return;
// interface 0x1070b22c, vfunc +0x8c:
EmitSound(entindex /* this+0x2e0 */, 4 /* CHAN_BODY */, name,
          volume, soundlevel, 0 /* flags */, 100 /* pitch */, 0, 0, 1, 0);
```

`RandomInt` is `(*0x1070b244)->vfunc+0x8 (0, 1)` — the shared RNG stream. Pitch is a hard `100`
(`0x64` at `1026d688`); there is no pitch jitter on the NPC path.

**No `CSoundEnt::InsertSound` anywhere on this path.** An NPC footfall is audio only: it raises no
AI hearing stimulus, so one NPC never hears another walk. (`CSoundEnt::InsertSound` is
`0x101bac90`, thunk `0x1000bca8`; its 18 call sites are weapons, damage, doors, corpses, grapples
and `ai_sound` — no movement site among them.)

### 1.7 Species overrides

Every one of these overrides `HandleAnimEvent` and **returns without calling `0x1026d460`**.

| class | fn | events | behaviour |
|---|---|---|---|
| `CNPC_VMingXiao` | `0x10392a70` | `2050`/`2051` | swallowed — silent |
| `CNPC_VHengeyokai` (shark form) | `0x1037fb60` | `2050`–`2053` | `UTIL_ScreenShake(amp 2.0, freq 0.2, dur 0.2 s, radius 1024)` **+** vfunc `+0x9a4` |
| `CNPC_VTzimisceHeadClaw` | `0x103c1540` | `2050`/`2051` | shake `(1.3, 0.2, 0.2 s, 1024)` **+** vfunc `+0x9ac(1` or `0)` |
| `CNPC_VTzimisceRunner` | `0x103c32c0` | `2050`/`2051` | vfunc `+0x9ac(1` or `0)` **only — no shake**; `2052`/`2053` fall through to the base |
| `CNPC_VSabbatLeader` | `0x103aa5e0` | — | `FootstepSound()` driven by the schedule tasks `TASK_VSABBATLEADER_PLAY_FOOTSTEP_SOUND` / `..._STOP_FOOTSTEP_SOUND`, not by events: `character/monster/andrei_transfo*`, `RandomInt(0,6)`, volume 0.8–1.0 |
| `CNPC_VWerewolf` | — | `2100`/`2101` | a different event pair; cvars `werewolf_footstep_sounds` (`0x103d8b10`) / `werewolf_footstep_shakes` (`0x103d8ba0`) |

`CNPC_VTzimisceRunner::HandleAnimEvent` in full (`vtmb_code` reports it damaged; this is the asm):

```asm
103c32c6  SUB EAX,0x802
103c32cb  JZ  103c32e9          ; 2050 -> arg 1
103c32cd  DEC EAX
103c32ce  JZ  103c32d9          ; 2051 -> arg 0
103c32d4  JMP 0x100146e1        ; everything else -> base HandleAnimEvent
103c32d9  MOV dword ptr [ESP+4],0x0
103c32e3  JMP dword ptr [EAX + 0x9ac]
103c32eb  MOV dword ptr [ESP+4],0x1
103c32f3  JMP dword ptr [EDX + 0x9ac]
```

The two vfuncs, read out of the class vtables in the image:

| slot | class | target | what it does |
|---|---|---|---|
| `+0x9a4` (617) | `CNPC_VHengeyokai` (`vftable 0x104b683c`) | `0x103817f0` | `EmitSound(CHAN_BODY 4, character/monster/hengeyokai/stomp_{1..4}.wav` chosen by `RandomInt(0,3)`, vol 1.0, 0.8, pitch 100) through a `CPASAttenuationFilter` |
| `+0x9ac` (619) | `CNPC_VTzimisceRunner` (`vftable 0x104cd144`), `CNPC_VTzimisceHeadClaw` | `0x103c4160` | picks `character/monster/TC_Runner/foot_steps_{1,2}` or `{3,4}` by the passed flag and `RandomInt(0,1)`, **plus** `character/monster/TC_Runner/Breath{1..4}` (`RandomInt(0,3)`); both `CHAN_BODY 4`, vol 1.0 / 0.8, pitch 100 |

Neither reads a `surfacedata_t`, a template or a cvar. Species footsteps are fixed wav pools.

### 1.8 What the port has

The inputs `0x1026d460` reads, and where each lives (the sequencing is §1.9).

| retail input | port |
|---|---|
| `2050`–`2053` delivered to the NPC | claimed by `FElysiumNpc::HandleAnimEvent` on every arm |
| `m_pSurfaceData` `+0x5b90` per NPC | `FElysiumLocomotionSample::GroundSurface`, traced per move step by `AElysiumNpcBody::RefreshGroundSurface`, cleared at spawn only |
| `NormalFootfallVol/Dist`, `HeavyFootfallVol/Dist` | `FElysiumClanTemplate::GeneralFloat` over the resolved template, latched per NPC |
| the five `footstep_*` cvars | `FElysiumFootstepTuning` on the entity world, declared into the VtMB console store |
| `20·log10(d/36)+40` and `SNDLVL_TO_ATTN` | `ElysiumSoundLevel::FromDistanceUnits` / `SourceAttenuation` (the latter inert, §3.5) |
| the three-way player gate | `ElysiumFootsteps::NpcStepsMuted`: `HasScriptedCamera` / `HasTrackCamera` / open conversation; terminal/keypad sessions still absent (§4.1) |
| `CHAN_BODY` one-voice-per-owner | `IElysiumAudio::PlayBodySound`, `(owner, channel)` replacement on the map actor |
| no AI stimulus | matches: emit nothing |

### 1.9 Port notes (wave 2, B1)

The table above is where each input lives. This is the whole of `0x1026d460`, sequenced
in `FElysiumNpc::NpcStep` (`Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp`) over the pure rules
in `Substrate/ElysiumFootsteps.{h,cpp}`.

| retail step | port |
|---|---|
| `CAI_BaseNPC::HandleAnimEvent 0x10274e30`, footstep arm | `FElysiumNpc::HandleAnimEvent` — 2050/2051 → `NpcStep(false)`, 2052/2053 → `NpcStep(true)`; every other id falls to `FElysiumCombatCharacter::HandleAnimEvent` and then to the census |
| step 1, the three-way player gate (`1026d467`) | `ElysiumFootsteps::NpcStepsMuted(World)` — `HasScriptedCamera()`, `HasTrackCamera()`, `GetOpenDialog()`; **partial**, see §4.1 |
| step 3, the source (`1026d4aa`) | `ElysiumFootsteps::NpcSource(Template, bHeavy, Tuning)` — `footstep_npc_use_templates` selects the four `General` keys with `0x101d3f10`'s defaults, or the cvar pair |
| step 2, the template resolve (`1026d4a0`) | latched once per NPC in `FElysiumNpc::ApplyResolvedTemplate` instead of re-resolved per step — the merge walks a parent chain and a walking body steps two or three times a second. **Divergence in cost, not in answer**: the record is the same resolved one, and a template cannot change under a live NPC |
| step 5, `m_pSurfaceData +0x5b90` (`1026d597`) | the motor's last published `FElysiumLocomotionSample::GroundSurface`, which is written per move step by `AElysiumNpcBody::RefreshGroundSurface` — `NAME_None` → silent, counted once per body on a Verbose line |
| step 6, `0x10228350` | `ElysiumSoundLevel::FromDistanceUnits` |
| step 8, `SNDLVL_TO_ATTN` (`1026d5e1`) | **not emitted.** It feeds `CPASAttenuationFilter` only, and that loop is skipped at `maxClients == 1` (§3.5). `ElysiumSoundLevel::SourceAttenuation` reproduces the number so the recovered values stay assertable |
| step 9, the coin flip (`1026d626`) | `ElysiumFootsteps::PickNpcWav` on `EElysiumRngStream::Footsteps` |
| step 11, the emit (`1026d668`) | `IElysiumAudio::PlayBodySound(Handle, { Rel, Volume, SoundLevelDb, Pitch 1.0, Channel Body })` |
| no `CSoundEnt::InsertSound` | matches: the arm emits no game sound at all |

Three things worth stating plainly, because none of them is obvious from the retail listing:

- **Every arm answers CLAIMED**, including the muted one, the surfaceless one and the poolless one.
  `0x1026d460` returns from each of its early exits and never falls through to
  `CBaseAnimating::HandleAnimEvent`, so an id it decided to make no sound for still *has* a handler.
  That is what keeps 2050–2053 off the anim-event census once this arm exists — an unclaimed id is
  work that has not landed, and "handled, deliberately silent" is a different fact.
- **The variation pick inside a pool is the port's own draw.** Retail's `surfacedata_t` carries one
  soundscript index a side and the *engine* picks among that script's repeats; the bake has already
  flattened the script into `FElysiumSurfaceSounds::StepLeft/StepRight` with its duplicates
  preserved, so `PickNpcWav` draws twice — the side, then the entry — from the Footsteps stream.
  Named divergence: it consumes a draw retail's game-DLL stream would not have consumed. Duplicates
  are alternates and are legal picks, which is why the pools are never collapsed.
- **A half-authored surface steps on one foot only.** The coin flip is taken before the name is
  resolved, so a `surfacedata_t` that declares `stepleft` and no `stepright` is silent on about half
  its footfalls. That is retail (`1026d666`), not a port artefact, and the case that pins it is
  `Elysium.Substrate.Footsteps.NpcNoPool`.

The species overrides (§1.7) landed as the classname-keyed table in `ElysiumFootsteps.h`
(`EElysiumFootstepPolicy`, `FElysiumFootstepSpecies`) read by `FElysiumNpc::OverrideFootstep`, which
runs *before* the gate because retail's overrides replace the whole function. `npc_VTzimisceRunner`
is the one row whose classname this port registers, so it is the only one in force; the other three
are recovered data. What is played and what is only reported is §4.1.

Tests: `Elysium.Substrate.Footsteps.{NpcNormal, NpcHeavy, NpcCvarPath, NpcMuted, NpcNoSurface,
NpcNoPool, CoinFlip, SpeciesPolicy}` and `Elysium.Content.FootstepRecords`.

---

## 2. Player footsteps

`CBasePlayer::HandleAnimEvent` swallows `2050`–`2053`. Everything below is driven by
`CGameMovement`.

### 2.1 The three functions and the clock

| fn | address | role |
|---|---|---|
| `CGameMovement::ReduceTimers` | `0x1011f520` (vfunc 24) | decrements the clock every move |
| `CGameMovement::UpdateStepSound` | `0x1011e940` | decides *whether*, *which surface*, *how loud*, and re-arms the clock |
| `CGameMovement::PlayStepSound` | `0x1011e430` (vfunc 16) | alternates feet and emits |
| `CGameMovement::CheckFalling` | `0x10125db0` | the landing step |
| `CGameMovement::CategorizePosition` | `0x1011e560` | refreshes the movement's cached surface |

`ReduceTimers` — called once per `PlayerMove` (`0x101274a0`):

```c
float dt = gpGlobals->frametime * 1000.0f;               // 0x10447ee0 = 1000.0
if (player->m_flStepSoundTime  > 0) { player->m_flStepSoundTime  -= dt; if (< 0) = 0; }  // +0x2328
if (player->m_flDucktime       > 0) { ... }                                             // +0x1ee0
if (player->m_flSwimSoundTime  > 0) { ... }                                             // +0x232c
```

**The clock is clamped at 0 and never goes negative.** That fact kills one arm of
`UpdateStepSound` outright (§2.2).

**Where in the move.** `PlayerMove` (`0x101274a0`) calls `UpdateStepSound` at the **top** of the
move — after `ReduceTimers`, after the `m_flFallVelocity` refresh (`if (!GetGroundEntity())
m_flFallVelocity = -velocity.z`) and *before* `CategorizePosition`, `Duck` and the movetype
switch — so it reads the previous move's ground entity, water level and velocity. `CheckFalling`
is the last line of `FullWalkMove`, after the move. On the frame a body lands the ordinary pass
therefore still sees no ground entity and takes early return #5; the only steps that frame are
`CheckFalling`'s pair (§2.4).

`CategorizePosition` (`0x1011e560`) is what fills the surface the step will use:

```c
m_nSurfaceProps  /* this+0x98 */ = trace.surface.surfaceProps;
m_pSurfaceData   /* this+0x9c */ = physprops->GetSurfaceData(m_nSurfaceProps);
m_surfaceFriction/* this+0xa0 */ = min(GetPhysicsProperties(...) * K, 1.0);   // K at 0x10462914
m_chTextureType  /* this+0xa4 */ = *(char *)(m_pSurfaceData + 0x70);          // game.material
```

### 2.2 `UpdateStepSound` (`0x1011e940`), arm by arm

Read from the asm; the decompiled C at this address **mislabels the term added to the clock** (it
renders `fStack_10`, the `velwalk` local, where the listing loads `[ESP+0x1c]`, the `flduck`
local). `ESI` = `CGameMovement`, `[ESI+4]` = `mv` (`CMoveData`), `[ESI+8]` = the player.

**Early returns**

| # | test | asm |
|---:|---|---|
| 1 | `m_flStepSoundTime > 0` | `1011e94c` |
| 2 | `GetFlags() & 0x60` (`FL_FROZEN` \| `FL_ATCONTROLS`) | `1011e96a` |
| 3 | `GetMoveType() == 9` (`MOVETYPE_NOCLIP` in VtMB's numbering; `10` is `MOVETYPE_LADDER`) | `1011e97d` |
| 4 | `sv_footsteps == 0` — **unconditional here**, unlike `PlayStepSound` | `1011e986`, cvar `0x109ef090` |
| 5 | `!onladder && GetGroundEntity() == NULL && GetWaterLevel() == 0` | `1011ea62` |
| 6 | 2-D speed `<= 0.0` (`0x1044fab0`, double 0.0) | `1011eaa5` |
| 7 | 3-D speed `< velwalk` **and** `m_flStepSoundTime != 0` | `1011eabb` — **dead**: the clock is clamped at 0, so it is always exactly 0 here |

Because of #7 the `velwalk` band never gates anything: **the only speed gate that survives is
"moving at all" (#6)**, and `velwalk` is used for nothing else.

**Bands** (`onladder = GetMoveType() == 10`; `waterlevel` is player `+0x3e0`):

| condition | `velwalk` | `velrun` | `flduck` |
|---|---:|---:|---:|
| `FL_DUCKING` (2) **or** on ladder **or** `waterlevel != 0` | 60.0 | 80.0 | **100.0** |
| otherwise | 120.0 | 220.0 | **0.0** |

`bWalking = (speed3D < velrun)` (`1011eae6`).

**The four arms** — each sets the sound pool, the volume and the fresh clock:

| arm | condition | surface | volume | `m_flStepSoundTime` |
|---|---|---|---|---:|
| ladder | `onladder` | `physprops->GetSurfaceData(GetSurfaceIndex("ladder"))` (`0x10572554`) | **0.35** (`0x3eb33333`) | **350.0** |
| wade | `waterlevel >= 2` | `..."wade"` (`0x1057254c`) | **1.0** | **600.0** |
| water | `waterlevel == 1` | `..."water"` (`0x10572544`) | **1.0** | `bWalking ? 400 : 300` |
| dry | `waterlevel == 0` | the movement's cached `m_pSurfaceData` (`CGameMovement+0x9c`) | by game material, below | `bWalking ? 400 : 300`, then **800.0** if 2-D speed `<= 100.0` (`0x10450564`) |

**The wade silent phase.** The `waterlevel >= 2` arm runs a **module-global** counter at
`0x1070b898` before doing anything:

```c
if (g_wadeStepCounter == 0) { g_wadeStepCounter = 1; return; }   // silent, clock NOT re-armed
if (g_wadeStepCounter == 3)   g_wadeStepCounter = 0;
else                          g_wadeStepCounter++;
```

so the cycle is **silent, sound, sound, sound** — one step in four is dropped. The counter is a
single static shared by everything, and the silent pass returns *before* the clock is re-armed, so
the next move retries immediately.

**Dry volume**, from `CGameMovement::m_chTextureType` (`+0xa4`) — a jump table over `'D'`…`'V'`
(table `0x1011ed90`, index bytes `0x1011eda0`). Only two materials are special; every other
character (`'E'`…`'U'` and anything outside `'D'`…`'V'`) takes the default:

| game material | walking | running | addresses |
|---|---:|---:|---|
| `'D'` (dirt) | **0.25** | **0.55** | `0x10449260` / `0x10462928` |
| `'V'` (vent) | **0.40** | **0.70** | `0x1045a3f0` / `0x104492d0` |
| default | **0.20** | **0.50** | `0x10449198` / `0x10449270` |

**The tail**, in order (`1011ec9c` onward):

```c
player->m_flStepSoundTime += flduck;              // 0 or 100 — the [ESP+0x1c] local, NOT velwalk
if (GetFlags() & FL_DUCKING) fvol *= 0.35;        // 0x10462918
fvol *= footstep_pc_vol.GetFloat();               // cvar 0x1070b2d0, default 0.5
if (fvol > 1.0) fvol = 1.0;                       // 0x10449280
if (onladder || waterlevel != 0 || GetGroundEntity() == NULL) goto play;
if (!(mv->m_nButtons & 0x79a)) {   // IN_JUMP|IN_FORWARD|IN_BACK|IN_LEFT|IN_RIGHT|IN_MOVELEFT|IN_MOVERIGHT
    if (speed2D <= velrun_slot) return;           // see below
}
play:
PlayStepSound(psurface, fvol, /*force*/ false);
```

So the **effective intervals**, after `+= flduck`:

| state | walking | running | slow (2-D ≤ 100) |
|---|---:|---:|---:|
| dry, standing | 400 ms | 300 ms | 800 ms |
| dry, ducked | 500 ms | 400 ms | 900 ms |
| ladder | 450 ms | 450 ms | — |
| water (level 1) | 500 ms | 400 ms | — |
| wade (level ≥ 2) | 700 ms | 700 ms | — |

**A retail quirk worth naming.** The final `speed2D <= …` compare reads `[ESP+0x18]`, the slot that
held `velrun` — but the *dry* arm overwrote that slot at `1011ebf2` with the **integer** interval
(300 or 400) so it could `FILD` it. Read back as a `float` at `1011ed52` the slot is a denormal
(≈ 4.2e-43), and the dry arm is the only path that reaches the compare (ladder and water jump
straight to `play`). The button test and the speed test are therefore both **no-ops in retail**:
on dry ground the step always plays. The port should mirror the observable behaviour — always
play — and not reproduce the clobber.

### 2.3 `PlayStepSound` (`0x1011e430`, `CGameMovement` vfunc 16, `RET 0xc`)

```c
void PlayStepSound(surfacedata_t *psurface, float fvol, bool force)
{
    if (gpGlobals->maxClients > 1 && sv_footsteps.GetFloat() == 0.0f) return;  // SP: never checked
    if (!psurface) return;
    physprops = MoveHelper()->GetSurfaceProps();          // 0x1071a278 vfunc +0x24
    if (!force && !this->ShouldPlayStepSound(psurface, fvol)) return;          // vfunc +0x3c
    sample = player->m_nStepside /* +0x1ee4 */ ? psurface->stepleft  /* +0x2c */
                                              : psurface->stepright /* +0x2e */;
    player->m_nStepside = (old == 0);                     // toggle
    if (sample == 0) return;                              // a surface with no step pool is silent
    if (!mv->m_bFirstRunOfFunctions /* *(char*)mv */) return;   // prediction guard
    MoveHelper()->StartSound(mv->m_vecAbsOrigin /* mv+0x70 */,
                             4              /* CHAN_BODY  */,
                             physprops->GetString(sample),
                             fvol,
                             75             /* soundlevel */,
                             0              /* flags      */,
                             95 + RandomInt(0, 10) /* pitch 95…105 */);
}
```

`sv_footsteps` (`0x109ef090`, default `1`, help *"Play footstep sound for players"*) gates the
whole clock at the top of `UpdateStepSound` regardless of `maxClients`, so it does disable player
footsteps in single-player — just from the other end.

The alternating foot lives on the **player** (`m_nStepside`, `+0x1ee4`), not on the movement, so it
survives across moves and is save-visible.

### 2.4 `CheckFalling` (`0x10125db0`) — the landing

**The safe-fall speed** (recomputed every call, `10125e04`…`10125e9d`):

```c
float safeSpeed = sqrtf(2.0f * sv_jump_boost * sv_gravity)          // 25.0 * 800.0 -> 200.0
                + rules.BaseJumpVelocity * mv[0xa8]
                + 0.75 * rules.BaseJumpVelocity * mv[0xa8] * mv[0xb4];   // 0.75 at 0x10462958
if (safeSpeed < fall_threshold) safeSpeed = fall_threshold;              // cvar, default 200
```

| symbol | address | default |
|---|---|---|
| `sv_gravity` | cvar `0x109ef2d0` | 800 |
| `sv_jump_boost` | cvar `0x109ef000` | 25.0 |
| `fall_threshold` | cvar `0x1070b708` | 200 |
| `rules.BaseJumpVelocity` | `rules+0x3c8`, accessor `0x101e7d90` (thunk `0x10006f0f`) | `rules.txt` `Jumping/BaseJumpVelocity` = **185** |

`mv+0xa8` and `mv+0xb4` are the per-character jump scalars the move setup copies out of the
`Jumping` feat tables (`Vertical` at `rules+0x20`, `JumpDuration` at `rules+0x98`); at feat 1 they
are `1.0` and `0.2 s`, giving **`safeSpeed ≈ 412.75`**. The shape is deliberate: *you can always
land your own jump without a hard landing.*

**The two derived fall speeds**, both built once at rules-load time (`0x101e6310`) from `sv_gravity`
and the authored distances:

| field | accessor | formula | shipped value |
|---|---|---|---:|
| `rules+0x3ec` | `0x101e7db0` (thunk `0x10010feb`) | `sqrt(2 · sv_gravity · SafeFallDist)`, `SafeFallDist` = `rules+0x3e0` = **240** | **619.68** |
| `rules+0x3f0` | `0x101e7e40` (thunk `0x100076fd`) | `sqrt(2 · sv_gravity · SupernaturalFallDist)`, `rules+0x3e8` = **500** | **894.43** |

(`FallDistPerDmg` = `rules+0x3e4` = 8.4 is the damage rule and is not read here.)

**The volume ladder.** Entered only when `GetGroundEntity() != NULL` and the "dead/disabled" test
`0x100138b3` is false. `fallVel` is `player->m_flFallVelocity` (`+0x1ee8`).

```c
float fvol = 0.0f;
if (fallVel < safeSpeed) {                       // SOFT
    if (fallVel > 0.0f) {
        fvol = 0.25f;
        mv[0xc0] = (mv[0xc0] < 5) ? 0 : 8;       // player anim state
        mv[0xbc] = 0.0f;
    }
} else if (waterLevel > 0) {                     // landed in water
    mv[0xc0] = 9;
    float t = fallVel / rules.SafeFallSpeed;     // rules+0x3ec
    fvol = (t > 1.0f) ? 1.0f : (t < 0.0f ? 0.0f : t);
} else {                                         // HARD, dry
    if (GetGroundEntity()->IsFloating())         // 0x1000b186
        player->m_flFallVelocity -= 173.0f;      // 0x104629e4 — a real write to the field
    if (fallVel > rules.SafeFallSpeed) {         // FATAL band
        (void)rules.SupernaturalFallSpeed;       // read and DISCARDED at 10125ffd
        mv[0xc0] = 8;
        mv[0xbc] = <0x101282b0(this)> + gpGlobals->curtime;
        physprops->vfunc_0x1c();
        fvol = 1.0f;
    } else {
        if (!(mv->m_nButtons & 0x79a)) mv[0xc0] = 8;
        mv[0xbc] = <0x101282b0(this)> + gpGlobals->curtime;
        if (fallVel > rules.SafeFallSpeed * 0.5f) fvol = 0.85f;   // 0.5 at 0x104454d0
        else if (fallVel >= 200.0f)               fvol = 0.5f;    // 200.0 at 0x104492b8
        else                                      fvol = 0.65f;
    }
}
```

Two things to keep straight:

- the `0.5` / `0.65` pair is **inverted** relative to intuition — the *faster* fall is the quieter
  one. That is what the listing says (`101260ab` `FCOMP 200.0`, `JP` → 0.5, fall-through → 0.65).
- with the shipped rules those two arms are **unreachable on ordinary ground**: to get here you
  need `fallVel ≥ 412.75`, and `SafeFallSpeed/2 = 309.8`, so `0.85` always wins. The only way in is
  the **`IsFloating` reduction**: standing on a floating entity subtracts 173 from the fall speed
  first, which can drop a 450 u/s landing to 277 and select `0.5`.

**The step itself** (`1012617a`):

```c
if (fvol > 0.0) {
    player->m_flStepSoundTime = 0;                       // reset the clock
    UpdateStepSound();                                   // re-arm it and refresh the surface
    PlayStepSound(m_pSurfaceData /* CGameMovement+0x9c */, fvol, /*force*/ true);
    ...
}
if (GetGroundEntity()) player->m_flFallVelocity = 0;
```

`UpdateStepSound` is called with the clock at exactly 0, so it *can* emit a second, ordinary step
in the same frame (it only bails on `speed2D <= 0`); the forced landing step then plays on the
other foot. Retail double-steps on a fast landing and that is not a bug to fix silently — name it
if the port chooses to suppress it. It never *triple*-steps: `PlayerMove`'s own call ran before the
move, over the previous frame's null ground entity, and returned at #5 (§2.1).

**What `+0x1f04` and `+0x1efc` are.** Neither is fall damage. `CBasePlayer::Dump` (`0x1015e140`)
names `+0x1efc … +0x1f04` as the QAngle **`m_vecPunchangle`**:

```c
if (fallVel >= safeSpeed && GetGroundEntity()) {
    if (mv[0xc0] == 0xb) physprops->vfunc_0x30();
    player->m_vecPunchangle[ROLL /* +0x1f04 */] = fallVel * 0.013;   // 0x104629d8
    if (player->m_vecPunchangle[PITCH /* +0x1efc */] > 8.0)          // 0x1045597c
        player->m_vecPunchangle[PITCH] = 8.0;                        // 0x41000000
}
```

This is the **landing view punch**, verbatim Source. Fall *damage* is computed elsewhere from
`FallDistPerDmg`. The footstep port needs neither; it needs only `fvol` and the clock reset.

Other names recovered from the same dump, all on `CBasePlayer`: `+0x1edd` `m_bDucked`,
`+0x1ede` `m_bDucking`, `+0x1ee0` `m_flDucktime`, `+0x1ee4` `m_nStepside`, `+0x1ee8`
`m_flFallVelocity`, `+0x1eec` `m_nOldButtons`, `+0x2088` `m_nButtons`, `+0x2328`
`m_flStepSoundTime`, `+0x232c` `m_flSwimSoundTime`, `+0x2330` `m_vecLadderNormal`.

### 2.5 The hearing stimulus — `CBasePlayer::UpdatePlayerSound` (`0x1016b480`)

Called from **`CBasePlayer::PostThink` (`0x1016be10`)** through thunk `0x10010578`. This is the
answer to "where do `PLAYER_FOOTSTEP_SNEAK/WALK/RUN` get emitted": **nowhere per-step**. The player
owns one permanently reserved `CSound` record and this function rewrites its volume every think.

```c
idx    = CSoundEnt::ClientSoundIndex(this->+0x2e0 /* edict */);
pSound = CSoundEnt::SoundPointerForIndex(idx);
if (!pSound) { Msg("Client lost reserved sound!\n"); return; }
if (GetFlags() & 0x8000 /* FL_NOTARGET */) { pSound->m_iVolume = 0; return; }

int vol = 0;
if (m_nButtons & IN_JUMP /* 2 */) {                       vol = T[PLAYER_JUMP];      occl = O[3]; }
else if (GetFlags() & FL_ONGROUND /* 1 */) {
    int anim = this->+0x1db4;                             // the player animation state
    if      (anim == 8)                {                  vol = T[PLAYER_LAND_SOFT]; occl = O[4]; }
    else if (anim == 10 || anim == 11) {                  vol = T[PLAYER_LAND_HARD]; occl = O[5]; }
    else {
        if (this->+0x268 & 0x1000) CalcAbsoluteVelocity();
        float speed = |m_vecAbsVelocity /* +0x3bc..+0x3c4 */|;   // 3-D
        if (speed > 0.0f) {
            if (GetFlags() & FL_DUCKING /* 2 */)          { vol = T[FOOTSTEP_SNEAK]; occl = O[0]; }
            else if (speed > PLAYER_RUN_SPEED)            { vol = T[FOOTSTEP_RUN];   occl = O[2]; }
            else                                          { vol = T[FOOTSTEP_WALK];  occl = O[1]; }
        }
        // speed == 0 -> vol stays 0
    }
}
// not on ground and not jumping -> vol stays 0

m_iTargetVolume /* +0x2130 */ = vol;
int v = pSound->m_iVolume;
if      (v <  vol) v = vol;
else if (v >  vol) { v = (int)((float)v - gpGlobals->frametime * 250.0f);   // 0x104704b8 = 250.0
                     if (v < vol) v = vol; }
if (m_fNoPlayerSound /* +0x22a0 */) v = 0;

pSound->m_hOwner    = GetRefEHandle();
pSound->m_vecOrigin = <vfunc +0x370>();
pSound->m_iType     = 4;                     // SOUND_PLAYER
pSound->m_iVolume   = v;
pSound->m_flTime    = gpGlobals->curtime;
m_iDebugStealthSound /* +0x1c9c */ = (v + 3 * prev) >> 2;   // the smoothed value the debug HUD shows
```

Answers to the questions this was opened for:

- **Where in the frame:** once per player think, in `PostThink`. Not per step, not per move.
- **Radius source:** the resolved `sound_volume_table.txt` row for the category, cached in the
  table object at `0x1072bc20` (§2.6).
- **Walk/run split:** 3-D absolute velocity against `PLAYER_RUN_SPEED` (`table+0xfc` =
  `0x1072bd1c`, authored **128.0**, loader default `-1.0`), strict `>`.
- **Sneak rule:** `FL_DUCKING`. **Ducking is the whole of "sneaking"** on this path — the stealth
  feat, light level and the player's stealth surface play no part in choosing the category.
- **In water:** the function never reads the water level. Wading (still `FL_ONGROUND`) keeps
  emitting the footstep categories; **swimming emits nothing**, because losing ground contact
  drops the volume to 0.
- **Do NPCs raise footstep stimuli?** No (§1.6).
- The volume never jumps down: it rises instantly and decays at **250 units per second**, so a
  sprint heard once keeps ringing for about a second of AI polling.

### 2.6 The table

`vdata/System/sound_volume_table.txt`, loaded by `0x101af9d0` → `0x101af9f0` into the global
`SoundVolumeTable` object at **`0x1072bc20`**; the `SoundTypes` resolution pass is the fragment at
`0x101afd01`; `MiscData` is `0x101afd90` → `0x101afdc0`.

Object layout: `+0x00` 34 dwords, the **resolved radius per named category**; `+0x88` 34 bytes, the
resolved occlusion flag per category; `+0xAC` 16 dwords, radius per `VolumeLevels` row; `+0xEC` 16
bytes, `OccludedVolumeLevels`; `+0xFC` `PLAYER_RUN_SPEED`; `+0x100` the loaded flag. A row naming a
level outside `0…15` warns and falls back to level **2**.

The category enum is file order: `0 PLAYER_FOOTSTEP_SNEAK`, `1 PLAYER_FOOTSTEP_WALK`,
`2 PLAYER_FOOTSTEP_RUN`, `3 PLAYER_JUMP`, `4 PLAYER_LAND_SOFT`, `5 PLAYER_LAND_HARD`,
`6 PLAYER_AGGRESSIVE_FEED`, … `27 NPC_DISCIPLINE_ALERT`, … `33 EXPLOSION` (34 rows).

| category | level | radius | occludable |
|---|---:|---:|---:|
| `PLAYER_FOOTSTEP_SNEAK` | 1 | **180** | yes |
| `PLAYER_FOOTSTEP_WALK` | 2 | **240** | yes |
| `PLAYER_FOOTSTEP_RUN` | 2 | **240** | yes |
| `PLAYER_JUMP` | 2 | **240** | yes |
| `PLAYER_LAND_SOFT` | 1 | **180** | yes |
| `PLAYER_LAND_HARD` | 2 | **240** | yes |

Note that walk and run resolve to the *same* level, so today the walk/run split changes the
category name and nothing else. Sneak is the only one that is quieter.

### 2.7 What the port has

The inputs §2 reads, and where each lives (the files and the decisions are §2.8).

| retail input | port |
|---|---|
| `m_flStepSoundTime` clock in `PlayerMove` | `FElysiumPlayer::StepSoundMs`, advanced by `ElysiumFootsteps::AdvanceStepClock` from `TickStepClock` — the one step producer; the map actor's water clock is gone |
| `m_chTextureType` from the ground trace | `FElysiumLocomotionSample::GroundSurface` off `CategorizePosition`'s sweep, and the `GameMaterial` letter on the resolved `FElysiumSurfaceSounds` |
| `m_nStepside` | `FElysiumPlayer::bStepSide` |
| `footstep_pc_vol`, `sv_footsteps` | `FElysiumFootstepTuning` |
| `CheckFalling` landing volume | `ElysiumFootsteps::LandingStepVolume` over `FElysiumLocomotionSample::FallSpeedAtLanding`, the mover's `m_flFallVelocity` on the landing frame |
| `UpdatePlayerSound` | `FElysiumPlayer::UpdatePlayerSound` from `Think`, over `ElysiumFootsteps::HearingCategory` |
| the reserved-slot semantics (one live stimulus, 250/s decay) | `FElysiumGameSoundBus::Refresh` / `Retire` on one slot, the radius decayed at 250 u/s |
| `FL_NOTARGET`, `m_fNoPlayerSound` | `FElysiumPlayer::bNoPlayerSound`, a seam nothing sets yet (§4) |

### 2.8 Port notes (wave 2, B2)

Where §2 landed, what it is called, and every place the port had to decide something the recovery
did not settle. Nothing here reopens the corpus.

**The files.**

| retail | port |
|---|---|
| `ReduceTimers 0x1011f520` + `UpdateStepSound 0x1011e940` | `ElysiumFootsteps::AdvanceStepClock` (`Private/Substrate/ElysiumFootsteps.{h,cpp}`), one call so a producer cannot forget the decrement |
| the `gamematerial` switch + the volume tail | `ElysiumFootsteps::PlayerDryVolume` |
| `CheckFalling 0x10125db0`'s volume ladder | `ElysiumFootsteps::LandingStepVolume` (fall speed IN/OUT — the floating reduction is a real write) |
| the derived safe-fall speed | `ElysiumFootsteps::MaxSafeFallSpeedUnits`, with the shipped 412.75 as `kMaxSafeFallSpeed` |
| `UpdatePlayerSound 0x1016b480` | `ElysiumFootsteps::HearingCategory` / `HearingRadiusUnits` / `DecayHearingRadiusUnits` + `FElysiumPlayer::UpdatePlayerSound` |
| `PlayStepSound 0x1011e430` | `PlayPlayerStepSound` in `Private/Substrate/ElysiumPlayerEntity.cpp` → `IElysiumAudio::PlayBodySound` |
| the sequencing (`PlayerMove` → clock at the top of the move, `CheckFalling` → landing at the end of it) | `FElysiumPlayer::TickStepClock`, called from `FElysiumEntityWorld::Tick` right after `SyncFromBody` on the post-move pass; on a landing frame the ordinary pass is handed the pre-landing premise, so a landing is retail's two steps and never three |
| `m_flStepSoundTime` / `m_nStepside` / the wade counter `0x1070b898` | `FElysiumPlayer::StepSoundMs` / `bStepSide` / `WadeStepPhase` |
| `m_flFallVelocity` `player+0x1ee8` | `UElysiumMovementComponent::FallVelocity`, published as `FElysiumLocomotionSample::FallSpeedAtLanding` on the one frame the ground returns |
| `CBasePlayer::HandleAnimEvent`'s 2050–2053 swallow | `FElysiumPlayer::HandleAnimEvent` → `ElysiumFootsteps::PlayerSwallows` |

**The water clock moved.** `AElysiumMapActor::UpdatePlayerWaterFootsteps` /
`PlayPlayerWaterFootstep` and `ElysiumWaterAudio`'s `StepIntervalSeconds` / `IsSoundingStep` /
`StepCue` are deleted: the water and wade arms are two arms of `UpdateStepSound`, not a water
feature, and two producers meant two step timers and two feet. The map actor keeps the water-LEVEL
classification (`PlayerWaterLevelNow`), which reaches the clock on the locomotion sample; the pools
come off the same baked `PM_water` / `PM_wade` through `IElysiumEmbodiment::ResolveSurfaceSounds`.
**Two numbers changed with the move, and both are corrections this document made**: the term added
to the interval is `flduck` 100 and not `velwalk` 60 (so a level-1 walk is 500 ms, not 460), and the
water band's 60 u/s minimum gates nothing because the arm that reads it is dead. The moved
assertions live in `Elysium.Substrate.Footsteps.PlayerWater`.

**Dead arms, reproduced as observable behaviour and not as code.** The `velwalk` gate
(`0x1011eabb`) and the button/`velrun` test at the tail (`0x1011ed52`, reading the slot the dry arm
clobbered with an integer) are both no-ops in retail. The port writes neither and comments both at
`AdvanceStepClock`; on dry ground the step always plays.

**The ladder arm is dead in retail too.** `PlayerMove`'s movetype switch has no ladder case and no
map places a ladder entity (`source_movement.md` → "Ladders: VtMB has none"), so the `"ladder"`
string at `0x10572554` survives only as a footstep material name. `FElysiumLocomotionSample::
bOnLadder` exists, is published false by the player mover and false by every NPC motor, and the arm
is written and asserted against 350 ms / 0.35 anyway — a port that dropped it would be reading a
different function.

**Decisions the recovery did not settle.**

1. **The landing's animation state.** §2.5 needs `+0x1db4 == 8` for `PLAYER_LAND_SOFT` and `10`/`11`
   for `PLAYER_LAND_HARD`, and §4 says the enum is unrecovered. The port latches the band
   `CheckFalling` classified — `fallVel < kMaxSafeFallSpeed` is soft, at or above it is hard — which
   is the split those two authored rows are named after. `FElysiumPlayer::PendingLandCategory`, set
   by the per-frame step clock and consumed by the next think.
2. **`IN_JUMP`.** The port has no button bitfield in the substrate; the stand-in is the sample's
   `JumpHoldRemaining > 0`, VtMB's held-push window. It closes earlier than a held key would, and
   the 250 u/s decay covers the difference.
3. **`GetGroundEntity()->IsFloating()`** (`0x1000b186`, the −173 u/s reduction) has no port: the
   sample publishes a surfaceprop, not the entity under the foot. It is passed `false`, which leaves
   retail's `0.5` and `0.65` arms exactly as unreachable as they are on ordinary ground. The rule is
   still implemented and asserted, so a floating platform is one argument away.
4. **The stealth subtraction on the hearing stimulus is listener-side in retail.**
   `CBaseEntity::AdjustSoundDistForStealth` (`0x1009d850`) has three callers: the two NPC hearing
   predicates `CAI_BaseNPCTroika::0x102b35b0` and `0x1030f7b0`, which invoke it on the sound's
   **owner** entity with the sound and the audible reach (after the listener's own hearing scale),
   and `0x101b9a50`, the rate-limited stealth debug print `UpdatePlayerSound` itself calls. The
   function acts only on `m_iType == 4` and subtracts the owner's stealth hearing distance
   (vfunc `+0x78`), floored at 0; `UpdatePlayerSound` writes the raw table volume. The port applies
   the same subtraction producer-side at its one insertion point
   (`ElysiumStealth::HearingReductionCmFor`); this runtime's listeners carry no hearing scale, so
   the number is identical.
5. **The reserved slot on a retention-window bus.** `FElysiumGameSoundBus` gained
   `Refresh(Slot, …)` / `Retire(Slot)`: the previous record for the slot is removed and the fresh
   one emitted, so the window never grows with a stimulus rewritten every think and a cursor-based
   consumer still sees it. Re-serialising rather than editing in place is load-bearing — a consumer
   that had already passed the old serial would never look at it again. The decay is modelled
   (250 u/s).
6. **Cadence.** Retail runs `UpdatePlayerSound` from `PostThink`, every frame. The port runs it from
   `FElysiumPlayer::Think`, which is deadline-driven at the stealth surface's 0.1 s heartbeat, and
   `RunPlayerThink` is a PRE-move pass — so the stimulus is one frame behind the body and refreshed
   at 10 Hz rather than at the frame rate. The landing latch exists because of exactly this: the
   clock ticks every frame and the think does not.

**Not ported here, deliberately.** `m_vecPunchangle` (the landing view punch,
`ROLL = fallVel * 0.013` clamped at PITCH 8.0) and fall damage: both are `CheckFalling`'s other
outputs and neither is a footstep. `ShouldPlayStepSound` (vfunc `+0x3c`) is still unread, so the
port's unforced step is never suppressed.

**Tests.** `Elysium.Substrate.Footsteps.PlayerClock` / `PlayerVolumes` / `PlayerLanding` /
`PlayerWater` / `PlayerHearing` / `PlayerSwallows` / `PlayerServerFootstepsOff`
(`Private/Tests/ElysiumPlayerFootstepTests.cpp`).

---

## 3. The engine's distance law

Everything in §1 and §2 hands the engine one number: a **soundlevel in dB**. This section is what
that number means, read out of `engine.dll`.

### 3.1 The cvars

| cvar | object | default | read by |
|---|---|---:|---|
| `snd_refdist` | `0x21307378` | **36** | `SNDLVL_TO_DIST_MULT`, `DIST_MULT_TO_SNDLVL`, `SND_GetGain` |
| `snd_refdb` | `0x21315bd8` | **60** | same |
| `snd_gain` | `0x213109d8` | **1** | `SND_GetGain` |
| `snd_gain_max` | `0x21310658` | **1** | `SND_GetGain` |
| `snd_gain_min` | `0x21310470` | **0.01** | `SND_GetGain` |
| `snd_foliage_db_loss` | `0x213108b0` | **4** | `SND_GetGain` |

(A `ConVar` in this build reads its value through `m_pParent` at `+0x04` and `m_fValue` at `+0x28`;
that is why the code references `object+4` rather than the object.)

### 3.2 soundlevel ↔ distance multiplier

`DIST_MULT_TO_SNDLVL`, standalone at `engine.dll 0x20119fe0` and inlined into `SND_GetGain` at
`0x2011a19e`:

```c
int DIST_MULT_TO_SNDLVL(channel_t *ch)
{
    if (ch->dist_mult /* +0x50 */ == 0.0f) return 0;
    return (int)( 20.0 * log10( pow(10.0, snd_refdb / 20.0)
                                / (snd_refdist * ch->dist_mult) ) );
}   //  20.0 at 0x201734f0 and 0x20188038
```

Inverting it gives the direction the game DLL uses when a sound starts:

```c
dist_mult(L) = pow(10, (snd_refdb - L) / 20) / snd_refdist
             = 10^((60 - L)/20) / 36
```

so the **reference distance** of a soundlevel — the range at which `dist * dist_mult == 1` — is

```c
D_ref(L) = 36 * 10^((L - 60) / 20)
```

### 3.3 `SND_GetGain` (`engine.dll 0x2011a0b0`)

```c
float SND_GetGain(channel_t *ch, bool flooping, ?, float dist)
{
    float gain = snd_gain;                                   // 1
    float relative = 0;
    if (ch->dist_mult != 0.0f)
    {
        float dbLoss = (dist / 1200.0f) * snd_foliage_db_loss;        // 1200.0 at 0x20188098
        relative     = pow(10.0, dbLoss / 20.0) * dist * ch->dist_mult;
        gain = (relative > 0.1) ? gain / relative     // 0.1 at 0x201736e0
                                : gain * 10.0;        // i.e. gain / max(relative, 0.1)

        if (gain > 0.5)                               // 0.5 at 0x20173680
        {
            int   L = DIST_MULT_TO_SNDLVL(ch);
            float e = (L > 90) ? 2.5f - (L - 90) * 1.7f / 50.0f : 2.5f;
            //   90.0 @0x20188090/0x20188088, 1.7 @0x20188084, 50.0 @0x201734ec, 2.5 @0x20188080
            gain = snd_gain_max * ( 1.0 - 1.0 / ( pow(gain, e) * 2.0 * pow(2.0, e) ) );
        }
    }
    if (gain < snd_gain_min)
    {
        gain = snd_gain_min * (2.0 - snd_gain_min * relative);        // 2.0 at 0x20173630
        if (gain <= 0.0) gain = 0.001f;                               // 0x3a83126f
    }
    ...
}
```

In plain terms, and this is the whole model the port has to reproduce:

- **inside `D_ref`** the raw gain would exceed 1; it is capped at `10 ×` (`relative` floored at 0.1)
  and then soft-compressed above `0.5` towards `snd_gain_max = 1`;
- **beyond `D_ref`** the gain is `1 / (d / D_ref)` — a pure inverse-distance law, that is
  **6 dB per doubling of distance**;
- **the floor** is `snd_gain_min = 0.01`, that is **−40 dB**, below which a short knee takes it to
  0.001 and it is inaudible;
- `snd_foliage_db_loss = 4` adds 4 dB of extra loss per 1200 units of range, on every sound.

Setting `gain = snd_gain_min` gives the **audible range** of a soundlevel:

```c
D_audible(L) = D_ref(L) / snd_gain_min = 100 * 36 * 10^((L-60)/20)      (ignoring foliage loss)
```

| soundlevel | `D_ref` (gain 1) | −20 dB (gain 0.1) | `D_audible` (gain 0.01) |
|---:|---:|---:|---:|
| 40 | 3.6 | 36 | 360 |
| 57 | 25.5 | 255 | 2 552 |
| 58 | 28.6 | 286 | 2 863 |
| 63 | 50.9 | 509 | 5 090 |
| 64 | 57.1 | 571 | 5 712 |
| 75 (player) | 202.4 | 2 024 | 20 244 |

### 3.4 What the authored distance actually means

`20·log10(d/36) + 40` (§1.4) is **not** the engine's own `refdb`-60 relation — it uses **40**. Put
the two together:

```c
d_authored * dist_mult(L)  =  d_authored / (36 * 10^((L-60)/20))
                           =  10^((L-40)/20) / 10^((L-60)/20)  =  10
```

**The authored `NormalFootfallDist` / `HeavyFootfallDist` is exactly the distance at which the
step has fallen 20 dB — to one tenth of its reference gain.** That is the number the port should
treat as "the audible reach the designer meant", and it is a clean, checkable identity: a 300-unit
walk step is level 58, reference distance 28.6 units, and 300 units is 10× that.

### 3.5 soundlevel versus attenuation

Both exist and they are **not** interchangeable:

| quantity | who consumes it | effect |
|---|---|---|
| **soundlevel** (dB int) | passed to `EmitSound` / `StartSound` and turned into `ch->dist_mult` by the engine | the actual falloff the player hears |
| **attenuation** (`20/(L−50)`, `SNDLVL_TO_ATTN`) | only `CPASAttenuationFilter` (`0x1019d4b0`) | a **server-side recipient cull** at `2000 / attenuation` units — and the loop is skipped entirely when `maxClients == 1`, so in VtMB it is inert |

So `ElysiumSoundLevel::MakeAttenuation` must be built from the **soundlevel**, not from the
attenuation number, and the retail attenuation is worth computing only to keep parity with the
recorded values in tests.

### 3.6 What the port needs

`FSoundAttenuationSettings` with inner radius `D_ref(L)` (converted from Source units by the
project's existing unit scale), an inverse falloff of 6 dB per doubling, and a falloff distance
where the gain reaches `snd_gain_min` (0.01). If a single falloff distance is wanted instead of the
full curve, `D_ref(L) * 10` (the −20 dB point, which is the authored distance) is the
designer-visible number and `D_ref(L) * 100` is where retail actually goes silent.

---

## 4. Unrecovered

- The **look-ahead**: the server dispatcher fires events up to 0.1 s early
  (`animation_events.md`), so an NPC footfall is up to 0.1 s ahead of the pose. Not modelled here.
- `CGameMovement::ShouldPlayStepSound` (vfunc `+0x3c`, called from `PlayStepSound` when
  `force == false`) is **not read**. It can suppress an unforced step; every constant in §2.2 is
  upstream of it.
- `mv+0xa8` / `mv+0xb4` in `CheckFalling` are identified by shape (the `Jumping` feat's `Vertical`
  scalar and `JumpDuration`) and by the `0.75` that matches `JumpGravityMultiplier`, but the
  **write site in the move setup was not located**. The derived `safeSpeed ≈ 412.75` at feat 1 is
  therefore a computed value, not a read one.
- `mv+0xc0` (values 0, 4, 7, 8, 9, 0xb) and player `+0x1db4` (8, 10, 11) are the same player
  animation-state enum — read by `CBasePlayer::SetAnimation` (`0x10164870`) and `PostThink` — but
  the enum is **not named**. §2.5 depends on `8` meaning "soft landing" and `10`/`11` meaning
  "hard landing"; that reading comes from which `sound_volume_table` row each selects.
- `physprops` vfunc `+0x1c` (fatal-fall arm) and `+0x30` (anim state `0xb` arm) are not identified.
- `0x101282b0` (the float `CheckFalling` adds to `curtime` for `mv+0xbc`) is not identified; it is
  an animation timer, not audio.
- The four trailing `EmitSound` arguments on the NPC path (`0, 0, 1, 0`) are assumed to be
  Source's `pOrigin, pDirection, bUpdatePositions, soundtime`; not proven.
- The `CNPC_VTzimisce` spiderchick pool (`character/monster/spiderchick/spi_footstep_indiv_{1..6}`,
  `CNPC_VTzimisce::vfunc104` `0x103b8fa0`) is a third species footstep path that was not traced to
  its trigger.
- `mp_footsteps` (`0x10117e30`) exists and is never read on any path recovered here.
- The setter of `m_fNoPlayerSound` (`player+0x22a0`) and the `notarget` path that raises
  `FL_NOTARGET` are not traced; the port carries both as `FElysiumPlayer::bNoPlayerSound`, false.
- The origin `UpdatePlayerSound` stamps on the reserved record comes from vfunc `+0x370`, which is
  not identified (the NPC step's PAS filter uses `+0x378`); the port stamps the entity origin.
- Which `TC_Runner/foot_steps_*` pair the `+0x9ac` flag selects: the two pointer tables at
  `0x1065d680` / `0x1065d688` are read only by the vfunc and the runner's precache, and their
  contents are not exposed; the port takes flag 1 (2050) as `foot_steps_1/2`.

### 4.1 Recovered, deliberately not ported (the port's own out-of-scope list)

These are not gaps in the recovery. Each is a retail behaviour this document states in full and the
port does not build, with the reason and the thing that would close it.

- **The species footstep classes.** `CNPC_VMingXiao`, `CNPC_VHengeyokai`, `CNPC_VTzimisceHeadClaw`,
  `CNPC_VTzimisceRunner`, `CNPC_VSabbatLeader` and `CNPC_VWerewolf` (§1.7) are recovered arm by arm
  and **none of those NPC classes exists in the port**, so there is nothing for the override to hang
  on. What landed is the seam: a footstep policy resolved per NPC (`EElysiumFootstepPolicy`:
  `Default` / `Silent` / `Shake` / `CustomWav`) carrying each species' recovered wav pool and, for
  `Shake`, its amplitude, frequency, duration and radius. One row IS in force — the port registers
  `npc_VTzimisceRunner`, whose `CustomWav` arm plays `TC_Runner/foot_steps_*` plus a `Breath*` and
  leaves 2052/2053 to the shared chain, exactly as `103c32d4`'s `JMP` does. The pools play; the
  **screenshake is reported and not performed** — `UTIL_ScreenShake` has
  no port yet and is worth building once, for the werewolf's `werewolf_footstep_shakes` group as
  well as for these — so a `Shake` row says out loud, once, that the camera did not move. The Sabbat
  leader's `FootstepSound` is task-driven rather than event-driven and is a seam on the schedule
  runner, not on the animation-event path. Closing this is the species classes' own lane.
- **Terminal, keypad and hacking session muting.** The third arm of `0x1026d460`'s player gate reads
  the dialogue/control entity at player `+0x19b4` with its timer `+0x1ec4 > 0`, and `0x1017cef0`
  sets that field for the **monitor, keypad and hacking sessions as well as dialogue** (§1.2). The
  port composes the gate from `HasScriptedCamera` / `HasTrackCamera` / an open conversation, which
  is two of the three handles and the dialogue half of the third; a terminal session leaves NPC
  footfalls audible where retail silences them. Named divergence, closed when 13.4 exposes the
  interaction-session state to the substrate.

---

## 5. Wave 2 spec — every constant by name

Nothing below requires reopening the corpus.

### 5.1 NPC

| name | value | where |
|---|---|---|
| `EVENT_NPC_WALK_LEFT` / `_RIGHT` | 2050 / 2051 | `0x10274e30` |
| `EVENT_NPC_RUN_LEFT` / `_RIGHT` | 2052 / 2053 | `0x10274e30` |
| `kNpcModeNormal` / `kNpcModeHeavy` | 0 / 1 | `0x1026d460` arg |
| `footstep_npc_use_templates` | **1** | cvar `0x109203f8` |
| `footstep_normal_vol` | **0.5** | cvar `0x10920440` |
| `footstep_normal_dist` | **256** | cvar `0x10920280` |
| `footstep_heavy_vol` | **0.85** | cvar `0x10920160` |
| `footstep_heavy_dist` | **512** | cvar `0x109204a8` |
| template keys | `NormalFootfallVol` `+0x24`, `NormalFootfallDist` `+0x28`, `HeavyFootfallVol` `+0x2c`, `HeavyFootfallDist` `+0x30` | loader `0x101d3f10` |
| template loader defaults | 0.45 / 256 / 0.85 / 512 | `0x101d3f10` |
| shipped authored values | 0.45 / 300 / 0.85 / 600 | `npctemplate*.txt` |
| `kSoundLevelRefDist` | **36.0** (`1/36 = 0.0277777778`) | `0x1047aa18` |
| `kSoundLevelDbPerDecade` | **20.0** | `0x104704a8` |
| `kSoundLevelBias` | **40.0** | `0x10462950` |
| zero-distance rule | dist ≤ 0 → level 0 | `0x104454c4` = 0.0 |
| `kAttnPivot` | level > **50** | `0x1026d5e1` |
| `kAttnNumerator` | **20** (integer divide) | `0x1026d5ee` |
| `kAttnBelowPivot` | **4.0** | `0x10449148` |
| `kNpcChannel` | `CHAN_BODY` = **4** | `0x1026d691` |
| `kNpcPitch` | **100** | `0x1026d688` |
| coin flip | `RandomInt(0, 1)`; 1 → `stepleft` `+0x2c`, 0 → `stepright` `+0x2e` | `0x1026d626` |
| surface field | `m_pSurfaceData` at NPC `+0x5b90`; null → silent | `0x1026d597` |
| PAS cull radius | `2000.0 / attenuation`, **skipped when `maxClients == 1`** | `0x104492ac`, `0x1019d4b0` |
| AI stimulus | **none** | — |
| Ming Xiao | silent on 2050/2051 | `0x10392a70` |
| Hengeyokai (shark) | shake `2.0 / 0.2 / 0.2 s / 1024` plus `hengeyokai/stomp_{1..4}` vol 1.0 pitch 100 | `0x1037fb60`, `0x103817f0` |
| Tzimisce head-claw | shake `1.3 / 0.2 / 0.2 s / 1024` plus `TC_Runner/foot_steps_*` | `0x103c1540`, `0x103c4160` |
| Tzimisce runner | **no shake**, `TC_Runner/foot_steps_{1,2}` or `{3,4}` plus `TC_Runner/Breath{1..4}`, vol 1.0, pitch 100 | `0x103c32c0`, `0x103c4160` |
| Sabbat leader | `character/monster/andrei_transfo*`, `RandomInt(0,6)`, vol 0.8–1.0, task-driven | `0x103aa5e0` |

### 5.2 Player clock

| name | value | where |
|---|---|---|
| `kClockUnits` | milliseconds; `-= frametime * 1000`, **clamped at 0** | `0x1011f520`, `0x10447ee0` = 1000.0 |
| ducked band `velWalk` / `velRun` / `flduck` | **60.0 / 80.0 / 100.0** | `0x1011ea48`, `0x1011ea50`, `0x1011ea58` |
| normal band `velWalk` / `velRun` / `flduck` | **120.0 / 220.0 / 0.0** | `0x1011ea30`, `0x1011ea38`, `0x1011ea40` |
| ducked band condition | `FL_DUCKING(2)` **or** ladder **or** `waterLevel != 0` | `0x1011ea16` |
| `bWalking` | `speed3D < velRun` | `0x1011eae6` |
| `kIntervalDryWalk` / `Run` | **400 / 300 ms** | `0x1011ebe9` (`(walking ? 100 : 0) + 300`) |
| `kIntervalSlow` | **800 ms** when 2-D speed ≤ **100.0** | `0x44480000`, `0x10450564` |
| `kIntervalLadder` | **350 ms**, volume **0.35**, surfaceprop `"ladder"` | `0x43af0000`, `0x3eb33333`, `0x10572554` |
| `kIntervalWater` | **400 / 300 ms**, volume **1.0**, surfaceprop `"water"` | `0x1011ebc2`, `0x10572544` |
| `kIntervalWade` | **600 ms**, volume **1.0**, surfaceprop `"wade"` | `0x44160000`, `0x1057254c` |
| wade silent phase | global counter `0x1070b898`: `0 → silent and set 1`; `3 → 0`; else `+1` → **1 silent in 4**, clock not re-armed on the silent pass | `0x1011eb42` |
| the added term | `m_flStepSoundTime += flduck` (**0 or 100**) — *not* `velwalk` | `0x1011ec9c` (`FLD [ESP+0x1c]`) |
| `kVolDirtWalk` / `Run` (`'D'`) | **0.25 / 0.55** | `0x10449260` / `0x10462928` |
| `kVolVentWalk` / `Run` (`'V'`) | **0.40 / 0.70** | `0x1045a3f0` / `0x104492d0` |
| `kVolDefaultWalk` / `Run` | **0.20 / 0.50** | `0x10449198` / `0x10449270` |
| `kVolDuckScale` | **0.35** | `0x10462918` |
| `footstep_pc_vol` | **0.5** | cvar `0x1070b2d0` |
| `kVolClamp` | **1.0** | `0x10449280` |
| `sv_footsteps` | **1**; gates `UpdateStepSound` unconditionally | cvar `0x109ef090` |
| early returns | clock > 0; flags & `0x60`; movetype == 9; `sv_footsteps == 0`; no ground and no water and no ladder; 2-D speed ≤ 0 | §2.2 |
| dead arms | the `velwalk` gate (the clock is always 0); the button/`velrun` test at the tail | §2.2 |
| game material source | `m_pSurfaceData->game.material` at `surfacedata_t+0x70`, cached in `CGameMovement+0xa4` | `0x1011e560` |

### 5.3 Player emit

| name | value | where |
|---|---|---|
| `kPlayerChannel` | `CHAN_BODY` = **4** | `0x1011e50f` |
| `kPlayerSoundLevel` | **75** | `0x1011e50b` |
| `kPitchBase` / `kPitchJitter` | **95** + `RandomInt(0, 10)` → 95…105 | `0x1011e502`, `0x1011e4f0` |
| foot alternation | `m_nStepside` (player `+0x1ee4`): set → `stepleft` `+0x2c`, clear → `stepright` `+0x2e`, toggled every emit | `0x1011e4a4` |
| silent surface | `sample == 0` → no sound | `0x1011e4c7` |

### 5.4 Landing

| name | value | where |
|---|---|---|
| `sv_gravity` / `sv_jump_boost` / `fall_threshold` | **800 / 25.0 / 200** | cvars `0x109ef2d0`, `0x109ef000`, `0x1070b708` |
| `rules.BaseJumpVelocity` | **185** (`rules.txt` `Jumping`) | `rules+0x3c8`, `0x101e7d90` |
| `rules.SafeFallDist` / `SupernaturalFallDist` | **240 / 500** | `rules+0x3e0` / `+0x3e8` |
| `kSafeFallSpeed` | `sqrt(2·800·240)` = **619.68** | `rules+0x3ec`, `0x101e7db0` |
| `kSupernaturalFallSpeed` | `sqrt(2·800·500)` = **894.43** (read and discarded) | `rules+0x3f0`, `0x101e7e40` |
| `kMaxSafeFallSpeed` | `max( sqrt(2·25·800) + 185·M·(1 + 0.75·H), 200 )` ≈ **412.75** at feat 1 | `0x10125e04`, `0x10462958` = 0.75 |
| `kFloatingFallReduction` | **173.0** subtracted from `m_flFallVelocity` when the ground entity `IsFloating()` | `0x104629e4`, `0x1000b186` |
| `kLandVolSoft` | **0.25** when `0 < fallVel < kMaxSafeFallSpeed` | `0x3e800000` |
| `kLandVolHard` | **0.85** when `fallVel > kSafeFallSpeed * 0.5` | `0x3f59999a`, `0x104454d0` = 0.5 |
| `kLandVolMid` | **0.5** when `fallVel >= 200.0` | `0x3f000000`, `0x104492b8` = 200.0 |
| `kLandVolLow` | **0.65** otherwise (only reachable after the floating reduction) | `0x3f266666` |
| `kLandVolFatal` | **1.0** when `fallVel > kSafeFallSpeed` | `0x3f800000` |
| water landing | `clamp(fallVel / kSafeFallSpeed, 0, 1)` | `0x10125f25` |
| the step | `m_flStepSoundTime = 0` → `UpdateStepSound()` → `PlayStepSound(surface, vol, force = true)` | `0x1012617a` |
| view punch (not footsteps) | `m_vecPunchangle.ROLL = fallVel * 0.013`; `PITCH` clamped to 8.0 | `0x104629d8`, `0x1045597c` |
| `m_flFallVelocity` cleared | on any frame with a ground entity | `0x10126226` |

### 5.5 Hearing

| name | value | where |
|---|---|---|
| call site | `CBasePlayer::PostThink` → `UpdatePlayerSound`, **once per think** | `0x1016be10` / `0x1016b480` |
| `PLAYER_RUN_SPEED` | **128.0** (`MiscData`; loader default `-1.0`) | table `+0xfc` = `0x1072bd1c` |
| priority order | `IN_JUMP` → `PLAYER_JUMP`; on-ground and anim 8 → `PLAYER_LAND_SOFT`; anim 10/11 → `PLAYER_LAND_HARD`; else speed > 0 → duck ? `SNEAK` : (speed > 128 ? `RUN` : `WALK`); else silent; **in air and not jumping → silent** | §2.5 |
| radii | `SNEAK` 180, `WALK` 240, `RUN` 240, `JUMP` 240, `LAND_SOFT` 180, `LAND_HARD` 240 | `sound_volume_table.txt` |
| occlusion | all six occludable (levels 1 and 2) | `sound_volume_table.txt` |
| sound type | `SOUND_PLAYER` = **4**, one permanently reserved slot, refreshed not inserted | `0x1016b642` |
| rise / decay | instant up; down at **250 units/s** | `0x104704b8` |
| kill switches | `FL_NOTARGET (0x8000)` → 0 and return; `m_fNoPlayerSound` (`+0x22a0`) → 0 | `0x1016b4b8`, `0x1016b610` |
| water | not consulted; wading emits, swimming does not (no ground contact) | §2.5 |
| NPC footstep stimulus | **none exists** | §1.6 |

### 5.6 Engine

| name | value | where (`engine.dll`) |
|---|---|---|
| `snd_refdist` | **36** | cvar `0x21307378` |
| `snd_refdb` | **60** | cvar `0x21315bd8` |
| `snd_gain` | **1** | cvar `0x213109d8` |
| `snd_gain_max` | **1** | cvar `0x21310658` |
| `snd_gain_min` | **0.01** (−40 dB) | cvar `0x21310470` |
| `snd_foliage_db_loss` | **4** dB per 1200 units | cvar `0x213108b0`, `0x20188098` |
| `SNDLVL_TO_DIST_MULT(L)` | `10^((snd_refdb − L)/20) / snd_refdist` | inverse of `0x20119fe0` |
| `DIST_MULT_TO_SNDLVL` | `(int)(20·log10(10^(refdb/20) / (refdist·dist_mult)))` | `0x20119fe0` |
| reference distance | `D_ref(L) = 36 · 10^((L − 60)/20)` | derived |
| falloff | `gain = 1 / max(dist · dist_mult, 0.1)` → **inverse distance, 6 dB per doubling**, capped at 10× inside `D_ref` | `0x2011a119`…`0x2011a185` |
| compression | above raw gain **0.5**: `snd_gain_max · (1 − 1/(2·(2·gain)^e))`, `e = L > 90 ? 2.5 − (L−90)·1.7/50 : 2.5` | `0x2011a18d`, `0x2011a259`…`0x2011a30c` |
| floor knee | below `snd_gain_min`: `gain_min · (2 − gain_min · relative)`, then `≤ 0 → 0.001` | `0x2011a310`…`0x2011a3a0` |
| audible range | `D_audible(L) = 100 · D_ref(L)` | derived |
| `SNDLVL_TO_ATTN` | `L > 50 ? 20/(L−50) : 4.0` — **recipient cull only**, inert in single-player | `0x1026d5e1`, `0x1019d4b0` |
| worked levels | 300 u → 58 → attn 2; 600 u → 64 → attn 1; 256 u → 57 → 2; 512 u → 63 → 1; player step = **75** | §1.4, §3.3 |
| identity to test | `authored dist == D_ref(level) · 10` — the authored distance is the **−20 dB** point | §3.4 |
