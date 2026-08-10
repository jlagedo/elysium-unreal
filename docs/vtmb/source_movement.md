# Source movement + camera (VtMB tuning)

The numbers behind first-person game feel: view angles, FOV, walk/run speed,
acceleration, gravity, jump. VtMB runs an early-Source `CGameMovement`, so the
*math* is stock Source; the *constants* are Troika's and several differ from
Half-Life 2.

## Provenance

Every value here is read out of the user's own install, not from HL2 lore:

- `cfg/default.cfg` (inside `pack000.vpk`, read via `pipeline/src/elysium_pipeline/formats/vpk.py`) — Troika's
  authored defaults + the key binds.
- ConVar defaults compiled into `Vampire/dlls/vampire.dll` and
  `Vampire/cl_dlls/client.dll`. MSVC emits each registration as
  `push <flags>; push <default-string>; push <name-string>; mov ecx,<ConVar*>; call ConVar::ConVar`,
  so name, default, and the ConVar object address all fall out of a byte scan of
  `.text`.
- Decompiles of `CGameMovement` (RTTI vftable `0x10462874` in `vampire.dll`) via
  `$ELYSIUM_WORK_ROOT/research/ghidra/`.

A ConVar object is read through **base+4**, not base+0 (`ConVar` carries a second
vtable for its `IConVar` base, shifting the subobject the code holds a pointer
to). `m_fValue` sits at +0x28 from that pointer. Scanning `.text` for `base+4`
therefore enumerates every consumer of a ConVar — and finding **zero** hits means
the ConVar is dead (see below).

`cfg/valve.rc` does **not** exec `default.cfg` (the line is commented out), so at
boot the compiled-in ConVar defaults are what apply; `default.cfg` is what the
engine seeds a fresh `config.cfg` from.

## Where the move runs

`CGameMovement` is driven from `CPlayerMove::RunCommand` (`0x101874a0`), one call per
`CUserCmd`, while the engine drains the client's `clc_move` message — **before** the server
frame runs a single think. The move integrates on the command's rebound `frametime`/`curtime`,
not the frame's. Full chain, addresses and the ordering evidence: `docs/vtmb/game_runtime.md` §1.

## Frame timing — the host bound, and why there is no tick

`_Host_RunFrame` (`engine.dll` `0x2008e450`) gates its entire body on
**`Host_FilterTime` (`0x2008ba30`)**, which decides whether a frame runs at all and, if it does,
sets `host_frametime`. Read out in full:

```
realtime += dt
if (fps_max is set and != 0) {
    fps = clamp(fps_max, 0.1, 1000.0)          // MIN_FPS / MAX_FPS
    if (!recording && realtime - oldrealtime < 1.0 / fps)
        return false                            // too early — no frame this pass
}
host_frametime = realtime - oldrealtime
oldrealtime    = realtime
host_frametime *= timescale                     // host_timescale
host_frametime  = min(host_frametime, timescale * 0.1)     // MAX_FRAMETIME
host_frametime  = max(host_frametime, timescale * 0.001)    // MIN_FRAMETIME
```

**The bound is `[0.001, 0.1]` seconds** — a hard 10 fps floor on a single frame's delta. Both
constants are `double` in `.rdata`: `0x201736e0` = `0.1`, `0x20173650` = `0.001`
(`0x20174070` = `1000.0` is `MAX_FPS`). So a hitch — a level-load flush, an alt-tab, a
debugger break — can never hand the game more than 100 ms of simulation in one frame.

`host_framerate` replaces the measurement outright rather than bounding it: when set, it wins and
the clamp is skipped.

**There is no fixed tick, and no accumulator.** `Host_FilterTime` bounds a *variable*
`host_frametime` and returns; nothing accumulates a residual or runs the game N times at a fixed
interval. This agrees with `docs/vtmb/game_runtime.md` § "Time model", which pins VtMB to the pre-tick
Source branch on the absence of `interval_per_tick` / `sv_tickrate` / `TICK_INTERVAL`. 66.7 Hz is
*modern* Source's default tickrate and has no presence in this build.

**The consequence for movement, measured.** Because every `CGameMovement` formula integrates on
that variable delta, some of retail's movement is frame-rate dependent — but **less of it than the
shape of the code suggests**, and the jump is not among it:

