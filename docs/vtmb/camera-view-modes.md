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
default completion blend, and an explicit restore parameter overrides the latter. `OnReachedKeyframe`,
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
(default string `"0 0 0"`), `+0x20` `OffsetAngles` (default `"0 0 0"`).

`Position` is matched by `_strstr` in this order — Player `0x1`, DialogTarget `0x2`,
**GrappleVictim `0x80000`**, **GrappleAttacker `0x100000`**, Named `0x8` — and **anything unrecognised
falls through to World `0x4`**, which is also the default. `AttachPos`, also `_strstr` and also in
order: Bone `0x200`, Attachment `0x400`, Center `0x20`, EyePosition `0x40`, Top `0x100`, Bottom `0x80`,
**AbsMin `0x800`**, **AbsMax `0x1000`**, default Origin `0x10`. `AttachType` is an exact byte compare
including the NUL — **case-sensitive, not `strstr`** — Follow `0x4000`, FollowNoAngles `0x8000`,
FollowEntAngles `0x10000`, default None `0x2000`. `0x20000` marks a non-zero `OffsetOrigin`; `0x40000`
marks a non-zero `OffsetAngles`.

**`OffsetAngles` is parsed and dead — settling this document's open question.** `FUN_1006f080` only
ever reads `+0x14`, and no cine-camera site anywhere in `vampire.dll` tests `0x40000`. The key is
accepted by the grammar and has no effect on the server. (The client mirrors the same parser at
`client.dll` `FUN_10028a10`; that half is unaudited.)

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
| 3 | `CBaseCineCamUpdate_Mode_FollowEntity` `0x1006fe00` | origin = anchor 0's world centre (vfunc `0x300`), angles = its abs angles; created by `FUN_100705d0`, **no callers** |
| 4 | `CBaseCineCamUpdate_Mode_Animated` `0x1006f870` | pushes only `m_flFOV`; pose is the entity's own animation; created by `FUN_10070690` for `camera_animated` (`CCameraAnimated::StartCamera` `0x10071550`), **zero instances shipped** |
| >4 | `CBaseCineCamCamEndThink` `0x1006e850` | `UTIL_Remove(this)` |

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
| `+0x640` | `m_bDrawPlayer` | copied to the runtime camera |

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

**`m_bDrawPlayer` (`+0x640`) has no server reader.** It is registered as a `DT_BaseCineCam` SendProp by
`FUN_1006d2f0` and written by exactly two functions — `FUN_10070780` (copying the director's keyvalue)
and `CBasePlayer::HandleAnimEvent` `0x10178a10`, where anim event 4050 forces it to 1. It is a pure
replication channel; the drawing decision is the client's (see "The draw gates and the HUD mask").

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

**`CBasePlayer::StartPlayerDialog` `0x10178280`** runs the dialogue-manager admission test, sets the
dialogue partner (`player+0xFE8`), `SetImmobilized(true)`, records whether the active weapon was drawn
(`+0x1e01`) and holsters to `item_w_unarmed`, and then — unless the partner RTTI-casts to a payphone,
which grapples instead — creates the camera from the NPC's `default_camera` keyvalue (`npc+0x64C4`,
`""` when null) with `FUN_10070470(name, NULL, NULL, NULL, NULL)`: **no anchor entities at all**, so
every anchor must come from the shot file's own `Position` keyword. It writes no origin and no angles
for either party, confirming that starting a conversation turns nobody. **`StartPlayerDialog` has no
`DialogDefault` fallback** — a shot name that does not load leaves `cam == NULL` and
`SetCineCamera(NULL)`, and the conversation runs with no camera at all. Only `SetCamera` falls back.

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

**There is no engine-side per-line camera.** `vampire.dll` parses no `.dlg` file — the string does not
occur in the image, and the dialogue tree is read by the Python bridge — and `SetCamera` has exactly
one caller. A per-line shot therefore reaches the engine only as an explicit `SetCamera(actor, "Shot")`
in the dialogue's own `.py` sequence; the camera created at `StartPlayerDialog` persists unchanged for
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

### The client view-composition chain — who wins, and how they compose (2026-09-07)

