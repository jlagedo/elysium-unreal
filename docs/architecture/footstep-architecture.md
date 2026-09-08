# Footstep architecture — proposal (2026-09-06)

Status: **proposal, not built.** The retail recovery below is complete enough to build against; the
port-side design is what this document asks the owner to approve. Programme home: AUD2 in
`docs/project/plans/audio.md` (footsteps, surfaces, the AI-hearing producer). Consumers: 13.5 combat
AI senses (`plans/gameplay.md`).

**That status line is superseded by §8.** The three decisions in §7 were approved and both waves
landed on 2026-09-06; §1–§7 are kept as the design of record, and §8 is what was built, by file,
with every named divergence.

## 1. Why a subsystem and not a handler

Footsteps are the same fact produced twice and consumed three times. Retail produces a step from
an NPC's **animation event** (2050–2053) and from the player's **movement clock**
(`UpdateStepSound`); it consumes the step as a rendered surface sound, as an AI-hearing stimulus
(player only) and, per species, as a screenshake or a custom wav. Every one of those reads the
**surface under the body**, which neither producer in the port publishes today. Building 2050 as a
handler alone would leave the player silent on dry ground, duplicate the water step clock that
already exists on `AElysiumMapActor`, and hard-wire a surface trace into one call site. The
subsystem is the four seams below, each owned once and shared by both producers.

## 2. Retail, recovered

Addresses are `vampire.dll` unless stated. Constants were read out of the shipped DLL image.

### 2.1 NPC steps — `CAI_BaseNPC::HandleAnimEvent` `0x10274e30` → `0x1026d460`

Events `0x802`/`0x803` (2050/2051) call `FUN_1026d460(this, 0)`; `0x804`/`0x805` (2052/2053) call it
with mode 1. The mode is **normal versus heavy** footfall, not left versus right; the clips use it as
walk versus run (all 37 `2052/2053` clips are `*_run`). Only `CAI_BaseNPC` reaches this function; it
has exactly one caller.

`0x1026d460`, in order:

1. **Player gate.** `UTIL_GetLocalPlayer`; return if the player has a camera view entity
   (`+0x19c0 m_hCameraViewEntity`), a camera target entity (`+0x19cc m_hCameraTargetEntity`), or an
   active dialogue entity (`+0x19b4` with its timer `+0x1ec4 > 0`, set by `0x1017cef0` from the
   dialogue/monitor/keypad/hacking sessions). NPC footsteps are silent during cinematic cameras and
   conversations.
2. **Volume and distance.** If cvar `footstep_npc_use_templates` (default **1**) is non-zero, read
   the character template (`GetCharTemplate`, loader `0x101d3f10`): normal mode reads
   `NormalFootfallVol` `+0x24` / `NormalFootfallDist` `+0x28`; heavy reads `HeavyFootfallVol`
   `+0x2c` / `HeavyFootfallDist` `+0x30`. Loader defaults 0.45 / 256 / 0.85 / 512; the shipped
   `npctemplate*.txt` author 0.45 / 300 / 0.85 / 600 and inherit through `ParentTemplateName`.
   Otherwise read the cvar pair `footstep_normal_vol`/`_dist` (0.5 / 256) or
   `footstep_heavy_vol`/`_dist` (0.85 / 512).
3. **Ground surface.** Requires the physprops interface and the NPC's cached `surfacedata_t*` at
   `+0x5b90`. That cache is written once per move step by `CAI_Navigator::MoveEnact` `0x102ef870`
   from the move trace, and cleared by `0x10273390` / `0x1027bf50` (spawn, reset). Null → return,
   no sound.
4. **Distance → sound level.** `0x10228350`: `level = int(20·log10(dist / 36) + 40)` for `dist > 0`,
   else 0. Attenuation for the PAS filter: `level > 50 ? 20 / (level − 50)` (**integer** division)
   `: 4.0`. Shipped: 300 u → 58 dB → 2; 600 u → 64 dB → 1; 256 → 57 → 2; 512 → 63 → 1.
5. **Which wav.** `RandomInt(0,1)`: non-zero → `stepleft` (`+0x2c`), zero → `stepright` (`+0x2e`).
   A **coin flip**; the event's foot is ignored. Each key is a variation pool on the surface
   (`docs/vtmb/surface_properties.md`).
