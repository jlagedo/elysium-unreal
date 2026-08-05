# Camera view modes — first person ↔ third person

VtMB ships one player camera with a **blend weight**, not two cameras. `togglecamera` flips a
bool; a per-frame driver ramps a 0→1 weight; every third-person effect — the boom offset, the
view angles, the player-model draw, the crosshair — is a function of that weight. This doc
records the reverse-engineered behaviour and the Unreal 5.8 design that reproduces it.

**Provenance.** Decompiled from the retail client:
`Vampire/cl_dlls/client.dll`, MD5 `96f8ef8f93411e9cb9805a54dcad747e`, imagebase `0x10000000`
(the Unofficial Patch ships no `cl_dlls/`, so retail is the only client binary; server-side
bits cited from `Vampire/dlls/vampire.dll`). Recovered with
`research/tooling/ghidra/driver/run.ps1 -Script DumpGrep|DumpFuncs|DumpAsm|DumpConVars`. Addresses below are
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

`camortho` and `snapto` exist in the command list (`docs/vtmb/controls.md`); no orthographic path was
found in the recovered code.

### The default bind is `z`, not `h`

`cfg/default.cfg` line 46 and `scripts/kb_def.lst` both bind `"z"` `"togglecamera"`, in retail
**and** in the Unofficial Patch. `"h"` is `holster` in both. The patch relabels the action in
`scripts/kb_act.lst` — `togglecamera` → `"Toggle View"` (retail: `"Toggle 3rd person camera"`) —
and adds `+camin`/`+camout`/`cam_rotateleft`/`cam_rotateright`/`cam_restore` to the bindable
list. Full bind tables: `docs/vtmb/controls.md`.

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
| **Crosshair / look cursor** | `0x1009b9e0` | third person draws the plain white reticle at the crosshair rect; first person runs the full use-icon/arrow cursor path (see `docs/vtmb/entity_io.md`) |
| **An entity/handle lookup** | `0x100a99b0` | returns the result of `0x1008f720` in third person and null in first — most plausibly the player model to attach effects to; the callee is not identified |
| **Particle systems** | `0x10147be0` (client), `0x101dc8a0`/`0x101dce30` (server) | particle definitions carry `Particle_FirstPerson` / `Particle_ThirdPerson` blocks; emitters are tagged with a mode index at parse time (`0` = third, `1` = first) |
| **Equipped item** | `CAM_ToggleCamera` | toggling out of a forced-third weapon runs `inven_holster` |
| **Sniper zoom** | `CAM_ToggleCamera` → `force_sniper_third_person` | a server `ConCommand` in `vampire.dll` (`0x100d61a0`), help text *"called when the sniper rifle is forced into third person."* |
| **Persistence** | `camera_prefs` | the choice is written back per weapon class and archived to `config.cfg` |

**What it does not affect**: movement (the character still steers by view yaw — the camera is a
view-space offset, nothing is reparented), the aim/attack origin, FOV, or input sensitivity.

### The strafe bank is first-person only

Strafing banks the camera around the view axis, and it is a **view** effect — no character
animation is involved. `client.dll` `0x101907a0` is Quake's `V_CalcRoll`, unchanged:

```
side = DotProduct(velocity, right);   sign = side >= 0 ? 1 : -1;   side = |side|
side < rollspeed ? (side / rollspeed) * rollangle * sign
                 : rollangle * sign
```

Its caller (`0x10190850`) fetches the two cvars, adds the result to `view.roll`, and — when a
vtable predicate fires — adds a literal `0.0` (`0x101e34f0`) instead. That gate is what makes the
bank exist in first person and vanish in third.