- **The gravity integration is exact and rate-invariant.** The half-step split plus the *additive*
  jump impulse (below) form a velocity-Verlet integrator, which is exact for constant
  acceleration, so a given launch velocity produces the same apex at any frame rate. (This says
  nothing about the *height* — VtMB's launch is not a single impulse; see "The jump is not stock
  Source's".)
- **Air acceleration is the part that really does vary.** `AirAccelerate`'s `addspeed` clamp stops
  binding above ~117 fps, so a strafe-jump's exit speed drifts with the frame rate — measured at
  259.5 / 259.8 / 261.0 u/s across 60 / 120 / 240 Hz, about **0.6%** over a doubling-and-doubling
  of the rate.

So reproducing VtMB faithfully means reproducing a small residual air-control drift, not a
wandering jump height. Measurements are `uv run elysium debug move` + `pipeline/src/elysium_pipeline/validation/channel_diff.py --hz 60 120 240`.

*Provenance: `DumpFuncs funcs=2008ba30 depth=1` on `engine.dll`; the four constants read from
`.rdata` directly.*

## View / camera

| ConVar | Value | Meaning |
|---|---|---|
| `default_fov` | 75 | horizontal FOV at 4:3 |
| `viewmodel_fov` | 54 | first-person weapon |
| `cl_pitchup` / `cl_pitchdown` | 89 / 89 | pitch clamp, degrees |
| `m_pitch` / `m_yaw` | 0.022 | degrees per mouse count |
| `sensitivity` | 3 | multiplies both → 0.066 °/count |
| `m_filter` | 0 | no mouse smoothing |
| `m_side` / `m_forward` | 0.8 / 1 | inert — `default.cfg` ends with `+mlook` |
| `cl_bob` / `cl_bobcycle` / `cl_bobup` | 0.01 / 0.8 / 0.5 | view bob |
| `sv_rollangle` / `sv_rollspeed` | 2 / 200 | view roll while strafing |
| `v_kickpitch` / `v_kickroll` / `v_kicktime` | 0.6 / 0.6 / 0.5 | damage punch |
| `v_centerspeed` / `v_centermove` | 500 / 0.15 | auto-center |
| `cl_pitchspeed` / `cl_yawspeed` | 225 / 210 | keyboard look, °/sec |

Third-person command, cvar, transition, and solver behavior is canonical in
`docs/vtmb/camera-view-modes.md`; this section retains only first-person view and movement tuning.

**FOV is horizontal at 4:3.** Source is Hor+: the vertical angle is what holds at
every aspect, and a wider window shows more horizontally. Converting to a vertical
FOV (the convention most engines default to):

```
vfov = 2 * atan(tan(hfov/2) / (4/3))       // 75 -> 59.84
```

## Movement

| ConVar | Value | Notes |
|---|---|---|
| `sv_gravity` | 800 | units/s² |
| `sv_friction` | 4 | |
| `sv_stopspeed` | 16 | |
| `sv_accelerate` | 10 | |
| `sv_airaccelerate` | 10 | |
| — | 30 | air speed cap, hardcoded (`0x104492a8` = 30.0f) |
| `sv_stepsize` | 18 | |
| `sv_maxvelocity` | 3500 | terminal clamp |
| `sv_maxspeed` | 2048 | a ceiling, **not** the player's speed |
| `sv_jump_boost` | 25.0 | **inches of instant origin pop** on the jump's first frame — not the apex |
| `sv_jump_maxspeed` | 350.0 | `m_flMaxSpeed` while airborne |
| `sv_jump_boost_immediate` | 0 | gates the origin pop |
| `sv_walkscale` / `sv_runscale` / `sv_sneakscale` | 1.0 / 1.0 / 2.3 | function-local statics in `CHL2_Player::PreThink` |

## The jump is not stock Source's

**Troika replaced it with a held, gravity-modified, Feat-scaled "unified jump", and none of it is
a `CGameMovement` constant.** The tuning lives in `vdata/system/rules.txt` → `RuleData/Jumping`,
and the engine reads it into a settings singleton (`0x10739d08`) that `CheckJumpButton` queries
through per-field getters — `FUN_101e7d90` returns `*(float*)(obj + 0x3c8)`, the jump impulse.
Nothing in `.rdata` holds the value; the field is uninitialised in the PE and populated at load.

| `rules.txt` key | Value | Meaning (Troika's own comments) |
|---|---|---|
| `BaseJumpVelocity` | **185** | "the base velocity that is added to the player when you hit **or hold** the jump button". The file notes HL2's is 268, and that 270 was the pre-`SKS_JUMP` value |
| `JumpGravityMultiplier` | **0.75** | gravity runs at 75% for the duration of the jump |
| `JumpHoldTime` | **0.2** | s |
| `Vertical[Feat]` | 0.30 … 0.48 | Jumping-Feat → vertical velocity modifier, ranks 1–10 |
| `Horizontal[Feat]` | 1.05 … 1.50 | ditto, horizontal |
| `JumpDuration[Feat]` | 0.11 … 1.10 | "how long you can hold the jump button down for increased effect", s |
| `SafeFallDist` / `FallDistPerDmg` / `SupernaturalFallDist` | 240 / 8.4 / 500 | the fall-damage curve |

So the jump height is **not** a single constant and **not** frame-symmetric: the button is held,
velocity keeps being added while it is, gravity is reduced throughout, a Feat scales both the
magnitude and the hold window, and `sv_jump_boost` teleports the origin up 25 inches on the first
frame on top of all of it. A rank-1 (tutorial) character therefore clears far more than the ~25
units a single 185 u/s impulse against full gravity would give.

### Where each field lands

`FUN_101e6310` parses the `Jumping` block into the settings singleton, and `CheckJumpButton`
reads one of those fields back through `FUN_101e7d90`:

| Field | Key | Read by |
|---|---|---|
| `obj+0x3c8` | `BaseJumpVelocity` | **`CheckJumpButton` — this is the impulse it adds** (getter `0x101e7d90`, called from `0x1012287a`) |
| `obj+0x3cc` | `JumpGravityMultiplier` | applied to the player's own gravity scale |
| `obj+0x3d0` | `JumpHoldTime` | the push window |
| `obj+0x3e0` / `+0x3e4` / `+0x3e8` | `SafeFallDist` / `FallDistPerDmg` / `SupernaturalFallDist` | fall damage |
| `obj+0x3ec` / `+0x3f0` | — | `sqrt(2 · sv_gravity · SafeFallDist)` and the supernatural equivalent, precomputed at load |
| `obj+0x20` / `+0x48` | `Vertical[1..10]` / `Horizontal[1..10]` | the Feat tables |

The gravity scale is the player's stock Source `m_flGravity` (`player+0x3ec`), which
`StartGravity` / `FinishGravity` / `AddGravity` each multiply by, defaulting to `1.0` when zero.
`player+0x2314` next to it is `m_flWaterJumpTime`, not a jump timer — `FinishGravity`'s early-out
on it is Source's stock water-jump guard.

### The model

**The jump is a constant upward push sustained while the button is held, under reduced gravity,
after an instant origin pop.** On the press frame: `origin.z` pops up `sv_jump_boost × 0.99 ×`
the hull-trace fraction, `velocity.z += BaseJumpVelocity × groundFactor`, and the player's gravity
scale becomes `JumpGravityMultiplier`. For as long as the button stays down inside the window, the
push keeps being applied — which is what `rules.txt` means by *"added to the player when you hit
**or hold** the jump button"*. Releasing early ends it, so a tap and a hold give different
heights; players know this as the spacebar jumping higher than the mousewheel, which cannot be
held.

**Unverified: which window the engine uses.** `rules.txt` carries two candidates —
`JumpHoldTime` (a flat `0.2`) and the Feat-indexed `JumpDuration` (`0.11` at rank 1) — and the
code that selects between them has not been read. The Feat itself is moot in the shipped game:
**Athletics and the Jumping feat are both unused content**, so every character is effectively
rank 1. Verifying this needs the RPG-layer jump driver, not `CGameMovement`.

*Provenance: `FUN_101e6310` (the `Jumping` parser), `FUN_101e7d90` (the `BaseJumpVelocity`
getter) and its call site in `CheckJumpButton`; `sv_jump_boost`'s registered help string; the
`0.99` and `0.5` scalars read from `.rdata` as doubles.*

**`CheckJumpButton` (`0x101226b0`) is additive, and that is load-bearing.** The decompile reads
`v.z = impulse * groundFactor + v.z` — it *adds* to the existing vertical velocity rather than
overwriting it. `FullWalkMove` has already run `StartGravity` by the time the jump is checked, so
the launch velocity carries the half-step offset the leapfrog integration wants; overwriting
instead discards it and makes the apex frame-rate dependent. The ground factor is the surface's
own jump scale (`surfacedata` +0x6c), 1.0 for the `default` prop every world surface resolves to;
a moving ground entity's own velocity is added on top.

`m_nOldButtons` stops a held jump from *re-firing*; it does not stop the hold from extending the
same jump, which is a separate timer on the player.

**There is no jump buffer and no coyote time.** No accumulator and no press queue exist anywhere in
the path: `CheckJumpButton` returns the instant the player is not on the ground, so a press one
frame after the feet leave a ledge is discarded outright, and a press made while falling is
discarded and never replayed on landing. The only forgiveness anywhere in the system is
`CategorizePosition`'s 2-unit down-trace, and that is a ground-*detection* tolerance for descending
a step — VtMB has no `StayOnGround`, so the trace is what re-finds the floor — not an input grace.
The two are easy to conflate because both make a jump work that "should" have failed; only the
first is about input.

### Formulas (verified against the decompile)

`Friction` — called only while on ground, and scales all three velocity
components:

```
speed = |v|                       // 3D, not horizontal
if (speed < 0.1) return
control  = max(speed, sv_stopspeed)
drop     = control * sv_friction * surfaceFriction * dt
v       *= max(0, speed - drop) / speed
```

`Accelerate`:

```
addspeed = wishspeed - dot(v, wishdir)
if (addspeed <= 0) return
accelspeed = min(sv_accelerate * dt * wishspeed * surfaceFriction, addspeed)
v += accelspeed * wishdir
```

`AirAccelerate` — the cap applies to the *target* while the uncapped `wishspeed`
still drives `accelspeed`. That asymmetry is the whole of Source's air-strafing
behaviour:

```
wishspd  = min(wishspeed, 30)
addspeed = wishspd - dot(v, wishdir)
if (addspeed <= 0) return
accelspeed = min(sv_airaccelerate * wishspeed * dt * surfaceFriction, addspeed)
v += accelspeed * wishdir
```

### `surfaceFriction` is 1.0 on every world surface

`surfaceFriction` is a field on **`CGameMovement` itself** at `+0xa0` (not on the
player), read by `Accelerate` (`0x101212e0`) and `AirAccelerate` (`0x10121000`) as
`dt * m_surfaceFriction * wishspeed * accel`, and by `Friction` (`0x10120ba0`) as
`sv_friction * m_surfaceFriction`.

`CategorizePosition` (`0x1011e560`) is its only writer. Its tail reaches the
`IPhysicsSurfaceProps` interface (through `0x1071a278`, **not** the
`VPhysicsSurfaceProps001` pointer `CPhysicsHook::Init` caches at `0x10723944`), stores
the trace's surface index at `+0x98` and its `surfacedata_t*` at `+0x9c`, then:

```
GetPhysicsProperties(m_nSurfaceProps, NULL, NULL, &m_surfaceFriction, NULL)   // vtbl +0x10
m_surfaceFriction *= 1.25                  // 0x10462914 = 1.25f
if (m_surfaceFriction > 1.0) m_surfaceFriction = 1.0
```

The `1.25` scale-and-clamp is present in VtMB's 2003 build, and it is what makes the
value **1.0 in practice**: a material's friction comes from its VMT's `$surfaceprop`
looked up in `scripts/surfaceproperties.txt`, and **Troika authored almost none**
— exactly **1 of the install's 11,624 VMTs** carries a `$surfaceprop`, and **0 of
`sp_tutorial_1`'s 597 world materials** do. Every world surface therefore resolves to
the `default` prop, whose `friction` is `0.8`, and `0.8 × 1.25 = 1.0` exactly.

So a port hardcoding `surfaceFriction = 1.0` for world geometry is not an
approximation — it is the value the retail game computes. The 35 friction-carrying
props in `surfaceproperties.txt` that differ (`ice` 0.1, `bottle` 0.2, `mud` 1.5,
`rubber` 100) are reachable only by VPhysics props, never by the player's ground trace.
There is nothing per-surface to export from the BSP.

*Provenance: `DumpFuncs range=1011e000-10128000` + `DumpConst addrs=10462914` on
`vampire.dll`; the VMT survey is over `install.build_index()`.*

### StepMove — walking up curbs and stairs

`sv_stepsize` never reaches the movement code directly: `CBasePlayer::Spawn`
(`0x1016d28c`) copies it into `m_flStepSize` at `player+0x1f24`, which is what
`WalkMove` reads (alongside `m_bAllowAutoMovement` at `player+0x1f28`).

`WalkMove` (`vampire.dll` `0x101213b0`, with `StepMove` inlined) first traces the
flat horizontal move; if it completes, that is the move. If it is obstructed the
move runs a **second** time, and the two are compared:

```
flat:  TryPlayerMove()                       -> downPos, downVel
reset to the start
up:    trace up   (m_flStepSize + DIST_EPSILON)
       TryPlayerMove()                       -> slide forward, raised
       trace down (m_flStepSize + DIST_EPSILON)
       if (trace.plane.normal.z < 0.7) -> use the flat result
keep whichever attempt covered more ground horizontally;
when the raised one wins, its Z still comes from the flat attempt
```

Nothing detects a stair. Two attempts are run and the better one wins, which is
why doorways, slopes and corners need no special cases. The `0.7` standable-normal
test is a **double** at `0x104492d0`; `DIST_EPSILON` is what stops the down trace
from arriving flush with the floor and reporting no contact at all.

**The hull must be a box.** Source's player is an AABB (`-16,-16,0`..`16,16,72`),
and StepMove depends on it: a flat bottom lands squarely on a step top, reporting a
`(0,1,0)` normal. A capsule's rounded bottom catches the step's top edge instead
and reports ~0.65 — just under the 0.7 test — so every climb is rejected and the
step is never taken.

VtMB has no `StayOnGround`: descending a step briefly leaves the ground rather than
gluing to it, and `CategorizePosition`'s 2u down-trace is what re-detects the floor.

## The hulls and the view offsets

All six vectors are literals in the `CGameMovement` constructor (`0x1011e0d0`), stored on the
mover itself and selected by the three accessors `GetPlayerMins` (`0x1011e310`), `GetPlayerMaxs`
(`0x1011e350`) and `GetPlayerViewOffset(bool ducked)` (`0x1011e390`):

| State | Field | mins | maxs | view offset |
|---|---|---|---|---|
| standing | `+0x14` / `+0x20` / `+0x5c` | `-16 -16 0` | `16 16 72` | `0 0 64` |
| **ducked** | `+0x2c` / `+0x38` / `+0x68` | `-16 -16 0` | **`16 16 36`** | **`0 0 30`** |
| observer | `+0x44` / `+0x50` | `-10 -10 -10` | `10 10 10` | — |

So the ducked hull is the standing footprint at **half the height**, and the ducked eye sits at
**30**, not the 28 that stock Source's `VEC_DUCK_VIEW` uses — a Troika value. The mins/maxs
selector keys off `m_bDucked` (`player+0x1edd`),
while the view-offset selector takes the ducked flag as an argument.

The same constructor seeds `surfaceFriction` (`+0xa0`) to `1.0`, which is the value the game then
computes every frame anyway (above).

### Ducking

**The crouch is a toggle, and the retention is state rather than the button** [observed in the
shipped game]. `CTRL` ducks on the press edge and stays ducked when the key comes up; pressing again
stands the player up, and a jump taken from a crouch lands still crouched. The decompiled surface
below is entirely consistent with that and does not settle it either way: `Duck` reads a **held**
bit off the server's button field, which a client-side latch keeps asserted exactly as a held key
would. What decides when that bit is set is `client.dll`'s `CInput`, which is not decompiled.
So the toggle lives at the client input layer, not in `CGameMovement`, and `m_bDucked` is the state
it drives (`docs/vtmb/controls.md` → "What is bindable" carries the binding-layer half).

`Duck` (`0x10126fd0`) is called from `PlayerMove` between the step-sound update and
`CategorizePosition`, so the ground trace runs against the hull the rest of the frame will use. It
latches `IN_DUCK` (button bit `4`) into `m_nOldButtons` itself — the latch that makes a press edge
detectable at all, which is what a toggle reads — and drives four pieces of player state: `m_bDucked` (`+0x1edd`), `m_bDucking` (`+0x1ede`), `m_flDucktime` (`+0x1ee0`) and
`m_flDuckJumpTime` (`player[0x7b8]`). `m_bDucked` is the one the hull selector reads, which is why
`CanUnduck` (`0x101265d0`) clears it around its own trace to make `TracePlayerBBox` pick the
standing size.

`GAMEMOVEMENT_DUCK_TIME` is **1000.0** (milliseconds — `0x447a0000`, assigned to `m_flDucktime`
on both the duck and the unduck edge), and the elapsed fraction is formed as
`(1000 − ducktime) × 0.001` (`0x10454b8c` = `0.001f`) before being compared against the two
transition thresholds, which are stock Source's:

| Constant | Address | Value | Type |
|---|---|---|---|
| `GAMEMOVEMENT_DUCK_TIME` | `0x10447ee0` | `1000.0` | float (ms) |
| `TIME_TO_DUCK` | `0x1044a2bc` | `0.4` | float |
| `TIME_TO_UNDUCK` | `0x10449198` | `0.2` | **double** |

**Both thresholds are skipped entirely while airborne.** The elapsed-time comparison guards only
the `SetDuckedEyeOffset` lerp (`0x10126e20`), and that branch is additionally gated on
`GetGroundEntity()` being non-null:

```
if (elapsed*0.001 <= TIME_TO_DUCK && GetGroundEntity() && !(flags & FL_DUCKING))
    { SetDuckedEyeOffset(elapsed/TIME_TO_DUCK); return }     // ground: slide the eye
FinishDuck()                                                 // air, or ramp over: snap
```

so off the ground the duck — and symmetrically the unduck — **completes on the frame the button
changes**, with no 0.4 s ramp. `SetDuckedEyeOffset` is a plain linear lerp between the two view
offsets, not stock Source's `SimpleSpline`.

`FinishDuck` (`0x10126cb0`) and `FinishUnDuck` (`0x101269e0`) swap the hull and then fix the
origin, and the fixup differs by ground state:

| | origin fixup | net effect |
|---|---|---|
| on ground | `origin += duckMins − standMins` — **zero**, both mins are `0` | feet planted, head drops 36 |
| **airborne** | `origin ± (standHeight − duckHeight) × 0.5` = **±18** (`0x104454d0` = `0.5`, `0x1044f030` = `−0.5`) | feet rise 18, head drops 18 — the **centre** is what stays fixed |

**This is the crouch-jump, and it is the only way past a step taller than the 25-unit apex.**
Ducking in flight lifts the feet 18 units, so the reachable ledge goes from `sv_jump_boost` = **25**
to **43** units. `AirMove` never runs the step attempt, so `sv_stepsize` adds nothing here — 43 is
the hard ceiling on what can be climbed in one jump.

`FinishUnDuck` and `CanUnduck` (`0x101265d0`) both apply the airborne `−18` *before* running
`TracePlayerBBox` at the standing size, and refuse the unduck if it would not fit — which is what
stops a stand-up through a low ceiling on the ground, and through the floor in the air.

*Provenance: `DumpFuncs range=1011e000-10128000` on `vampire.dll`; the hull literals decoded from
the constructor's immediate dwords.*

## Where each move function lives

The whole mover, named by behaviour off the decompile (`CGameMovement::PlayerMove` and
`CGameMovement::TracePlayerBBox` are the only two functions carrying their own profile string, so
the rest are identified by their bodies):

| Function | Address | Note |
|---|---|---|
| `PlayerMove` | `0x101274a0` | the dispatcher; profile string `CGameMovement::PlayerMove` |
| `CategorizePosition` | `0x1011e560` | ground + `surfaceFriction` |
| `Duck` | `0x10126fd0` | → `FinishDuck` `0x10126cb0` / `FinishUnDuck` `0x101269e0`, `CanUnduck` `0x101265d0` |
| `CheckJumpButton` | `0x101226b0` | **additive**, and latches `m_nOldButtons` |
| `FullWalkMove` | `0x10121ce0` | the gravity split lives here |
| `FullNoClipMove` | `0x10122190` | reads `sv_noclipspeed` / `sv_noclipaccelerate` |
| `StartGravity` / `FinishGravity` | `0x1011fa80` / `0x10120f30` | the half-step pair |
| `Friction` | `0x10120ba0` | |
| `Accelerate` / `AirAccelerate` | `0x101212e0` / `0x10121000` | |
| `WalkMove` (`StepMove` inlined) | `0x101213b0` | |
| `WaterMove` | `0x101200c0` | |
| `CheckWater` / `CheckWaterJump` / `WaterJump` | `0x101252e0` / `0x1011fb50` / `0x1011ffe0` | |
| `CheckFalling` | `0x10125db0` | |
| `TracePlayerBBox` | — | profile string `CGameMovement::TracePlayerBBox` |
| `UpdateStepSound` | `0x1011e940` | step intervals 60/80 walking, 120/220 running |

### The gravity half-step split

`FullWalkMove`'s body, in order — this is where the split actually is, not in the caller:

```
if (!CheckWater())  StartGravity()                  // the FIRST half
if (m_flWaterJumpTime != 0) { WaterJump(); TryPlayerMove(); CheckWater(); return }
if (waterlevel >= 2) { if (waterlevel == 2) CheckWaterJump(); ... WaterMove(); ... }
else {
    IN_JUMP ? CheckJumpButton() : (oldbuttons &= ~IN_JUMP)
    if (onground) { velocity.z = 0; Friction() }
    CheckVelocity()
    onground ? WalkMove() : AirMove()
    CategorizePosition()
}
velocity -= basevelocity
CheckVelocity()
if (!CheckWater())  FinishGravity()                 // the SECOND half
if (onground) velocity.z = 0
CheckFalling()
```

### Ladders: VtMB has none

**`PlayerMove`'s movetype switch has no ladder arm.** Its cases are `0` (none), `2`/`3` (both →
`FullWalkMove`), `5`/`6` (→ the toss move), `9` (→ `FullNoClipMove`) and `10` (→ the observer
move), with everything else falling through to the `Bogus pmove player movetype %i` DevMsg. The
`"ladder"` string in `vampire.dll` at `0x10572554` is a **footstep material name** read by
`UpdateStepSound`, not a move type.

This agrees with the content: no `func_ladder`, `func_useableladder` or any other ladder classname
appears in the exported entity lumps. Ladder movement is not part of this game, so a port has
nothing to reproduce.

### Water

Water movement *does* exist — `WaterMove` (`0x101200c0`), reached from `FullWalkMove` whenever
`m_nWaterLevel` (`player+0x3e0`) is 2 or more — but **no exported map places a water brush**, so
it is unexercised by current content. The shape, read off the decompile:

```
wishvel  = forward*forwardmove + right*sidemove          // the full 3D view basis, pitch included
wishvel.z += upmove                                       // or += m_flUpMove's jump-button variant
if (no buttons and no move input)
    wishvel.z -= 40                                       // idle sink  (0x10462950; +40 when flagged)
wishspeed = min(|wishvel|, maxspeed) * 0.8                // 0x104491a8 = 0.8 (double)

// friction: no stopspeed floor, and it scales all three components
if (speed != 0) {
    newspeed = speed - dt * surfaceFriction * sv_friction * speed
    if (newspeed < 0.1) newspeed = 0                      // 0x104491b4 = 0.1f
    v *= newspeed / speed
}
// then a capped acceleration toward wishdir, on its own ConVar rather than sv_accelerate
```

Two constants differ from HL2's `CGameMovement`: the idle **sink rate is 40**, not 60, and the
water speed scale is the stock `0.8`. Vertical placement uses the hull's **midpoint** (`0.5`,
`0x10449270`, a double) rather than the eye.