6. **Emit.** `IEngineSound::EmitSound(filter, entindex, CHAN_BODY=4, wav, volume, level, 0,
   PITCH_NORM=100)` through a `CPASAttenuationFilter` at the NPC origin.

No AI-hearing sound is inserted for an NPC step.

Species overrides exist on the same virtual and are **not** this proposal's scope: `CNPC_VMingXiao`
`0x10392a70`, `CNPC_VTzimisceRunner` `0x103c32c0`, `CNPC_VHengeyokai` `0x1037fb60` (case 0x802
arms), the werewolf `werewolf_footstep_sounds` / `_shakes` cvars, and
`CNPC_VSabbatLeader::FootstepSound` `0x103aa5e0` driven by its own schedule tasks.

### 2.2 Player steps — `CBasePlayer::UpdateStepSound` `0x1011e940` → `CGameMovement::PlayStepSound` `0x1011e430`

`CBasePlayer::HandleAnimEvent` **swallows** 2050–2053. The player's steps are a movement clock:

- Gate: `m_flStepSoundTime > 0` counts down; `FL_FROZEN|FL_ATCONTROLS` (`0x60`) returns; move type
  noclip (9) returns; `sv_footsteps` zero returns.
- Speed bands: ducked, on a ladder, or in water → `velwalk 60 / velrun 80`; else `120 / 220`.
  Walking = `speed < velrun`. Off-ladder and airborne with no water → return.
- Interval (ms): dry `walking ? 400 : 300`, and **800** when 2D speed `< 100`; ladder 350;
  water level 1 → 300/400 (`Surfaces/Water` pool, volume 1.0); level ≥ 2 → 600 with the
  one-in-four silent phase (`0x1070b898`, `Wade` pool). Then `+= velwalk` as decompiled — verify
  in the listing whether this is Source's `flduck` add (100 when ducked) before porting.
- Dry volume by the surface's `gamematerial` letter: `D` 0.25 walk / 0.55 run; `V` 0.40 / 0.70;
  default 0.20 / 0.50. Ducked × 0.35. × cvar `footstep_pc_vol` (default 0.5). Clamp > 1 → 1.
- `PlayStepSound` `0x1011e430`: alternates feet through `player+0x1ee4` (`stepright` when the flag
  is clear, `stepleft` when set, then toggles), emits `CHAN_BODY`, volume as above, sound level
  **75** (`0x4b`), pitch `95 + RandomInt(0,10)`. Predicate `0x1011e3c0` only matters in multiplayer
  (2D speed < 195 hides the step).
- The `PLAYER_FOOTSTEP_SNEAK/WALK/RUN` hearing categories (`sound_volume_table.txt`, loader
  `0x101afd01`) are consumed elsewhere; the exact emission site is **still unrecovered** — the
  lane that builds §4.5 must find it (candidates: the caller of `UpdateStepSound` in
  `CBasePlayer::PostThink`, or the stealth/noise pass).

## 3. What the port has today

| retail input | port today | gap |
|---|---|---|
| surface under the body (`+0x5b90`, `m_pSurfaceData`) | `UElysiumPhysicalMaterial` on world hits, used only by decals; `CategorizePosition` sweep does not request it; NPC floor via `UCharacterMovementComponent` | **no published ground surface on either producer** |
| `stepleft`/`stepright` pools | `UElysiumPhysicalMaterial::FootstepsLeft/Right` (engine tier) | no engine-neutral table the substrate can resolve against |
| 2050–2053 records | delivered by `FElysiumAnimating::AdvanceAnimEvents`; locomotion cells publish a phase (`RefreshLocomotionArm`) keyed by the **sequence label** | unclaimed → census |
| step clock | `AElysiumMapActor::UpdatePlayerWaterFootsteps` (water half only, presentation side) | dry, ladder branches missing; clock lives on the wrong side of the seam |
| template keys | `FElysiumClanTemplate::General` keeps every key raw, parent resolution exists | nothing reads the four footfall keys |
| cvars | `FElysiumMoveTuning` mirrors `sv_*` | no footstep cvars |
| sound level / attenuation | `FElysiumPlayParams::AttenuationRadiusCm` (NaturalSound, −60 dB at max) | no sound-level model, no channel replacement |
| hearing | `FElysiumGameSoundBus` with the categories loaded | no footstep producer (seam named in `ElysiumPlayerEntity.cpp`) |
| gate | `FElysiumEntityWorld::HasScriptedCamera/HasTrackCamera`, open conversation state | not composed into one predicate |

