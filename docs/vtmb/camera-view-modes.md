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

The weapon-class bitmask lives at `+0x2440` on the **equipped item's record**, not on the player.
Both readers reach it through `0x1007b160`, which is
`mov ax,[player+0x95e]` (a 16-bit item id) → `push eax` → `call 0x101a4770` (the item-record lookup)
→ the caller then does `mov esi,[eax+0x2440]` off the returned record [decompile-verified]. By the
same helper, `+0x24d0` and `+0x4fe84` below are item-record fields too.

The value is authored. Every `vdata/items` record carries a symbolic **`camera_class`**, parsed by
an inlined case-sensitive `memcmp` ladder inside the item-record vdata parser — `client.dll`
`0x101a5394`–`0x101a5438` (parser at `0x101a48b0`) and `vampire.dll` `0x1025aa65`–`0x1025ab08`
(parser at `0x10259f80`), byte-identical in both [data-verified]:

| `camera_class` | Bit | Effect | Shipped items |
|---|---:|---|---|
| `ranged` | `0x02` | settable; first person under `camera_prefs 6` | 18 — every firearm, the crossbow, the flamethrower, three Discipline records |
| `thrown` | `0x04` | settable; first person under `camera_prefs 6` | 4 — throwing star, frag grenade, Chang's two |
| `force_1st` | `0x08` | always first, no preference written | 4 — the lockpick and three physics-gun dev items |
| `melee` | `0x10` | `ForceThirdPersonOn`; always third, no preference written | 20 — fists, katana, baseball bat, baton, knife, sledgehammer, tire iron, fire axe, torch… |
| `force_3rd` | `0x10` | the same class under a second spelling | 0 — no item authors it |
| anything else, including `noswitch`, and an absent key | `0` | the early-out; equipping changes nothing | 119 authored `noswitch` — every quest item and key, plus `item_w_unarmed` and the ghoul/Protean claws |

Three consequences the bits alone do not show. **`melee` *is* the engine's force-third class** rather
than a class that merely defaults third, so a melee weapon cannot be held in first person. **`noswitch`
is not a recognized literal** — the string occurs in neither DLL and reaches `0` through the same
fall-through as a typo or a missing key, so the authored vocabulary is a convention, not a checked
enum, and a mis-cased `Ranged` would silently read as `0`. And **bit `0x01` is dead**: no ladder arm
produces it and no item carries it, so `camera_prefs`' default `6` and the shipped `7` differ only in
a bit nothing can match.

Two functions read the class:

```c
// 0x1009c250 — re-arbitrate on weapon change
void ApplyWeaponCameraPref()
{
    int cls = EquippedItemRecord()->cameraClass;        // 0x1007b160, then +0x2440
    if (!cls || !camera_weaponswitch.GetInt()) return;
    if (cls == 0x08) { input->CAM_ToFirstPerson(); return; }   // always first person
    if (cls == 0x10) { input->ForceThirdPersonOn(); return; }  // always third person
    if (cls & camera_prefs.GetInt()) input->CAM_ToFirstPerson();
    else                             input->CAM_ToThirdPerson();
}

// 0x1009c2e0 — remember what the player just chose, for this weapon class
void SaveWeaponCameraPref(bool bThirdPerson)
{
    int cls = EquippedItemRecord()->cameraClass;        // same helper
    if (cls == 0x08 || cls == 0x10 || cls == 0) return;        // not user-settable
    int prefs = camera_prefs.GetInt();
    prefs = bThirdPerson ? (prefs & ~cls) : (prefs | cls);     // set bit = first person
    camera_prefs.SetValue(prefs);
}
```

So `togglecamera` is **sticky per weapon class and persisted to `config.cfg`**: toggle to third
person while a gun is out and every future draw of that class starts in third person.
Class `0x08` is pinned to first person and class `0x10` to third person, and neither writes a
preference. Default `camera_prefs 6` sets bits `0x02` (`ranged`) and `0x04` (`thrown`) to first
person; bit `0x01` is set by the shipped `7` but matches no class.

**Live retail confirms the mapping independently of the ladder.** An install carrying
`camera_prefs "3"` — `0x02` set, `0x04` clear — plays a firearm in first person and throws a grenade
in third. The reversed assignment would produce the opposite pairing, so `ranged` = `0x02` and
`thrown` = `0x04` are fixed by behaviour as well as by the parsed constants. In the same install a
melee weapon cannot be brought to first person at all, which is the `0x10` force.

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

### The ordinary feed camera is automatic

The feed-family weight at `+0x138` is a separate layer over the ordinary third-person result. In
the ordinary feed state, `0x100fc900` increases it by `dt` and clamps it to `[0, 1]`; after the
state clears, it decreases by the same `dt`. Entry and exit therefore each take one second under
the player's time scale. The same updater forces the third-person weight upward at `2*dt`. When
feed weight first leaves zero it stores the engine time at `+0x13c` and the current view yaw at
`+0x140`; on exit it captures the outgoing camera state so a reversal continues from that state
rather than cutting. The feed layer is consumed through `SimpleSpline(weight)`. `[VtMB]`

The ordinary branch of the dedicated solver `0x100fe7f0` uses

```text
t       = max(0, (engine_time - entry_time) * player_time_scale)
yaw     = entry_yaw + camfeed_yaw * t
pitch   = min(camfeed_pitch_max,
              camfeed_pitch - camfeed_pitch * pow(camfeed_pitch_pow1,
                                                    camfeed_pitch_pow2 * t))
roll    = camfeed_roll
offset  = camfeed_forward_base * pow(t, camfeed_forward_pow)
```

The retail defaults are `camfeed_yaw=50`, `camfeed_pitch=80`,
`camfeed_pitch_max=60`, `camfeed_pitch_pow1=2`, `camfeed_pitch_pow2=-0.35`,
`camfeed_roll=0`, `camfeed_forward_base=-50` and `camfeed_forward_pow=0.5`. Thus the view makes a
full yaw revolution in 7.2 seconds, pitch starts at zero and reaches its 60-degree clamp after
about 5.714 seconds, and the camera recedes by `50*sqrt(t)` along the computed view direction. The
resulting angles and offset are eased over the previously solved view by the spline-weighted helper
at `0x100fe600`. Registered `camfeed_yaw_end=2.0` and `camfeed_pitch_min=0` have no reads in this
ordinary solver. `[VtMB]`

No current mouse/look angle participates after `entry_yaw` is captured. The rendered feed camera is
therefore **look-locked and automatically orbiting**, even if lower-level input state continues to
update. The solver performs no trace or collision query: ordinary third-person collision solves
first, then the feed layer overwrites/blends the view. The exact suppression of non-look gameplay
buttons remains a separate input question. The desaturated circular feeding-view renderer driven by
the same weight is specified in `docs/vtmb/feeding.md`.

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
6. If the second weight (`0x108`) is > 0, run `0x100fdfa0`; if the feed-family weight (`0x138`)
   is > 0, run `0x100fe7f0`.
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

*Elysium divergence, owner-called.* The recovered sweep is a `UTIL_TraceHull` **box** of half-extents
`±cam_trace_radius`; Elysium sweeps a **sphere** of that radius on its own camera channel. Geometry
queries belong to the engine (`docs/project/reconstruction-direction.md` → the Ownership test), so the
recovered *rule* — probe from the eye toward the desired point at the authored radius, allow
`dist * fraction`, skip entirely under `cam_collide 0` — is reproduced while the query itself is
Unreal's. A sphere rounds the corners a box would catch, so the camera clears a doorway jamb slightly
earlier than retail's.

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
| **World weapon / attachments** | `0x100a7a50`, `0x100aef40` | entities owned by the local player draw only in third person (`IsLocalPlayerEntity` `0x100a99d0` + `CAM_IsThirdPerson`). `0x100a7a50` is `C_BasePlayer::ShouldDrawLocalPlayer`, and an adopted cine camera **short-circuits** this rule off the replicated `m_bDrawPlayer` — see "The draw gates and the HUD mask" |
| **Crosshair / look cursor** | `0x1009b9e0` | third person draws the plain white reticle at the crosshair rect; first person runs the full use-icon/arrow cursor path (see `docs/vtmb/entity_io.md`) |
| **An entity/handle lookup** | `0x100a99b0` | returns the result of `0x1008f720` in third person and null in first — most plausibly the player model to attach effects to; the callee is not identified |
| **Particle systems** | `0x10147be0` (client), `0x101dc8a0`/`0x101dce30` (server) | particle definitions carry `Particle_FirstPerson` / `Particle_ThirdPerson` blocks; emitters are tagged with a mode index at parse time (`0` = third, `1` = first) |
| **Equipped item** | `CAM_ToggleCamera` | toggling out of a forced-third weapon runs `inven_holster` |
| **Sniper zoom** | `CAM_ToggleCamera` → `force_sniper_third_person` | a server `ConCommand` in `vampire.dll` (`0x100d61a0`), help text *"called when the sniper rifle is forced into third person."* |
| **Persistence** | `camera_prefs` | the choice is written back per weapon class and archived to `config.cfg` |

**What it does not affect**: movement (the character still steers by view yaw — the camera is a
view-space offset, nothing is reparented), the aim/attack origin, FOV, or input sensitivity.

### The render hand-off is a cut around a blended camera

Draw policy reads `CAM_IsThirdPerson`, not the smoothed third-person weight. The viewmodel gate in
`ShouldDrawViewModel` (`0x10198ea0`) calls the predicate through `CInput` slot `+0x74` and rejects
both first-person viewmodel slots whenever it is true. The body and carried-world-model gates call
the same predicate with the opposite sense. Consequently the first rendered frame after a mode
request behaves as follows [static-verified]:

| Surface | First → third | Third → first |
|---|---|---|
| Camera origin / angles | begin the 0.5 s weight ramp away from the eye | begin the 0.5 s weight ramp back to the eye |
| First-person hands + weapon | stop submitting immediately | stay suppressed throughout the return; resume only when the weight reaches exactly `0` |
| Full local-player model | becomes draw-eligible immediately, but starts at alpha `0` while the solved camera remains within `cam_fadeend` | remains draw-eligible throughout the return and fades with solved camera distance; stops submitting when the weight reaches `0` |
| Carried world weapon / owned attachments | become draw-eligible immediately | remain draw-eligible until the weight reaches `0` |
| Crosshair path | switches immediately to the plain third-person reticle | remains the third-person reticle until the weight reaches `0`, then returns to the first-person use-icon/arrow path |

The body fade is the only recovered soft render hand-off: solved eye-to-camera distance at or below
`cam_fadeend` (18 Source units) gives alpha `0`, distance at or above
`min(cam_idealdist, cam_fadestart)` (normally 32 units) gives alpha `1`, and the band between uses
`SimpleSpline`. The carried-world-model gates above are boolean; no matching attachment-alpha
consumer is recovered. Thus "third person" may already be true while the body is still fully
transparent, particularly on entry or while collision holds the boom against a wall.

Suppressing a first-person viewmodel is submission-only. Both viewmodel entities retain their
models, sequences, cycles and independent bone palettes while hidden, so the final third → first
frame resumes the existing visual state rather than spawning or resetting hands and weapon. The
ordinary mode toggle does not hide the rest of the HUD; it selects the other crosshair path.

### The viewmodel has its own projection and draw lifetime

The first-person hands/weapon pass does not reuse the world projection [static-verified]. Client
`SetupView` `0x10191710` writes independent world and viewmodel fields into `CViewSetup`:
`fov`/`fovViewmodel`, `zNear`/`zNearViewmodel`, and `zFar`/`zFarViewmodel`. `DrawViewModels`
`0x10198fa0` forwards only the viewmodel triplet through `VEngineRenderView008` slot `+0x84`;
the pinned engine wrapper `0x2010f280` passes it to projection builder `0x2007a3e0` with the normal
perspective flag. The world setup reaches the same builder separately. There is no addition,
subtraction or ratio involving the player FOV:

```text
t = tan(viewmodel_fov * pi / 360)
xScale = 1 / t
yScale = aspect / t
```

Thus `viewmodel_fov 54` remains 54° when the player's FOV changes. The viewmodel clipping range in
the pinned build is **1..28400** Source units; the ordinary world range is seeded separately (near
plane 8). The dedicated near plane is what lets the camera-space weapon survive close to the eye.

The builder's normal aspect policy is **4:3**. It selects **16:9** when `r_anamorphic` is enabled;
an alternate projection flag selects 1:1, but the viewmodel caller passes false and never takes that
branch. This is a projection rule, not an asset transform: both viewmodel palettes remain in their
shared `Camera01` space while the second projection maps them to the viewport.

Visibility is evaluated per slot on every draw. In order, `ShouldDrawViewModel` `0x10198ea0`
applies the caller's draw flag, model presence, local-player/observer state, global entity drawing,
`r_drawviewmodel`, engine broadcast state, the internal viewmodel policy, player-body visibility
state and the current view entity. A false result suppresses submission only. It does not destroy
either `C_BaseViewModel`, clear its model, reset its sequence/cycle, or transfer animation
ownership. A `DrawViewmodel 0` → `1` transition therefore resumes the existing two-entity visual
state rather than recreating it.

*Elysium divergence, owner-called.* Retail's melee `camera_class` forces third person. When the
retained-first-person option prevents that camera move, melee renders **neither hands nor weapon**;
the authored hands bank has no melee family and the project does not invent one.

### The viewmodel is placed by its own per-frame transaction

The projection above maps the pass to the viewport; where the two entities *sit* is decided
separately, once per slot, by `CViewRender::CalcViewModelView` (`0x10190ba0`) [static-verified].
`CalcView` (`0x10191200`) calls it in a two-iteration loop over slots 0 and 1, passing the slot
index, the player origin, the frame's view angles, the player's `+0x148` offset vector and the
water offset. Both helpers it calls take only the slot and refetch the same per-slot record.

```text
origin = playerOrigin + player(+0x148) ; origin.z += waterOffset ; angles = viewAngles
CalcViewModelAngle(slot)                       // idle drift, below
if (activeWeapon) {
    CalcViewModelLag(slot)                     // turn lag, below
    origin = VectorMA(origin, -0.1, forward)   // pull back along the view forward
    blend(origin, angles, 0.25)
}
switch (viewmodel_fov) { 80: z += 0.5 ; 90, 110: z += 1.0 ; 100: z += 2.0 ; default: — }
if (slot != 0) origin += CViewRender(+0x4) projected onto the view basis
```

**Two of these are inert in a stock configuration.** `viewmodel_fov` defaults to `54`, which takes
the switch's `default:` arm, so the z nudge only appears when the player moves it to one of those
four values; and the three view-origin weights `CalcView` blends onto the view basis are
`scr_ofsx`, `scr_ofsy` and `scr_ofsz`, all defaulting to `0`.

**The idle drift is off whenever a weapon is held.** `CalcViewModelAngle` (`0x1018fcb0`) subtracts
`sin(cycle · time) · level · 4.0` from three angle components — pitch taking an extra `0.5` factor —
from `v_ipitch_cycle` 1 / `v_ipitch_level` 0.3, `v_iyaw_cycle` 2 / `v_iyaw_level` 0.3,
`v_iroll_cycle` 0.5 / `v_iroll_level` 0.1. The whole body is gated on there being **no** active
weapon, so on the only frames a viewmodel is drawn it contributes nothing.

**The lag has no discriminator.** `CalcViewModelLag` (`0x1018fb70`) keeps a smoothed facing vector on
the view renderer at `+0x458`, approaches it toward the current forward at rate `5.0`, negates the
residual and adds `5.0 ×` it to the origin — the classic swing-and-settle. Its gate is the weapon's
vtable `+0x3dc` (slot 247), which is `return 1` in the one implementation all 214 classes that fill
that slot share. Lag therefore runs whenever an active weapon exists; only an unarmed frame skips
it.

**The stair-step smoothing reaches the viewmodels.** `CalcView` keeps one persistent smoothed
player-origin z, approaches it at `150` units/second, clamps it to an `18`-unit band below the
player's own z, and adds the same `(smoothed − actual)` term to the view origin **and to both
viewmodel slots**. The `+0x400` player-state branch that replaces the `+0x148` offset instead bumps
view z by `64.0`.

**One alternate source exists and is unidentified.** When the active weapon passes a three-part gate
on `+0x45c`, `+0x465` and `+0x4a4`, both origin and angles are taken from a cached transform on the
weapon at `+0x468`..`+0x47c` instead of from the view, and lag, the `-0.1` step and the `0.25` blend
are all skipped. What populates those fields is not recovered. **What would close it:** the writers
of `+0x468` and its gate fields.

The blend calls route through the interface at `PTR_DAT_102d5238` (slots `+0x18`/`+0x1c`/`+0x20`,
shaped as a weighted origin-and-angle commit); its concrete identity is not recovered, and one of the
seven stack arguments `CalcViewModelView` receives is never read inside the body.

*Provenance: `client.dll` static decompilation; ConVar names, defaults and the float constants read
from the shipped PE, since the registration stubs' `mov ecx, imm32` does not survive decompilation.*

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
`0x1017d460`), which it does through the scripted weight at `CInput+0x100` — one of
`CAM_IsThirdPerson`'s disjuncts, and the only client-side blend weight in the whole scripted-camera
surface (see "The `camera_track` override channel"). Starting it transfers the view, not the player: it does not teleport or immobilise
the pawn, start a choreography, or create a cinematic body double. Maps and scripts author those
operations separately (`docs/vtmb/choreographed_scenes.md`). Once the client adopts the scripted
view, `CAM_IsThirdPerson` is true: the first-person hands/weapon pass is suppressed, while the
local full body and carried world model become draw-eligible subject to their ordinary gates (§5).

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
- When a selected stream reaches the tail with `HoldAtEnd == 0`, the scheduler verifies that the
  player's matching position/target handle still names this track and then restores player camera
  control. That restore releases the complete position/target camera session, not only the stream
  whose clock completed. A superseded stream still completes and fires outputs but fails the handle
  guard, so it cannot tear down the newer camera.
- `RestoreCameraToPlayerControl` is the explicit form of the same player-level operation. Its retail
  input handler accepts the restore only when this entity is still the player's current position
  track; the float parameter is the return blend. It does not stop the entity's authored clocks.

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

## 7. Rebuild compatibility surface

Implementation status is `docs/project/roadmap.md`. This document owns the faithful evaluator,
original script/map inputs, and remaining RE questions. The shipped player/dialogue/focus/cinematic
architecture is `docs/architecture/camera-architecture.md`.

### Integration boundary

- The recovered weight ramp, priority latches, draw policy, fade band, cvar surface, named shot
  grammar, and map-track scheduler are reproduced and are the compatibility surface.
- `UElysiumCameraComponent` owns those recovered rules — the four weights and three latches, the
  scripted-shot stack, the fade band and the draw policy. It is not the final camera authority and
  does not arbitrate dialogue, focus, Sequencer, or player modes itself; the manager does.
- The console/cvar bridge reproduces VtMB's camera names so `config.cfg`, patch aliases, and original
  scripts continue to resolve. Those settings tune the recovered rules and the one boom, not the
  Elysium's user preference or project-authored profiles.
- Game-derived `vdata/camerashots/`, map entities, and VCDs stay external runtime inputs. Original
  project camera profiles and Level Sequences may be authored under `/Game/ElysiumAuthored/**` but
  do not replace source data silently.
- Original map-track timing is already authoritative. Its evaluator publishes sampled values into
  the shared request surface without a second spring, turn tracker, or blend.

### Divergence, owner-called — no first-person weapon camera

Elysium is a pure third-person game for ranged and thrown weapons as well as melee: the
`camera_class` arbitration in §2 (`ranged`/`thrown` defaulting to first person under
`camera_prefs`, the player-settable toggle, `togglecamera`/`thirdperson`/`firstperson`, and the
first↔third weight blend of §3) is retired. Every weapon behaves like retail's `melee` class —
always third person, camera never approaches the eye. `viewmodel_fov`, the dedicated viewmodel
projection (§"The viewmodel has its own projection and draw lifetime") and the first-person
hands/weapon draw path have no consumer.

In its place, ranged combat gets a hip-fire/aim-mode pair retail never authored — VtMB's only
aiming mechanic *was* the first-person switch. Both states run the same third-person boom; only
its target transform changes, blended with the same weight-ramp/ease shape as the retired
first↔third toggle (§3) rather than a hard cut:

- **Hip-fire** (default): the ordinary third-person boom, at the player's chosen FOV.
- **Aim mode** (held/toggled): boom pulls in, offsets toward one shoulder, and FOV narrows per
  weapon; the ranged spread cone (`docs/vtmb/combat-and-damage.md` → "Shot count, accuracy and
  kick") is multiplied down while held.

| Weapon | Aim FOV (% of hip) | Shoulder offset (right/up, cm) | Aim spread multiplier |
|---|---:|---|---:|
| Glock 17c, .38 revolver, Desert Eagle | 88% | 35 / 15 | ×0.5 |
| Colt Anaconda (aimed mode) | 85% | 35 / 15 | ×0.45 |
| Colt Anaconda (fan mode) | no aim state — spray by design | — | ×1.0 |
| Uzi, Mac-10 | 92% | 30 / 10 | ×0.7 |
| Steyr AUG | 80% | 35 / 15 | ×0.4 |
| Ithaca M37, super shotgun | 95% | 30 / 10 | ×0.9 |
| Remington M700 | 65% | 40 / 20 | ×0.1 |
| Crossbow | 70% | 35 / 15 | ×0.15 |

### The lens (2026-09-07)

**Corrected.** This paragraph previously stated a 90° horizontal hip-fire baseline and "Elysium ships
a user-facing FOV slider". Neither existed in code: nothing set the gameplay FOV at all, so the player
view ran on `UCameraComponent`'s default 90 with Unreal's default horizontal-held aspect constraint —
a fixed horizontal angle that *crops vertically* as the window widens, which is the opposite of
Source's Hor+. There is no FOV slider.

Retail is reproduced instead. `default_fov` (75) and `viewmodel_fov` (54) are declared into the VtMB
console store beside the `cam_*` set, so a user's `config.cfg` and the patch aliases govern them; both
are **horizontal angles at Source's 4:3 reference under the Hor+ rule**
(`vfov = 2*atan(tan(hfov/2)/(4/3))`, `docs/vtmb/source_movement.md` → "View / camera"). The window's
own aspect widens them at the point of use through the one conversion,
`ElysiumCam::WidenSourceFov` — `default_fov 75` renders ≈ 91.3° horizontal at 16:9 and exactly 75° at
4:3. The same function serves every `vdata/camerashots/` `FieldOfView`, so a scripted shot's weight
lerps two angles that are in the same space.

`viewmodel_fov` is declared, loaded and readable as `FElysiumCameraCvars::ViewmodelFov` and **has no
consumer**: the port has no first-person viewmodel renderer (only `SolveDrawPolicy`'s
`bViewmodelEligible` gate, which nothing draws from). It stands as the seam for retail's ConVar.

Each weapon's aim percentage in the table above is relative to whatever `default_fov` resolves to, not
an absolute value; no aim-FOV path is implemented yet, so the percentages are unretuned plan.

Faithful first/third behaviour — the toggle, the weight ramp, `camera_class` arbitration, and the
dedicated viewmodel projection — stays recoverable through git history and the RE record above;
no A/B mechanism or cvar toggles between the two.

### Faithful evaluator shape

The faithful path preserves VtMB's one weight and one solve order: advance the third-person weight
at **2.0/s × player time scale**, apply the recovered latch priority, smooth with
`SimpleSpline(t) = t²(3−2t)`, approach distance/yaw/pitch, run the camera collision trace, integrate
the two-constant Hooke damper, and blend offset/rotation once. `cdamp_on 0` remains the direct A/B
bypass.

This path deliberately does not constrain Elysium's third-person rig. The modern player rig uses
Unreal's Spring Arm obstruction model, independent camera orbit and explicit character-facing
policies; the camera manager arbitrates its output with every scoped request. `SetViewTargetWithBlend`
is not a public gameplay API, and Unreal's experimental Gameplay Camera System is not the production
foundation. Those owner calls and their reasoning live only in
`docs/architecture/camera-architecture.md`.

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

#### Dialogue source-shot demand

The patch-first exported corpus contains **251** entities with `default_camera` across 15 maps,
written in **11 literal forms**, and the external camera-shot directory contains **66** files
**[data, RE46]**. Values occur both as bare names (`DialogDefault`, `Jack`) and as mixed-case paths
such as `vdata/CameraShots/DialogDefaultWoman.txt`. Resolution is therefore
separator-insensitive, case-insensitive basename-without-extension normalization; spelling form is
not shot identity.

Jack's `sp_tutorial_1` definition selects `Jack`. The corresponding source shot authors a
`DialogTarget` `Follow` origin offset, a `DialogTarget` head target, FOV 40, `DialogPOV 1`, and
`SyncRotateOnMove 1` **[data]**. This establishes shot demand and its value fields, not physical
player/NPC placement. The pinned server opener does not write transforms and ignores the authored
`Remote 256` argument; downstream client/body behavior remains behind RE46's hash-gated capture.

The source-shot offset is expressed in the subject's authored character basis: a baked character's
forward is component +X and Source yaw is reflected once into Unreal. The legacy-shot adapter and
the character body therefore use the same `ElysiumSkeletalBasis::FromSourceAngles` conversion.
For UP Jack at Source yaw 190, `[50, 0, 65]` lies 50 authored units along his rendered forward and
65 units above his origin; no extra model yaw or camera-side facing correction is applied. This is
a coordinate/basis fact and does not imply that retail turns Jack before the shot.

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
not cancel the other for an internal role-local replacement or stop. Returning **player control** is
different: a current non-held completion or `RestoreCameraToPlayerControl` clears both selected roles
and pops the composed shot, while its stale-owner guard prevents an older clock from tearing down a
newer camera. The composed track shot is **direct** (`FElysiumCameraShot::bTracked` false): the
authored position/target samples already define the complete view, and retail applies this channel
through `CInput`'s override rather than the cine-camera tracker (see "`CamMode`" below), so no
second yaw/pitch scroll is ever added between them. Map teardown clears all owners.

### `camera_track` / `camera_keyframe`

A `camera_track` is also its first keyframe (§6: `CCameraTrack` derives from `CCameraKeyFrame`, and
the scheduler reads key fields off the track entity itself). `NextKey` walks through `camera_keyframe` or another
`camera_track`; cycles and missing/wrong-class links terminate with one warning, and content tests
require the shipped opening chains to be complete and acyclic. `PlayAsCameraPosition` and
`PlayAsCameraTarget` select independently owned streams. Each role has one current track: a newer
selection supersedes the previous one, while the superseded track may continue its authored clock
and outputs without driving the view. `HoldAtEnd` retains a selected stream's final sample; otherwise
its completion returns the complete paired camera to player control. `RestoreCameraToPlayerControl`
(with `Restore` as a compact compatibility alias) performs the same session-level return when the
receiver is still the current position track. `FromPlayerTime` is the push blend, `ToPlayerTime` the
default completion blend, and an explicit restore parameter overrides the latter. In retail those two
keyvalues reach the fade machinery through the virtual pair `GetCameraFadeInTime` (`vfunc0xD0`) and
`GetCameraFadeOutTime` (`vfunc0xD4`), each `max(0, keyvalue)`, and each is a **minimum** raised over
the caller's requested crossfade rather than an assignment — see the override-channel subsection
below. `OnReachedKeyframe`,
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
is also direct (`bTracked` false), so the `C_BaseCineCamera` tracker cannot turn those authored cuts
into secondary camera pans — or, with the shot's zero rates, freeze the aim. Crossing a folded or exact-zero edit, replacing a zero-blend track owner,
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
the default's exact value is not pinned). Both streams and their elapsed/output latches are runtime
session state. Elysium refuses a save while an authored legacy or Sequencer camera track is
active, so those latches are neither serialized nor republished after load
(`docs/architecture/save-architecture.md`).

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
  facing) | `None` (offset in world axes). `OffsetOrigin "[F, R, U]"` is Source units.