**Confidence:** the wish-velocity build, the sink branch, the `0.8` clamp and the friction step are
transcribed from the decompile. The acceleration tail and the `WaterJump` /
`CheckWaterJump` pair (`0x1011ffe0` / `0x1011fb50`) are located and their ConVars identified
(`0x109ef28c` drives the water accel) but **not yet transcribed line-by-line** — there is no
content to validate them against, so they are the one part of this survey left at
"located, not read".

*Provenance: `DumpFuncs range=1011e000-10128000` on `vampire.dll`; every constant above read
directly from `.rdata` — note that several are `double` where the decompiler prints a bare `_DAT_`,
including the `0.7` standable normal (`0x104492d0`) and `TIME_TO_UNDUCK`.*

## Player speed is animation-driven

**There is no scalar player gait speed.** `CHL2_Player::PreThink` (`vampire.dll` `0x10350830`,
vftable `0x104a271c` slot 436) builds **six networked per-direction speed tables** every grounded
frame, and the client writes one cell of one table straight into the move command. `m_flMaxspeed`
is a *ceiling* derived from those tables, not the speed the player travels at.

### The tables

Each frame `PreThink` runs a fan extractor three times — once per gait — over the model's own
9×1 locomotion grids:

```c
float peak = 0.0f;
Fan(ACT_WALK  /* 9*/, m_flWalkForwardSpeeds,  m_flWalkSideSpeeds,  &peak, sv_walkscale);
Fan(ACT_RUN   /*19*/, m_flRunForwardSpeeds,   m_flRunSideSpeeds,   &peak, sv_runscale  * m_flSpeedScale);
Fan(ACT_SNEAK /*18*/, m_flSneakForwardSpeeds, m_flSneakSideSpeeds, &peak, sv_sneakscale* m_flSpeedScale);
if (m_bForceWalk) { copy 8+8 Walk*Speeds over Run*Speeds; }   // does NOT re-take the peak
if (peak > 0.0f) m_flMaxspeed = peak;                          // else hold the previous value
```