**The live pair is `cl_rollangle` (default `2`, degrees) and `cl_rollspeed` (default `200`).**
`sv_rollangle` / `sv_rollspeed` carry the same defaults and are **dead**: both game DLLs register
them, neither reads them, and the string does not occur in `engine.dll` at all. (The community
knows this empirically as "`sv_rollangle` doesn't work"; the RE says why.) ConVar objects:
`cl_rollangle` `0x105fc550`, `cl_rollspeed` `0x105fc5b0`, recovered from the `mov ecx, imm32` in
each registration stub — the decompiler drops the `this` argument on these thiscall ctors, so the
raw bytes are the only place the object address survives.

At the 225 u/s run speed a pure sideways strafe exceeds `cl_rollspeed`, so it banks the full 2°;
a walk lands proportionally short of it.

*Provenance: `client.dll` decompiled in `$ELYSIUM_WORK_ROOT/research/ghidra/project_client`; defaults and ConVar object
addresses read out of the PE.*

---

## 6. Worldcraft camera tracks — `camera_track` / `camera_keyframe`

Maps drive their authored shots from a second, **server-side** system: a chain of keyframe
entities laid out in Worldcraft, sampled by the map's own think. It is independent of the client
weights of §2–§3 and meets them only where the client adopts the resulting view (`0x1017d280` /
`0x1017d460`).

*Provenance: `Vampire/dlls/vampire.dll`, imagebase `0x10000000`, static decompilation. Member
names are reconstructed, but the datamap records carry the external key names verbatim, so the
field ↔ key mapping is read rather than inferred.*

### The track is its own root key

`CCameraTrack` derives from `CCameraKeyFrame` — **the track entity is itself the first key of the
chain it plays**. The constructor `0x100cbe60` walks the vftable chain `0x104475c4` →
`0x104536ec` (`CCameraKeyFrame`) → `0x10453b9c` (`CCameraTrack`), and the scheduler reads
keyframe fields straight off `this`.

| Symbol | Address |
|---|---|
| `CCameraKeyFrame::Activate` (shared, vslot `+0x1c4`) | `0x100cb750` |
| `CCameraTrack::TrackThink` (datamap thinkfunc record `0x1055d20c`) | `0x100cc360` |
| the scheduler/sampler — one call per sub-chain | `0x100cc430` |
| start a chain: position (vslot `+0xbc`) / target (vslot `+0xb8`) | `0x100cc250` / `0x100cc1c0` |
| inputs `PlayAsCameraPosition` / `PlayAsCameraTarget` / `RestoreCameraToPlayerControl` | `0x100cc0e0` / `0x100cc090` / `0x100cc130` |
| player-side adopt | `0x1017d280` / `0x1017d460` |
| four-point Catmull | `0x1013b610` |

`CCameraKeyFrame` fields, from the datamap records `0x1055ceac`…`0x1055d090`:

| Offset | Field |
|---|---|
| `+0x450` | `m_iNextKey` — the authored `NextKey` target name (string) |
| `+0x454` / `+0x458` | resolved `m_pNextKey` / `m_pPrevKey` |
| `+0x45c` | `m_flRollDegrees` |
| `+0x460` | `m_fl35mmFocalLength` |
| `+0x464` | `m_bTimeControlsSpeed` |
| `+0x468` / `+0x46c` | `m_flMoveSpeed` / `m_flMoveTime` |
| `+0x470` | `m_flPauseTime` |
| `+0x474` / `+0x478` | `m_flRateIn` / `m_flRateOut` |
| `+0x47c` | `m_bCorner` |
| `+0x480` / `+0x498` | `m_OnReached` / `m_OnLeaving` |

`CCameraTrack` adds `m_bHoldAtEnd` `+0x4b0`, `m_flFromPlayerTime` `+0x4b4`, `m_flToPlayerTime`
`+0x4b8`, `m_OnCompleted` `+0x4f4` (external name `OnAnimationCompleted`), and **two independent
clocks over the same key chain**:

| Clock | `startTime` | `hKey` | `bPaused` | outputs |
|---|---|---|---|---|
| target | `0x4bc` | `0x4c0` | `0x4c4` | position `0x4c8` |
| position | `0x4d4` | `0x4d8` | `0x4dc` | position `0x4e0`, roll `0x4ec`, FOV `0x4f0` |