- **`CameraConstraints`** carries `MoveSpeed`/`MoveAccel` (inches/sec, inches/sec²),
  `MaxTurnRate`/`TurnAccel` (deg/s, deg/s²), `DistanceTolerance`/`AngularTolerance` (the **deadbands**
  the camera parks inside — see the tracker below), `FieldOfView`, `DialogPOV` (NPCs look at the
  camera rather than the player's eye), `AutoPositionFromTarget`, `SyncRotateOnMove`,
  `SnapOnShotChange`, `ShowHud`, `DrawViewmodel`.

`ShowHud` and `DrawViewmodel` both parse with a default of `0`; an absent field therefore asks the
shot to hide that surface. The corpus uses explicit opt-ins for interaction shots rather than story
cinematics: `special-case.txt` sets `ShowHud 1` for Hacking and sets both fields to `1` for Intrusion.
This is the named `SetCamera` shot policy, not a key on Worldcraft `camera_track`. Independently,
any adopted scripted camera satisfies `CAM_IsThirdPerson`, whose client draw gate suppresses the
ordinary first-person viewmodels even if another policy would otherwise permit them.

**`OffsetOrigin`'s Y is positive-right, not Source's positive-left.** The how-to spells the axes as
`[Forward/Backward, Right/Left, Up/Down]` and reads its own `[40, -10, 25]` example as "10 to our
left", so this designer-facing offset is already Unreal's local frame and is the one Source-authored
vector in this repo that takes **no Y negation**. World positions still do. The corpus writes `Y = 0`
on nearly every dialogue shot, so the theatre (12.x) is what will confirm it against a shot that does
not.

`MoveSpeed` and `MaxTurnRate` limit the shot **tracking a moving subject** — not its arrival. A shot
arriving is the *weight ramp*; the shot itself starts where it was authored.

### The shot record, the anchor resolve, and the client tracker (2026-09-07)

Recovered whole, because the port had parsed half of `CameraConstraints` and read none of it — which
is why a conversation camera moved with the NPC's idle and framed tighter than retail's.

**The record.** Shot table stride `0x104`, parser `FUN_100721e0`. `+0x20` flags: `0x01` Start,
`0x02` End, `0x04` Point1, `0x08` Point2, `0x10` DialogPOV, `0x20` AutoPositionFromTarget, `0x40`
DrawViewmodel, `0x80` SnapOnShotChange, `0x100` SyncRotateOnMove, `0x200` ShowHud. `+0x00` the shot
name (`Q_strncpy` of the KeyValues name into 0x20 bytes), `+0x24`/`+0x50`/`+0x7c`/`+0xa8` the four
0x2c-byte anchor records (Start, End, Point1, Point2), **`+0xD4` the count of `Target` sub-blocks the
parser found (0, 1 or 2)**, `+0xD8` MoveSpeed, `+0xDC` MoveAccel, `+0xE0` TurnAccel, `+0xE4..0xEC`
MaxTurnRate[3], `+0xF0..0xF8` AngularTolerance[3], `+0xFC` DistanceTolerance, `+0x100` FieldOfView.
`FUN_1006eeb0(this, i)` returns anchor `i`; `FUN_1006ee10(this, i)` returns that anchor's flags.

**`+0xD4` is the Target-point count, not a key — correcting this document.** The `CamMode` section
below previously recorded `+0xd4` as a gate "whose parse key is not identified". There is no key: the
parser increments a counter per `Target` sub-block it finds and stores it there.

```c
pvVar3 = KeyValues::FindKey(param_1,"Target",false);
if (pvVar3) {
  pvVar4 = FindKey(pvVar3,"Point1",false);
  if (pvVar4) { FUN_10071e00(pvVar4, &anchor2); flags |= 1 << (count + 2); count++; }
  pvVar3 = FindKey(pvVar3,"Point2",false);
  if (pvVar3) { FUN_10071e00(pvVar3, &anchor3); flags |= 1 << (count + 2); count++; }
}
```

So the mode-1 gate `if (rec->+0xd4 > 0)` reads "this shot authored at least one target point". *Retail
bug:* the flag bit is raised by **order of presence** (`1 << (count + 2)`) while the anchor data goes
into the fixed Point1/Point2 slot, so a file authoring `Point2` without `Point1` fills slot 3 but
raises `0x04`, and the look-at solve then reads the empty slot 2 — a shot that aims at `(0,0,0)`. No
shipped file does this (`docs/vtmb/retail-defects.md` §7).

**The parse defaults are not zero.** `0x100721e0` seeds the record before it reads the block, and the
whole-block-absent path `0x10072300` seeds the identical set: MoveSpeed 150 u/s, MoveAccel 50 u/s²,
TurnAccel 30 °/s², MaxTurnRate [90, 90, 90] °/s, DistanceTolerance 10 u, AngularTolerance [1, 1, 1] °,
FieldOfView 75 clamped to [20, 120], every flag clear. A file that writes only `FieldOfView` still
gets a rate-limited, deadbanded camera, which is why so few shipped shots bother with the rates.

**Anchor parse** `FUN_10071e00`, complete. The record is 0x2c bytes: `+0x00` flags, `+0x04` a 16-byte
inline name (`Q_trimspace` of whatever follows `Bone:` / `Attachment:`), `+0x14` `OffsetOrigin`
(default string `"[0, 0, 0]"`, literal at `0x1054732c`), `+0x20` `OffsetAngles` (the same literal).
The `Position`, `AttachPos` and `AttachType` keys all default to the literal `"None"` (`0x10547418`),
which matches no keyword and therefore lands on the `World` / `Origin` / `None` tails.

`Position` is matched by `_strstr` in this order — Player `0x1`, DialogTarget `0x2`,
**GrappleVictim `0x80000`**, **GrappleAttacker `0x100000`**, Named `0x8` — and **anything unrecognised
falls through to World `0x4`**, which is also the default. `AttachPos`, also `_strstr` and also in
order: Bone `0x200`, Attachment `0x400`, Center `0x20`, EyePosition `0x40`, Top `0x100`, Bottom `0x80`,
**AbsMin `0x800`**, **AbsMax `0x1000`**, default Origin `0x10`. `AttachType` is an exact byte compare
including the NUL — **case-sensitive, not `strstr`** — Follow `0x4000`, FollowNoAngles `0x8000`,
FollowEntAngles `0x10000`, default None `0x2000`. `0x20000` marks a non-zero `OffsetOrigin`; `0x40000`
marks a non-zero `OffsetAngles`.

**`Position` resolves in `SetShot` (`FUN_1006e130`), and the grapple pair is `m_GrapplePartner` /
`m_GrappleRole` (2026-09-07).** With `subject = param_3 ? param_3 : UTIL_PlayerByIndex(1)`, per anchor
`i`:

```
Player           → UTIL_PlayerByIndex(1)
DialogTarget     → EHANDLE_Get(subject + 0xFE8), NULL when player 1 is NULL
GrappleVictim    → the partner  when the partner handle is live and m_GrappleRole == 0
                 → the subject  when it is live and m_GrappleRole == 1
                 → NULL         when the partner handle is dead
GrappleAttacker  → the mirror of the above
World            → FindEntityByClassname(NULL, "worldspawn")
Named            → NULL; the caller supplies it through SetShotAnchorEntity
```

then `SetShotAnchorEntity(this, resolved, i)` for `i = 0..3`, and finally `this->+0x63c = framecount`,
`this->+0x638 = param_2`. The pair it reads is `CBaseCombatCharacter+0x1538` **`m_GrapplePartner`**
(EHANDLE) and `+0x153c` **`m_GrappleRole`**, whose values are **`-1` none, `0` attacker, `1` victim** —
proved by `StartGrappleAttack`'s two `EnterGrappleState` dispatches, which push the literals `0` for
the attacker and `1` for the victim (`0x10329285` / `0x103292d3`), and corroborated by
`EnterGrappleState`'s `if (role != 0) m_hGrappleAnimDriver = partner`: the attacker drives the paired
animation. The names come from `CBaseCombatCharacter::Dump` `0x103222c0`, which also names
`+0x1534` `m_GrappleSavedMoveType`, `+0x1540` `m_GrappleType` and `+0x1544` `m_GrapplePosition`. The
full writer/reader ledger and the nine-valued `m_GrappleType` enum are in `docs/vtmb/stealth.md`.

**`OffsetAngles` is parsed and dead — settling this document's open question.** `FUN_1006f080` only
ever reads `+0x14`, and no cine-camera site anywhere in `vampire.dll` tests `0x40000`. The key is
accepted by the grammar and has no effect on the server.

**The client parses the identical grammar — one grammar, parsed twice (2026-09-07).** `client.dll`
`FUN_10028a10` (anchor) and `FUN_10028d20` (shot record) were read against `vampire.dll`
`FUN_10071e00` and `FUN_100721e0` field by field, and **there is no divergence at all**: the same
`0x104` stride and the same offsets, the same `_strstr` orders and flag bits for `Position` and
`AttachPos`, the same 16-byte inline-name extraction at `+5` / `+0xb`, the same case-sensitive
`AttachType` byte compare with lengths `7` / `0xf` / `0x10`, the same `"None"` and `"[0, 0, 0]"`
defaults, the same `"[90,90,90]"` / `"[1,1,1]"` vector defaults, the same 150 / 50 / 30 / 10 / 75
scalars, the same `[20, 120]` FOV clamp in the same `if (fov <= 120) { if (fov < 20) fov = 20; } else
fov = 120;` shape (`_DAT_101e55c0` = 120, `_DAT_10224ae8` = 20 on the client;
`_DAT_1044f00c` / `_DAT_1044eb0c` on the server), the same key read order, the same six
`CameraConstraints`-scoped booleans, and **the same `Point2`-without-`Point1` order-of-presence flag
bug**. Even the dead anchor pre-init that the record-wide zero immediately overwrites is present on
both halves. Nothing the client tracker reads can disagree with what the server think reads.

**Anchor to world** (`CBaseCineCam` `FUN_1006f080`, offset step `0x1006f430`):

| `AttachPos` | resolves to |
|---|---|
| `Origin` | `GetAbsOrigin()`, with `GetAbsAngles()` as the rotation basis |
| `Center` | the world-space centre (vfunc `0x300`) |
| `EyePosition` | `CBaseCombatCharacter::CalcLookData`'s eye when the entity has one (`+0x9c`), else `EyePosition()` (vfunc `0x304`) — a **fixed** `origin + m_vecViewOffset`, not a bounds fraction and not a bone |
| `Top` / `Bottom` | `(absOrigin.x, absOrigin.y, surroundingBounds.maxs.z / mins.z)` — the **abs origin's XY** with only Z from the bounds |
| `AbsMin` / `AbsMax` | `surroundingBounds.mins` / `.maxs`, all three components |
| `Bone: <name>` | `GetBonePosition02`, with the bone's own angles — animated |
| `Attachment: <name>` | `GetAttachment02`, with the attachment's angles |

Every arm but `Bone:`/`Attachment:` takes the entity's abs angles as its rotation basis. The bounds are
the world-space surrounding bounds, `ent->m_Collision (+0x270)->vfunc 0x3c`, read once at the top of
`FUN_1006f080` for every arm.

`OffsetOrigin` is added in world axes for `None` and `FollowNoAngles`, rotated by the attach point's
angles for `Follow`, and by the entity's abs angles for `FollowEntAngles`. **A DialogTarget/Origin/
Follow `End` anchor therefore never moves with animation**; only a `Bone:`/`Attachment:` anchor does.

**`AttachType None` *is* "sample once", and `FUN_1006e8e0` is a shot start — correcting this document
twice.** This document previously said "`AttachType None` is not sample once" and "the camera think
`FUN_1006e8e0` re-resolves all four anchors every server tick". Neither holds. `FUN_1006e8e0` is not in
any vtable and is never handed to `ThinkSet`; all six of its call sites are "a shot has just been set"
(`FUN_10070470` create, `FUN_10070780` re-shot, `FUN_1006e4c0` once per `FindBestShot` candidate,
`FUN_10070550`, `FUN_100705d0`, `FUN_10070690`), and its work is one-shot — stamp
`m_nClientResetFrame`, `SetAbsOrigin`/`SetAbsAngles`, `Relink`, and fill the shot-start anchor cache at
`this+0x598+i*0xc`. Per-tick resolution is decided instead by `FUN_1006f010`:

```c
uVar2 = thunk_FUN_1006ee10(this,0);      // NOTE the literal 0 — anchor 0's flags, for every index
if ((uVar2 & 0x2000) != 0) {             // AttachType None
  pfVar1 = (float *)((int)this + param_2 * 0xc + 0x598);   // the shot-start cache
  ... return cached ...
}
thunk_FUN_1006f080(this,param_1,param_2);                  // otherwise re-resolve live
```

So **`AttachType None` freezes an anchor at shot start** and every other `AttachType` re-resolves it
every tick. *Retail bug:* the test reads the **Start** anchor's flags for all four indices, so a shot
with **no `Start` block** reads a zeroed record (`0x2000` clear) and therefore re-resolves **all four**
anchors live every tick. `jack.txt` and `dialogdefault.txt` have no `Start` block, so the conclusion
below — retail re-resolves the head bone every tick, and `AngularTolerance [10, 10, 10]` is what holds
the shot still — stands, by this path rather than by `FUN_1006e8e0`. A shot that *does* author
`Start { AttachType None }` freezes all four anchors in retail. The bug is catalogued in
`docs/vtmb/retail-defects.md` §7.

Camera abs origin is written once, by `FUN_1006e8e0` at shot start: the Start anchor when present, else
the cached pose; on the very first frame with neither it is the player's `EyePosition` plus abs angles,
so an End-only shot dollies in from the player's view. The *published* origin thereafter is the mode-1
think's, off the `+0x594` selector (see "The mode-1 think" below). Look-at (`FUN_1006f670`) is Point1,
Point2, or their midpoint.

**The client tracker — `C_BaseCineCamera` — is what holds a shot still.** It runs every *rendered*
frame from `C_BasePlayer`'s view calc: `FUN_100a7770` then `FUN_10001b50`, `FUN_10001a20`,
`FUN_10001fa0`, then move `FUN_10001fe0`, turn `FUN_10001d40`, FOV `FUN_10001c20`. Fields: `0x410`
goal origin, `0x41c` look-at, `0x468` current origin, `0x474` current angles, `0x4a4` current speed,
`0x4a8[3]` per-axis turn rates, `0x4c0` position-settled, `0x4c1[3]` per-axis angle-settled.

- **Position** `FUN_10001fe0`: `threshold = settled ? DistanceTolerance : 1.0 u`;
  `dist = |goal - current|`; `settled = dist < threshold`. Settled means speed 0 and no movement.
  Otherwise accelerate toward `MoveSpeed` by `MoveAccel`, decelerate by `MoveAccel` inside the
  stopping distance, and step toward the goal. **Hysteresis**: parked until the goal drifts more than
  `DistanceTolerance`, moving until back within 1 unit.
- **Angles** `FUN_10001d40`: desired = `VectorAngles(lookAt - currentOrigin)`, re-derived after the
  position step. Per axis, `tol = settled[i] ? AngularTolerance[i] : 1.0°` — **correcting this
  document**, which said "`: tiny`" and understated the unsettled band by more than an order of
  magnitude; the constant is `_DAT_101e34ec`, the same `1.0f` the position arm uses as its unsettled
  threshold. A delta inside the band
  settles the axis, zeroes its rate and rotates nothing; outside it, the axis turns at
  `FUN_10001c80`'s rate, clamped to the desired angle.
- **Turn rate** `FUN_10001c80`: accelerate by `TurnAccel` toward `MaxTurnRate[i]`, decelerate to 0
  inside the stopping distance. With `SyncRotateOnMove` set **and the position not settled**,
  `rate = |delta| / T`, where `T` is the predicted remaining translation time from (current speed,
  `MoveSpeed`, `MoveAccel`, distance remaining) — `MaxTurnRate` is bypassed so the pan lands with the
  dolly.
- **Shot start** `FUN_10002210`: the current pose is the goal when the shot has a `Start` or has no
  `End`; otherwise it is the live view setup, which is the dolly-in. Position is marked settled and
  the rates zeroed. `SnapOnShotChange` additionally hard-copies goal to current (`FUN_10002390`). FOV
  is copied to `0x480` every frame.

**This is the whole answer to "the dialogue camera wobbles as the NPC animates."** `jack.txt` and
`dialogdefault.txt` both write `Target Point1` as `Bone: Bip01 Head`, and retail re-resolves that bone
every tick exactly as the port does — the goal angle genuinely jitters all conversation long. What
stops the camera following it is the shipped `AngularTolerance [10, 10, 10]`, a band far wider than
any head motion. Nothing in the anchor grammar is involved.

`jack.txt`, verbatim: no `Start`; `End` DialogTarget / Origin / Follow / `OffsetOrigin [50, 0, 65]`;
`Target Point1` DialogTarget / `Bone: Bip01 Head` / None; MoveSpeed 500, MoveAccel 250, TurnAccel 30,
MaxTurnRate [60, 60, 60], DistanceTolerance 5, AngularTolerance [10, 10, 10], FieldOfView 40,
DialogPOV 1, SyncRotateOnMove 1. `dialogdefault.txt` is the same shot with `OffsetOrigin [40, 0, 65]`.
So retail's Jack camera sits 50 u (127 cm) along Jack's own forward and 65 u (165.1 cm) up, framing
his head.

**`DialogTarget` is `player+0xFE8`,** the NPC in the conversation. `StartPlayerDialog` (`0x10178280`)
writes no origin and no angles for either party: nothing about starting a conversation turns anyone.

**Units.** `OffsetOrigin`, `MoveSpeed`, `MoveAccel` and `DistanceTolerance` are raw Source units with
no scale anywhere on the path; the conversion to cm happens once, in the parse, at
`ElysiumCam::U = 2.54`.

**Field of view is 4:3-referenced and Hor+.** A shot's `FieldOfView` is the horizontal angle at
Source's 4:3 reference; a wider window holds the vertical angle and earns horizontal
(`vfov = 2*atan(tan(hfov/2)/(4/3))`). Jack's authored 40 therefore renders about 51.8 degrees
horizontal at 16:9, and handing the authored number straight to Unreal — whose
`FMinimalViewInfo::FOV` is horizontal *at the current aspect* — magnified the shot about 1.4x and read
as "the camera is closer than retail". This is retail Source semantics, not a modernization; the
conversion is `ElysiumCam::WidenSourceFov`, applied at **apply time**, so the parsed shot keeps the
number its file wrote. The player view now runs through the same rule (see "The lens" below), so the
shot weight lerps two angles in one space.

**Three open items here are now closed.** The anchor's `OffsetAngles` is parsed and dead, not
unrecovered (above). The "approach-integration constant folded behind a normalize" does not exist: the
listing (`0x10002083`–`0x100021b1`) shows a plain first-order Euler step of length
`min(speed·dt, dist)` along the unit direction — the normalize hid only the direction, and the curve
shape is exact, not approximate (see "The client tracker's exact numerics"). The camera think interval
`_DAT_1044eb04` reads `0.04165999963879585` out of the image — 1/24 s (see "The mode-1 think"). The
recorded port-side assumption is retired too: the listing shows `MOV byte ptr [EBP],0x0` in **both**
turning arms (`0x10001e64`, `0x10001ead`) and `MOV byte ptr [EBP],0x1` only in the settled arm
(`0x10001eb3`), so retail does clear the angular `settled` flag on a turn and the deadband is
symmetric with the position arm's in retail as well. What remains open in the scripted camera is
listed under [Not yet recovered](#not-yet-recovered).

### `CamMode` — which cameras the tracker drives (2026-09-07)

Recovered because porting the tracker above fixed the conversation camera and broke the
`sp_tutorial_1` scenematic: the tracker was applied to every scripted shot, and retail applies it to
exactly one kind.

**`CBaseCineCam` has one mode field and no subclasses.** `camera_cinematic` (`vampire.dll`, datamap
`0x105463a8`, chain `CBaseCineCam → CBaseToggle → CBaseEntity`; `vtmb_slot` over its overrides returns
only itself) replicates `CamMode` at `+0x638` (client `+0x45c`) in `DT_BaseCineCam` (`FUN_1006d2f0`),
beside `m_ShotIndex` `+0x630`, `m_vecCamOrigin` `+0x5ec`, `m_vecCamTarget` `+0x5f8`, `m_angCamAngles`
`+0x604`, `m_flFOV` `+0x634`, `m_nClientResetFrame` `+0x63c`, `m_bDrawPlayer` `+0x640`. `CamMode` is
written only by `SetShot(name, mode, subject)` `FUN_1006e130` and cleared by `FUN_1006e0e0`. The server
think dispatcher `CBaseCineCamSetCamThink` `FUN_1006e770` is a jump table on it:

| `CamMode` | server think | what it is |
|---|---|---|
| 0 | none | idle |
| 1 | `CBaseCineCamUpdate_Mode_NamedShot` `0x1006f8f0` | a `vdata/camerashots/` shot: anchors resolved per tick unless `AttachType None` froze them at shot start, look-at solved (`FUN_1006f670`) |
| 2 | `CBaseCineCamUpdate_Mode_OnRails` `0x1006fde0` | **empty**, server and client |
| 3 | `CBaseCineCamUpdate_Mode_FollowEntity` `0x1006fe00` | origin = anchor 0's world centre (vfunc `0x300`), angles = its abs angles; **never** `m_vecCamTarget`, **never** `m_flFOV`; created by `FUN_100705d0`, **no callers** |
| 4 | `CBaseCineCamUpdate_Mode_Animated` `0x1006f870` | pushes only `m_flFOV`; pose is the entity's own animation; created by `FUN_10070690` for `camera_animated` (`CCameraAnimated::StartCamera` `0x10071550`), **zero instances shipped** |
| >4 | `CBaseCineCamCamEndThink` `0x1006e850` | `UTIL_Remove(this)` |

**The two unshipped arms in full (2026-09-07).**

```c
// 1006fe00  CamMode 3 — FollowEntity
if (!handleLive(this->+0x610)) this->+0x55c = 0;      // integer 0, NOT FUN_1006e8b0
if (FUN_1006f7d0(this)) return;                        // 24 Hz reschedule + expiry
ent = resolve(this->+0x610);                           // no null guard on this second resolve
this->m_vecCamOrigin (+0x5ec) = ent->WorldSpaceCenter();   // vfunc 0x300
this->m_angCamAngles (+0x604) = ent->GetAbsAngles();       // vfunc 0x36c

// 1006f870  CamMode 4 — Animated
if (!handleLive(this->+0x610)) FUN_1006e8b0(this, 0.0);    // +0x55c = curtime
if (FUN_1006f7d0(this)) return;
this->m_flFOV (+0x634) = shotRecord->FieldOfView (+0x100);
```

`FUN_1006e8b0(this, secs)` is `+0x55c = curtime + secs`; the constructor seeds `+0x55c = -1.0`
(`0xbf800000`), so a fresh camera never expires, and the gate in `FUN_1006f7d0` is
`+0x55c > 0.0f && +0x55c < curtime` — strict on both sides. Mode 4's `0.0` therefore fails the gate
on the tick it is armed (equality) and removes the camera on the **next** think, one 1/24 s later.
Mode 3's integer `0` fails the `> 0.0` half forever, so a mode-3 camera whose anchor dies never
expires **and then dereferences the dead handle** — `retail-defects.md` §7. Mode 3 leaves `m_flFOV`
at the constructor's 75.0 (`0x42960000`) because nothing on that path ever writes it.

Mode 4's FOV publish is dead on arrival: the client's `if (m_CamMode == 4) { FUN_10002200(dt);
return; }` skips the copy-through that would move `m_flFOV` into `m_flCurFov`, and `FUN_10002200` is
an empty `RET`. An `Animated` camera renders pose *and* FOV frozen at shot start.

The two factories differ in one load-bearing way. `FUN_100705d0(ent)` (mode 3) creates
`camera_cinematic`, sets the disposable bit, `SetShot("Follow", 3, NULL)` — the only failure path,
which `UTIL_Remove`s and returns NULL — then stores the anchor EHANDLE **directly** at `+0x610`
(`-1` when `ent` is NULL, which walks straight into the crash above), leaving the bone/attachment
index at `-1`; that matches `special-case.txt`'s `Follow`, which is
`Start { Position Named; AttachPos Center; AttachType None }` and needs no index.
`FUN_10070690(ent)` (mode 4) does the same but names `"Animated"`, mode `4`, and goes through
`SetShotAnchorEntity` `FUN_1006ef50`, so the `Bone: cam_bone` index `special-case.txt`'s `Animated`
block asks for is looked up. Both set `+0x204 |= 0x4`.

**`camera_animated` — `CCameraAnimated` (2026-09-07).** A separate entity class
(`LINK_ENTITY_TO_CLASS` at `FUN_10070a10`, string `10546edc`), derived from `CBaseAnimating`, not a
mode of `camera_cinematic`. Its own datamap rows: `+0x7f0` the created cine camera's EHANDLE
(constructor `-1`), `+0x7f4` `m_sAnimName` key **`animname`**, `+0x7f8` output **`OnCameraBegin`**,
`+0x810` output **`OnCameraComplete`**, `+0x828` the cached `camera_showdebug` state, `+0x82c` a float
the constructor seeds to `-1.0` and nothing opened reads. Inputs **`StartCamera`** (`10546b18`) →
`0x10071440` and **`EndCamera`** (`10546af8`) → `0x10071470`.

* `Spawn` `0x10071330` — with a model name, `Precache` then `SetModel` and return; without one,
  `Warning("%s at %.0f %.0f %0.f missing modelname\n")` and `UTIL_Remove`. (The warning itself pushes
  two doubles for three conversions.)
* `StartCamera` `0x10071550` — `EndCamera` first; `m_iEFlags &= ~0x42`; `RemoveFlag(0x40000)`;
  `cam = FUN_10070690(this)`; store its handle at `+0x7f0`; `SetCineCamera(player, cam)`;
  `FUN_10071770(this, m_sAnimName)`; **`if (m_spawnflags & 1) SetImmobilized(player, true)`**.
* `FUN_10071770(name)` — `LookupSequence`, else `Msg("%s no sequence named:%s\n")` and
  `m_nSequence = 0`; on success set `m_nSequence`/`m_flCycle = 0`, `ResetSequenceInfo`, fire
  `OnCameraBegin`, `ThinkSet(0x10071840)` and `m_flNextThink = curtime + 0.1`
  (`_DAT_104493d0`, a **double** `0.1` read from the image).
* The think `0x10071840` — `StudioFrameAdvance` + `DispatchAnimEvents`, then
  `m_bSequenceFinished ? EndCamera() : (m_flNextThink = curtime + 0.1)`, then caches
  `camera_showdebug` into `+0x828`. **The shot ends when the sequence finishes**, and the entity's own
  clock is 10 Hz while the cine camera it drives thinks at 24 Hz.
* `EndCamera` `0x10071660` — fire `OnCameraComplete`; `UTIL_Remove` the cine camera **directly**
  (not through `SetCineCamera(player, NULL)`, so `m_iCameraOverrideIdx` is left pointing at a dead
  index until the client's handle stops resolving); `MakeDormant`; `AddFlag(0x40000)`;
  `m_nSequence = m_flCycle = 0`; `ResetSequenceInfo`; `if (m_spawnflags & 1) SetImmobilized(player,
  false)`. It clears no `m_iVFlags` and restores nothing else.

**`DeathCam` has no driver (2026-09-07).** `special-case.txt` defines the shot with the comment
*"the game will set this to the corpse"* on its `Named` Point1. `vtmb_string "DeathCam"` returns
**zero strings in every module** — `vampire.dll`, `client.dll`, `engine.dll`, `GameUI.dll`,
`vguimatsurface.dll`, `vphysics.dll`, `tier0.dll` — and a byte scan of the whole shipped install finds
the literal exactly once, at offset `720502` of `Vampire/pack101.vpk`, inside `special-case.txt`
itself. The compiled Python (`pack008.vpk`) carries `StartShot` ×9 and `SetCamera` ×4 and no
`DeathCam`; no dialogue file names it. Its three siblings *are* reached — `"Follow"` (`10546ea0`) by
`FUN_100705d0`, `"Animated"` (`10546ea8`) by `FUN_10070690`, `"Hacking"` by
`CPropHacking::vfunc39`/`CPropKeypad::vfunc39`, `"Intrusion"` by `FUN_10225070`, `"FuncMonitor"` by
`CFuncMonitor::vfunc39` — which makes the absence conclusive rather than a search failure. **The
retail death view is not a cine camera at all**: it is the spectator-target replace at the tail of
`CViewRender::CalcView` `client.dll 0x10191200`, where an `IVRenderView::GetViewEntity()` index
strictly greater than `IVEngineClient::GetMaxClients()` hard-replaces origin and angles with that
entity's.

**The client divides on it once.** `C_BaseCineCamera::Update` `client.dll` `FUN_10001a20`, reached
each rendered frame from `C_BasePlayer::CalcView` `0x100a7770` → `C_BaseCineCamera::CalcView`
`0x10001b50`:

```c
if (*(char *)(this + 0x4a1)) FUN_10002390();                 // SnapOnShotChange hard copy
if (*(int *)(this + 0x45c) == 4) { FUN_10002200(this); return; }   // Animated: empty
if (*(int *)(this + 0x45c) == 1) { FUN_10001fa0(this); return; }   // NamedShot: the tracker
current origin (0x468) = m_vecCamOrigin (0x410);              // everything else: copy through
current fov    (0x480) = m_flFOV        (0x458);
current angles (0x474) = m_angCamAngles (0x428);
```

So the hysteresis, `MoveAccel`, `TurnAccel`, `SyncRotateOnMove` and the `VectorAngles(lookAt −
currentOrigin)` re-derivation exist for **mode 1 only**. The snap path agrees: `FUN_10002390` copies
replicated → current for every mode and derives angles from the look-at only `if (CamMode == 1)`.

**The Worldcraft track never reaches the cine camera.** `camera_track` / `camera_keyframe` (§6) is
applied by `CInput` (`client.dll` `FUN_100ffb90`, `CInput+0x84`) off the player's
`m_vecCameraViewOverride` / `m_vecCameraTargetOverride`, with the scripted origin/target/roll/FOV at
`CInput+0x17c/0x188/0x194/0x198`:

```c
e = SimpleSpline(weight);
origin = lerp(origin, override.origin, e);
target = lerp(viewForwardPoint, override.target, e);
VectorAngles(target - origin, angles);      // fresh, every frame
angles.roll = e * override.roll;
fov = lerp(fov, override.fov, e);
```

No tracker, no deadband, no rate limit, no settle state. The `sp_tutorial_1` scenematic is this
channel — `trackb00` as position and `focusb00` as target (`sp_tutorial_1-event-surface.md`), a
`.vcd` with three actors and no camera channel — not a `camera_cinematic` at all. The map's three
`camera_cinematic` entities (`DialogMediumShot`, `cam_3` `DialogDefault`, `feedcamera`
`LookAtTarget_Snap`) are mode 1 with anchors forced by targetname: `InputStartShot` `0x10070720` →
`FUN_10070780` resolves `startent`/`endent`/`target1`/`target2` by name, spawns or re-shots the
player's cine camera with `SetShot(shotname, 1, player)` and `SetShotAnchorEntity` ×4, then adopts it
(`FUN_1017cef0`) and **immobilizes the player** (`FUN_1015ef40` = `SetImmobilized(true)`; it changes no
view state — see "The server shot lifecycle"). All 51 shipped `camera_cinematic`
entities are this shape. Script `SetCamera` (`CBasePlayer::SetCamera` `FUN_1017d020`) is the same
mode-1 spawn with the `Position` keyword resolving the anchors (`FUN_1006ef50`: `0x1` Player →
`UTIL_PlayerByIndex(1)`, `0x2` DialogTarget → `player+0xFE8`, `0x4` World → worldspawn; `0x80000` /
`0x100000` are two further keywords gated on `subject+0x1538`/`+0x153c`, the grapple/victim pair),
falling back to `DialogDefault` when the name does not load.

**Retail's "aims at nothing" is (0,0,0), and it never surfaces.** The shot-start anchor cache
`FUN_1006e8e0` writes `vec3_origin` (`DAT_1070d1b0`) for an invalid handle, and the look-at solve
`FUN_1006f670` starts from `vec3_origin` and only overwrites it when Point1 (`flags & 4`) or Point2
(`flags & 8`) exists — but a shot with no target is only ever used in a mode where the client never
derives angles from the look-at.

**The port.** `FElysiumCameraShot::bTracked` is the `CamMode == 1` test. `FElysiumCameraDirector::Resolve`
(every `vdata/camerashots/` shot: `SetCamera`, the terminal shots, the dialogue source shot;
`camera_cinematic`'s `StartShot`/`EndShot` is still the stub in `ElysiumStubClasses.cpp` and would
land on the same converter) and the conversation profiles that stand in for a dialogue shot set it; every value
producer — `PublishTrackCamera`, the green room, the theatre — leaves it clear and
`FElysiumScriptedShotTracker::Advance` copies origin and look-at-derived angles through every frame.
Before this the tracker ran for all of them, and the track shot's zero `MaxTurnRate` (its old
"no limiter" sentinel) read as a zero rate: the dolly travelled and the aim froze on the seed frame.
`Elysium.Substrate.Camera` drives a direct shot and its tracked twin through the tracker;
`Elysium.Substrate.CameraTrack` asserts the composed track shot is direct.

**Re-seeding on a shot change** (`C_BaseCineCamera::OnDataChanged` `0x100024c0`, adjustor thunk, real
offsets `ESI+d+8`): a new `m_nClientResetFrame` (stamped by `SetShot`) sets shot-start pending `0x4a2`
and `bActive` `0x465`; a new `m_ShotIndex` either requests the snap (`0x4a1`, flag `0x80`) or clears
the three angle-settled flags so the axes re-acquire, and applies the shot's `ShowHud` (`0x200`).
`IsActive()` `FUN_10001970` is `CamMode != 0 && bActive`, and gates the whole `CalcView`. The port's
re-seed is the top-shot-id change in `UElysiumCameraComponent::SolveShot` / the service's
`SelectWinner`; an update on a live id continues the tracker from its current pose, which is retail's
End-only "dolly from where you are" for a mid-conversation shot change.

**Corrections to the tracker section above.** `AutoPositionFromTarget` *does* have a reader — the
mode-1 think `FUN_1006f8f0`, on `flags & 0x20` — and `point_player` / `m_bForcePlayerLook` (`+0x5e8`)
is consumed at the tail of the same think and turns the *player*, not the camera. Shot-record `+0xd4`
gates whether the server derives `m_angCamAngles` from the look-at at all; it is the Target-point
count, not a key (see the record layout above). `CBaseCineCam::FindBestShot` `FUN_1006e4c0` tries
`"%s_%d"` variants from 1 upward and picks a passing index at random. All four are recovered in full
in "The mode-1 think" and "`FindBestShot` and the anim-event channel" below.

### The server shot lifecycle — `StartShot`, `SetShot`, adoption and end (2026-09-07)

**The director and the runtime camera are two entities.** `InputStartShot` `0x10070720` ignores its
`inputdata` entirely — the activator is always `UTIL_PlayerByIndex(1)` — and calls
`FUN_10070780(director, player)`. The map-placed `camera_cinematic` is the *director*: it holds the
keyvalues and never becomes the view. The camera the player adopts is a **second** `camera_cinematic`,
created on demand.

| director offset | key | use |
|---|---|---|
| `+0x5d4` | shot name | the `SetShot` argument; `""` when null |
| `+0x5d8` | `startent` | resolved by targetname → anchor 0 |
| `+0x5dc` | `endent` | → anchor 1 |
| `+0x5e0` | `target1` | → anchor 2 |
| `+0x5e4` | `target2` | → anchor 3 |
| `+0x5e8` | `point_player` | `m_bForcePlayerLook` — parsed onto the director and **never copied anywhere**; see below |
| `+0x640` | — | `m_bDrawPlayer`; **not a datamap key**, written by `Spawn` from `spawnflags & 2`, then copied to the runtime camera |

The key names are the datamap's own (`vtmb_fields CBaseCineCam`): `shotname` / `m_sShotName`,
`startent` / `m_sStartEnt`, `endent` / `m_sEndEnt`, `target1` / `m_sTarget1`, `target2` /
`m_sTarget2`, `point_player` / `m_bForcePlayerLook`. `StartHidden` (5 shipped directors) is not a
camera key either — it is `CBaseEntity::m_bStartHidden` at `+0x0e0`, read only by the two
`CBaseEntity::PostSpawn` bodies (`0x1000c464`, `0x100aaf30`).

`FUN_10070780`, in order: four `FindEntityByName(NULL, name, 0, 0)` lookups with the empty string
substituted for a null keyvalue (a miss yields NULL and leaves that anchor to the shot file's own
`Position` keyword); `cam = player->GetCineCamera()` (`FUN_1017cf90`, which requires both
`+0x1ec4 > 0` and the `+0x19b4` handle to be live); **no camera yet** → create one with
`FUN_10070470(shotname, e0..e3)` and set `m_hSubject (+0x5d0)` to the player;
**camera already there** → `SetShot(shotname, 1, player)`, then `SetShotAnchorEntity` for each non-null
entity, then `FUN_1006e8e0`; on NULL, `DevWarning("%s could not start shot properly\n")` and return;
copy the director's `m_bDrawPlayer`; adopt (`FUN_1017cef0`); `FUN_1015ef40(player)`. **The re-shot
branch ignores `SetShot`'s return value**, so a bad shot name leaves `CamMode = 0` and
`m_ShotIndex = -1` and the camera goes idle rather than falling back.

**Creation, and what makes a camera disposable.** `FUN_10070470` creates a `camera_cinematic` at the
world origin, sets `+0x204 |= 0x4`, calls `SetShot(name, 1, NULL)` — the *only* failure path, which
`UTIL_Remove`s the new entity and returns NULL — then wires the anchor entities and runs
`FUN_1006e8e0`. `+0x204 & 0x4` means "this camera is owned by the adoption slot": `FUN_1017cef0`
removes the outgoing camera only when it carries the flag, so a map-placed director survives its own
`StartShot`. Its shipped callers besides `FUN_10070780` are `CBasePlayer::StartPlayerDialog`
(`0x10178280`, with the NPC's `default_camera`), `CBasePlayer::SetCamera` (`0x1017d020`),
`CFuncMonitor::vfunc39` (`"FuncMonitor"`), `CPropHacking::vfunc39` and `CPropKeypad::vfunc39`
(`"Hacking"`), and `FUN_10225070` (`"Intrusion"`).

**`SetShot` `FUN_1006e130(name, camMode, subject)`** clears the mode first, normalizes the name — *if
the string contains `.txt`, run it through `Q_FileBase` into a 32-byte buffer*, otherwise pass it
verbatim; the table lookup itself is what supplies case-insensitivity — looks it up in the shot table
at `&DAT_106c8298`, stores `m_ShotIndex (+0x630)`, and **returns 0 with the mode still 0 and the index
still `-1`** when the name is unknown. On success it stores `m_hSubject (+0x5d0)` (the passed subject,
else player 1; NULL fails), resolves each anchor's `Position` keyword to an entity, stamps
`m_nClientResetFrame (+0x63c)` with the frame count and writes `CamMode (+0x638)` last.

| anchor flag | keyword | resolves to |
|---|---|---|
| `0x1` | `Player` | `UTIL_PlayerByIndex(1)` |
| `0x2` | `DialogTarget` | `subject+0xFE8` (`m_hDialogPartner`); NULL if player 1 is NULL |
| `0x80000` | `GrappleVictim` | `subject+0x1538` when the role `subject+0x153c == 0`; the **subject itself** when it is `1` |
| `0x100000` | `GrappleAttacker` | `subject+0x1538` when the role is `1`; the **subject itself** when it is `0` |
| `0x4` | `World`, and the fallthrough default | `FindEntityByClassname(NULL, "worldspawn")` |
| `0x8` | `Named` | **NULL** — a named anchor is never resolved here; the caller supplies it through `SetShotAnchorEntity` |

Both grapple arms additionally require the `+0x1538` handle to be live, or the anchor falls through to
the `World`/NULL tail. `+0x153c` is the grapple role slot (`-1` = not grappling), the same pair
`CBaseCombatCharacter::CanStartGrappleAttack` `0x103285a0` and `CPlayerMove::SetupMove` `0x10186120`
read.

`SetShotAnchorEntity` `FUN_1006ef50(this, ent, i)` stores the EHANDLE at `+0x610 + i*4` (`0xffffffff`
for NULL) and, when the entity animates, resolves the anchor record's inline name to a bone index
(`flags & 0x200`) or an attachment index (`flags & 0x400`) into `+0x620 + i*4`. A non-animating entity
leaves that index at whatever `FUN_1006e0e0` last wrote, i.e. `-1`.

**Adoption — `CBasePlayer::SetCineCamera` `FUN_1017cef0(player, cam)`** writes
`+0x1ec4 = engine->IndexOfEdict(cam->edict)` (`m_Local.m_iCameraOverrideIdx`, the replicated channel
the client finds its `C_BaseCineCamera` through) and `+0x19b4 = cam->GetRefEHandle()`, or zero and
`-1` for NULL, and then `UTIL_Remove`s the outgoing camera if `old != cam && (old->+0x204 & 0x4)`.

**`FUN_1015ef40` is `SetImmobilized(true)`, not "enter a view mode" — correcting this document.**
`player+0x19f7` is the replicated `m_bIsImmobilized` (SendProp registered by `FUN_10179840`, 1 byte,
1 bit); `FUN_1015ef40` sets it, `FUN_1015ef60` clears it, `FUN_1015ef20` returns its negation. Its
readers are `CPlayerMove::SetupMove` `0x10186120`, `CGameMovement::CheckJumpButton` `0x101226b0`,
`CGameMovement::Duck` `0x10126fd0`, `CBaseCombatWeapon::ItemPostFrame` `0x10253ea0` and
`CWeaponMelee::ItemPostFrame` `0x103eaec0`. Starting a scripted shot therefore freezes **movement,
jump, duck and weapon use** and changes no view state at all; the view switch is entirely the client's,
off `m_iCameraOverrideIdx`. Only `StartShot` and `StartPlayerDialog` immobilize — script `SetCamera`
does not (see "How dialogue drives the camera"), and neither does `camera_track`
(`docs/vtmb/choreographed_scenes.md` → "Player pawn versus cinematic double").

**`InputEndShot` `0x10070750` → `FUN_10070990(director, player)` does not blend.** It clears the
*director's* mode and anchors (`FUN_1006e0e0`), `ThinkSet(NULL)`s it, then `SetCineCamera(player, NULL)`
— which destroys the runtime camera — `SetImmobilized(false)`, clears bits `0x1` and `0x8` of
`player+0x1d60` (`FUN_101815b0` is a plain `&= ~mask`), and `UTIL_Remove`s the director if it too
carries `+0x204 & 0x4`. The client's next rendered frame finds `m_iCameraOverrideIdx == 0` and falls
back to the player's own eye. Every other server end path is the same same-tick removal:
`EndPlayerDialog`, `CBasePlayer::InputRemoveCamera` (`0x10171f10`, the `RemoveCamera` input on
`CBasePlayer`'s datamap), the interaction closers (`CFuncMonitor::vfunc42`, `CPropHacking::vfunc42`,
`CPropKeypad::vfunc42`, `FUN_10225140`), the feeding/grapple exit `FUN_10169660`, and `FUN_10170090`.
`FUN_10071970` is the map-level teardown: for each `camera_cinematic`, run the full `EndShot` if it is
active and then `UTIL_Remove` it unconditionally, then `UTIL_Remove` every `camera_track`; it has no
in-image caller. **The only time-based smoothing in the whole scripted-camera surface is the client
tracker inside a live mode-1 shot and the `camera_track` weight ramp**, and the latter is cleared, not
blended, when a cine camera is adopted.

Mode housekeeping: `FUN_1006e0e0` sets all four anchor handles and bone indices to `-1`, `m_ShotIndex`
to `-1` and `CamMode` to `0`, and **does not touch** `+0x55c` (expiry), `+0x594` (the origin selector),
`+0x5e8` (`m_bForcePlayerLook`) or `+0x640` (`m_bDrawPlayer`). `FUN_1006e890` (`IsActive`) is
`CamMode != 0`. `CBaseCineCamCamEndThink` `FUN_1006e850` is `UTIL_Remove(this)` plus a reschedule.

**`m_bDrawPlayer` (`+0x640`) has no server reader, and `spawnflags & 2` is its authored source.** It
is registered as a `DT_BaseCineCam` SendProp by `FUN_1006d2f0` and written by exactly three
functions — `CBaseCineCam::Spawn` (from the spawnflag, below), `FUN_10070780` (copying the director's
byte onto the runtime camera) and `CBasePlayer::HandleAnimEvent` `0x10178a10`, where anim event 4050
forces it to 1. It is a pure replication channel; the drawing decision is the client's (see "The draw
gates and the HUD mask").

### The entity's own class surface — `spawnflags`, the nine overridden slots, `camera_showdebug` (2026-09-07)

**There is no `KeyValue` handler.** `vtmb_vtable CBaseCineCam` (table `1044e67c`, 241 slots) shows the
class overriding exactly nine: **5** (destructor), **80/81/82** (the `DECLARE_SERVERCLASS` /
`DECLARE_DATADESC` triple), **86** (`ShouldTransmit`), **103** (`Spawn`), **117** (`ObjectCaps`),
**123** (`DrawDebugGeometryOverlays`) and **193** (`EyePosition`, already recorded as returning
`m_vecCamOrigin`). Slot 107 is `CBaseEntity::ParseMapData`, 118 `AcceptInput`, 121 `ReadKeyField` —
all inherited. Every authored key is a plain datamap row and `spawnflags` is parsed by the engine into
`m_spawnflags` (`+0x204`); the datamap carries no `spawnflags` row at all. There is likewise **no
`Precache` override (104), no `Activate` override (113, `CBaseEntity::Activate` `0x100a0bc0`) and no
`UpdateOnRemove` override (180, `0x100a47d0`)** — `Spawn` is the only entity hook the class owns, and
creation, adoption and teardown are done entirely by free functions.

**`CBaseCineCam::Spawn` `0x1006d9a0` is four instructions and does not chain to its base:**

```asm
1006d9a0  TEST byte ptr [ECX + 0x204],0x2      ; m_spawnflags & 2
1006d9a7  JZ 0x1006d9b0
1006d9a9  MOV byte ptr [ECX + 0x640],0x1       ; m_bDrawPlayer = 1
1006d9b0  RET
```

so the three `spawnflags` bits are:

| bit | reader | meaning |
|---|---|---|
| `0x1` | **nothing on this class** | `CCameraAnimated`'s "freeze the player" bit (`FUN_10071550` / `FUN_10071660`). `camera_cinematic`'s `StartShot` immobilizes unconditionally, so the bit is authored and dead here |
| `0x2` | `CBaseCineCam::Spawn` | **draw the player's body** — `m_bDrawPlayer = 1`, then copied to the runtime camera by `FUN_10070780` |
| `0x4` | `FUN_1017cef0` (`piVar1[0x81] >> 2 & 1`) and `FUN_10070990` (`*(uint *)(this+0x204) >> 2 & 1`) | **disposable** — the same bit `FUN_10070470` / `FUN_10070550` / `FUN_100705d0` / `FUN_10070690` set at runtime with `this[0x81] \|= 4` |

**Shipped census** (51 `camera_cinematic`, 0 `camera_animated`, over `E:\elysium-work\exports`):
`spawnflags 0` ×1, `1` ×20, `3` ×18, `5` ×3, `7` ×9 — so **bit 1 on 50 (dead), bit 2 on 27, bit 4 on
12**. The 27 that draw the player body include `sp_tutorial_1`'s `feedcamera` (`spawnflags 3`),
`sm_hub_1`'s four, `la_library_1`'s three and `sm_asylum_1`'s `tourette_cam` and
`jeanette_to_elevator_camera`.

**The disposable bit never bites on shipped content**, because **`EndShot` is never fired**: no map
I/O in any `.ents` targets a `camera_cinematic` with any input, and a scan of every shipped `.vpk`
finds `EndShot` 0 times, against `StartShot` ×9 and `SetCamera` ×4 in the compiled Python
(`pack008.vpk`) and `SetCamera` ×101 / `RemoveCamera` ×2 in `pack101.vpk`. Directors are started by
`Find("<targetname>").StartShot()` from Python and released by `pc.RemoveCamera()` from a dialogue
script column (`CBasePlayer::InputRemoveCamera` `0x10171f10`), by dialogue end, or by an interaction
closer — never by `EndShot`.

**`ShouldTransmit` (slot 86, `0x1006e6a0`) restricts the camera to its subject's client.**

```asm
1006e6a6  FLD [ECX + 0x90] ; FCOMP [EAX + 0xc]   ; the base's force-transmit-until vs curtime
1006e6b6  JNZ … ; MOV AL,1 ; RET 0x14            ; inside the window -> true
1006e6be  MOV EAX,[ECX + 0x5d0]                  ; m_hSubject; not live -> false
1006e71f  MOV EAX,[ESP + 0xc]                    ; arg 2: the recipient's edict
1006e723  MOV ESI,[EDX + 0x2e0]                  ; subject->m_pEdict
1006e72b  JZ 0x1006e733 ; XOR AL,AL ; RET 0x14   ; different client -> false
1006e733  CALL 0x10011bb7                        ; -> FUN_1006e890  IsActive()  (CamMode != 0)
```

`+0x2e0` is `m_pEdict`, the same field `FUN_1017cef0` hands to `engine->IndexOfEdict`
(`param_1[0xb8]`); `+0x90` is the force-transmit timestamp `CBaseEntity::ShouldTransmit` `0x100ab020`
opens on. The base's whole PVS / `EF_NODRAW` logic is replaced: a cine camera is a per-player channel.

**`ObjectCaps` (slot 117, `0x1006d8f0`) is `CBaseEntity::ObjectCaps() & ~0x2`**, and the base
(`0x100b4320`) returns the single cap `2` — `FCAP_ACROSS_TRANSITION`. So `CBaseCineCam::ObjectCaps()
== 0` and **a live scripted shot does not cross a level transition**.

**The destructor (slot 5, `0x1006d950`)** destroys `m_OnAngularMoveDone` / `m_OnLinearMoveDone`,
chains to the base and, with bit 1 of its argument, `operator delete`s. It restores no HUD and drops
no player state — unlike the client destructor `FUN_10001920`, which does restore the HUD. Every
restore is the client's.

**Slots 80 / 81 / 82** return `&DAT_106c816c`, `0`, and `&datamap_CBaseCineCam`. `DAT_106c816c` is
filled by `staticinit_1006d1a0` as `{ "CBaseCineCam" (10546d30), &DAT_106c7f68 (the `DT_BaseCineCam`
SendTable), next, … }` and head-inserted into the global ServerClass list at `DAT_1072bbd8`, so 80 is
`GetServerClass()` and 82 is `GetDataDescMap()`. The SendTable's encoder limits, from `FUN_1006d2f0`:
`m_ShotIndex` `+0x630` **8 bits** (a 256-shot cap), `CamMode` `+0x638` **4 bits**, `m_bDrawPlayer`
`+0x640` 1 bit, `m_nClientResetFrame` `+0x63c` full int, and `m_vecCamOrigin` `+0x5ec` /
`m_vecCamTarget` `+0x5f8` / `m_angCamAngles` `+0x604` / `m_flFOV` `+0x634` all `SPROP_NOSCALE`.

**`camera_showdebug` is the debug cvar, and its test is `== 1`.** Registered by `FUN_1006d5b0`
(`"camera_showdebug"` `10546df4`, default string `"0"` `105399a0`, flags 0) at `0x106c7fa0`; its only
two readers are slot 123 and `CCameraAnimated`'s think `FUN_10071840`, both testing
`!IsCommand() && GetInt() == 1` — not truthiness. Slot 123's body, from the listing:

```c
origin = m_vecCamOrigin (+0x5ec);  mode = CamMode (+0x638);
if (mode == 2 || mode == 1) {
    AngleVectors(m_angCamAngles (+0x604), &fwd);
    Line(origin, origin + fwd * 20.0f, 255,255,255, 1, 0);      // 20.0 = _DAT_1044eb0c (image)
    Box (origin, (-2,-2,-2), (2,2,2), 255,255,255, 1, 0);
}
if (handleLive(+0x610)) Box(FUN_1006f010(this,&t,0), ±2,   0,  0,255, 1, 0);  // Start  blue
if (handleLive(+0x614)) Box(FUN_1006f010(this,&t,1), ±2,   0,255,  0, 1, 0);  // End    green
if (handleLive(+0x618)) Box(FUN_1006f010(this,&t,2), ±2, 125,  0,  0, 1, 0);  // Point1 dark red
if (handleLive(+0x61c)) Box(FUN_1006f010(this,&t,3), ±2, 125,  0,  0, 1, 0);  // Point2 dark red
Box(FUN_1006f670(this), ±3, (frameCounter[0x1070ba38] % 50) + 200, 0, 0, 1, 0);  // look-at, pulsing red
EntityText(VarArgs("(%.1f, %.1f, %.1f"), …);                                     // 10546e88, unmatched paren
```

The forward line is drawn only in modes 1 and 2, and from the **replicated** origin using the
**replicated** angles — in mode 1 that is the server's `VectorAngles(lookAt − shotStartOrigin)`, not
what the client renders.

### The shot-start placement pose and its one-deep memory — `+0x564` / `+0x570` / `+0x588` (2026-09-07)

`FUN_1006e8e0` (the shot **start**, not a think) keeps four vectors on the entity, all seeded to zero
by the constructor `FUN_1006d620`:

| offset | contents |
|---|---|
| `+0x564` | the **placement origin** being computed |
| `+0x57c` | the **placement angles** being computed |
| `+0x570` | the **saved local origin** of the previous placement |
| `+0x588` | the **saved local angles** of the previous placement |

The same constructor seeds `+0x594 = 2` (the origin selector's "leave the abs origin alone" value),
`+0x55c = -1.0` (never expire), `+0x634 = 75.0`, `+0x630 = -1`, the four anchor handles to `-1`,
`+0x640 = 0` and — see below — **`+0x5e8 = 1`**.

```asm
1006e8e9  CALL 0x1000e2a5                        ; FUN_1006e770  SetCamThink()  — FIRST
1006e8ee  FLD [0x1070d1b0] ; FCOMP [ESI+0x564]   ; is the placement origin still vec3_origin?
1006e925  JNP 0x1006e96f                         ;   yes -> save nothing
1006e92b  CALL [EAX + 0x370] ; MOV [ESI+0x570]…  ; +0x570 = GetOrigin()   (LOCAL, vfunc 220)
1006e94f  CALL [EDX + 0x374] ; MOV [ESI+0x588]…  ; +0x588 = GetAngles()   (LOCAL, vfunc 221)
1006e96f  PUSH 1 ; CALL …                        ; player = UTIL_PlayerByIndex(1)
1006e97d  CALL 0x10002a6d                        ; flags = shot record +0x20
1006e982  TEST AL,0x1 ; JZ 0x1006e9fd
        ; --- Start present: place at anchor 0, aim at the look-at
1006e98f  CALL 0x10011ebe                        ; FUN_1006f010(this,&t,0)  -> +0x564
1006e9ab  CALL 0x1000b4ce                        ; FUN_1006f670(this,&t)    -> lookAt
1006e9f0  CALL 0x10003d4b                        ; VectorAngles(lookAt - +0x564, &+0x57c)
1006e9fd  TEST AL,0x2 ; JZ 0x1006eabd
        ; --- End without Start:
1006ea0b  FCOMP [ESI + 0x570] …                  ; is the SAVED origin still vec3_origin?
1006ea48  CALL [EAX + 0x304]                     ;   yes -> +0x564 = player->EyePosition()
1006ea63  CALL [EAX + 0x36c]                     ;          +0x57c = player->GetAbsAngles()
1006ea7f  MOV ECX,[ESI + 0x570] …                ;   no  -> +0x564 = +0x570,  +0x57c = +0x588
        ; --- publish, in this order
1006eac2  CALL [EDX + 0x360]                     ; SetAbsOrigin(+0x564)   vfunc 216
1006eacd  CALL [EAX + 0xf8]                      ; SetOrigin   (+0x564)   vfunc 62
1006eade  CALL [EDX + 0x100]                     ; SetAngles   (+0x57c)   vfunc 64
1006eae9  CALL [EAX + 0x368]                     ; SetAbsAngles(+0x57c)   vfunc 218
1006eaf1  CALL 0x1001514a                        ; Relink
```

So the memory is written on **every shot start after the first** (the first runs with
`+0x564 == vec3_origin` and saves nothing), from the *local* transform the previous
`FUN_1006e8e0` wrote through `SetOrigin`/`SetAngles`. It is consumed by exactly one arm, and the
answer to what it is for is: **an `End`-without-`Start` shot continues from where the last shot left
this camera entity instead of snapping back to the player's eye.** That is the server twin of the
client's shot-start live-view arm (`FUN_10002210`'s `else`, taken on the same `!(End) || Start`
test): the server seeds the replicated goal from the previous placement, the client seeds its tracker
from the rendered view, and the tracker dollies between them.

`vtmb_readers` over all three offsets and `vtmb_grep '0x15c\]|0x162\]|0x159\]'` return no cine-camera
function besides `FUN_1006e8e0` and the constructor: **nothing else in the image touches the triple.**

Neither `SetShot` `FUN_1006e130` nor the mode clear `FUN_1006e0e0` writes any of the four vectors, and
the mode-1 think never calls `SetAbsOrigin` — so the placement survives a re-shot. `FUN_10070780`'s
re-shot branch runs `FUN_1006e8e0` (saves, then re-places); `CBasePlayer::SetCamera` `FUN_1017d020`'s
does **not** (neither saves nor re-places), which is why a mid-conversation `SetCamera` leaves the
entity where the first shot put it.

One ordering trap the listing exposes: `FUN_1006e8e0` calls `SetCamThink` first and, in the `Start`
arm, resolves anchor 0 through the cache-aware `FUN_1006f010` **before** refilling the anchor cache at
`+0x598` at the bottom of the same function. A `Start` block with `AttachType None` therefore places
the camera from the *previous* shot's cached anchor position. `special-case.txt`'s `Follow` is the
only shipped shot that can reach it.

### `point_player` never reaches the camera that runs the shot (2026-09-07)

`m_bForcePlayerLook` (`+0x5e8`, byte, key `point_player`) is initialised to **1** by the constructor
(`FUN_1006d620`: `*(byte *)(this + 0x17a) = 1`). Its complete write set is that constructor,
`CFuncMonitor::vfunc39` `0x10115260`, `CBasePlayer::HandleAnimEvent` 4050 `0x10178a10` and the
`"Intrusion"` opener `FUN_10225070` — each of the last three clearing it to 0 on the camera it has
just created — plus the datamap key on whatever entity a map authored it on. Its only reader is the
tail of the mode-1 think.

`FUN_10070780` copies **only** `+0x640` from the director to the runtime camera; `FUN_10070470`,
`CBasePlayer::SetCamera` and `StartPlayerDialog` copy nothing; and a map-placed director is never
adopted, so its own `+0x5e8` is never read. **All 35 authored `point_player` values — 26 of them
`0` — are discarded**, and every director shot, script `SetCamera` shot, dialogue opening shot and
terminal/hacking shot runs with `m_bForcePlayerLook = 1`, driving the subject's eye angles onto the
shot's look-at every tick through `FUN_10178590` → `FUN_10178550` (`m_angEyeAngles` `+0x206c..0x2074`,
pending flag `+0x207c`). The three explicit opt-outs are the proof that 1 is the default and that
clearing it is deliberate. Recorded in `retail-defects.md` §7.

### `player+0x1d60` is `CBasePlayer::m_iVFlags`, and what `EndShot` releases (2026-09-07)

`vtmb_fields CBasePlayer --offset 0x1d60` names it `m_iVFlags` (FIELD_INTEGER, no key, not a
SendProp). Its complete accessor set — and `vtmb_grep '0x1d60'` finds nothing else that touches the
dword:

| address | body | name |
|---|---|---|
| `0x10181580` | `\|= mask` | `AddVFlags` |
| `0x101815b0` | `&= ~mask` | `RemoveVFlags` |
| `0x101815e0` | `return m_iVFlags` | `GetVFlags` |
| `0x10181600` | `= 0` | `ClearVFlags` — no callers |
| `0x10181620` | `^= mask` | `ToggleVFlags` — no callers |
| `0x10181650` | `(m_iVFlags & mask) == mask` | `HasAllVFlags` |

* **`0x1` — the scripted-interaction pose lock.** Read only by `CPlayerMove::SetupMove` `0x10186120`:
  in the not-grappling arm, `if ((GetVFlags() & 1) != 0) movedata->viewangles = this->GetAngles()`
  (vfunc `0x374`, the **local** angles), discarding the `m_angEyeAngles.y` substitution the top of the
  function made. "The player's body is being posed by something else — drive the move from the
  entity's own angles." Set by `CBasePlayer::EnterGrappleState` `0x101695f0` and by the interaction
  *openers* — `CBaseTerminal::vfunc39` `0x102181a0`, `CPropSign::vfunc39` `0x10211db0`,
  `CTriggerBombSite::vfunc39` `0x102113c0`, `CTriggerElectricBugaloo::vfunc39` `0x10231640`, and
  `CGameSign`'s read-begin `FUN_10212810`. Cleared by `CBasePlayer::LeaveGrappleState` `0x10169660`,
  by each opener's matching `vfunc42`, by `FUN_100db5c0`, and by `EndShot`.
* **`0x2` / `0x4` — a pending "grapple release" / "seductive release" animation**, consumed by
  `CBasePlayer::SetAnimation` (slot 417, `0x10164870`) as activities `0xfa3` / `0xfc9` and cleared by
  its `RemoveVFlags(6)`. That clear is the **only** writer of either bit in the image: both are read
  and cleared and never set (`retail-defects.md` §7).
* **`0x8` — the view-angle lock.** `CBasePlayer::ProcessUsercmds` `0x1016aaf0` and
  `CPlayerMove::RunCommand` `0x101874a0` both refuse to copy `cmd->viewangles` into `m_angEyeAngles`
  when it is set (or when the one-shot `+0x207c` is raised), and `CPlayerMove::SetupMove` feeds the
  move `m_angEyeAngles` instead of the command's. Its only setter is `CGameSign`'s `FUN_10212810`,
  gated on `!(sign->spawnflags & 2)`, alongside `SetImmobilized(true)` and `AddVFlags(1)`.

**`EndShot` clears locks it never took.** `FUN_10070990` runs `RemoveVFlags(1)` and `RemoveVFlags(8)`,
but `FUN_10070780` sets neither — the same trio (`SetImmobilized(false)`, `RemoveVFlags(1)`,
`RemoveVFlags(8)`) is what `CBaseTerminal::vfunc42` `0x10218220`, `CFuncMonitor::vfunc42`
`0x10115300` and `FUN_10212a30` run when their interaction ends. `InputEndShot` is copying that
closer: it is a general "release the player from whatever scripted state he is in", not the mirror of
`StartShot`. Note these are three *separate* locks: immobilize (`+0x19f7`) stops movement, jump, duck
and weapons; VFlag `0x1` redirects the move's angle source; VFlag `0x8` stops the client's view angles
from being accepted at all.

### The mode-1 think — the 24 Hz clock, the origin selector, `AutoPositionFromTarget` (2026-09-07)

**The server cine camera thinks at 24 Hz, and a mode-1 shot never expires.** Every cine-cam think runs
`FUN_1006f7d0` first: it sets `m_flNextThink = curtime + _DAT_1044eb04` and hands off to `CamEndThink`
when the expiry `this+0x55c` is positive and past. **`_DAT_1044eb04 = 0.04165999963879585`** — 1/24 s,
read out of `Vampire/dlls/vampire.dll` at its `.rdata` address; no cvar or keyvalue touches it, and the
same constant is re-applied by the dispatcher `FUN_1006e770` and by `CamEndThink`. The expiry's only
writers are `FUN_1006e8b0(this, secs)` — whose only caller is the **mode-4** think, with `0.0` — and
the mode-3 think, which zeroes it. A **mode-1 shot therefore lives until `EndShot`,
`SetCineCamera(NULL)` or `UTIL_Remove`**, never on a timer.

```c
// CBaseCineCamUpdate_Mode_NamedShot 0x1006f8f0
if (FUN_1006f7d0(this)) return;                  // 24 Hz reschedule + expiry check
flags = FUN_1006edb0(this); rec = FUN_1006ede0(this);
origin = GetAbsOrigin(); angles = GetAbsAngles();
FUN_1006f670(this, &lookAt);                     // Point1 / Point2 / their midpoint
sel = this->+0x594;
if (sel != 2) {
  if (sel == 1)      origin = anchorPos(0);      // Start
  else if (sel == 0) origin = anchorPos(1);      // End
  if (flags & 0x20) { ... AutoPositionFromTarget ... }
}
if (rec->+0xd4 > 0) { base = GetOrigin(); VectorAngles(lookAt - base, angles); }   // vfunc 0x370, LOCAL
m_vecCamOrigin (+0x5ec) = origin;  m_vecCamTarget (+0x5f8) = lookAt;
m_angCamAngles (+0x604) = angles;  m_flFOV (+0x634) = rec->FieldOfView;   // every tick
this->+0x5cc = curtime;
if (m_bForcePlayerLook (+0x5e8) && subject live) FUN_10178590(subject, lookAt);
```

**`+0x594` is an origin-source selector, not a phase**: `0` drives the published origin from anchor 1
(`End`), `1` from anchor 0 (`Start`), `2` leaves the entity's own abs origin alone and skips
`AutoPositionFromTarget` entirely. Its only writer is `FUN_1006e8e0`, which sets it to `0` (and zeroes
`+0x5c8`) when the **End** anchor's handle is live; otherwise it is whatever the entity was constructed
with. In shipped content a shot with an `End` anchor therefore drives from `End`.

**The published angle is derived from `GetOrigin()`, not from the origin just computed.** The think
never calls `SetAbsOrigin`, so the entity's transform stays wherever `FUN_1006e8e0` put it at shot
start, and `m_angCamAngles` is `VectorAngles(lookAt − shotStartOrigin)` while `m_vecCamOrigin` tracks
the anchor live. This only reaches the screen for `CamMode != 1`, because mode 1 re-derives the angle
from `lookAt − currentOrigin` on the client every frame; modes 3 and 4 and the snap path use the
replicated angles. With `+0xd4 == 0` — no `Target` block — the server publishes the entity's abs angles
unchanged.

**`AutoPositionFromTarget` (`flags & 0x20`), the formula exactly.** Recovered from the disassembly at
`0x1006fa50`–`0x1006fb85`; the constants are read from the image (`_DAT_104454d0 = 0.5`,
`_DAT_1044eb08 = 0.0174532924`, the double `2.0` at `0x10449400`, `_DAT_104492dc = -1.0`), and
`FUN_10431c90` is `_CIpow`.

```
swap P1,P2 so that P1 is the HIGHER-Z target point and P2 the LOWER
A   = FieldOfView * 0.5 * DEG2RAD
C   = ClosestPointOnLine(P2, lookAt, camOrigin)      // FUN_1013ca00, closest point on lookAt→camOrigin
d   = |C − P2|                                       // perpendicular offset from the camera axis
h   = d / sin(A)
r   = sqrt(h*h + d*d)
camOrigin = lookAt − normalize(lookAt − camOrigin) * r
```

**`ClosestPointOnLine` is an *infinite* line — `t` is not clamped (2026-09-07).** `FUN_1013ca00`
delegates to `FUN_1013c940`, which is nine lines of x87 with exactly one branch, the degenerate-length
guard:

```c
// FUN_1013c940(P, A, B, &dirOut) — the whole function
dir  = B - A;                       // written through the fourth argument, unnormalised
len2 = dir·dir;
if (len2 < 1e-05f) return 0.0f;     // _DAT_1046a5e4 = 1e-05f, _DAT_104454c4 = 0.0f
return (P·dir - A·dir) / len2;      // FDIVRP at 0x1013c9c5, RET at 0x1013c9c7 — no clamp
```

There is no `FCOM` against `0.0` or `1.0` anywhere in the body, and `FUN_1013ca00` then writes
`out = A + t·dir` (storing `t` through its fifth argument when non-NULL). `FUN_1013cd40` is the 2-D
twin, also unclamped and guarded by the same two constants; **the image contains no segment-clamped
variant of either**. Read from the call site at `0x1006fa35`–`0x1006fa4b` (five arguments pushed in
reverse), the parameters are `P = the lower-Z target point`, `A = lookAt`, `B = camOrigin`, `out = C`,
`t out = NULL`. So `d` is the **exact perpendicular distance from the lower target point to the
infinite camera→look-at axis**, and it stays that even when the point projects behind the camera or
beyond the look-at, where a segment-clamped implementation would report a strictly larger `d` and back
the camera further off.

**Only the lower-Z target point participates**; the higher one is used only to decide the swap. This is
*not* the tight `d / tan(A)` framing — retail takes the hypotenuse `d / sin(A)` and then adds a second
`d` in quadrature, so it always backs off further than an exact fit. The arithmetic is what to
reproduce, not a corrected version of it. `FieldOfView` here is the raw authored 4:3-referenced
horizontal number; the Hor+ widening is a render-time affair (see "The lens").

**`point_player` / `m_bForcePlayerLook` (`+0x5e8`) turns the subject, never the camera.**
`FUN_10178590(ent, point)` takes the entity's `EyePosition()`, normalizes `point − eye`, runs
`VectorAngles`, and hands the result to `FUN_10178550`, which writes a pending eye-angle snap at
`+0x206c..0x2074` and raises `+0x207c`. It fires every tick of the shot. `+0x5e8` is written by the
entity's keyvalue and cleared by anim event 4050; it is not in `DT_BaseCineCam`, so it is server-only.

### `FindBestShot` and the anim-event channel (2026-09-07)

`CBaseCineCam::FindBestShot` `FUN_1006e4c0(this, baseName)` sets `CamMode = 1` directly, then
enumerates `<base>_1`, `<base>_2`, … with `Q_snprintf(buf, 0x40, "%s_%d", ...)`, **stopping at the
first name the shot table does not hold**. Each name that loads is actually placed (`FUN_1006e8e0`) and
tested by two predicates; every index that passes both is collected, and one is chosen **uniformly at
random** (`RandomInt`), re-set with `SetShot` and logged as `CBaseCineCam::FindBestShot chose %s`. It
does not re-run `FUN_1006e8e0` after the final `SetShot` — its caller does. There is no scoring: the
name is a misnomer for "any shot that fits, chosen at random".

* **`FUN_1006d9d0` — the shot's declared anchors exist.** False when the flags read `0xffffffff`; then,
  for each of `Start 0x1` / `End 0x2` / `Point1 0x4` / `Point2 0x8` that the shot declares, the
  matching handle at `+0x610 + i*4` must be live; finally `m_ShotIndex != -1`.
* **`FUN_1006db10` — the camera can see its target.** Solves the look-at, then for each live
  `Start`/`End` anchor traces a 2-unit hull (`mins (-1,-1,-1)`, `maxs (1,1,1)`) from that anchor's
  resolved world position to the look-at with mask `0x1400b` and a `CTraceFilterSimple` that ignores
  the shot's subject, and fails on `fraction < 1.0`, `startsolid` or `allsolid`. The player's own body
  therefore never blocks a candidate.

**Its only route in is an animation event.** `FUN_1006e4c0` is reached only from `FUN_10070550(base)`
— create a `camera_cinematic`, flag it disposable, `FindBestShot`, `UTIL_Remove` on failure — and
`FUN_10070550`'s only caller is `CBasePlayer::HandleAnimEvent` `0x10178a10`:

* **event 4050** (`0xfd2`): the event's `options` string is the shot **base name**. On success the
  camera gets `m_bDrawPlayer = 1`, the player adopts it, and `m_bForcePlayerLook` is cleared.
* **event 4051** (`0xfd3`): `SetCineCamera(player, NULL)` — drop and destroy — then
  `FUN_10178590(player, EyePosition() + forward*K)` with the player's own yaw flattened to the
  horizontal, i.e. snap his eye angles level and straight ahead.

Neither event immobilizes. So the `FindBestShot` base names are whatever the shipped `.mdl`s carry as
4050 `options` — the anim-event channel, not a script native and not the dialogue system. See
`docs/vtmb/anim-events.md`, whose unclaimed audit does not yet list this pair.

### How dialogue drives the camera (2026-09-07)

**The admission test, recovered (2026-09-07).** `FUN_10178120(player)` is a one-line accessor
returning `player + 0x1d24`, the player's embedded **`CDialog`**; `FUN_100e05f0` is
**`CDialog::Acquire(player, npc)`** (named in the image by `s_CDialog__Acquire_10562118`). The opener's
shape is

```c
npc = *(int**)(npcEnt + 0x98);                        // the entity's AI / combat-character pointer
if (npc->+0x6495 == 0 && FUN_10178170(player)) {      // "the NPC does not interrupt" && "the player is busy"
    FUN_101cebc0(player);                             // → SetDialogPartner(NULL); return
} else if (CDialog::Acquire(player->+0x1d24, player, npc)) {
    ... the opener below ...
} else {
    SetDialogPartner(player, NULL);                   // no camera, no immobilize, no holster
}
```

`FUN_10178170` is the busy test: true if `FUN_1017f8d0(player)`; or if `FUN_101800e0(player, 1)`
returns a time `> 0` that is `>= curtime − 10.0` (**`_DAT_1046fb2c = 10.0f`**); or if
`max(curtime − 10.0, 0)` is below either of the timers `player+0x1dd0` / `player+0x1dd8`; or if
`FUN_1017f770(player) > 0`; or if `FUN_1017f8b0(player) > 0`; or if `player+0x1cf8 != 0x7F7FFFFF`
(FLT_MAX); else it defers to `FUN_10175180(player)`. `npc+0x6495` is the NPC-side override that
bypasses the whole test. `FUN_101cebc0(player)` is not a state change — gated on `player+0x1e00 == 0`,
it opens a `CSingleUserRecipientFilter` on the player and sends one usermessage (`DAT_10726084`).

`CDialog::Acquire` itself: **`if (this->+0x8 != 0) return true;`** — a dialog is already loaded and
nothing is re-read. Otherwise it stores `m_hNPC` (`this+0x00`, from `npc+0x98`) and `m_hPlayer`
(`this+0x04`, from `player+0xa8`), returns false if either handle is dead, then
`load(get_dialog_filename())` — the filename is `Q_trimspace` + `_strlwr` of the NPC's `+0x128`
keyvalue — sets `this+0x30e8` to the load result, picks `GetStartingLine()` into `this+0x2830`
(falling back to the dialog's first line with `"%s has invalid starting conditio…"` when that line does
not resolve), fills and sends the packet, and shows the caption history when `cl_captions` is set.
**Its return is `loaded && !this->+0x30e9`**: a **one-shot / bark dialog (`+0x30e9` set) sends its
line, calls `CDialog::Release` and returns *false*** — so `StartPlayerDialog` takes the
`SetDialogPartner(NULL)` path and **creates no camera, does not immobilize and does not holster**.
That is a normal path, not an error path; it is distinct from "the shot file failed to load", where
`Acquire` succeeded and only `cam` is NULL.

**`CBasePlayer::StartPlayerDialog` `0x10178280`** runs the dialogue-manager admission test, sets the
dialogue partner (`player+0xFE8`), `SetImmobilized(true)`, records whether the active weapon was drawn
(`+0x1e01`) and holsters to `item_w_unarmed`, and then — unless the partner RTTI-casts to a payphone,
which grapples instead — creates the camera from the NPC's `default_camera` keyvalue (`npc+0x64C4`,
`""` when null) with `FUN_10070470(name, NULL, NULL, NULL, NULL)`: **no anchor entities at all**, so
every anchor must come from the shot file's own `Position` keyword. It writes no origin and no angles
for either party, confirming that starting a conversation turns nobody. **`StartPlayerDialog` has no
`DialogDefault` fallback** — a shot name that does not load leaves `cam == NULL` and
`SetCineCamera(NULL)`, and the conversation runs with no camera at all. Only `SetCamera` falls back.

**The payphone arm is `CPayphone` (2026-09-07).** The test is
`__RTDynamicCast(npc, 0, TypeDescriptor(".?AVCAI_BaseNPCTroika@@") @0x10587908,
TypeDescriptor(".?AVCPayphone@@") @0x10587930, 0)` — a `CAI_BaseNPCTroika` → **`CPayphone`** downcast.
`CPayphone` is a real class with its own `EnterGrappleState` override (slot 379, `0x101aade0`). On a
hit the opener runs `StartGrappleAttack(player, phone, 5)` — `DevWarning("Couldn't grapple payphone!\n")`
on failure — and **creates no camera**. Grapple mode 5 takes its facing yaw from the **victim's own
abs-angles yaw**, round-tripped through 16 bits
(`((int)((yaw + 180) * 182.04444885f) & 0xFFFF) * 0.0054931640625f`), rather than from the approach
vector, so the player aligns to the phone; its distance limit is 144 units and both parties holster.

Two more members of the same transaction, outside the camera boundary but part of the opener:
`FUN_10167fd0(player)` releases whatever the player is currently using (`player+0x1040`), and
`FUN_10147a60` is named in the image as **`DisciplineGlobalTeardown`** — the player's active
disciplines are dropped when a conversation opens. `FUN_100826b0(NULL)` then sweeps `gEntList` and
calls vfunc `0x134()` on every entity matching `FUN_10082520(ent, NULL)`.

**`CBasePlayer::SetCamera` `FUN_1017d020(shotName)`** is the script path:

```c
if (GetCineCamera() == NULL) {
  cam = FUN_10070470(shotName, NULL,NULL,NULL,NULL);
  if (!cam) cam = FUN_10070470("DialogDefault", NULL,NULL,NULL,NULL);
  FUN_1017cef0(this, cam);
} else {
  if (!SetShot(shotName, 1, NULL)) SetShot("DialogDefault", 1, NULL);
}
```

Three properties the shape hides: it **never immobilizes**; the re-shot branch never calls
`FUN_1006e8e0`, so it neither re-places the entity nor refills the shot-start anchor cache — combined
with "the angle comes from `GetOrigin()`", a mid-conversation `SetCamera` leaves the entity where the
*first* shot put it; and the `"DialogDefault"` literal is verbatim
(`s_DialogDefault_10587f04`). **Its only caller is the Python native `SetCamera(entity, shotname)`**
(`FUN_10198070`, `PyArg_ParseTuple(args, "Os", …)`, `"bad args to SetCamera()"`), which
`docs/vtmb/script_api.md` counts at 115 shipped call sites.

**There is no engine-side per-line camera — proved from the `.dlg` parser (2026-09-07).**
`CDialog::read_line_data` (`0x100e61d0`) reads exactly **thirteen** `{…}` fields per line, and the
error strings name every one: `1` line index (`atoi`), `2` text (`"Missing text on line %d, dialog…"`),
`3` gender text (`"Missing gender field on line %d…"`), `4` response/link — `#` becomes `-1`, else
`atoi` (`"Missing response value on line …"`), `5` (`"Missing trait dependency field o…"`),
`6` (`"Missing event script field on li…"`), and then a `while (i < 7)` loop of seven clan fields
(`"Missing clan field %c on node …"`). **No camera column, no shot name, no FOV, no anchor exists in
the record.** The exported corpus agrees: all 147 files under `dlg/` carry exactly thirteen
tab-delimited `{…}` fields per line.

The only per-line escape hatch is a Python string. `CDialog::process_npc_line` (`0x100e8100`) calls
`CDialog::CallEventScript` (`0x100e4f30`) once per NPC line, which copies the field into a **256-byte**
buffer (`Q_strncpy(buf, script, 0x100)` — so the field is truncated at 255 characters), splits it on
**`;`** (`0x10562f58`) then **`&`** (`0x10562f54`), and runs each fragment through
`CDialogDependency::CallPyDialogFunc(dep, fragment, playerEnt, npcEnt, 0x100, NULL)`. That is where
every shipped `SetCamera` lives: 110 occurrences across five files (`downtown la/chunk2.dlg`,
`downtown la/chunk3.dlg`, `main characters/gary.dlg`, `main characters/nines.dlg`,
`santa monica/tourette.dlg`), always inside a script field, e.g.
`{ pc.SetCamera("Chunk2"); npc.SetDisposition("PrinceSitting", 1) }`.

The bridge itself was read: the Python native `SetCamera` is `FUN_10198070`
(`PyArg_ParseTuple(args, "Os", …)`, `"bad args to SetCamera()"`, else
`"SetShot needs to be called on a v…"`), which resolves the actor to a `CBasePlayer*` through `+0xa8`
and calls `CBasePlayer::SetCamera`. Its table neighbours are the whole actor API (`GetOrigin`,
`SetGesture`, `SetDisposition`, `React`, `SeductiveFeed`, `DialogDiscipline`, `GiveItem`,
`StartBarter`, …) and **there is no dialogue-advance native at all** — line advance is
`CDialog::Pick` / `goto_line_for_response` / `process_npc_line`, entirely inside `vampire.dll`, driven
by a client usermessage, and the only thing it hands to Python is the line's script fields.

A per-line shot therefore reaches the engine only as an explicit `SetCamera(actor, "Shot")`
in the dialogue's own script field or `.py`; the camera created at `StartPlayerDialog` persists unchanged for
every line that does not ask. When a new shot *is* set, the re-shot branch reuses the same
`camera_cinematic` and changes only `m_ShotIndex`, the anchors and `m_nClientResetFrame` — which is
exactly the client's "new `m_ShotIndex` ⇒ clear the three angle-settled flags and continue from the
current pose" re-seed. No entity is destroyed and no blend is armed.

**`CBasePlayer::EndPlayerDialog` `0x10178400` cuts.** `UTIL_Remove(GetCineCamera())`, unconditionally —
*not* gated on `+0x204 & 0x4` — then `SetCineCamera(NULL)`, `SetImmobilized(false)`, re-draw the
holstered weapon if `+0x1e01` was set, clear the dialogue partner, and notify the player-events
manager. No blend, no fade, no dampening: the camera entity dies the same tick and the client's next
frame renders the player's own eye. (`m_flResetCameraDampeningTime`, `DT_Local +0x13c`, is written only
by `CPointTeleport::vfunc113` — a teleport affordance, not a camera-restore blend.)

