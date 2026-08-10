# Feeding — command, paired action and blood transaction

VtMB feeding is a **native paired gameplay action**. It is not a `.vcd` choreography and it is
not a single animation with blood changes attached to its loop. Native code acquires and accepts
a victim, starts a two-actor grapple mode, and selects `ACT_*` activities. The attacker and victim
models answer those activities with role/size/direction-specific sequences. Model-authored
animation events mark the bite and release boundaries; after the bite boundary, an independent
server timer owns blood loss, blood gain and healing.

This document owns the feed transaction. Generic command transport is in `docs/vtmb/controls.md`,
the opposed check is in `docs/vtmb/skills-and-checks.md`, and the common paired activity resolver
is in `docs/vtmb/animation_and_movers.md`.

## Evidence boundary

The native findings below are from the pinned retail binaries:

| Module | SHA-256 |
|---|---|
| `client.dll` | `e88beae0dd03af06493c71c5e8d87a6993b54e590cb6ad37cd3513c588582870` |
| `vampire.dll` | `c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f` |

`[VtMB]` means verified in those native bodies; `[model]` means decoded from the installed MDL
corpus; `[script/data]` means read from the patch-first game corpus. No controlled live feeding
trace has yet joined input, paired activities, animation events and blood values in one run. Static
ownership is closed where stated; the final section keeps the live and semantic gaps explicit.

## End-to-end ownership

The ordinary retail path is:

```text
F binding
  -> client +feed / -feed kbutton
  -> usercmd held bit 0x00400000
  -> server player command consumer
  -> CBasePlayer::Replenish(false)
  -> target acquisition and AttemptFeed
  -> StartGrappleAttack(target, mode 0 or 6)
  -> paired ACT_* selection on attacker and victim
  -> model animation event 4007
  -> CBaseCombatCharacter::FeedBegin(target)
  -> CBasePlayer update calls Feed() against the server clock
  -> model event 4006, damage, invalid state or completed release
  -> FeedInterrupt() and EndGrapple()
```

The transaction therefore has five separate owners:

| Layer | Owner | Result |
|---|---|---|
| intent | client command button | held `+feed` bit and release edge |
| eligibility | `CBasePlayer::Replenish` / `AttemptFeed` | accepted target and paired mode |
| performance | paired grapple router plus MDL sequences | synchronized attacker/victim animation |
| commit | `FeedBegin` / `Feed` / `FeedInterrupt` | blood, health, timer and victim outcome |
| authored consequence | NPC feed callbacks and entity outputs | `OnFedUponBegin` / `OnFedUponEnd` wires |

These layers must remain observable separately. A selected feed animation does not prove that
`FeedBegin` fired, and a visible loop does not identify the time of an individual blood pulse.

## Command and initial request

### Retail `+feed`

`client.dll` registers `+feed` at `0x10105a90` and `-feed` at `0x10105ae0`. Their handlers at
`0x10102230` and `0x10102240` press and release kbutton `0x104d2e40`. The command packer rooted at
`0x10104460` writes its held state as usercmd bit **`0x00400000`**. `[VtMB]`

The server consumer at `0x101728f0` tests that held bit. While the general player action-denied
predicate is false, it invokes player virtual `+0x6a0`, `CBasePlayer::Replenish`, with a false
`CantBreakGrapple` argument. `Replenish` refuses to start another request while the player already
has a paired peer. `[VtMB]`

The released-button bit is also published by the server's general input-edge event surface at
`0x10118db0`. That body does **not** call `FeedInterrupt` directly. The accepted action has its own
continuation latch and exits through paired state, animation-event and interruption policy; the
exact player-controlled early-cancel gesture still needs a live trace.

### Patch alias is a producer, not the transaction

The shipped retail default binds `F` to `+feed`. The installed patch can rebind the same chosen key
to `vm_feed`, whose Python `checkFeed()` selects prayer for its special clans and the ordinary feed
producer otherwise. `SeductiveFeed` is another Python producer of paired mode 2. `[script/data]`

Those aliases decide how a request enters native behavior. They do not own target selection,
paired sequence resolution or timed blood transfer. A faithful ordinary feed implementation must
not depend on Plus Patch Python or on a choreography player.

## Target acquisition and acceptance

`CBasePlayer::Replenish` at `0x10168320` first asks `0x101690b0` for a victim. The direct search is
a hull trace from the view position toward local offset `(32 forward, 0 right, -32 vertical)`,
using extents `(-8,-8,-8)` to `(8,8,8)` and mask `0x0201400b`. A secondary cone/radius survey
supplies the small-animal route and is governed by native `rat_feed_arc` and `rat_feed_radius`
configuration. `[VtMB]`