One entity can therefore be the position track and the target track at the same time, running two
unsynchronised walks of one chain. `startTime < 0` marks a clock inactive; the constructor's
default is `−1`. Only the position clock carries roll and field of view — the target clock
contributes a look-at point and nothing else, so the view direction is the position→target vector
rather than an authored yaw/pitch pair.

### `Activate` folds a short segment into a cut

`CCameraKeyFrame::Activate` rewrites the key **once at spawn**, before anything samples the chain.
The threshold is the constant `0x10453b74` = `0.05f`:

```c
if (m_flMoveTime <= 0.05f && m_bTimeControlsSpeed) {
    if (m_flMoveTime > 0) {
        if (m_pNextKey && m_pNextKey->m_flPauseTime > 0)
            m_pNextKey->m_flPauseTime += m_flMoveTime;   // re-attributed to the NEXT key's pause
        else if (m_flPauseTime > 0)
            m_flPauseTime += m_flMoveTime;               // else to this key's own pause
        // else the time is DROPPED and the chain is that much shorter
    }
    m_flMoveTime = 0;
    m_bCorner = true;
    if (m_pNextKey) m_pNextKey->m_bCorner = true;        // forced on BOTH ends
}
```

Three consequences the sampler cannot recover from, so they have to be reproduced at load:

- The folded time is **not spent where it was authored** — it lands at the start of a pause rather
  than the end of one, or is lost outright when neither key pauses.
- `Corner` is forced on **both** ends. That changes the segment-duration rule for any speed-driven
  segment arriving at the key (below) and collapses the Catmull endpoint selection to the segment
  endpoints.
- The rewrite is **order-sensitive**, exactly as retail is: a key whose pause was just raised from
  zero by its predecessor's fold is a legal re-attribution target for its own fold.

`Activate` also normalises `m_flRollDegrees` once, and clamps an out-of-range `FocalLength` to a
default derived from the same 18 mm half-gate as the FOV conversion — of the form `18 / sin k`,
from constants `0x10453b78` / `0x10453b88`. **The exact default is not pinned**; reading those two
float constants out of the PE settles it.

### The schedule

`0x100cc430(startTime*, hKey*, bPaused*, vecOut*, rollOut*, fovOut*, isTarget, lookahead)` walks
the chain from the root every call:

```c
if (*startTime < 0) return false;
elapsed = curtime - *startTime - this->m_flPauseTime;   // this == the ROOT key, at 0x100cc46c
cur = this;
while (elapsed >= 0) {
    if (resolve(*hKey) == cur && *bPaused) { *bPaused = false; Fire(cur->m_OnLeaving); }
    next = cur->m_pNextKey;
    if (!next) { *startTime = -1; Fire(m_OnCompleted); if (!m_bHoldAtEnd) release camera; return false; }
    segTime = cur->m_flMoveTime;                                  // 0x100cc52d
    if (!cur->m_bTimeControlsSpeed)
        segTime = |next->origin - cur->origin| /
                  (next->m_bCorner ? cur->m_flMoveSpeed           // Corner read off the DESTINATION, 0x100cc544
                                   : 0.5f*(cur->m_flMoveSpeed + next->m_flMoveSpeed));  // const 0x104454d0
    if (segTime > 0) {
        if (elapsed < segTime && elapsed > 0) { interpolate; return true; }
        elapsed -= segTime;
    }
    // segTime <= 0 consumes NO time
    if (resolve(*hKey) == cur) { *hKey = next; *bPaused = true; Fire(next->m_OnReached); }
    elapsed -= next->m_flPauseTime;
    cur = next;
}
// paused on `cur`: emit its position / roll / FOV verbatim
```

