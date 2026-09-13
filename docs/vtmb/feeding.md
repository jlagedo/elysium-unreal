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
corpus; `[script/data]` means read from the patch-first game corpus; `[capture]` means visually
verified in the 2560x1440, 60 fps retail recording `E:\gamecapture\feed.mkv`; `[capture/audio]`
means a shipped sound asset was correlated against that recording's decoded mono track. The
recording carries no command, activity, event or blood telemetry. No controlled live feeding trace
has yet joined all channels in one run. Static ownership is closed where stated; the final section
keeps the remaining live and semantic gaps explicit.

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
| intent | client command button | low-level `+feed`/`-feed` transport; first press starts, second press releases |
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

That general predicate is also the world-policy boundary. Its terminal branch rejects Elysium
(`m_nAreaType == 2`) or a world with `nofrenzyarea` set, so the request never reaches victim
selection, `StartGrappleAttack` or `FeedBegin`. The server owns this refusal; the HUD icon is only a
replicated presentation of the world state. `[VtMB]`

The released-button bit is also published by the server's general input-edge event surface at
`0x10118db0`. That body does **not** call `FeedInterrupt` directly. The accepted action has its own
continuation latch and exits through paired state, animation-event and interruption policy; the
exact native route from a second press into that latch still needs a live trace. `[VtMB]`

The accepted gameplay interaction is toggle-style, not hold-to-feed: the first Feed press makes
one acquisition request, releasing the physical button does not stop an accepted pair, and a
second Feed press requests the ordinary release family. The following release edge is inert again.
That interaction contract is owner-confirmed; it does not turn the low-level held bit into paired
action state, and it does not claim the still-untraced native call path behind the second press.

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
2. A target whose `ResistsFeeding` predicate is false also accepts without a roll. The NPC stat
   template value `General.FastFood` resolves through inherited stat-template inheritance to
   `ResistsFeeding == false`; this is general template behavior, not a tutorial-specific special
   case.
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
| `5116` | ordinary attacker feed-loop cycle 0, option `force_feeding_emitter` | starts one mouth-attached feeding burst |

Thus animation does gate the **start** of the authoritative transaction at the bite and helps gate
its release, but no model event transfers one unit of blood. Once event 4007 has called
`FeedBegin`, server updates call `Feed()` against `m_flNextFeedPulse` independently of feed-loop
cycles. The loop may repeat without a pulse on its boundary, and a pulse may occur part-way through
a loop.

#### Event 5116 is a repeating mouth burst

The eight ordinary male/female, short/tall-victim, front/back attacker feed-loop sequences carry
event 5116 at cycle 0; the complementary victim sequences do not. `C_BaseAnimating::FireEvent` at
`client.dll` `0x100935a0` resolves the attacker's `mouth` attachment (with the event handler's
generic attachment fallback), takes its transform and asks `0x100b47b0` to create the named effect
on that model. Empty options, a missing attachment and an unknown particle name all have explicit
diagnostics. `[VtMB] [model]`

`force_feeding_emitter` is a non-looping ten-frame parent definition. Each event occurrence emits
20 `force_feeding_fx1` particles at radius zero and 10 `force_feeding_fx2` particles within radius
3/theta 20. Both children are red blood-spray sprites: `fx1` is the short, broad burst (10 lifetime
frames, size 4--6); `fx2` is the narrower moving spray (60 lifetime frames, size 1--3, initial
elevation speed 40--60 followed by -100 to -130, plus lateral velocity). The definition states
lifetime frames rather than a playback rate, so it does not by itself pin lifetime in seconds.
Every feed-loop wrap can therefore start a fresh one-shot mouth burst; `FeedInterrupt` owns no
particle handle and issues no particle stop. Leaving the loop prevents further event occurrences,
while particles already emitted finish their authored lifetimes. `[script/data] [VtMB]`

### Representative clip timing

These are decoded metadata for the ordinary front variants and representative exceptional modes in
the shared banks. They are useful reproduction targets, not proof of live playback rate or blend
time. The two ordinary roles must select complementary height cells: an attacker using
`tallvictim` pairs with a victim using `shortattacker`, and `shortvictim` pairs with
`tallattacker`. `[model]`