## 4. Design

Four seams, one rule module, two producers. The substrate/services split follows
`runtime-architecture.md` §7: decisions (which wav, how loud, whether to step) stay in plain C++;
the engine answers geometry (what is under the foot) and performs playback.

### 4.1 Ground-surface sense — a query, published on the locomotion sample

Add to `FElysiumLocomotionSample` (`ElysiumLocomotionSample.h`), the record both producers already
fill after their move (`animation-architecture.md` §3.2):

```cpp
FName GroundSurface;   // the surfaceprop name under the foot ("concrete"), NAME_None when nothing
                       // standable was hit — retail's null surfacedata_t
```

- **Player:** `UElysiumMovementComponent::CategorizePosition` sets `bReturnPhysicalMaterial` on its
  sweep and reads `UElysiumPhysicalMaterial::SourceName` off the hit. Stands for
  `CGameMovement::CategorizePosition` writing `m_pSurfaceData`.
- **NPC:** `AElysiumNpcBody::SampleLocomotion` reads `CurrentFloor.HitResult.PhysMaterial`; if the
  engine's floor query does not return a material, the body owns one down-trace per move step with
  `bReturnPhysicalMaterial`. Stands for `CAI_Navigator::MoveEnact` `0x102ef870` writing `+0x5b90`.
  Cleared to `NAME_None` with the motor's reset, as `0x10273390` does.
- **Headless answer:** `NAME_None`. A step with no surface is silent, which is retail's own
  `+0x5b90 == 0` return. It is counted once per body in the anim-event census rather than warned,
  because a recording double has no floor.

### 4.2 Surface sound table — substrate data

`FElysiumSurfaceSoundTable` beside the rulebook (`ElysiumRulebook.h` family), loaded from the same
exported surface-property unit the physical materials are baked from (`seam_map_surface_property.md`):

```cpp
struct FElysiumSurfaceSounds { TArray<FString> StepLeft, StepRight; TCHAR GameMaterial; FString Impact, Scrape; };
const FElysiumSurfaceSounds* Find(FName Surface) const;
```

Pools keep their duplicates (variation, never collapsed). Impacts and scrapes ride along for AUD2's
later surface work. This is what lets `Elysium.Substrate.*` resolve a step without a `UObject`.

### 4.3 The rule module — `Substrate/ElysiumFootsteps.{h,cpp}`

Pure functions over value types, one per retail function:

```cpp
namespace ElysiumFootsteps
{
  struct FFootfallSource { float Volume; float DistanceUnits; };            // template or cvar pair
  FFootfallSource NpcSource(const FElysiumClanTemplate* T, bool bHeavy, const FElysiumFootstepTuning&);
  int32 SoundLevelDb(float DistanceUnits);        // 0x10228350
  float AttenuationFor(int32 SoundLevelDb);       // 20/(lvl-50) integer, else 4.0
  bool  NpcStepsMuted(const FElysiumEntityWorld&);// camera view/target entity, open dialogue
  const FString* PickNpcWav(const FElysiumSurfaceSounds&, FRandomStream&);   // coin flip
  // player
  struct FStepClockIn  { float Speed2D, Speed3D; bool bDucked, bLadder, bOnGround; EElysiumWaterLevel Water; TCHAR GameMaterial; float PcVol; };
  struct FStepClockOut { float NextIntervalSeconds; float Volume; EStepPool Pool; bool bSilentPhase; };
  bool  AdvanceStepClock(float& StepSoundSeconds, float Dt, const FStepClockIn&, FStepClockOut&); // 0x1011e940
  FName HearingCategory(float Speed2D, bool bSneaking);   // PLAYER_FOOTSTEP_SNEAK/WALK/RUN, PLAYER_RUN_SPEED 128
}
```