`0x100a7770` is not the whole priority ladder; it is `C_BasePlayer::CalcView`, vtable slot 187, and it
is two arms long. The ladder is spread over four functions, in this order per rendered frame.

1. **`CViewRender::SetUpView` `0x10191710`** seeds the `CViewSetup` (it lives at `CViewRender + 0x10`;
   `fov` at `+0x28`, `fovViewmodel` `+0x2c`, `origin` `+0x38`, `angles` `+0x50`, `zNear` `+0x5c` =
   `8.0`, `zFar` `+0x60` = `28400.0`, the off-centre rect at `+0x06..0x14`), calls
   `CViewRender::CalcView`, and then calls `g_pClientMode->OverrideView` **only if** a cine camera is
   adopted **or** the player is in third person with no intermission and no view-effect veto. In plain
   first person with no cine camera nothing after `CalcView` touches the view — so the `camera_track`
   override cannot fire in pure first person either, except that a live scripted weight is itself one
   of `CAM_IsThirdPerson`'s disjuncts (§2), which makes the test true.
2. **`CViewRender::CalcView` `0x10191200`** is the ordinary first/third-person view and mentions no
   cine camera: `DriftPitch`, `CalcBob`, eye origin, engine view angles, view shake, water offset,
   `V_CalcRoll` (first person only), the three `scr_ofs*` offsets, punch angles, both viewmodel
   solves, and the Z step-smoother. Its last arm hard-replaces origin and angles with a spectated
   entity's — **there is no separate death, feed or seduction `CalcView` arm**; feed and seduction
   reach the view through the weights of §3, and a scripted feed shot through a `camera_cinematic`.
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
   with one, it calls slot 33 (`FUN_100ffb90`, the track override) **directly**. Then the two dev-cvar
   off-centre fields; then an early return when the live shot's viewmodel predicate passes; then the
   `scr_ofs*` viewmodel adjustment.

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
engine untouched beside `zNear`, `zFar`, an aspect field of `1.0` and the off-centre rect. The cine
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
`dt = engine->GetCurTime() − m_flLastTime (0x490)`, **clamped to 1.0 s at the top and replaced by a
flat 0.01 s whenever it falls below ~1/255 s** (which covers zero and negative), then `m_flLastTime`
and `m_nFrameCache` are restamped. It is a `curtime` difference, not `gpGlobals->frametime`.

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
a dev-cvar guard (`DAT_102de30c`, one referrer, name unrecovered): when it is set the function returns
the cvar's value **without writing `m_flCurFov`**, so the rendered FOV freezes at its previous value
rather than following the cvar.

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

### The `camera_track` override channel — fields, ramp and the server fade machinery (2026-09-07)

**The `CInput` override `FUN_100ffb90` (slot 33), exactly:**

```c
if (0.0f < m_flScriptedWeight /*CInput+0x100*/) {
  e = SimpleSpline(m_flScriptedWeight);                 // FUN_100fdb30 = t*t*(3 − 2t)
  AngleVectors(angles, fwd);
  viewFwdPoint = *origin + fwd * 100.0f;                // 100 Source units along the current forward
  *origin = *origin + (m_vecOverrideOrigin /*+0x17c*/ − *origin) * e;
  dir = (viewFwdPoint + (m_vecOverrideTarget /*+0x188*/ − viewFwdPoint) * e) − *origin;
  VectorAngles(normalize(dir), angles);
  angles->roll = e * m_flOverrideRoll /*+0x194*/;       // the base roll is discarded outright
  *fov = *fov + (m_flOverrideFov /*+0x198*/ − *fov) * e;
}
```

`viewFwdPoint` is a stand-in "what you are looking at" point — the view origin plus **100 Source units
(254 cm) along the current view forward** — so the aim interpolates as a *point* and pitch and yaw fall
out of `VectorAngles`. Roll is not interpolated from the base: it is `e × override.roll`, so the base
view's roll vanishes the instant the weight is non-zero. `CInput+0x17c/0x188/0x194/0x198` are refreshed
once per `CAM_Think` and **only while the weight is already non-zero**, straight off the replicated
player fields.