- **`Corner` is read off the destination key, not the departing one** (`0x100cc544`). A corner
  destination makes the segment run at the *departing* key's own `MoveSpeed`; otherwise the two
  endpoint speeds are averaged.
- **Sampling is per frame.** `TrackThink` calls the scheduler twice — position group, then target
  group — and re-arms `SetNextThink(curtime)` if either returned true.
- **The clock cannot drift.** `elapsed` is recomputed absolutely from `curtime − startTime` on
  every call; nothing accumulates.
- The root's `OnReachedKeyframe` fires on the frame the `PlayAsCamera*` input lands: `0x100cc250`
  stamps `startTime = curtime`, `hKey = root`, `bPaused = true`.

### Interpolation

Segment time is remapped by a Hermite curve on the two authored rates, `r0 = cur->m_flRateOut` and
`r1 = next->m_flRateIn` (constants `0x10452dc4` = `2.0f`, `0x10449258` = `3.0f`):

```
u = (r0 + ((r1 + r0 - 2)*t + (3 - r1 - 2*r0))*t)*t
```

The four-point Catmull (`0x1013b610`) picks its outer control points by `Corner`:

```c
p0 = (cur->m_pPrevKey  == 0 || cur->m_bCorner)  ? cur  : cur->m_pPrevKey;
p3 = (next->m_pNextKey == 0 || next->m_bCorner) ? next : next->m_pNextKey;
```

**Roll and field of view are packed into a second vector and run through the same four-point
Catmull as the position.** Two things follow: the authored focal length is converted to a field of
view *before* interpolating, not after; and roll is swept as a plain scalar with **no shortest-path
unwrapping** — the keys are normalised once in `Activate` and then swept literally, so 170° to
−170° travels the long way, through zero.

---

## 7. Reproducing it on Unreal 5.8

Implementation status is `docs/project/roadmap.md` 11.7. The sections below own the Unreal design and the
remaining RE questions; source owns the current type inventory.

### Constraints this project imposes

- **No `.uasset` authoring.** Every engine object is built in C++ at map-load
  (root `CLAUDE.md`). That rules out any asset-authored camera solution.
- **A console/cvar bridge already exists** (`ccmd`/`cvar`, roadmap 9.3b), so VtMB's cvar names can
  be reproduced 1:1 and the shipped `cfg/` + the patch's `user.cfg` aliases keep working verbatim.
- **One eye-point contract.** The boom hangs from the pawn's camera origin; player-model
  visibility is driven through the body seam in `docs/architecture/runtime-architecture.md`, not by a second
  camera or a camera-owned mesh.
- **Fully dynamic renderer**, HWRT Lumen + VSM, static lighting disabled
  (`docs/architecture/rendering-perf.md`).
- **Feel is reproduce-first, then polish by explicit owner call** (`docs/project/remaster-direction.md`).
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
two lines, so write it literally to match the decompiled constant.

### Collision and damping

`UWorld::SweepSingleByChannel` with a sphere of `cam_trace_radius` on `ECC_Camera`, ignoring the
pawn, is the direct equivalent of `UTIL_TraceHull` (VtMB uses a *box* hull with equal half-extents;
a sphere is the closer match to how it actually reads in motion, and is what `USpringArmComponent`
uses too — record the substitution here, it is a feel delta).

The damper is a Hooke spring with **two constants** — 4.0 free, 15.0 wall-clipped — the reason the
stock spring arm is not enough (below). Implement it explicitly; keep `cdamp_on 0` working as a
bypass for A/B.

### Options considered and rejected

