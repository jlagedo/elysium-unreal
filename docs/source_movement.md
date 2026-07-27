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
