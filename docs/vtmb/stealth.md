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

1. If the engine's client entity list is unavailable (engine slot 3), return without
   manufacturing replacement values.
2. Resolve the Sneaking feat through the ordinary CharacterData path. Category 1 includes
   `CBaseCombatCharacter::GetStealthModifier`; the result is capped at 10 before table lookup.
3. Evaluate the eligibility predicate: `FL_DUCKING` set and no `D_HT` NPC has assessed the
   player within the last 1.0 s (decoded 2026-09-08, see "The light query, recovered"). Failure
   does not skip the update: it installs the non-stealth fallback described below.
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

When the eligibility predicate is false, the routine writes `m_flLightOnMe = -1.0`, sight scalar
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
across every abnormal teardown.

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

## Stealth-kill transaction (RE50)

VtMB's stealth-kill system is a separate consumer of `vdata/system/StealthKillRules.txt` and the
combat character grapple pipeline (grapple mode 3). Unlike ordinary detection, stealth-kill
eligibility requires an active rear-approach deaf arc, valid melee weapon capability, strict victim
state/condition filters, and a paired synchronized grapple action.

### Rulebook and table initialization

`CStealthKillRules` is initialized at startup via `CStealthKillRules::InitTable` (`0x101be020`) on the
global singleton instance (`0x1072c540`).

`CStealthKillRules::LoadFile` (`0x101bebf0`) parses `vdata/system/StealthKillRules.txt`:

1. **`DeafZoneArc` section** (`CStealthKillRules::LoadKey` @ `0x101bedb0`):
   - Reads 20 degree entries (`0..19` corresponding to Sneaking ratings 1..20).
   - If keys are missing, entries default to the value of the immediately preceding index.
   - Stores into degree array `m_fDeafZoneArcDegrees[0..19]` at offset `+0x00`.
2. **`StatInfo` section**:
   - `StealthFeatMin`: integer minimum Sneaking feat clamp (default `1`, stored at `+0xa0`).
   - `StealthFeatMax`: integer maximum Sneaking feat clamp (default `10`, stored at `+0xa4`).
   - `HearingScalarMin`: float minimum hearing scalar clamp (default `0.0`, stored at `+0xa8`).
   - `HearingScalarMax`: float maximum hearing scalar clamp (default `3.0`, stored at `+0xac`).
   - `StealthKillDistMax`: float maximum acquisition distance (default `70.0`, stored at `+0xb0`).
3. **Precomputed cosine dot table**:
   - `InitTable` computes half-angle cosine dot thresholds for all 20 entries:
     $$\text{Dot}_i = \cos\left(\text{Degrees}_i \times \frac{\pi}{180} \times 0.5\right)$$
   - Stored into `m_fDeafZoneArcDots[0..19]` at offset `+0x50` (`+0x14` words).

### Deaf arc and minimum approach depth

#### Deaf Arc Angle Test (`InDeafArc` @ `0x101be500`)

To test if the attacker is behind the victim:

1. Obtain attacker origin $\mathbf{P}_{\text{atk}}$ and victim origin $\mathbf{P}_{\text{vic}}$ via `GetAbsOrigin()`.
2. Obtain victim forward direction $\mathbf{F}_{\text{vic}}$ and invert to get rear vector $\mathbf{B}_{\text{vic}} = -\mathbf{F}_{\text{vic}}$.
3. Compute normalized direction from victim to attacker:
   $$\mathbf{D} = \frac{\mathbf{P}_{\text{atk}} - \mathbf{P}_{\text{vic}}}{\|\mathbf{P}_{\text{atk}} - \mathbf{P}_{\text{vic}}\|}$$
4. Compute rear dot product $\text{Dot} = \mathbf{D} \cdot \mathbf{B}_{\text{vic}}$.
5. Attacker Sneaking feat rating ($1..10$, 0-indexed as $\text{feat} - 1$) retrieves the precomputed threshold $\text{Dot}_{\text{req}}$ via `GetDeafZoneArcDot` (`0x101be930`).
6. Attacker is in the deaf arc if and only if $\text{Dot} > \text{Dot}_{\text{req}}$.

#### Minimum Approach Depth Formula (`ComputeMinDepth` @ `0x101bef50`)

Approach depth scales the required rear proximity based on attacker Sneaking versus victim Hearing:

1. Attacker Sneaking feat is clamped and normalized over $[\text{StealthFeatMin}, \text{StealthFeatMax}]$:
   $$\text{NormSneak} = \text{clamp}\left(\frac{\text{Sneak} - \text{StealthFeatMin}}{\text{StealthFeatMax} - \text{StealthFeatMin}}, 0.0, 1.0\right)$$
