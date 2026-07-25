# Camera view modes — first person ↔ third person

VtMB ships one player camera with a **blend weight**, not two cameras. `togglecamera` flips a
bool; a per-frame driver ramps a 0→1 weight; every third-person effect — the boom offset, the
view angles, the player-model draw, the crosshair — is a function of that weight. This doc
records the reverse-engineered behaviour and the Unreal 5.8 design that reproduces it.

**Provenance.** Decompiled from the retail client:
`Vampire/cl_dlls/client.dll`, MD5 `96f8ef8f93411e9cb9805a54dcad747e`, imagebase `0x10000000`
(the Unofficial Patch ships no `cl_dlls/`, so retail is the only client binary; server-side
bits cited from `Vampire/dlls/vampire.dll`). Recovered with
`tools/ghidra/run.ps1 -Script DumpGrep|DumpFuncs|DumpAsm|DumpConVars`. Addresses below are
absolute at that imagebase. Class and method names are **reconstructed** — MSVC RTTI recovers
`CInput` / `CHLInput` as class namespaces but no method names; the slot mapping is inferred
from stock Source's `IInput` ordering plus the observed bodies, and every claim is anchored to
an address.

Confidence: the state machine, the blend, the cvar set and the consumer list are read directly
out of the decompilation. The angle math inside the third-person solver is **partially
recovered** — see [Not yet recovered](#not-yet-recovered).

---

## 1. The control surface

### Commands

Three `ConCommand`s, all registered client-side and all dispatching through the `::input`
singleton's vftable (`client.dll` `0x102352ec`, instance at `0x102ea6c8`, pointer at
`0x1027ba58`):

| Command | Thunk | Slot | Handler |
|---|---|---|---|
| `thirdperson` | `0x100fbd70` | `+0x88` | `CAM_ToThirdPerson` `0x100ff7c0` |
| `firstperson` | `0x100fbd80` | `+0x8c` | `CAM_ToFirstPerson` `0x100ff7e0` |
| `togglecamera` | `0x100fbd90` | `+0x90` | `CAM_ToggleCamera` `0x100ff800` |

Five further camera verbs bind to slots `+0xa4`…`+0xb0` and `+0xbc`; those handlers
(`0x100ff9d0`–`0x100ffa00`) are stubs or two-field state resets. The orbit and dolly are not
driven through them — `CAM_Think` polls the `kbutton_t` states directly (`0x100fc170`), and the
Unofficial Patch drives the camera through cvar aliases instead (`cam_restore`,
`cam_rotateleft`/`cam_rotateright` in `user.cfg`).

`camortho` and `snapto` exist in the command list (`docs/controls.md`); no orthographic path was
found in the recovered code.

### The default bind is `z`, not `h`

`cfg/default.cfg` line 46 and `scripts/kb_def.lst` both bind `"z"` `"togglecamera"`, in retail
**and** in the Unofficial Patch. `"h"` is `holster` in both. The patch relabels the action in
`scripts/kb_act.lst` — `togglecamera` → `"Toggle View"` (retail: `"Toggle 3rd person camera"`) —
and adds `+camin`/`+camout`/`cam_rotateleft`/`cam_rotateright`/`cam_restore` to the bindable
list. Full bind tables: `docs/controls.md`.

### Cvars

All client-side, defaults as registered (Source units and degrees):