| server `DT_Local` | server `CBasePlayer+` | client local | client `player+` | field | encoding |
|---|---|---|---|---|---|
| — | — | `+0x34` | `0x168c` | `m_iHideHUD` | |
| — | — | `+0x38` | `0x1690` | `m_iFOV` | |
| — | — | `+0x4c` | `0x16a4` | `m_iViewmodelFOV` | |
| `+0x84` | `0x1ec4` | `+0x88` | `0x16e0` | `m_iCameraOverrideIdx` | int, 11 bits |
| — | — | `+0xd8` | `0x1730` | `m_bDrawViewmodel` | |
| `+0xec` | `0x1f2c` | `+0xf0` | `0x1748` | `m_vecCameraViewOverride` | Vector |
| `+0xf8` | `0x1f38` | `+0xfc` | `0x1754` | `m_vecCameraTargetOverride` | Vector |
| `+0x104` | `0x1f44` | `+0x108` | `0x1760` | `m_flCameraFOVOverride` | float, 10 bits, `[0, 180]` |
| `+0x108` | `0x1f48` | `+0x10c` | `0x1764` | `m_flCameraRollOverride` | float, 12 bits, `[−180, 180]` |
| `+0x10c` | `0x1f4c` | `+0x110` | `0x1768` | `m_flCameraOverrideTimestamp` | SendPropTime |
| `+0x110` | `0x1f50` | `+0x114` | `0x176c` | `m_flCameraOverrideFadeStartTime` | SendPropTime |
| `+0x114` | `0x1f54` | `+0x118` | `0x1770` | `m_flCameraOverrideFadeDuration` | float, 10 bits, **`[−10, +10]`** |
| — | — | `+0x11c…+0x138` | | `m_vecCrossfadeFrom/ToLandmark`, `m_flCrossfadeYawDifference`, `…StartTime`, `…Duration` | |
| — | — | `+0x140` | `0x1798` | `m_flResetCameraDampeningTime` | |

The client's local block is at `player+0x1658` and its offsets run **4 bytes higher than the server's
`DT_Local` offsets throughout** — a fixed skew across every field, so one leading member is not sent.
`m_Local` itself is `CBasePlayer + 0x1e40`. The SendProp registration (`FUN_1018a730`) is the source of
truth for which of `+0x1f44`/`+0x1f48` is FOV and which is roll; the client-side reading of
`SetupVisibility` transcribes them the other way round, and a re-read of `corpus asm 10352120` would
settle it beyond the registration.

**The ramp, `FUN_100fc900`'s tail** (the same driver that advances every `CInput` weight, §3):

```c
if (m_flCameraOverrideFadeStartTime <= 0.0f) { m_flScriptedWeight = 0.0f; return; }
m_flScriptedWeight = 1.0f;
dur = m_flCameraOverrideFadeDuration;
if (dur <= 0.0039f) { if (_DAT_10235278 <= dur) goto clamp;          // ≈0 duration: stay at 1
                      m_flScriptedWeight = 1.0f + ((now − startTime) / dur); }   // dur < 0: blend OUT
else                  m_flScriptedWeight = (now − startTime) / dur;
clamp: m_flScriptedWeight = clamp(m_flScriptedWeight, 0.0f, 1.0f);
```

Three regimes: **`startTime ≤ 0` ⇒ the override is off**; **`0 ≤ duration ≤ 0.0039` ⇒ weight 1
immediately, a hard cut in that stays**; **`duration < 0` ⇒ the weight starts at 1 and decays to 0 over
`|duration|`, the blend out**. The ramp is **linear**; the ease is applied at the point of use by
`SimpleSpline` inside `FUN_100ffb90`. Nothing cancels the override abruptly except the server writing
`m_flCameraOverrideFadeStartTime ≤ 0`.