`FElysiumFootstepTuning` is the console surface, shaped like `FElysiumMoveTuning`:
`footstep_npc_use_templates=1`, `footstep_normal_vol=0.5`, `footstep_normal_dist=256`,
`footstep_heavy_vol=0.85`, `footstep_heavy_dist=512`, `footstep_pc_vol=0.5`, `sv_footsteps=1`.

A new RNG stream `EElysiumRngStream::Footsteps` owns the coin flip, the player pitch jitter and the
wade silent phase, so a Substrate test can pin the draw.

### 4.4 Producer A — the NPC animation event

`FElysiumNpc::HandleAnimEvent` override (the NPC leaf has none yet; this is also where the rest of
the `CAI_BaseNPC` band lands later):

```
2050|2051 → NpcStep(bHeavy=false)   2052|2053 → NpcStep(bHeavy=true)   else → Super
```

`NpcStep`: gate → source → surface (`GroundSurface` from the motor's last sample) → level → wav →
`Audio->PlayBodySound(...)` (§4.6). Every arm answers claimed; the failure paths (no surface, no
pool) are silent as in retail and counted once.

`FElysiumPlayer::HandleAnimEvent` claims 2050–2053 and **drops** them, as `CBasePlayer` does, so the
census stops listing records the player's clips carry.

### 4.5 Producer B — the player step clock

Move the clock from `AElysiumMapActor::UpdatePlayerWaterFootsteps` into the substrate:
`FElysiumPlayer::TickStepClock` on the post-move pass, fed by the locomotion sample (which now
carries water level, duck, ground and `GroundSurface`). The water branch becomes one arm of the
retail switch beside dry, ladder and wade; `ElysiumWaterAudio::StepIntervalSeconds/IsSoundingStep`
move into `ElysiumFootsteps` unchanged. The map actor keeps water-level classification (a query) and
loses the clock. Feet alternate per `+0x1ee4`; volume, level 75 and pitch 95–105 per §2.2.

The same tick raises the hearing stimulus: `GameSounds().Emit({Category = HearingCategory(...),
RadiusCm from sound_volume_table})` — the AUD2 producer the seam in `ElysiumPlayerEntity.cpp` names.
Its retail emission site is the one open recovery item; until it is found the stimulus fires with
the rendered step and the doc says so.

### 4.6 Emission seam — `IElysiumAudio::PlayBodySound`

One new execution call on the existing audio service:

```cpp
struct FElysiumBodySound { FString Rel; float Volume; int32 SoundLevelDb; float Pitch; EElysiumSoundChannel Channel; };
virtual FElysiumAudioVoiceHandle PlayBodySound(const FElysiumEntityHandle& Owner, const FElysiumBodySound&) = 0;
```

The map actor implements it over `PlayVoice`, attached to the owner's body component, with two
policies the request layer owns:

- **Sound level → radius.** Named modernization. Source's attenuation is `20/(lvl−50)` and its
  audible reach is roughly `1024 u / attenuation`; the port maps `SoundLevelDb → AttenuationRadiusCm`
  through one function (`ElysiumSoundLevel::RadiusCm`) so every later `EmitSound` port (impacts,
  4020, weapons) shares it. Calibrated once by ear against retail; recorded here when done.
- **Channel replacement.** `CHAN_BODY` holds one voice per entity: a new step stops the previous
  step on the same body. The ledger keys the voice by `(Owner, Channel)`.

Headless: the recording services record the request; no playback.

### 4.7 Gate composition

`NpcStepsMuted` reads `FElysiumEntityWorld`: `HasScriptedCamera() || HasTrackCamera()` stand for the
two camera handles, and the open conversation stands for the dialogue entity. Named divergence:
retail also mutes during monitor/keypad/hacking sessions (`0x1017cef0` callers); the port composes
those from the interaction session state when 13.4's terminal session exposes it, and says so in the
predicate's comment.

## 5. Tests

Substrate tier, recording services, no RHI:

- `Elysium.Substrate.Footsteps.NpcNormal` — 2050 on `walk` with `GroundSurface=concrete`, template
  0.45/300 → one body sound from concrete's step pools, volume 0.45, level 58, attenuation 2,
  `CHAN_BODY`, pitch 100.
- `...NpcHeavy` — 2052 → 0.85, level 64, attenuation 1.
- `...NpcCvarPath` — `footstep_npc_use_templates 0` → 0.5/256 → level 57.
- `...NpcMuted` — open conversation, scripted camera, track camera → no request.
- `...NpcNoSurface` — `NAME_None` → no request, counted once.
- `...CoinFlip` — pinned stream draws left then right; pools with duplicates keep their size.
- `...PlayerClock` — 300/400/800 intervals, ladder 350, water 300/400, wade 600 with the silent
  phase, feet alternate, `D`/`V`/default volumes × duck × pc_vol, clamp, level 75, pitch in 95–105.
- `...PlayerHearing` — walk vs run split at 128 u/s, sneak category when sneaking, radius from the
  table.
- `...PlayerSwallows` — 2050 on the player body is claimed and emits nothing.
- `Elysium.Content.FootstepRecords` — every `*_run` clip in the shared male bank carries 2052/2053
  at 1/3, 5/9 and 7/9, 8/9; walk clips carry 2050/2051.
- `Elysium.Content.SurfaceSoundTable` — 17 surfaces with step pools, pool sizes match the export.
- The player water footstep tests keep passing after the clock moves.

## 6. Delivery lanes

1. **L1 Sense + table.** §4.1, §4.2, the RNG stream, the tuning type. Both producers publish
   `GroundSurface`; tests for the sense and the table. No audible change.
2. **L2 NPC steps.** §4.3 NPC half, §4.4, §4.6, §4.7. The tutorial's walking NPCs step on the right
   surface. Census loses 2050–2053 for NPCs.
3. **L3 Player steps.** §4.3 player half, §4.5 including the water-clock move and the hearing
   producer, the player swallow. Closes the `ElysiumPlayerEntity.cpp` seam.
4. **L4 later.** Species overrides (§2.1 list), the 0.1 s event look-ahead (G3 in
   `plans/animation.md`) which shifts NPC steps relative to foot contact, terminal-session muting.

Each lane ends with its tests, its retail band in `docs/vtmb/animation_events.md`, and the
recovery in a new `docs/vtmb/footsteps.md` (this section 2, plus the L3 emission-site finding).

## 7. Decisions asked of the owner

- Approve moving the player water step clock off `AElysiumMapActor` into the substrate (L3). It is
  the only refactor of existing behaviour in the proposal.
- Approve the `SoundLevelDb → radius` modernization as the shared model for every `EmitSound` port,
  calibrated once rather than per producer.
- Confirm species footstep overrides stay out of scope until their NPC classes are ported.

---

## 8. Status (2026-09-06)

The three decisions in §7 were approved and the subsystem was built in two waves. This section is
what landed, by file, and every divergence from retail that the build named. Retail itself is
`docs/vtmb/footsteps.md`; nothing here restates it.

### 8.1 Wave 1 — the sense, the table, the seams

| §  | what | files |
|---|---|---|
| 4.1 | `FElysiumLocomotionSample::GroundSurface`, and the two rules that fill it | `Public/ElysiumGroundSurface.h`, `Private/ElysiumGroundSurface.cpp`, `Public/ElysiumLocomotionSample.h` |
| 4.1 | the player's half — `CategorizePosition`'s hit through `AtFloorHit` | `Private/Player/ElysiumMovementComponent.cpp`, `Public/ElysiumMovementComponent.h` |
| 4.1 | the NPC's half — `RefreshGroundSurface` per move step, cleared at spawn only (`InitializeAtFeet`, retail's `NPCInit 0x10273390` / `OnRestore 0x1027bf50`); the cache survives an arrival and a teleport as retail's does | `Private/Visual/ElysiumNpcBody.{h,cpp}` |
| 4.2 | `FElysiumSurfaceSounds` and the `ResolveSurfaceSounds` query over the 63 baked `PM_*` | `Public/ElysiumSurfaceSounds.h`, `Private/Audio/ElysiumSurfaceSoundTable.{h,cpp}` |
| 4.3 | the console surface, `GeneralFloat`, the RNG stream | `Public/ElysiumFootstepTuning.h`, `Private/Audio/ElysiumFootstepTuning.cpp`, `Private/Player/ElysiumCommandBus.cpp`, `Private/Substrate/ElysiumRulebook.{h,cpp}`, `Public/ElysiumRng.h`, `Private/Session/ElysiumRng.cpp` |
| 4.6 | the sound-level model and `PlayBodySound` | `Public/ElysiumSoundLevel.h`, `Private/Audio/ElysiumSoundLevel.cpp`, `Public/ElysiumWorldServices.h`, `Private/Map/ElysiumMapActorEmbodiment.cpp`, `Private/Tests/ElysiumTestServices.h` |