2. Victim hearing scalar (obtained via `victim->GetHearingScalar()` or by mapping victim Perception through `Inspection_Hearing_Scalars`) is clamped and normalized over $[\text{HearingScalarMin}, \text{HearingScalarMax}]$:
   $$\text{NormHear} = \text{clamp}\left(\frac{\text{Hear} - \text{HearingScalarMin}}{\text{HearingScalarMax} - \text{HearingScalarMin}}, 0.0, 1.0\right)$$
3. Minimum approach depth is:
   $$\text{MinDepth} = \max\left(0.0, \text{StealthKillDistMax} \times (1.0 - \text{NormSneak} + \text{NormHear})\right)$$

High Sneaking against deaf/unaware targets reduces $\text{MinDepth}$ toward zero, while low Sneaking against sharp targets expands $\text{MinDepth}$ out to the full acquisition envelope.

### Victim selection and per-frame cache (`FindVictim` @ `0x101be1f0`)

`CStealthKillRules::FindVictim` maintains a per-player-slot cache (`m_hCachedVictim` at `+0xb4`, `m_nCachedFrame` at `+0x134`) updated once per engine frame:

1. **Player eligibility (`PlayerStealthKillEligibility` @ `0x10167320`)**:
   - Player must not be busy in dialog, cinematics, death, menus or camera lock (`0x101681a0`).
   - Player must be in sneak posture, ducking, or active Obfuscate (`0x101671a0`).
   - Active weapon (`GetActiveWeapon()`) must author stealth-kill capability (`m_bCanStealthKill` at `weapon + 0x872 != 0`).
2. **Forward acquisition trace**:
   - Traces forward from player eye position along eye view direction by `StealthKillDistMax` (70 units) with mask `0x201400b` (`MASK_SHOT`), filtering out the player.
3. **Victim admission (`IsValidStealthKillTarget` @ `0x10341850` / `0x102c2300` / `0x1037bbc0`)**:
   - Base `CBaseCombatCharacter::IsValidStealthKillTarget` returns `false`.
   - `CVHuman::IsValidStealthKillTarget` enforces:
     - Target must be alive (`!IsDead()`);
     - Target must not already be in a grapple/paired action (`m_hGrapplePartner == NULL`);
     - Target must not have boss/immunity flag (`m_bNoStealthKill == 0` at `+0x18f6`);
     - Target state (`GetNPCState()`) must be `NPC_STATE_IDLE` (1) or `NPC_STATE_ALERT` (0xd); target cannot be `NPC_STATE_DEAD` (7) or in active combat (`NPC_STATE_COMBAT`);
     - Target must NOT have conditions `COND_SEE_PLAYER` (`0x6f`) or `COND_IN_COMBAT` (`0x5a`);
     - Target must be human/biped (`IsHuman()`).
   - `CNPC_VGhoulCroucher::IsValidStealthKillTarget` override allows croucher ghouls when undisturbed (`!IsDisturbed()` at `+0x6666 == 0`).
4. **Arc test or state override**:
   - Requires `InDeafArc(player, victim)` **OR** victim mesmerized/blinded/trance override (`victim->NPCData + 0x5bb4 > 0`).
5. **Grapple admission**:
   - Verifies `CombatCharacterCanStartGrapple(player, victim, 3)`.

If all pass, `victim` is cached in `m_hCachedVictim[player_index]`.

### HUD publication and input commitment

1. **HUD prompt**:
   `CBasePlayer::UpdateClientActionState` (`0x101755d0`) / `FUN_10174580` queries `FindVictim`. When non-null, it publishes the stealth-kill interaction icon/prompt (`player + 0x1ea4`).
2. **Input precedence**:
   - **Secondary attack (`+attack2` / `+wpn_secondaryatk`)**: `CWeaponMelee::SecondaryAttack` (`0x103eaca0`) calls `PlayerTryStealthKill` (`0x10167370`) before regular heavy attack or blocking. If a victim is qualified, the stealth kill commits immediately.
   - **Primary attack / Use**: Handled via `PlayerTryStealthKillThunk` (`0x100154a1`).
3. **Grapple Mode 3 execution (`StartGrappleAttack` @ `0x10328df0`)**:
   - Stores victim handle into `player->m_hStealthKillTarget` at `+0x1c58` and `+0x1c60`.
   - Resolves paired activity pair via `CheckAndTranslateBaseActivity` (`0x10328af0`) and `TranslateBaseActivity` (`0x10328380`) based on attacker/victim gender, skeleton, and orientation.
   - Locks both combatants into grapple mode 3 via `EnterGrappleState` (`0x10329760`, vtable slot 379) — attacker with role `0`, victim with role `1` — freezing standard movement and AI; `SetGrappleActivity` (`0x1032a100`) then picks the paired activity and calls `EndGrapple` if it cannot.
   - Invokes `WeaponStealthKill` (vtable index `+0x534`) on the active weapon.
   - Snaps attacker relative to victim `Bip01` bone offset.
   - Paired death animation plays, committing fatal damage (`Event_Killed` / `TakeDamage`) on the victim upon completion.