**Server side, the fade is one signed weight over a per-channel crossfade stack.** The unreplicated
state on `CBasePlayer`: `+0x19b4` the cine camera's EHANDLE, `+0x19b8` `m_flCameraOverrideFadeMarkTime`,
`+0x19bc` the duration **whose sign is its direction** (`> 0` fade in, `< 0` fade out), `+0x19c0/c4/c8`
the view entity, its set time and its crossfade duration, `+0x19cc/d0/d4` the same for the target
entity, and `+0x19d8`/`+0x19e4` a `CUtlVector` of fade-out entries with stride `0x10` —
`{ byte kind (0 view, 1 target), EHANDLE, setTime, crossfadeDuration }`.

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
  list **newest first**, pulling the published value back toward each older camera by that camera's own
  progress and accumulating coverage multiplicatively (`w = 1 − (1 − f)(1 − w)`), dropping entries
  whose channel has reached full coverage or whose entity has died. **N cameras can be crossfading at
  once**, on top of the one global signed fade weight, and no duration on this path is ever a constant
  — every value is the caller's argument raised to whatever minimum the entity demands. Finally it adds
  the published view origin to the PVS; separately, an active cine camera **replaces** the PVS with its
  own `m_vecCamOrigin` and returns, skipping the ordinary visibility pass entirely.

The decompile of the per-entry lerp confuses its factor register (it prints `curtime` where the listing
multiplies by the entry's fraction); the shape is certain, the exact register for each of the three
components is not. `corpus asm 10352120` is the source of truth before that stack is reproduced.

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
retail input on this path with no source in the port's shot record.

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
| unsettled angular deadband **1.0°** | `SettleAngle 0.05f` | unintended — the port parks 20× tighter on the acquire |
| speed floor **1.0 u/s** while unsettled | `clamp(speed, 0, MoveSpeed)` | unintended |
| `SnapOnShotChange` is a **one-shot** flag consumed by `FUN_10002390` | applied every frame (`Shot.bSnapOnShotChange \|\| MoveSpeed <= 0`) | unintended — retail snaps on the change and then tracks |
| `RemainingTime` carries both defects above (the NaN band) | `RemainingTranslationSeconds` is a correct kinematic solve | **undecided** — the NaN band is unreachable on shipped content, and the trapezoid defect is what shipped shots are tuned against (`jack.txt` closing 100 u from rest: retail 0.17 s, the correct solve 1.27 s — the port pans ~7× slower). The owner call is M6 in `docs/project/plans/spine.md` §11.13i: reproduce retail's arithmetic with the radicand clamped at zero |
| `MoveAccel == 0` divides by zero, pinning the speed at the 1.0 floor | an explicit `Speed = MoveSpeed` arm | **named modernization**; the parse default is 50, so no shipped shot reaches it |
| every scripted-shot exit is a **hard cut** (`EndShot`, `EndPlayerDialog`, the interaction closers, `CamMode → 0`, adoption cleared, entity removed) | `FElysiumCameraShotStack` ramps the cine channel **out** over `RampSeconds` | **named modernization** — VtMB's cuts are jarring; declared here, and the authored `MoveTime ≤ 0.05` cut fold of §6 is still reproduced exactly |
| the scripted weight is **eased at the point of use**, `e = SimpleSpline(w)` inside `FUN_100ffb90` | `ElysiumCam::ComposeScriptedShot` receives the raw linear weight (the ease is applied to `Third` and `Feed` only) | unintended |
| composition lerps the **origin and a look-at point** (`origin + fwd*100` → the override target) and re-derives angles with `VectorAngles` | `FMath::Lerp` over `FRotator`s | unintended — for a large angular delta a point lerp swings faster at the start and settles, an angle lerp is uniform |
| `angles.roll = e * shotRoll`, discarding the base roll | roll lerps as part of the rotator | unintended |
| `duration < 0` **is** the blend-out encoding | `Pop(Id, BlendOutSeconds)` — the same behaviour, a different encoding | equivalent |
| the body is drawn iff the replicated `m_bDrawPlayer` says so, short-circuiting `CAM_IsThirdPerson` | `bBodyEligible = bThirdPerson`; no `bDrawPlayer` on `FElysiumShotPresentation` | unintended — a shot cannot ask for the body |
| the viewmodel gate is `DrawViewmodel && speed ≤ 1.0` | `!bThirdPerson && (!bNamed \|\| bDrawViewmodel)` — no speed term | unintended — the hands appear during the dolly |
| `dt` is latched once per rendered frame, clamped to 1.0 s, floored at 0.01 s below ~1/255 s | the engine `DeltaSeconds` passes straight through | low-risk under Unreal's own clamp; the 0.01 s floor is real retail behaviour on a stalled frame |
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

- **`FUN_1013c940`**, the closest-point-on-line kernel behind `FUN_1013ca00`, was not opened. The
  `AutoPositionFromTarget` formula assumes it is the standard clamped projection; if retail clamps `t`
  to `[0,1]` the pull-back changes when the target point projects behind the camera.
- **The per-entry lerp factors in `CHL2_Player::SetupVisibility`** `0x10352120` — decompiler register
  confusion over the three components. Reading `corpus asm 10352120` settles it.
- **The camera-track entity's `vfunc0xBC` / `0xC0` / `0xC4` / `0xC8` / `0xCC` / `0xD0` / `0xD4`** are
  identified only by use (notify / roll / FOV / origin / target-from / minimum crossfade in / minimum
  crossfade out); `camera_track`'s and `camera_keyframe`'s own factories `FUN_100cb910` /
  `FUN_100cb580` were not opened. `CBaseCombatCharacter::SetAsCameraTarget` (`0x1000a2d6`) and
  `GetCameraFadeOutTime` (`0x10332240`) carry an authored fade time and have **zero recovered
  callers**, so the wire from an authored `camera_track` fade to `m_flCameraOverrideFadeMarkTime` is
  one hop short.
- **`FUN_10325da0`**, the NPC head-turn feasibility gate that decides whether `DialogPOV` actually
  redirects the gaze.
- **`FUN_10178120` / `FUN_100e05f0`**, the dialogue manager and its "can this conversation start"
  test. The claim "no per-line camera reaches the engine" rests on `SetCamera` having exactly one
  caller and on `vampire.dll` containing no `.dlg` string — strong, but not a read of the Python
  bridge. **What would close it:** a read of the bridge's dialogue-advance path.
- **`FUN_1006e8e0`'s `+0x564` / `+0x570` / `+0x588` triple** (a saved local origin/angles pair, written
  only when `+0x564` is already non-zero) is understood mechanically; its purpose — presumably
  "remember where an End-only shot started dollying from across a re-shot" — is inferred, and nothing
  else in the image reads `+0x570`/`+0x588`.