| Option | Why not |
|---|---|
| **`USpringArmComponent`** with `TargetArmLength` lerped to 0 | The obvious idiom and fine for a prototype, but its lag model is an exponential interpolation on location/rotation, not a spring with a separate wall constant; its probe retracts instantly and springs back linearly; and there is no weight to hang the angle blend, the model fade and the crosshair switch on. Reproducing VtMB's damper inside it means subclassing it anyway, at which point the component buys nothing. |
| **`SetViewTargetWithBlend`** between two camera actors | Blends between *actors* over a fixed time with a fixed curve. Re-triggering mid-blend restarts rather than resuming from the current weight, and VtMB's toggle is symmetric and interruptible by design. It also implies a second view target, which the single-pawn model does not have. |
| **Gameplay Camera System** (UE 5.5+) | Still **Experimental** in 5.8 ("use caution when shipping"), and it is a *data asset* system — camera rigs authored in the editor. Directly contrary to the no-`.uasset`, code-built rule. Revisit if it stabilises and exposes a code path to build rigs without assets. |

### Player mesh, fade and first-person rendering

There is no player mesh yet, so this is design intent for when one lands — **roadmap 8.11a** owns
it, and the P8 skeletal path (8.2/8.5) supplies the machinery:

- **Visibility**: draw the mesh from the first frame of the blend, not switched at the end
  (`CAM_IsThirdPerson`'s "true throughout" behaviour, §2). Drive a MID scalar from the alpha
  rather than toggling `SetOwnerNoSee`; use `SetOwnerNoSee(true)` only as a cull when the weight
  is exactly 0.
- **The fade band** (`cam_fadeend` → `cam_fadestart`) is a near-camera dissolve, so it needs
  dithered or masked opacity on the character material, not translucency — translucency would
  take the player mesh off the opaque path and out of Lumen's GI, which is load-bearing here. The
  one state that quantises the alpha to 0/1 maps to a simple `bDitherEnabled = false` branch.
- **First Person Rendering** (UE 5.5+) already qualifies: its advanced features require *Allow
  Static Lighting* to be **disabled**, already the case, and its world-space-representation path
  needs VSM or ray-traced shadows, already the render path. If a first-person weapon/hands mesh
  is ever added, `FirstPersonPrimitiveType = FirstPerson` plus a `WorldSpaceRepresentation` twin
  gives correct HWRT reflections and VSM shadows without a second render pass. It is not needed
  for the camera itself.

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
the same rule the rest of the pipeline already applies (`docs/project/rebuild-strategy.md` →
*Coordinate conventions*).

### Console surface

Reproduced verbatim through the existing bridge: `togglecamera`, `thirdperson`, `firstperson`,
`cam_idealdist`, `cam_yaw`, `cam_targetangle`, `cam_collide`, `cam_trace_radius`, `cam_fadestart`,
`cam_fadeend`, `cdamp_on`, `cdamp_hookesconstant`, `cdamp_hookesconstantwall`,
`cdamp_springlength`, `cdamp_maxdist`, `c_minpitch`, `c_maxpitch`, `c_minyaw`, `c_maxyaw`,
`c_mindistance`, `c_maxdistance`, `cam_command`, `camera_prefs`, `camera_weaponswitch`.
The input layer binds `z` → the *command*, never a hardcoded key, so `kb_def.lst` and a user's
rebinds keep governing (`docs/vtmb/controls.md`).

`camera_prefs` / `camera_weaponswitch` only become meaningful once weapons exist; they are registered
anyway so the archived value survives round-tripping a user's `config.cfg`.

The whole set lives in the **VtMB console store** (`FElysiumConsole`), not as `elysium.*` engine
cvars, so `config.cfg` and the patch's aliases keep governing. Compiled code owns the defaults through
`FElysiumConsole::DeclareCvar(name, default)`: a declared name is a *known* cvar, so a bare
`cam_idealdist 50` resolves at step 3 of the precedence (command → alias → cvar → Python) instead of
falling through to the interpreter, the declaration survives a re-seed, and any cfg carrying the name
shadows it. The table itself is `ElysiumCam::CvarDefs()`, defaults typed exactly as a `config.cfg`
writes them; `FElysiumCameraCvars::LoadFrom` is the read and converts Source units to cm once.

**`camortho` is not implemented**: no orthographic path exists in the recovered client (§1). The verb
stays declared so a bind resolves and does nothing, which is what retail does.

An axis the player has not orbited or dollied follows its cvar **live**, so retuning `cam_idealdist`
or `cam_targetangle` moves the camera at once; `snapto` returns a hand-orbited axis to its cvar and
re-seeds the smoothing. The orbit/dolly step rates and the approach speeds are **ours** — no ConVar
holds them, and `CAM_Think`'s `kbutton_t` step (`0x100fc170`) is not recovered.

### The scripted-shot channel

The channel is a **handle-based** stack of shots-as-values (`FElysiumCameraShotStack`), not LIFO: a
conversation ends behind a running cutscene, so a pop removes a shot from wherever it sits and the top
re-resolves. Ids are never reused, so a stale or doubled pop is a no-op — the same discipline the input
scopes use (`docs/architecture/input-architecture.md`). Each shot carries its own **timed** ramp duration, matching
VtMB's scripted weight (`(now − startTime) / duration` off fields on the player entity) rather than
the toggle's fixed rate. A shot is pushed as origin / look-at / roll / FOV / rate limits; whoever
pushed it keeps those values current, which is what a `Follow` attach type is. The camera therefore
never learns what an entity is.

The substrate reaches the raw value channel through
**`IElysiumEmbodiment::PushCameraShotValue` / `UpdateCameraShotValue` / `PopCameraShot`**, not
`IElysiumPresenter`: the camera is part of the player's *body* (`docs/architecture/runtime-architecture.md` §5–6),
which is where `GetPlayerViewPoint` already lives. `SetCamera` owns one replaceable named-shot slot.
Worldcraft tracks instead own independent **position** and **target** streams in the map epoch;
`FElysiumEntityWorld` composes whichever streams are live into one raw shot. Restoring one owner does
not cancel the other, and the raw shot is popped only when neither stream remains. The composed track
shot sets `MaxTurnRate` to zero: the authored position/target samples already define the complete view,
so the generic moving-subject tracker must not add a second yaw/pitch scroll between them. Map teardown
clears all owners.

### `camera_track` / `camera_keyframe`

A `camera_track` is also its first keyframe (§6: `CCameraTrack` derives from `CCameraKeyFrame`, and
the scheduler reads key fields off the track entity itself). `NextKey` walks through `camera_keyframe` or another
`camera_track`; cycles and missing/wrong-class links terminate with one warning, and content tests
require the shipped opening chains to be complete and acyclic. `PlayAsCameraPosition` and
`PlayAsCameraTarget` select independently owned streams. Each role has one current track: a newer
selection supersedes the previous one, while the superseded track may continue its authored clock
and outputs without driving the view. `RestoreCameraToPlayerControl` (with `Restore` as a compact
compatibility alias) releases that entity's live roles; `HoldAtEnd` retains the final sample until
restore. `FromPlayerTime` is the push blend, `ToPlayerTime` the default pop blend, and an explicit
restore parameter overrides the latter. `OnReachedKeyframe`,
`OnLeavingKeyframe`, and exactly-once `OnAnimationCompleted` fire from crossed authored times,
including zero-duration chains.

Each key is reached before its timing is consumed. The root is reached immediately when playback
starts and fires `OnReachedKeyframe`; its `Pause` holds that first sample before
`OnLeavingKeyframe` and the first segment. Every later key follows the same reached → pause → leave
order. With `TimeControl`, the departing key's `MoveTime` is the following segment duration.
Otherwise duration is distance divided by endpoint `MoveSpeed`: the two endpoint speeds are averaged,
except that a **`Corner` destination** makes the segment run at the departing key's own speed — the
flag is read off the destination, not the departure (§6). Units convert once from Source units/s to
cm/s. `RateOut`/`RateIn` ease normalized segment time through the recovered Hermite remap; position
and a packed roll + field-of-view vector each run through the four-key Catmull form (duplicating an
endpoint at chain ends or corners). Focal length converts to a field of view **before**
interpolating, and roll sweeps plainly with **no shortest-path unwrapping** — keys are normalized
once at spawn, so a 170° → −170° edit travels the long way through zero, as retail does. The view
direction is the position→target vector, so there are no authored pitch/yaw to unwrap.
`PositionInterpolator` is parsed, retained, and shown in diagnostics but is a dead VtMB key with no
runtime effect.

A time-controlled segment is a **hard cut whenever its authored `MoveTime` is at or below 0.05 s**,
not only when it is exactly zero. Retail's `Activate` fold (§6) rewrites such a key at spawn —
zeroing `MoveTime`, forcing `Corner` on both ends, and re-attributing the time to the next key's
`Pause`, else to its own `Pause`, else dropping it — so the edit lands at the *start* of a pause
rather than the end of one. The fold is reproduced at load, in authored order, with the same
re-attribution priority. It carries the shipped corpus: sp_theatre's `courtroom_*` and `walk_out_*`
chains author 87 edits as `MoveTime 0.03`, and only its `embrace_*` chain uses exact zeros (28 of
them). Corpus-wide the authored `TimeControl` `MoveTime` values below 0.1 s are `0.03` ×87
(sp_theatre) and ×10 (sm_medical_1), `0.01` ×7 (sm_gallery_1), and `0.05` ×1 (sp_tutorial_1) —
without the fold every one of them reads as a 10–50 ms slew instead of a cut. The value-shot seam
also sets `MaxTurnRate` to zero so the generic moving-subject tracker cannot turn those authored cuts
into secondary camera pans. Crossing a folded or exact-zero edit, replacing a zero-blend track owner,
or popping a zero-blend top shot also marks Unreal's `bGameCameraCutThisFrame` and resets the
previous view transform at the single camera apply point.

**Divergence, by explicit owner call.** The fold threshold is exposed as the engine cvar
`elysium.CameraCutSeconds`, default `0.05` — retail's constant, so the default reproduces the
faithful behaviour above exactly. Setting it to `0` disables the fold and leaves the corpus's short
edits as authored slews. It exists only as an A/B switch over this finding, and it is deliberately an
`elysium.*` name rather than a VtMB one because retail holds the threshold in a code constant
(`0x10453b74`) with no cvar behind it. That one-frame signal invalidates temporal history.
While the scripted-shot stack has non-zero weight, the same apply point overrides motion-blur amount
to zero: Unreal's ordinary camera blur otherwise makes the opening's rapid authored dollies and
closely spaced edits read as continuous scrolling even when every sampled transform and cut boundary
is correct. The override leaves gameplay motion blur unchanged after the scripted channel releases.

The authored focal value is 35 mm focal length, not degrees. The unresolved client helper is isolated
behind the standard 36 mm horizontal-gate conversion
`FOV = 2 * atan(18 / focalMm)`; non-positive values preserve the player's FOV, where retail instead
clamps an out-of-range focal length once at spawn to a default of the same `18 / sin k` form (§6 —
the default's exact value is not pinned). Both streams and their
elapsed/output latches serialize with the map snapshot, so restore republishes the current sample
without replaying crossed outputs.

### `vdata/camerashots/` — the shot files

`SetCamera(char, shotfile)` names one of the 66 files under `vdata/camerashots/`
(`docs/vtmb/script_api.md`: 115 call sites). The grammar is **documented by Troika themselves** in the
shipped `camera shots how-to.txt`, so it is read rather than reconstructed. One file carries one shot,
named after the file:

```
CameraShotTable { <ShotName> { Start {…} End {…} Target { Point1 {…} Point2 {…} }
                               CameraConstraints {…} } }
```

- **`Start`** is where the shot begins. Absent, it starts from wherever the camera is — and for a
  camera with no position yet, from the player's standard view.
- **`End`** is where it transitions to. Absent, the camera does not move. *Both absent* makes the shot
  a pure target definition other systems (worldcraft keyframed cameras) borrow.
- **`Target`** is what it looks at. One point is tracked directly; **two are tracked at their
  midpoint**.
- Each anchor is `Position` × `AttachPos` × `AttachType` × `OffsetOrigin`:
  `Position` ∈ `Player` | `DialogTarget` | `GrappleTarget` | `World` | a named entity;
  `AttachPos` ∈ `Origin` | `Center` | `EyePosition` | `Top` | `Bottom` | `Bone: <name>` |
  `Attachment: <name>`; `AttachType` ∈ `Follow` (position + the offset rotated by the attachment's
  facing) | `FollowNoAngles` (position only) | `FollowEntAngles` (offset rotated by the entity's
  facing) | `None` (set once, stay put). `OffsetOrigin "[F, R, U]"` is Source units.
- **`CameraConstraints`** carries `MoveSpeed`/`MoveAccel` (inches/sec), `MaxTurnRate`/`TurnAccel`
  (deg/s), `DistanceTolerance`/`AngularTolerance` (how far the subject may drift before the camera
  follows), `FieldOfView`, `DialogPOV` (NPCs look at the camera rather than the player's eye),
  `AutoPositionFromTarget`, `SyncRotateOnMove`, `SnapOnShotChange`, `ShowHud`, `DrawViewmodel`.

**`OffsetOrigin`'s Y is positive-right, not Source's positive-left.** The how-to spells the axes as
`[Forward/Backward, Right/Left, Up/Down]` and reads its own `[40, -10, 25]` example as "10 to our
left", so this designer-facing offset is already Unreal's local frame and is the one Source-authored
vector in this repo that takes **no Y negation**. World positions still do. The corpus writes `Y = 0`
on nearly every dialogue shot, so the theatre (12.x) is what will confirm it against a shot that does
not.

`MoveSpeed` and `MaxTurnRate` limit the shot **tracking a moving subject** — not its arrival. A shot
arriving is the *weight ramp*; the shot itself starts where it was authored.

### Verification hooks

- `Elysium.Substrate.Camera` (`uv run elysium test Substrate`, `-nullrhi`) over the weight driver and the shot
  stack alone: 0→1 in 0.5 s at time scale 1, frame-rate independence, correct scaling by time scale,
  clamping, the priority order, symmetric resume on a mid-blend reversal, and the stack's
  out-of-order pop / no-op double pop / ramp-preserving refresh.
- `Elysium.Substrate.CameraShots` reads a `dialogdefault`-shaped file and asserts it against the
  how-to. `Elysium.Substrate.CameraTrack` covers authored duration/pause/easing, four-key spatial and
  focal sampling, shortest-path roll, zero/30 ms cuts, independent owners, exact track-shot rotation,
  holds/restores, outputs, blend times, and snapshot restore.
- `elysium_player_get` (MCP) reports the view mode, the deciding latch, the weight, the scripted
  weight, the solved boom length, the model alpha and the live shot name, so an agent can drive the
  toggle and assert the transition. `elysium.camera` is the same state as a console dump.
- `uv run elysium debug shots` vantages captured at weight 0, 0.5 and 1 give a look-regression baseline for the
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

Server-side, in the camera-track system (§6, `vampire.dll`):

- The `sp_endsequences_b`-gated branch at `0x100cc6c1`, where a zero-length segment recursively
  drives the *paired* sub-chain with a 0.1 s lookahead, together with the matching lookahead
  skip-ahead at `0x100cc5af`. Both are map-specific and unreachable from `TrackThink` on any other
  map, so only a trace of `sp_endsequences_b` playing its own chain would establish what they do.
- The default `FocalLength` that `Activate` clamps an out-of-range authored value to. It has the
  form `18 / sin k`, from constants `0x10453b78` / `0x10453b88`; reading those two floats out of the
  PE settles the number.