For an ordinary target, `AttemptFeed` at `0x10168910` applies this policy:

1. `ACT_DISPOSITION_MESMERIZED`, `ACT_DISORIENTED`, `ACT_LOST` and `ACT_COWER` are automatic
   acceptance states.
2. A target whose `ResistsFeeding` predicate is false also accepts without a roll.
3. Otherwise the attacker succeeds when current `Close_Combat_Brawl` is greater than the
   victim's non-negative Hacking net at difficulty 6.
4. A separate stealth predicate can still authorize the feed after that opposed comparison
   fails.

The rating and dice semantics remain canonical in `docs/vtmb/skills-and-checks.md`. The Hacking
use is literal retail behavior, not a proposed trait rename.

On acceptance, `Replenish` chooses paired mode `6` when the target's native type is rat and mode
`0` otherwise, calls `StartGrappleAttack`, sets the continuation latch at player `+0x14a8`, records
`CantBreakGrapple`, and clears the auxiliary release timer. If target validation or paired sequence
resolution fails, it plays the registered `ACT_FEEDING_ENGAGE_FAILURE` path instead of creating a
partial transaction. `[VtMB]`

## Animation: hard-coded policy over model-authored clips

### It is not a choreo file

There is no `.vcd` scene in the ordinary feeding path. The hard-coded part is the native state
machine: grapple mode numbers, `ACT_*` activity requests, transition predicates, animation-event
IDs and transaction callbacks. The performances themselves are authored sequence and animation
records in shared male/female MDLs such as:

```text
models/character/shared/{male,female}/forced_feed.mdl
models/character/shared/{male,female}/seductive_feed.mdl
models/character/shared/{male,female}/animal_feed.mdl
models/character/shared/{male,female}/zombie_feed.mdl
```

`StartGrappleAttack` validates that both actors can answer the initial base activity. The paired
translator expands that base into attacker/victim, short/tall-partner and front/back variants,
then each model performs normal activity and weapon translation plus weighted sequence selection.
A missing answer ends the pair; it does not guess a clip name. `[VtMB] [model]`

### Feed modes

| Mode | Initial activity | Producer and continuation |
|---:|---|---|
| `0` | `ACT_FEEDING_ENGAGE` (`0xf5b`) | ordinary `Replenish`; forced-feed state machine |
| `1` | `ACT_FEEDING_ENGAGE` (`0xf5b`) | same registered state machine; no pinned-binary caller |
| `2` | `ACT_SEDUCTIVE_ENGAGE` (`0xfa5`) | Python `SeductiveFeed`; seductive state machine |
| `6` | `ACT_RAT_FEED_ENGAGE` (`0x1027`) | rat target from `Replenish`; rat state machine |
| `8` | `ACT_ZOMBIE_FEEDING_ENGAGE` (`0xfca`) | `CBasePlayer::BeFedOnByZombie`; reverse feeding role |

The attacker advances the paired state; a role-1 victim does not independently choose the next
base activity. The ordinary state family progresses through engage/idle, bite, feed loop and one
of feed-release, ordinary release, attack release or player-flyback release. Rat feeding uses
engage, loop and release. Seductive feeding uses engage, loop, ordinary release or
`ACT_SEDUCTIVE_RELEASE_TO_MEZ`. Zombie feeding has the analogous engage/idle, bite, loop and
release families. `[VtMB]`

The compact player action `PLAYER_FEED` (code 6) has no recovered producer. Live ordinary feeding
is represented by `PLAYER_GRAPPLE` and its paired mode, so a remake must not use the dormant
compact code as the transaction owner.

### Animation events are boundaries, not the pulse clock

`CBaseCombatCharacter::HandleAnimEvent` at `0x1032e330` gives three feed events distinct jobs:

| Event | Authored location | Native role |
|---:|---|---|
| `4007` (`0xfa7`) | ordinary/zombie bite cycle 0; late rat/seductive engage | calls virtual `FeedBegin(target)` for the valid paired attacker |
| `4006` (`0xfa6`) | release sequences | calls `FeedInterrupt` for the relevant paired role/state |
| `5116` | forced-feed loop cycle 0, option `force_feeding_emitter` | starts the mouth-attached feeding visual effect |

Thus animation does gate the **start** of the authoritative transaction at the bite and helps gate
its release, but no model event transfers one unit of blood. Once event 4007 has called
`FeedBegin`, server updates call `Feed()` against `m_flNextFeedPulse` independently of feed-loop
cycles. The loop may repeat without a pulse on its boundary, and a pulse may occur part-way through
a loop.