### 8.2 Wave 2 — the two producers

| §  | what | files |
|---|---|---|
| 4.3 | the rule module: every retail function as a pure function over value types | `Private/Substrate/ElysiumFootsteps.{h,cpp}` |
| 4.4 | `FElysiumNpc::HandleAnimEvent` → `NpcStep`, the species-policy seam | `Private/Substrate/ElysiumNpc.{h,cpp}` |
| 4.5 | `FElysiumPlayer::TickStepClock`, the landing step, the hearing producer, the 2050–2053 swallow | `Private/Substrate/ElysiumPlayerEntity.cpp`, `Public/ElysiumPlayer.h`, `Private/Substrate/ElysiumEntityWorld.cpp`, `Private/Substrate/ElysiumGameSound.{h,cpp}` |
| 4.5 | the water clock moved off the map actor; it keeps water-level classification only | `Private/Audio/ElysiumWaterAudio.{h,cpp}`, `Private/Map/ElysiumMapActor.cpp` |

Tests: `Elysium.Substrate.Footsteps.{SoundLevel, Tuning, TemplateFootfall, BodySoundReplacement,
GroundSurface, GroundSurfaceWorld, NpcNormal, NpcHeavy, NpcCvarPath, NpcMuted, NpcNoSurface,
NpcNoPool, CoinFlip, SpeciesPolicy, PlayerClock, PlayerVolumes, PlayerLanding, PlayerWater,
PlayerHearing, PlayerSwallows, PlayerServerFootstepsOff}` and `Elysium.Content.{SurfaceSoundTable,
FootstepRecords, FootstepTemplates, FootstepTuningDefaults, FootstepSurfacesForSteps,
FootstepEventCensus}`.