**`DialogPOV` (`flags & 0x10`) has exactly one reader**, the NPC eye/look-target maintenance
`CAI_BaseNPC::FUN_1026b810`. Its first arm resolves the conversation partner's player, takes that
player's cine camera, and aims at the **camera's** `EyePosition` — `CBaseCineCam::vfunc193`
`0x1006d910`, which returns `m_vecCamOrigin (+0x5ec)` — instead of the player's eye whenever the camera
is live and its shot sets `0x10`. Either choice is gated by the head-turn feasibility test
`FUN_10325da0`; if that fails the function falls through to the ordinary target chain
(`m_hTargetEnt`, enemy, navigator goal, hint, nearest-NPC scan). Nothing else in `vampire.dll` reads
`0x10`. So `DialogPOV 1` makes the conversation NPC address the lens rather than the player.

**`FUN_1026b810` is `CAI_BaseNPC::MaintainEyeDirection`, vtable slot 333 (2026-09-07).** The base is
`CBaseCombatCharacter::MaintainEyeDirection` `0x10325580`. Every shipped `CNPC_V*` class fills slot 333
with `CAI_BaseNPCTroika::FUN_102bff20`, which runs the blink timer (gated on
`m_flPlayerDist < 512.0f`, `_DAT_10483aac`), pushes the idle-scan re-schedule `+0x5d6c` to
`curtime + 2.0` (`_DAT_10452dc4`) for as long as a dialogue partner is live — suppressing the
nearest-NPC scan for the whole conversation — and then calls the base unchanged. **The `DialogPOV` arm
is therefore reached by every shipped NPC.**

