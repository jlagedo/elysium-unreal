# VtMB stealth observer and detection

This document owns VtMB's ordinary player-stealth transaction: the `stealth.txt` rulebook, the
player's light-derived target surface, NPC visual and auditory admission, the
`trigger_stealth_mod` contribution, and the distinction between current sight, memory, enemy
selection, outputs, and HUD observability. Stealth-kill eligibility is a separate consumer of
`stealthkillrules.txt`; feat construction is documented in [skills-and-checks.md](skills-and-checks.md),
and the general sense/memory/enemy transaction is documented in
[npc-ai-reverse-engineering.md](npc-ai-reverse-engineering.md).

## Evidence and confidence

The native addresses below are pinned to retail `vampire.dll` SHA-256
`c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f`. The four table schemas and
stock values were checked against the same installation's packed retail `Stealth.txt`; the normal
project export is patch-first and contains the Unofficial Patch's selected values. The current
map-export census supplies authored `trigger_stealth_mod` uses. The reproducible static case is
`research/cases/stealth-detection/`.

The transaction, guards, constants, field writes, table indexing, output producers, and call order
are confirmed static facts. No controlled retail incident was captured for this pass. Rendered
light values, frame-exact output delivery, partial-occlusion behavior, and save/load while inside a
modifier volume remain live-acceptance questions. Unreal owns light sampling, traces, and audio
mixing; the substrate owns the recovered game-event sound radii and the rules around those world
answers.

## The rulebook

`StealthData` loads four named tables from `vdata/system/stealth.txt` at startup
(`0x1034fee0`):

| Table | Shape | Consumer |
|---|---:|---|
| `StealthVisionScalarTable` | `Light0..10 × Stealth0..10` | multiplier on an observer's effective visual range |
| `StealthVisionConeScalarTable` | `Light0..10 × Stealth0..10` | multiplier applied in the observer's view-cone test |
| `StealthHearingDistTable` | `Stealth0..10` | distance removed from an eligible player sound radius |
| `StealthLightRangeTable` | `Light0..10` | descending normalized-light thresholds used to choose a row |

The two matrices use row-major `light * 11 + Sneaking`; hearing uses `Sneaking` alone. Missing
sections or rows produce developer diagnostics, while individual reads retain the table's existing
default. An implementation consumes the selected exported sidecar rather than hard-coding the
retail matrix.

The retail light thresholds are `0.99, 0.95, 0.87, 0.75, 0.60, 0.42, 0.28, 0.17, 0.08, 0.03,
0.00`. Retail hearing reduction is linear: `0, 8, 16, …, 80` units for `Stealth0..10`. The patch
does not change those tables or the cone matrix. It changes the high-Sneaking tail of the vision
matrix and labels that section `changed by wesp`; for example, `Light10/Stealth10` is `0.00` in
retail and `0.14` patch-first. This is a data-stack difference, not a different algorithm.

## Player target-surface update

The player think at `0x10350830` runs the stealth-factor virtual at `0x103517e0` when `curtime`
reaches `m_flNextStealthUpdate`, then advances the deadline by **0.1 seconds**. The recompute is
ordered:

1. If the world-light service is unavailable, return without manufacturing replacement values.
2. Resolve the Sneaking feat through the ordinary CharacterData path. Category 1 includes
   `CBaseCombatCharacter::GetStealthModifier`; the result is capped at 10 before table lookup.
3. Evaluate the native eligibility predicate. Its concrete test is a player-state flag bit plus a
   second state helper; their stable human-readable names remain unresolved. Failure does not skip
   the update: it installs the non-stealth fallback described below.
4. If the active usable weapon is `item_w_torch`, force normalized light to `1.0`. Otherwise sample
   one of three vertical body points—feet, centre, head—and advance the saved sample index. Point
   construction uses the recovered `0.875/0.125` weights.
5. Compute raw body light as `(feet + centre + head) * 0.083325`, clamp it to the configured world
   minimum/maximum, subtract the minimum, and divide by the remaining range. The developer
   `debug_stealth_light` override replaces this normalized value with `value * 0.1` when enabled.
6. Choose the light row. A value at or above `1.0` is `Light0`; below that, advance while the value
   is less than or equal to the current descending threshold. Equality therefore enters the next
   darker row.
7. Write sight-range scalar, cone scalar, hearing reduction, and debug feat/light indices from the
   tables.

Only one body point is refreshed per 0.1-second pass, so a complete feet/centre/head refresh takes
about **0.3 seconds**. The other two saved samples participate unchanged. This lag is part of the
retail transaction; querying three points every observer frame changes both cost and behavior.

When the eligibility predicate is false, the routine writes `m_flLightOnMe = -4.0`, sight scalar
`1.0`, hearing `Stealth0` (`0` in retail), and cone `Light0/Stealth0` (`1.0` in retail). Thus the
tables shape detection only while the player is in the eligible stealth state; ordinary movement
does not retain the last dark-room advantage.

The relevant player fields are:

| Offset | Native field | Meaning |
|---:|---|---|
| `+0x1c6c` | `m_flNextStealthUpdate` | next 0.1-second recompute deadline |
| `+0x1c70` | `m_flStealthVisionScalar` | target-side visual-range multiplier |
| `+0x1c74` | `m_flStealthVisionCone` | target-side cone multiplier |
| `+0x1c78` | `m_flStealthHearingDist` | target-side sound-radius reduction |
| `+0x1c7c` | `m_nNextLightPositionTest` | feet/centre/head round-robin state |
| `+0x1c80..0x1c88` | `m_flLightOnFeet/Center/Head` | retained samples |
| `+0x1c8c` | `m_flLightOnMe` | normalized aggregate or inactive sentinel |

The custom player save path explicitly serializes the next update, sight scalar, cone scalar, and
hearing reduction; save evidence also names the three samples, aggregate, and next-sample index.
Restore retains or deliberately recomputes this group as one generation—never combining a restored
sample triplet with newly defaulted derived values.

## Visual observer transaction

### Observer baseline and target range

An NPC first resolves its own authored sensory baseline. `InitPerceptionDistances` (`0x1028fb70`)
uses `-1.0` as the sentinel for `vision` or `hearing`: a sentinel derives the effective value from
`npc_perception` and its inspection table, while any other authored value is copied directly and
makes `npc_perception` inert for that channel.

The concrete VHuman target-admission body at `0x102b4760` obtains the candidate's target-side
scalar and computes:

```text
effective visual radius = observer effective vision distance × target vision scalar
```

It rejects a candidate beyond that radius before later actor/relationship eligibility work. A
target between `0.7 × radius` and the full radius sets a separate outer-band byte consumed by an
attention/investigation path at `0x102b3e00`; the exact authored name of that secondary policy is
still unresolved. The outer band is not a second binary visibility result.

### Closest-player cone and LOS cache

On each eligible Troika-NPC think, `SetClosestPlayer` (`0x10293a80`) enumerates player client slots,
chooses the nearest present player by Euclidean distance, stores its handle and distance, and
publishes the NPC/distance pair to the player's HUD observer surface. This nearest-player cache is
not hostility admission and does not itself fire a found output.

`SetPlayerLOS` (`0x10291610`) refreshes its cached player sight on a **2.0-second** cadence:

1. Resolve the closest-player handle.
2. Call `CBaseCombatCharacter::FInViewCone` (`0x10326750`). The observer's cone threshold and the
   target player's `m_flStealthVisionCone` are applied in that test.
3. If out of cone, cache LOS false.
4. If in cone and distance is at or below **512 units**, cache LOS true without a trace.
5. Beyond 512 units, trace from the NPC eye-side point to the player-side point with mask `0x4091`;
   fraction below one, start-solid, or all-solid means blocked.
6. Whenever the far trace is clear, update the last-clear-LOS time. If it becomes blocked while the
   player remains in cone, preserve LOS for **8.0 seconds** after that last clear time.

Special disabled/no-player branches initialize the cached cone and LOS bytes true with current
timestamps. They are sentinel/default branches under their surrounding guards, not evidence that
an NPC without a player has detected one. Consumers preserve the handle and state gates.

### Sight is not memory or enemy assignment

The cache and range/cone tests supply sensory facts. The ordinary AI pass still owns sense refresh,
relationship filtering, enemy memory, schedule-gated enemy replacement, and committed-enemy
conditions. The high-level order is:

```text
player light/Sneaking update
  -> observer effective range and outer-band admission
  -> closest-player cone/LOS cache
  -> sense refresh and memory mutation
  -> schedule interrupt-interest gate
  -> enemy eligibility/ranking and SetEnemy
  -> committed-enemy LOS debounce and found/lost-LOS outputs
  -> state, schedule, tasks, movement and attacks
```

`GatherEnemyConditions` (`0x10270b20`) debounces the committed enemy's LOS separately. It clears
the transient enemy conditions, runs the current LOS query, and increments a failure counter up to
10. Before ten consecutive failures it retains `HAVE_ENEMY_LOS`; at ten it changes to
`ENEMY_OCCLUDED`. A retained memory bit makes the output edge-triggered:

- admitted committed-enemy LOS with the bit clear fires `OnFoundEnemy` and, for the player,
  `OnFoundPlayer`, then sets the bit;
- the tenth consecutive failed check with the bit set fires `OnLostEnemyLOS` and, for the player,
  `OnLostPlayerLOS`, then clears the bit; and
- `OnLostPlayer` is later: `ChooseEnemy` emits it when an eluded remembered player resolves away
  through the schedule-gated enemy-loss transaction.

Consequently `OnLostPlayerLOS` does not clear the enemy, forget the player, or force idle, and
`OnFoundPlayer` is not an alias for writing `m_hEnemy`. Search, investigate, chase, and combat may
continue from memory after current sight is lost.

## Auditory stealth