| Cvar | Default | Role |
|---|---|---|
| `cam_idealdist` | `85` | desired boom length |
| `cam_targetangle` | `15` | camera pitch offset above the eye line |
| `cam_yaw` | `0` | yaw offset from the view direction (the patch's rotate-left/right aliases step this ±15°) |
| `cam_idealyaw`, `cam_idealpitch` | `0` | registered; not read in any recovered path |
| `cam_snapto` | `0` | registered; role unverified |
| `cam_collide` | `1` | run the boom collision trace |
| `cam_trace_radius` | `9.0` | half-extent of the boom hull trace |
| `cam_fadestart` | `32` | player model fully visible at/above this camera distance |
| `cam_fadeend` | `18` | player model fully hidden at/below this camera distance |
| `c_mindistance` / `c_maxdistance` | `30` / `200` | boom length clamp |
| `c_minpitch` / `c_maxpitch` | `0` / `90` | orbit pitch clamp |
| `c_minyaw` / `c_maxyaw` | `-135` / `135` | orbit yaw clamp |
| `cdamp_on` | `1` | enable the spring damper on the final camera position |
| `cdamp_hookesconstant` | `4.0` | spring constant, free camera |
| `cdamp_hookesconstantwall` | `15.0` | spring constant while the boom is wall-clipped (stiffer) |
| `cdamp_springlength` | `0.1` | spring rest length |
| `cdamp_maxdist` | `50.0` | damper clamp |
| `cam_command` | `0` | one-shot mode request consumed by `CAM_Think` (1 = go third, 2 = go first) |
| `camera_weaponswitch` | `1` | *archive* — master enable for auto-switching camera on weapon change |
| `camera_prefs` | `6` | *archive* — per-weapon-class bitmask; **set bit = first person** |

`camera_prefs` and `camera_weaponswitch` are `FCVAR_ARCHIVE` (`0x80`) and appear in a shipped
`config.cfg` as `camera_prefs "7"` / `camera_weaponswitch "1"`.

---

## 2. The state

`CInput` (`CHLInput`) carries the whole camera state. Offsets are from the instance base:

| Offset | Type | Meaning |
|---|---|---|
| `0xf0` | bool | `m_fCameraInThirdPerson` — the user's toggle |
| `0xf8` | bool | forced-third latch (set by weapon-class arbitration, slots `+0x94`/`+0x98`) |
| `0xf9` | bool | forced-first latch (slots `+0x9c`/`+0xa0`) |
| `0xfc` | float | **third-person blend weight**, 0…1 |
| `0x100` | float | scripted-camera blend weight, 0…1 (dialogue/cutscene) |
| `0x108` | float | a second scripted weight, decays at 0.5/s |
| `0x138` | float | feed / seduction / death camera weight, 0…1 |
| `0x154`,`0x158`,`0x15c`,`0x160` | float | smoothed distance, yaw, pitch, roll-lag state |
| `0x164`–`0x16c` | Vector | `m_vecCameraOffset` — the boom offset added to the eye position |
| `0x170`–`0x178` | QAngle | the camera's own angles |
| `0x17c`–`0x198` | — | scripted-camera target: origin, look-at, roll, FOV (copied from the player entity) |
| `0x104` | float | **player-model visibility alpha**, 0…1 |

### `CAM_IsThirdPerson` (`0x100ffa20`) is a disjunction

```c
int CAM_IsThirdPerson()
{
    if (!m_fForcedThird(0xf8) && !m_fCameraInThirdPerson(0xf0)
        && m_flThirdWeight(0xfc) <= 0 && m_flWeight108 <= 0
        && m_flFeedWeight(0x138) <= 0 && m_flScriptedWeight(0x100) <= 0)
        return 0;
    return 1;
}
```

Two consequences that matter for reproduction:

- **It is true throughout the blend, in both directions.** Everything gated on it (player model,
  crosshair, world weapon) switches on the *first frame* of a first→third transition and off
  only on the *last* frame of third→first, when the weight reaches exactly 0.
- **A scripted camera counts as third person.** Dialogue, feeding and death cameras reuse the
  same predicate, so they get the player model drawn for free.

### `CAM_ToggleCamera` (`0x100ff800`)

```c
void CAM_ToggleCamera()
{
    cam_command.SetValue(0);
    m_fCameraInThirdPerson = !m_fCameraInThirdPerson;

    if (!LocalPlayer_CanUseThirdPerson())          // player vtable +0x250, via 0x1009b210
        m_fCameraInThirdPerson = false;
    else {
        SaveWeaponCameraPref(m_fCameraInThirdPerson);   // 0x1009c2e0
        ApplyWeaponCameraPref();                        // 0x1009c250
        if (LocalPlayerRecord()->field_0x4fe84 > 0)
            engine->ClientCmd("force_sniper_third_person");
    }

    if (m_fForcedThird(0xf8) && localPlayer) {
        if (LocalPlayerRecord()->field_0x24d0 == 0) {
            engine->ClientCmd("inven_holster");    // the equipped item cannot leave third person
            m_fCameraInThirdPerson = false;
            m_fForcedThird = false;
        }
    }
}
```

`CAM_ToThirdPerson` / `CAM_ToFirstPerson` are the minimal pair — set `0xf0`, then
`cam_command.SetValue(0)`. They do **not** run the weapon arbitration or the holster check;
only `togglecamera` does.

### Weapon-class arbitration (sticky per weapon)

The local player record carries a weapon-class bitmask at `+0x2440`. Two functions read it:

```c
// 0x1009c250 — re-arbitrate on weapon change
void ApplyWeaponCameraPref()
{
    int cls = LocalPlayerRecord()->weaponCameraClass;   // +0x2440
    if (!cls || !camera_weaponswitch.GetInt()) return;
    if (cls == 0x08) { input->CAM_ToFirstPerson(); return; }   // always first person
    if (cls == 0x10) { input->ForceThirdPersonOn(); return; }  // always third person
    if (cls & camera_prefs.GetInt()) input->CAM_ToFirstPerson();
    else                             input->CAM_ToThirdPerson();
}

// 0x1009c2e0 — remember what the player just chose, for this weapon class
void SaveWeaponCameraPref(bool bThirdPerson)
{
    int cls = LocalPlayerRecord()->weaponCameraClass;
    if (cls == 0x08 || cls == 0x10 || cls == 0) return;        // not user-settable
    int prefs = camera_prefs.GetInt();
    prefs = bThirdPerson ? (prefs & ~cls) : (prefs | cls);     // set bit = first person
    camera_prefs.SetValue(prefs);
}
```

So `togglecamera` is **sticky per weapon class and persisted to `config.cfg`**: toggle to third
person while a melee weapon is out and every future draw of that class starts in third person.
Class `0x08` is pinned to first person and class `0x10` to third person, and neither writes a
preference. Default `camera_prefs 6` = classes `2` and `4` first person, class `1` third person.

---

## 3. The transition

One function drives every camera weight: `0x100fc900`, called at the top of `CAM_Think`.

```c
float dt = engine->GetFrameTime() * localPlayer->m_flTimeScale;   // player +0x1078

// ... feed/death weight (0x138) and the 0x108 weight advance first ...

if (m_fForcedThird(0xf8) || m_flFeedWeight(0x138) > 0)  w = m_flThirdWeight + 2*dt;
else if (m_fForcedFirst(0xf9))                          w = m_flThirdWeight - 2*dt;
else if (m_fCameraInThirdPerson(0xf0))                  w = m_flThirdWeight + 2*dt;
else                                                    w = m_flThirdWeight - 2*dt;

m_flThirdWeight(0xfc) = clamp(w, 0.0f, 1.0f);
```

The properties that define the feel:

- **Rate 2.0 per second** (`FADD st,st` on the frame delta) — a full first↔third traversal takes
  **0.5 s**.
- **Linear in the weight, eased at the point of use.** The weight itself ramps linearly; the
  consumers pass it through `SimpleSpline(t) = t²(3 − 2t)` (`0x100fdb30`, constant `3.0` at
  `0x10227ee0`).
- **Scaled by the player's time scale** (`+0x1078`), so the transition slows down with
  bullet-time / Celerity rather than running on wall-clock.
- **Symmetric and interruptible.** Reversing mid-blend continues from the current weight; there
  is no transition object, no start/end snapshot, no restart.
- **Priority order**: forced-third and the feed camera win, then forced-first, then the user
  toggle.

### What the weight is applied to

In the third-person solver (`0x100fd350`, tail):

```c
float e = SimpleSpline(m_flThirdWeight);
m_vecCameraOffset *= e;                          // boom scales 0 → full
LerpAngles(&viewAngles, &m_vecCameraAngles, e);  // 0x1010c500
```

The camera therefore **slides out of the player's head along the solved boom** and the view
angles interpolate from the player's eye angles to the camera's own angles. There is no FOV
change and no separate camera object; at weight 0 the offset is the zero vector and the result
is exactly the first-person eye view.

The scripted camera (weight `0x100`) is applied on top, in slot `+0x84` (`0x100ffb90`): origin,
look-at, **roll** (`0x194`) and **FOV** (`0x198`) all lerp by that weight, and its weight is a
*timed* ramp `(now − startTime) / duration` from fields on the player entity (`+0x1658+0x114`,
`+0x118`), optionally reversed — i.e. cutscene cameras get an explicit start time and duration,
unlike the toggle's fixed-rate blend.

---

## 4. The third-person solve

`CAM_Think` (`0x100ff130`) each frame:

1. Consume `cam_command`: `1` → `CAM_ToThirdPerson` unless a player state value equals `10`;
   `2` → `CAM_ToFirstPerson`; otherwise, if that state equals `10` **and** the third weight is
   > 0, force `CAM_ToFirstPerson` — i.e. one player state kicks the camera out of third person
   and holds it there.
2. `0x100fc170` — poll the orbit/dolly `kbutton_t`s, clamp against `c_minyaw`/`c_maxyaw`,
   `c_minpitch`/`c_maxpitch`, `c_mindistance`/`c_maxdistance`.
3. `0x100fc900` — advance all four weights (above).
4. Origin = player eye position + a per-player offset; angles = player eye angles with
   `yaw += cam_yaw`.
5. If the third weight is > 0, run the solver `0x100fd350`; otherwise mark the camera as needing
   a re-seed on the next entry (a flag at `+0x4`, which makes the spring damper snap rather than
   ease when third person is re-entered).
6. If the feed weight (`0x138`) or the second weight (`0x108`) is > 0, run those solvers
   (`0x100fdfa0`, `0x100fe7f0`).
7. Compute the player-model alpha (below).

The solver `0x100fd350`:

- Desired distance = `cam_idealdist`; desired pitch starts from `cam_targetangle`.
- Distance, yaw and pitch are each pushed toward their targets by a **rate-limited approach**
  (`0x100fc000` — clamp the delta to `speed*dt`, scale down below a minimum time step, clamp to a
  max delta; an angle flag routes the value through 16-bit angle space so wrap works).
- **Collision** (`0x100fdb50`): when `cam_collide` is on, a `UTIL_TraceHull` sweep runs from the
  eye toward the desired camera point with half-extents `±cam_trace_radius` (9.0) and mask
  `0x0601400b` (`0x0601403b` when the start point is not in solid/water contents); the allowed
  distance is `dist * trace.fraction`. With `cam_collide 0` the trace is skipped entirely.
- A wall-contact case pulls the camera in a further `7.0` units and forces a re-seed.
- **Spring damper** (`0x100fd0b0`, gated by `cdamp_on`): the final camera position is a Hooke
  spring toward the solved point, using `cdamp_hookesconstantwall` (15.0) when the boom is
  clipped by the trace and `cdamp_hookesconstant` (4.0) otherwise. So the camera is *stiffer*
  against walls than in open space — it snaps in and eases out.
- Finally the weight scaling and angle lerp from §3.

`CAM_ApplyToView` (slot `+0x7c`, `0x100ffb00`) is the single point where the view is modified:

```c
view->origin += m_vecCameraOffset;
view->angles  = m_vecCameraAngles;
ApplyScriptedBlend(&view->origin, &view->angles, &view->fov);
```

---

## 5. What the mode affects

| Effect | Where | Behaviour |
|---|---|---|
| **View origin + angles** | `CAM_ApplyToView` `0x100ffb00` | the only spatial change; a pure offset + angle override on the first-person view |
| **Local player model** | `ShouldDrawLocalPlayer` `0x100a5910` | `return (this != localPlayer) \|\| CAM_IsThirdPerson()` — the classic Source rule |
| **Player-model alpha** | `CAM_Think` tail, `CInput+0x104` | ramp by camera distance: `0` below `cam_fadeend`, `1` at/above `min(cam_idealdist, cam_fadestart)`, `SimpleSpline` in between. In one player state (flag at player `+0x16f0`, or predicate `0x10192850`) the alpha is quantised to `0`/`1` at a `0.1` threshold — no partial transparency in that state. |
| **World weapon / attachments** | `0x100a7a50`, `0x100aef40` | entities owned by the local player draw only in third person (`IsLocalPlayerEntity` `0x100a99d0` + `CAM_IsThirdPerson`) |
| **Crosshair / look cursor** | `0x1009b9e0` | third person draws the plain white reticle at the crosshair rect; first person runs the full use-icon/arrow cursor path (see `docs/entity_io.md`) |
| **An entity/handle lookup** | `0x100a99b0` | returns the result of `0x1008f720` in third person and null in first — most plausibly the player model to attach effects to; the callee is not identified |
| **Particle systems** | `0x10147be0` (client), `0x101dc8a0`/`0x101dce30` (server) | particle definitions carry `Particle_FirstPerson` / `Particle_ThirdPerson` blocks; emitters are tagged with a mode index at parse time (`0` = third, `1` = first) |
| **Equipped item** | `CAM_ToggleCamera` | toggling out of a forced-third weapon runs `inven_holster` |
| **Sniper zoom** | `CAM_ToggleCamera` → `force_sniper_third_person` | a server `ConCommand` in `vampire.dll` (`0x100d61a0`), help text *"called when the sniper rifle is forced into third person."* |
| **Persistence** | `camera_prefs` | the choice is written back per weapon class and archived to `config.cfg` |

**What it does not affect**: movement (the character still steers by view yaw — the camera is a
view-space offset, nothing is reparented), the aim/attack origin, FOV, or input sensitivity. The
player keeps authority over the view angles the whole time; the camera derives from them.

---

## 6. Reproducing it on Unreal 5.8

### Constraints this project imposes

- **No `.uasset` authoring.** Every engine object is built in C++ at map-load
  (root `CLAUDE.md`). That rules out any asset-authored camera solution.
- **A console/cvar bridge already exists** (`ccmd`/`cvar`, roadmap 9.3b), so VtMB's cvar names can
  be reproduced 1:1 and the shipped `cfg/` + the patch's `user.cfg` aliases keep working verbatim.
- **The pawn is first-person today**: `AElysiumPawn` is an `ACharacter` with one
  `UCameraComponent` at `Z = 71.2` cm and `bUsePawnControlRotation = true`
  (`Source/ElysiumUE/Private/ElysiumPawn.cpp`). There is no player mesh yet.
- **Fully dynamic renderer**, HWRT Lumen + VSM, static lighting disabled
  (`docs/rendering-perf.md`).
- **Feel is reproduce-first, then polish by explicit owner call** (`docs/remaster-direction.md`).
  The blend rate, the damper constants and the fade band are all *feel*, so they get built
  faithfully and stay A/B-able behind the cvars.

### The shape: one camera, one weight, one chokepoint

Mirror VtMB's structure rather than Unreal's idioms:

- Keep the single `UCameraComponent`. It stays the source of FOV, post-process settings and the
  first-person-rendering flags.
- Add a camera state object (a component on the pawn, or a member of `AElysiumPawn`) holding the
  weight, the latches and the smoothed distance/yaw/pitch — VtMB's `CInput` camera block.
- Advance the weight in `Tick` at **2.0/s × time scale**, clamped `[0,1]`, with VtMB's priority
  order. This is the whole transition; there is no state machine.
- Override **`AElysiumPawn::CalcCamera(float, FMinimalViewInfo&)`** as the single apply point —
  the structural analogue of `CAM_ApplyToView`. Call `Camera->GetCameraView(DeltaTime, Out)`
  first so post-process and the first-person-rendering fields are filled, then add the offset and
  override the rotation on top. (Overriding `CalcCamera` without delegating to the camera
  component is the documented cause of first-person rendering silently not applying — Lyra's
  custom camera component hit exactly this.)
- Solve the boom in the same order VtMB does: desired vector from control rotation with
  `cam_yaw` added to yaw and `cam_targetangle` added to pitch → rate-limited approach on
  distance/yaw/pitch → collision sweep → spring damper → `Offset *= SimpleSpline(w)` and
  `Rotation = Lerp(ViewRotation, CameraRotation, SimpleSpline(w))`.

`SimpleSpline` exists in Unreal as `FMath::SmoothStep`/`FMath::InterpEaseInOut`; `t²(3−2t)` is
two lines and worth writing literally so it matches the decompiled constant.

### Collision and damping

`UWorld::SweepSingleByChannel` with a sphere of `cam_trace_radius` on `ECC_Camera`, ignoring the
pawn, is the direct equivalent of `UTIL_TraceHull` (VtMB uses a *box* hull with equal half-extents;
a sphere is the closer match to how it actually reads in motion, and is what `USpringArmComponent`
uses too — record the substitution here, it is a feel delta).

The damper is a Hooke spring with **two constants** — 4.0 free, 15.0 wall-clipped — which is the
single most characteristic part of the VtMB camera and the reason the stock spring arm is not
enough (below). Implement it explicitly; keep `cdamp_on 0` working as a bypass for A/B.

### Options considered and rejected

| Option | Why not |
|---|---|
| **`USpringArmComponent`** with `TargetArmLength` lerped to 0 | The obvious idiom and fine for a prototype, but its lag model is an exponential interpolation on location/rotation, not a spring with a separate wall constant; its probe retracts instantly and springs back linearly; and there is no weight to hang the angle blend, the model fade and the crosshair switch on. Reproducing VtMB's damper inside it means subclassing it anyway, at which point the component buys nothing. |
| **`SetViewTargetWithBlend`** between two camera actors | Blends between *actors* over a fixed time with a fixed curve. Re-triggering mid-blend restarts rather than resuming from the current weight, and VtMB's toggle is symmetric and interruptible by design. It also implies a second view target, which the single-pawn model does not have. |
| **Gameplay Camera System** (UE 5.5+) | Still **Experimental** in 5.8 ("use caution when shipping"), and it is a *data asset* system — camera rigs authored in the editor. Directly contrary to the no-`.uasset`, code-built rule. Revisit if it stabilises and exposes a code path to build rigs without assets. |

### Player mesh, fade and first-person rendering

There is no player mesh yet, so this is design intent for when one lands (the P8 skeletal path
supplies the machinery):

- **Visibility**: `CAM_IsThirdPerson`'s "true throughout the blend" semantics matter — the mesh
  must be registered and drawn from the first frame of the blend, not switched at the end. Drive
  a MID scalar from the alpha rather than toggling `SetOwnerNoSee`; use `SetOwnerNoSee(true)`
  only as a cull when the weight is exactly 0.
- **The fade band** (`cam_fadeend` 18 → `cam_fadestart` 32 Source units) is a near-camera
  dissolve, so it needs dithered or masked opacity on the character material, not translucency —
  translucency would take the player mesh off the opaque path and out of Lumen's GI, which is
  load-bearing here. The one state that quantises the alpha to 0/1 maps to a simple
  `bDitherEnabled = false` branch.
- **First Person Rendering** (UE 5.5+) is available to this project for free: its advanced
  features require *Allow Static Lighting* to be **disabled**, which is already the case, and its
  world-space-representation path needs VSM or ray-traced shadows, which is already the render
  path. If a first-person weapon/hands mesh is ever added, `FirstPersonPrimitiveType = FirstPerson`
  plus a `WorldSpaceRepresentation` twin gives correct HWRT reflections and VSM shadows without
  a second render pass. It is not needed for the camera itself.

### Units

Keep the **cvar surface in Source units** and convert internally (× 2.54), so a user's
`config.cfg` and the patch aliases transfer unchanged:

| Cvar | Source units | cm |
|---|---|---|
| `cam_idealdist` | 85 | 215.9 |
| `c_mindistance` / `c_maxdistance` | 30 / 200 | 76.2 / 508 |
| `cam_fadestart` / `cam_fadeend` | 32 / 18 | 81.28 / 45.72 |
| `cam_trace_radius` | 9.0 | 22.86 |
| `cdamp_maxdist` | 50.0 | 127 |
| `cdamp_springlength` | 0.1 | 0.254 |

Angles (`cam_targetangle`, `cam_yaw`, `c_min*`/`c_max*`) are degrees and need no conversion, but
Source's `(pitch, yaw, roll)` maps to `FRotator(Pitch, Yaw, Roll)` with **pitch sign flipped** —
the same rule the rest of the pipeline already applies (`docs/rebuild-strategy.md` →
*Coordinate conventions*).

### Console surface to expose

Reproduce verbatim through the existing bridge: `togglecamera`, `thirdperson`, `firstperson`,
`cam_idealdist`, `cam_yaw`, `cam_targetangle`, `cam_collide`, `cam_trace_radius`, `cam_fadestart`,
`cam_fadeend`, `cdamp_on`, `cdamp_hookesconstant`, `cdamp_hookesconstantwall`,
`cdamp_springlength`, `cdamp_maxdist`, `c_minpitch`, `c_maxpitch`, `c_minyaw`, `c_maxyaw`,
`c_mindistance`, `c_maxdistance`, `cam_command`, `camera_prefs`, `camera_weaponswitch`.
The input layer binds `z` → the *command*, never a hardcoded key, so `kb_def.lst` and a user's
rebinds keep governing (`docs/controls.md`).

`camera_prefs` / `camera_weaponswitch` only become meaningful once weapons exist; register them
early anyway so the archived value survives round-tripping a user's `config.cfg`.

### Verification hooks

- An automation test (`test.bat Substrate`, `-nullrhi`) over the weight driver alone: 0→1 in
  0.5 s at time scale 1, correct scaling by time scale, clamping, the priority order, and
  symmetric resume on a mid-blend reversal.
- `elysium_player_get` (MCP) reports the view mode, the weight and the solved boom length, so an
  agent can drive the toggle and assert the transition.
- `shots.bat` vantages captured at weight 0, 0.5 and 1 give a look-regression baseline for the
  blend.

---

## Not yet recovered

- The exact angle math in `0x100fd350` — how `cam_targetangle` composes with the view pitch, and
  the 16-bit-angle round trip (`× 360/65536`, constant `0x101e34f8`) around the yaw. The clamp at
  `270.0` (`0x10234c80`) is observed but its role is not pinned.
- `cam_idealyaw`, `cam_idealpitch`, `cam_snapto` — registered, never read in the recovered paths.
- The player-class gate `vtable +0x250` (`0x1009b210`) — what makes third person unavailable.
- The weapon-class bit meanings at player record `+0x2440` (`1`, `2`, `4`, `8`, `0x10`) and the
  record fields `+0x24d0` (holster gate) and `+0x4fe84` (sniper state).
- The feed / seduction / death camera solvers `0x100fdfa0` and `0x100fe7f0`, and the
  `camfeed_*` / `camseduct_*` / `camdead_*` cvar families that drive them.
- The player-state flag at `+0x16f0` and predicate `0x10192850` that quantise the model alpha.