The full chain, in order (`m_hEyeLookTarget` `+0xe64`, `m_vEyeLookTarget` `+0xe44`, `m_vCurEyeTarget`
`+0xe50`, `m_flEyeIntegRate` `+0xe3c`, `m_RelativeEyeTarget` `+0x5b94`, next-scan time `+0x5d6c`):

0. **`m_RelativeEyeTarget > 0`** — the whole selection is skipped and `FUN_1026b580` resolves a
   relative target in the tail.
1. **Dialogue partner.** `p = EHANDLE_Get(this->m_hDialogPartner +0xFE8)`; `player = *(p + 0xa8)` — the
   cached `CBasePlayer*`, non-NULL only for players, the same field `EndGrapple`,
   `CanStartGrappleAttack` and `CDialog::Acquire` use to mean "this entity is a player". NULL skips the
   arm. Then `cine = GetCineCamera(player)` (`FUN_1017cf90`: needs `player+0x1ec4 > 0` **and** the
   handle at `player+0x19b4` live). With `cine` live **and** its shot record's `0x10` set the candidate
   is the camera's `vfunc0x304` (slot 193, `m_vecCamOrigin`); otherwise it is `player->EyePosition()`.
   Whichever is chosen is gated; **a refusal from either branch falls through to step 2, it does not
   retry the other branch.**