- **`player+0x1d60`**, the bitfield `EndShot` clears bits `0x1` and `0x8` of, was not chased to its
  readers.
- **`CBaseCineCam::vfunc5 / 80 / 81 / 82 / 86 / 103 / 117 / 123`**, including its `KeyValue` handler.
  The keyvalue *names* for `+0x5d4`, `+0x5d8`–`+0x5e4`, `+0x5e8` and `+0x640` are therefore inferred
  from the port's vocabulary rather than read out of the datamap; opening the handler settles them.
  (`camera_showdebug` is a cvar in the same file.)
- **The client half of the anchor parser**, `client.dll` `FUN_10028a10`, which mirrors the server's
  `FUN_10071e00`. Only the server half was audited.
- **`_DAT_101e34f4`** and the **name** of the ConVar `DAT_102de30c` in `FUN_10001c20`'s cine-FOV guard
  (one referrer, no recovered constructor).
- **`<DAT_104a57e4>->vfunc40()`**, the base FOV `SetUpView` seeds the view setup with, and `vfunc38` /
  `vfunc39` on the same object; its class is unnamed in the corpus.
- **The intent behind `FUN_100010e0(a,b) = sqrt(2a − b)` and its `0.5*(d − R2)` argument.** The bytes
  are unambiguous; the expression they came from is not.
- **`_DAT_10235278`**, the near-zero guard in the ramp's negative-duration branch: its sign follows
  from the branch structure, its value is unread.

Server-side, in the camera-track system (§6, `vampire.dll`):

- The `sp_endsequences_b`-gated branch at `0x100cc6c1`, where a zero-length segment recursively
  drives the *paired* sub-chain with a 0.1 s lookahead, together with the matching lookahead
  skip-ahead at `0x100cc5af`. Both are map-specific and unreachable from `TrackThink` on any other
  map, so only a trace of `sp_endsequences_b` playing its own chain would establish what they do.
- The default `FocalLength` that `Activate` clamps an out-of-range authored value to. It has the
  form `18 / sin k`, from constants `0x10453b78` / `0x10453b88`; reading those two floats out of the
  PE settles the number.