### The grapple role pair and the `m_GrappleType` enum (2026-09-07)

The stealth kill is one member of a nine-valued grapple enum sharing one block of state on
`CBaseCombatCharacter`. The field names are read out of `CBaseCombatCharacter::Dump` (`0x103222c0`):

| offset | name | notes |
|---|---|---|
| `+0x1534` | `m_GrappleSavedMoveType` | `vfunc0x178()` captured on enter, restored on leave |
| `+0x1538` | **`m_GrapplePartner`** | EHANDLE, `-1` when idle |
| `+0x153c` | **`m_GrappleRole`** | **`-1` none, `0` attacker, `1` victim** |
| `+0x1540` | **`m_GrappleType`** | the mode below |
| `+0x1544` | `m_GrapplePosition` | the variant chosen by `CheckAndTranslateGrapplePosition` (`0x10328af0`) |
| `+0x1548`–`+0x1550` | — | `GetAbsOrigin()` saved on enter, read back on leave |
| `+0x1554` | — | 1 when entering holstered the active weapon |
| `+0x1558` | `m_hGrappleAnimDriver` | written **only when `role != 0`**: the victim points at the attacker |

**The role polarity is proved twice.** `StartGrappleAttack`'s two `EnterGrappleState` dispatches push
the role literal directly — `PUSH 0x0` for the attacker at `0x10329285`, `PUSH 0x1` for the victim at
`0x103292d3` — and `EnterGrappleState`'s `if (role != 0) m_hGrappleAnimDriver = partner` says the same
thing: the attacker drives the paired animation. `CBaseCineCam::SetShot`'s `GrappleVictim` /
`GrappleAttacker` anchor keywords read the same pair (see `docs/vtmb/camera-view-modes.md`), which is
why the `Stealth_Kill_1..4` shots in `vdata/camerashots/stealth_kill.txt` frame the right body.

**The scripted camera is a consumer of this pair (2026-09-07).** `stealth_kill.txt` anchors on
`GrappleAttacker` ×6 and `GrappleVictim` ×2 — the only file in the whole 66-file shot corpus that
writes either keyword — and each resolves to *the partner* or to *the subject itself* purely off
`m_GrappleRole`. The anchor arms fall through to `World` the moment the partner handle is not live,
so those four shots frame the world origin for any pair state other than this one.

**Writers — exactly three.**

1. **`CBaseCombatCharacter::EnterGrappleState` `0x10329760`** (vtable **slot 379**, `+0x5ec`) — the only
   setter, called twice per grapple by `StartGrappleAttack`, attacker first then victim, and rolled
   back on the attacker if the victim refuses. It writes the whole block in one transaction, optionally
   holsters, saves the move type, and for a player sets `MOVETYPE_NONE`.
   Overrides: `CBasePlayer::FUN_101695f0` (base + `FUN_10181580(player, 1)`, which sets bit `0x1` of
   `player+0x1d60`), `CAI_BaseNPC::FUN_1026cdc0`, `CAI_BaseNPCTroika::FUN_102b5c00`,
   `CPayphone::vfunc379` (`0x101aade0`), `CNPC_VGhoulCroucher::vfunc379` (`0x1037b500`).
2. **`CBaseCombatCharacter::LeaveGrappleState` `0x10329a70`** (slot **380**, `+0x5f0`) — the only
   clearer; sets all five of `+0x1538`, `+0x153c`, `+0x1540`, `+0x1544`, `+0x1558` to `-1`, restores the
   weapon and the move type, and re-places the character. Before clearing, `role == 0 && (type == 1 ||
   type == 4)` reads the **partner's** saved origin for the exit placement.
   `CBasePlayer::FUN_10169660` extends it with `FUN_101815b0(player, 1)` (clearing the same
   `+0x1d60` bit), **`SetCineCamera(NULL)`** — so ending a grapple ends any scripted shot — and the feed
   aftermath (blood stolen, bad blood, blue blood, masquerade).
   `CBaseCombatCharacter::EndGrapple` `0x10329560` is the paired teardown that calls `vfunc0x5f0` on
   both sides; it also fires `FUN_10175400(player, false)` when `role != -1 && type == 8`.
