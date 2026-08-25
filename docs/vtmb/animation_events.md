# Animation events — the record, the two dispatchers, and the code table

A studio sequence carries a timed callback list. This document owns the record, the dispatch rules
and the complete code vocabulary. The *mechanics of the sequence clock* the dispatcher reads belong
to `docs/vtmb/animation_and_movers.md`.

## `mstudioevent_t` — 76 bytes

| off | field | type | note |
|---:|---|---|---|
| 0 | `cycle` | float | **normalized `[0,1]`**, not a frame number |
| 4 | `event` | int | the dispatch id |
| 8 | `type` | int | **`0` on all 1,872 shipped records, and read by nobody** |
| 12 | `options[64]` | char | fixed array, **not guaranteed NUL-terminated by the loader** |

Stride confirmed three ways: `ADD EDX,0x4c` in the server loop (`0x10091b72`), `ADD EDI,0x4c` in the
client loop (`0x10093580`), and `pfVar3 + 0x13` (19 dwords) in the shared iterator (`0x104289a0`).

**The table hangs off the *sequence* descriptor, not the animation** — `numevents`@20 /
`eventindex`@24, the index descriptor-relative. So a nine-cell blend fan is one descriptor carrying
nine animations and **one** timeline.

There is **no `szeventindex`**; Source 2013's 80-byte record with an event-name index postdates the
fork.

That the time field is a normalized cycle is settled three ways: the dispatch comparison is against
`m_flCycle + 0.1f * cycleRate`, a normalized quantity; the non-looping finish branch writes literal
`1.0f` into the same slot; and the shipped run-clip footsteps sit at exactly `3/9`, `5/9`, `7/9`
and `8/9`.

## `animevent_t` — the runtime copy, 20 bytes

`{ int event; const char *options; float cycle; float eventtime; CBaseEntity *pSource; }`

`options` points at a **64-byte stack copy**, not into the model. `eventtime` is
`(cycle − m_flCycle) / flCycleRate + m_flAnimTime` — the true absolute wall time. **There is no
`type` member**: the mdl field is dropped at dispatch.

## The server dispatcher — and its 0.1 s look-ahead

`CBaseAnimating::DispatchAnimEvents` (`0x10091880`):

```c
flCycleRate = GetSequenceCycleRate(m_nSequence) * m_flPlaybackRate;
flStart     = m_flLastEventCheck;                 // +0x658
flEnd       = m_flCycle + 0.1f * flCycleRate;     // <-- 0.1f at 0x104491b4
m_bSequenceFinished = false;
if (!m_bSequenceLoops) {
    if (flEnd >= 1.0 || flEnd < 0.0) { m_bSequenceFinished = true; flEnd = 1.0f; }
    else m_fSequencePastHalf = (flEnd > 0.5f);
} else {
    if (flEnd >= 1.0 || flEnd < 0.0) m_bSequenceFinished = true;
    else m_fSequencePastHalf = (flEnd > 0.5f);
    if (flStart >= 1.0) flStart -= 1.0;
    if (flStart <  0.0) flStart += 1.0;
}
m_flLastEventCheck = flEnd;                       // <-- stores the LOOK-AHEAD, not the cycle
for each event e:
    if (e.event >= 5000) continue;
    fire = (flStart <= e.cycle && e.cycle < flEnd)
        || ((seqdesc.flags & 1) && flEnd >= 1.0 && e.cycle < flEnd - 1.0);
    if (fire) eventHandler->HandleAnimEvent(&ev);      // vfunc +0x40c
if (m_bSequenceFinished && !wasFinished) OnSequenceFinished();
```

**The window's upper bound is ahead of the pose, not at it.** Retail fires every sub-5000 event up
to **0.1 s before** the pose reaches it. `eventtime` compensates, but no shipped handler reads it —
so muzzle flashes, footsteps, melee contact and feeding grapples all happen early, by design or by
accident.