2. **`m_hTargetEnt` (`+0x5ce4`)** — `EyePosition()`, gated.
3. **`vfunc 0x29c` (the enemy)** — `EyePosition()`, gated.
4. **The navigator goal** (`m_pNavigator +0x5d34`): position from `FUN_102ee5e0`, with its **Z replaced
   by the NPC's own `EyePosition().z`** when `navigator+0x18 == 0`; gated; on a pass it aims and
   **returns immediately without writing `m_hEyeLookTarget`** — a positional, non-entity target.
5. **A sound/hint memory** — `FUN_10269aa0`/`FUN_10269c70` on `0x6d` or on `0x6a`, then `vfunc 0x768`'s
   record whose `+0x4` is `1` or `8`, point at `record+0x20`; gated; same immediate return.
6. **The nearest-NPC idle scan**, only when `+0x5d6c < curtime` (and first, if the gate now refuses the
   currently held target, `+0x5d6c` is zeroed to force the scan). It sweeps a **300-unit sphere centred
   at `EyePosition() + forward × 300`** (`_DAT_10462b84 = 300.0f`), skipping `this`, requiring
   `+0x94 != 0` (an AI) or `GetFlags() & 0x80` (`FL_CLIENT`), taking the nearest candidate from a seed
   distance of **16384.0**, each gated. With no winner `m_vEyeLookTarget = EyePosition() + forward ×
   500` (`_DAT_10457f5c`) and `+0x5d6c = curtime + 0.5` (double `_DAT_10449270`); with a winner
   `+0x5d6c = curtime + RandomInt(1,5)`.

The tail always runs: a resolved handle equal to `this` is cleared; then a live handle gives
`m_vEyeLookTarget = target->EyePosition()` and a dead one gives `CalcLookData` + `forward × 25`
(`_DAT_10462994`); and `m_vCurEyeTarget` is integrated toward it in **fixed 0.1 s steps** (double
`_DAT_104493d0`) as `m_vCur = (1 − rate)·m_vCur + rate·m_vLook`.

**`FUN_10325da0` is a 30° cone about the head's current forward — nothing else (2026-09-07).**

```c
// FUN_10325da0(this, const Vector &target) — the whole function
CalcLookData(this, &headPos, &headForward);          // 0x10014eb6
v = Normalize(target - headPos);                     // VectorNormalize, length discarded
return dot(headForward, v) > 0.866;                  // FCOMP *double* ptr [0x1049e0b8]
```

**`0x1049e0b8` is a `double` and reads `0.866`** — `acos(0.866) = 29.9995°`, i.e. a 30° half-angle
cone. There is **no distance term, no separate yaw or pitch limit, and no flag**: one 3-D dot product
against one constant, returning `AL = 1` only on a strict `>`. Its three callers all treat the result
as a boolean: `CAI_BaseNPC::FUN_1026b810`, `CAI_BaseNPC::FUN_1026b270` and
`CBaseCombatCharacter::MaintainScriptedEyeDirection` `0x10325620`.

The basis matters. `CBaseCombatCharacter::CalcLookData` (`0x10331da0`) returns, when the model has a
valid `m_idxHeadBone`, the **head bone's world position** (`m_vecViewOffset` transformed through the
head bone matrix, cached at `+0x1090` and refreshed once per engine frame against `+0x108c`) and the
**head's world forward** (`m_vecHeadLocalForward` rotated by the same matrix, cached at `+0x109c`);
with no head bone it falls back to `EyePosition()` (vfunc `0x304`) and `AngleVectors(eyeAngles)`
(vfunc `0x5bc`). Because the head's forward itself follows the previous frame's smoothed
`m_vCurEyeTarget`, **the gate is hysteretic**: a target the head is already turned toward stays
admissible, and one 45° off the current head pose is refused even when it is straight ahead of the
body. The gate is an admission test, not a clamp — a refusal advances the chain rather than
constraining the aim.

### The client view-composition chain — who wins, and how they compose (2026-09-07)

`0x100a7770` is not the whole priority ladder; it is `C_BasePlayer::CalcView`, vtable slot 187, and it
is two arms long. The ladder is spread over four functions, in this order per rendered frame.

1. **`CViewRender::SetUpView` `0x10191710`** seeds the `CViewSetup` (it lives at `CViewRender + 0x10`;
   `fov` at `+0x28`, `fovViewmodel` `+0x2c`, `origin` `+0x38`, `angles` `+0x50`, `zNear` `+0x5c` =
   `8.0`, `zFar` `+0x60` = `28400.0`, `m_bOrtho` `+0x16` and the ortho rect `+0x18..0x24`), calls
   `CViewRender::CalcView`, and then calls `g_pClientMode->OverrideView` **only if** a cine camera is
   adopted **or** the player is in third person with the engine's suspend flag clear and no
   view-effect veto. In plain
   first person with no cine camera nothing after `CalcView` touches the view — so the `camera_track`
   override cannot fire in pure first person either, except that a live scripted weight is itself one
   of `CAM_IsThirdPerson`'s disjuncts (§2), which makes the test true.

   **The intermission arm and the suspend flag are two different slots.** `SetUpView` reads
   `render->vfunc37()` (`+0x94`) and calls `CViewRender::CalcIntermissionView` `0x10190a70` when it is
   true; only when it is false does it test `render->vfunc38()` (`+0x98`) and call `CalcView` if
   *that* is clear. Slot 38 is not the intermission query: it appears in three places and every one
   *suspends* work — it skips `CalcView` entirely (so the view setup keeps last frame's pose), it is a
   conjunct of the third-person `OverrideView` gate above, and it gates the weapon input dispatch in
   `FUN_100fcca0`. It is a paused/suspended flag; the corpus names neither it nor its writer.

   **The base FOV seed is the engine's own scalar.** `viewsetup.fov` is
   `render->GetFieldOfView()` (`vfunc40`, `+0xa0`), which returns `engine.dll _DAT_201a1bd8`. That
   scalar is written by `IVEngineClient::SetFieldOfView` (`CEngineClient::vfunc77` `0x2001b130`,
   vtable `+0x134`), and `client.dll`'s only caller of it is `FUN_100f12b0`:
   `engine->SetFieldOfView(localplayer ? (float)localplayer->m_iFOV /*client player+0x1690*/
   : default_fov.GetFloat())`. **`default_fov`** is the ConVar at object `0x105f9990`, default `"75"`,
   flags 0, constructed at `0x101622d0`. `fovViewmodel` is seeded from the ConVar at `0x105fc71c` and
   then overridden by `m_iViewmodelFOV` (`player+0x16a4`) when it is non-zero; `zFar` is replaced by
   `m_skybox3d_scale (player+0x17a4) × _DAT_1024fa54` when `m_skybox3d_area (+0x17b4) != 0xff` and the
   scale is positive; and `scr_ofsx/y/z` are forced to 0 when `engine->GetMaxClients() > 1`.

   `DAT_104a57e4` itself is the named interface **`VEngineRenderView008`**, `engine.dll`'s
   `CVRenderView` (49 slots, vftable `0x20187e2c`), written once by `CHLClient::vfunc0` `0x100cb5a0`.
   Slot 39 (`+0x9c`) is `GetViewEntity()` (`DAT_20315be0`), slot 41 (`+0xa4`) is `GetAreaBits()`.
2. **`CViewRender::CalcView` `0x10191200`** is the ordinary first/third-person view and mentions no
   cine camera: `DriftPitch`, `CalcBob`, eye origin, engine view angles, view shake, water offset,
   `V_CalcRoll` (first person only), the three `scr_ofs*` offsets, punch angles, both viewmodel
   solves, and the Z step-smoother. Its last arm hard-replaces origin and angles with a spectated
   entity's — **there is no separate death, feed or seduction `CalcView` arm**; feed and seduction
   reach the view through the weights of §3, and a scripted feed shot through a `camera_cinematic`.

   **The spectator replace, exactly** (`0x1019158e`–`0x101915f2`): `viewent =
   render->GetViewEntity()` (`+0x9c`), `maxcl = engine->GetMaxClients()`
   (`+0xcc` → `CEngineClient::vfunc51` `0x2001a8d0` → `DAT_20315bdc`), then
   `CMP ESI,EAX ; JLE skip` — the replace runs only when **`viewent > maxcl`, strictly greater**. There
   is no literal constant: in single player `maxcl` is 1, so any view-entity index ≥ 2 replaces the
   view. `ClientEntityList->GetEnt(viewent)` (`0x100d0cf0`) must resolve; then `GetAbsOrigin()`
   (vfunc `+0x24`) is copied into `CViewRender+0x48` and `GetAbsAngles()` (`+0x28`) into
   `CViewRender+0x60`. FOV is not touched. (The duck/wolf arm earlier in the function tests
   `player+0x16f4 & 0x400` and adds `_DAT_1022406c = 64.0` to `origin.z` instead of the view offset.)
3. **`ClientModeVampire::OverrideView` `0x10029980`** is two calls: `C_BasePlayer::CalcView`, then
   `ClientModeShared::OverrideView` `0x100d4040`.
4. **`C_BasePlayer::CalcView` `0x100a7770`** runs the vehicle arm first, then
   `cine = GetCineCamera()` (`FUN_100a7890`: a master enable cvar, `m_iCameraOverrideIdx != 0`, and an
   EHANDLE cache at `player+0x1640` invalidated when its entity index stops matching), and calls
   `C_BaseCineCamera::CalcView` `FUN_10001b50` when `IsActive()` (`FUN_10001970` =
   `CamMode(0x45c) != 0 && bActive(0x465)`). `CalcView` starts the shot if one is pending, and — only
   once `m_flLastTime (0x490)` is positive, i.e. the shot has actually started — runs `Update` and
   **hard-writes all three** of origin (`0x468`), angles (`0x474`) and FOV (`0x480`). No blend, no
   weight.
5. **`ClientModeShared::OverrideView` `0x100d4040`** applies the active weapon's own view override
   (`vfunc244`) first, then branches: with **no** live cine camera it calls `CInput` slot 31
   (`FUN_100ffb00` — `origin += m_vecCameraOffset`, `angles = m_angCamera`, then the track override);
   with one, it calls slot 33 (`FUN_100ffb90`, the track override) **directly**. Then the
   orthographic block below; then an early return when the live shot's viewmodel predicate passes;
   then the `scr_ofs*` viewmodel adjustment.

   **That block is Source's orthographic debug view, not an off-centre projection** (listing
   `0x100d40b6`–`0x100d4103`):

   ```c
   if (input->CAM_IsOrthographic() /* CInput slot 48, +0xc0 = FUN_100ff940 → CInput+0x1b8 */) {
       v->m_bOrtho       /*CViewSetup +0x16*/ = 1;
       input->CAM_OrthographicSize(&w, &h);   /* slot 49, +0xc4 = FUN_100ff950 */
       v->m_OrthoLeft    /*+0x18*/ = −w * 0.5f;      // _DAT_101e34dc = 0.5
       v->m_OrthoTop     /*+0x1c*/ = −h * 0.5f;
       v->m_OrthoRight   /*+0x20*/ =  w * 0.5f;
       v->m_OrthoBottom  /*+0x24*/ =  h * 0.5f;
   }
   ```

   `w` and `h` are the ConVars **`c_orthowidth`** (object `0x104d2130`, static init `0x100fb240`) and
   **`c_orthoheight`** (object `0x104d24f0`, init `0x100fb290`), both default `"100"` and flags `0x80`
   (`FCVAR_ARCHIVE`); `FUN_100ff950` returns `0.0` for either when its `IsCommand()` is true. The
   enable is `CInput+0x1b8`, reached from the `camortho` console command (`FUN_101001e0`, string
   `0x102bd490`). No shipped content sets any of the three.

**So the cine camera wins the base pose and the third-person boom is skipped entirely** — a scripted
shot is never displaced by the boom. **The `CInput` track override then composes on top of whatever
the base is**, because both branches end in `FUN_100ffb90`: with a live cine camera *and* a live
`m_flCameraOverrideFadeStartTime` ramp, the track override lerps **from the cine camera's pose** toward
the track's at weight `CInput+0x100`, so at weight 1 the track wins outright and at weight 0 the cine
camera stands. They are one camera in series, never two rival viewpoints. The weapon override runs
before both and is overwritten by either, and the vehicle arm loses to the cine camera (VtMB ships no
drivable vehicle, so that arm is dead in retail too).

**All three paths write one FOV scalar.** There is no `ScaleFOVByWidthRatio` and no aspect arithmetic
anywhere in `client.dll` — the string does not exist, and `SetUpView` hands `viewsetup.fov` to the
engine untouched beside `zNear`, `zFar`, an aspect field of `1.0` and the ortho rect. The cine
camera writes that scalar (`= m_flCurFov`), the player path leaves the base FOV in it, and the track
override lerps whatever is there toward `m_flCameraFOVOverride`. So the 4:3-referenced Hor+ widening
recorded under "The lens" applies identically to all three; the client never distinguishes them.

**`C_BaseCineCamera::OnDataChanged` `0x100024c0`, complete** (decompiled through the adjustor thunk:
real offset = printed offset + 8):

```c
rec = GetShotRecord(); flags = rec[0x20];
if (m_nClientResetFrameCache /*0x498*/ != m_nClientResetFrame /*0x460*/) {
    m_bShotStartPending /*0x4a2*/ = 1;  m_bActive /*0x465*/ = 1;
}
if (m_ShotIndexCache /*0x49c*/ != m_ShotIndex /*0x454*/) {
    if (flags & 0x80) m_bSnapPending /*0x4a1*/ = 1;            // SnapOnShotChange
    else              m_bAngleSettled[0..2] /*0x4c1..0x4c3*/ = 0;
    if ((flags & 0x200) == 0) gHUD->HideHud(0xa06d); else gHUD->ShowHud(0xa06d);
}
if (!IsActive() && m_bWasActive /*0x4a0*/) gHUD->ShowHud(0xa06d);
m_bWasActive = IsActive();  m_ShotIndexCache = m_ShotIndex;
```

Note the asymmetry: `m_nClientResetFrame` is **not** written back into `0x498` here — that write is
shot start's, so the pending flag survives until the shot actually starts. `m_bDrawPlayer (0x464)` and
`DrawViewmodel` are not consulted here; they are read live by the draw gates below.

**Shot start `FUN_10002210`, both arms exactly:**

```c
if ((flags & 2) == 0 || (flags & 1) != 0) {          // no End, OR has Start
    m_vecCurOrigin /*0x468*/ = m_vecCamOrigin /*0x410*/;
    m_angCurAngles /*0x474*/ = m_angCamAngles /*0x428*/;
    m_vecShotStart /*0x484*/ = m_vecCamOrigin;
} else {                                             // End without Start: dolly in from the live view
    vs = CViewRender::GetViewSetup();
    m_vecCurOrigin = vs->origin;  m_angCurAngles = vs->angles;  m_vecShotStart = vs->origin;
}
m_bPositionSettled /*0x4c0*/ = 1;
m_flLastTime /*0x490*/ = engine->GetCurTime();
m_nClientResetFrameCache /*0x498*/ = m_nClientResetFrame /*0x460*/;
m_vecSettledOrigin /*0x4b4*/ = m_vecCurOrigin;  m_flSpeed /*0x4a4*/ = 0;
for (i=0;i<3;i++) { m_flTurnRate[i] = 0; m_bAngleSettled[i] = 0; }
m_bShotStartPending = 0;
if (flags & 0x80) m_bSnapPending = 1;
```

The live-view arm is taken **only for `End`-without-`Start`** — the shipped `jack.txt` /
`dialogdefault.txt` shape. Neither `m_flFOV (0x458)` nor `m_flCurFov (0x480)` is touched by shot start:
in mode 1 the FOV is re-established every frame from the *shot file*, and the replicated `m_flFOV` is
only used by the `CamMode != 1 && != 4` copy-through. The settle flags start **asymmetric** —
position settled (so its deadband is the wide `DistanceTolerance` from frame one), all three angle axes
unsettled (so their deadband is the tight 1.0° and the camera acquires its aim precisely before
parking).

The destructor `FUN_10001920` restores the HUD when the shot had hidden it (`(flags & 0x200) == 0`).

### The client tracker's exact numerics (2026-09-07)

**The delta time is the camera's own, latched once per rendered frame.** `C_BaseCineCamera::Update`
`FUN_10001a20` returns immediately when `m_nFrameCache (0x494)` already equals the engine frame count,
so the tracker advances exactly once per rendered frame however often `CalcView` is reached. Otherwise
`dt = engine->GetCurTime() − m_flLastTime (0x490)`, **clamped to 1.0 s at the top
(`FCOMP _DAT_101e34ec = 1.0f` at `0x10001a58`) and replaced by a flat 0.01 s whenever it falls below
0.01 s** (`FCOMP _DAT_101e34e8` at `0x10001a75`, then `MOV 0x3c23d70a`), which covers zero and
negative, then `m_flLastTime` and `m_nFrameCache` are restamped. It is a `curtime` difference, not
`gpGlobals->frametime`. **`_DAT_101e34e8` reads `0.00999999977f` out of the image** — the compare
constant and the stored literal are the same 0.01, so the floor is a single threshold and there is no
separate ~1/255 s boundary.

**Position `FUN_10001fe0`** — the listing (`0x10002083`–`0x100021b1`) resolves what the decompile's
normalize hides:

```c
threshold = m_bPositionSettled ? rec->DistanceTolerance : 1.0f;
dist = |m_vecCurOrigin − m_vecCamOrigin|;   m_flDistRemaining /*0x4c4*/ = dist;
m_bPositionSettled = (dist < threshold);
if (settled) { m_vecSettledOrigin = m_vecCurOrigin; m_flSpeed = 0; }
else {
  dir      = normalize(m_vecCamOrigin − m_vecCurOrigin);
  stopDist = m_flSpeed*m_flSpeed / (2*rec->MoveAccel);                  // FUN_100010b0
  m_flSpeed = Approach(m_flSpeed, stopDist < dist ? rec->MoveSpeed : 0.0f, rec->MoveAccel, dt);
  m_flSpeed = clamp(m_flSpeed, 1.0f, rec->MoveSpeed);                   // ← a 1.0 u/s FLOOR
  m_vecCurOrigin += dir * min(m_flSpeed * dt, dist);
}
```

There is no hidden integration constant: the step is a first-order Euler advance clamped by the
remaining distance. The helpers are pure FPU leaves — `FUN_100010b0(v,a) = v²/2a`,
`FUN_10001070(cur,tgt,a,dt)` is an unclamped approach, `FUN_100010d0(a,b,c) = |(a−b)/c|`,
`FUN_100010e0(a,b) = sqrt(2a − b)`. The **1.0 u/s speed floor** is what makes the viewmodel predicate's
`speed > 1.0` test below a clean "is the camera dollying" question.

**Turn rate `FUN_10001c80`** is the stopping-distance approach of the tracker section above, except
that with `SyncRotateOnMove` set and the position unsettled it returns `|delta| / T` and bypasses
`MaxTurnRate`, where `T = FUN_100010f0(speed, MoveSpeed, MoveAccel, distRemaining)`. That solve,
recovered from the listing `0x100010f0`–`0x100011bb` because the decompile is unusable:

```c
R1 = vmax*vmax/(2*a);  R2 = v*v/(2*a);
if (2*R1 < d)  return (d − ((vmax−v)*(vmax−v)/(2*a) + R1))/vmax + |(vmax−v)/a| + |(0−vmax)/a|;
if (R2 >= d)   return |(0−v)/a|;
vpeak = v + sqrtf(2*a − 0.5f*(d − R2));       // FUN_100010e0(a, 0.5*(d − R2))
return |(vpeak−v)/a| + |(0−vpeak)/a|;
```