Wave 3 review, against `vtmb_asm 1026d460` / `1011e940` / `1011e430` / `10125db0` and
`vtmb_code 101274a0` / `1016b480`: the landing frame now runs the ordinary pass over the
pre-landing premise, as `PlayerMove`'s top-of-move call does (two steps on a landing, never three);
the NPC surface cache is cleared at spawn only, as `NPCInit` / `OnRestore` clear `+0x5b90`; the
stealth subtraction was confirmed listener-side on the owner's distance (`0x102b35b0`,
`0x1030f7b0`) and stays producer-side at the same number; `FL_NOTARGET` / `m_fNoPlayerSound` are a
seam (`FElysiumPlayer::bNoPlayerSound`); the species vfuncs were confirmed to emit
`(CHAN_BODY, vol 1.0, ATTN 0.8, pitch 100)`, which is level 75.

### 8.3 The named divergences

Eight, each stated where it lives in code as well as here. Four came out of the wave-1 lanes that
found them; the fifth is the plan's own, carried into the producer that implements the gate; the
last three are the two producers'.

**A1, the ground sense.**

1. **The render-surface probe.** In Source the collision brush and the drawn brush are one object,
   so `CategorizePosition`'s single trace answers both "is there a floor" and "what is it made of".
   The port's converted maps split them: the solid body is the material-less `.hulls` convex
   collider, and the `$surfaceprop` rides the drawn geometry on `ElysiumPickOnly`. The sense
   therefore asks twice — the floor hit's own material first, then a short probe on the
   render-surface channel — and answers `default` (Source's surfaceprop index 0) for a floor with no
   drawn half. Stated in `Public/ElysiumGroundSurface.h`.
**A2, the sound-level model.**

2. **`snd_foliage_db_loss` is not applied.** `SND_GetGain` folds 4 dB of loss per 1200 units into
   `relative` on every sound; stock Source gates the same term behind a foliage trace and this port
   has no such trace. Applying it would shorten a level-58 step's reach from 2860 to about 1570
   units. The recovered reach table in `footsteps.md` §3.3 is itself stated "ignoring foliage loss",
   so the model and the recovery agree. Stated on the constant in `Public/ElysiumSoundLevel.h`.
3. **The near field inside `D_ref` is flat.** `FSoundAttenuationSettings` holds the curve's first
   key across the inner radius, so a listener inside the reference distance hears the edge value
   (0.912 at level 58) where retail's compressor rises to 0.99972. Bounded by construction — it is
   under one dB, over a radius of 28 units at level 58 — and everything from `D_ref` outward,
   including the −20 dB authored distance and the −40 dB floor, is `SND_GetGain` sampled exactly.

**A3, the recovery.**

4. **The dry arm's final test is reproduced as "always plays".** Retail's tail reads `[ESP+0x18]`
   for `velrun`, but the dry arm overwrote that slot with the integer interval so it could `FILD`
   it; read back as a float it is a denormal, so the button test and the speed test are both no-ops
   and the dry step always plays (`footsteps.md` §2.2). The port reproduces the observable behaviour
   and not the clobber. Stated at the arm in `Private/Substrate/ElysiumFootsteps.cpp`.

**The gate (§4.7, carried from the plan).**

5. **The NPC gate is two of retail's three handles plus half the third.** `NpcStepsMuted` reads the
   scripted camera, the track camera and an open conversation; the monitor, keypad and hacking
   sessions set the *same* dialogue/control field through `0x1017cef0` and have no state in the
   substrate yet, so NPC footfalls stay audible during a terminal session where retail silences
   them. Recorded in `footsteps.md` §4.1 and at the predicate; closed when 13.4 exposes the
   interaction-session state.

**B1, the NPC producer.**

6. **The variation pick inside a step pool is the port's own draw.** Retail's `surfacedata_t` carries
   one soundscript index per foot and the *engine* picks among that script's repeats; the bake has
   already flattened the script into `FElysiumSurfaceSounds::StepLeft/StepRight` with its duplicates
   preserved, so `ElysiumFootsteps::PickNpcWav` draws twice from `EElysiumRngStream::Footsteps` —
   the side, then the entry — where retail's game-DLL stream draws once. The audible result is the
   same variation set at the same weights; what differs is the stream position, which a save
   carries. Stated on the function in `Private/Substrate/ElysiumFootsteps.h`.
7. **The NPC's stat template is resolved once, not per footfall.** `1026d4a0` re-resolves the
   template on every step; `FElysiumClanTable::Resolve` merges seven maps down a parent chain, and a
   walking body steps two or three times a second, so `FElysiumNpc::ApplyResolvedTemplate` latches
   the record it has already resolved. A divergence in cost rather than in answer — nothing rewrites
   a template under a live NPC — and the cvar half of the source is still read at the step, so
   `footstep_npc_use_templates` still switches source mid-run. Stated on the member in
   `Private/Substrate/ElysiumNpc.h`.

**B2, the player producer.**

8. **The hearing stimulus is refreshed at the think's 0.1 s cadence, not per frame.** Retail
   rewrites the reserved `CSound` from `CBasePlayer::PostThink` every frame; the port rewrites it
   from `FElysiumPlayer::Think`, whose deadline is the stealth surface's 0.1 s heartbeat, on the
   pre-move pass. Judged observably equivalent and left there: the listeners poll at their own AI
   cadence, the radius rises instantly and falls 25 units per think at retail's 250 u/s, and the
   two landing categories are latched by the per-frame step clock so a landing between two thinks
   is not lost. Stated in `footsteps.md` §2.8, decision 6.

### 8.4 What is out of scope, and where it is written down

`docs/vtmb/footsteps.md` §4 (unrecovered) and §4.1 (recovered, deliberately not ported): the species
footstep classes and their policy seam, the terminal-session muting above, the 0.1 s event
look-ahead that shifts every NPC footfall ahead of the pose (G3 in `plans/animation.md`),
`CGameMovement::ShouldPlayStepSound` (`+0x3c`), and the unnamed player animation-state enum whose
values 8 / 10 / 11 the hearing producer's landing categories depend on.