| Field | Offset | Field | Offset |
|---|---|---|---|
| `m_flRunForwardSpeeds[8]` | `+0x1b84` | `m_flRunSideSpeeds[8]` | `+0x1ba4` |
| `m_flWalkForwardSpeeds[8]` | `+0x1bc4` | `m_flWalkSideSpeeds[8]` | `+0x1be4` |
| `m_flSneakForwardSpeeds[8]` | `+0x1c04` | `m_flSneakSideSpeeds[8]` | `+0x1c24` |
| `m_flMaxspeed` | `+0x2310` | `m_flSpeedScale` | `+0x1488` |

All six are declared in the player SendTable (`FUN_10179840`) with exactly 8 elements, 13 bits,
range −2048…2048; `m_flMaxspeed` is 12 bits over 0…2048.

The extractor `FUN_10350620(activity, fwd[8], side[8], &outMax, scale)` resolves the activity
through `NPC_EarlyTranslateActivity` → `Weapon_TranslateActivity` → `NPC_TranslateActivity` →
`SelectHeaviestSequence`, requires `seqdesc->numblends == 9`, and then walks **cells 0…7** of
axis 0 — `anim[i][0]` at row stride 32 bytes. Cell 8 is skipped as the wrap duplicate of cell 0
(`docs/vtmb/animation_and_movers.md` → "A `move_yaw` fan's cells are angles"). Per cell:

```c
if (!Studio_AnimMovement(anim, 0.0f, 1.0f, &dPos, &dAng)) continue;   // cell left untouched
k = scale * anim->fps / (float)(anim->numframes - 1);                 // FMUL then FIDIV
fwd[i]  =  dPos.x * k;
side[i] = -dPos.y * k;
peak    = fmaxf(peak, |dPos * k|);                                    // 3D magnitude, after scaling
```

`Studio_AnimMovement` is `0x100c5b00`, stock Source. **The scale multiplies** — `FMUL` at
`0x10350728`, read at opcode level, with no reciprocal anywhere on the path.

**`m_flMaxspeed` is the running peak over all 24 cells.** One local, initialized to 0.0 once,
threaded through all three calls and never reset between them. It exists so the clamp below never
cuts a cell — it is a ceiling, not a gait.

### The scales, and the walk asymmetry

`sv_walkscale`, `sv_runscale` and `sv_sneakscale` are function-local statics in `PreThink`:
**1.0**, **1.0** and **2.3**.

**Walk is the only gait not multiplied by `m_flSpeedScale`** — the walk call passes the ConVar as
a raw copy while run and sneak apply `FMUL [ESI+0x1488]`. `m_flSpeedScale` defaults to 1.0 and is
written by `CBaseCombatCharacter::UpdateDisciplineVisuals`; `NPC_VFrenzyShadow` hard-sets 8.0. So
under any speed buff the run and sneak tables rise while the walk table and the walk/run threshold
that reads from it do not.