| Family / sequence | Frames at 30 fps | Nominal `frames / fps` | Boundary event cycle |
|---|---:|---:|---:|
| ordinary engage, either front height pair | 16 | 0.533 s | — |
| ordinary bite, either front height pair | 19 | 0.633 s | `4007 @ 0.0` |
| ordinary feed loop, either front height pair | 61 | 2.033 s | `5116 @ 0.0` |
| ordinary release, attacker/short-victim/front | 73 | 2.433 s | `4006 @ 0.305556` |
| ordinary release, attacker/tall-victim/front | 68 | 2.267 s | `4006 @ 0.328358` |
| rat engage | 34 | 1.133 s | `4007 @ 0.818182` |
| rat loop | 61 | 2.033 s | — |
| rat release | 58 | 1.933 s | `4006 @ 0.631579` |
| seductive engage | 166 | 5.533 s | `4007 @ 0.951515` |
| seductive loop | 60 | 2.000 s | — |
| seductive release | 62 | 2.067 s | `4006 @ 0.508197` |

Male and female shared banks answer the same activity vocabulary. Several zombie clips differ in
length between banks, so the activity/state contract is stable while exact clip duration remains
model-authored.

### Captured ordinary presentation sequence

The retail recording shows a shorter female player feeding from the front on a taller male victim,
pinning the ordinary pair to these complementary sequence families: `[capture] [model]`

```text
feeding_attacker_tallvictim_front_{engage,bite,feed_loop,feed_release}
feeding_victim_shortattacker_front_{engage,bite,feed_loop,feed_release}
```

The visible ordering is:

1. Focus shows the ordinary interaction cursor; accepted engage replaces it with the victim's
   horizontal blood meter at the top of the screen. The left/right player HUD remains visible.
2. Engage, bite and the repeated feeding loop hold both bodies as one front-facing pair. The
   dedicated feed camera automatically orbits and rises; rendered mouse look does not steer this
   path. As its weight reaches one, the ordinary scene is replaced by a desaturated feed pass inside
   a feathered circular spotlight, with pure black outside the mask.
3. Event `4006` lies about 0.744 seconds into the 68-frame tall-victim release. At that boundary the
   feed transaction ends and the camera/feeding-view weight begins its one-second return, while both
   actors visibly finish the remaining release pose for about 1.52 seconds.
4. The partially drained victim meter remains briefly after the pair releases while the victim is
   still focused, then disappears on focus loss (visible through roughly 24.6 seconds and gone by
   roughly 24.8 seconds in this recording).

The recording visually supports reticle suppression, side-HUD retention, paired pose continuity,
camera-boundary ordering and focus-owned meter persistence. Native camera and renderer paths close
the orbit, rendered-look lock, blend curve and black isolation mask; this is not occluding geometry,
scene lighting or a generic full-screen UI vignette. Exact suppression of non-look gameplay inputs
still needs a joined input trace.

### Dedicated feed camera and feeding-view pass

The ordinary client camera is not merely forced third person. Camera weight `+0x138` ramps linearly
from 0 to 1 at one unit per scaled second while the feed state is active and back down at the same
rate after it clears. Its consumer applies `SimpleSpline(w) = w^2(3 - 2w)`, so entry and exit each
take one player-time-scale second with an eased spatial blend. The updater captures the current view
yaw when entry begins; the dedicated solver at `client.dll` `0x100fe7f0` then derives the whole
ordinary path from elapsed time and `camfeed_*` values. No live mouse/look angle enters the desired
camera solve, so look input does not steer the rendered feed camera. The exact formula and defaults
are owned by `docs/vtmb/camera-view-modes.md`. `[VtMB]`

The same feed weight drives `CViewRender::ViewDrawScene` at `0x1019b370`. Below weight 1 it draws the
ordinary view and then `CViewRender::DrawFeedingView` (`0x1019b030`) as a transition pass; at weight
1 the feeding view replaces the ordinary pass. `CViewRender::Init` (`0x1018f630`) loads the
`effects/desaturatespotlightmodel*` material family. Their `DesaturateSpotlight` shader reads
`effects/spotlight`, a 128x128 white feathered circle on black, and the corresponding world
materials provide the desaturated scene inside it. `DrawFeedingView` updates `$lightorigin` from the
local player/view point before rendering. This pins the capture's stable circular isolation as a
renderer effect centered on the feed action, not a HUD widget. A debug `feedvision` cvar contributes
`0.1 * value` to the effective weight; ordinary play gets its value from the camera/input feed
weight. `[VtMB] [script/data] [capture]`