3. **`CBaseCombatCharacter::CBaseCombatCharacter` `0x10326de0`** seeds `+0x1538 = -1` and
   `+0x153c = -1`. It does **not** seed `+0x1540` / `+0x1544`, so `m_GrappleType` reads `0` until the
   first leave — harmless, because every consumer tests the role or the partner first.

**`m_GrappleType`, and where each mode is started** (`StartGrappleAttack(this = attacker, victim,
type)` `0x10328df0`):

| mode | started by | meaning |
|---|---|---|
| `0` | `CBasePlayer::Replenish` `0x10168320` | the feed, ordinary victim |
| `1` | no shipped caller | the second feed variant (shares mode 0's sound and 64-u distance) |
| `2` | the Python native `SeductiveFeed(a, b)` `0x10198150` | requires **both** parties idle; sets `player+0x14a8 = 1`; **no distance check at all** |
| `3` | `PlayerTryStealthKill` `0x10167370` | **the stealth kill** |
| `4` | no shipped caller | the stealth-kill twin — weapon activity `0x18` instead of `0x17` |
| `5` | `CBasePlayer::StartPlayerDialog` `0x10178280` | the payphone (`CPayphone`) |
| `6` | `CBasePlayer::Replenish` when `victim->vfunc0x228() == 0xd` | the feed on a type-13 victim |
| `7` | no shipped caller | shares mode 5's 144-u distance and holster policy |
| `8` | `CBasePlayer::BeFedOnByZombie` `0x10168700` | the player is the **victim** |

Mode-dependent behaviour inside `StartGrappleAttack`:

- **Facing yaw** is `VecToYaw(victim.origin − attacker.origin)`, except for **mode 5**, which takes the
  victim's own abs-angles yaw round-tripped through 16 bits
  (`((int)((yaw + 180) * 182.04444885f) & 0xFFFF) * 0.0054931640625f`, constants `_DAT_1044c3a8`,
  `_DAT_1044ffe0`, `_DAT_1044ffdc`).
- `sameSide = |AngleDiff(desiredYaw, victim.angles.yaw)| <= 90.0` (double `_DAT_1044e668`) is the
  position hint handed to `CanStartGrappleAttack` and `CheckAndTranslateGrapplePosition`.
- **Holster flags** are asymmetric: mode `3` holsters the **victim only**; modes `4` and `7` holster
  neither; every other mode holsters both.
- The post-enter jump table (`0x103293c4`, index = type, `0..4` only): modes `0`/`1` →
  `GrappleSoundCmd(attacker, 2)` + `GrappleSoundCmd(victim, 0)`; mode `2` → nothing; mode `3` →
  active weapon `vfunc0x534(0x17, 1, …)`; mode `4` → `vfunc0x534(0x18, 1, …)`.
- `attacker->m_iBloodStolen = 0` is zeroed just before the enter, and the
  `"StartGrappleAttack %s"` player event fires only for a player attacker on a live victim whose type
  is neither `8` nor `2`.

**`CBaseCombatCharacter::CanStartGrappleAttack` `0x103285a0`**, in order:

1. `victim == NULL` ⇒ false.
2. If the attacker is a player and `FL_DUCKING (0x2)`: a hull trace `[-16,-16,0]…[16,16,72]` from
   `GetAbsOrigin()` to itself with mask `0x201400b`; **fraction < 1, startsolid or allsolid ⇒ false** —
   a crouching player must have room to stand.
3. `this->vfunc0x5e4(victim, type)` and `victim->vfunc0x5e8(this, type)` must both pass.
4. If the **victim** is already grappling: its partner must be `this` **and** this's own current type
   must equal the requested type, in which case it **succeeds immediately**, skipping the distance and
   position checks. Otherwise false.
5. Else if **this** is already grappling: `role != 0` ⇒ false; `role == 0` ⇒ `EndGrapple(this)` and
   continue. An attacker can hand its grapple over; a victim cannot start one.
6. 2-D (x/y) distance against `GetAbsOrigin()`: type `2` skips it; types `3`/`4` use
   `CStealthKillRules`' `GrappleDistanceMax`; type `8` uses `Zombie_Grapple_Info`'s distance — both
   skipped when the value is `<= 0`; types `5`/`7` use **144.0**; type `6` uses **120.0**; everything
   else **64.0**.
7. `CheckAndTranslateGrapplePosition(...) != -1`.

**Readers.** `CBaseCineCam::SetShot` `0x1006e130` (the camera anchor keywords);
`CanStartGrappleAttack`; `EndGrapple`; `LeaveGrappleState`; `CBasePlayer::Replenish` (early-out when
already grappling); the `SeductiveFeed` native; `CPlayerMove::SetupMove` `0x10186120`;
`CBasePlayer::GetSaveBlockedReason` `0x10174f80`; `CBaseCombatCharacter::ChooseMeleeAttackSequence`
`0x10347180`; the player anim-state builder `0x10146e20` (`partner live && role != -1 && type == 3`
selects the stealth-kill anim state); the locomotion-state selector `0x1016bb50` (`partner live &&
role == 0` ⇒ state 8); the "find a grapple target" scan `0x10167470`; the crouch predicates
`0x101671a0` / `0x10167240` (true while `type == 3`); `CWeaponMelee::SecondaryAttack` and
`CWeaponMelee_Torch::PrimaryAttack`; the player state/HUD helpers `0x10170090`, `0x10170340`,
`0x10174580`, `0x10181780`, `0x1018b7e0`; and `CPointTeleport::InputTeleport` `0x1018dc00`
(`EndGrapple` before teleporting).

Three of those are worth spelling out:

- **`CPlayerMove::SetupMove` `0x10186120`** computes `role = (partner live) ? m_GrappleRole : -1` at the
  top. When the role is set it switches on the player's current activity (`player+0xFF0`): for
  `0xf88, 0xf91, 0xf9a, 0xfb7, 0xfc0, 0xff7, 0x1000, 0x1009, 0x1039` it behaves as if not grappling;
  **otherwise it slaves the move to the partner** — `cmd.origin = partner->GetAbsOrigin()` with
  `origin.z` corrected by the two collision-mins difference, and a globally stored view angle. This is
  the freeze that holds a stealth-kill pair together.
- **`CBasePlayer::GetSaveBlockedReason` `0x10174f80`** returns, in order: `2` a live dialogue partner ·
  **`3` `role != -1` with a live partner** · `4` a live `player+0x1040` · `7` `FUN_10175180` · `6` a
  live `player+0x19C0`, `player+0x19CC`, **a live cine camera**, or `FUN_101618a0` · `1`
  `player+0x1EB8` or `FUN_1023bd00()->+0x4ac` · then `5` when `vfunc0x278()` and `0` otherwise. **A
  grapple in progress blocks saving with reason 3.**
- **`CBaseCombatCharacter::ChooseMeleeAttackSequence` `0x10347180`** only reads the pair, and for one
  purpose: **a grappled target's partner is allowed to stand in for the target in the melee trace.** At
  `0x103472b9` a third entity blocking the trace is compared against `EHANDLE_Get(target+0x1538)`
  (evaluated only when `target+0x153c != -1`); a match means "not blocked", any other blocker aborts the
  attack and raises condition `0x3a`. At `0x103479ac` the same substitution is applied inside the
  per-sequence loop.

### Tutorial lesson completion

The tutorial stealth-kill sequence in `sp_tutorial_1` completes when:
- The qualified tutorial guard is dispatched via grapple mode 3;
- The guard's `OnDeath` output triggers the tutorial progression relay;
- Jack advances the lesson script.

## The light query, recovered (2026-09-08)

The "world-light service" above is now decoded end to end. It is **not a lightmap or leaf-ambient
sample**: it is a live, per-light, ray-traced evaluation of the BSP's worldlight array (lump 15),
run three times per player think on the server.

**The call.** `CHL2_Player::vfunc471` (`0x103517e0`) reads the `VEngineServer014` interface from
`0x1070b22c` and calls **slot 118** (`engine.dll CVEngineServer::vfunc118` `0x20108970`) with one
`Vector*` at `0x10351966`, `0x103519b1`, `0x103519d6`; a float returns in `ST0`. Slot 118 is the
server twin of `IVEngineClient::GetLightForPoint` (client slot 23, `0x2001a450`); its authored
server name is UNRECOVERED. Body:
```
FUN_200a4e90(&rgb, point, bClamp = 0);
return rgb.r*0.30 + rgb.g*0.59 + rgb.b*0.11;     // doubles at 0x20174c20/28/30 — luminance, unclamped
```

**`GetLightForPoint` (`engine.dll 0x200a4e90`).** Point → leaf (`0x2002fbc0`) → cluster
(`0x2002f2b0`); either `< 0` (a point in solid) returns black. `CM_ClusterPVS` (`0x20031f80`).
Loop over `worldbrush(0x20b42aec)+0x13c` entries of `dworldlight_t` (stride 0x58; offsets as in
`lighting.md`): a light with `cluster < 0` is skipped; **type 5 skyambient is skipped**; **type 3
sun**: only the first one, a trace of `−MAX_TRACE_LENGTH · normal` (`−56755.84`) that ends on
`SURF_SKY` adds the raw `intensity` with no attenuation, style or angle term, not PVS-gated;
every other light is **PVS-gated** by its cluster, then `scale = d_lightstylevalue[style] ×
r_lightmapcolorscale / 264 × DistanceFalloff`, and contributes `intensity × scale × Angle` **only
if a trace from the point to the light origin has `fraction == 1.0`**.

- Falloff `0x200a5620` = Source's `Engine_WorldLightDistanceFalloff`: type 0 `1/max(d², 1)` with the
  `radius` cutoff; types 1/2 `1/(quad·d² + linear·d + const)` with the cutoff; type 4
  `max(linear_attn − d, 0)` (field `+0x44`, **not** radius `+0x3c`); 3/5 `1.0`.
  Radius equality is admitted. **Not flat inside the radius.**
  Type 0's near-source cap lives behind the `InvRSquared` dispatch at `0x201a65a8`, assigned
  by `0x200b3730` to scalar `0x200ad040` or SSE `0x200ad3f0`; both cap the squared distance at
  one Source unit squared before taking its reciprocal.
- Angle `0x200a57e0` = `Engine_WorldLightAngle` called with `snormal == delta == direction to the
  light`, so point lights read 1.0, spots apply the `stopdot/stopdot2/exponent` cone, texlights
  and sky `−dot(lightnormal, dir)` when `> 0.01`.
- **Lightstyles apply at query time**: `d_lightstylevalue[]` (`0x20a6dcb0`), normalized by 264,
  animated by `0x20076eb0`. The raw body selects `trunc(time × 10) % patternLength` and writes
  `(letter − 'a') × 22`; there is **no interpolation**. An empty pattern writes 256 (therefore
  256/264 in this query). A flickering style modulates the player's light directly.
- `r_lightmapcolorscale` (`0x20a6e400`, default `"1"`, quantized 1/2/4/8/16) multiplies every
  contribution; inert at default.
- **No dynamic lights of any kind** — no dlights, entity lights, muzzle flashes; they are
  client-side. This is why `item_w_torch` is hard-forced to 1.0.

**Shadows.** Both traces go through the **client** engine-trace object (`PTR_DAT_201a0228 →
0x20a57180`, `vftable_CEngineTraceClient`), slot 4 `TraceRay`, mask `0x4191` (the shared engine
light/shadow mask; its VtMB `CONTENTS_*` decomposition is UNRECOVERED), filter
`CTraceFilterAllowWorldAndShadowProps` (`0x20174c3c`): trace type 3, `ShouldHitEntity` = is a
static prop (handle bit `0x40000000`) **and** its flag byte `+0xac` lacks bit `0x10`. So light is
blocked by **world brushes, displacements and unflagged static props only**; doors, `func_brush`,
physics props, NPCs and the player cast no stealth shadow. The availability gate (step 1 above)
is engine slot 3 (`0x201089f0`): "the client entity list exists and entity 0 is present" — the
only use of that slot in `vampire.dll`.

**The min/max are two flag-0 ConVars, never written.** `worldlight_min` (object `0x10938310`,
ctor `0x10351770`, default `"0.0"`) and `worldlight_max` (`0x10938358`, ctor `0x103516e0`,
default `"1.0"`); their only readers are `vfunc471` and the `debug_stealth_toggle` printer
(`vfunc470` `0x10351e10`: `"World Min: %.2f   Max: %.2f   Scale: %.2f"`). A binary grep of the
whole retail install (every `.bsp`, `.vpk`, `cfg/`, `python/`) for `worldlight_m` matches only
`vampire.dll`. **In shipped play the normalization is an identity; only the `[0, 1]` clamp
survives.** `debug_stealth_light` (`0x109384d8`, default `-1`, range `-1..10`, applied after the
torch label so it overrides the torch too) and `debug_stealth_show_light` (`0x109383a0`, a spew
interval; `"%6.3f feet %6.3f cent %6.3f head -> %6.3f -> %d"`) are the two debug knobs.

**The eligibility predicate, resolved** (`0x10351865`): `(GetFlags() & FL_DUCKING 0x2) &&
!FUN_101672d0(this)`, where `0x101672d0` = "a `D_HT` NPC has assessed me within the last 1.0 s"
(`this+0x1d28+4·cat` = the time, `+0x1d3c+4·cat` = the handle, written by `0x1017ff40` from the
NPC `OnLooked` pass `0x1026a2c0` with `cat` = the NPC's disposition to the player; 1 = D_HT).
**The tables apply only while crouched and unseen by any hostile for a second.** Obfuscate and the
sneak activity do not enter this predicate (they enter the general `0x101671a0`).

**Three corrections to the sections above.** (1) The inactive sentinel is **`-1.0`**
(`0x10351c99`), not `-4.0`. (2) A failed predicate does **not** skip the update: the AABB, the
three samples, the round-robin and the row/feat indices are all computed first; only
`m_flLightOnMe`, the vision scalar (`1.0`), the hearing reduction (`hearing[0] = 0.0`) and the
cone (`cone[0] = 1.0`) are then overwritten at `0x10351c95`. (3) The DLL-compiled row thresholds
are `0.9 … 0.1, 0.05, 0.0`; the `0.99, 0.95, 0.87 …` set is what `StealthLightRangeTable`
overwrites them with.

**The three sample points** (`0x103518da`–`0x103519dc`): `absmin/absmax` = the world-space
collision AABB (`0x100dcd90`); `centre = WorldSpaceCenter()` (slot 192, `0x10027160`) verbatim;
`head = (centre.x, centre.y, 0.875·absmax.z + 0.125·absmin.z)`; `feet = (centre.x, centre.y,
0.875·absmin.z + 0.125·absmax.z)` (`0x104a3050/54`). Round-robin state `+0x1c7c`: `1` → centre →
`2`; `2` → head → `0`; else feet → `1`. The centre is its own virtual's answer, numerically the
0.5/0.5 midpoint. Aggregate `× 0.083325` (`0x104a304c`, not `1/12`). The torch branch skips the
sampling entirely, so the saved samples go stale while it is held. The `SuppressLists(4, …)`
bracket around the query is inert (nothing in `engine.dll` reads the mask).

**Consumers.** `m_flLightOnMe` (`+0x1c8c`) has exactly one reader besides its writer:
`CBasePlayer::DrawDebugTextOverlays` (`0x10161460`, `"Light: %f"`). It is saved, never
networked. NPC admission reads only the derived scalar (`0x1029c9f0`: `max(dist × player+0x1c70,
0)`). The HUD light gauge (`client.dll CStealth::vfunc98` `0x10062690`, `hud/lightgauge_%d`)
animates from the **networked row index** (`m_iDebugStealthLight` = `field_0x1c98`), never the
raw luminance. `lighting.md`'s claim that lump 15 is read at runtime only by the model light
cache is corrected by this: `0x200a4e90` is a second consumer, and the stealth system is built
on it.

**The trace mask `0x4191`, decomposed.** VtMB's `CONTENTS_*` enum is stock Source below `0x100`
(`MASK_SOLID 0x200400b` etc. are literals in `PhysicsSolidMaskForEntity` `client.dll 0x1011c520`;
`MASK_WATER 0x4030` in `PhysicsCheckWater` `0x1003e880`) and shifted above it (`TESTFOGVOLUME`
is `0x200`). `0x4191 = SOLID 0x1 | SLIME 0x10 | OPAQUE 0x80 | 0x100 | MOVEABLE 0x4000`. Across the
101 shipped BSPs: `SOLID` on 121 350 brushes; `SLIME` on none (inert); `OPAQUE` on 8; **`0x100` on
944 brushes over 57 maps, all `0x18000120` — the `%compileShadowOnly` / `TOOLS_SHADOW` bit**, so
shadow-only brushes block light while being invisible and non-solid (this corrects
`bsp_format.md`'s "`0x100`: nothing in engine.dll reads it"); `MOVEABLE` is engine-set and never
reached. Absent: `WINDOW 0x2` and `GRATE 0x8` — glass and grates cast no stealth shadow. The
sibling `0x4091` (no `0x100`) is the NPC line-of-sight mask (`SetPlayerLOS`, `StartTask`), so a
`TOOLS_SHADOW` brush blocks light but not sight. Brush entities (`func_door`, `func_brush`, movers)
carry `SOLID` and would pass the mask; they are removed by the **filter**
(`CTraceFilterAllowWorldAndShadowProps`, trace type 3: every enumerated entity that is not an
unflagged static prop answers false). The `0x100` bit's authored name is not in the image.

**The HUD light gauge, decoded** (`client.dll CStealth::vfunc98` `0x10062690`, the vgui
`Paint` slot; sprites from `FUN_10063210`). There is **no row-to-frame mapping**: the five
`hud/lightgauge_%d` sprites, `lightgauge_frame`, `HealthSneak_*` and `Light_Level` are allocated
and never drawn (a superseded HUD generation). The live gauge: `m_iGaugeValue +0x1ac` starts at
`-1` and on its first paint snaps to the raw row (0..10), then every painted frame moves toward
`row × 10` by **±3, unscaled by frame time**; `clipTop = trunc(2.97 × (value − 100)) + 404`,
and `hud/new_ui/stealthfillfull` (64×404 at (30, 128) on a 1024×768 reference layout) is drawn
clipped to `[clipTop, 404]` over `stealthfillbg`, under `stealthframe` (31, 110). The bar fills
from the bottom; **full = fully lit (row 0), empty = fully dark (row 10)**, linear at 29.7 px per
row. It is drawn only while `m_fFlags & FL_DUCKING` (the client does not mirror the server's
"unseen by a hostile for 1 s" arm, so the bar keeps reporting light while a hostile watches).
Beside it an icon from `stealth_green / _orange / _red / _seen` chosen by `(player+0x16b4 >> 8) &
0x7f`, forced to `_seen` when spotted, with the number `player+0x16b4 & 0x7f` under it; with
Obfuscate active (stat 8 of the discipline list) `Sneak_Icon_Obfuscated` replaces both. The
spotted edge (`player+0x16f4 & 0x40`) plays `player_stealth_discovered.wav`. Debug text under
`m_iDebugFlags +0x15f4`: `LIGHT:` / `ST(%3d) * LI(%3d)` (`m_iDebugStealthFeat +0x15f8`,
`m_iDebugStealthLight +0x15fc`) / `-> LIs(%3.0f%%)` (`(1 − m_flStealthVisionScalar) × 100`,
`+0x1604`); and under bit 2 the sound lines with `m_iDebugStealthSound +0x1600` and
`m_flStealthHearingDist +0x1608`. UNRECOVERED: the recv-prop names of `+0x16b4` and `+0x16f4`.

**Names not in the image.** `IVEngineServer` slot 118 carries no string (only its client twin
`CEngineClient::GetLightForPoint` is named; `CVEngineServer` has 124 slots, only the four
`Precache*` methods carry strings); `CAI_BaseNPCTroika+0x6081` has no datamap entry and no
printer; the `m_bfNPCFrenziedFlags` bits have no table; virtual slot 587 has no string, VProf
scope or Python native. Stop searching for these; keep the project names.

**Additional closure (2026-09-08).** `FUN_1017ff40` excludes class id 13 before writing the
assessment timestamp/handle: slot 138 is `CNPC_VRat::vfunc138` `0x103ad660`, which returns 13.
`FUN_101672d0` requires that handle still resolve and uses strict `time + 1.0 > curtime`;
equality at one second is eligible. The torch's byte `+0x871` is class capability, not deployment:
`CWeaponMelee` constructor `0x103e9ac0` writes 1; `CWeaponUnarmed` constructor `0x103f53f0`
writes 0; several blunt weapon `vfunc103` methods clear it. A torch on the melee class retains 1.

**Port data seam and query (0005 requirement 1).** The V2 map bake now authors
`DA_<map>_LightQuery`: original worldlight rows (including rows that place no rendering actor),
the BSP node/leaf partition and decompressed PVS, mask-`0x4191` world convexes, displacement
triangles, and the `SURF_SKY` boundaries of those world brushes. Positions, planes and falloff
coefficients are converted to native centimetres offline. Source I/O and the live style patterns
remain runtime-owned. The native asset uses Unreal's cooked collision data; `QueryGameplayLight`
queries those components and the unflagged GAME_LUMP static props directly. Direct component
traces implement the filter without a second global collision channel: no door, brush entity,
physics entity, NPC or player is enumerated. Glass/grate and playerclip-only world brushes are
absent; invisible shadow-only brushes are present. A sun requires an authored sky hit at or
before the first blocking boundary, not merely an unobstructed ray.

`Adopt` and `AdoptBaked` both load this same gameplay asset. Rendering's `FLightSource` calibration,
disabled-light list, transformed miniature lights and `light_dynamic` actors never enter it.
`ElysiumWorldLight::Query` preserves source order, first-sun selection, cluster/PVS gates, Source
falloff and angle branches, discrete live styles and unclamped luminance. The player then takes
one point per 0.1 s from the collision world AABB, with the real centre, and clamps only the
`0.083325` aggregate against the shipped 0/1 range. Ineligibility overwrites four values with the
`-1`/neutral arm after row computation; it never freezes sampling. Missing native query data
warns and reports service unavailable, preserving the last surface instead of manufacturing light.

The tutorial V2 stage witness retains **396/396** raw RGB rows, **2651** clusters, **2335** world
shadow convexes, **1468** sky triangles and **3584** displacement triangles. Focused pipeline
tests establish those counts and raw intensity conservation; cooked/native component traces and
rendered crouch/occlusion remain separate execution acceptance gates. `r_lightmapcolorscale`
retains its shipped default 1; the retail developer min/max and override consoles are not exposed.