### The client picks one cell

`client.dll`'s `CInput` writes a cell's **absolute speed** into the user command, choosing the
table by duck state and the walk key:

```c
idx = DigitalKeyTable3x3();                       // FUN_10102940 -> DAT_102352c0, -1 when centred
if (idx >= 0) {
    if (playerFlags & FL_DUCKING) { forwardmove += sneakFwd[idx]; sidemove += sneakSide[idx]; }
    else if (KeyState(in_walk))   { forwardmove += walkFwd[idx];  sidemove += walkSide[idx];  }
    else                          { forwardmove += runFwd[idx];   sidemove += runSide[idx];   }
}
```

The 3×3 table maps the eight digital directions onto the fan's cells, aligning with
`move_yaw = −180 + 45k`:

|  | strafe left | none | strafe right |
|---|---|---|---|
| **forward** | 3 | 4 | 5 |
| **neither** | 2 | −1 | 6 |
| **backward** | 1 | 0 | 7 |

**Nothing fractional touches the speed at any stage.** The pose path blends two adjacent cells; the
speed path snaps to one. There is no analog-input behaviour here — the source is digital direction
keys only.

This is also what `+speed` does: **`SHIFT` selects the walk table**, client-side. The server-side
`IN_SPEED` crop in `CheckParameters` multiplies by virtual `+0x5c` (`FUN_1011ef00`), which is
`return 1.0f` — a no-op whose only effect is setting the `m_bSpeedCropped` latch.