### Sound and heartbeat state machine

`CBaseCombatCharacter::GrappleSound` at `vampire.dll` `0x1033b100` maps integer commands onto six
scheme cues and a direct heartbeat loop. The ordinary path is: `[VtMB]`

| Boundary | Attacker | Victim |
|---|---|---|
| paired engage accepted (`StartGrappleAttack`, `0x10328df0`) | start `Feed_On_Start` | start `Fed_Upon_Start` |
| bite boundary (`FeedBegin`, `0x10339d90`) | start `Feed_On_Loop` | start `Fed_Upon_Loop`; start `Interface/heartbeat_loop.wav` |
| transaction end (`FeedInterrupt`, `0x1033a9e0`) | stop `Feed_On_Loop`; start `Feed_On_End` | stop heartbeat; stop `Fed_Upon_Loop`; start `Fed_Upon_End` |

The scheme data binds each start/loop/end name to the matching attacker or victim bite, feed-loop
and release activity family. The heartbeat is emitted on the victim-bound sound channel with volume
1.0, attenuation 0.8 and pitch 100; command 9 stops that same direct wave. Its lifetime therefore
belongs to `FeedBegin`/`FeedInterrupt`, not animation-loop repetition. `[script/data] [VtMB]`

First-difference normalized correlation against the recording independently pins the audible
ordering: both start assets begin at 11.264853 s, both feed-loop assets and the heartbeat begin at
11.674830 s, and both end assets begin at 22.024399 s. The strongest individual matches are the
attacker start (`0.7719`), victim loop (`0.5395`), heartbeat (`0.2886`) and attacker end (`0.2434`).
The roughly 0.410-second start-to-loop separation is the bite transition; the native calls, not the
correlation alone, pin the stop operations at teardown. `[capture/audio] [VtMB]`

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

The same accepted ordinary feed pulse is also a player-law producer. For a player feeder it raises
supernatural activity to 2 and criminal activity to 3, each for an explicit two seconds. It opens
the victim NPC's criminal and supernatural observation windows for three seconds, allowing that
NPC's ordinary condition-gathering pass to compare the new player act counts with its authored
`pl_*` thresholds. A qualifying schedule later submits the witnessed incident; `Feed()` itself does
not mutate Masquerade or spawn police. The interrupted-feed path opens the same victim windows and
raises only criminal activity 1 for two seconds. `[VtMB]`

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
4. otherwise puts the victim into the post-feed trance (below);
5. calls the victim feed-end callback;
6. clears the target handle and current interval.

### Step 4, decoded — the post-feed trance

Step 4 is one call, and it is what a player sees as the fed-upon NPC standing entranced for a while
before resuming its life:

```c
// FeedInterrupt 0x1033a9e0, after reading BloodPool (stat index 0xc)
if (BloodPool >= 1) {
    npc = victim->+0x98;                                // cached NPC downcast
    if (npc && npc->vtbl[404](attacker) != 1) {         // IRelationType != D_HT
        npc->vtbl[614]();                               // 0x102c23f0 — think timers := curtime
        npc->SetSchedule(0xfb, false);                  // SCHED_TROIKA_MESMERIZED
    }
}
```

Slot 404 is `IRelationType`; `CAI_BaseNPCTroika`'s override is `0x10299da0` and `1` is `D_HT`. **A
victim that already hates its attacker gets no trance** and returns straight to what it was doing,
which is why feeding on a hostile mid-combat looks nothing like feeding on a civilian. Slot 614
(`0x102c23f0`) stamps `m_flNextUpdateThink` and four sibling timers to `curtime`, so the schedule
takes effect on the very next AI pass rather than waiting out the current one. `[VtMB]`

`SCHED_TROIKA_MESMERIZED` (0xfb, blob at `0x105e6f40`):