The look-ahead is also what makes the loop wrap seamless: the frame *before* the visual wrap already
has `flEnd > 1.0`, so the base test sweeps the tail up to 1.0 and the second clause sweeps
`[0, flEnd − 1.0)`, the head of the next lap. **The wrap is covered exactly once, one frame early,
with no gap and no duplicate** — provided the clip sets `STUDIO_LOOPING`. A clip that loops in
practice without the flag loses the wrapped interval.

Other properties worth stating:

- **An event can never fire twice.** Windows are half-open at the top and chain end to end.
- **A whole skipped lap under-fires.** The cycle is wrapped upstream, so `flEnd` stays below 1.1 and
  an event the clip passed twice fires once. Systematic under-fire, never over-fire.
- **A backwards seek on a non-looping clip silently drops the skipped span** — `flStart > flEnd`
  leaves the window empty.
- **Cycle exactly 1.0 never fires on a non-looping sequence.** The finish branch sets `flEnd = 1.0f`
  and the test is strict. This is live in shipped data: `2050`/`2051` are authored at 1.0000.
- `ResetSequenceInfo` (`0x10090950`) zeroes `m_flLastEventCheck`, so a sequence change restarts the
  window at cycle 0 and a record at cycle 0.0 fires on the first advance (the lower bound is closed).
- `m_fSequencePastHalf`@`+0x568` is a Troika side effect written here against a 0.5 threshold, and
  it has a **second writer in `StudioFrameAdvance`** using the non-look-ahead cycle, so the two can
  disagree within one tick.

## Overlay layers dispatch their own timelines; autolayers never do

`CBaseAnimatingOverlay::DispatchAnimEvents` (`0x10098c80`) runs the base sequence, then all four
layers through a per-layer body at `0x10098cd0` — same 76-byte scan, same `< 5000` gate, same
window rule, but reading the **layer's own** `m_nSequence`, `m_flCycle`, `m_flPlaybackRate` and its
**own `m_flLastEventCheck` at `layer + 0x2c`**. It does not clamp to 1.0 and does not call
`OnSequenceFinished`. There is **no weight gate** — a layer at weight 0 still dispatches.

**87 of 300 classes** take that path (`CBaseAnimatingOverlay`, `CBaseFlex`, `CBasePlayer`, the
`CAI_BaseNPC` family, `CCineNPC`/`CCineAI`, `CPayphone`, …); the other 213 — props, ragdolls, gibs,
view models, all 169 weapons — take the base-only `0x10091880`.

**Autolayers are not `CAnimationLayer` entries and nothing reads an autolayer's `numevents`.** An
autolayered sequence's timeline is unreachable unless that same sequence is also the base or is
separately pushed as an overlay. The shipped data corroborates it: the `*_attack_layer` and
`*_reload_layer` labels carry `3031`, `5003` and `6002` — exactly the sequences pushed as overlays.

## Client versus server

VtMB is single-player and still keeps the split, with a hard numeric partition at **5000**.

| | server (`vampire.dll`) | client (`client.dll`) |
|---|---|---|
| scanner | `0x10091880` + per-layer `0x10098cd0` | **`0x10093370` only** |
| gate | `event < 5000` | `event > 4999` |
| window | `[m_flLastEventCheck, m_flCycle + 0.1·rate)` | `(m_flPrevEventCycle, m_flCycle]` |
| look-ahead | **yes, 0.1 s** | **none** |
| wrap handling | yes | **none** |
| reset on sequence change | `m_flLastEventCheck = 0` | `m_flPrevEventCycle = −0.01f` |
| overlay layers | 4 scanned | **base only** |
| entry point | `HandleAnimEvent(&animevent_t)` slot 259 | `FireEvent(origin, angles, event, options)` slot 127 |

Note the window bounds are mirror images: the server's is closed at the bottom and open at the top,
the client's open at the bottom and closed at the top.

**The client wrap is a genuine retail defect.** With no `+1.0` adjustment and
`m_flPrevEventCycle = m_flCycle` written unconditionally, a looping clip loses **every** `>= 5000`
event in the wrapped interval, once per lap.