### The ducking speed crop is dead code

`CGameMovement::HandleDuckingSpeedCrop` (`FUN_10126f60`) is stock Source — `forwardmove`,
`sidemove` and `upmove` each `*= 1/3` under `FL_DUCKING`, latched by `m_bSpeedCropped`
(`CGameMovement+0xdc`) — and it is **never executed**. It occupies vtable slot 25 (`+0x64`) of the
`CGameMovement` vftable `0x10462874`, and no `CALL dword ptr [reg+0x64]` exists anywhere in either
DLL's movement region: Ghidra xrefs show only the ILT thunk and the vftable data slot, an operand
scan over all 942,828 disassembled instructions finds none, and `Duck()` (`FUN_10126fd0`) makes 18
calls to other slots. `client.dll`'s copy at `0x100ed950` is identical and equally uncalled.

**So there is no duck speed multiplier.** Ducked movement differs only in which table the client
reads.

### The clamp

`CGameMovement::CheckParameters` (`FUN_1011f140`) is Source 1:1:

```c
mv->m_flMaxSpeed = sv_maxspeed;                                   // 2048, set in ProcessMovement
if (mv->m_flClientMaxSpeed != 0)
    mv->m_flMaxSpeed = min(mv->m_flClientMaxSpeed, mv->m_flMaxSpeed);
mv->m_flMaxSpeed *= min(m_pSurfaceData->game.maxSpeedFactor, ComputeConstraintSpeedFactor());
if (|forwardmove, sidemove, upmove| > mv->m_flMaxSpeed) scale all three down to it;
```

`m_pSurfaceData->game.maxSpeedFactor` is the authored `"maxspeedfactor"` key from
`scripts/surfaceproperties*.txt` (default `1.0`); `ComputeConstraintSpeedFactor` (`FUN_1011ef20`)
is Source's constraint-ring leash and returns 1.0 unless a constraint entity is active.