```text
Tasks       TASK_MAKE_OBLIVIOUS    TRUE
            TASK_SET_NPC_FLAG      NPCFlag:D_IS_BUSY
            TASK_SET_NPC_FLAG      NPCFlag:DONT_INVESTIGATE
            TASK_SET_NPC_FLAG      NPCFlag:NO_DIALOG
            TASK_SET_ACTIVITY      ACTIVITY:ACT_DISPOSITION_MESMERIZED
            TASK_WAIT              30
            TASK_WAIT_RANDOM       120
Interrupts  COND_LIGHT_DAMAGE  COND_HEAVY_DAMAGE  COND_REPEATED_DAMAGE
Flags       DELAY_INTERRUPTS
```

**30 + uniform(0, 120) seconds**, so 30 to 150, standing in `ACT_DISPOSITION_MESMERIZED`: sensing
nothing at all (`TASK_MAKE_OBLIVIOUS` gates the whole sense pass), taking interest in nothing,
unconversable, refused an ordinary schedule, and breakable only by damage. The task semantics, the
complete NPC flag vocabulary, and the schedule-change virtual that unwinds all of it are recovered
in `docs/vtmb/npc-ai/schedule-kernel.md` → "The incapacitation tasks and the NPC flag word";
`DELAY_INTERRUPTS` is decoded in the same document under "Interrupt conditions". The program carries
no teardown tasks and needs none — the next schedule the victim is given releases every bit and the
obliviousness refcount together. `[VtMB]`

This is the **only** producer of `SCHED_TROIKA_MESMERIZED` in the shipped game: no other
`SetSchedule(0xfb)` site exists in `vampire.dll`, and no script, `disciplinetgt` record or vdata
file in the install names the string. `[VtMB] [script/data]`

**When the program starts.** Not while the victim is still paired. `CAI_BaseNPCTroika::NPCThink`
(`0x10292de0`) calls `RunAlternateAI` (`0x1028fd80`) and runs `RunAI` (slot `0x6c0`) only when it
returns false; `RunAlternateAI` returns **true** while the grapple partner at `+0x1538` is valid and
this body's role at `+0x153c` is 1 (the victim), doing nothing but `AutoMovement` for the paired
activities. So the schedule installed by `FeedInterrupt` runs its first task on the first think
after `LeaveGrappleState` clears the pair — after the authored release clip, not over it. The port's
`TickFeed` early-return for the victim is the same gate by the same mechanism. `[VtMB]`

**How it ends.** `CAI_BaseNPCTroika::SelectSchedule` (`0x102af660`) case 1 opens with
`if (IsBusyWithDiscipline() || m_bInChoreoScene) return 0x6b`. When the two WAITs run out the NPC is
still `D_IS_BUSY`, so it selects `SCHED_TROIKA_IDLE_DISPOSITION`, and that install's schedule-change
virtual is what releases the three flags and the obliviousness. One hop through 0x6b, then ordinary
selection. The port's `SelectIdleSchedule` step 1 carries the same test. `[VtMB]`

Two consequences worth stating because they close loops elsewhere in this document. The trance sets
`ACT_DISPOSITION_MESMERIZED`, which is the first of the four automatic feed-acceptance states in
"Target acquisition and acceptance" — so **a victim still in its post-feed trance can be fed on
again with no opposed roll**. And `m_iIsOblivious > 0` is the same field `docs/vtmb/stealth.md`
names as the backstab arc override, so a tranced victim is stealth-killable from any angle.

The NPC callback surface exposes `OnFedUponBegin` and `OnFedUponEnd`; `FeedBegin` brackets the
victim with the start callback and `FeedInterrupt` brackets it with the end callback. Those outputs
are what the tutorial blueblood's maker wiring consumes. The exact caller/activator identity and
maker-child forwarding order still require the controlled tutorial trace.

The recreation's narrow depleted-victim outcome collapses the native death lifecycle into an
immediate `OnKilled` call. It must enqueue `OnFedUponEnd` before that collapsed call, so the single
equal-time FIFO leaves the consequent `OnDeath` outputs later and terminal. This is an implementation
ordering correction, not a map-specific exception: the tutorial blueblood authors distinct success
and death assignments on those two callbacks, and the death assignment must not be overwritten by
the success callback.

Confirmed interruption producers include:

- release event `4006` on the authored paired release sequence;
- incoming player damage while paired in ordinary or seductive feeding, before damage commit;
- paired sequence resolution failure or normal paired state completion;
- invalid feed target/state detected by the common grapple lifecycle.