`CBaseEntity::AdjustSoundDistForStealth` (`0x1009d850`) acts on eligible type-4 sound insertion when
the source exposes the combat-character/player stealth surface. It reads
`m_flStealthHearingDist`, subtracts that distance from the sound radius, and floors the result at
zero:

```text
stealth-adjusted radius = max(0, authored sound radius - player hearing reduction)
```

The adjusted stimulus then enters the ordinary sound system, where the observer's authored hearing
value, sound category, occlusion policy, conditions, and memory apply. Target hearing stealth is
therefore neither a universal mute nor an observer hearing multiplier. A sound that remains in
range can still create last-heard memory and `OnHearPlayer`/combat reactions independently of
visual sight.

## `trigger_stealth_mod`

The trigger authors integer key `stealth_modifier` in field `m_nStealthMod` at `+0x598`. Its
specialized touch callbacks are deliberately small:

```text
StartTouch(other):
    BaseTrigger::StartTouch(other)
    if other exposes a combat-character component:
        other.rawStealthModifier += m_nStealthMod

EndTouch(other):
    BaseTrigger::EndTouch(other)
    if other exposes a combat-character component:
        other.rawStealthModifier -= m_nStealthMod
```

The specialized increment/decrement occurs after the base callback and does not inspect the base
filter result. Spawnflags/filter policy controls the base trigger's own admission/output work, but
the modifier body itself is guarded by combat-character embodiment. The underlying touch-link
system still supplies one begin/end pair per contact.

Contributions from overlapping volumes add in the raw aggregate at combat-character `+0x1084`.
Only `GetStealthModifier` clamps its return to `[-10, +10]`; the stored sum is not clamped. The
clamped value is added while resolving category-1 CharacterData, which is how a volume changes the
Sneaking rating consumed by the tables. The raw sum remains raw so that leaving one of several
overlaps restores the correct remaining contribution.

The current export corpus contains six such volumes: three in `sp_tutorial_1` and three in
`sp_theatre`, all authored with modifier `2`. The tutorial volumes have no outputs; their effect is
the balanced contribution, not an I/O pulse and not a request to hide the player.

Removal, disable, and save/load while a combat character remains inside a modifier volume require
focused live acceptance. Static code proves ordinary paired enter/leave and generic touch-list
teardown rules, but does not yet prove which edge rebuilds or releases this specialized contribution
across every abnormal teardown. The implementation uses an overlap lease keyed by trigger/contact,
so a tested teardown policy can be attached without changing the aggregate contract.

## HUD observability is not authority

The player retains a nearest eligible hostile observer handle, distance, and meter/status at
`+0x1cc0..+0x1cc8`. `SetClosestPlayer` feeds this surface; the player-side update filters observer
state and relationship and replaces the incumbent only with an eligible nearer observer. Separate
debug/presentation helpers derive a readout from observer distance and the same sight scalar.

The rebuild publishes an equivalent stable snapshot for the PP4 HUD, but the UI never runs range,
cone, trace, memory, or enemy-selection logic. Gameplay owns the observer transaction; the HUD
consumes observer handle/generation, distance, and committed detection state. An absent or stale
observer clears presentation without mutating gameplay.

## PP4 and PP6 contract

PP4 owns the modern HUD slot and snapshot consumer. It may establish the view-model shape and
render an absent/cleared observer without waiting for stealth authority. PP6 owns the player target
surface, every observer calculation and state transition, and publication of the committed
snapshot. The presentation seam is therefore ready before detection lands without becoming a
second detection path.

The PP6 implementation needs all of these boundaries, not only a stealth score:

- load all four selected `StealthData` tables and diagnose missing/malformed rulebook input;
- recompute the player target surface on its cadence, retaining the three-sample light cycle;
- keep observer baseline, target range scalar, target cone scalar, sound-radius reduction, current
  sight, memory, committed enemy, and outputs as distinct state;
- preserve the two-second closest-player LOS cache, 512-unit near bypass, eight-second in-cone grace,
  and ten-failed-check committed-enemy LOS debounce;
- make `trigger_stealth_mod` a balanced overlap contribution whose effective read is clamped;
- enqueue found/lost outputs through ordinary entity I/O rather than calling script consequences in
  the sensing stack; and
- publish HUD observability after gameplay state commits, with no UI authority over detection.

Static tests prove table indexing, threshold equality, cadence scheduling, range/cone/sound
arithmetic, stacking, debounce edges, memory separation, output order, snapshot generation, and
save/restore grouping. Played acceptance covers bright/dark threshold boundaries, rapid light
changes across all three sample points, the 512-unit trace crossover, transient and sustained
occlusion, overlapping modifier volumes, sound-only discovery, save/load inside a volume, and the
tutorial's actual stealth success/failure branches from real input.

## Separate stealth-kill work

`stealthkillrules.txt` is not part of the ordinary observer. Its victim selection, feat bands,
distance, hearing-scalar/deaf-zone admission, and transition into the stealth-kill action remain a
separate native recovery and implementation transaction. Completing the observer does not close
that work, and loading the file without its consumer is not stealth-kill support.