`CPlayerMove::SetupMove` copies `m_flMaxspeed` into `mv->m_flClientMaxSpeed` (`CMoveData+0x3c`) and
`FinishMove` copies it back; `CheckParameters` only ever mutates `m_flMaxSpeed` (`+0x38`), so the
round trip is an identity.

### Effective speeds, and why crouching is faster than walking

Every number in the DLL is **units/second**, one space, with no conversion between the animation
displacement and the ConVar space. On `tremere_Male_Armor_0` with `m_flSpeedScale` = 1.0:

| Gait | Authored cells | Effective | Ceiling relevance |
|---|---|---|---|
| walk | 60.7–136.7 cm/s | **23.9–53.8 u/s** | — |
| sneak | 69.7–79.3 cm/s ×2.3 | **63.1–71.8 u/s** | — |
| run | 457.8–528.3 cm/s | **180.2–208.0 u/s** | supplies the peak |

`m_flMaxspeed` = **208.0 u/s**. A ducked move at 63.1–71.8 u/s sits a factor of 8.4 below the
clamp's trigger in squared terms, so **the clamp never fires on a sneak move** — and the slowest
ducked cell still beats the fastest walk cell. **Retail's crouch-move is faster than its walk**, a
consequence of sneak's ×2.3 against a walk table that ignores `m_flSpeedScale` and carries no scale
of its own.

The repo's cm/s figures are `u/s × 2.54`, applied once in `mdl_skel.py` for presentation.
Comparing an authored cm/s figure against `sv_maxspeed` or `sv_jump_maxspeed` is wrong by 2.54×.

### Airborne

While the jump-phase field (`+0x1db4`) is in 1…7 — `ACT_LEAP`, `ACT_HOP`, `ACT_HOP_UP`,
`ACT_HOP_DOWN`, `ACT_LEAP_ASCEND`, `ACT_LEAP_DESCEND`, `ACT_FALLING` — `m_flMaxspeed` is pinned to
`sv_jump_maxspeed` (**350**) and the six tables are **not refreshed**, so the client keeps reading
the last grounded values for the whole jump. Since the largest of those is the run peak at 208.0
u/s, **the 350 pin does not cut at default settings**. It becomes live only above
`m_flSpeedScale > 1.683`, where airborne movement is capped while grounded movement is not.

Whether `AirAccelerate` applies Source's separate 30 u/s air wish-speed cap is
**[unresolved]** — the air branch reached from `FullWalkMove` is not read out.

### Fallbacks

Three, all "hold", none "zero": a cell whose `Studio_AnimMovement` reports no movement keeps its
previous value and does not contribute to the peak; a gait whose resolved sequence is not a 9-blend
grid leaves all 16 of its slots untouched; and if all three gaits yield a zero peak, `m_flMaxspeed`
holds. The tables and `m_flMaxspeed` are zeroed exactly once, in `Spawn` (`FUN_1016d260`).

`m_flMaxspeed`'s complete writer set is six sites, closed by two independent scans:
`ClientDisconnect` → 0, `Spawn` → 0, `FinishMove` ← `m_flClientMaxSpeed`, and the three in
`PreThink` (0.0, `sv_jump_maxspeed`, the peak).

### `m_bForceWalk` is unwired

`CHL2_Player::m_bForceWalk` (`+0x2544`, a `BOOLEAN` in `CHL2_Player`'s own 11-record datamap at
`0x106266e4`) copies the walk tables over the run tables when set — a "cannot run" switch. **Nothing
sets it.** It has exactly two code sites, the `PreThink` read and a clear at spawn; the string
occurs once in `vampire.dll`, in no other game binary, and in none of the 15 shipped `.vpk`
archives, so no level script reaches it through the datamap walk either. Note it does not re-take
the peak, so under it the tables say walk speeds while `m_flMaxspeed` still says the run peak.

### Ordering

`CPlayerMove::RunCommand` (`FUN_101874a0`) runs `PreThink` → `SetupMove` → `ProcessMovement` /
`PlayerMove` → `CheckParameters` → `FinishMove` → `PostThink`, all inside one command.

**`PreThink` does not read the currently-playing sequence.** It re-resolves all three activities
from scratch every frame and never consults `m_nSequence`, so there is no one-frame lag between the
animation selection made in `PostThink` and the speed source. The lag that does exist is a network
one: the client builds `forwardmove`/`sidemove` from the last received snapshot of the six tables.

### The intended figures

There is no ConVar holding the retail player speed. The intended figures are:

| ConVar | Value |
|---|---|
| `speed_walk` | 100 |
| `speed_runbase` | 225 |
| `speed_runbonusathletics` | 5 |

giving run = `225 + 5 * Athletics` (225–250) and walk = 100. `SHIFT` is bound to
`+speed`, which selects the *slow* gait: default locomotion is the run, holding Shift walks — in
the retail build by selecting the walk table client-side, as above.

### Dead ConVars

Registered, given plausible defaults, and **never read** — no `.text` reference to
`base+4` in either DLL:

`speed_walk`, `speed_runbase`, `speed_runbonusathletics`, `sv_backspeed`,
`sv_edgefriction`, `sv_deccelerate`, `sv_autojump`.

The edge-friction *trace* still runs (its result feeds `surfaceFriction`), but the
2× multiplier `sv_edgefriction` is gone. `speed_walk`/`speed_runbase` are still the
best statement of Troika's intended tuning and are what a port with no player
animation should use — they are simply wired to nothing in the retail build.