The exact native second-press route is not closed by static command inspection alone: `-feed`
updates the command button and publishes a release edge, but does not directly call
`FeedInterrupt` in that publisher. The recreation therefore maps the second press to the paired
continuation latch and still exits through the release activity and event 4006.

## Recreation contract

The narrow first faithful slice is ordinary player-on-humanoid feeding, including the tutorial
blueblood. It needs:

1. A toggle-style `Feed` action over the low-level button pair: the first press makes one request,
   release is inert, and a second press clears paired continuation; input still maps to the retail
   `0x00400000` intent rather than directly playing a montage.
2. A target query and acceptance service that retains automatic states, `ResistsFeeding`, the
   asymmetric Brawl/Hacking decision and stealth override as separate verdicts.
3. One paired-action owner that aligns two actors and resolves role/size/front-back activities
   from the model-derived activity catalog.
4. Native-equivalent ordinary state transitions through engage, bite, loop and release.
5. An animation-event bridge where 4007 opens `FeedBegin`, 4006 ends the transaction and starts the
   camera/view return while the authored release pose finishes, and every attacker feed-loop event
   5116 emits one mouth-attached blood burst whose particles finish independently.
6. A server-simulation-clock transaction using the exact 0.30/0.15 cadence and explicit feed
   state fields; animation time never substitutes for the next-pulse deadline.
7. Atomic feeder/victim blood and health changes plus a single idempotent teardown path.
8. Stable victim and maker ownership so `OnFedUponBegin` and `OnFedUponEnd` reach authored map
   wires with the correct caller and activator.
9. Save/restore of an in-progress feed without duplicating a pulse or losing the victim link.
10. A semantic Feed camera lease that reproduces the recovered automatic orbit/rise and one-second
    spline-weighted entry/exit, locks rendered look, retains the second Feed press, and drives the
    desaturated feathered-spotlight renderer pass rather than a HUD vignette. Exact non-look input
    suppression remains a separate gate.
11. Reticle suppression, side-HUD retention, and a top victim-blood meter retained until
    post-release focus is lost.
12. Attacker/victim start, loop and end cues at their recovered boundaries, with the victim-bound
    heartbeat starting at `FeedBegin` and stopping at `FeedInterrupt`.

No `.vcd` reader, choreography timeline or exact sequence-label command is a dependency of that
slice. Seductive, rat and zombie modes should extend the same paired and transaction services
rather than introduce separate montage-owned implementations.

## Open verification gaps

- Capture one instrumented ordinary humanoid feed joining command down/up, grapple mode/role,
  activities, sequences, events 4007/4006/5116, timer fields, both BloodPool values, health and
  outputs. The retail video closes the visible sequence and UI ordering only.
- Trace the exact native route by which the second Feed press clears continuation and transitions
  into a release activity; static `-feed` inspection proves only that button-up is not teardown.
- Name and capture every exceptional trait-effect branch, especially Nosferatu rat gain, Ventrue
  rat rejection and the two feed-bonus histories.
- Resolve the exact depleted mortal/Kindred/unkillable victim outcome matrix, humanity and
  Masquerade consequences, and frenzy interaction.
- Trace exact non-look gameplay-input suppression while paired. Camera/look authority, the
  feeding-view mask and blend, sound/heartbeat calls, and event-5116 particle ownership are closed
  statically; the joined instrumented capture above remains their live acceptance.
- Verify seductive, rat and zombie modes independently; mode presence and model timing do not
  prove that every branch is reachable in ordinary retail play.
- Verify `OnFedUponBegin`/`OnFedUponEnd` caller, activator, maker forwarding and save/load behavior
  on `sp_tutorial_1`.

## `CFeedBar` — the victim blood meter

Recovered in full. The meter is a `vgui` panel on the client HUD; nothing about its lifetime is
owned by focus, by the grapple, or by the camera.

### The value channel

`CBasePlayer::UpdateClientActionState` (`vampire.dll` `0x101755d0`) is the only producer. It runs on
the ordinary HUD update, not on a feed event, and sends the one-byte `FeedBar` usermsg
(registered at `0x10350340`, message id `DAT_10726060`, payload width 1) **only when the value
differs from the cached `m_iClientFeedBloodPool` at `+0x1a8c`**. Two arms select the entity whose
`BloodPool` (stat slot 12, `CVStatList_t` index `0xc`) is sent, in this order:

1. `m_hFeedTarget` (`+0x149c`) when its handle still resolves; otherwise the grapple partner
   (`+0x1538`) while this player is the role-0 half (`+0x153c == 0`) and the continuation latch
   (`+0x14a8`) is set. This is the feed arm.
2. Otherwise the **active weapon's blood source** — weapon vfunc `0x474` -> `+0x9c` — read at
   `0x10175bbb`. The same panel doubles as a carried blood source's readout. This runtime has no
   blood-source weapon, so that arm has no producer and is left saying nothing.

Both arms mask a negative to zero (`((int)v < 1) - 1 & v`) before sending.

The denominator is separate and is not sent with the value: `CFeedBar::vfunc114` seeds it with the
literal `0xf` and overrides it with the player's replicated `m_iClientFeedMaxBloodPool`
(`+0x14cc` client / `+0x1a90` server) when that is non-zero. That field is written in exactly one
place — `CBaseCombatCharacter::EnterGrappleState` `0x10329760`, from the VICTIM's char-template
`Attributes[BloodPool]` (`template+0xd0` `+0x30`) — and nothing ever clears it, so the meter keeps
the denominator the grapple opened with for the rest of the panel's life. Slot 13 `BloodPool_Max`
has no reader anywhere in the image.

### The value gate and the hold — `CFeedBar::vfunc114` `client.dll` `0x100503d0`

Signature `(int iValue, bool bShow)` (`RET 0x8`; `EDI = [ESP+0x10]` is the value, `AL = [ESP+0x14]`
the show flag). In order:

1. `+0x1b0 = curtime` — when this update happened.
2. `+0x1ac = 15`, then the replicated maximum when non-zero.
3. If the client-side HUD-suppression latch (`0x100a5a80`) is set, the value is forced to 0 and the
   deadline is pushed into the past.
4. `DevMsg(2, "FeedBar: %d\n", value)`.
5. **`value >= 15`** (`CMP EDI,0xf / JGE 0x100504d4`) -> percent 0, `SetVisible(0)`, return.
6. **`value <= 0`** (`JLE 0x100504b3`) -> percent 0, `SetVisible(0)`, return.
7. Otherwise `percent (+0x1a8) = value * 100 / max`, and if `bShow`: `SetVisible(1)` and
   `hide deadline (+0x1b4) = curtime + 3.0`.

The hold constant `_DAT_10227ee0` is the same rdata double `SimpleSpline` `0x100fdb30` reads as its
3, so it is exactly **3.0 seconds**. The only recovered `bShow == false` call is the constructor's
`vfunc114(15, false)` at `0x10050160`, which the upper arm already hides — every call that can
raise the panel raises it.

Consequences, both load-bearing:

- **Retail never draws an empty bar.** A victim at zero hides the panel outright. Since an ordinary
  feed always ends with the victim at zero, the meter disappears at the last pulse rather than
  sitting emptied through the release.
- **A victim standing at exactly its 15-point stat ceiling shows no meter at all** — 15 is also the
  client's default denominator, and the client refuses to draw a bar it cannot have been told the
  size of. Retail's own quirk, reproduced.

### The paint-time hide and the ramps — `CFeedBar::vfunc98` `client.dll` `0x10050560`

- When the player is not feeding (`+0x14d4` clear) and `curtime >= +0x1b4`, `SetVisible(0)` and
  paint nothing. **This is the whole of the meter's post-feed persistence: a three-second timer off
  the last value change.** There is no focus test in either function. The capture note in
  "Captured ordinary presentation sequence" step 4 above reads the disappearance as focus loss;
  the recovered code says it is this timer, and the two are indistinguishable in that recording
  because the player kept looking at the body.
- Pre-pulse anticipation (`0x100507a5`): while feeding with a running pulse interval (`+0x14d0`),
  `_DAT_103ede90 = (int)((100 / max) * clamp((curtime - +0x1b0) / +0x14d0, 0, 1))` is subtracted
  from the drawn percent, so the bar slides one blood point's worth down across each interval
  instead of stepping. The counter is reset to 0 by `vfunc114` whenever the value goes **up**.
  When not feeding, `+0x1b0` is re-stamped to curtime each paint so the ramp stays at zero.
