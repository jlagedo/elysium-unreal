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
every aspect, and a wider window shows more horizontally. Godot's `Camera3D.Fov`
is *vertical* under the default `KeepAspectEnum.Height`, so it needs converting:

```
vfov = 2 * atan(tan(hfov/2) / (4/3))       // 75 -> 59.84
```

(`MenuBackground3D` applies the same conversion to the menu's `camera_fov`.)

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

`surfaceFriction` is a per-player field the ground trace fills in; absent surface
data it is 1.0.

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

## Godot mapping

> **Godot-target mapping (reference).** This section maps the constants above onto the read-only
> Godot prototype (`E:\dev\elysium`: `CharacterBody3D`, `PlayerController.StepMove`). It is porting
> reference, not the Unreal target — see `docs/rebuild-strategy.md` (Track A: custom
> `UCharacterMovementComponent`, port `CGameMovement` line-by-line). The unit factor and the
> box-hull / step-move requirements carry over unchanged.

1 unit = 0.0254 m (`Assets`/`World` use the same factor).

| Quantity | Source | Godot |
|---|---|---|
| gravity | 800 u/s² | 20.32 m/s² |
| run (`speed_runbase`) | 225 u/s | 5.715 m/s |
| walk (`speed_walk`) | 100 u/s | 2.54 m/s |
| jump velocity | 200 u/s | 5.08 m/s (apex 0.635 m) |
| `sv_stopspeed` | 16 u/s | 0.406 m/s |
| air speed cap | 30 u/s | 0.762 m/s |
| `sv_stepsize` | 18 u | 0.457 m |
| `sv_maxvelocity` | 3500 u/s | 88.9 m/s |
| eye height | 64 u | 1.626 m |
| hull (box) | 72 × 32 u | 1.83 × 0.81 m |
| ground tolerance | 2 u | 0.051 m (`FloorSnapLength`) |
| FOV | 75 horizontal @ 4:3 | 59.84 vertical |
| mouse | 0.022 °/count × sens 3 | 0.0011519 rad/count |

`sv_friction` and `sv_accelerate` are per-second rates and carry over unitless.

Eye height is the value `PlayerController` already used and is **not** confirmed
against the binary. The hull is a `BoxShape3D` because StepMove requires it (above).

Godot's `CharacterBody3D` has **no step-height property** — `floor_snap_length`
only keeps a body attached going *down*. `PlayerController.StepMove` ports the
algorithm above, with `Trace` standing in for `TracePlayerBBox`: `MoveAndCollide`
is not a substitute, as its depenetration pass nudges the hull sideways out of a
flush contact. `ApplyFloorSnap` afterwards re-derives `IsOnFloor`, which both
attempts otherwise leave reflecting the raised (airborne) move.