**Two retail defects fall out of the bytes** and are catalogued in `docs/vtmb/retail-defects.md` §7:
the trapezoid arm's acceleration distance is `(vmax − v)²/2a` rather than `(vmax² − v²)/2a`, correct
only from `v = 0`; and the triangle arm's radicand `2a − 0.5(d − R2)` is dimensionally inconsistent
(an acceleration minus a distance) and goes negative whenever `d − R2 > 4a`, producing a NaN `vpeak`
and a NaN turn rate. At the parse defaults (`MoveSpeed 150`, `MoveAccel 50`) the triangle arm is
entered for `d ≤ 450` and the radicand is negative past `d − R2 > 200`, so **a default-rate
`SyncRotateOnMove` shot travelling 200–450 units NaNs its pan in retail**. The band exists only when
`MoveSpeed > 2·MoveAccel` (the triangle arm needs `d ≤ vmax²/a`, the radicand goes negative past
`d − R2 > 4a`), and **no shipped `SyncRotateOnMove` shot satisfies that** — all 32 author their own
rates (`jack.txt` 500/250 needs `d ≤ 1000` with the radicand negative only past `d − R2 > 1000`;
`special-case.txt` 300/450, `andreibasement.txt` 80/40, `npcfollowfromplayer.txt` 80/100), so the NaN
is reachable only for a shot that leaves both rates at the parse defaults, which none does.

**FOV `FUN_10001c20` is a copy, never a lerp**: `m_flCurFov (0x480) = rec->FieldOfView (+0x100)`, every
frame, from the *shot record* — the replicated `m_flFOV` is never consulted in mode 1. Ahead of it sits
a dev-cvar guard: when it is set the function returns the cvar's value **without writing `m_flCurFov`**,
so the rendered FOV freezes at its previous value rather than following the cvar.

**The cvar is `camera_fov`, default `"-1"`, flags 0.** `DAT_102de30c` is not the object but
`object + 4`, the parent/self pointer the compiler emits for `ConVar::GetFloat()`; the object is at
`0x102de308` and the CRT static init at `0x10001be0` is
`PUSH 0 ; PUSH "-1" (0x10270c94) ; PUSH "camera_fov" (0x10270c88) ; MOV ECX,0x102de308 ;
CALL 0x100df540`, with the `atexit` destructor pair at `0x10001c00` / `0x10001c10`. The threshold is
**`_DAT_101e34f4 = 10.0f`** (image bytes `00 00 20 41`), so the guard is
`if (!camera_fov.IsCommand() && camera_fov.GetFloat() > 10.0f) return camera_fov.GetFloat();`. The
second `IsCommand()` test at `0x10001c4c` is dead — control only reaches it when the first returned
false. Because the default is `-1`, the guard never fires in a shipped run; setting `camera_fov` above
10 *freezes* the scripted-shot FOV rather than overriding it, since `C_BaseCineCamera::CalcView` reads
`m_flCurFov (0x480)` directly and `FUN_10001fa0` discards the returned value (`FSTP ST0`).

**Snap `FUN_10002390` is one-shot.** It copies goal origin/angles to current, re-seeds the shot start
and settled origin, zeroes the speed and the three turn rates, clears the three angle-settled flags,
re-derives the angles from the look-at when `CamMode == 1`, and then **clears `m_bSnapPending`
(`0x4a1`)** — so `SnapOnShotChange` snaps once on the shot change and the camera tracks normally
afterwards.

**No blend field exists on the class.** `0x480` is FOV, `0x484..0x48c` the shot-start origin, `0x490`
the last time, `0x494` the frame latch, `0x498` the reset-frame cache, `0x49c` the shot-index cache,
`0x4a0..0x4a2` the three bools, `0x4a4` speed, `0x4a8[3]` turn rates, `0x4b4` the settled origin,
`0x4c0..0x4c3` the settle flags, `0x4c4` the remaining distance. All three client exits — `CamMode`
going 0, `m_iCameraOverrideIdx` going 0, and the entity being removed — are hard cuts on the frame the
change arrives.

**All of the above is ported** (`Public/ElysiumCameraSolve.h`,
`Private/Player/ElysiumCameraSolve.cpp`, asserted by `Elysium.Substrate.CameraTracker`). The two frame
deltas are guards on `Advance`'s `DeltaSeconds` **parameter** — `FrameDeltaCeiling` 1.0 s,
`FrameDeltaFloorThreshold` 1/255 s, `FrameDeltaFloor` 0.01 s — because the substrate never reads a
clock. **Retail's threshold and floor are the same constant, 0.01 s** (`_DAT_101e34e8`, read from the
image above), so `FrameDeltaFloorThreshold` should be 0.01 s, not 1/255 s: retail floors a 5 ms frame
to 10 ms and the port currently passes it through. The once-per-rendered-frame latch is the
`GFrameCounter` stamp the camera component, the camera
service and the camera modifier already take. `UnsettledAngleTolerance` is retail's 1.0°,
`MinTrackSpeed` retail's 1.0 u/s floor, and `IsDollying()` is `FUN_100019a0`'s `speed <= 1.0` test read
off that floor. `RemainingTranslationSeconds` is `FUN_100010f0`'s three arms verbatim with **both
defects kept** and only the triangle arm's radicand clamped at zero. `MoveAccel == 0` is an explicit
decel branch, so retail's crawl is reproduced without a hardware divide. `Snap()` is `FUN_10002390` as
a real one-shot, armed by `bSnapPending` and consumed at the top of the next `Advance`. `TrackFov()` is
the per-frame copy from the shot record, with the dev-cvar guard **including the freeze** — the guard
returns the cvar value without writing the cached FOV — under `elysium.CameraShotFovOverride` and a
`FovOverrideThreshold` constant. Retail's name and threshold are now read: the cvar is **`camera_fov`**
(default `"-1"`, flags 0) and the threshold is **`_DAT_101e34f4 = 10.0f`**, so the port's cvar takes
the retail name and `FovOverrideThreshold` becomes 10.0 with a −1 default.

Two things on this path are the port's own and are marked as such in the source: a `MoveSpeed <= 0`
arm, which retail's parser can never reach (its `CameraConstraints` default is 150 u/s and no shipped
file writes 0) but the port's constraint-less producers rely on; and a floor of one frame's delta under
the `SyncRotateOnMove` divisor, standing where retail's unguarded `|delta| / T` would divide by a zero
`T`.

### The `camera_track` override channel — fields, ramp and the server fade machinery (2026-09-07)

**The `CInput` override `FUN_100ffb90` (slot 33), exactly:**

```c
if (0.0f < m_flScriptedWeight /*CInput+0x100*/) {
  e = SimpleSpline(m_flScriptedWeight);                 // FUN_100fdb30 = t*t*(3 − 2t)
  AngleVectors(angles, fwd);
  viewFwdPoint = *origin + fwd * 240.0f;                // _DAT_1022b298, 240 Source units forward
  *origin = *origin + (m_vecOverrideOrigin /*+0x17c*/ − *origin) * e;
  dir = (viewFwdPoint + (m_vecOverrideTarget /*+0x188*/ − viewFwdPoint) * e) − *origin;
  VectorAngles(normalize(dir), angles);
  angles->roll = e * m_flOverrideRoll /*+0x194*/;       // the base roll is discarded outright
  *fov = *fov + (m_flOverrideFov /*+0x198*/ − *fov) * e;
}
```

`viewFwdPoint` is a stand-in "what you are looking at" point — the view origin plus **240 Source units
(609.6 cm) along the current view forward** (`_DAT_1022b298`, read from the image; the listing multiplies
each of the three forward components by it at `0x100ffbd3` / `0x100ffbdd` / `0x100ffbe7`) — so the aim
interpolates as a *point* and pitch and yaw fall
out of `VectorAngles`. Roll is not interpolated from the base: it is `e × override.roll`, so the base
view's roll vanishes the instant the weight is non-zero. `CInput+0x17c/0x188/0x194/0x198` are refreshed
once per `CAM_Think` and **only while the weight is already non-zero**, straight off the replicated
player fields.

| server `DT_Local` | server `CBasePlayer+` | client local | client `player+` | field | encoding |
|---|---|---|---|---|---|
| `+0x34` | `0x1e74` | `+0x34` | `0x168c` | `m_iHideHUD` | int, 9 bits |
| `+0x38` | `0x1e78` | `+0x38` | `0x1690` | `m_iFOV` | int, 9 bits |
| `+0x3c` | `0x1e7c` | `+0x4c` | `0x16a4` | `m_iViewmodelFOV` | int, 8 bits |
| `+0x84` | `0x1ec4` | `+0x88` | `0x16e0` | `m_iCameraOverrideIdx` | int, 11 bits |
| `+0xd4` | `0x1f14` | `+0xd8` | `0x1730` | `m_bDrawViewmodel` | bool, 1 bit |
| `+0xec` | `0x1f2c` | `+0xf0` | `0x1748` | `m_vecCameraViewOverride` | Vector |
| `+0xf8` | `0x1f38` | `+0xfc` | `0x1754` | `m_vecCameraTargetOverride` | Vector |
| `+0x104` | `0x1f44` | `+0x108` | `0x1760` | `m_flCameraFOVOverride` | float, 10 bits, `[0, 180]` |
| `+0x108` | `0x1f48` | `+0x10c` | `0x1764` | `m_flCameraRollOverride` | float, 12 bits, `[−180, 180]` |
| `+0x10c` | `0x1f4c` | `+0x110` | `0x1768` | `m_flCameraOverrideTimestamp` | SendPropTime |
| `+0x110` | `0x1f50` | `+0x114` | `0x176c` | `m_flCameraOverrideFadeStartTime` | SendPropTime |
| `+0x114` | `0x1f54` | `+0x118` | `0x1770` | `m_flCameraOverrideFadeDuration` | float, 10 bits, **`[−10, +10]`** |
| `+0x118` | `0x1f58` | `+0x11c` | | `m_vecCrossfadeFromLandmark` | Vector |
| `+0x124` | `0x1f64` | `+0x128` | | `m_vecCrossfadeToLandmark` | Vector |
| `+0x130` | `0x1f70` | `+0x134` | | `m_flCrossfadeYawDifference` | float, 12 bits, `[−360, 360]` |
| `+0x134` | `0x1f74` | `+0x138` | | `m_flCrossfadeStartTime` | SendPropTime |
| `+0x138` | `0x1f78` | `+0x13c` | | `m_flCrossfadeDuration` | float, 7 bits, `[0, 10]` |
| `+0x13c` | `0x1f7c` | `+0x140` | `0x1798` | `m_flResetCameraDampeningTime` | SendPropTime |

The server column is `FUN_1018a730`'s `DT_Local` registration read prop by prop; `m_Local` itself is
`CBasePlayer + 0x1e40`, so the `CBasePlayer+` column is `0x1e40 + DT_Local`. The registrars are
`FUN_10246af0` (int/bool: name, offset, size, nbits, flags), `FUN_10246510` (float: … min, max,
proxy), `FUN_10246700` (vector) and `FUN_101ab310` (SendPropTime: name, offset, size).

The client's local block is at `player+0x1658`, but **the skew against the server's `DT_Local` offsets
is not uniform**: `m_iHideHUD` and `m_iFOV` sit at the *same* `+0x34` / `+0x38` on both sides,
`m_iViewmodelFOV` is `+0x10` higher on the client (`+0x3c` → `+0x4c`), and only from
`m_iCameraOverrideIdx` onward does the fixed `+4` hold. The two `CPlayerLocalData` layouts simply
differ; the skew is not one unsent leading member.

**`+0x1f44` is FOV and `+0x1f48` is roll, settled three ways.** The registration above is the first:
`m_flCameraFOVOverride` at `DT_Local +0x104` with 10 bits over `[0, 180]`, `m_flCameraRollOverride` at
`+0x108` with 12 bits over `[−180, 180]`. Second, `SetupVisibility` writes `+0x1f44` from `vfunc0xC4`
(`0x103521f0`) and `+0x1f48` from `vfunc0xC0` (`0x10352228`), and on a `camera_track` slot `0xC4`
returns `m_flViewFOV` (`+0x4ec`) while slot `0xC0` returns `m_flViewRoll` (`+0x4f0`). Third, the
`CBaseEntity` defaults are `75.0f` for slot `0xC4` (`_DAT_104454cc`, the shot-record `FieldOfView`
default) and `0.0f` for slot `0xC0` (`_DAT_104454c4`). The client-side transcription that reads them
the other way round is wrong.

**The ramp, `FUN_100fc900`'s tail** (the same driver that advances every `CInput` weight, §3):

```c
if (m_flCameraOverrideFadeStartTime <= 0.0f) { m_flScriptedWeight = 0.0f; return; }
m_flScriptedWeight = 1.0f;
dur = m_flCameraOverrideFadeDuration;
if (dur <= 0.01f /*_DAT_101e34e8*/) {
    if (-0.01f /*_DAT_10235278*/ <= dur) goto clamp;                 // |dur| <= 0.01: stay at 1
    m_flScriptedWeight = 1.0f + ((now − startTime) / dur);           // dur < −0.01: blend OUT
} else  m_flScriptedWeight = (now − startTime) / dur;                // dur >  0.01: blend IN
clamp: m_flScriptedWeight = clamp(m_flScriptedWeight, 0.0f, 1.0f);
```

Both guards are read from the image: **`_DAT_101e34e8 = +0.00999999977f`** and
**`_DAT_10235278 = −0.00999999977f`** (`0x100fcbd0` and `0x100fcbf7` in the listing) — a symmetric
±10 ms dead band, not the ~1/255 s the first pass assumed. `now` is `engine->GetCurTime()`, fetched at
`0x100fc92c` into `[ESP+0x10]`.

Four regimes: **`startTime ≤ 0` ⇒ the override is off**; **`|duration| ≤ 0.01 s` ⇒ weight 1
immediately, a hard cut in that stays** — so a *negative* duration inside the band is a cut in, not a
blend out; **`duration > 0.01` ⇒ `(now − startTime)/duration`, the blend in**; **`duration < −0.01` ⇒
the weight starts at 1 and decays to 0 over `|duration|`, the blend out**. The ramp is **linear**; the
ease is applied at the point of use by
`SimpleSpline` inside `FUN_100ffb90`. Nothing cancels the override abruptly except the server writing
`m_flCameraOverrideFadeStartTime ≤ 0`.

**Server side, the fade is one signed weight over a per-channel crossfade stack.** The unreplicated
state on `CBasePlayer`: `+0x19b4` the cine camera's EHANDLE, `+0x19b8` `m_flCameraOverrideFadeMarkTime`,
`+0x19bc` the duration **whose sign is its direction** (`> 0` fade in, `< 0` fade out), `+0x19c0/c4/c8`
the view entity, its set time and its crossfade duration, `+0x19cc/d0/d4` the same for the target
entity, and `+0x19d8`/`+0x19e4` a `CUtlVector` of fade-out entries with stride `0x10` —
`{ byte kind (0 view, 1 target), EHANDLE, setTime, crossfadeDuration }`. The corpus names the two
handles `CBasePlayer::m_hCameraViewEntity` (`+0x19c0`) and `m_hCameraTargetEntity` (`+0x19cc`), and the
two durations `m_flCameraViewCrossfadeDuration` / `m_flCameraTargetCrossfadeDuration`.

* **`FUN_1017d900` (`GetCameraOverrideWeight`)** returns `1.0` when the duration is exactly zero,
  `clamp((t − mark)/dur, 0, 1)` when it is positive, and `clamp(1 + (t − mark)/dur, 0, 1)` when it is
  negative. When there is no override, or both entities have gone invalid, it **clears the whole
  channel** (`+0x19b8 = 0`, `+0x19bc = 0`, both handles `-1`, the fade list emptied) and returns 0 —
  the getter is the reaper, so the state is collected lazily on the next query.
* **`FUN_1017d280(player, ent, crossfade)` — set the view entity — starts with
  `SetCineCamera(NULL)`.** The two channels are mutually exclusive by construction, which is why the
  map teardown tears both down together and why adopting a cine camera cancels a track. It then
  clamps a negative crossfade to 0, pushes the outgoing camera onto the fade list when its set time has
  passed and its handle is live, raises the crossfade to the entity's own minimum (`vfunc0xD0`), arms
  the fade, stores the new handle/time/duration, and notifies the entity (`vfunc0xBC`). A null entity
  routes to the fade-out instead.
* **`FUN_1017d460(player, ent, crossfade)` — set the *target* entity — is its exact twin on the
  `+0x19cc/d0/d4` trio, with two differences.** It **does not** call `SetCineCamera(NULL)`
  (`FUN_1017d280` opens with `PUSH 0; CALL 0x100015cd` at `0x1017d285`; `FUN_1017d460` has no such
  call), so *only setting the view entity cancels a cine camera*; and it notifies through **`vfunc0xB8`**
  (`0x1017d5c2`), the target notify, not `vfunc0xBC`. The push condition is the same shape —
  `curtime > m_flCameraTargetSetTime` **and** `m_hCameraTargetEntity` still resolves — and the entry is
  pushed to the front carrying the *outgoing* handle, set time and crossfade duration.
* **Retail defect: both pushers write the kind byte as `0`.** `0x1017d386` (view) and `0x1017d55f`
  (target) are both `MOV byte ptr [ESI],0x0`. `SetupVisibility` discriminates on that byte
  (`0x10352485 TEST DL,DL ; JNZ 0x1035259e`), so a superseded **target** camera is folded through the
  **view** arm: its `vfunc0xC8` viewpoint, `vfunc0xC0` roll and `vfunc0xC4` FOV are crossfaded into the
  published *view* override and it consumes the *view* channel's coverage, while the target point
  itself simply snaps. The fade list's kind-1 branch is unreachable in a shipped run.
* **`FUN_1017d0b0(player, dur)` — arm or re-time — back-dates the start.** Fresh (`mark <= 0`): mark
  now, duration `dur`. Reversing a fade-out: keep the current weight `w` by setting `mark = t − w*dur`.
  A zero duration that has already elapsed restarts as a fade-in. A fade-in that would otherwise finish
  later than `t + dur` is shortened the same way. This is the **symmetric mid-blend reversal** the
  port's weight driver already asserts — retail achieves it by back-dating, not by tracking a separate
  weight.
* **`FUN_1017d6d0(player, dur)` — fade out.** No-op at weight ≤ 0; raises `dur` to each live end's own
  minimum (`vfunc0xD4`); `dur <= 0` hard-clears the mark (an instant snap back); otherwise
  `+0x19bc = −dur` and `+0x19b8 = curtime − (1 − w)*dur`.
* **`CHL2_Player::SetupVisibility` `0x10352120` composes and publishes once per server frame**, not in
  a think. It copies the mark and the signed duration into the replicated pair, and when the weight is
  positive stamps the timestamp, reads the view entity's origin/FOV/roll and the target entity's aim
  point (the target is asked to aim *from* the published view origin, or from `EyePosition()` when
  there is no view entity), computes each channel's own crossfade fraction, and then walks the fade
  list **newest first**, pulling the published value back toward each older camera and accumulating
  coverage multiplicatively (`w[kind] = 1 − (1 − f)(1 − w[kind])`), dropping entries whose channel has
  reached full coverage or whose entity has died. **N cameras can be crossfading at
  once**, on top of the one global signed fade weight, and no duration on this path is ever a constant
  — every value is the caller's argument raised to whatever minimum the entity demands. Finally it adds
  the published view origin to the PVS; separately, an active cine camera **replaces** the PVS with its
  own `m_vecCamOrigin` and returns, skipping the ordinary visibility pass entirely.

**Which factor scales which component, read off the listing.** The frame is `SUB ESP,0x2c` plus four
pushes, so inside the block `ESP = entry − 0x3c` and the slots are: `[ESP+0x10]` the entry's own
fraction `f`, `[ESP+0x14]` the loop counter, `[ESP+0x18]` `now`, **`[ESP+0x1c]` `w[0]` — the view
channel's accumulated coverage**, **`[ESP+0x20]` `w[1]` — the target channel's**, `[ESP+0x24…0x2c]`
the `vfunc0xC8` temp, `[ESP+0x30…0x38]` the `EyePosition` / `vfunc0xCC` temp. Both weights are
initialised to **0** at `0x10352179` / `0x1035217d` and raised to 1.0 or to the clamped live fraction
only inside their own "entity is live" block.

```
103524c9  FMUL float ptr [ESP + 0x1c]    ; origin.x  ×  w[0]   (also .y 103524e1, .z 103524f9)
10352542  FMUL float ptr [ESP + 0x1c]    ; roll      ×  w[0]
1035258b  FMUL float ptr [ESP + 0x1c]    ; fov       ×  w[0]
103525e1  FMUL float ptr [ESP + 0x20]    ; target.x  ×  w[1]   (also .y 103525f9, .z 10352611)
10352633  FSUB float ptr [ESP + 0x10]    ; 1 − f     — the ONLY use of the entry's own fraction
```

So **all three view components — origin, roll and FOV — are folded by the channel's accumulated
weight, and the target point by the target channel's**; the entry's own fraction `f` never scales a
value, it only advances coverage. The drop test at the loop head is `w[kind] >= 1.0` **or** a dead
handle (`0x103523d7`–`0x10352422`), and the drop is a `memmove` compaction plus `--count`. The live
channel fractions are `clamp((now − setTime)/crossfadeDuration, 0, 1)`, or **1.0** when the duration is
`<= 0.0` (`0x10352234`, `0x1035234c`). A consequence worth naming: with no live view entity `w[0]`
stays 0, so every queued view entry folds the published value all the way back to its own camera.