- Alpha (`+0x1a0`) ramps +/-5 per frame between 0 and 255, driven by the latches at `+0x1a5`/`+0x1a6`.
  What sets those latches is **not recovered**.
- Sprites `hud/BloodBar_Empty` and `hud/BloodBar_Full` (512x64), the fill clipped from the right at
  `0x1b3 - percent * 0x168 / 100`.

### Port

`ElysiumFeedBar::Update` in `Source/ElysiumUE/Public/ElysiumViewState.h` is the whole contract as
one total function over `FElysiumFeedView`; `UElysiumPresentationSubsystem::Publish` supplies the
source in retail's precedence and `UElysiumHUDWidget` draws the published fraction and derives
nothing. `Elysium.Substrate.UI.HUDFeedBar` asserts the window, the hold, the change gate and the
anticipation.

DEFECT CLOSED. The publisher previously owned the panel with gameplay ownership — visible while
`FElysiumFeedState::IsPaired()`, then retained while the released victim remained the focused
usable. `FElysiumCombatCharacter::ShouldReleaseFeed` selects the release family exactly at
`BloodPoolValue() < 1`, so every completed feed reached Release and ReleaseTail with the victim at
zero while the pair was still held, and the focus branch then held that same zero-valued view for
as long as the drained body stayed in view. The panel the player saw was therefore an empty one,
for the whole tail of every feed and indefinitely after it. Neither owner exists in retail.

Named modernizations:

- The alpha ramp is not ported as a 51-step per-frame integer fade; the panel is drawn at full
  opacity and Slate's own visibility flip replaces it. The latches that drove it are unrecovered,
  so there is nothing faithful to reproduce.
- The anticipation is published as a float fraction rather than retail's truncated integer percent,
  and it rides the substrate clock rather than the frame.

Still unrecovered: the `+0x1a5`/`+0x1a6` alpha latches' producer, and the blood-source weapon arm of
the value channel (no producer in this runtime).

## Reproducible evidence

| Surface | Entry points / command |
|---|---|
| client command registration and bit packing | `client.dll` `0x10105a90`, `0x10105ae0`, `0x10102230`, `0x10102240`, `0x10104460` |
| request, target and check | `vampire.dll` `0x10168320`, `0x101690b0`, `0x10168910`, `0x101728f0` |
| paired routing | `0x10328df0`, `0x1032a100`, `0x10164240`; `uv run elysium research player_grapple_survey` |
| animation event boundary | `CBaseCombatCharacter::HandleAnimEvent` `0x1032e330`; `uv run elysium research animation_event_survey` |
| event-5116 particle bridge | `client.dll` `C_BaseAnimating::FireEvent` `0x100935a0`, creator `0x100b47b0`; `force_feeding_{emitter,fx1,fx2}` data |
| transaction | `FeedBegin` `0x10339d90`, `Feed` `0x1033a400`, `FeedInterrupt` `0x1033a9e0` |
| victim blood meter | `client.dll` `CFeedBar::vfunc114` `0x100503d0`, `vfunc98` `0x10050560`, ctor `0x10050160`; sender `vampire.dll` `CBasePlayer::UpdateClientActionState` `0x101755d0`, usermsg table `0x10350340` |
| feed camera | `client.dll` weight updater `0x100fc900`, ordinary solver `0x100fe7f0`, blend helper `0x100fe600` |
| feeding-view renderer | `client.dll` `CViewRender::ViewDrawScene` `0x1019b370`, `DrawFeedingView` `0x1019b030`, `Init` `0x1018f630`; `effects/spotlight` |
| feed sound state | `vampire.dll` `StartGrappleAttack` `0x10328df0`, `FeedBegin` `0x10339d90`, `FeedInterrupt` `0x1033a9e0`, `GrappleSound` `0x1033b100`; `vdata/system/sndscheme_char.txt` |
| blood and health helpers | `IncBloodPool` `0x10338cb0`, `DecBloodPool` `0x10338df0`, `HealthHeal` `0x1033c340` |
| tracked native context | `research/cases/core-mechanics/specs/core_mechanics_server.json` |
| model sequence/frame inventory | `uv run elysium research inventory_player_animations` |
| ordinary retail visual capture | `E:\gamecapture\feed.mkv` (2560x1440, 60 fps, 34.167 s) |

