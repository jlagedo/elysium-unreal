# Source movement + camera (VtMB tuning)

The numbers behind first-person game feel: view angles, FOV, walk/run speed,
acceleration, gravity, jump. VtMB runs an early-Source `CGameMovement`, so the
*math* is stock Source; the *constants* are Troika's and several differ from
Half-Life 2.

## Provenance

Every value here is read out of the user's own install, not from HL2 lore:

- `cfg/default.cfg` (inside `pack000.vpk`, read via `tools/vpk.py`) — Troika's
  authored defaults + the key binds.
- ConVar defaults compiled into `Vampire/dlls/vampire.dll` and
  `Vampire/cl_dlls/client.dll`. MSVC emits each registration as
  `push <flags>; push <default-string>; push <name-string>; mov ecx,<ConVar*>; call ConVar::ConVar`,
  so name, default, and the ConVar object address all fall out of a byte scan of
  `.text`.
- Decompiles of `CGameMovement` (RTTI vftable `0x10462874` in `vampire.dll`) via
  `tools/ghidra/`.

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
`CUserCmd`, and that whole chain runs while the engine drains the client's `clc_move`
message — **before** the server frame runs a single think. `PreThink`, the player's own
think and `PostThink` all sit inside `RunCommand` around the move, and
`gpGlobals->frametime`/`curtime` are rebound to the command's timing for its duration, so
the move integrates on the command's clock rather than the frame's. Full chain, addresses
and the ordering evidence: `game_runtime.md` §1.

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
interval. This closes the question from the other direction than the cvar sweep did, and it agrees
with it: `game_runtime.md` § "Time model" pins VtMB to the pre-tick Source branch on the absence of
`interval_per_tick` / `sv_tickrate` / `TICK_INTERVAL`, and the frame pacing confirms it. 66.7 Hz is
*modern* Source's default tickrate and has no presence in this build.

**The consequence for movement is real, not theoretical.** Because every `CGameMovement` formula
integrates on that variable delta, retail's movement genuinely is frame-rate dependent:
`AirAccelerate`'s `addspeed` clamp stops binding above ~117 fps, and the full-step gravity puts the
jump apex at `25 − 100·dt` units rather than a flat 25. Reproducing VtMB faithfully means
reproducing that dependence.

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

Third-person (`z` = `togglecamera`): `c_mindistance` 30, `c_maxdistance` 200,
`c_minpitch` 0, `c_maxpitch` 90, `c_minyaw`/`c_maxyaw` ±135.

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
| `sv_jump_boost` | 25.0 | jump apex, in units |
| `sv_jump_maxspeed` | 350.0 | `m_flMaxSpeed` while airborne |
| `sv_jump_boost_immediate` | 0 | when 1, the jump teleports up instead of adding velocity |
| `sv_walkscale` / `sv_runscale` / `sv_sneakscale` | 1.0 / 1.0 / 2.3 | function-local statics in `CHL2_Player::PreThink` |

**Jump velocity** = `sqrt(2 * sv_jump_boost * sv_gravity)` = `sqrt(2*25*800)` =
**200 u/s**, scaled by a ground factor. `sv_jump_boost` is literally the apex
height in units, since `v²/2g = 25`.

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
**30**, not the 28 that stock Source's `VEC_DUCK_VIEW` uses — a Troika value, and the reason it had
to be read rather than assumed. The mins/maxs selector keys off `m_bDucking` (`player+0x1edd`),
while the view-offset selector takes the ducked flag as an argument.

The same constructor seeds `surfaceFriction` (`+0xa0`) to `1.0`, which is the value the game then
computes every frame anyway (above).

### Ducking

`Duck` (`0x10126fd0`) is called from `PlayerMove` between the step-sound update and
`CategorizePosition`, so the ground trace runs against the hull the rest of the frame will use. It
latches `IN_DUCK` (button bit `4`) into `m_nOldButtons` itself, and drives four pieces of player
state: `m_bDucking` (`+0x1edd`), `m_bDucked` (`+0x1ede`), `m_flDucktime` (`+0x1ee0`) and
`m_flDuckJumpTime` (`player[0x7b8]`).

`GAMEMOVEMENT_DUCK_TIME` is **1000.0** (milliseconds — `0x447a0000`, assigned to `m_flDucktime`
on both the duck and the unduck edge), and the elapsed fraction is formed as
`(1000 − ducktime) × 0.001` (`0x10454b8c` = `0.001f`) before being compared against the two
transition thresholds, which are stock Source's:

| Constant | Address | Value | Type |
|---|---|---|---|
| `GAMEMOVEMENT_DUCK_TIME` | `0x10447ee0` | `1000.0` | float (ms) |
| `TIME_TO_DUCK` | `0x1044a2bc` | `0.4` | float |
| `TIME_TO_UNDUCK` | `0x10449198` | `0.2` | **double** |

`FinishDuck`
(`0x10126cb0`) and `FinishUnDuck` (`0x101269e0`) swap the hull and fix the origin up by half the
height difference; `FinishUnDuck` first runs a `TracePlayerBBox` at the standing size and refuses
the unduck if it would not fit, which is what stops a stand-up through a low ceiling.

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

`CHL2_Player::PreThink` (`vampire.dll` `0x10350830`, vftable `0x104a271c` slot
436) sets `m_flMaxSpeed` from the **root motion of the current animation
sequence** — move distance / frame count for the active activity — scaled by
`sv_walkscale` / `sv_runscale` / `sv_sneakscale`. While airborne it is pinned to
`sv_jump_maxspeed` (350).

So there is no ConVar holding the retail player speed. The intended figures are:

| ConVar | Value |
|---|---|
| `speed_walk` | 100 |
| `speed_runbase` | 225 |
| `speed_runbonusathletics` | 5 |

giving run = `225 + 5 * Athletics` (225–250) and walk = 100. `SHIFT` is bound to
`+speed`, which in Source selects the *slow* gait: default locomotion is the run,
holding Shift walks.

### Dead ConVars

Registered, given plausible defaults, and **never read** — no `.text` reference to
`base+4` in either DLL:

`speed_walk`, `speed_runbase`, `speed_runbonusathletics`, `sv_backspeed`,
`sv_edgefriction`, `sv_deccelerate`, `sv_autojump`.

The edge-friction *trace* still runs (its result feeds `surfaceFriction`), but the
2× multiplier `sv_edgefriction` is gone. `speed_walk`/`speed_runbase` are still the
best statement of Troika's intended tuning and are what a port with no player
animation should use — they are simply wired to nothing in the retail build.