### Representative clip timing

These are decoded metadata for the attacker/short-victim/front variant in the shared banks. They
are useful reproduction targets, not proof of live playback rate or blend time. `[model]`

| Family / sequence | Frames at 30 fps | Nominal `frames / fps` | Boundary event cycle |
|---|---:|---:|---:|
| ordinary engage | 16 | 0.533 s | — |
| ordinary bite | 19 | 0.633 s | `4007 @ 0.0` |
| ordinary feed loop | 61 | 2.033 s | `5116 @ 0.0` |
| ordinary feed release | 73 | 2.433 s | `4006 @ 0.305556` |
| rat engage | 34 | 1.133 s | `4007 @ 0.818182` |
| rat loop | 61 | 2.033 s | — |
| rat release | 58 | 1.933 s | `4006 @ 0.631579` |
| seductive engage | 166 | 5.533 s | `4007 @ 0.951515` |
| seductive loop | 60 | 2.000 s | — |
| seductive release | 62 | 2.067 s | `4006 @ 0.508197` |

Male and female shared banks answer the same activity vocabulary. Several zombie clips differ in
length between banks, so the activity/state contract is stable while exact clip duration remains
model-authored.

## Authoritative transaction state

`FeedBegin` at `0x10339d90` refuses a null target, a disallowed attacker or a second active feed.
It clears per-feed counters, stores the victim handle, calls the victim's feed-begin callback and
initializes these fields: `[VtMB]`

| Offset | Recovered meaning |
|---:|---|
| `+0x1490` | next feed-pulse absolute server time |
| `+0x1494` | current pulse interval |
| `+0x1498` | feed-start absolute server time |
| `+0x149c` | authoritative feed-target handle |
| `+0x14a0` | blood successfully credited/stolen counter |
| `+0x14a4` | signed initial blood-gain modifier |
| `+0x14a8` | player feed-continuation latch |
| `+0x14a9` | `FeedInterrupt` re-entry guard |

The save schema names `m_flNextFeedPulse` and `m_flFeedStartTime`, confirming that feed cadence is
simulation state rather than a transient animation notification.

## Blood-transfer timing and commit

Let `B` be the victim's current `BloodPool` integer when `FeedBegin` runs. The pinned binary stores
the two timing constants as doubles:

```text
minimum interval = 0.30 seconds
interval step    = 0.15 seconds
initial interval = 0.30 + (B + 1) * 0.15 seconds
first deadline   = server_now + initial interval
```

When `server_now >= m_flNextFeedPulse`, `Feed()` at `0x1033a400` performs at most one scheduled
pulse in that player update, then applies:

```text
if current_interval > 0.30:
    current_interval -= 0.15
next_deadline = server_now + current_interval
```

This is an accelerating server-timer cadence, not a fixed two-second loop cadence and not a
catch-up loop based on elapsed animation cycles.

The baseline unit transaction is:

1. Try `IncBloodPool()` on the feeder.
2. Increment `m_iBloodStolen` only when that blood-pool increment succeeds.
3. Compute the victim/rules-derived feed-heal amount and call `HealthHeal()` on the feeder.
4. Call `DecBloodPool(false)` once on the victim.

Healing is still evaluated in the normal branch when the feeder's blood pool is full; the
successful-blood counter is not. Victim depletion is passed with the immediate-resolution flag
false, so the final death/incapacitation decision is deferred to feed teardown. `[VtMB]`

Trait-effect branches can change the number of feeder gain/heal iterations for a rat pulse, make a
rat pulse consume without the normal gain branch, and add a signed first-pulse modifier. The data
surface contains `FX_Increased_Rat_Feed`, `Fx_Feed_Bonus_Opp_Gender`, `Fx_Feed_Bonus_Tramps`,
`Fx_Cannot_Rat_Feed` and `Fx_No_Resist_Feeding`. The bit-to-name mapping and each exceptional
branch still need a focused semantic trace before implementation; the ordinary one-unit contract
above is closed.

## Interruption, completion and outputs

`FeedInterrupt` at `0x1033a9e0` is authoritative, idempotent teardown. It clears the continuation,
cant-break and frenzy-grapple latches, stops feed sound/camera state, and then, when a victim is
present:

1. stops feeder/victim loop and heartbeat presentation;
2. reads the victim's remaining `BloodPool`;
3. when it is below one, selects the native death/incapacitation outcome for that victim/context;
4. otherwise returns the victim to its non-depleted post-feed path;
5. calls the victim feed-end callback;
6. clears the target handle and current interval.