All decompilation, decoded model inventory and game-derived reports remain under
`$ELYSIUM_WORK_ROOT/research/`; only the reproducible specifications and engine-neutral findings
are tracked.

## Port status - 4006/4007 now arrive from the clip

The note in `ElysiumFeed.cpp` that called the animation-event bridge "a scheduler, not a notify
listener" described the state before the character bake carried sequence events. It now does
(`UElysiumClipData::Events` rides every native `UAnimSequence`), so
`FElysiumCombatCharacter::HandleAnimEvent` claims 4007 and 4006 for real, on retail's own guards -
the paired partner at `+0x1538` resolving and the paired role at `+0x153c` being 0, the ATTACKER
half - and calls `OnFeedAnimEvent` from the authored record. The vtable slots are named:
`+0x57c` is `CBaseCombatCharacter::FeedBegin` `0x10339d90` and `+0x584` is
`CBaseCombatCharacter::FeedInterrupt` `0x1033a9e0`. Full recovery:
`docs/vtmb/animation_events.md` -> "The combat-character band".

**The phase scheduler is kept as the FALLBACK, not removed.** `feeding.md`'s own recreation
contract requires engage -> bite -> loop -> release to run with nothing rendered, and that is
exactly what the schedule carries: a headless world, a bodiless character, a channel publishing no
phase, or a clip whose timeline declares no boundary record.

`FElysiumCombatCharacter::FeedBoundaryArrivesFromAnimEvent` is what decides between them - the same
question `FElysiumWeapon::CommitArrivesFromAnimEvent` already asks of a shot commit. It resolves the
current phase's clip pair, asks the embodiment which owner owns that label (the feed families live
in the shared banks), checks a polled channel is standing on it, and scans that timeline for the id.
**Only a positive answer stands the scheduled raise down.** Every "cannot tell" keeps the schedule,
because a boundary that fires twice is inert and a boundary that never fires hangs the transaction.

Exactly one FIRING reaches the transaction, and it is guarded on both sides:

- 4007 - the schedule stands down when the bite clip carries the record, and `FeedBegin` refuses
  anyway while `FeedState.Target` is set (retail's own `m_hFeedTarget` guard). The Bite -> Loop arm
  is the backstop: if the stand-down was taken and no record opened the transaction by the time the
  bite clip is over (the channel was taken over before the next dispatch pass, or the clip was
  displaced), it raises 4007 itself there, and ends the grapple if `FeedBegin` still refuses -
  otherwise the pair would enter Loop with no transaction and `ShouldReleaseFeed` would hold on a
  victim still carrying blood.
- 4006 - the schedule stands down when the release clip carries the record, and the notify moves the
  phase to `ReleaseTail`, after which `AdvanceFeedPhase`'s `Release` arm is unreachable.

For 4006 the stand-down deliberately leaves `PhaseDeadline` at the authored event estimate rather
than re-arming it, because `CompleteFeedTransaction` anchors the presentation-only release tail to
that value so a late observation consumes pose time instead of extending the animation. The arm is
therefore re-entered on each feed think until the notify lands - bounded by the release clip, and
self-healing, since a clip that stops being dispatched makes the predicate false and the schedule
fires. `Elysium.Substrate.Feeding` and `Elysium.Substrate.FeedMakerOutputs` are unchanged and still
green: with no clip phase published, the predicate is false and the scheduler behaves exactly as
before.

## The auto-accept test reads the ideal activity (2026-09-08)

`AttemptFeed` (`0x10168910`) tests the victim's **`m_IdealActivity`** (`+0xff0`), not the playing
activity, against exactly `ACT_DISPOSITION_MESMERIZED 0x104e`, `ACT_DISORIENTED 0x1068`,
`ACT_LOST 0x1069`, `ACT_COWER 0x1098` ("Feed attempt! Success is automatic because defender is
mesmerized, disoriented, lost or cowering", `0x105865a8`); anything else goes through the
`ResistsFeeding` gate and the opposed roll. Because `TASK_PLAY_COWER`/`TASK_SET_COWER` pick
`ACT_COWER`, `COWER2` or `COWER3` by `m_iCowerAnimOffset` (`+0x6414`), only one cowering NPC in
three auto-accepts, and the `ACT_COWER_INTO` window never does. The producers of the four
activities are in `npc-ai/programs.md`, "The flee state and the cower, disoriented
and lost programs".