**The camera entity's interface — slots 46 to 53, read out of the bodies.** The two factories are
`FUN_100cb910` (`camera_track`, `0x50c` bytes, `SetClassname("camera_track")`, vftable `0x10453b9c`)
and `FUN_100cb580` (`camera_keyframe`, `0x4b0` bytes, base `CLogicalEntity`, two
`CBaseEntityOutput` constructions at `+0x480`/`+0x498`, vftable `0x104536ec`). `CCameraTrack` extends
`CCameraKeyFrame` with `m_bHoldAtEnd` (`+0x4b0`, key `HoldAtEnd`), `m_flFromPlayerTime` (`+0x4b4`,
key `FromPlayerTime`), `m_flToPlayerTime` (`+0x4b8`, key `ToPlayerTime`) and the runtime block
`m_flTargetStartTime`/`m_hTargetKey`/`m_bTargetPaused` (`+0x4bc/c0/c4`), `m_vecTargetPos` (`+0x4c8`),
`m_flViewStartTime`/`m_hViewKey`/`m_bViewPaused` (`+0x4d4/d8/dc`), `m_vecViewPos` (`+0x4e0`),
`m_flViewFOV` (`+0x4ec`), `m_flViewRoll` (`+0x4f0`), `m_OnCompleted` (`+0x4f4`). **`CCameraKeyFrame`
fills none of these slots** — it keeps the `CBaseEntity` stubs; only `CCameraTrack` implements the
interface.

| vtable | slot | `CCameraTrack` | `CBaseEntity` default | `CBaseCombatCharacter` |
|---|---|---|---|---|
| `+0xB8` | 46 | `0x100cc1c0` — "you are now the camera **target**": stamp `m_flTargetStartTime`, `m_hTargetKey`, `m_bTargetPaused = 1`, fire `m_OnReached`, `SetNextThink(curtime)` + `ThinkSet(LAB_10014d58)` | `0x100267d0` `RET` | (default) |
| `+0xBC` | 47 | `0x100cc250` — the same on the **view** trio | `0x100267f0` `RET` | (default) |
| `+0xC0` | 48 | `0x100cbef0` → `m_flViewRoll` (`+0x4f0`) | `0x10026810` → `_DAT_104454c4` = **0.0** | (default) |
| `+0xC4` | 49 | `0x100cbf10` → `m_flViewFOV` (`+0x4ec`) | `0x10026830` → `_DAT_104454cc` = **75.0** | (default) |
| `+0xC8` | 50 | `0x100cc2e0` → `m_vecViewPos` (`+0x4e0`) | `0x10026850` → `WorldSpaceCenter()` (vfunc `0x300`) | `GetCameraViewpointPosition` `0x10332010` → `CalcLookData(&out, NULL)` |
| `+0xCC` | 51 | `0x100cc320` → `m_vecTargetPos` (`+0x4c8`) | `0x10026890` → `WorldSpaceCenter()` | `GetCameraTargetPosition` `0x103320b0` |
| `+0xD0` | 52 | `0x100cbf30` → `max(0, m_flFromPlayerTime)` | `0x100268d0` → **0.0** | `GetCameraFadeInTime` `0x103321a0` → `m_flCameraOverrideFadeTime` (`+0x10d0`) |
| `+0xD4` | 53 | `0x100cbf70` → `max(0, m_flToPlayerTime)` | `0x100268f0` → **0.0** | `GetCameraFadeOutTime` `0x10332240` → **the same** `+0x10d0` |

So the **per-entity minimum crossfade is a virtual pair, `GetCameraFadeInTime` (`0xD0`) and
`GetCameraFadeOutTime` (`0xD4`), with two shipped implementations**: a `camera_track` answers exactly
the authored `FromPlayerTime` / `ToPlayerTime` keyvalues clamped at zero, while a
`CBaseCombatCharacter` answers **one runtime field for both directions**, unclamped. Any other entity
answers 0 for both, 75.0 for the FOV and 0.0 for the roll.

**The "aim from" argument of `vfunc0xCC` is dead in every shipped implementation.**
`CCameraTrack::vfunc51` reads `[ESP+8]` (the out pointer) and never touches `[ESP+4]`; the
`CBaseEntity` default does the same; and `CBaseCombatCharacter::GetCameraTargetPosition` writes
`[ESP+0x14]` in **both** arms (`0x10332127` head → `CalcLookData`, `0x1033214d` body →
`WorldSpaceCenter()`). The source vector `SetupVisibility` computes — the published view origin, or
`EyePosition()` when there is no view entity — is never read.

**How an NPC becomes the camera target.** `CBaseCombatCharacter::SetAsCameraTarget`
(`0x1000a2d6` / `0x103322e0`) writes `m_bCameraTargetIsHead` (`+0x10d4`) and
`m_flCameraOverrideFadeTime` (`+0x10d0`), then calls `FUN_1017d460(player, this, 0.0f)` **for every
player index 1..maxClients** — a broadcast, not an activator dispatch, and always with a zero
crossfade argument, so the entity's own `GetCameraFadeInTime()` is the only source of a non-zero
target crossfade in shipped content. Its five callers are `CSceneEntity::DispatchStartEvent`
(`0x10082ee0`, the choreo scene event) and four entity inputs on every combat character:

| input | call |
|---|---|
| `SetHeadAsCameraTarget` `0x10332570` | `SetAsCameraTarget(1, 0)` |
| `SetBodyAsCameraTarget` `0x10332610` | `SetAsCameraTarget(0, 0)` |
| `FadeHeadAsCameraTarget` `0x103323d0` | `SetAsCameraTarget(1, inputdata.type == 1 ? inputdata.value : 0)` |
| `FadeBodyAsCameraTarget` `0x103324a0` | `SetAsCameraTarget(0, same)` |

`GetCameraFadeOutTime` has no static caller by design — it is only ever reached as
`viewEnt->vfunc0xD4()` / `targetEnt->vfunc0xD4()` inside `FUN_1017d6d0`.

### The draw gates and the HUD mask (2026-09-07)

**`m_bDrawPlayer` short-circuits `CAM_IsThirdPerson`.** `C_BasePlayer::ShouldDrawLocalPlayer`
`FUN_100a7a50` tests two entity-level predicates, then:

```c
cine = GetCineCamera();
if (cine) return cine->m_bDrawPlayer /*0x464*/;      // FUN_10001990, a one-line accessor
if (IsLocalPlayerEntity() && !g_pInput->CAM_IsThirdPerson()) return false;
return true;
```

`0x464` is read **exactly once in the image, here**, and the test does not call `IsActive` — so while a
cine camera is merely *adopted*, the body is drawn iff the server said so through the replicated
`m_bDrawPlayer` (`DT_BaseCineCam +0x640`), and the third-person weight has no say. This is the one
retail input on this path with no source in the port's shot record — and it is **shipped content, not
just anim event 4050**: `CBaseCineCam::Spawn` raises it from `spawnflags & 2`, which **27 of the 51
shipped `camera_cinematic` directors** author, including `sp_tutorial_1`'s `feedcamera` (see "The
entity's own class surface").

**The viewmodel gate carries a speed term.** `C_BasePlayer::ShouldHideViewModel` (slot 174,
`FUN_100a7ab0`) is `cine && !ShotWantsViewmodel(cine)`, called from `C_BaseViewModel`'s `vfunc4`,
`vfunc9` (`ShouldDraw`) and `vfunc11`, always on the local player. The predicate `FUN_100019a0` reads,
from the listing:

```
FLD [ECX+0x4a4] ; FCOMP double 1.0 ; jump when speed <= 1.0 ; else return false
CALL GetShotRecord ; MOV AL,[EAX+0x20] ; SHR AL,6 ; AND AL,1 ; RET      ; flags & 0x40 DrawViewmodel
```

i.e. **`ShotWantsViewmodel() = (m_flSpeed <= 1.0f) && (shot->flags & DrawViewmodel)`** — the shot opted
in *and* the camera has stopped dollying, `1.0` being exactly the tracker's speed floor. So the hands
come back only after the dolly parks, which is why the shipped interaction shots (terminals, the gym
beat) all snap or arrive before the hands appear. The same predicate is the early-out that skips the
`scr_ofs*` viewmodel adjustment in `ClientModeShared::OverrideView`, so a parked `DrawViewmodel` shot
shows the hands at their authored offset rather than pushed by the player's own view cvars.

**`0xa06d` is a HUD-element bitmask, edge-triggered.** `CHudManager::HideHud(bits)` (slot 113,
`0x10057f50`) walks the element list and `SetVisible(false)`s every element whose own `GetHudBits()`
intersects the mask; slot 114 is the `SetVisible(true)` twin. The same mask is used by
`C_BaseTerminal::vfunc5` and by the dialogue/loading/map UI (`FUN_1016ef40`, `FUN_1016f1b0`,
`FUN_1016f2c0`, `FUN_10170180`, `FUN_10170200`). The cine camera writes it in exactly three places, all
in `OnDataChanged`/the destructor above — shot change with `ShowHud` clear, shot change with `ShowHud`
set, and going inactive or being destroyed after having been active. **There is no per-frame
enforcement**, so a HUD-hiding shot replaced by another HUD-hiding shot does not re-issue the call.

### Divergences from retail in the scripted camera (2026-09-07)

Recorded here beside the faithful behaviour, per `docs/CLAUDE.md`. `docs/project/plans/spine.md` owns
what happens to them.

| Retail | Elysium | Bearing |
|---|---|---|
| unsettled angular deadband **1.0°** | `FElysiumScriptedShotTracker::UnsettledAngleTolerance = 1.0f` | matches — the old `SettleAngle 0.05f` parked 20× tighter on the acquire; retail's `_DAT_101e34ec` band landed with SC1, so a pan now stops inside a degree of its goal |
| speed floor **1.0 u/s** while unsettled | `clamp(Speed, MinTrackSpeed, MoveSpeed)`, `MinTrackSpeed = 1.0 u/s` | matches — the floor landed with SC1 and is read back as `IsDollying()`, which is retail's `FUN_100019a0` `speed <= 1.0` test |
| `SnapOnShotChange` is a **one-shot** flag consumed by `FUN_10002390` | `bSnapPending` armed by `Start` (retail's `FUN_10002210` tail and `OnDataChanged`) and consumed at the top of `Advance` by `Snap()` | matches — the shot cuts on the change and tracks its anchor afterwards, which is what `sp_tutorial_1`'s `LookAtTarget_Snap` needs |
| `RemainingTime` carries both defects above (the NaN band) | `ElysiumCam::RemainingTranslationSeconds` is retail's three arms verbatim, radicand clamped at zero | reproduced — M6, landed with SC1. `jack.txt` closing 100 u from rest answers retail's **0.17 s** again instead of the correct solve's 1.27 s, so the pan lands with the dolly across all 33 `SyncRotateOnMove` shots. The clamp removes only the NaN state, which needs `MoveSpeed > 2·MoveAccel` and no shipped shot has it |
| `MoveAccel == 0` divides by zero, pinning the speed at the 1.0 floor | an explicit decel branch that leaves the speed unchanged; the clamp then pins it at `MinTrackSpeed` | reproduced — M7, landed with SC1: retail's crawl, written as a branch so no hardware divide, NaN or infinity is ever produced. The parse default is 50, so nothing shipped changes; a mod that writes 0 gets the crawl |
| every scripted-shot exit is a **hard cut** (`EndShot`, `EndPlayerDialog`, the interaction closers, `CamMode → 0`, adoption cleared, entity removed) | `FElysiumCameraShotStack` ramps the cine channel **out** over `RampSeconds` | **named modernization** — VtMB's cuts are jarring; declared here, and the authored `MoveTime ≤ 0.05` cut fold of §6 is still reproduced exactly |
| the scripted weight is **eased at the point of use**, `e = SimpleSpline(w)` inside `FUN_100ffb90` | `ElysiumCam::ComposeScriptedShot` receives the raw linear weight (the ease is applied to `Third` and `Feed` only) | unintended |
| composition lerps the **origin and a look-at point** (`origin + fwd*100` → the override target) and re-derives angles with `VectorAngles` | `FMath::Lerp` over `FRotator`s | unintended — for a large angular delta a point lerp swings faster at the start and settles, an angle lerp is uniform |
| `angles.roll = e * shotRoll`, discarding the base roll | roll lerps as part of the rotator | unintended |
| `duration < 0` **is** the blend-out encoding | `Pop(Id, BlendOutSeconds)` — the same behaviour, a different encoding | equivalent |
| the body is drawn iff the replicated `m_bDrawPlayer` says so, short-circuiting `CAM_IsThirdPerson`; its authored source is `spawnflags & 2` on the director, set on **27 of 51** shipped `camera_cinematic` entities | `bBodyEligible = bThirdPerson`; no `bDrawPlayer` on `FElysiumShotPresentation`, and no `spawnflags` parse | unintended — a shot cannot ask for the body, and 27 shipped shots are asking |
| the runtime camera's `m_bForcePlayerLook` defaults to **1**, the director's `point_player` is never copied onto it, and only `CFuncMonitor::vfunc39`, anim event 4050 and the `"Intrusion"` opener clear it | `point_player` has no counterpart at all | unintended — every shipped scripted shot forces the subject's gaze in retail, including the 26 directors that author `point_player 0` |
| `CBaseCineCam::ObjectCaps()` is `0` — the base's `FCAP_ACROSS_TRANSITION` is cleared, so a live shot does not survive a level change | not modelled | unintended |
| `CBaseCineCam::ShouldTransmit` sends the camera **only** to `m_hSubject`'s client, and only while `CamMode != 0` | single-player, no transmit model | equivalent in practice; a shot whose subject is not player 1 would be invisible in retail rather than merely inactive |
| the viewmodel gate is `DrawViewmodel && speed ≤ 1.0` | `!bThirdPerson && (!bNamed \|\| bDrawViewmodel)` — no speed term | unintended — the hands appear during the dolly |
| `dt` is latched once per rendered frame, clamped to 1.0 s, floored at 0.01 s below ~1/255 s | the latch is `LastAdvancedFrame == GFrameCounter` (`ElysiumCameraComponent::AdvanceFrame`, `ElysiumCameraService::Advance`, the modifier's zeroed second pass); the two guards are applied to `Advance`'s `DeltaSeconds` parameter | matches — landed with SC1. The tracker never reads a clock: the ceiling and the 10 ms floor sit inside `Advance`, and the floor is also retail's zero-and-negative handling |
| the cine-FOV dev cvar **`camera_fov`** (default `"-1"`) short-circuits `FUN_10001c20` above `_DAT_101e34f4 = 10.0f` and returns without writing `m_flCurFov`, so the rendered FOV **freezes** | `FElysiumScriptedShotTracker::TrackFov` behind `elysium.CameraShotFovOverride`, freeze included; the threshold is a named `FovOverrideThreshold` constant defaulting to 0 | **the name and the threshold** — M12. Retail's name and threshold are now read: the cvar is `camera_fov`, its default is `-1` and the guard fires above `10.0`, so the port takes the retail name and `FovOverrideThreshold` becomes 10.0 |
| `AttachType` is matched **case-sensitively** | `IgnoreCase` | unintended |
| an unrecognised `Position` value falls through to **`World`** | falls through to `Named` | unintended |
| `Top`/`Bottom` are the abs origin's XY with only Z from the bounds; `AbsMin`/`AbsMax` are whole bounds corners | `BoundsPoint(1.0f)`/`BoundsPoint(0.0f)` — a bounds fraction on all three axes; no `AbsMin`/`AbsMax` | unintended |
| `GrappleVictim` / `GrappleAttacker`, resolved through the grapple role slot | one `GrappleTarget` keyword | unintended — `stealth_kill.txt` writes `GrappleAttacker` ×6 and `GrappleVictim` ×2 (the `Stealth_Kill_1..4` family anim event 4050 selects); `GrappleTarget` appears only in the how-to and in no shot file |
| `OffsetAngles` is parsed and never read | not parsed | equivalent — retail's key is dead |
| `AutoPositionFromTarget` recomputes the origin per tick | parsed and asserted in tests; no solver reads it | unintended |
| the anchor cache is gated on `AttachType None` (and, through the retail bug, on the *Start* anchor's flags) | every anchor re-resolves every frame | matches retail for the 56 shot files with no `Start` block. Ten files author one (`andreibasement`, `kilpatrick`, `lookattarget_b`, `npcfollow`, `npcfollowcut`, `npcfollowfromplayer`, `npcfollowmove`, `special-case`, `stealth_kill`, `tong`); nine of those Starts are `Follow`/`FollowEntAngles` and resolve live in retail too, and exactly one — `special-case.txt`'s `Follow` shot, `Start { AttachType None }` — latches all four anchors at shot start in retail and re-resolves them here |
| `StartShot`/`StartPlayerDialog` immobilize the player and `EndShot` clears bits `0x1`/`0x8` of `player+0x1d60`; `SetCamera` does not | `camera_cinematic`'s `StartShot`/`EndShot` are the stub in `ElysiumStubClasses.cpp`; the director entity, its four `targetname` anchors, the `+0x204 & 0x4` destroy rule and the immobilize pair have no counterpart | unintended |
| `FindBestShot`, its two visibility predicates and anim events 4050/4051 | absent | unintended |
| the 24 Hz server think, the `+0x594` origin selector, `point_player`/`m_bForcePlayerLook` | absent | unintended |
| `EndPlayerDialog` removes the camera unconditionally; the payphone RTTI arm and the holster restore | the port keeps a weight ramp; neither arm is present | the ramp is the declared cut modernization above; the payphone and holster arms are unintended gaps |
| `DialogPOV`'s aim is gated by the head-turn feasibility test `FUN_10325da0` | the port's `DialogPOV` resolver has no feasibility gate | unintended |
| the server track channel: sign-as-direction fades, `FUN_1017d0b0`'s back-dating re-time, per-entity minimum crossfades (`vfunc0xD0`/`vfunc0xD4`), the multi-entry crossfade stack, and "a cine camera cancels the track" | `PublishTrackCamera` and the port's own weight driver; the symmetric mid-blend reversal is reproduced | the reversal matches; the rest is unwired |

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
- The semantic identities of equipped-item record fields `+0x24d0` (the forced-third holster gate)
  and `+0x4fe84` (the sniper-related state). The `camera_class` bit meanings at `+0x2440` are
  recovered in §2.
- The semantic identities and exact formulas of the non-ordinary branches within feed-family
  solver `0x100fe7f0`: player state `0x20` selects `camseduct_*`, while `0x400` selects
  `camdead_*`; their presence and routing are recovered, but their complete solves are not.
- The player-state flag at `+0x16f0` and predicate `0x10192850` that quantise the model alpha.
- Whether the local player's projected-shadow submission follows the model draw gate during a mode
  switch. Model visibility alone does not establish shadow policy.
- The consumer and lifetime policy for the parsed `Particle_FirstPerson` / `Particle_ThirdPerson`
  records: their mode tags are recovered, but not whether a live emitter is stopped, restarted or
  merely culled when the camera predicate changes.

In the scripted shot / cine camera (the 2026-09-07 subsections above) — what the two recovery passes
left open, and nothing else:

- **`CBaseEntity + 0x90`**, the force-transmit-until timestamp both `CBaseEntity::ShouldTransmit`
  `0x100ab020` and `CBaseCineCam::ShouldTransmit` `0x1006e6a0` open on. Its writers were not chased;
  `vtmb_readers 0x90 --cls CBaseEntity` returns only two `client.dll` water functions, so the
  server-side producer is untyped and unfound.
- **`CCameraAnimated`'s `+0x828` / `+0x82c` pair.** `+0x828` is the cached `camera_showdebug` state
  the think writes; `+0x82c` is a float the constructor seeds to `-1.0` and nothing in the opened
  bodies reads.
- **Whether `m_iVFlags` has a client-side twin.** It is a datamap field with no SendProp name, but
  `CPlayerMove::SetupMove` reads it inside the movement path; `client.dll` was not searched for a
  predicted copy.
- **The intent behind `FUN_100010e0(a,b) = sqrt(2a − b)` and its `0.5*(d − R2)` argument.** The bytes
  are unambiguous; the expression they came from is not.
- **The identity of `IVRenderView` slot 38** (`client.dll` `DAT_104a57e4 + 0x98`, `engine.dll`
  `CVRenderView::vfunc38` `0x2010f310`, reading `DAT_20314874`; the twin of `CEngineClient::vfunc15`
  `0x2001a2b0`). Its three use sites all suspend work, so it behaves as a paused/suspended flag, but
  the corpus names neither it nor a writer — `DAT_20314874` is a member of the engine's client-state
  struct reached through a base register, so no absolute-address store exists to chase. **What would
  close it:** a read of the engine's `svc_intermission` / pause message handlers around
  `FUN_20025fe0`, the one other reader.
- **The source expression behind `CCameraKeyFrame::Activate`'s `FocalLength` fallback.** The bytes are
  now read (below) and the number is `−90.0479`; what the programmer wrote to produce a negative focal
  length is not recoverable from the folded constants.

Server-side, in the camera-track system (§6, `vampire.dll`):

- The `sp_endsequences_b`-gated branch at `0x100cc6c1`, where a zero-length segment recursively
  drives the *paired* sub-chain with a 0.1 s lookahead, together with the matching lookahead
  skip-ahead at `0x100cc5af`. Both are map-specific and unreachable from `TrackThink` on any other
  map, so only a trace of `sp_endsequences_b` playing its own chain would establish what they do.

The `FocalLength` clamp is now read out of the PE. It lives in `0x100cb750` — the corpus labels that
function `CCameraTrack::TrackThink`, but its body opens with `CBaseEntity::Activate` and it fills
**slot 113 on both `CCameraTrack` and `CCameraKeyFrame`**, so it is the shared `Activate`, not the
think. The guard is
`focal < 18.0f (_DAT_10453b94)` **or** `focal > 2000.0f (_DAT_104492ac)`, and the replacement is

```
100cb780  dd 05 88 3b 45 10    FLD   qword ptr [0x10453b88]   ; 18.75   (a double)
100cb786  d9 fe                FSIN
100cb788  dc 3d 78 3b 45 10    FDIVR qword ptr [0x10453b78]   ; 8.95    (a double)
100cb78e  d9 9e 60 04 00 00    FSTP  dword ptr [ESI + 0x460]  ; m_fl35mmFocalLength
```

i.e. `8.95 / sin(18.75 rad)` = **`−90.0479f`** — a *negative* focal length. Both operands are 8-byte
doubles; the corpus's `float10` rendering hides that. An authored `FocalLength` outside `[18, 2000]`
therefore poisons the keyframe with `−90.05` rather than falling back to anything usable. `Activate`
also runs `m_flRollDegrees` through `anglemod` (`FUN_1013d650`).