Client `FireEvent` has three bodies over 237 `C_BaseAnimating` descendants: `0x100935a0` (the bulk,
229 classes), `C_BaseCombatCharacter::FireEvent` `0x10099c00` (7 — prefers the active weapon's
attachment for `5003/5013/5023/5033`, owns `5120`), and `C_BaseViewModel::FireEvent` `0x100ab530`
(1 — owns `6001..6004`/`6011..6014`, then offers the event to the owning weapon's `OnFireEvent`).
The view-model path and the weapon path handle the same ids and are mirror images, gated on
first-person versus world-model — which is why `v_*.mdl` clips carry `5001`/`6001` and the
third-person character banks carry `5003`/`6002`.

## The code table

The name↔code table is **static code, not data**: `CBaseAnimating::GetEventName` (`0x1008c170`), a
`strcpy`-per-case switch used only by `DisplayAnimEvent`. It exists **only in `vampire.dll`** — the
client has no name table — and its default is `"**UNKNOWN**"`.

**Name** is the literal from `GetEventName`; `—` means the code is dispatched but unnamed.
**Shipped** counts occurrences across the full 4,445-model install (58 distinct ids, 1,872 records).

### Band markers (named, never authored)

`0 SPECIFIC` · `1000 SCRIPTED` · `2000 SHARED` · `3000 WEAPON` · `3999 WEAPON_LAST` · `5000 CLIENT`

### 1 … 9 — per-NPC private events

Class-private, unnamed. Owners: `CGenericNPC`, `CNPC_VManBat` (wing sound, scream, exhale),
`CNPC_VGargoyle` (roar), `CNPC_VTzimisce` (six vfunc dispatches at `+0x7a8`, `+0x9a8`, `+0x9b4`,
`+0x9b8`, `+0x9bc`, `+0x9c0`, `+0x9c4`, `+0x9c8`), `CNPC_Crow`. Shipped: 10, 7, 3, 2, 0, 5, 3, 2, 3.
Unmatched codes fall through to `CAI_BaseNPCTroika::HandleAnimEvent`.

### 1000 … 1022 — the scripted band, stock Source

| code | name | behaviour | shipped |
|---:|---|---|---:|
| 1000 | `SCRIPT_EVENT_DEAD` | in state 4, `m_lifeState = 1; m_iHealth = 0` | 0 |
| 1001 / 1002 | `SCRIPT_EVENT_NOINTERRUPT` / `CANINTERRUPT` | `m_hCine->AllowInterrupt(false/true)` | 0 |
| **1003** | `SCRIPT_EVENT_FIREEVENT` | `atoi(options)` → `OnAnimEventN` on `m_hCine`, else the hint node, else the interesting place | **37** (`1`×30, `2`×5, `3`×2) |
| 1004 | `SCRIPT_EVENT_SOUND` | play `options` on CHAN_BODY | 0 |
| 1005 | `SCRIPT_EVENT_SENTENCE` | sentence by name | 1 |
| 1006 … 1010 | `INAIR`, `ENDANIMATION`, `SOUND_VOICE`, `SENTENCE_RND1`, `NOT_DEAD` | | 0 |
| 1020 … 1022 | `SCRIPT_EVENT_BODYGROUP{ON,OFF,TEMP}` | `DevMsg("Bodygroup!\n")` — stub | 0 |

The sound-precache walk recognises exactly `1004` and `1008`.

### 2001 … 2109 — the NPC band

| code | name | behaviour | shipped |
|---:|---|---|---:|
| 2001 / 2002 | `NPC_BODYDROP_LIGHT` / `HEAVY` | body-drop sound when grounded | 0 |
| 2005 | — | Troika: `EmitSound(options)`, channel 0x42 | 0 |
| **2006** | — | Troika: build a damage packet against self, target the nearest player — **a scripted self-kill**. Clips: `choking`, `jump_to_death`, `vision_of_death` | 6 |
| 2010 | `NPC_SWISHSOUND` | | 0 |
| 2020 / **2021** | `NPC_180TURN` / `NPC_FINISHTURN` | clear a memory bit and set the pose yaw / commit the turn | 0 / 5 |
| 2022 | `NPC_FORCE_ADD_YAW` | `atoi(options)` degrees onto the motor's ideal yaw | 4 |
| 2040 | `NPC_PICKUP` | pick up a weapon by name; overridden by Troika, Tzimisce, MingXiao, Hengeyokai | 7 |
| 2041 … 2044 | — | drop weapon; set the weapon's sequence by name / index; set its activity | 0 |
| **2050** | `NPC_LEFTFOOT` | **walk** footstep, left | **228** |
| **2051** | `NPC_RIGHTFOOT` | **walk** footstep, right | **227** |
| **2052** | — | **run** footstep, left | **77** |
| **2053** | — | **run** footstep, right | **77** |
| 2060 / 2061 | `EVENT_EXPRESSION` / — | facial expression envelope; see `docs/vtmb/facial_animation.md` | 2 / 0 |
| 2070 / 2071 | `EVENT_PHYSICSCHAIN_OFF` / `ON` | `TurnOn/OffPhysicsChain(LookupPhysicsChain(options))` — a **bone chain**, and the shipped options are bone names | 18 / 18 |
| 2100 / 2101 | — | MingXiao screenshake, small (amp 5, freq 0.6, 0.2 s, r 512) and big (amp 15, freq 1.0, 1.5 s, r 1024); Werewolf bone-position impact | 17 / 65 |
| 2102 … 2109 | — | swallowed by `CNPC_VWerewolf` | 0 |

**`2050`/`2051` versus `2052`/`2053` is the walk/run split, not left/right-only.** The handler takes
a mode parameter selecting a different volume/attenuation pair and a different cvar quartet, then
coin-flips the surface property's two step sounds. Shipped corroboration is exact: all 37 clips
carrying `2052`/`2053` are `*_run`, at cycles `1/3`, `5/9` and `7/9`, `8/9`. `2052`/`2053` are
**unnamed** in `GetEventName` — a Troika extension.

**`CBasePlayer::HandleAnimEvent` swallows 2050 … 2053 outright.** Player footsteps do not come from
animation events.

### 3000 … 3999 — the weapon band

Routed to `Operator_HandleAnimEvent` (slot `+0x5c8`). `CBaseCombatCharacter` routes `3000..3999`;
**`CAI_BaseNPC` routes a wider `3000..4002`**, which is exactly why `4001`/`4002` are weapon
bodygroup events. `CBasePlayer` routes **nothing** — it returns immediately when
`pEvent->pSource != this`.

Ranged fire is the whole range `3030..3044`; melee contact is `{3001} ∪ {3030..3037} ∪
{3039..3044} ∪ {3047}` — note the **3038 and 3045/3046 holes**.

| code | name | shipped |
|---:|---|---:|
| 3001 | `WEAPON_MELEE_HIT` | 4 |
| 3002 | `WEAPON_SMG1` | 13 (all `v_*.mdl` `attack1` at cycle 0 — dormant) |
| 3003 | `WEAPON_MELEE_SWISH` | 5 |
| 3004 … 3013 | `WEAPON_SHOTGUN_FIRE`, `THROW`, `AR1`, `AR2`, `HMG1`, `SMG2`, `MISSILE_FIRE`, `SNIPER_RIFLE_FIRE`, `AR2_GRENADE`, `THROW2` | 3005 → 5, rest 0 |
| 3014 | `WEAPON_PISTOL_FIRE` | 0 |
| 3030 … 3044 | the per-weapon fire set: `PISTOL_SMITH`, **`PISTOL_ANACONDA`**, `PISTOL_GLOCK`, `PISTOL_P220S`, `SMG_UZI`, `SMG_MAC10`, `RIFLE_REMINGTON`, `RIFLE_STEY_AUG`, *(3038 gap)*, `RIFLE_ITHACA`, `RIFLE_STRYKER`, `RIFLE_DRAGONSB`, `RIFLE_PANIC`, `RIFLE_CROSSBOW`, `PISTOL_DUALPISTOL` | **3031 → 105**, rest 0 |
| 3045 / 3046 | — | Tzimisce claw variants | 1 / 1 |
| 3101 / 3102 | `WEAPON_SMG1_BURST1` / `BURSTN` | 0 |
| 3200 / 3201 | `WEAPON_MELEE_BEGIN_SWING` / `END_SWING` | 1 / 0 |

### 4001 … 4155 — the Troika band

| code | name | behaviour | shipped |
|---:|---|---|---:|
| 4001 / 4002 | `WEAPON_HIDE_BODYGROUP` / `SHOW_BODYGROUP` | `SetBodygroup(atoi(options), 1/0)` | 0 |
| 4005 | `EVENT_SWITCH_TO_ACTIVITY` | `SetSequence(SelectWeightedSequence(atoi(options)))` + `ResetSequenceInfo` | 0 |
| **4006** | — | vfunc `+0x584` on the interaction partner — **feed/grapple release** | **272** |
| **4007** | — | vfunc `+0x57c` — **feed/grapple bite (engage)** | **104** |
| 4020 | — | named effect | 2 |
| **4050** / **4051** | — | create the `camera_cinematic` action object and switch to it / release it and place it — **the stealth-kill pair** | 40 / 40 |
| 4100 / 4101 / 4102 | — | attach `"%s.mdl"` from options as the follow model / detach it / attach the **gendered** `"%s_%s.mdl"` | 28 / 5 / 36 |
| 4150 … 4155 | — | `Interesting_places/<gender>/<opt>.wav` on channel 4 or 2, plus their stop verbs and a second slot each | 10, 2, 2, 0, 2, 0 |

### 5001 … 5120 — the client band

| code | name | behaviour | shipped |
|---:|---|---|---:|
| **5001** | `CL_EVENT_MUZZLEFLASH0` | attachment 1, first person; `options` is the flash style | **48** (`v_*.mdl` only) |
| 5002 | `CL_EVENT_SPARK0` | `DevWarning("Renable model spark effects in client .dll!!!")` — **disabled in retail** | 0 |
| **5003** | `CL_EVENT_NPC_MUZZLEFLASH0` | attachment 1, third person | **101** (character banks only) |
| 5004 / 5005 | `CL_EVENT_SOUND` / — | `EmitSound(options)` / `EmitSound("Disciplines/%s")`, channel 6 | 0 / 1 |
| 5011 … 5033 | the `MUZZLEFLASH1..3` and `NPC_MUZZLEFLASH1..3` pairs, attachments 2–4 | | 0 |
| 5101 / 5102 | `CL_EVENT_HIDE_BODYGROUP` / `SHOW_BODYGROUP` | | 2 / 2 |
| 5103 | — | effect teardown | 0 |
| **5105** | — | activity name → weighted sequence + playback reset | **45** |
| 5111 … 5114 | — | attach particle `options` to attachment 1–4, type 6, follow | 0, 1, 0, 0 |
| 5115 | — | attachment named **`"eyes"`**, falling back to the one named **`"3"`** | 4 |
| 5116 | — | attachment named **`"mouth"`**, falling back to **`"2"`** | 30 |
| 5117 | — | at the entity's own origin/angles, index −1, type 1 | 5 |
| **5118** | — | split `options` on **`";"`**, `LookupBone(left)`, attach `right` to that bone, type 2 | **59** |
| 5119 | — | attachment named **`"crotch"`** | 0 |
| 5120 | — | attach to the **weapon's** `"slampoint"` attachment | 12 |

### 6001 … 6014 — shell and clip ejection

`GetEventName` names only `6001` (`CL_EVENT_EJECTBRASS1`).

| code | behaviour | shipped |
|---:|---|---:|
| 6001 … 6004 | **brass eject** at attachment `event − 6000`. `sscanf(options, "%d %d", &type, &count)`, **both defaulting to 1**; emit `count` shells of `type`. Warns `"weapon does not have attachment for shell ejection!"` and falls back to origin/angles | 6001 ×26, 6002 ×24 |
| 6011 … 6014 | **clip eject** at attachment `event − 6010`. `type = atoi(options)`, count fixed at 1 | 6013 ×10 |

The argument order is settled from the assembly: the first int is the type, the second the loop
bound. Shipped proof: `anaconda_reload` carries `6002` with `"0 6"` — a revolver ejecting **six**
casings of type 0 — while every other `6002` carries a bare type and defaults to one.

## How `options` is parsed

**There is no central parser.** The dispatcher copies the fixed 64 bytes verbatim onto the stack and
hands a `const char*` to the handler; every handler parses its own.

| form | codes |
|---|---|
| `atoi` | 1003, 2022, 2043, 4001, 4002, 4005, 5101, 5102, 6011–6014 |
| `sscanf("%s %f %f %f")` | 2060 |
| `sscanf("%d %d")` | 6001–6004 |
| `strstr(";")` + split | **5118 only** |
| raw sound/sentence name | 1004, 1005, 1008, 2005, 4020, 4150–4155, 5004, 5005 |
| raw model path | 4100, 4101, 4102 |
| raw bone name | 2070, 2071 |
| raw activity/sequence name | 2042, 2044, 5105 |
| raw entity classname | 4050 |
| raw particle/emitter name | 5111–5120 |
| ignored | everything else |

Failure reporting is per-handler: `"Invalid options for animation event on %s!!"` when `options` is
empty for the emitter family, `"Could not attach particle %s to point %d on %s"` on a failed
attachment lookup, `DevWarning("Unhandled animation event %d for %s")` at the base handler, and
`"Bad sound event %d in sequence %s :: %s"` from the precache walk when the 64-byte field carries no
terminator.

## Troika additions beyond stock Source

Stock accounts for 1000–1010, 1020–1022, 2001, 2002, 2010, 2020, 2040–2044, 2050, 2051, 3000–3014,
3101/3102, 3200/3201, 3999, 5000–5004, the `MUZZLEFLASH` set, 5101/5102 and 6001.

Troika-added, all unnamed in `GetEventName` except the two `EVENT_PHYSICSCHAIN_*`: **1–9**,
**2005**, **2006**, **2021**, **2052/2053**, **2060/2061**, **2070/2071**, **2100/2101**,
**2102–2109**, **3030–3047**, **4005**, **4006/4007**, **4020**, **4050/4051**, **4100–4102**,
**4150–4155**, **5005**, **5103**, **5105**, **5111–5120**, **6002–6004**, **6011–6014**.

## The project's decoder

`pipeline/src/elysium_pipeline/formats/mdl_skel.py` reads `numevents`@20 / `eventindex`@24, the
76-byte descriptor-relative record and all four fields including `type`, and rejects a non-positive
relative index, a count outside `(0, 256]`, an out-of-image array, or an options field with no NUL.
That matches the binary exactly. The sidecar is keyed by **sequence label**, correctly, because the
timeline lives on the descriptor.

The one thing it does not carry beside the events is the sequence's `flags & 1` (`STUDIO_LOOPING`),
which the wrap rule needs — and which is already read in the same walk.

## Against this repo's runtime

`Source/ElysiumUE/Private/Substrate/ElysiumAnimEvents.cpp` implements `[Lo, Hi)` with
`Lo = LastCycle` and `Hi = Cycle` — the **catch-up** reading, with no look-ahead. Its wrap is
`[Last, 1)` then `[0, Cycle)`, which covers the same interval as retail's but **shifted 0.1 s
later**. Its server-band ceiling matches retail's 5000. The comment calling it "the recovered
comparison verbatim" is true of the shape and false on the upper operand.

## Provenance

Read from `vampire.dll` and `client.dll` via the RE corpus under `ELYSIUM_WORK_ROOT`. Shipped counts
are over the full 4,445-model install. Ground-truth cross-check for the vocabulary is the
`event_fields` / `event_options` / `events` tables in
`$ELYSIUM_EXPORT_ROOT/npc/blends/*.json`.