The NPC callback surface exposes `OnFedUponBegin` and `OnFedUponEnd`; `FeedBegin` brackets the
victim with the start callback and `FeedInterrupt` brackets it with the end callback. Those outputs
are what the tutorial blueblood's maker wiring consumes. The exact caller/activator identity and
maker-child forwarding order still require the controlled tutorial trace.

Confirmed interruption producers include:

- release event `4006` on the authored paired release sequence;
- incoming player damage while paired in ordinary or seductive feeding, before damage commit;
- paired sequence resolution failure or normal paired state completion;
- invalid feed target/state detected by the common grapple lifecycle.

The exact user-requested early-release route is not closed by static command inspection alone:
`-feed` updates the command button and publishes a release edge, but does not directly call
`FeedInterrupt` in that publisher.

## Recreation contract

The narrow first faithful slice is ordinary player-on-humanoid feeding, including the tutorial
blueblood. It needs:

1. A `Feed` input action that preserves press, held and release state; ordinary input maps to the
   retail `0x00400000` intent rather than directly playing a montage.
2. A target query and acceptance service that retains automatic states, `ResistsFeeding`, the
   asymmetric Brawl/Hacking decision and stealth override as separate verdicts.
3. One paired-action owner that aligns two actors and resolves role/size/front-back activities
   from the model-derived activity catalog.
4. Native-equivalent ordinary state transitions through engage, bite, loop and release.
5. An animation-event bridge where 4007 opens `FeedBegin`, 4006 requests teardown and 5116 is
   presentation only.
6. A server-simulation-clock transaction using the exact 0.30/0.15 cadence and explicit feed
   state fields; animation time never substitutes for the next-pulse deadline.
7. Atomic feeder/victim blood and health changes plus a single idempotent teardown path.
8. Stable victim and maker ownership so `OnFedUponBegin` and `OnFedUponEnd` reach authored map
   wires with the correct caller and activator.
9. Save/restore of an in-progress feed without duplicating a pulse or losing the victim link.

No `.vcd` reader, choreography timeline or exact sequence-label command is a dependency of that
slice. Seductive, rat and zombie modes should extend the same paired and transaction services
rather than introduce separate montage-owned implementations.

## Open verification gaps

- Capture one ordinary humanoid feed with command down/up, grapple mode/role, activities,
  sequences, events 4007/4006/5116, timer fields, both BloodPool values, health and outputs.
- Identify the exact player-controlled early-cancel gesture and its transition into a release
  activity; static `-feed` inspection alone does not prove it.
- Name and capture every exceptional trait-effect branch, especially Nosferatu rat gain, Ventrue
  rat rejection and the two feed-bonus histories.
- Resolve the exact depleted mortal/Kindred/unkillable victim outcome matrix, humanity and
  Masquerade consequences, and frenzy interaction.
- Capture camera, input suppression, feed bar, audio and particle start/stop ordering.
- Verify seductive, rat and zombie modes independently; mode presence and model timing do not
  prove that every branch is reachable in ordinary retail play.
- Verify `OnFedUponBegin`/`OnFedUponEnd` caller, activator, maker forwarding and save/load behavior
  on `sp_tutorial_1`.

## Reproducible evidence

| Surface | Entry points / command |
|---|---|
| client command registration and bit packing | `client.dll` `0x10105a90`, `0x10105ae0`, `0x10102230`, `0x10102240`, `0x10104460` |
| request, target and check | `vampire.dll` `0x10168320`, `0x101690b0`, `0x10168910`, `0x101728f0` |
| paired routing | `0x10328df0`, `0x1032a100`, `0x10164240`; `uv run elysium research player_grapple_survey` |
| animation event boundary | `CBaseCombatCharacter::HandleAnimEvent` `0x1032e330`; `uv run elysium research animation_event_survey` |
| transaction | `FeedBegin` `0x10339d90`, `Feed` `0x1033a400`, `FeedInterrupt` `0x1033a9e0` |
| blood and health helpers | `IncBloodPool` `0x10338cb0`, `DecBloodPool` `0x10338df0`, `HealthHeal` `0x1033c340` |
| tracked native context | `research/cases/core-mechanics/specs/core_mechanics_server.json` |
| model sequence/frame inventory | `uv run elysium research inventory_player_animations` |

All decompilation, decoded model inventory and game-derived reports remain under
`$ELYSIUM_WORK_ROOT/research/`; only the reproducible specifications and engine-neutral findings
are tracked.
