# The scripted-camera subsystem — `CBaseCineCam`, `C_BaseCineCamera` and the override channel

> Working note, owned by `docs/project/plans/spine.md` §11.13. It carries **no task status** —
> `docs/project/roadmap.md` owns that — and it is not a facts document: every recovered fact below is
> written into `docs/vtmb/camera-view-modes.md`, `docs/vtmb/retail-defects.md`,
> `docs/vtmb/animation_events.md` or `docs/vtmb/computer-terminals.md` as a deliverable of the slice
> that consumes it. It exists to close the design of the whole subsystem before code.
>
> The raw decompiles it rests on are under `$ELYSIUM_WORK_ROOT/_camera_recovery/` —
> [`server_cine_camera.md`](file:///E:/elysium-work/_camera_recovery/server_cine_camera.md) (the
> `vampire.dll` half), [`client_cine_camera.md`](file:///E:/elysium-work/_camera_recovery/client_cine_camera.md)
> (the `client.dll` half) and
> [`plan_gap_ledger.md`](file:///E:/elysium-work/_camera_recovery/plan_gap_ledger.md) (the
> per-behaviour ledger, the corpus sweep and the first draft of the modernization list). Every
> `file:line` in this note was re-verified against **HEAD `26a33d75`**, which contains the
> `bTracked` split; where a citation drifted from the ledger, this note carries the corrected one and
> says so.

---

## 0. Scope

**The deliverable is VtMB's scripted-camera subsystem, entire**, not a tutorial beat. The subsystem is
six things that retail runs as one:

1. **`camera_cinematic` / `CBaseCineCam`** (`vampire.dll`) — the map entity, the director/runtime
   split, `SetShot`, the anchor resolve, the five `CamMode` arms, the 24 Hz think,
   `AutoPositionFromTarget`, `point_player`, `FindBestShot`, the immobilize pair and the same-tick
   teardown.
2. **`C_BaseCineCamera`** (`client.dll`) — `OnDataChanged`, shot start's two arms, the frame-latched
   tracker (position, angles, turn rate, FOV), the one-shot snap, and the three hard-cut exits.
3. **The `CInput` override channel** — `FUN_100ffb90`, the linear ramp with the ease at the point of
   use, the signed-duration fade encoding, and the server-side fade machinery
   (`GetCameraOverrideWeight`, the back-dating re-time, the per-entity minimum crossfades and the
   N-entry crossfade stack composed in `CHL2_Player::SetupVisibility`).
4. **The view-composition chain** — `CViewRender::SetUpView` → `CalcView` →
   `ClientModeVampire::OverrideView` → `C_BasePlayer::CalcView` → `ClientModeShared::OverrideView`;
   who wins, what is skipped, and the single FOV scalar all three paths write.
5. **The drivers** — the entity input (`StartShot`/`EndShot`), the script native (`SetCamera`, 115
   shipped call sites), the dialogue opener (`StartPlayerDialog` / `EndPlayerDialog`), the anim-event
   channel (4050/4051 → `FindBestShot`), the interaction shots (`CFuncMonitor`, `CPropHacking`,
   `CPropKeypad`, `Intrusion`) and the death shot.
6. **The presentation gates** — `ShouldDrawLocalPlayer`'s `m_bDrawPlayer` short-circuit,
   `ShouldHideViewModel`'s speed-gated `DrawViewmodel` predicate, and the edge-triggered
   `HideHud(0xa06d)` mask.

`sp_tutorial_1` is **one acceptance witness among five** (§9): sp_theatre, a dialogue map,
`stealth_kill` and the Hacking/Intrusion terminals are the others, and each proves parts of the
subsystem the tutorial never touches. The slices below are ordered by **dependency**, not by which
one the tutorial makes visible.

**The owner's framework, ruled 2026-09-07.** The port is a **VM host** for VtMB's authored content:
every *transition* — adopt, re-shot, snap, end, immobilize / mobilize, the HUD and draw edges, and
which camera is live at any instant — is contract and is reproduced, and a modernization is
admissible only when it is **pixel-only and Unreal already has the algorithm**. §7 records the
ruling on every item under that framework; five of the earlier recommendations reverse under it.

**There is no deferral in this plan.** Every retail behaviour recovered by the two passes is either
(a) ported by a named slice, (b) a **ruled** item in §7 with its rationale, or (c) a **recovery
task** in §8 that opens the named function,
lands the answer into `docs/vtmb/`, and unblocks the slice that consumes it. Nothing is a seam that
answers "nothing", and nothing is skipped because it is unreachable on shipped content — §4 rules on
each unreachable behaviour explicitly, and §5's ledger maps every remaining row to the slice that
owns it.

---

## 1. Corrections this note carries into the record

Three facts changed while the plan was written and every downstream row depends on them.

| # | What changed | Evidence |
|---|---|---|
| C1 | **The `+0x19d8` crossfade-list pusher and the `m_flCameraOverrideFadeMarkTime`/`Duration` writers are *not* unrecovered.** The client report listed both as open because it had not read the server half. `FUN_1017d280` (set view entity) is the kind-0 pusher — `push_front(fadeList, { 0, +0x19c0, +0x19c4, +0x19c8 })` — and `FUN_1017d0b0` / `FUN_1017d6d0` / `FUN_1017d900` are the three writers of `+0x19b8`/`+0x19bc`. What remains open is only the **kind-1 (target) setter**, the twin of `FUN_1017d280`, and `CBaseCombatCharacter::SetAsCameraTarget`'s callers → **RC7** | `server_cine_camera.md` §7 vs `client_cine_camera.md` §4 |
| C2 | **`special-case.txt` carries five shots, not one**, and both earlier notes conflated their rate pairs. The file authors `DeathCam` (MoveSpeed 25, MoveAccel defaulted 50, `Position Player` / `Origin` / `None` / `[0,0,100]`, `Target Point1 Named/Center/Follow` with the comment *"the game will set this to the corpse"*), `Follow` (a `Start`-only test shot, `Named`/`Center`/**`AttachType None`** — the one latching anchor in the whole corpus), **`Animated`** (`Start` `Named` / `Bone: cam_bone` / `Follow`, commented *"special case shot info used by camera_animated entity. Don't change this."*), `Intrusion` (300/450) and `Hacking` (300/250). The ledger's "`special-case` 25/450" and the doc's "`special-case.txt` 300/450" are both a conflation; the NaN-band conclusion survives — no shot in the file has `MoveSpeed > 2·MoveAccel` | `$ELYSIUM_WORK_ROOT/exports/vdata/camerashots/special-case.txt` |
| C3 | **The death camera is a scripted shot, and `camera_animated` is authored content.** `DeathCam` names the corpse through a `Named` anchor the game supplies, which is `SetShotAnchorEntity`, which is a driver nobody had listed; the `Animated` shot's `Bone: cam_bone` is what `CamMode 4` samples. Neither string occurs anywhere else in the exported corpus (`.py` or `.txt`), so both callers are engine-side and unopened → **RC12** | same sweep, plus a corpus-wide `grep -ril DeathCam` returning only the shot file |

The directory holds **66 files: 65 shots plus Troika's own `camera shots how-to.txt`**. Where this
note says "all 66 files" it means the grammar test's input set, the how-to included, because the
how-to is the grammar's own specification and a keyword it names must have a resolver too.

---

## 2. The port today, in one page

| Retail piece | Port today | File |
|---|---|---|
| the shot-file grammar and record defaults | ported, asserted | `Source/ElysiumUE/Private/Player/ElysiumCameraShots.{h,cpp}` |
| the mode-1 client tracker | ported, four measured numeric divergences | `Private/Player/ElysiumCameraSolve.cpp:288-460`, `Public/ElysiumCameraSolve.h` |
| `CamMode == 1` | ported as one bool, `FElysiumCameraShot::bTracked` | `Public/ElysiumCameraSolve.h:273`; writers `ElysiumCameraShots.cpp:458`, `ElysiumDialogueCamera.cpp:161`; sole reader `ElysiumCameraSolve.cpp:387` |
| the composition | one weighted stack for both channels | `Private/Player/ElysiumCameraComponent.cpp:335-375`, `Private/Player/ElysiumCameraService.cpp:223-242` |
| `camera_track` / `camera_keyframe` | ported whole (schedule, fold, Catmull, two clocks, leases) | `Private/Substrate/ElysiumCameraTrack.{h,cpp}` |
| the adoption slot | a one-slot model that replaces rather than stacks | `Private/Substrate/ElysiumEntityWorld.cpp:997-1023` |
| `camera_cinematic` | **a stub row** | `Private/Substrate/ElysiumStubClasses.cpp:46-47` (`"EndShot StartShot"`), dispatched at `:120` |
| `SetCamera` native | ported, no `DialogDefault` fallback | `Private/Scripting/ElysiumScriptNatives.cpp:61`, `:415`, `:419` |
| `default_camera` opener | ported | `Private/Substrate/ElysiumEntityWorldDialogue.cpp:521-568`; field `Private/Substrate/ElysiumNpc.h:46`; registered `ElysiumNpcClasses.cpp:129` |
| `DialogPOV` | ported with its own aim resolver, no feasibility gate | `Public/ElysiumDialogueCamera.h:35-40`, `ElysiumEntityWorldDialogue.cpp:706-733`, `Public/ElysiumEntityWorld.h:458`, `Public/ElysiumPlayer.h:1604/1609` |
| immobilize | the mechanism exists; **no camera path calls it** | `Public/ElysiumPlayer.h:1812/1815/1817`; used at `ElysiumTerminal.cpp:637/736`, `ElysiumEventClasses.cpp:89` |
| `FindBestShot`, 4050/4051, `point_player`, `m_bDrawPlayer`, `AbsMin`/`AbsMax`, `GrappleVictim`/`GrappleAttacker`, `DialogDefault` fallback | **zero hits under `Source/ElysiumUE`** | grep sweep at `26a33d75` |

**The full per-behaviour ledger — one row per recovered retail fact, its HEAD-verified port status,
its disposition and the slice that owns it — is §5.** The table above is the summary.

Existing test surface (the naming convention new tests follow):
`Elysium.Substrate.Camera` · `.CameraDraw` · `.CameraRig` · `.CameraTrack` · `.CameraShots` ·
`.ViewState` (all `Private/Tests/ElysiumCameraTests.cpp`);
`Elysium.Substrate.DialogueCamera.{Session,Registry,BodyOwnerLifecycle,Grammar,DialogPOV,JackBasis}`
(`Private/Tests/ElysiumDialogueCameraTests.cpp`); `Elysium.Content.DialogueCameraDemand`,
`Elysium.Content.OpeningCameraTracks`.

---

## 3. Design constraints the whole plan obeys

**The framework.** The port is a **VM host** for VtMB's authored content (§7). Every *transition* —
adopt, re-shot, snap, end, immobilize / mobilize, the HUD and draw edges, and which camera is live at
any instant — is contract and is reproduced. A modernization is admissible only when it is
**pixel-only and Unreal already has the algorithm**.

**The per-frame order is itself contract.** The subsystem is **one ordered pass**, and its order is
retail's, not an implementation detail:

> substrate goal publish (the 24 Hz think, M2) → shot-change edges (reset-frame / shot-index,
> `OnDataChanged`) → the one-shot snap → the `CamMode` branch → compose (a cine shot hard-writes;
> the third-person boom is skipped) → the `CInput` override → the draw gates.

Every slice lands inside that order; none of them re-orders it, and a test that asserts a transition
asserts the frame it happens on. Retail's own two-clock split is preserved with it: the goal advances
on the substrate's 24 Hz accumulator, the tracker advances once per **rendered** frame.

From `.claude/rules/cpp.md` and `docs/architecture/runtime-architecture.md`:

- **One primary class per `.h`/`.cpp`**, under its owning layer folder. The `camera_cinematic` entity
  is a new `Private/Substrate/ElysiumCameraCinematic.{h,cpp}` — it never lands inside
  `ElysiumStubClasses.cpp` or `ElysiumEntityWorld.cpp`. Where a slice substantially touches a class
  in an oversized multi-class file, the verbatim move lands as its own commit ahead of the
  behavioural edit.
- **Below `Private/Substrate/` nothing reaches `GetWorld()`, `GEngine`, `GWorld` or
  `GetFirstPlayerController`.** The world is reached through `FElysiumWorldServices`. The trace the
  `FindBestShot` visibility predicate needs (SC8) is a world service, not a direct `LineTraceSingle`.
- **`DeltaTime` arrives as a parameter.** SC1's 1.0 s ceiling and 0.01 s floor are applied *inside*
  `FElysiumScriptedShotTracker::Advance`, and SC4's `_DAT_1044eb04` think accumulator is advanced by
  the `DeltaTime` its caller hands it — neither ever reads a clock.
- **Randomness is a named `ElysiumRng::Stream`**, never `FMath::Rand*`. SC8's uniform pick over the
  passing candidate set draws from one.
- **No arithmetic is left to the FPU where retail's answer is a branch.** M7's `MoveAccel == 0` is an
  explicit decel-arm branch, never a real divide; M6's radicand is clamped rather than allowed to
  produce a NaN.
- **Logic in C++; a graph carries composition, data and cosmetics only**, so every assertion below
  has a C++ entry point.
- The plain-C++ substrate stays reflection-free; `UPROPERTY`/`UCLASS` appear only where the engine
  must see the type.

---

## 4. Behaviours unreachable on shipped content — each ported or ruled

The project rule is *reproduce every recovered behaviour*. "No shipped file does this" is never a
reason to skip; it is only ever a reason a divergence is safe. Each row below states its disposition.

| Behaviour | Retail | Disposition | Slice |
|---|---|---|---|
| **`Point2` without `Point1`** — the parser raises `1 << (count + 2)` by *order of presence* while writing into the fixed slot, so the look-at solve reads the empty slot 2 and the shot aims at `(0,0,0)` | `FUN_100721e0`; `retail-defects.md` §7 says *reproduce* | **Ported as-is.** The parser carries an explicit `TargetPointCount` and raises the presence flag by count, exactly as retail. A test authors `Point2` alone and asserts the shot aims at the world origin | SC6 |
| **The `RemainingTime` NaN band** — the triangle arm's radicand `2a − 0.5(d − R2)` goes negative whenever `d − R2 > 4a`, needing `MoveSpeed > 2·MoveAccel`; no shipped shot reaches it | `FUN_100010f0` | **M6 ruled 2026-09-07.** Retail's arithmetic reproduced verbatim, radicand clamped at 0 — the clamp removes **only the NaN state**, which is unreachable on shipped content. The trapezoid defect is reproduced *unclamped* because shipped shots are tuned against it | SC1 |
| **`MoveAccel == 0`** — `stopDist = v²/(2·0)`; retail's `0/0` compare is **false**, so control takes the decel arm, `Approach(v, 0, 0, dt)` leaves the speed unchanged and `clamp(v, 1.0, MoveSpeed)` pins it at 1 u/s: the camera crawls. The parse default is 50 and no shipped file writes `MoveAccel`, `TurnAccel` or `MoveSpeed` as 0 | `FUN_10001fe0` + `FUN_100010b0` | **Ported as-is — M7 ruled 2026-09-07.** The crawl is reproduced through an **explicit decel branch, never a real divide**; the port's `Speed = MoveSpeed` arm is removed | SC1 |
| **`AttachType None` on a `Start` block** — `FUN_1006f010` latches all four anchors at the shot-start cache; and, through retail's own bug, it reads *anchor 0's* flags for every index, so any shot without a `Start` re-resolves everything. Exactly one shipped shot latches: `special-case.txt`'s `Follow` | `FUN_1006f010`, `FUN_1006e8e0`; `retail-defects.md` §7 | **Ported as-is, bug included.** The latch is real and the anchor-0 test is what decides which shots latch, so reproducing one without the other would change 55 shots | SC6 |
| **`AbsMin` / `AbsMax`** (`0x800`/`0x1000`) — whole world-space surrounding-bounds corners; shipped once each in `centerfullview.txt`, absent from the how-to | `FUN_10071e00`, `FUN_1006f080` | **Ported as-is** | SC6 |
| **`CamMode` 2 (`OnRails`)** — the think is an empty function on both server and client | `FUN_1006fde0`, `FUN_10002200` | **Ported as-is**: mode 2 publishes nothing, never expires, and the client copies the replicated pose through. It is a real arm of the jump table and the enum must carry it | SC4 |
| **`CamMode` 3 (`FollowEntity`)** — origin = anchor 0's world centre (vfunc `0x300`), angles = its abs angles; zeroes the expiry; created by `FUN_100705d0`, no in-image callers | `FUN_1006fe00` | **Ported as-is** after **RC12** reads the body in full. "No caller" is a fact about shipped content, not about the arm | SC4 |
| **`CamMode` 4 (`Animated`) and `camera_animated`** — pushes only `m_flFOV`; the pose is the entity's own animation; created by `FUN_10070690` from `CCameraAnimated::StartCamera` `0x10071550`; the client update `FUN_10002200` is confirmed empty (`RET`); zero shipped entity instances — **but `special-case.txt` authors the `Animated` shot for it, `Bone: cam_bone`, with a "don't change this" comment** | `FUN_1006f870`, `0x10071550` | **Ported as-is** after **RC12**. The authored shot is the evidence the arm is content-facing | SC4 |
| **The `DeathCam` shot** — `Position Player` / `Origin` / `None` / `[0,0,100]` with a `Named` `Center` `Follow` target the game fills with the corpse; MoveSpeed 25 | `special-case.txt`; the filler is unrecovered | **Ported as-is**; its driver is **RC12**'s second question. Until RC12 names the caller, the shot is reachable through `SetShotAnchorEntity` from the death path, which is the mechanism regardless of which function retail uses | SC4 |
| **The vehicle arm** in `C_BasePlayer::CalcView` — runs before the cine arm, so the cine camera beats a vehicle view | `0x100a7770` | **Ruled dead — M10, 2026-09-07.** VtMB ships no drivable vehicle and the arm's inputs (`m_bInVehicle`, `field_0x19c4`) have no writer in the image, so there is no transition to reproduce. The *ordering* fact is preserved as a comment at the compose site; no vehicle channel is built | SC2 |
| **`bOffCenter`** — `ClientModeShared::OverrideView` sets the view setup's off-centre rect from two dev cvars: `offCenter[0..3] = (−x·0.5, −y·0.5, x·0.5, y·0.5)` | `0x100d4040` | **Ported as-is — M11 ruled 2026-09-07, the names only.** Unreal expresses this as `FMinimalViewInfo::OffCenterProjectionOffset`; the two cvars land as `elysium.CameraOffCenterX` / `…Y` because retail's own names are unrecovered (**RC9**). If RC9 recovers them, the retail names win | SC2 |
| **The cine-FOV dev-cvar guard** — when `DAT_102de30c` is set, `FUN_10001c20` returns its value **without writing `m_flCurFov`**, so the rendered FOV freezes at its previous value rather than following the cvar | `FUN_10001c20`, threshold `_DAT_101e34f4` | **Ported as-is including the freeze — M12 ruled 2026-09-07, the name only.** `elysium.CameraShotFovOverride` until **RC9** recovers the retail name and the threshold | SC1 |
| **Replication quantisation** — FOV 10 bits `[0,180]`, roll 12 bits `[−180,180]`, fade duration 10 bits **`[−10,+10]`**, `m_iCameraOverrideIdx` 11 bits | `FUN_1018a730` | **Split — M4 ruled 2026-09-07.** The **bit widths** are skipped (pixel-only, no wire behind them); the **encoder clamps are contract and are kept** — FOV `[0,180]`, roll `[−180,180]`, fade duration `±10 s`, warning when one bites. Corpus: max `FromPlayerTime` 1.0, `ToPlayerTime` 0, so it never bites | SC3 |
| **`AddOriginToPVS` / `ResetPVS`** — an active cine camera *replaces* the PVS with `m_vecCamOrigin` and skips the ordinary visibility pass entirely | `0x10352120` | **Ruled not applicable — M13, 2026-09-07.** Unreal culls from the **actual view**, which is what retail approximated by hand-moving a PVS origin: the algorithm is native and the difference is pixel-only. The behavioural consequence — entities near the player but far from the lens stop receiving updates during a retail shot — is recorded as a retail artefact and deliberately **not** reproduced | SC3 |
| **`m_flResetCameraDampeningTime`** (`DT_Local +0x13c`) | written only by `CPointTeleport::vfunc113` | **Ruled out of this subsystem.** It is a teleport affordance, not a camera-restore blend; recorded in `camera-view-modes.md` so a future reader does not mistake it for one | — |

---

## 5. The gap ledger

One row per recovered retail fact, from `plan_gap_ledger.md` §A, **re-verified against HEAD
`26a33d75`**. Every `file:line` below was read at HEAD; where the ledger's citation drifted, this
table carries the corrected one and the note says so. Paths are relative to `Source/ElysiumUE/`.

Disposition key: **P** port as-is · **M<n>** port with the named modernization of §7 ·
**D** a retail defect, reproduced deliberately · **R** blocked on the §8 recovery task named in the
slice column. Nothing here is a seam: an **R** row names the task that answers it and the slice that
then ports it.

### A1 — `InputStartShot` / `InputEndShot` and adoption *(server report §1)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 1.1 | `InputStartShot` `0x10070720` ignores `inputdata`; the activator is always `UTIL_PlayerByIndex(1)` | absent — a stub row, `Private/Substrate/ElysiumStubClasses.cpp:46-47` (`"EndShot StartShot"`), dispatched to `StubInput` at `:120` | **P** | SC4 |
| 1.2 | `FUN_10070780(director, player)`: the map entity is the **director** and holds the keyvalues; the runtime camera is a **second** `camera_cinematic` | absent — no director/runtime split; `FElysiumCameraDirector` (`Private/Player/ElysiumCameraShots.h:173`) is a per-embodiment list, not an entity | **P** | SC4 |
| 1.3 | Director fields `+0x5d4` `shotname`/`m_sShotName`, `+0x5d8` `startent`, `+0x5dc` `endent`, `+0x5e0` `target1`, `+0x5e4` `target2`, `+0x5e8` `point_player`/`m_bForcePlayerLook`, `+0x640` `m_bDrawPlayer`; key names **read** at `10546b*`–`10546dc8` | absent | **P** | SC4 |
| 1.4 | Four `FindEntityByName(NULL, name, 0, 0)` lookups, `&DAT_106b8540` (empty string) for a null keyvalue; a miss leaves the anchor to the shot file's `Position` | absent — `Resolve` has no external anchor-override path at all (`ElysiumCameraShots.cpp:428-479`) | **P** | SC4 |
| 1.5 | `FUN_10070470(shot, e0..e3)` — create, `+0x204 \|= 0x4`, `SetShot(name, 1, NULL)`, `SetShotAnchorEntity` ×4, `FUN_1006e8e0`; a failed `SetShot` is the **only** failure path and `UTIL_Remove`s the camera | partial — "the shot did not load ⇒ no camera, run on" is reproduced for the terminal (`Private/Substrate/ElysiumTerminal.cpp:652-663`) and for the director (`ElysiumCameraShots.cpp:490-494` returns 0) | **P** | SC4 |
| 1.6 | The re-shot branch `SetShot(name, 1, player)` **ignores the return value**: a bad name leaves `CamMode 0` / `m_ShotIndex -1` and the camera goes idle rather than falling back | absent | **P** | SC4 |
| 1.7 | `+0x204 & 0x4` marks a camera *disposable*; `FUN_1017cef0` removes the outgoing camera only when it carries the flag, so a map-placed director survives its own `StartShot` | absent | **P** | SC4 |
| 1.8 | `FUN_1017cef0` is the single adoption slot: `+0x1ec4 = IndexOfEdict(cam)`, `+0x19b4 = cam handle`; null clears both | partial — a one-slot model exists (`ElysiumEntityWorld.cpp:997-1023`, "replaces the first rather than stacking" at `:1004-1005`), but the terminal stacks a separate handle beside it (`ElysiumTerminal.h:344-347`, corrected from the ledger's `:345-346`; pushed at `.cpp:644-651`), which retail does not | **P** + **M8** | SC4 |
| 1.9 | `FUN_1015ef40(player)` — `SetImmobilized(true)` on `StartShot`; `m_bIsImmobilized` at `player+0x19f7`, read by `SetupMove` `0x10186120`, `CheckJumpButton` `0x101226b0`, `Duck` `0x10126fd0`, `CBaseCombatWeapon::ItemPostFrame` `0x10253ea0`, `CWeaponMelee::ItemPostFrame` `0x103eaec0` | the mechanism exists (`Public/ElysiumPlayer.h:1812/1815/1817`, used at `ElysiumTerminal.cpp:637`/`:736` and `ElysiumEventClasses.cpp:89`) but **no camera path calls it** | **P** | SC4 |
| 1.10 | `InputEndShot` `0x10070750` → `FUN_10070990`: clear the director's mode, `ThinkSet(NULL)`, `SetCineCamera(NULL)`, `SetImmobilized(false)`, `player->+0x1d60 &= ~0x1`, `&= ~0x8`, `UTIL_Remove(this)` when disposable | absent | **P**, **R** (`+0x1d60`'s readers) | SC4, RC4 |
| 1.11 | **There is no blend on `EndShot`.** The entity dies the same tick; the client's next frame finds `m_iCameraOverrideIdx == 0` and falls back to the eye | **diverges** — `Pop` ramps over `RampSeconds` (`ElysiumCameraSolve.cpp:502-520`, `:535-551`); `Resolve` invents `BlendSeconds = 0.5` for a non-snap shot (`ElysiumCameraShots.cpp:469`) | **P** — M1 ruled: reproduce the cut, no cvar | SC2, SC4 |
| 1.12 | `m_bDrawPlayer` `+0x640` is a `DT_BaseCineCam` SendProp (`FUN_1006d2f0`), written only by `FUN_10070780` and anim event **4050**; **no server function reads it** | absent from the shot record — `FElysiumShotPresentation` (`Public/ElysiumCameraSolve.h:161-174`) has `bShowHud` and `bDrawViewmodel` and no body field | **P** | SC4 (field), SC5 (gate), SC8 (writer) |

### A2 — The mode-1 think *(server report §2)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 2.1 | The server cine camera thinks at **24 Hz** — `_DAT_1044eb04 = 0.04165999963879585`, re-applied by `FUN_1006f7d0`, `FUN_1006e770`, `FUN_1006e850`; no cvar or keyvalue touches it, and the client tracker runs per rendered frame against that stepped goal | **diverges** — `FElysiumCameraDirector::Tick` re-resolves every frame (`ElysiumCameraShots.cpp:604-629`) | **P** — M2 ruled: reproduce the 24 Hz publish, keep the two-clock split | SC4 |
| 2.2 | `+0x55c` is the shot expiry; writers are the **mode-4** think (with `0.0`) and mode 3 (zeroing it). **A mode-1 shot never expires on its own** | equivalent — the port's shots live until popped | **P** (nothing to do) | — |
| 2.3 | `+0x594` is an **origin-source selector**: `0` drive from anchor 1 (`End`), `1` from anchor 0 (`Start`), `2` leave the entity's abs origin alone *and skip `AutoPositionFromTarget`*. Only `FUN_1006e8e0` writes it | partial — `Resolve` prefers `End` then `Start` (`ElysiumCameraShots.cpp:435-439`), reproducing arms `0`/`1`; arm `2` has no counterpart | **P** | SC5 |
| 2.4 | `if (rec->+0xd4 > 0)` gates whether the server derives `m_angCamAngles` from the look-at. `+0xd4` is the **count of `Target` sub-blocks** (0/1/2), not a key | partial by accident — `Out.bUseLookAt = bHasTarget` (`ElysiumCameraShots.cpp:453`) is the same gate from block presence rather than a count | **P** — make the count explicit so 2.5 is reproducible | SC5, SC6 |
| 2.5 | **Retail bug:** the parser raises `1 << (count + 2)` — the flag follows *order of presence* while the data goes into the fixed slot. `Point2` alone ⇒ a shot aiming at `(0,0,0)`. No shipped file does this | not reproduced (no flag dword; blocks are read by name) | **D** — reproduce (`retail-defects.md` §7 says so) | SC6 |
| 2.6 | The angle is derived from `GetOrigin()` (vfunc `0x370`, the **local** origin), so `m_angCamAngles = VectorAngles(lookAt − shotStartOrigin)` while `m_vecCamOrigin` tracks live. Only reaches the screen for `CamMode != 1` | not applicable — no replicated angle channel; mode 1 re-derives client-side, which the port does | **P** (nothing to do) | — |
| 2.7 | Look-at `FUN_1006f670`: both points ⇒ midpoint (`×0.5`), one ⇒ that point, none ⇒ `vec3_origin` | ported — `ElysiumCameraShots.cpp:442-448` | **P** (landed) | — |
| 2.8 | `AutoPositionFromTarget` (`flags & 0x20`): swap so `P1` is the higher Z, `C = ClosestPointOnLine(P2, lookAt, camOrigin)`, `d = |C − P2|`, `A = FOV·0.5·DEG2RAD`, `h = d/sin A`, `r = √(h²+d²)`, `camOrigin = lookAt − normalize(lookAt − camOrigin)·r` | **parsed and never read** — field `ElysiumCameraShots.h:99`, parse `.cpp:104`; the only readers are tests (`Private/Tests/ElysiumCameraTests.cpp:1854`, `:1890`) | **P** | SC7 |
| 2.9 | `FUN_1013c940`, the closest-point kernel, was not opened; whether `t` clamps to `[0,1]` is unknown | n/a | **R** | RC1 → SC7 |
| 2.10 | `point_player` / `m_bForcePlayerLook` `+0x5e8` → `FUN_10178590(subject, lookAt)` **every tick**: `dir = lookAt − EyePosition()`, `VectorAngles`, then `FUN_10178550` writes a pending eye-angle snap at `+0x206c..0x2074` and raises `+0x207c`. It turns the **subject**. Server-only. **35 of 51 shipped cinematics author the key** | absent — `grep point_player\|ForcePlayerLook` = 0 hits | **P** | SC4 |

### A3 — The shot-record parser *(server report §3)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 3.1 | Record stride `0x104`; `+0x00` name, `+0x20` flags, `+0x24/0x50/0x7c/0xa8` the four anchors, `+0xd4` target count, `+0xd8` MoveSpeed, `+0xdc` MoveAccel, `+0xe0` TurnAccel, `+0xe4` MaxTurnRate[3], `+0xf0` AngularTolerance[3], `+0xfc` DistanceTolerance, `+0x100` FieldOfView | ported as named fields — `ElysiumCameraShots.h:89-108`; the retail stride/offset listing is the comment at `:82-88` (`:86-88` is the offset table — corrected from the ledger's `:84-88`) | **P** (landed) | — |
| 3.2 | Defaults 150 / 50 / 30 / [90,90,90] / [1,1,1] / 10 / 75; FOV clamped `[20,120]` (`_DAT_1044eb0c`/`_DAT_1044f00c`); `FUN_10072300` writes the identical set | ported and asserted — `ElysiumCameraShots.cpp:98-108` (clamp `:102`); tests `ElysiumCameraTests.cpp:1860-1898` | **P** (landed) | — |
| 3.3 | Flags `+0x20`: `0x01` Start · `0x02` End · `0x04`/`0x08` Point1/Point2 · `0x10` DialogPOV · `0x20` AutoPositionFromTarget · `0x40` DrawViewmodel · `0x80` SnapOnShotChange · `0x100` SyncRotateOnMove · `0x200` ShowHud | ported as separate bools; no flag dword (`ElysiumCameraShots.cpp:103-108`, blocks `:164-169`) | **P** (landed); the explicit Start/End/Point presence flags the shot-start arm needs land in SC5 | SC5 |
| 3.4 | Anchor record `0x2c`: `+0x00` flags, `+0x04` a 16-byte inline name (`Q_trimspace` after `Bone:`/`Attachment:`), `+0x14` OffsetOrigin, `+0x20` OffsetAngles | ported except the name is kept as the raw `AttachPos` string (`ElysiumCameraShots.h:68`, parse `.cpp:74`) | **P** (landed) | — |
| 3.5 | `Position` (`_strstr`, in order): `Player` `0x1`, `DialogTarget` `0x2`, **`GrappleVictim` `0x80000`**, **`GrappleAttacker` `0x100000`**, `Named` `0x8`, **anything else ⇒ `World` `0x4`** | **diverges twice** — `ElysiumCameraShots.cpp:43-55` parses a `GrappleTarget` keyword (`:48`) that ships nowhere and no retail parser accepts, and its fallthrough (`:51-54`) takes an unrecognised value as an **entity name**, where retail falls to `World` | **P** — both are straight corrections | SC6 |
| 3.6 | `Position` resolve in `SetShot`: `Player` → player 1; `DialogTarget` → `subject+0xFE8`; `GrappleVictim` → `subject+0x1538` when `+0x153c == 0`, **the subject itself** when `== 1`; `GrappleAttacker` the mirror; both require the handle live, else the `World`/NULL tail; `World` → `worldspawn`; **`Named` → NULL** | partial — `AnchorEntity` (`ElysiumCameraShots.cpp:305-333`): `Player` ✓, `DialogTarget` ✓, `GrappleTarget` degrades to the dialogue target (`:317-318`), `World` → nullptr rather than worldspawn, `Named` resolves by name | **P** + **R** (the grapple role pair) | SC6, RC13 |
| 3.7 | `AttachPos`: `Bone:` `0x200` (position **and** angles), `Attachment:` `0x400`, `Center` `0x20`, `EyePosition` `0x40`, `Top` `0x100` = `(absOrigin.x, absOrigin.y, bounds.maxs.z)`, `Bottom` `0x80` the same with `mins.z`, **`AbsMin` `0x800`** = `mins` all three, **`AbsMax` `0x1000`** = `maxs` all three, default `Origin` `0x10`. Bounds from `m_Collision->vfunc 0x3c`, read once at the top | partial — `ElysiumCameraShots.cpp:342-392`: `Bone:`/`Attachment:` ✓ (`:359-378`), `Center` ✓ (`:379`), `EyePosition` ✓ (`:382-390`), **`Top`/`Bottom` are `BoundsPoint(1.0)`/`BoundsPoint(0.0)`** (`:380`/`:381`) — a fraction on all three axes — and **`AbsMin`/`AbsMax` absent** (grep: 0 hits) | **P** | SC6 |
| 3.8 | `AttachType`: an **exact byte compare including the NUL** (`Follow` 7, `FollowNoAngles` 15, `FollowEntAngles` 16), **case-sensitive**; default `None` `0x2000` | **diverges** — `ElysiumCameraShots.cpp:57-64` matches `ESearchCase::IgnoreCase` | **P** — M3 ruled: reproduce the case-sensitive compare, warn on a case-only mismatch | SC6 |
| 3.9 | `OffsetOrigin` → `+0x14`, flag `0x20000` when non-zero; the offset step `LAB_1006f430` is gated on it: `None\|FollowNoAngles` add in world axes; `Follow` rotates by the *attach point's* angles; `FollowEntAngles` by the *entity's* abs angles | partial — `ElysiumCameraShots.cpp:79-83`, `:414-424`. `Follow` and `FollowEntAngles` **both** rotate by the entity's angles, so a `Bone:`/`Attachment:` `Follow` anchor loses retail's bone-space offset frame | **P** | SC6 |
| 3.10 | `OffsetAngles` → `+0x20`, flag `0x40000`; **no reader anywhere in `vampire.dll`** | not parsed — matches retail behaviour | **P** (nothing to do); record the settlement | SC6 (doc) |
| 3.11 | `SetShotAnchorEntity` `FUN_1006ef50`: null ⇒ handle `-1`; else store the handle and, when the entity animates, `LookupBone`/`LookupAttachment` into `+0x620[i]`; a non-animating entity leaves `-1` | partial — the port resolves by socket **name each frame** (`ElysiumCameraShots.cpp:369-377`) rather than caching an index, and falls back to `Origin + Z(64u)` on a miss (`:377`), which has no retail counterpart | **P** — and the entry point 1.4 needs | SC4, SC6 |

### A4 — `SetCamera`, `GetCineCamera`, `FindBestShot` *(server report §4)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 4.1 | `CBasePlayer::SetCamera` `0x1017d020`: no camera ⇒ create; **create fails ⇒ create `"DialogDefault"`**; camera present ⇒ `SetShot(name,1,NULL)`, **failing ⇒ `SetShot("DialogDefault",1,NULL)`** | **absent** — `Push` returns 0 on a load failure (`ElysiumCameraShots.cpp:490-494`); the only `DialogDefault` hits in the tree are test fixtures (`ElysiumCameraTests.cpp:415`, `:420`, `:1782`) | **P** | SC9 |
| 4.2 | `SetCamera` **does not immobilize**; only `StartShot` and `StartPlayerDialog` do | equivalent today (nothing immobilizes) but must stay true once 1.9 lands | **P** — assert it | SC9 |
| 4.3 | The re-shot branch **never calls `FUN_1006e8e0`**, so it neither re-places the entity nor refills the shot-start anchor cache; with 2.6, a mid-conversation `SetCamera` leaves the entity where the *first* shot put it | matches in effect — the port continues the tracker from its current pose on a shot-id change (`ElysiumCameraComponent.cpp:185-191`) | **P** (landed); the retail reason gets recorded | SC9 (doc) |
| 4.4 | `SetCamera`'s only caller is the Python native `FUN_10198070`; 115 shipped call sites | ported — `Private/Scripting/ElysiumScriptNatives.cpp:61`/`:415`/`:419` → `SetScriptedCamera` | **P** (landed) | — |
| 4.5 | `FindBestShot` `FUN_1006e4c0`: `CamMode = 1` directly, enumerate `"%s_%d"` from 1 until the first missing name, keep every index passing both predicates, pick **uniformly at random**, log `CBaseCineCam::FindBestShot chose %s`; no `FUN_1006e8e0` after the final `SetShot` | **absent** — 0 hits | **P**; the draw comes from a named `ElysiumRng::Stream` | SC8 |
| 4.6 | `FUN_1006d9d0` — the declared anchors exist: `flags == 0xffffffff` ⇒ false; each declared Start/End/Point1/Point2 needs a live handle; `m_ShotIndex != -1` | absent | **P** | SC8 |
| 4.7 | `FUN_1006db10` — the camera can see its target: `UTIL_TraceHull` from each live Start/End anchor to the look-at, hull `±1`, mask `0x1400b`, filter ignoring the subject; fail on `fraction < 1 \|\| startsolid \|\| allsolid` | absent | **P** | SC8 |
| 4.8 | `FUN_10070550(baseName)` — create disposable, `FindBestShot`, `UTIL_Remove` on failure, `FUN_1006e8e0` on success. Its **only** caller is `CBasePlayer::HandleAnimEvent` `0x10178a10` | absent | **P** | SC8 |
| 4.9 | Anim event **4050** (`0xfd2`): `options` is the shot base name; on success `m_bDrawPlayer = 1`, adopt, **clear** `m_bForcePlayerLook`. **4051** (`0xfd3`): `SetCineCamera(NULL)`, then face the player's own flattened forward. 40 shipped occurrences each; `stealth_kill.txt` supplies `Stealth_Kill_1..4` | **absent** — the only `4051` in the tree is an unrelated activity id (`Private/Visual/ElysiumNpcActivityTables.cpp:1469`, `ACT_ZOMBIE_FEEDING_IDLE`); `4050` appears nowhere | **P**; corrects `animation_events.md` | SC8 |

### A5 — Dialogue *(server report §5)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 5.1 | `StartPlayerDialog` `0x10178280`: `SetDialogPartner`, `SetImmobilized(true)`, latch `+0x1e01` from the active weapon's `+0x870`, holster `"item_w_unarmed"`, then — unless the partner RTTI-casts to a payphone — `FUN_10070470(npc->+0x64C4, NULL×4)` with **no anchor entities**, and adopt. The payphone arm runs `StartGrappleAttack(this, npc, 5)` | partial — the `default_camera` opener is modelled (`Private/Substrate/ElysiumEntityWorldDialogue.cpp:521-568`; field `ElysiumNpc.h:46`; registered `ElysiumNpcClasses.cpp:129`; debug row `:751`); the immobilize, the holster latch and the payphone arm are absent | **P** | SC9 |
| 5.2 | **`StartPlayerDialog` has no `DialogDefault` fallback** — a name that does not load leaves `cam == NULL` and the conversation runs cameraless. Only `SetCamera` falls back | matches in effect but for the port's own reason | **P** — assert the asymmetry | SC9 |
| 5.3 | There is **no engine-side per-line camera**: no `.dlg` string in `vampire.dll`, and `SetCamera` has exactly one caller | matches — the port's per-line selection is its own declared grammar | **P** (landed) + **R** (a read of the bridge closes it) | RC6 |
| 5.4 | `EndPlayerDialog` `0x10178400`: **`UTIL_Remove(cam)` unconditionally** — not gated on `+0x204 & 0x4` — then `SetCineCamera(NULL)`, `SetImmobilized(false)`, restore the weapon, `SetDialogPartner(NULL)`. **No blend, no fade, no dampening** — control returns on the same tick the camera dies | **diverges** — the scoped request releases over `BlendOutSeconds = 0.25` (`Private/Player/ElysiumDialogueCamera.cpp:95`; ramp `ElysiumCameraService.cpp:184-192`) | **P** — M1 ruled: cut | SC2, SC9 |
| 5.5 | `m_flResetCameraDampeningTime` (`DT_Local +0x13c`) is written only by `CPointTeleport::vfunc113` — a teleport affordance, **not** a camera-restore blend | not ported; nothing claims it is a blend | **P** (nothing to do); recorded so it is not mistaken for one | — |
| 5.6 | `DialogPOV` (`flags & 0x10`) has exactly one reader, `CAI_BaseNPC::FUN_1026b810`: the NPC's eye-look target becomes the camera's `EyePosition()` = `m_vecCamOrigin` (`vfunc193` `0x1006d910`), **gated by `FUN_10325da0`**; a failed gate falls through to the ordinary target chain | ported with its own resolver — `Public/ElysiumDialogueCamera.h:35-40`, `ElysiumEntityWorldDialogue.cpp:706-733` (the flag-resolution / default-set modernization comment is `:452-469`, corrected from the ledger's "per-line grammar"), consumed at `Public/ElysiumPlayer.h:1604`/`:1609`, head at `Public/ElysiumEntityWorld.h:458`. **The feasibility gate is not reproduced** | **P** + **R** | SC9, RC5 |

### A6 — Camera end and removal *(server report §6)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 6.1 | `FUN_1006e0e0` clears the four anchor handles and bone indices to `-1`, `m_ShotIndex = -1`, `CamMode = 0`, and **does not touch** `+0x55c`, `+0x594`, `+0x5e8` or `+0x640` | n/a (no mode field yet) | **P** — the same "what survives a clear" discipline | SC4 |
| 6.2 | `FUN_1006e770`'s jump table: 0 none · 1 `0x1006f8f0` · 2 `0x1006fde0` **empty** · 3 `0x1006fe00` (no callers) · 4 `0x1006f870` (zero shipped instances) · >4 `CamEndThink` only past the expiry | ported as **one bool** — `FElysiumCameraShot::bTracked` is the `CamMode == 1` test (`Public/ElysiumCameraSolve.h:273`; writers `ElysiumCameraShots.cpp:458`, `ElysiumDialogueCamera.cpp:161`; sole reader `ElysiumCameraSolve.cpp:387`) | **P** — the bool stays as the tracker's contract; the entity gains all five arms | SC4, RC12 |
| 6.3 | `FUN_10071970` — level teardown: full `EndShot` when active then `UTIL_Remove` for each `camera_cinematic`, then `UTIL_Remove` every `camera_track` | ported in spirit — `ElysiumEntityWorld.cpp:2113-2115` (`ClearTrackCamera(0)` then `ClearScriptedCamera()`) | **P** — gains the "full EndShot when active" step | SC4 |
| 6.4 | **Every** retail exit is a same-tick removal: `InputEndShot`, `EndPlayerDialog`, `InputRemoveCamera` `0x10171f10`, `CFuncMonitor::vfunc42`, `CPropHacking::vfunc42`, `CPropKeypad::vfunc42`, `FUN_10225140`, `FUN_10169660`, `FUN_10170090`. A terminal closer drops to the **player view**, never to a previous shot | the terminal already cuts (`ElysiumTerminal.cpp:763-773`, `PopCameraShot(…, 0.0f)`); `RemoveCamera` clears without a stated blend (`Private/Substrate/ElysiumPlayerClasses.cpp:427-429`); the dialogue and generic paths ramp | **P** — M1 ruled: cut everywhere, no cvar | SC2, SC4, SC9 |

### A7 — The `camera_track` override channel *(server report §7 / client report §4)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 7.1 | `DT_Local`: `m_iCameraOverrideIdx` (11 bits), `m_vecCameraViewOverride`, `m_vecCameraTargetOverride`, `m_flCameraFOVOverride` (10 bits `[0,180]`), `m_flCameraRollOverride` (12 bits `[−180,180]`), `m_flCameraOverrideTimestamp`, `m_flCameraOverrideFadeStartTime`, `m_flCameraOverrideFadeDuration` (10 bits **`[−10,+10]`**) | not modelled by name; the port publishes one composed value shot (`ElysiumEntityWorld.cpp:1040-1115`) | **P** in shape; **M4 ruled**: the ranges are kept as contract clamps, the bit widths skipped; **R** for the `+0x1f44`/`+0x1f48` FOV-vs-roll disagreement | SC3, RC8 |
| 7.2 | `GetCameraOverrideWeight` `FUN_1017d900`: `dur == 0` ⇒ 1; `> 0` ⇒ `clamp((t − mark)/dur)`; `< 0` ⇒ `clamp(1 + (t − mark)/dur)`. **The sign is the direction**, and the getter is also the **reaper** | **diverges in encoding** — `Advance` ramps toward `Shots.Num() > 0 ? 1 : 0` at `1/RampSeconds` (`ElysiumCameraSolve.cpp:535-551`); `Pop(Id, BlendOutSeconds)` covers the same behaviour. The lazy reap has no counterpart | **P** — M5 ruled: port the state machine verbatim, the sign **is** the stored state; `Pop` survives only as a façade | SC3 |
| 7.3 | `FUN_1017d0b0` re-times by **back-dating the start by `w·dur`**, so a mid-blend reversal resumes symmetrically without a separate weight; a `> 0` fade shortens only when it would otherwise finish later | equivalent behaviour, different mechanism — asserted at `ElysiumCameraTests.cpp:192-208` | **P** — M5 ruled: the back-dating becomes the mechanism, not an equivalent; the existing assertion re-points at it | SC3 |
| 7.4 | `FUN_1017d280` (set view entity) **starts with `SetCineCamera(NULL)`** — the two channels are mutually exclusive by construction | **absent** — the port's two channels coexist on one stack; `SelectTrackCameraRole` (`ElysiumEntityWorld.cpp:1025-1038`) leases roles but does not clear a live cine shot | **P** | SC2 |
| 7.5 | `max(crossfade, ent->vfunc0xD0())` on push and `max(dur, ent->vfunc0xD4())` on release — each entity can demand a **minimum** crossfade; `FUN_1017d6d0` additionally treats `dur ≤ 0` as an **instant snap** | the authored `FromPlayerTime`/`ToPlayerTime` **are** routed (`Private/Substrate/ElysiumCameraTrack.cpp:324-326`, `:328-360` — path corrected from the ledger's `Private/Player/`), but as a straight assignment, not a `max()` | **P** — M5 ruled: the `max()` and the `dur ≤ 0` snap are both reproduced | SC3, RC7 |
| 7.6 | A **stack** of outgoing entries at `+0x19d8` (stride `0x10`: kind byte, EHANDLE, setTime, duration), folded newest-first in `SetupVisibility` with multiplicative coverage. The kind-0 pusher is `FUN_1017d280` (§1, C1); the kind-1 pusher is not | absent | **P** — M5 ruled: the N-entry stack is built verbatim, never a single-entry stand-in | SC3, RC7 |
| 7.7 | `CHL2_Player::SetupVisibility` `0x10352120` composes once per **server frame**, and an active cine camera `ResetPVS` + `AddOriginToPVS(m_vecCamOrigin)` and **skips the ordinary PVS** | the once-per-frame publish is reproduced; the PVS half is not applicable | **P** for the publish cadence; **M13 ruled** for the PVS (Unreal culls from the actual view) | SC3 |
| 7.8 | The per-entry lerp factors in `SetupVisibility` are decompiler-confused; the shape is certain, the register per component is not | n/a | **R** — read `corpus asm 10352120` before porting the stack | RC8 → SC3 |
| 7.9 | `vfunc0xBC/0xC0/0xC4/0xC8/0xCC/0xD0/0xD4` identified only by use; `CCameraTrack`/`CCameraKeyFrame`'s own classes (`FUN_100cb910`, `FUN_100cb580`) unopened | the port implements the schedule from `camera-view-modes.md` §6, which is opened | **R** | RC7 → SC3 |

### A8 — The client view chain *(client report §1)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 8.1 | `CViewRender::SetUpView` `0x10191710` runs the override chain **only** when a cine camera is adopted or `CAM_IsThirdPerson`; in pure first person nothing else touches the view. Base FOV from `<DAT_104a57e4>->vfunc40()` | equivalent — the port is third-person-only by the CCC divergence already recorded in `camera-view-modes.md` | **P** + **R** for the base-FOV source | SC2, RC10 |
| 8.2 | `C_BasePlayer::CalcView` `0x100a7770`: the vehicle arm, then the cine arm — the cine camera beats the vehicle view | vehicle arm absent | **M10** — ruled dead; the ordering fact survives as a comment | SC2 |
| 8.3 | `C_BaseCineCamera::CalcView` `FUN_10001b50` **hard-writes** origin, angles and FOV — no blend, no lerp, no weight — gated on `m_flLastTime > 0` and `IsActive()` | **diverges** — the port composes the cine channel through the same weighted `ComposeScriptedShot` as the track channel (`ElysiumCameraComponent.cpp:362-374`, `ElysiumCameraService.cpp:223-242`) | **P** — split the channels | SC2 |
| 8.4 | `ClientModeShared::OverrideView` `0x100d4040`: weapon override first, then **either** `CInput` slot 31 (boom `+=`, angles `=`, then the track override) **or**, with a live cine camera, slot 33 alone. The boom is **skipped entirely** under a cine camera | collapsed into one "compose the shot over the rig" call (`ElysiumCameraComponent.cpp:243-247`, `:335-375`) | **P** | SC2 |
| 8.5 | The `bOffCenter` block (two dev cvars) | absent; no shipped use | **M11** — ported under `elysium.*` names | SC2 |
| 8.6 | `CalcView`'s last arm hard-replaces origin and angles with the spectated entity's — the death/observer view; there is no separate death/feed/seduction arm | the port has its own observer seam | **P** + **R** for the exact test | SC2, RC10 |

### A9 — `C_BaseCineCamera` lifecycle *(client report §2)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 9.1 | `OnDataChanged` `0x100024c0`: a new `m_nClientResetFrame` ⇒ shot-start pending + `bActive`; a new `m_ShotIndex` ⇒ **either** the snap **or** clear all three angle-settled flags, and apply `ShowHud`; going inactive after being active restores the HUD. `m_nClientResetFrame` is **not** written back here | maps to the top-shot-id change (`ElysiumCameraComponent.cpp:185-191`) and `SelectWinner` (`ElysiumCameraService.cpp:136-150`); the HUD restore falls out of the stack emptying | **P** (landed) — the explicit two-signal split lands in SC5 | SC5 |
| 9.2 | Shot start `FUN_10002210`, the **replicated-goal arm** (`!(End) \|\| Start`): current = goal, shot-start = goal | ported exactly — `FElysiumScriptedShotTracker::Start` (`ElysiumCameraSolve.cpp:354-368`) | **P** (landed) | — |
| 9.3 | Shot start, the **live-view arm** (`End` without `Start`): the current pose and the shot-start origin come from `CViewRender::GetViewSetup()` — the shot **dollies in from the live view**. The shipped `jack.txt` / `dialogdefault.txt` / `DialogMediumShot` shape | **absent** — documented as absent in `Public/ElysiumCameraSolve.h`; the scripted weight ramp stands in for it | **P** — `FElysiumCameraShot` carries the presence flags and `Start` takes the current view as a parameter | SC5 |
| 9.4 | `m_flFOV (0x458)` is **never touched** by shot start, and neither is `m_flCurFov`; mode 1 takes the shot file's `FieldOfView` every frame | matches — the port's FOV comes from the shot record | **P** (landed) | — |
| 9.5 | The settle flags are **asymmetric** at start: position settled, all three angle axes unsettled | ported exactly — `ElysiumCameraSolve.cpp:363-366` | **P** (landed) | — |
| 9.6 | Update `FUN_10001a20` latches on `engine->GetFrameCount()` — the tracker advances **once per rendered frame** | ported by a different route — `GFrameCounter` latches at `ElysiumCameraComponent.cpp:83-87`, `ElysiumCameraService.cpp:154-158`, `ElysiumCameraModifiers.cpp:13-15` | **P** (landed) — asserted, not rebuilt | SC1 |
| 9.7 | `dt` is the camera's **own** `curtime − m_flLastTime`, **clamped to 1.0 s** and **replaced by a flat 0.01 s** below ~1/255 s | partial — the port clamps negatives to 0 and early-outs at zero (`ElysiumCameraSolve.cpp:377-381`), pauses to 0 (`ElysiumCameraComponent.cpp:94`), but has **no ceiling and no floor** | **P** | SC1 |
| 9.8 | The destructor `FUN_10001920` restores the HUD when the shot had hidden it | equivalent (stack emptying) | **P** (landed) | — |

### A10 — The tracker's numerics *(client report §3)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 10.1 | Position `FUN_10001fe0`: `threshold = settled ? DistanceTolerance : 1.0 u`; `dist = \|cur − goal\|`; `settled = dist < threshold`; settled ⇒ speed 0 and no movement | ported — `ElysiumCameraSolve.cpp:402-430`, threshold `:407-408`; `SettleDistance = 1.0f * 2.54f` at `Public/ElysiumCameraSolve.h:345` | **P** (landed) | — |
| 10.2 | `stopDist = v²/(2·MoveAccel)`; `speed = Approach(speed, stopDist < dist ? MoveSpeed : 0, MoveAccel, dt)`; **`speed = clamp(speed, 1.0f, MoveSpeed)`** — a **1.0 u/s floor**; `step = min(speed·dt, dist)`; a plain unit-direction Euler advance with **no hidden integration constant** | **diverges** — `ElysiumCameraSolve.cpp:425` clamps to `[0, MoveSpeed]`; the floor is absent | **P**; the floor is load-bearing for 12.2 | SC1 |
| 10.3 | Angles `FUN_10001d40`: `desired = VectorAngles(lookAt − curOrigin)` re-derived after the position step; per axis `tol = settled[i] ? AngularTolerance[i] : **1.0°**`; `anglemod` wrap `×182.0444 & 0xffff × 0.0054932 − 180`; inside ⇒ settle and zero the rate; outside ⇒ turn and **clear** the flag (`0x10001e64`, `0x10001ead`) | ported except the deadband — `ElysiumCameraSolve.cpp:309-351` (the function is `AdvanceAngleAxis`); **`SettleAngle = 0.05f`** at `Public/ElysiumCameraSolve.h:346` where retail is **1.0°** | **P**. The flag-clearing assumption in the doc is **confirmed as retail** and retires | SC1 |
| 10.4 | Turn rate `FUN_10001c80`: `stopAngle = rate²/(2·TurnAccel)`; with `SyncRotateOnMove` **and the position unsettled**, `rate = \|delta\|/T` and `MaxTurnRate` is **bypassed**; otherwise `Approach` toward 0 inside the stopping angle, else toward `MaxTurnRate[i]` | ported — `ElysiumCameraSolve.cpp:328-346`; the gate at `:446-449` | **P** (landed) | — |
| 10.5 | `RemainingTime` `FUN_100010f0`: the trapezoid arm's accel distance is `(vmax − v)²/(2a)` (correct only from `v = 0`); the triangle arm's `vpeak = v + √(2a − 0.5(d − R2))` is dimensionally inconsistent and NaNs whenever `d − R2 > 4a` | **diverges** — `RemainingTranslationSeconds` (`ElysiumCameraSolve.cpp:288-303`) is a *correct* solve, so `jack.txt` pans ~7× slower than retail (0.17 s vs 1.27 s at 500/250 over 100 u). The NaN band needs `MoveSpeed > 2·MoveAccel` and **no shipped shot reaches it** | **M6** | SC1 |
| 10.6 | `MoveAccel == 0` ⇒ the `0/0` compare is false ⇒ the decel arm ⇒ `Approach` leaves the speed unchanged ⇒ the clamp pins it at the 1.0 u/s floor | an explicit `Speed = MoveSpeed` arm (`ElysiumCameraSolve.cpp:417-420`) | **P** — M7 ruled: reproduce the crawl through an explicit decel branch, never a real divide | SC1 |
| 10.7 | FOV `FUN_10001c20` is a **copy**, never a lerp, from the shot record; a dev ConVar (`DAT_102de30c`, threshold `_DAT_101e34f4`) short-circuits it and **freezes** the rendered FOV | the port lerps the shot FOV by the channel weight (`ElysiumCameraSolve.cpp:219-222`), which becomes retail once the channels split; the guard is absent | **P** + **M12** | SC1, SC2, RC9 |
| 10.8 | Snap `FUN_10002390`: hard-copy goal → current, settle position, zero rates, clear all three angle flags, re-derive angles from the look-at **only when `CamMode == 1`**, then **clear the pending flag — it is a one-shot** | **diverges** — the port re-applies it every frame (`ElysiumCameraSolve.cpp:396-401`, `:437-443`), so a `SnapOnShotChange` shot can never track. 9 shipped files set it, including `sp_tutorial_1`'s `LookAtTarget_Snap` | **P** | SC1 |
| 10.9 | `FUN_100010b0(v,a) = v²/2a`; `FUN_10001070` is `Approach` with **no clamp** (the caller clamps) | ported | **P** (landed) | — |

### A11 — Composition *(client report §4)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 11.1 | `CInput::OverrideView` `FUN_100ffb90`: `e = SimpleSpline(weight)`; `viewFwdPoint = origin + forward × 100 u`; `origin = lerp(origin, override.origin, e)`; `dir = lerp(viewFwdPoint, override.target, e) − origin`; `VectorAngles(normalize(dir))`; **`roll = e × override.roll`** (the base roll discarded); `fov = lerp(fov, override.fov, e)` | **diverges** — `ElysiumCam::ComposeScriptedShot` (`ElysiumCameraSolve.cpp:207-223`) lerps the **rotator** directly and lerps roll as part of it | **P** | SC2 |
| 11.2 | The ease is `SimpleSpline` applied **at the point of use**; the ramp itself is linear | **diverges** — `ElysiumCam::SimpleSpline` exists (`Public/ElysiumCameraSolve.h:36-40`) and is applied to `Third` (`:136`), `Feed` (`:137`) and the fade band (`ElysiumCameraSolve.cpp:109`), but **the scripted channel gets the raw linear weight** (`Public/ElysiumCameraComponent.h:57` "never re-eased" — corrected from the ledger's `:58`, which is the field it annotates; `ElysiumCameraModifiers.cpp:59`) | **P** — M9 reclassified: contract, not a modernization; lands with the shots baseline refresh | SC2 |
| 11.3 | `CAM_Think` copies the replicated override fields into `CInput` **only while the weight is already non-zero** | equivalent — the port's shot values are pushed by their owner | **P** (nothing to do) | — |
| 11.4 | The ramp `FUN_100fc900`'s tail: `startTime ≤ 0` ⇒ 0; `0 ≤ duration ≤ 0.0039` ⇒ **1 immediately, a hard cut in that stays**; `duration < 0` ⇒ from 1 down over `\|duration\|` | the port's zero-blend push is already a cut (`ElysiumCameraComponent.cpp:638-641`); the negative-duration convention is unrepresented; `_DAT_10235278`'s value is unread | **P** — M5 ruled: the three regimes are the stored state; **R** for `_DAT_10235278` | SC2, RC9 |
| 11.5 | **There is no `ScaleFOVByWidthRatio` and no aspect maths in `client.dll`** — all three paths write one `viewsetup.fov` scalar and the engine widens it identically | ported at apply time as `ElysiumCam::WidenSourceFov` for all three (`ElysiumCameraComponent.cpp:261-262`, `:369-372`; `ElysiumCameraService.cpp:232-237`) | **P** (landed); **M15 ruled**: Unreal-native, pixel-only, no change | — |
| 11.6 | `CBaseCombatCharacter::SetAsCameraTarget` `0x1000a2d6` / `GetCameraFadeOutTime` `0x10332240` carry an authored fade and have **zero recovered callers**. The mark/duration writers themselves are recovered (§1, C1) | the port routes `FromPlayerTime`/`ToPlayerTime` directly (`Private/Substrate/ElysiumCameraTrack.cpp:328-360`) | **R** — the missing hop is the callers only | RC7 → SC3 |

### A12 — Draw policy and the HUD *(client report §6)*

| # | Retail fact | Port status at HEAD | Disposition | Slice |
|---|---|---|---|---|
| 12.1 | `0xa06d` is a HUD-element **bitmask**; `CHudManager::HideHud` (slot 113, `0x10057f50`) walks the element list and `SetVisible(false)`s every element whose `GetHudBits()` intersects. **Edge-triggered on the shot index changing**, never per frame | equivalent in effect — `bShowHud = !Shot.bNamed \|\| Shot.bShowHud` (`ElysiumCameraSolve.cpp:200`), solved per frame from the top shot | **M14 ruled**: the **element set** is the port's own HUD (menu/HUD are new assets); the **edge timing is contract** and is reproduced, including the absence of per-frame enforcement | SC5 |
| 12.2 | `FUN_100019a0` — `ShotWantsViewmodel() = (m_flSpeed <= 1.0f) && (flags & DrawViewmodel)`. The `1.0` is exactly the tracker's speed floor | **the speed term is missing** — `bViewmodelEligible = !bThirdPerson && (!Shot.bNamed \|\| Shot.bDrawViewmodel)` (`ElysiumCameraSolve.cpp:192`) | **P**, needs 10.2 | SC5 |
| 12.3 | The same predicate gates the `scr_ofsx/y/z` viewmodel offset block in `ClientModeShared::OverrideView` | not applicable — no first-person viewmodel renderer | **P** (nothing to do) | — |
| 12.4 | `C_BasePlayer::ShouldDrawLocalPlayer` `0x100a7a50`: while a cine camera is **adopted** (not merely active), the body is drawn iff `m_bDrawPlayer`, and that **short-circuits the whole `CAM_IsThirdPerson` disjunction**. `0x464` is read exactly once in the image | **absent** — `bBodyEligible = bThirdPerson` (`ElysiumCameraSolve.cpp:178`); `FElysiumShotPresentation` has no body field (the only `bDrawPlayerBody` in the tree is the view state's, `Public/ElysiumViewState.h:521`, consumed at `Private/UI/ElysiumPresentationSubsystem.cpp:715` — a different concept) | **P** | SC5 |
| 12.5 | `C_BasePlayer::ShouldHideViewModel` (slot 174): the viewmodel is hidden whenever a cine camera is adopted, unless 12.2 passes | partial — the port's `!Shot.bNamed` stands in for "a cine camera is adopted" | **P** | SC5 |
| 12.6 | The client half of the anchor parser (`client.dll FUN_10028a10`) and the client half of `m_bDrawPlayer` were out of scope; `m_bDrawPlayer` has **no server reader at all**, so its whole behaviour lives there | n/a | **R** | RC11 → SC5, SC6 |

### A13 — `camera_track` / `camera_keyframe` *(landed; recorded for completeness)*

This half of the surface is **landed**. The plan reaches it only where the composition shape (11.1,
11.2), the channel split (7.4) and the fade machinery (7.2, 7.5, 7.6) touch it. Paths corrected: the
implementation is `Private/Substrate/ElysiumCameraTrack.{h,cpp}`, **not** `Private/Player/`.

| # | Retail fact | Port status at HEAD |
|---|---|---|
| 13.1 | `CCameraTrack` derives from `CCameraKeyFrame`; the track is its own first key | ported — `ElysiumCameraTrack.cpp:612-615` (`camera_track` registered **from** `camera_keyframe`) |
| 13.2 | `Activate`'s ≤ 0.05 s fold, re-attributing to the next key's pause, else its own, else dropped; `Corner` forced on both ends; order-sensitive | ported — `ElysiumCameraTrack.cpp:253-282`, threshold via `UElysiumChoreoSettings::CameraCutSeconds` |
| 13.3 | The schedule `0x100cc430`; `Corner` read off the **destination**; per-frame sampling; the clock cannot drift | ported — `ElysiumCameraTrack.cpp:53-68`, `:155-221`, `:362-376` |
| 13.4 | Hermite rate remap + four-point Catmull with `Corner` endpoint selection; roll and FOV packed into a second vector through the same Catmull; focal → FOV **before** interpolating; roll swept with no shortest-path unwrap | ported — `ElysiumCameraTrack.cpp:107-121`, `:123-130`, `:188-210` |
| 13.5 | Two independent clocks (position, target) over one chain; `HoldAtEnd`; the stale-owner guard on restore | ported — `ElysiumCameraTrack.cpp:324-326`, `:404-412`, `:487-523`; leases at `ElysiumEntityWorld.cpp:1025-1038` |
| 13.6 | The composed track shot is **direct** (`bTracked` false), because the authored samples already define the view and retail applies this channel through `CInput` | ported — `ElysiumEntityWorld.cpp:1086-1094` (the shot never sets `bTracked`; it stays default false at `:1089`); asserted `ElysiumCameraTests.cpp:1545-1546` |
| 13.7 | `PositionInterpolator` is a dead key | ported as dead — `ElysiumCameraTrack.cpp:239`, `:400` |
| 13.8 | **Nothing in the track path immobilizes the player** — `camera_track` transfers the view, not the player | matches; the port immobilizes nowhere on this path |

---

## 6. The slices

Ordered by dependency. Each carries: goal · the retail chain reproduced · the design in the port ·
tests · docs · the §7 rulings it applies · an empty **Closure record**.

---

### SC1 — The client tracker's numerics and the frame contract

**Goal.** `FElysiumScriptedShotTracker` becomes `C_BaseCineCamera`'s tracker exactly, including the
frame contract and the FOV path. Nothing else in the subsystem is trustworthy until it is, because
SC5's viewmodel predicate and SC2's compose weight both read tracker state.

**The retail chain.**

- **The frame latch.** `C_BaseCineCamera::Update` `FUN_10001a20` returns immediately when
  `m_nFrameCache (0x494) == engine->GetFrameCount()` (vfunc 44, `+0xb0`), so the tracker advances
  exactly once per rendered frame however often `CalcView` is reached.
- **The delta.** `dt = engine->GetCurTime() (vfunc39, +0x9c) − m_flLastTime (0x490)`; **`dt > 1.0f ⇒
  dt = 1.0f`** (`FCOMP _DAT_101e34ec`, `MOV 0x3f800000`); **`dt < 0.0039f ⇒ dt = 0.01f`**
  (`FCOMP _DAT_101e34e8 = 1/255`, `MOV 0x3c23d70a`), which covers zero and negative. Then
  `m_flLastTime` and `m_nFrameCache` are restamped.
- **Dispatch.** `if (m_bSnapPending 0x4a1) FUN_10002390();` then `CamMode == 4 → FUN_10002200(dt)`
  (empty, `RET`), `CamMode == 1 → FUN_10001fa0(dt)`, else copy through
  `m_vecCurOrigin = m_vecCamOrigin (0x410)`, `m_flCurFov = m_flFOV (0x458)`,
  `m_angCurAngles = m_angCamAngles (0x428)`.
- **Position `FUN_10001fe0`** (listing `0x10002083`–`0x100021b1`):
  `threshold = m_bPositionSettled (0x4c0) ? rec->DistanceTolerance (+0xfc) : 1.0f`;
  `dist = |m_vecCurOrigin (0x468) − m_vecCamOrigin (0x410)|` → `m_flDistRemaining (0x4c4)`;
  `settled = dist < threshold`. Settled ⇒ `m_vecSettledOrigin (0x4b4) = m_vecCurOrigin`,
  `m_flSpeed (0x4a4) = 0`, no movement. Unsettled ⇒ `dir = normalize(goal − cur)`;
  `stopDist = FUN_100010b0(speed, MoveAccel) = v²/(2a)`;
  `speed = FUN_10001070(speed, stopDist < dist ? MoveSpeed : 0, MoveAccel, dt)` — an **unclamped**
  approach; **`speed = clamp(speed, 1.0f, MoveSpeed)`** — the 1.0 u/s floor;
  `cur += dir * min(speed*dt, dist)`. There is no hidden integration constant.
- **Angles `FUN_10001d40`.** `desired = VectorAngles(m_vecLookAt (0x41c) − m_vecCurOrigin)`,
  re-derived **after** the position step. Per axis:
  `tol = m_bAngleSettled[i] (0x4c1+i) ? rec->AngularTolerance[i] (+0xf0+i*4) : **1.0f**`
  (`_DAT_101e34ec`); the `anglemod` wrap is
  `n = (int)((desired[i] − cur[i] + 180.0f) * 182.0444f) & 0xffff; delta = n * 0.0054932f − 180.0f`.
  `delta < −tol` ⇒ turn negative, clamp at `desired`, **clear** the settled flag
  (`MOV byte ptr [EBP],0x0` at `0x10001e64`); `delta > tol` ⇒ the mirror (`0x10001ead`); inside the
  band ⇒ **set** the flag (`0x10001eb3`) and zero the rate.
- **Turn rate `FUN_10001c80`.** `stopAngle = rate²/(2·TurnAccel)`. With `SyncRotateOnMove`
  (`flags & 0x100`) **and the position unsettled**, `return fabsf(delta / T)` — `MaxTurnRate` is
  **bypassed** — where `T = FUN_100010f0(speed, MoveSpeed, MoveAccel, m_flDistRemaining)`. Otherwise
  `Approach(rate, |delta| < stopAngle ? 0 : MaxTurnRate[i], TurnAccel, dt)`.
- **`RemainingTime` `FUN_100010f0`** (listing `0x100010f0`–`0x100011bb`), verbatim:
  `R1 = vmax²/(2a)`, `R2 = v²/(2a)`; `2*R1 < d` ⇒
  `(d − ((vmax−v)²/(2a) + R1))/vmax + |(vmax−v)/a| + |(0−vmax)/a|`; `R2 >= d` ⇒ `|(0−v)/a|`;
  else `vpeak = v + sqrtf(2a − 0.5f*(d − R2))`, `return |(vpeak−v)/a| + |(0−vpeak)/a|`.
- **FOV `FUN_10001c20`** is a **copy**, never a lerp: `m_flCurFov (0x480) = rec->FieldOfView (+0x100)`
  every frame, from the *shot record*; the replicated `m_flFOV` is never consulted in mode 1. Ahead
  of it, the dev-cvar guard: `if (!DAT_102de30c.IsCommand() && _DAT_101e34f4 < DAT_102de30c.GetFloat())
  return DAT_102de30c.GetFloat();` — **without writing `m_flCurFov`**, so the rendered FOV freezes.
- **Snap `FUN_10002390`** is **one-shot**: copy goal→current for origin and angles, re-seed
  `m_vecShotStart (0x484)` and `m_vecSettledOrigin`, `m_bPositionSettled = 1`, zero `m_flSpeed` and
  the three `m_flTurnRate`, **clear** all three `m_bAngleSettled`, re-derive the angles from the
  look-at **only when `CamMode == 1`**, then **clear `m_bSnapPending`**.

**The design in the port.**

- `Private/Player/ElysiumCameraSolve.cpp` — `Advance` gains the two dt guards
  (`Dt = FMath::Min(Dt, 1.0f); if (Dt < 1.0f/255.0f) Dt = 0.01f;`) applied to the **parameter**, with
  the retail constants named. The existing zero/negative early-out at `:377-381` is replaced by the
  floor, since retail's floor *is* the zero handling.
- `Public/ElysiumCameraSolve.h:346` `SettleAngle = 0.05f` → **`1.0f` degree**, renamed
  `UnsettledAngleTolerance` with `_DAT_101e34ec` cited. `:345` `SettleDistance` already matches
  (1.0 u × 2.54).
- `ElysiumCameraSolve.cpp:425` `clamp(Speed, 0, MoveSpeed)` → **`clamp(Speed, 1.0f * U, MoveSpeed)`**,
  with the floor exposed on the tracker so SC5's viewmodel predicate can read it as
  "is the camera dollying".
- The snap becomes a real one-shot: `FElysiumScriptedShotTracker` gains **`Snap()`** as its own entry
  point (retail `FUN_10002390`), armed by a `bSnapPending` flag rather than re-evaluated inline every
  frame at `:396-401` / `:437-443`. `Advance` consumes it at the top and clears it. Snap re-derives
  angles from the look-at only for a `bTracked` shot, matching `CamMode == 1`.
- `RemainingTranslationSeconds` (`:288-303`) is **replaced by retail's arithmetic verbatim**, with the
  radicand clamped at zero and both defects named in the comment (**M6**).
- The `MoveAccel <= 0` arm at `:417-420` is **replaced by retail's behaviour (M7 ruled)**: a zero
  `MoveAccel` makes retail's `stopDist < dist` compare false, so control takes the **decel** arm,
  `Approach(v, 0, 0, dt)` leaves the speed unchanged and the clamp pins it at the **1.0 u/s floor**.
  It lands as an **explicit branch** — `if (MoveAccel <= 0) { /* retail's 0/0 compare is false */ }` —
  and never as a real divide, so no NaN or infinity is ever produced.
- The direct-shot early-out at `:383-392` (`!Shot.bTracked`) is retained unchanged — it is retail's
  `CamMode != 1` copy-through and already correct.
- The FOV path: `TrackFov()` becomes a copy from the shot record with the dev-cvar guard, including
  the **freeze** (the guard returns without writing the cached FOV) — cvar `elysium.CameraShotFovOverride`
  pending **RC9** (**M12**).
- The frame latch already exists by a different route (`ElysiumCameraComponent.cpp:83-87`,
  `ElysiumCameraService.cpp:154-158`, `ElysiumCameraModifiers.cpp:13-15`); this slice asserts it
  rather than rebuilding it.

**Tests — `Elysium.Substrate.CameraTracker`** (new, `Private/Tests/ElysiumCameraTests.cpp`):

- `jack.txt`'s numbers (500/250/30, `MaxTurnRate [60,60,60]`, `AngularTolerance [10,10,10]`) with a
  0.5° goal error: the axis **settles** at the 1.0° acquire band and would have kept turning at 0.05°.
- An unsettled tracker with `MoveSpeed 500` and a 0.001 u gap advances at **≥ 1.0 u/s**, never below.
- A `LookAtTarget_Snap`-shaped shot (`bSnapOnShotChange`) snaps on the shot change, then **tracks** its
  anchor over the next 10 frames — the current build never tracks it.
- A 5 s frame is clamped to 1 s; a 0.1 ms frame is floored to 10 ms; a 0 s and a negative frame both
  become 10 ms.
- `RemainingTime(0, 500, 250, 100)` returns **~0.17 s** where the correct kinematic solve returns
  **~1.27 s**; the clamped radicand returns a finite value across the whole `MoveSpeed > 2·MoveAccel`
  band (sample `MoveSpeed 150, MoveAccel 50, d ∈ [200, 450]`).
- **`MoveAccel 0` pins the speed at 2.54 cm/s** (the 1.0 u/s floor) and the camera crawls, as retail
  does — asserted as retail, not as a divergence — and no divide ever executes on that path.
- FOV is a copy from the record every frame; with the override cvar set, the *rendered* FOV **freezes**
  at its previous value rather than following the cvar.

**Docs.** `docs/vtmb/camera-view-modes.md` §"The client tracker's exact numerics" loses the
settle-flag port-side assumption (confirmed retail) and the approach-constant caveat (does not exist);
the divergence table's four tracker rows resolve. `docs/vtmb/retail-defects.md` §7's `RemainingTime`
row gains the reproduce/clamp split as landed.

**Rulings applied.** **M6** — reproduce retail's arithmetic verbatim, radicand clamped at zero (only
the NaN state, unreachable on shipped content). **M7** — **reversed from the earlier default**:
reproduce retail's crawl through an explicit decel branch, never a real divide. **M12** — port the
guard including the freeze, under the `elysium.*` name until RC9 recovers retail's. All ruled
2026-09-07 under the owner framework of §7. **Deps:** RC9 for the FOV guard's constant and cvar name;
the slice lands the guard behind the `elysium.*` name and RC9 renames it.

**Closure record.** Landed 2026-09-07. `Public/ElysiumCameraSolve.h` — `RemainingTranslationSeconds`
declared on `ElysiumCam`; the tracker gains `Fov`, `bSnapPending`, `Snap()`, `TrackFov()`,
`IsDollying()` and the named retail constants `UnsettledAngleTolerance` 1.0°, `MinTrackSpeed` 1.0 u/s,
`FrameDeltaCeiling` / `FrameDeltaFloorThreshold` / `FrameDeltaFloor`, `FovOverrideThreshold`
(`SettleAngle` removed). `Private/Player/ElysiumCameraSolve.cpp` — `RemainingTranslationSeconds` is
`FUN_100010f0`'s three arms verbatim with the radicand clamped (M6); `elysium.CameraShotFovOverride`
declared; `Start` arms the snap; `Snap` / `TrackFov` are `FUN_10002390` / `FUN_10001c20` including the
FOV freeze (M12); `Advance` applies the two dt guards to its parameter, consumes the one-shot ahead of
the `bTracked` copy-through, takes an explicit `MoveAccel <= 0` decel branch and clamps the speed to
`[MinTrackSpeed, MoveSpeed]` (M7). Asserted by the new `Elysium.Substrate.CameraTracker`, which also
asserts the existing `GFrameCounter` latch rather than rebuilding it.

Two arms on this path are the port's own, marked in the source: the `MoveSpeed <= 0` early arm, which
retail's parser cannot reach (default 150) but the constraint-less dialogue profiles rely on until SC9
gives them `dialogdefault.txt`'s numbers; and the one-frame floor under the `SyncRotateOnMove` divisor,
standing where retail's unguarded `|delta| / T` would divide by a zero `T` (`RemainingTime` legitimately
returns 0 inside the clamped band).

---

### SC2 — The two channels, and the `CInput` composition shape

**Goal.** Retail runs **two** channels that the port has collapsed into one weighted stack. Split
them: the cine camera hard-writes the base pose with no weight at all, and the `camera_track`
override is the only blended one, composed on top of whichever base won. This is the structural move
every later slice sits on.

**The retail chain.**

- **`CViewRender::SetUpView` `0x10191710`** seeds the `CViewSetup` (at `CViewRender + 0x10`: `fov`
  `+0x28`, `fovViewmodel` `+0x2c`, `origin` `+0x38`, `angles` `+0x50`, `zNear` `+0x5c = 8.0`,
  `zFar` `+0x60 = 28400.0`, `bOffCenter` `+0x06`, the rect `+0x08..0x14`), seeds `fov` from
  `<DAT_104a57e4>->vfunc40()`, calls `CViewRender::CalcView`, and calls `OverrideView` **only if** a
  cine camera is adopted **or** `CAM_IsThirdPerson` with no intermission
  (`<DAT_104a57e4>->vfunc38()`) and no view-effect veto (`PTR_DAT_102d5238->vfunc12()`).
- **`CViewRender::CalcView` `0x10191200`** — the ordinary view. Its last arm hard-replaces origin and
  angles with a spectated entity's when `<DAT_104a57e4>->vfunc39()` returns an index above the max
  client. **There is no separate death, feed or seduction arm.**
- **`ClientModeVampire::OverrideView` `0x10029980`** = `C_BasePlayer::CalcView` then
  `ClientModeShared::OverrideView`.
- **`C_BasePlayer::CalcView` `0x100a7770`** — the vehicle arm, then
  `cine = FUN_100a7890()` (a master enable cvar `DAT_104a0c5c`, `m_iCameraOverrideIdx != 0`, and an
  EHANDLE cache at `player+0x1640` invalidated when its index stops matching), then
  `if (FUN_10001970(cine)) FUN_10001b50(cine, v)`. `IsActive()` is `CamMode (0x45c) != 0 &&
  bActive (0x465)`. `C_BaseCineCamera::CalcView` starts a pending shot, and — only once
  `m_flLastTime (0x490) > 0` — runs `Update` and **hard-writes all three**:
  `v->origin = 0x468`, `v->angles = 0x474`, `v->fov = 0x480`. No blend, no lerp, no weight.
- **`ClientModeShared::OverrideView` `0x100d4040`** — the weapon override `vfunc244` first
  (overwritten by either), then the branch: **no cine camera** ⇒ `CInput` slot 31 `FUN_100ffb00`
  (`origin += m_vecCameraOffset (+0x164)`, `angles = m_angCamera (+0x170)`, then slot 33);
  **a live cine camera** ⇒ slot 33 `FUN_100ffb90` **directly** — the boom is skipped entirely. Then
  the `bOffCenter` block; then the early-out when `FUN_100019a0` passes; then the `scr_ofs*`
  viewmodel adjustment.
- **`CInput::OverrideView` `FUN_100ffb90`**, exactly:
  `e = SimpleSpline(m_flScriptedWeight (+0x100))` = `t*t*(3 − 2t)`;
  `AngleVectors(angles, fwd)`; `viewFwdPoint = origin + fwd * 100.0f` (`_DAT_1022b298`, 254 cm);
  `origin = origin + (m_vecOverrideOrigin (+0x17c) − origin) * e`;
  `dir = (viewFwdPoint + (m_vecOverrideTarget (+0x188) − viewFwdPoint) * e) − origin`;
  `VectorAngles(normalize(dir), angles)`; **`angles.roll = e * m_flOverrideRoll (+0x194)`** — the base
  roll is discarded outright; `fov = fov + (m_flOverrideFov (+0x198) − fov) * e`.
- **The ramp `FUN_100fc900`'s tail** is **linear**; the ease lives at the point of use.
  `startTime ≤ 0` ⇒ weight 0. `dur > 0.0039` ⇒ `(now − startTime)/dur`. `dur ≤ 0.0039` and
  `_DAT_10235278 ≤ dur` ⇒ weight stays **1** (a hard cut in that stays). `dur < 0` ⇒
  `1 + (now − startTime)/dur` — the blend **out**. Then clamp to `[0,1]`.
- **Mutual exclusion.** `FUN_1017d280` (set the track view entity) **starts with
  `SetCineCamera(NULL)`**; nothing in the cine path touches `+0x19b8`. The two channels cannot both
  own the view; the map teardown `FUN_10071970` tears both down together.
- **One FOV scalar.** There is no `ScaleFOVByWidthRatio` and no aspect arithmetic anywhere in
  `client.dll`; all three paths write `viewsetup.fov` and the engine widens it identically.
- **Every scripted exit is a cut** — `CamMode → 0`, `m_iCameraOverrideIdx → 0`, or the entity removed.
  No blend field exists anywhere on `C_BaseCineCamera` (`0x480` FOV, `0x484` shot start, `0x490` last
  time, `0x494` frame latch, `0x498` reset-frame cache, `0x49c` shot-index cache, `0x4a0..0x4a2` the
  bools, `0x4a4` speed, `0x4a8[3]` rates, `0x4b4` settled origin, `0x4c0..0x4c3` settle flags,
  `0x4c4` remaining distance).

**The design in the port.**

- `ElysiumCam::ComposeScriptedShot` (`ElysiumCameraSolve.cpp:207-223`) becomes retail's shape: ease the
  weight with `ElysiumCam::SimpleSpline` **at compose** (it already exists at
  `Public/ElysiumCameraSolve.h:36-40` and is applied to `Third` `:136` and `Feed` `:137`); lerp the
  **origin**; build `viewFwdPoint = origin + forward * (100 u × 2.54)`; lerp that toward the shot's
  look-at; re-derive the rotation with `VectorAngles` of the lerped direction; set
  `roll = e * shotRoll` outright; lerp the FOV. The rotator lerp goes away.
- `Public/ElysiumCameraComponent.h:57`'s "never re-eased" comment and
  `ElysiumCameraModifiers.cpp:59`'s raw `Alpha = Shot.Weight` are corrected together: the ramp stays
  linear and the ease moves to the compose site, which is exactly retail's division of labour.
- **The channel split.** `FElysiumCameraShot` already carries `bTracked`
  (`Public/ElysiumCameraSolve.h:273`). This slice adds the second axis: a **cine** shot adopts at
  weight 1 with no ramp and is composed as a *hard write*, while a **value/track** shot keeps the
  timed ramp and is composed through the weighted `ComposeScriptedShot`. `FElysiumCameraDirector::Resolve`
  (`ElysiumCameraShots.cpp:469`) stops inventing `BlendSeconds = 0.5`.
- **The two-branch apply.** `UElysiumCameraComponent::ApplyToView` (`:243-247`, `:335-375`) and
  `UElysiumCameraService::ApplyToView` (`:223-242`) gain retail's structure: with a live cine shot the
  rig's boom offset and third-person angles are **not applied**, and the track override composes over
  the cine pose; with no cine shot the boom applies and the track override composes over that. One
  camera in series, never two viewpoints.
- **Mutual exclusion.** `FElysiumEntityWorld::SelectTrackCameraRole` (`ElysiumEntityWorld.cpp:1025-1038`)
  clears a live cine shot when a track role is leased, and SC4's adoption slot clears the track leases
  when a cine camera is adopted — retail's `FUN_1017d280`/`FUN_10071970` pair.
- **The release is a cut (M1, ruled).** `FElysiumCameraShotStack::Pop`
  (`ElysiumCameraSolve.cpp:502-520`, ramp `:535-551`) releases a cine-channel shot **instantaneously,
  and there is no release-blend cvar** — `elysium.CameraShotReleaseSeconds` is **not created**. The
  release is a transition: retail returns control on the same tick the camera dies, so a blend-out
  would compose a dead camera over a player who already has input and would manufacture a
  "cine blending out while a track ramps in" state the exclusion rule below makes impossible. The
  invented `BlendSeconds = 0.5` at `ElysiumCameraShots.cpp:469` and the dialogue's
  `BlendOutSeconds = 0.25` (`ElysiumDialogueCamera.cpp:95`) both go.
- **The ramp encoding is retail's (M5, ruled).** The three regimes of `FUN_100fc900`'s tail become the
  stored state — `startTime ≤ 0` ⇒ off, `0 ≤ duration ≤ 0.0039` ⇒ weight 1 immediately and it stays,
  `duration < 0` ⇒ 1 decaying over `|duration|`. `Pop(Id, BlendOutSeconds)` survives only as a
  **façade that writes those fields**; SC3 owns the machine behind it.
- **`bOffCenter` (M11).** `FMinimalViewInfo::OffCenterProjectionOffset` driven by
  `elysium.CameraOffCenterX` / `…Y`, with retail's `(−x·0.5, −y·0.5, x·0.5, y·0.5)` rect arithmetic.
- **The spectator replace.** `CalcView`'s last arm — origin and angles hard-replaced by the spectated
  entity — is the death/observer view and is wired to the port's observer seam; **RC10** names
  `vfunc39` so the "above the max client" test is reproduced rather than guessed.
- **The vehicle arm (M10)** is ruled dead; the ordering fact is a comment at the compose site.

**Tests — `Elysium.Substrate.CameraCompose`** (new):

- A point lerp and a rotator lerp over a 90° delta trace **measurably different** arcs; the point lerp
  matches `VectorAngles(lerp(fwdPoint, target, e) − lerp(origin, shotOrigin, e))` to a float epsilon
  at e ∈ {0.25, 0.5, 0.75}.
- `SimpleSpline` applied at compose and the ramp itself linear: weight 0.5 composes at e = 0.5, and
  the ramp reaches 0.5 at exactly half the duration.
- `roll = e × shotRoll` discards a banked base roll (base roll 20°, shot roll 0°, weight 1 ⇒ 0°).
- A live cine shot suppresses the boom contribution entirely (rig boom 250 cm, composed origin equals
  the shot origin).
- A track override at weight 0.5 over a live cine base lerps **from the cine pose**, not from the rig's.
- **A released shot cuts: the next frame is the player view.** Popping a live cine shot leaves zero
  residual weight on the frame it fires, the composed view equals the rig's on the next frame, and no
  cvar exists that could soften it (asserted by name — `elysium.CameraShotReleaseSeconds` must not be
  registered).
- Adopting a cine camera clears a live track lease and vice versa (extend
  `Elysium.Substrate.CameraTrack`'s temporal-cut cluster, `ElysiumCameraTests.cpp:1282+`).
- The off-centre rect from the two cvars.

**Docs.** `camera-view-modes.md` §"The scripted-shot channel" gains the two-channel split and the
composition shape; the divergence table's five composition rows resolve; **M1**, **M5**, **M10** and
**M11** are recorded beside the faithful behaviour.

**Rulings applied.** **M1** — **reversed**: reproduce the cut, and create no release-blend cvar.
**M5** — **reversed**: the signed-duration regimes are the stored state, `Pop` is a façade over them.
**M9** — **reclassified out of the register**: the ease and the point-lerp shape are contract, not a
modernization, because the authored `FromPlayerTime` values were tuned against retail's solve; they
land together with the `uv run elysium debug shots` vantage baseline refreshed in the **same commit**,
nothing else riding along. **M10** — dead code, not ported; the ordering comment stays. **M11** — the
behaviour is ported, only the cvar names diverge, and only until RC9. All ruled 2026-09-07 under the
owner framework of §7. **Deps:** SC1; RC9 (`_DAT_10235278`), RC10 (`vfunc38/39/40`).

**Closure record.** Landed 2026-09-07, **with RC9 and RC10 in hand** — which corrected three of the
numbers this slice was specified against. The corrections, first, because they change what landed:

* **`_DAT_1022b298` is 240.0f, not 100.0f.** The stand-in aim point is `origin + fwd × 240 u`
  (609.6 cm), not 254 cm. The spec above and §7's M9 row still say 100; the code and
  `camera-view-modes.md` carry 240.
* **The ramp's dead band is symmetric ±0.01 s**, not `0 ≤ dur ≤ 0.0039`: `_DAT_101e34e8` = `+0.01`
  and `_DAT_10235278` = `−0.01`, so a *negative* duration inside the band is a cut in, not a blend
  out. There is no `0.0039` anywhere.
* **M11 is not off-centre projection.** The block is Source's **orthographic debug view** —
  `CViewSetup::m_bOrtho` (`+0x16`) and the rect `+0x18..0x24` from `c_orthowidth` / `c_orthoheight`
  (`FCVAR_ARCHIVE`, default `100`), enabled by `CInput+0x1b8` and the `camortho` command. No
  `elysium.CameraOffCenterX/Y` was created and `OffCenterProjectionOffset` is untouched; the port
  drives `FMinimalViewInfo::ProjectionMode` + `OrthoWidth` from the two **VtMB-store** cvars.

**Composition.** `Public/ElysiumCameraSolve.h:33-56` — `ViewForwardPointUnits` 240 /
`ViewForwardPointCm`, `ScriptedShotTargetPoint`; `:704-737` the two `ComposeScriptedShot` overloads
(point-and-roll, and the rotation convenience that derives the target). Impl
`Private/Player/ElysiumCameraSolve.cpp:207-260`: `e = SimpleSpline(w)` at compose, origin lerp,
`viewFwdPoint = origin + fwd × 240 u`, target-point lerp, `VectorAngles` of the lerped direction,
`roll = e × shotRoll` as an **assignment**, FOV lerp. The rotator lerp is gone. Comments corrected at
`Public/ElysiumCameraComponent.h` (the published view) and `ElysiumCameraModifiers.cpp:55-66`.

**The channel split.** `FElysiumCameraShot::bCine` (`ElysiumCameraSolve.h:270-289`), a second axis
beside `bTracked`. `FElysiumCameraShotStack` gains `TopCine()` / `TopCineId()` / `TopTrack()` /
`TopTrackId()` / `GetTrackWeight()` (`:461-560`, impl `:628-820`); `GetWeight()` answers 1 while a cine
shot is adopted. `FElysiumCameraDirector::Resolve` stamps `bCine = true` and `BlendSeconds = 0`
(`ElysiumCameraShots.cpp:875-881`) — the invented 0.5 is deleted.

**The two-branch apply.** `ElysiumCameraComponent.cpp` — `SolveShot` follows the cine shot first
(`:175-215`); `ApplyBaseToView` **skips the boom** while a cine shot is live (`:281-292`);
`ApplyScriptedShotToView` hard-writes the cine pose and then composes the track override over it
(`:352-400`), with M10's vehicle-ordering fact as a comment there; `ScriptedShotView` publishes both
channels (`FElysiumScriptedShotPose`, `ChannelWeight()`). `UElysiumCameraService::ApplyToView` takes
the same two branches (`ElysiumCameraService.cpp:223-265`).

**Mutual exclusion.** `SelectTrackCameraRole` opens with `ClearScriptedCamera()`
(`ElysiumEntityWorld.cpp:1035-1044`, retail's `FUN_1017d280`) and `SetScriptedCamera` clears the track
leases with no blend (`:997-1020`).

**M1 — the release is a cut.** `FElysiumCameraShotStack::Pop` returns immediately for a cine shot and
never consults `BlendOutSeconds` (`ElysiumCameraSolve.cpp:744-776`). **No
`elysium.CameraShotReleaseSeconds` cvar was created**, asserted by name in the tests.
`ElysiumDialogueCamera.cpp:94-102` — `BlendOutSeconds` 0.25 → 0.

**M5 — the ramp encoding.** `RampStartTime` / `RampDuration` / `RampNow` with retail's four regimes
(`GetTrackWeight`, `:800-820`) and `RampDeadBandSeconds = 0.01`. `ArmRampIn` / `ArmRampOut`
(`:676-712`) back-date the start rather than writing the weight (`FUN_1017d0b0`); two entry points
rather than one signed argument because `-0.0f >= 0.0f` is true. `Advance` only moves the clock.

**M11 — `camortho`.** `c_orthowidth` / `c_orthoheight` declared at `100` in `ElysiumCam::CvarDefs()`
and loaded to cm; the `camortho` verb toggles `UElysiumCameraComponent::bOrthographic`
(`ElysiumCameraComponent.cpp:820-833`) and `ApplyBaseToView`'s tail writes the projection.

**The spectator replace.** `FElysiumSpectatedView` + `SetSpectatedView` / `ClearSpectatedView`, applied
at the tail of `ApplyBaseToView`: origin and angles replaced, FOV untouched, ahead of the cine write.
**Nothing writes it** — the port has no observer, death-cam or spectator producer, so it is a seam
standing for `IVRenderView::GetViewEntity()` (slot 39, RC10) with the `index > GetMaxClients()` test
made by whoever eventually fills it.

**SC1 corrections landed here** (RC9): `FrameDeltaFloorThreshold` deleted — the floor's compare
constant and its stored literal are the same `0.01` — and **M12 closes**: the guard is retail's
`camera_fov` (default `-1`, threshold `10.0`), declared in the VtMB console store, loaded into
`FElysiumCameraCvars::CameraFov` and passed to `TrackFov` / `Advance` as a parameter. The
`elysium.CameraShotFovOverride` cvar is deleted.

**Tests.** New `Private/Tests/ElysiumCameraComposeTests.cpp` —
`Elysium.Substrate.CameraCompose`, covering every bullet of the spec's list. One case appended to
`ElysiumCameraTests.cpp`, `Elysium.Substrate.CameraTrack.ChannelExclusion`; the existing
`Elysium.Substrate.CameraTracker` FOV-guard and dt-guard cases were edited for the two SC1
corrections and nothing else.

**Not done in this session, owner actions:**

* **M9's vantage baseline.** The composition shape changes the arc of every scripted arrival, so
  `uv run elysium debug shots` must be re-run and its baseline promoted — an RHI run, out of scope
  here. Until it is, the shots diff will report every scripted vantage as changed.
* **The dialogue *profile* ladder is still a value shot.** `bCine` is stamped in
  `FElysiumCameraDirector::Resolve`, so a `vdata/camerashots/` dialogue shot is a cine hard cut while
  an authored `ElysiumDialogueCamera` profile (which has no file behind it) keeps the Service's
  0.35 s blend-in. SC9 moves dialogue onto the adoption slot and retires that ramp.
* **The `camortho` verb is only reachable on a live component** (`RegisterCommands` runs at
  `BeginPlay`), so the test drives `SetOrthographic` directly and asserts the cvar declaration and
  conversion separately.
* The existing `Elysium.Substrate.Camera` compose cluster's comment still says "the rotator lerp takes
  the short way round"; its assertion holds under the point lerp (a symmetric ±170° pair still meets
  at 180°) and the comment was left alone under this session's file discipline.

---

### SC3 — The camera-override fade machinery and the crossfade stack

**Goal.** The server half of the `camera_track` channel: one signed global weight over a per-channel
stack of outgoing cameras, published once per frame. The port has the mid-blend reversal and nothing
else.

**The retail chain.** Unreplicated state on `CBasePlayer`: `+0x19b4` the cine EHANDLE, `+0x19b8`
`m_flCameraOverrideFadeMarkTime`, `+0x19bc` the duration **whose sign is its direction**,
`+0x19c0/c4/c8` the view entity / set time / crossfade, `+0x19cc/d0/d4` the same for the target, and
`+0x19d8`/`+0x19e4` a `CUtlVector` of outgoing entries, stride `0x10`:
`{ byte kind (0 view, 1 target), EHANDLE, setTime, crossfadeDuration }`.

- **`FUN_1017d900` `GetCameraOverrideWeight`** — `mark > 0 && (viewEnt live || targetEnt live)`:
  `dur == 0` ⇒ `1.0`; `dur > 0` ⇒ `clamp((t − mark)/dur, 0, 1)` (fade **in**); `dur < 0` ⇒
  `clamp(1 + (t − mark)/dur, 0, 1)` (fade **out**). Otherwise it **clears the whole channel**
  (`mark = 0`, `dur = 0`, both handles `-1`, the list emptied) and returns 0 — **the getter is the
  reaper**, so the state is collected lazily on the next query.
- **`FUN_1017d280(player, ent, crossfade)`** — `SetCineCamera(NULL)` first; clamp a negative crossfade
  to 0; when `curtime > +0x19c4 && handleLive(+0x19c0)`, **`push_front`** the outgoing camera as a
  kind-0 entry; `crossfade = max(crossfade, ent->vfunc0xD0())`; `FUN_1017d0b0(this, crossfade)`; store
  handle/time/duration; `ent->vfunc0xBC()` to notify. A null entity routes to `FUN_1017d6d0`.
- **`FUN_1017d0b0(player, dur)`** — fresh (`mark <= 0`): `mark = t`, `dur` as given. Reversing a
  fade-out (`+0x19bc < 0`): `+0x19bc = dur; mark = t − w*dur` — **back-dating**, which is how retail
  achieves a symmetric mid-blend reversal without a separate weight variable.
  **Corrected against the listing `0x1017d0b0`–`0x1017d21d` (`rc_group_bc.md`, RC7/RC8 pass):** the
  argument is clamped to `max(dur, 0)` before anything else (`0x1017d0c9`, absent from the
  decompile); the fade-in re-time fires when `(mark + duration) < (t + dur)` — `0x1017d17d`'s
  `FCOMPP`/`TEST AH,0x5`/`JP` falls through on *less than* — so a short fade in flight is
  **lengthened**, not shortened, and a longer one is left alone; and the zero-duration arm upgrades
  to a fade-in only while `t − ε <= mark` with **`ε = _DAT_10450aa4 = 0.00999999977`**, i.e. only
  while the snap is less than 10 ms old, not "already elapsed".
- **`FUN_1017d6d0(player, dur)`** — no-op at weight ≤ 0; `dur = max(dur, viewEnt->vfunc0xD4())` and
  the same for the target — each end can demand a **minimum** crossfade out; `dur <= 0` ⇒ hard-clear
  the mark (an instant snap back); else `+0x19bc = −dur`, `+0x19b8 = curtime − (1 − w)*dur`.
- **`CHL2_Player::SetupVisibility` `0x10352120`** composes **once per server frame**, not in a think:
  copy the mark and the signed duration into the replicated pair; when the weight is positive, stamp
  the timestamp, read the view entity's origin (`vfunc0xC8`), FOV (`vfunc0xC4`) and roll (`vfunc0xC0`)
  and the target entity's aim point (`vfunc0xCC`, asked to aim **from** the published view origin, or
  from `EyePosition()` when there is no view entity), compute each channel's own fraction
  `clamp((now − setTime)/crossfadeDuration, 0, 1)` (or 1 when the duration is ≤ 0), then walk the fade
  list **newest first**: drop entries whose channel has reached full coverage or whose entity has
  died; otherwise pull the published value back toward that camera and
  accumulate `w[kind] = 1 − (1 − f)(1 − w[kind])`.
  **Corrected (`rc_group_bc.md` RC8):** the pull-back multiplier is the **channel's accumulated
  weight**, never the entry's own fraction. `0x103524c9` / `0x10352542` / `0x1035258b` all
  `FMUL [ESP+0x1c]` (`w[0]`) for origin, roll and FOV, and `0x103525e1` `FMUL [ESP+0x20]` (`w[1]`)
  for the target point; `f` at `[ESP+0x10]` appears exactly once in the whole loop, in the coverage
  accumulator at `0x10352633`. Both weights start at 0, so with no live view entity every queued
  view entry folds the published value all the way back to its own camera.
- **Retail defect, reproduced:** both pushers write the kind byte `0` (`0x1017d386`, `0x1017d55f`),
  so the fold's target arm is unreachable and a superseded *target* camera crossfades through the
  *view* arm — dragging the published FOV toward the `CBaseEntity` default 75 and the roll toward 0,
  because `CBaseCombatCharacter` overrides neither slot. Row in `retail-defects.md` §7.
- **The replication table** is in §4's M4 row; the client's local block is at `player+0x1658` and its
  offsets run **4 bytes higher than the server's `DT_Local` offsets throughout**.

**The design in the port.**

- A new `Private/Substrate/ElysiumCameraOverride.{h,cpp}` — one class,
  `FElysiumCameraOverrideChannel`, holding the mark/signed-duration pair, the two channel slots
  (entity, set time, crossfade) and the outgoing entry list. Substrate rules hold: no `GetWorld`,
  `DeltaTime` by parameter, entity access through `FElysiumWorldServices`.
- `GetWeight()` reproduces the three arms **and the lazy reap** — the getter clears the channel when
  both ends are dead, exactly as retail, because the reap order is observable (a query on the frame
  both entities die returns 0 *and* leaves the channel clean for the next push).
- `SetViewEntity` / `SetTargetEntity` / `FadeOut` reproduce `FUN_1017d280` / `FUN_1017d460` /
  `FUN_1017d6d0`, including `max()` against each entity's **minimum crossfade**.
  **Corrected (`rc_group_bc.md` RC7): `FUN_1017d460` is not a twin of `FUN_1017d280`.** It does not
  call `SetCineCamera(NULL)` — only setting the *view* entity cancels a cine camera — it works the
  `+0x19cc/d0/d4` trio, and it notifies through vtable `0xB8` (become-target) where the view setter
  notifies through `0xBC` (become-view). **And the minimum crossfade is a virtual pair, not the
  authored keyvalues**: `GetCameraFadeInTime` (`0xD0`) / `GetCameraFadeOutTime` (`0xD4`). A
  `camera_track` answers `max(0, FromPlayerTime)` / `max(0, ToPlayerTime)`; a combat character
  answers ONE unclamped runtime field, `m_flCameraOverrideFadeTime` (`+0x10d0`), for both
  directions; anything else answers 0. So the plan's "the port already routes the authored
  `FromPlayerTime`/`ToPlayerTime`, this slice only makes the `max()` explicit" was right for
  `camera_track` and wrong for an NPC target — the query has to be virtual on the entity.
- `SetAsCameraTarget` (`0x1000a2d6`) and its four entity inputs — `SetHeadAsCameraTarget`,
  `SetBodyAsCameraTarget`, `FadeHeadAsCameraTarget`, `FadeBodyAsCameraTarget` — exist on **every**
  `CBaseCombatCharacter` and had no counterpart in the port. It broadcasts to every player index
  with a crossfade argument of exactly `0.0`, so the entity's own `GetCameraFadeInTime()` is the only
  source of a non-zero target crossfade in shipped content. Head → the look point; body →
  `WorldSpaceCenter()`.
- `Arm(dur)` reproduces `FUN_1017d0b0`'s **back-dating** re-time verbatim. The port's existing
  symmetric-reversal assertion (`ElysiumCameraTests.cpp:192-208`) is re-pointed at it.
- **The N-entry crossfade stack** is built, not a single-entry stand-in: `Publish(...)` folds the list
  newest-first with multiplicative coverage, over the origin, the roll, the FOV and the target point.
  **RC8** settles which register each of the three components uses before this lands, and **RC7**
  settles the kind-1 pusher and the `vfunc0xBC..0xD4` identities.
- `FElysiumEntityWorld::PublishTrackCamera` (`ElysiumEntityWorld.cpp:1040-1115`) becomes this channel's
  single caller, keeping the composed track shot **direct** (`bTracked` false, `:1086-1094`).
- **M4**: the ±10 s fade-duration clamp is preserved as a validation clamp on the authored times, with
  a warning naming the retail SendProp range; the bit widths are not reproduced.
- **M13**: no PVS. The consequence is recorded in the doc, not built.

**Tests — `Elysium.Substrate.CameraOverride`** (new):

- The three weight arms: `dur == 0` ⇒ 1; `dur > 0` ⇒ the linear ramp in; `dur < 0` ⇒ the ramp out.
- The **lazy reap**: with both entities dead, `GetWeight()` returns 0 *and* the channel is clean —
  a subsequent push behaves as fresh, not as a reversal.
- **The fade weight reverses mid-blend by back-dating the start.** Reverse a fade-out at w = 0.4: the
  stored `mark` moves to `t − w·dur` (never a separate weight variable), the weight is continuous
  across the reversal frame, and it reaches 1 in exactly `0.6 × dur`. Asserted on the stored fields,
  so an equivalent-but-different mechanism fails the test.
- The sign **is** the state: a push writes a positive duration, a release writes a negative one, and
  the getter reads the direction off it — `Pop(Id, BlendOutSeconds)` is only a façade that does so.
- Per-entity minimum crossfades: an entity demanding 1.0 s raises a 0.2 s request on both push and
  release; `FUN_1017d6d0`'s `dur ≤ 0` hard-clears the mark and snaps back instantly.
- The stack: three outgoing view cameras crossfading at once fold newest-first, and coverage
  accumulates as `1 − Π(1 − f_i)`; an entry whose entity dies mid-fold is dropped without disturbing
  the others.
- The encoder clamps hold as contract (**M4**): FOV to `[0,180]`, roll to `[−180,180]`, the authored
  fade duration to `±10 s`, each with a warning; no shipped value reaches any of them.
- Adopting a cine camera clears the channel (shared with SC2's mutual-exclusion assertion).

**Docs.** `camera-view-modes.md` §"The `camera_track` override channel" gains the reap order, the
kind-1 pusher (once RC7 lands), the resolved `+0x1f44`/`+0x1f48` identity (once RC8 lands), and the
**M4**/**M13** rulings.

**Rulings applied.** **M4** — the **bit widths** are skipped (pixel-only, no wire) and the **encoder
clamps are kept as contract**: FOV `[0,180]`, roll `[−180,180]`, fade duration `±10 s`, warning when
one bites; the corpus never reaches them (max `FromPlayerTime` 1.0, `ToPlayerTime` 0). **M5** —
**reversed**: this slice ports the fade state machine **verbatim** rather than an equivalent — the
signed-duration fields, the lazy reap, the back-dated re-time, the per-entity minimums, `dur ≤ 0 ⇒
instant snap`, the N-entry stack and `FUN_1017d280`'s `SetCineCamera(NULL)`. **M13** — Unreal culls
from the actual view, so the PVS half is not applicable and its consequence is recorded, not built.
All ruled 2026-09-07 under the owner framework of §7. **Deps:** SC2, RC7, RC8.

**Closure record.** Landed 2026-09-07.

`Private/Substrate/ElysiumCameraOverride.{h,cpp}` — `FElysiumCameraOverrideChannel` plus the two
interfaces the substrate rules require (`IElysiumCameraOverrideSource`, retail's slots 46-53 with
the `CBaseEntity` defaults as its own; `IElysiumCameraOverrideResolver`, the handle table and the
cine slot). `GetWeight` is `FUN_1017d900` including the lazy reap and its exact clear set;
`SetViewEntity` / `SetTargetEntity` are `FUN_1017d280` / `FUN_1017d460` with the cine clear on the
view setter only and the two distinct notifies; `Arm` is `FUN_1017d0b0` read off the listing, with
the entry clamp, the lengthen-not-shorten re-time and the 10 ms `_DAT_10450aa4` dead band; `FadeOut`
is `FUN_1017d6d0`; `Publish` is `SetupVisibility`'s fold, N entries newest-first, every value pulled
by the channel's accumulated weight and `f` used only for coverage. The kind-byte defect is
reproduced at `PushOutgoing` with both addresses named, and the fold's kind-1 arm is kept as
dead-but-present code.

`camera_track` implements the source (`ElysiumCameraTrack.cpp`): the runtime pose block
`m_vecViewPos` / `m_vecTargetPos` / `m_flViewFOV` / `m_flViewRoll`, filled by the sampler, and
`max(0, FromPlayerTime)` / `max(0, ToPlayerTime)` for the two minimums. `FElysiumCombatCharacter`
implements it (`ElysiumPlayer.h`, `ElysiumCombatCharacter.cpp`) with the single
`m_flCameraOverrideFadeTime` answering both directions, `m_bCameraTargetIsHead` selecting the look
point or the world-space centre, and `SetAsCameraTarget` broadcasting with crossfade 0; the four
entity inputs replace their `ELYSIUM_PENDING_INPUT` rows in `ElysiumPlayerClasses.cpp` and the two
fields are registered under retail's datamap names. `FElysiumEntity::GetCameraOverrideSource` is the
no-RTTI hop the resolver uses. `FElysiumEntityWorld::PublishTrackCamera` is the channel's single
driver, the shot stays direct, and `SetCameraOverrideTarget` is `SetAsCameraTarget`'s broadcast hop.

**Rulings as landed.** **M4** — bit widths skipped, encoder clamps kept as contract at the
publication point (FOV `[0,180]`, roll `[−180,180]`, replicated fade `±10 s`), each warning and
naming its SendProp; the unreplicated duration keeps what was armed. **M5** — reversed, as planned:
the state machine is verbatim rather than equivalent. **M13** — no PVS; the artefact it would have
reproduced (entities off-lens stalling during a shot) is recorded in `camera-view-modes.md` and
deliberately not built.

**Two named port constructs with no retail counterpart**, both stated in the code and in
`camera-view-modes.md`: `ReleaseSlot`, because this port leases the position and target roles
independently where retail's only per-slot writer is a replacement; and a **zero-blend**
`ClearTrackCamera` reaping the channel outright rather than letting an entity's `ToPlayerTime`
extend a teardown past the frame the shot was popped on (ruling M1 / SC2 make that edge a cut).

**Tests.** `Elysium.Substrate.CameraOverride` (world-free) and
`Elysium.Substrate.CameraOverrideCharacter` (headless world), `Private/Tests/ElysiumCameraOverrideTests.cpp`.
`ElysiumCameraTests.cpp:192-208` keeps its first/third assertions and its retail citation now points
at `Arm` rather than claiming to be the override reversal.

**What remains.** `CSceneEntity::DispatchStartEvent` `0x10082ee0` — `SetAsCameraTarget`'s fifth
caller — is **not** wired, and the reason is now specific rather than a shrug. Reading the switch out
in full (recorded in `camera-view-modes.md`): event type `0x10` (the port's `CameraMove`) resolves
param1 to the view entity and param2 to the target entity, calls `SetAsCameraTarget` on the target's
combat character and then nulls its own target pointer so the following per-player broadcast pushes
only the view; type `0x12` (`CameraRestore`) broadcasts `FUN_1017d6d0`; and type `0x11`
(`CameraShot`) falls to `default:` with no handler at all — which corroborates
`ElysiumSceneData.h`'s existing comment. The hook exists (`IElysiumScenePlayer::StartEvent`), but the
arm reads a boolean and a float off the `CChoreoEvent` record that `FElysiumSceneEvent` does not
parse, and **all ten non-live event types have zero authored uses**, so there is no instance to check
a guess against. That is the whole gap: one caller, two unparsed event fields, zero shipped
instances. The four entity inputs are the other four callers and they are wired. The channel is also driven only from
`PublishTrackCamera`, so an NPC pushed onto the target slot with no `camera_track` holding the
position role publishes nothing until one does; retail composes unconditionally from
`SetupVisibility`, which is the per-frame compose pass SC2 owns.

---

### SC4 — `camera_cinematic`: the director entity, the adoption slot, `CamMode`, and the immobilize pair

**Goal.** Retire the stub at `Private/Substrate/ElysiumStubClasses.cpp:46-47` and land the entity, its
five-mode dispatcher, the two-entity director/runtime split, retail's single adoption slot with its
destroy rule, `point_player`, `m_bDrawPlayer` and the same-tick teardown.

**The retail chain.**

- **`InputStartShot` `0x10070720`** ignores its `inputdata` entirely — the activator is always
  `UTIL_PlayerByIndex(1)` — and calls `FUN_10070780(director, player)`.
- **The director is a second entity.** The map-placed `camera_cinematic` holds the keyvalues and never
  becomes the view. Director fields, with the datamap key names **read, not inferred**
  (`vtmb_string` over `vampire.dll`): `+0x5d4` `shotname` / `m_sShotName` (`10546c38`/`10546c44`);
  `+0x5d8` `startent` / `m_sStartEnt` (`10546c1c`/`10546c28`); `+0x5dc` `endent` / `m_sEndEnt`
  (`10546c08`/`10546c10`); `+0x5e0` `target1` / `m_sTarget1` (`10546bec`/`10546bf8`); `+0x5e4`
  `target2` / `m_sTarget2` (`10546bdc`); `+0x5e8` `point_player` / `m_bForcePlayerLook`
  (`10546ba8`/`10546bb8`); `+0x640` `m_bDrawPlayer` (`10546dc8`, the SendProp name).
- **`FUN_10070780`, in order**: four `FindEntityByName(NULL, name, 0, 0)` lookups with `&DAT_106b8540`
  (the empty string) substituted for a null keyvalue — a miss yields NULL and leaves that anchor to the
  shot file's own `Position`; `cam = GetCineCamera()` (`FUN_1017cf90`, needs `+0x1ec4 > 0` **and** the
  `+0x19b4` handle live); **no camera** ⇒ `FUN_10070470(shotname, e0..e3)` then
  `cam->m_hSubject (+0x5d0) = player`; **camera present** ⇒ `SetShot(shotname, 1, player)`, then
  `SetShotAnchorEntity` for each non-null entity, then `FUN_1006e8e0` — and this branch **ignores
  `SetShot`'s return value**, so a bad name leaves `CamMode 0` / `m_ShotIndex -1` and the camera goes
  idle rather than falling back; NULL ⇒ `DevWarning("%s could not start shot properly\n")` and return;
  copy `m_bDrawPlayer`; `FUN_1017cef0(player, cam)`; `FUN_1015ef40(player)`.
- **`FUN_10070470(name, e0..e3)`** — `Create("camera_cinematic", vec3_origin)`; `+0x204 |= 0x4`
  (*disposable*); `SetShot(name, 1, NULL)` — the **only** failure path, which `UTIL_Remove`s the new
  entity and returns NULL; `SetShotAnchorEntity` ×4; `FUN_1006e8e0`.
- **`SetShot` `FUN_1006e130(name, camMode, subject)`** — clear the mode first (`FUN_1006e0e0`);
  normalize the name (*if it contains `.txt`, `Q_FileBase` into a 32-byte buffer*, else verbatim — the
  table lookup supplies case-insensitivity); look it up in `&DAT_106c8298`; store
  `m_ShotIndex (+0x630)`; **return 0 with the mode still 0** when unknown; store `m_hSubject (+0x5d0)`
  (the passed subject, else player 1; NULL fails); resolve each anchor's `Position` keyword to an
  entity; stamp `m_nClientResetFrame (+0x63c)`; write `CamMode (+0x638)` **last**.
- **`FUN_1006e8e0`** is a shot **start**, not a think: it is in no vtable and never handed to
  `ThinkSet`; all six call sites are "a shot has just been set". It stamps `m_nClientResetFrame`,
  `SetAbsOrigin`/`SetAbsAngles`, `Relink`, fills the shot-start anchor cache at `this+0x598+i*0xc`,
  and sets the origin selector `+0x594 = 0` (and zeroes `+0x5c8`) when the **End** handle is live. Its
  `+0x564`/`+0x570`/`+0x588` triple is **RC3**.
- **Adoption `FUN_1017cef0(player, cam)`** — `+0x1ec4 = IndexOfEdict(cam->edict)`
  (`m_Local.m_iCameraOverrideIdx`, DT_Local `+0x84`) and `+0x19b4 = cam->GetRefEHandle()`, or 0 and
  `-1` for NULL; then `UTIL_Remove(old)` when `old != cam && old != NULL && (old->+0x204 & 0x4)`. **A
  map-placed director never carries the flag and survives its own `StartShot`.**
- **`FUN_1015ef40` is `SetImmobilized(true)`.** `player+0x19f7` is the replicated `m_bIsImmobilized`
  (SendProp `FUN_10179840`, 1 byte, 1 bit); readers are `CPlayerMove::SetupMove` `0x10186120`,
  `CGameMovement::CheckJumpButton` `0x101226b0`, `CGameMovement::Duck` `0x10126fd0`,
  `CBaseCombatWeapon::ItemPostFrame` `0x10253ea0` and `CWeaponMelee::ItemPostFrame` `0x103eaec0`. So
  starting a shot freezes **movement, jump, duck and weapon use** and changes no view state.
- **`InputEndShot` `0x10070750` → `FUN_10070990`** — `FUN_1006e0e0` on the **director**;
  `ThinkSet(NULL)`; `SetCineCamera(player, NULL)` (which destroys the runtime camera);
  `SetImmobilized(false)`; `FUN_101815b0(player, 1)` and `(player, 8)` — a plain
  `player->+0x1d60 &= ~mask` (**RC4**); `UTIL_Remove(this)` if the director is itself disposable.
  **No blend.**
- **The think dispatcher `FUN_1006e770`** is a jump table on `CamMode`: 0 → `ThinkSet(NULL)`;
  1 → `0x1006f8f0`; 2 → `0x1006fde0` (**empty**); 3 → `0x1006fe00`; 4 → `0x1006f870`; >4 →
  `CamEndThink` `0x1006e850` (`UTIL_Remove` plus a reschedule) **only if `+0x55c > 0 && curtime >
  +0x55c`**. It always ends with `m_flNextThink = curtime + _DAT_1044eb04`.
- **The 24 Hz prologue `FUN_1006f7d0`** — `_DAT_1044eb04 = 0.04165999963879585` (1/24 s, read from the
  image); the expiry `+0x55c` hands off to `CamEndThink` when positive and past. Its only writers are
  `FUN_1006e8b0` (called by the **mode-4** think with `0.0`) and the mode-3 think, which zeroes it —
  so **a mode-1 shot never expires on its own**.
- **The mode-1 think `0x1006f8f0`.** `flags = FUN_1006edb0`, `rec = FUN_1006ede0`,
  `origin = GetAbsOrigin()`, `angles = GetAbsAngles()`, `FUN_1006f670(&lookAt)`;
  `sel = +0x594`; `sel != 2` ⇒ `sel == 1` ⇒ `origin = anchorPos(0)` (Start), `sel == 0` ⇒
  `origin = anchorPos(1)` (End), then `AutoPositionFromTarget` on `flags & 0x20` (SC7);
  `if (rec->+0xd4 > 0) { base = GetOrigin() /* vfunc 0x370, LOCAL */; VectorAngles(lookAt − base,
  angles); }`; publish `m_vecCamOrigin (+0x5ec)`, `m_vecCamTarget (+0x5f8)`, `m_angCamAngles (+0x604)`,
  `m_flFOV (+0x634) = rec->FieldOfView` **every tick**, `+0x5cc = curtime`; then
  `if (m_bForcePlayerLook (+0x5e8) && subject live) FUN_10178590(subject, lookAt)`.
- **`point_player` turns the *subject*, never the camera.** `FUN_10178590(ent, point)` takes
  `EyePosition()` (vfunc `0x304`), normalizes `point − eye`, `VectorAngles`, and hands the result to
  `FUN_10178550`, which writes a pending eye-angle snap at `+0x206c..0x2074` and raises `+0x207c`. It
  fires **every tick of the shot**. `+0x5e8` is server-only (not in `DT_BaseCineCam`), written by the
  keyvalue and cleared by anim event 4050. **35 of the 51 shipped cinematics author the key**
  (26 zero, 9 one).
- **`m_bDrawPlayer` has no server reader.** SendProp `FUN_1006d2f0`; written only by `FUN_10070780`
  (the director's keyvalue) and by anim event 4050 (forces 1). **No shipped map authors a
  `drawplayer`-shaped key**, so its only non-zero writer in the whole game is 4050.
- **Mode housekeeping `FUN_1006e0e0`** sets all four anchor handles and bone indices to `-1`,
  `m_ShotIndex = -1`, `CamMode = 0`, and **does not touch** `+0x55c`, `+0x594`, `+0x5e8` or `+0x640`.
- **Teardown `FUN_10071970`** — for each `camera_cinematic`, the full `EndShot` when active, then
  `UTIL_Remove` unconditionally; then `UTIL_Remove` every `camera_track`.
- **Corpus.** 51 shipped `camera_cinematic` entities; authored keys `spawnflags` ×51, `target1` ×51,
  `shotname` ×51, `origin` ×51, `endent` ×49, `startent` ×46, `target2` ×42, `point_player` ×35,
  `StartHidden` ×5. `shotname` is authored both as `vdata/CameraShots/X.txt` (43×) and bare (8×), so
  the `.txt` → `Q_FileBase` normalization is load-bearing. `spawnflags` values 0, 1, 3, 5 and 7 are
  all shipped and their bit meanings are **RC2**.

**The design in the port.**

- New **`Private/Substrate/ElysiumCameraCinematic.{h,cpp}`** — one class, `FElysiumCameraCinematic`,
  the director entity: the seven keyvalues, `StartShot`, `EndShot`, the disposable flag and the
  `CamMode` dispatcher. Registered from the same registration site the stub row occupied; the stub row
  is deleted.
- **`CamMode` becomes a real enum.** `Public/ElysiumCameraSolve.h:265-273`'s `bTracked` bool is the
  `CamMode == 1` test today; it stays as the tracker's contract, and the entity gains
  `EElysiumCineCamMode { Idle, NamedShot, OnRails, FollowEntity, Animated }` with all five think arms
  (§4). `bTracked` is derived from the mode at the one place the shot is built, so no reader changes.
- **`FElysiumCameraDirector`** (`Private/Player/ElysiumCameraShots.h:173`) grows
  `PushFromDirector(ShotName, E0..E3)` and `SetShotAnchorEntity(Index, Entity)`, so the four resolved
  entities override the shot file's `Position` exactly as `FUN_1006ef50` does. `Resolve`
  (`ElysiumCameraShots.cpp:428-479`) gains the "an externally supplied entity wins" path it does not
  have at all today.
- **The adoption slot.** `FElysiumEntityWorld::SetScriptedCamera` / `ClearScriptedCamera`
  (`ElysiumEntityWorld.cpp:997-1023`, "replaces the first rather than stacking" at `:1004-1005`)
  becomes retail's slot: it carries the **disposable** bit and destroys the outgoing camera **only**
  when it owns it. The terminal moves onto it (**M8**), retiring its own stacked handle
  (`Private/Substrate/ElysiumTerminal.h:344-347`, pushed at `.cpp:644-651`) — retail's `CFuncMonitor`,
  `CPropHacking`, `CPropKeypad` and `FUN_10225070` all reach the same slot through
  `FUN_10070470`/`FUN_1017cef0`.
- **Immobilize.** `StartShot` calls `FElysiumPlayer::SetImmobilized(true)` (`Public/ElysiumPlayer.h:1815`)
  and `EndShot` clears it, joining the terminal (`ElysiumTerminal.cpp:637`/`:736`) and
  `events_player.ImmobilizePlayer` (`ElysiumEventClasses.cpp:89`) on the same mechanism. The four
  retail consumers (move, jump, duck, weapon) are asserted individually.
- **`point_player`** lands as what it is: a per-tick call into the port's existing gaze arm that writes
  a pending eye-angle snap on the **subject**, named for `FUN_10178590` → `FUN_10178550`. The camera is
  untouched.
- **`m_bDrawPlayer`** joins `FElysiumShotPresentation` (`Public/ElysiumCameraSolve.h:161-174`) as
  `bDrawPlayerBody`, defaulting to the director's keyvalue (always 0 on shipped maps); SC8 supplies its
  only non-zero writer. SC5 consumes it.
- **The teardown is a cut (M1, ruled).** `EndShot` clears the shot on the same frame with no residual
  weight and no cvar behind it, and `SetImmobilized(false)`, the `+0x1d60` clear and the HUD restore
  land on that same frame — the whole transition is one tick. `FElysiumEntityWorld`'s map teardown
  (`ElysiumEntityWorld.cpp:2113-2115`) already runs `ClearTrackCamera(0)` then `ClearScriptedCamera()`
  and gains the "full EndShot when active" step.
- **`+0x1d60` bits 0x1 and 0x8** are cleared through a named accessor once **RC4** identifies the
  readers; the accessor is written against the recovered meaning, not against the bit number.
- **The 24 Hz think (M2, ruled — reversed).** The entity owns a **goal-publish think** that runs on a
  `_DAT_1044eb04 = 0.04165999963879585` accumulator advanced by the `DeltaTime` its caller hands it
  (never a clock read), and the client-side tracker keeps running per **rendered** frame against that
  stepped goal. That split *is* retail's server/client split, and the tracker's deadbands are defined
  against a 24 Hz-stepped target. The think's internal order is **verbatim**: the `AttachType None`
  anchor cache → the look-at solve `FUN_1006f670` → the `+0x594` origin selector →
  `AutoPositionFromTarget` (SC7) → the `+0xd4` angle gate → publish origin / target / angles / FOV →
  `point_player`. `FElysiumCameraDirector::Tick`'s every-frame re-resolve
  (`ElysiumCameraShots.cpp:604-629`) becomes the accumulator's driver.

**Tests — `Elysium.Substrate.CameraCinematic`** (new):

- `sp_tutorial_1`'s `feedcamera` verbatim (`shotname LookAtTarget_Snap`, `startent`/`endent` = itself,
  `target1 tutwareportal03`, `point_player 0`, `spawnflags 3`): `StartShot` resolves the three named
  anchors, adopts, and the shot's `Named` anchors come from the targetnames rather than from
  `Position`.
- A `StartShot` freezes movement, jump, duck and attack; `EndShot` releases all four.
- The re-shot branch with an unknown shot name leaves the camera **idle** (mode 0, no shot) rather
  than falling back — the asymmetry with `SetCamera` (SC9).
- A map-placed director **survives** its own `StartShot`; a camera created by the director **is
  destroyed** on the next adoption.
- `EndShot` cuts on the same frame with zero residual weight, and the **same** frame carries the
  mobilize, the `+0x1d60` clear and the HUD restore — a released shot's next frame is the player view.
- **The goal publish advances at 1/24 s**: driving the think with 120 Hz deltas publishes a new goal
  every third frame and holds it in between, and a 0.5 s delta publishes exactly once with the
  remainder carried in the accumulator.
- `point_player 1` turns the subject and never the camera; `point_player 0` turns neither.
- The `.txt` → basename normalization: `vdata/CameraShots/Jack.txt`, `CameraShots\Jack.TXT` and `Jack`
  all resolve to one shot.
- The five `CamMode` arms: 1 tracks, 2 publishes nothing, 3 follows anchor 0's world centre and abs
  angles, 4 pushes only the FOV, >4 removes the camera when the expiry has passed.
- A mode-1 shot never expires; a mode-4 shot arms the expiry with 0.0 and is removed.
- `m_bDrawPlayer` defaults to 0 on every shipped director (a content assertion over the 51 entities).

**Docs.** `camera-view-modes.md` §"The server shot lifecycle" gains the read key names, the corpus
counts, the disposable rule and the immobilize chain as landed; `docs/vtmb/computer-terminals.md`
records the terminal's move onto the shared slot (**M8**); `docs/vtmb/choreographed_scenes.md`'s
"Player pawn versus cinematic double" gains the pointer that the lock now exists in the port.

**Rulings applied.** **M2** — **reversed**: reproduce the 24 Hz goal-publish think in the substrate,
keep the two-clock split, keep the think's internal order verbatim. **M1** — the teardown is a
same-tick cut with no cvar. **M8** — one adoption slot with retail semantics; the terminal moves onto
it and a terminal closer drops to the **player view**, never to a previously stacked shot. All ruled
2026-09-07 under the owner framework of §7. **Deps:** SC1, SC2, RC2, RC3, RC4, RC12.

**Closure record.**

---

### SC5 — Shot start, the draw gates and the HUD edge

**Goal.** The client-side lifecycle around a live shot: the two shot-start arms, the two
`OnDataChanged` signals kept apart, and the two draw gates the port derives differently.

**The retail chain.**

- **`OnDataChanged` `0x100024c0`** (adjustor thunk; real offset = printed + 8):
  a new `m_nClientResetFrame (0x460)` vs its cache `(0x498)` ⇒ `m_bShotStartPending (0x4a2) = 1` and
  `m_bActive (0x465) = 1`. A new `m_ShotIndex (0x454)` vs its cache `(0x49c)` ⇒ **either**
  `flags & 0x80` ⇒ `m_bSnapPending (0x4a1) = 1` **or** clear all three `m_bAngleSettled (0x4c1..0x4c3)`
  — never both — and apply `ShowHud`: `(flags & 0x200) == 0 ⇒ HideHud(0xa06d)` else `ShowHud(0xa06d)`.
  `!IsActive() && m_bWasActive (0x4a0)` ⇒ `ShowHud`. Then `m_bWasActive = IsActive()` and
  `m_ShotIndexCache = m_ShotIndex`. **`m_nClientResetFrame` is *not* written back here** — that write
  belongs to shot start, so the pending flag survives until the shot actually starts.
- **Shot start `FUN_10002210`, both arms**:
  `if ((flags & 2) == 0 || (flags & 1) != 0)` — no `End`, **or** has `Start` — ⇒ the **replicated-goal
  arm**: `m_vecCurOrigin (0x468) = m_vecCamOrigin (0x410)`,
  `m_angCurAngles (0x474) = m_angCamAngles (0x428)`, `m_vecShotStart (0x484) = m_vecCamOrigin`.
  Otherwise — `End` **without** `Start` — the **live-view arm**: all three come from
  `CViewRender::GetViewSetup()` (`&DAT_105fbf38 + 0x10`), so **the shot dollies in from wherever the
  player is looking**. Then `m_bPositionSettled = 1`; `m_flLastTime = GetCurTime()`;
  `m_nClientResetFrameCache = m_nClientResetFrame`; `m_vecSettledOrigin = m_vecCurOrigin`;
  `m_flSpeed = 0`; all three turn rates 0 and all three angle-settled flags **0**;
  `m_bShotStartPending = 0`; `if (flags & 0x80) m_bSnapPending = 1`.
  Neither `m_flFOV (0x458)` nor `m_flCurFov (0x480)` is touched.
  The settle flags are **asymmetric**: position settled (the wide `DistanceTolerance` band from frame
  one), all three angle axes unsettled (the tight 1.0° band, so the camera acquires its aim precisely
  before parking).
- **`ShouldDrawLocalPlayer` `FUN_100a7a50`**: two entity-level predicates, then
  `cine = GetCineCamera(); if (cine) return cine->m_bDrawPlayer (0x464);` — `FUN_10001990`, a one-line
  accessor — `if (IsLocalPlayerEntity() && !CAM_IsThirdPerson()) return false; return true;`.
  `0x464` is read **exactly once in the image, here**, the test does **not** call `IsActive`, and it
  **short-circuits the whole `CAM_IsThirdPerson` disjunction**.
- **`ShouldHideViewModel`** (slot 174, `FUN_100a7ab0`) is `cine && !ShotWantsViewmodel(cine)`, called
  from `C_BaseViewModel::vfunc4`, `vfunc9` and `vfunc11` on the local player.
  **`FUN_100019a0`: `ShotWantsViewmodel() = (m_flSpeed (0x4a4) <= 1.0f) && (flags & 0x40 DrawViewmodel)`**
  — the `1.0` is exactly SC1's speed floor, so the test reads "the shot opted in **and** the camera has
  stopped dollying". The same predicate is the early-out that skips the `scr_ofs*` viewmodel
  adjustment.
- **`HideHud(0xa06d)`** — `CHudManager::HideHud(bits)` (slot 113, `+0x1c4`, `0x10057f50`) walks the
  element list and `SetVisible(false)`s every element whose `GetHudBits()` (vfunc4) intersects the
  mask; slot 114 (`+0x1c8`) is the twin. **Edge-triggered on the shot index changing, never per
  frame**, so a HUD-hiding shot replaced by another HUD-hiding shot does not re-issue the call. The
  destructor `FUN_10001920` restores the HUD when `(flags & 0x200) == 0`.
- **The `+0xd4` gate.** `if (rec->+0xd4 > 0)` decides whether the server derives `m_angCamAngles` from
  the look-at at all; with no `Target` block the entity's **abs angles** are published unchanged.
- **`+0x594`'s third arm** (`2`) leaves the entity's own abs origin alone **and skips
  `AutoPositionFromTarget` entirely**.

**The design in the port.**

- `FElysiumCameraShot` carries the four **presence flags** (Start / End / Point1 / Point2) and the
  explicit **`TargetPointCount`**, so the tracker can choose the shot-start arm and the `+0xd4` gate
  is a count rather than a derived boolean (`bUseLookAt` at `ElysiumCameraShots.cpp:453` becomes
  `TargetPointCount > 0`).
- `FElysiumScriptedShotTracker::Start` (`ElysiumCameraSolve.cpp:354-368`) takes **the current view as
  a parameter** and implements the live-view arm; it never reaches for a view itself. The
  `Public/ElysiumCameraSolve.h` comment recording the arm as absent is retired.
- The re-seed splits into its two real signals at `UElysiumCameraComponent::SolveShot`
  (`ElysiumCameraComponent.cpp:185-191`) and `UElysiumCameraService::SelectWinner`
  (`ElysiumCameraService.cpp:136-150`): a **reset-frame** change arms shot start; a **shot-index**
  change arms the one-shot snap **or** clears the three angle-settled flags. Never both.
- `ElysiumCam::SolveDrawPolicy` (`ElysiumCameraSolve.cpp:178-200`) gains both retail gates:
  `bBodyEligible` becomes the adopted shot's `bDrawPlayerBody` **short-circuiting** the third-person
  test (`:178` `bBodyEligible = bThirdPerson` today), and `bViewmodelEligible` (`:192`) gains the
  **speed term** from SC1's tracker.
- **The HUD edge (M14, ruled).** The **element set** behind retail's `0xa06d` is *not* reproduced —
  the port's HUD is a new asset, so the port hides its own. The **edge timing is contract**: the
  per-frame `bShowHud` solve (`:200`) is replaced by an edge issued on the shot-index change (hide
  into a `ShowHud`-clear shot, show into a `ShowHud`-set shot), plus show on going inactive after
  having been active and on destruction, and **no per-frame enforcement** — so a HUD-hiding shot
  replaced by another HUD-hiding shot does not re-issue the call.
- `+0x594`'s third arm is represented on the shot so SC7 has somewhere to hang its suppression.

**Tests.**

- **`Elysium.Substrate.CameraShotStart`** (new): an `End`-without-`Start` shot seeds from a supplied
  view and dollies to its goal; a `Start`-bearing shot seeds **on** the goal; a shot with neither
  `Start` nor `End` takes the replicated-goal arm too (the `!(End) || Start` test, both halves).
  A reset-frame change arms shot start **without** clearing the settle flags; a shot-index change does
  the converse. A shot with no `Target` block keeps its authored angles. Position settled and all three
  angle axes unsettled at start.
- **`Elysium.Substrate.CameraDraw`** (extend, `ElysiumCameraTests.cpp:584`): a first-person run with an
  adopted shot whose `bDrawPlayerBody` is 1 **draws the body**; with 0 it does not, whatever the
  third-person state says. Hands suppressed at 40 u/s and restored at 0.5 u/s with the **same**
  `DrawViewmodel` shot. The HUD hide/show fires **on the edge**: once on a change into a
  `ShowHud`-clear shot, **not re-issued** when that shot is replaced by another HUD-hiding shot, and
  once again on going inactive after having been active.

**Docs.** `camera-view-modes.md`'s "the port also does not implement retail's start-from-the-live-view
arm" paragraph is retired; the draw-gate section gains the `m_bDrawPlayer` short-circuit and the speed
term as landed; the divergence table's three draw rows resolve.

**Rulings applied.** **M14** — the HUD *element set* is the port's own (menu and HUD are new assets),
the *edge timing* is contract and is reproduced. Ruled 2026-09-07 under the owner framework of §7.
**Deps:** SC1, SC4, RC11.

**Closure record.**

---

### SC6 — The anchor grammar completed

**Goal.** Every remaining divergence in `FUN_10071e00` (parse) and `FUN_1006f080` (resolve), plus the
grapple-role state the two grapple keywords need.

**The retail chain.**

- **The record.** Shot stride `0x104`: `+0x00` name (`Q_strncpy` into 0x20), `+0x20` flags,
  `+0x24`/`+0x50`/`+0x7c`/`+0xa8` the four `0x2c`-byte anchors, **`+0xd4` the Target-block count**,
  `+0xd8` MoveSpeed, `+0xdc` MoveAccel, `+0xe0` TurnAccel, `+0xe4..0xec` MaxTurnRate[3],
  `+0xf0..0xf8` AngularTolerance[3], `+0xfc` DistanceTolerance, `+0x100` FieldOfView. Defaults
  150 / 50 / 30 / [90,90,90] / [1,1,1] / 10 / 75, FOV clamped `[20,120]`
  (`_DAT_1044eb0c`/`_DAT_1044f00c`); `FUN_10072300` (the whole-block-absent seeder) writes the
  identical set. Flags: `0x01` Start · `0x02` End · `0x04`/`0x08` Point1/Point2 · `0x10` DialogPOV ·
  `0x20` AutoPositionFromTarget · `0x40` DrawViewmodel · `0x80` SnapOnShotChange · `0x100`
  SyncRotateOnMove · `0x200` ShowHud.
- **The target count and its bug.** `flags |= 1 << (count + 2); count++` per `Target` sub-block found —
  the flag follows **order of presence** while the data goes into the fixed slot (§4, row 1).
- **Anchor record `0x2c`**: `+0x00` flags, `+0x04` a 16-byte inline name (`Q_trimspace` of whatever
  follows `Bone:` / `Attachment:`), `+0x14` `OffsetOrigin` (default `"0 0 0"`), `+0x20` `OffsetAngles`
  (default `"0 0 0"`).
- **`Position`** — `_strstr`, in this order: `Player` `0x1`, `DialogTarget` `0x2`,
  **`GrappleVictim` `0x80000`**, **`GrappleAttacker` `0x100000`**, `Named` `0x8`, and **anything
  unrecognised falls through to `World` `0x4`**, which is also the default.
- **`Position` resolve in `SetShot`**: `Player` → `UTIL_PlayerByIndex(1)`; `DialogTarget` →
  `subject+0xFE8` (NULL if player 1 is NULL); `GrappleVictim` → `subject+0x1538` when the role
  `subject+0x153c == 0`, **the subject itself** when it is `1`; `GrappleAttacker` the mirror; **both
  additionally require the `+0x1538` handle live**, else falling through to the `World`/NULL tail;
  `World` → `FindEntityByClassname(NULL, "worldspawn")`; **`Named` → NULL**, the caller supplies it
  through `SetShotAnchorEntity`. `+0x153c` is the grapple role slot (`-1` = not grappling), the same
  pair `CBaseCombatCharacter::CanStartGrappleAttack` `0x103285a0`, `CPlayerMove::SetupMove`
  `0x10186120`, `CBasePlayer::GetSaveBlockedReason` `0x10174f80` and
  `CBaseCombatCharacter::ChooseMeleeAttackSequence` `0x10347180` read.
- **`AttachPos`** — `_strstr`, in order: `Bone:` `0x200` (`GetBonePosition02` — position **and**
  angles), `Attachment:` `0x400` (`GetAttachment02`), `Center` `0x20` (`WorldSpaceCenter`, vfunc
  `0x300`), `EyePosition` `0x40` (`m_pCombatCharacter (+0x9c) ? CalcLookData(&eye, NULL) :
  EyePosition()` vfunc `0x304`), `Top` `0x100` = **`(absOrigin.x, absOrigin.y, bounds.maxs.z)`**,
  `Bottom` `0x80` = the same with `mins.z`, **`AbsMin` `0x800` = `bounds.mins`, all three**,
  **`AbsMax` `0x1000` = `bounds.maxs`, all three**, default `Origin` `0x10` = `GetAbsOrigin()`. Bounds
  are the world-space surrounding bounds `ent->m_Collision (+0x270)->vfunc 0x3c`, read **once at the
  top** of `FUN_1006f080` for every arm. Every arm but `Bone:`/`Attachment:` takes the entity's abs
  angles as its rotation basis.
- **`AttachType`** — an **exact byte compare including the NUL** (`Follow` 7 bytes, `FollowNoAngles`
  15, `FollowEntAngles` 16), **case-sensitive**, not `strstr`: `Follow` `0x4000`, `FollowNoAngles`
  `0x8000`, `FollowEntAngles` `0x10000`, default `None` `0x2000`.
- **The offset step `LAB_1006f430`**, gated on `0x20000` (a non-zero `OffsetOrigin`):
  `flags & 0xa000` (`None | FollowNoAngles`) ⇒ add in world axes; `flags & 0x4000` (`Follow`) ⇒
  `AngleMatrix(attachAngles, pos, m); VectorTransform(rec+0x14, m, pos)` — **the attach point's own
  angles**, a bone's or an attachment's; `flags & 0x10000` (`FollowEntAngles`) ⇒ the same with the
  entity's abs angles.
- **`OffsetAngles`** (`+0x20`, flag `0x40000`) — **no reader anywhere in `vampire.dll`**. Parsed and
  dead. This settles the document's last open question on this path.
- **`SetShotAnchorEntity` `FUN_1006ef50`** — NULL ⇒ handle `0xffffffff`; else store the handle at
  `+0x610 + i*4` and, when the entity animates (vfunc `0x224`), resolve the inline name to a bone index
  (`flags & 0x200`) or an attachment index (`flags & 0x400`) into `+0x620 + i*4`. A non-animating
  entity leaves that index at `-1`.
- **The latch `FUN_1006f010`** and its anchor-0 bug — §4, row 4.
- **Corpus.** `GrappleAttacker` ×6 and `GrappleVictim` ×2, all in `stealth_kill.txt`'s
  `Stealth_Kill_1..4`. `AbsMin`/`AbsMax` once each in `centerfullview.txt`. Ten files author a `Start`
  (`andreibasement`, `kilpatrick`, `lookattarget_b`, `npcfollow`, `npcfollowcut`,
  `npcfollowfromplayer`, `npcfollowmove`, `special-case`, `stealth_kill`, `tong`), nine of those Starts
  are `Follow`/`FollowEntAngles`, and **exactly one is `AttachType None`** — `special-case.txt`'s
  `Follow`. `GrappleTarget` — the keyword the port implements — ships **nowhere**; it exists only in
  Troika's how-to, where retail's `_strstr` chain would resolve it to `World`.

**The design in the port.**

- `ElysiumCameraShots.cpp:43-55` — the invented `GrappleTarget` (`:48`) is deleted and
  **`GrappleVictim`** / **`GrappleAttacker`** land in retail's `_strstr` order; the fallthrough
  (`:51-54`, "take the value as an entity name") becomes **`World`** with a warning naming the token,
  and `Named` keeps resolving to nothing until a caller supplies the entity — which SC4's
  `SetShotAnchorEntity` now does.
- `AnchorEntity` (`:305-333`) gains the grapple role resolve verbatim, including the "**resolves to the
  subject itself**" arm and the dead-handle fall-through to `World`; the `GrappleTarget` degrade at
  `:317-318` goes away. `World` resolves to worldspawn's origin rather than `nullptr`.
- **The grapple role pair becomes real substrate state.** `FElysiumCombatCharacter` gains the partner
  handle and the role slot (`-1` none, **0 attacker, 1 victim** — RC13 corrected this label; the
  behavioural text above was always right) that retail keeps at `+0x1538`/`+0x153c`,
  written by the feed, stealth-kill and melee-grapple paths that already exist. **RC13** names the
  retail writers so the port's writers land on the same transitions rather than on a guess. This is
  not a seam answering "nothing": the state is built and wired in this slice.
- `AttachPoint` (`:342-392`) gains **`AbsMin`** and **`AbsMax`**, and `Top`/`Bottom` (`:380-381`) are
  corrected from `BoundsPoint(1.0f)`/`BoundsPoint(0.0f)` — a fraction on all three axes — to retail's
  abs-origin XY with only Z from the bounds. The bounds are read **once** per resolve.
- The `Follow` offset frame (`:414-424`) is corrected to rotate by the **attach point's** angles;
  `FollowEntAngles` keeps the entity's. A `Bone:` `Follow` anchor regains its bone-space offset frame.
- `SetShotAnchorEntity` caches the bone/attachment **index** at resolve time as retail does, replacing
  the per-frame socket-name lookup (`:369-377`) and its `Origin + Z(64u)` fallback (`:377`), which has
  no retail counterpart.
- **`AttachType None` latching** (§4, row 4) lands with the shot-start anchor cache and retail's
  anchor-0 flags test, reproduced with a comment naming the bug and `retail-defects.md` §7.
- **`AttachType` matching becomes case-sensitive (M3, ruled — reversed)**: an exact byte compare
  including the NUL (`Follow` 7 bytes, `FollowNoAngles` 15, `FollowEntAngles` 16), so a mis-cased
  value falls to `None` exactly as retail. `ESearchCase::IgnoreCase` at
  `ElysiumCameraShots.cpp:57-64` goes. A **parse warning** fires on a case-only mismatch, naming the
  exact retail spelling and stating that retail reads the value as `None` — the warning is the
  observability, not a behavioural rescue, because `None` versus `Follow` decides whether the anchor
  latches at shot start or tracks.
- **`OffsetAngles` stays unparsed**, now on evidence, with the settlement recorded.
- The **target-point count** and its order-of-presence flag (§4, row 1) land here.

**Tests — `Elysium.Substrate.CameraAnchors`** (new):

- `centerfullview.txt` verbatim: `AbsMin`/`AbsMax` against a known bounds box, and the
  `FollowEntAngles` `[16,0,0]` offset rotated by the entity's angles.
- `Top`/`Bottom` on an off-origin body: XY comes from the origin and only Z from the bounds.
- `stealth_kill.txt` verbatim through **both** grapple roles, including the "resolves to the subject
  itself" arm and the dead-handle fall-through to `World`.
- An unrecognised `Position` value resolves to the **world origin**, not to a named entity, and warns.
- A `Bone:` `Follow` anchor's offset rotates with the **bone**, not with the pawn; the
  `FollowEntAngles` twin rotates with the pawn.
- `special-case.txt`'s `Follow` shot: its `AttachType None` `Start` **holds its shot-start sample**
  while the entity walks away; `jack.txt` (no `Start`) re-resolves everything, reproducing the
  anchor-0 bug.
- A shot authoring `Point2` alone aims at the world origin (§4, row 1).
- **`AttachType "follow"` (lowercase) parses as `None`, with a warning** naming `Follow` as the
  retail spelling — and the resulting anchor **latches** at shot start rather than following, which is
  the behavioural fork the warning is about.
- The bone index is resolved once and re-used; a non-animating entity leaves it unresolved.

**Docs.** `camera-view-modes.md`'s anchor table gains `AbsMin`/`AbsMax`, the corrected `Top`/`Bottom`,
the grapple keyword pair and role resolve, the corrected fallthrough, the corrected `Follow` offset
frame, and the settled `OffsetAngles` question; **M3** is recorded; the divergence table's six anchor
rows resolve. `docs/vtmb/stealth.md` gains the pointer that the stealth-kill shots read the grapple
role pair.

**Rulings applied.** **M3** — **reversed**: reproduce the case-sensitive byte compare and warn on a
case-only mismatch. Ruled 2026-09-07 under the owner framework of §7. **Deps:** SC4, RC11, RC13.

**Closure record — landed 2026-09-07 (headless; not yet built or run).**

*Parse* (`Private/Player/ElysiumCameraShots.{h,cpp}`). `Position` is retail's `_strstr` order —
`Player`, `DialogTarget`, `GrappleVictim`, `GrappleAttacker`, `Named` — with **everything
unrecognised falling through to `World`** and a warning naming the token; the invented
`GrappleTarget` keyword and the "take the value as an entity name" arm are both deleted, and
`FElysiumShotAnchor::NamedEntity` is now always empty because retail reads no name off the record.
`AttachPos` becomes a parsed `EElysiumShotAttachPos` beside the verbatim string, gaining `AbsMin` /
`AbsMax` and splitting the inline `Bone:` / `Attachment:` name off at retail's 15-character cap
(`Q_trimspace(..., 0x10)`; `camera_position` is exactly 15). `AttachType` is an exact
`ESearchCase::CaseSensitive` compare with a parse warning on a case-only mismatch that names the
retail spelling and states that retail reads the value as `None` — **M3, landed**. The shot record
carries `TargetPointCount` (`+0xd4`) and two presence flags raised by `flags |= 1 << (count + 2);
count++`, so the `Point2`-without-`Point1` bug is reproduced and the look-at reads the **flags**
(`FUN_1006f670`), not the slots. `OffsetAngles` stays unparsed with the evidence in a comment.
The record defaults (150 / 50 / 30 / [90,90,90] / [1,1,1] / 10 / 75) and the `[20,120]` FOV clamp
were already correct and were verified, not changed.

*Resolve.* `AnchorEntity` became `SetShotEntity`, retail's `SetShot` loop verbatim: `World` resolves
to **worldspawn**, `Named` to nothing, and the two grapple arms read the role pair — `GrappleVictim`
→ the partner when the role is `Attacker`, **the subject itself** when it is `Victim`;
`GrappleAttacker` the mirror; both require the partner handle live, else the `World` tail. The
surrounding bounds are read **once** per resolve and every arm indexes into them; `Top`/`Bottom` are
the abs origin's XY with only Z from the bounds. The `Follow` offset turns in the **attach point's**
frame and `FollowEntAngles` in the entity's. `FElysiumShotBindings` carries retail's `+0x610`
handle, `+0x620` index and `+0x598` shot-start cache per anchor; `FElysiumCameraDirector::
SetShotAnchorEntity` is `FUN_1006ef50` (store the handle, resolve the name to an index once), which
replaced the per-frame socket lookup and let the invented `Origin + Z(64 u)` fallback go. `Resolve`
keeps its old 4-argument shape for the dialogue ladder and gains an optional bindings table and a
pass selector.

*The latch.* `ElysiumCameraShots::LatchesAnchors` is `FUN_1006f010`'s test **including its anchor-0
bug** — `Start` present and its `AttachType` is `None` — applied per shot in the director's
re-resolve path. Exactly one shipped shot latches.

*Substrate.* `FElysiumGrappleState` / `EElysiumGrappleRole` / `EElysiumGrappleType` on
`FElysiumCombatCharacter` (`Public/ElysiumPlayer.h`), written only by `EnterGrappleState` /
`LeaveGrappleState` and their two-party `EnterGrapplePair` / `LeaveGrapplePair` wrappers
(`Private/Substrate/ElysiumCombatCharacter.cpp`); the feed pair routes through them as type 0
(`Private/Substrate/ElysiumFeed.cpp`). **RC13 landed with this slice and left no seam**: there are
exactly three retail writers (the two transactions and the constructor) and no NPC melee path among
them, so nothing is stubbed.

*Corrections carried in from RC11 / RC13.* The role enum is `None = -1, **Attacker = 0**,
**Victim = 1**` — §8's "0 victim, 1 attacker" label was inverted; the behavioural text in this slice
was always right. The pair carries `m_GrappleType` and `m_GrapplePosition` as well;
`m_GrappleSavedMoveType` is **not** ported, because the port's motor has no MoveType to save. The
`OffsetOrigin` / `OffsetAngles` parse default is the literal `"[0, 0, 0]"`, not `"0 0 0"`. The client
parser agrees with the server exactly, so there is nothing to reconcile. **A new camera edge for
SC4/SC9:** `CBasePlayer::LeaveGrappleState` calls `SetCineCamera(NULL)`, so ending a grapple ends the
shot; recorded in `camera-view-modes.md`, not wired here.

*Tests.* `Elysium.Substrate.CameraAnchors` (`Private/Tests/ElysiumCameraAnchorTests.cpp`) and
`Elysium.Content.CameraShotGrammar` (`Private/Tests/ElysiumCameraShotGrammarTests.cpp`).

*Corpus census, measured.* §9's table labelled its six numbers "files"; read against the corpus they
are **shots that write the key**, and two of the six were wrong. The real numbers, now asserted:
written by 59 / 34 / 9 / 5 / **4** / **3** shots (`DialogPOV`, `SyncRotateOnMove`,
`SnapOnShotChange`, `AutoPositionFromTarget`, `ShowHud`, `DrawViewmodel`) and **set to 1** by
50 / 33 / 7 / 3 / 2 / 1. The corpus is 66 files parsing to **72** shots (`special-case` 5,
`stealth_kill` 4, the how-to 0). Everything else in §9's content list verified as written:
`GrappleAttacker` ×6 and `GrappleVictim` ×2 all in `stealth_kill.txt`, `AbsMin`/`AbsMax` once each in
`centerfullview.txt`, `GrappleTarget` only in the how-to, ten files authoring a `Start`, exactly one
`Start { AttachType None }` (`special-case.txt`'s `Follow`), and **no** `SyncRotateOnMove` shot
inside the `MoveSpeed > 2·MoveAccel` NaN band. `Top` and `Bottom` are documented and **never
authored**, which is why the port's wrong reading of them survived until the decompile was read.

*Known breakage outside this slice's files.* `ElysiumCameraTests.cpp:2242-2250` (the `Vantage` case)
asserts the deleted fallthrough — that `"Position" "cam_marker_1"` parses to `Named` with
`NamedEntity == "cam_marker_1"`. It now parses to `World`, correctly, so that case must be rewritten
or dropped by whoever owns that file.

**Closure record.**

---

### SC7 — `AutoPositionFromTarget` and the origin selector's third arm

**Goal.** The one shot-record field parsed since the reader landed and read by nothing.

**The retail chain.** Applied by the mode-1 think on `flags & 0x20`, disassembly `0x1006fa50`–
`0x1006fb85`, constants read from the image (`_DAT_104454d0 = 0.5`, `_DAT_1044eb08 = 0.0174532924`,
the double `2.0` at `0x10449400`, `_DAT_104492dc = −1.0`; `FUN_10431c90` is `_CIpow`):

```
resolve P1 = anchorPos(2), P2 = anchorPos(3)
if (P2.z > P1.z) swap(P1, P2)                 // 1006f9e6: FCOMP + AND EAX,0x4100 + JNZ-skip
                                              //   => P1 ends HIGH, P2 ends LOW
C   = ClosestPointOnLine(P2, lookAt, camOrigin)     // FUN_1013ca00 → FUN_1013c940
A   = FieldOfView * 0.5 * DEG2RAD
d   = |C − P2|
h   = d / sin(A)
r   = sqrt(h*h + d*d)
camOrigin = lookAt − normalize(lookAt − camOrigin) * r
```

**Only the lower-Z point participates**; the higher exists solely to decide the swap. This is **not**
the tight `d / tan(A)` fit — retail takes the hypotenuse and then adds a second `d` in quadrature, so
it always backs off further than an exact frame. `FieldOfView` here is the raw authored 4:3-referenced
horizontal number; the Hor+ widening is a render-time affair. The whole block is **skipped** when the
origin selector `+0x594` is `2`.

Five shipped shots set the flag: `centerfullview`, `dialogmediumshot`, `mediumshot`, `dialoglowleft`,
`dialoglowright`. `DialogMediumShot` is one of `sp_tutorial_1`'s placed `camera_cinematic` shots,
though the map never fires it.

**The design in the port.** The solve enters `FElysiumCameraDirector::Resolve`
(`ElysiumCameraShots.cpp:428-479`) **after** the origin and look-at are solved
(`:435-439` / `:442-448`) and **before** `bTracked` is stamped (`:458`), reading
`bAutoPositionFromTarget` (`ElysiumCameraShots.h:99`, parsed at `.cpp:104`) which no solver reads
today. It is suppressed on SC5's third origin-selector arm, as retail is. `ClosestPointOnLine` is
implemented as the **standard clamped projection** and the assumption is named in the code and the
doc until **RC1** opens `FUN_1013c940`; if RC1 finds the projection unclamped, the one-line change
lands with it.

**Tests — `Elysium.Substrate.CameraAutoPosition`** (new):

- `centerfullview.txt` with two known target points produces the recovered `r` to within a float
  epsilon.
- The swap is proven by feeding the two points in **both orders** and getting one answer.
- A narrower `FieldOfView` backs the camera **further** off.
- The result differs measurably from the `d / tan(A)` fit, so the divergence cannot regress silently.
- The flag clear leaves the origin untouched; the third origin-selector arm suppresses the solve even
  with the flag set.

**Docs.** `camera-view-modes.md`'s `AutoPositionFromTarget` entry moves from "parsed and read by
nothing" to the recovered formula as landed, with the `FUN_1013c940` assumption named (or resolved).

**Rulings applied.** None — every behaviour in this slice is contract. **Deps:** SC5, SC6, RC1.

**Closure record.**

---

### SC8 — `FindBestShot` and the anim-event channel (4050 / 4051)

**Goal.** The last unported entry point into the scripted channel, and the only non-zero writer of
`m_bDrawPlayer` in the game.

**The retail chain.**

- **`FindBestShot` `FUN_1006e4c0(this, baseName)`** — sets `CamMode = 1` **directly**, before any
  `SetShot`; then `for (i = 1; ; ++i) { Q_snprintf(buf, 0x40, "%s_%d", baseName, i);
  if (SetShot(buf, 1, NULL)) { FUN_1006e8e0(this); if (FUN_1006d9d0() && FUN_1006db10())
  vec.AddToTail(i); } if (m_ShotIndex == -1) break; }` — **the first name the table does not hold ends
  the scan**. Then `k = RandomInt(0, vec.Count()−1)`, re-`SetShot` that name, and
  `Msg("CBaseCineCam::FindBestShot chose %s\n", buf)`. It does **not** re-run `FUN_1006e8e0` after the
  final `SetShot` — the caller does. There is no scoring; the name is a misnomer for "any shot that
  fits, chosen at random".
- **`FUN_1006d9d0`** — the shot's **declared** anchors exist: `flags == 0xffffffff` ⇒ false; then each
  of `Start 0x1` / `End 0x2` / `Point1 0x4` / `Point2 0x8` the shot declares must have a live handle at
  `+0x610 + i*4`; finally `m_ShotIndex != -1`.
- **`FUN_1006db10`** — the camera can see its target: solve the look-at, then for each **live**
  `Start`/`End` anchor, `UTIL_TraceHull` from that anchor's resolved world position to the look-at with
  `mins (−1,−1,−1)`, `maxs (1,1,1)`, mask **`0x1400b`**, and a `CTraceFilterSimple` that **ignores the
  shot's subject**, failing on `fraction < 1.0 || startsolid || allsolid`. The player's own body
  therefore never blocks a candidate.
- **`FUN_10070550(baseName)`** — `Create("camera_cinematic")`, `+0x204 |= 0x4`, `FindBestShot`,
  `UTIL_Remove` on failure, `FUN_1006e8e0` on success. Its **only** caller is
  `CBasePlayer::HandleAnimEvent` `0x10178a10`.
- **Event 4050 (`0xfd2`)** — gated on `!this->vfunc0x658() && event->owner == this`; the `options`
  string is the shot **base name**; on success `cam->m_bDrawPlayer (+0x640) = 1`,
  `FUN_1017cef0(this, cam)`, and `cam->m_bForcePlayerLook (+0x5e8) = 0`.
- **Event 4051 (`0xfd3`)** — `FUN_1017cef0(this, NULL)` (drop **and destroy**), then
  `AngleVectors(GetAngles(), fwd); fwd.z = 0; VectorNormalize(fwd);
  FUN_10178590(this, EyePosition() + fwd * _DAT_10447ee0)` — snap the player's eye angles level and
  straight ahead along his own flattened forward.
- **Neither event immobilizes.** Forty shipped occurrences each, and `stealth_kill.txt`'s
  `Stealth_Kill_1..4` is exactly the `"%s_%d"` family they name — which **corrects
  `docs/vtmb/animation_events.md`**, whose 4050 row records the `options` string as a raw entity
  classname.

**The design in the port.**

- `FElysiumCameraDirector` gains `FindBestShot(BaseName, Rng)`: the enumeration, the two predicates and
  the uniform pick from a named **`ElysiumRng::Stream`** (never `FMath::Rand*`), logging retail's
  message verbatim under the camera log category.
- The visibility predicate's trace goes through `FElysiumWorldServices` (substrate rule), with retail's
  hull, mask and subject filter named in the call.
- The two anim events land on the existing anim-event dispatch, closing them out of
  `docs/vtmb/anim-events.md`'s unclaimed audit. 4050 raises `bDrawPlayerBody` (SC5's consumer) and
  clears `point_player`; 4051 destroys the camera and levels the player's eye angles through the same
  gaze arm SC4 wired for `point_player`.
- The `+0x204 & 0x4` disposable rule from SC4 is what makes 4051's "drop and destroy" a one-liner.

**Tests — `Elysium.Substrate.CameraFindBestShot`** (new):

- The enumeration stops at the **first gap**: a `_1`, `_2`, `_4` family yields two candidates.
- The anchor predicate rejects a shot whose declared `Start` has no entity, and accepts one that
  declares nothing.
- The visibility predicate rejects a candidate whose trace is blocked and **accepts** one whose only
  obstruction is the subject.
- A seeded stream picks deterministically across the passing set; a different seed picks differently.
- Anim event 4050 adopts a camera, raises `bDrawPlayerBody` and clears `point_player`; 4051 destroys it
  and levels the player's eye angles.
- The whole chain over `stealth_kill.txt` verbatim, both grapple roles live (shares SC6's fixture).

**Docs.** `docs/vtmb/animation_events.md`'s 4050 row is corrected to "the shot base name" and both
events leave the unclaimed list; `camera-view-modes.md` §"`FindBestShot` and the anim-event channel"
moves from recovered to landed.

**Rulings applied.** None — every behaviour in this slice is contract. **Deps:** SC4, SC6.

**Closure record.**

---

### SC9 — The dialogue and script chain around the camera

**Goal.** The retail entry and exit points the dialogue director stands beside, and the asymmetry
between them.

**The retail chain.**

- **`CBasePlayer::SetCamera` `FUN_1017d020(shotName)`**:
  ```c
  if (GetCineCamera() == NULL) {
    cam = FUN_10070470(shotName, NULL,NULL,NULL,NULL);
    if (!cam) cam = FUN_10070470("DialogDefault", NULL,NULL,NULL,NULL);
    FUN_1017cef0(this, cam);
  } else {
    if (!SetShot(shotName, 1, NULL)) SetShot("DialogDefault", 1, NULL);
  }
  ```
  Three properties the shape hides: it **never immobilizes**; the re-shot branch **never calls
  `FUN_1006e8e0`**, so it neither re-places the entity nor refills the shot-start anchor cache — which,
  with "the angle comes from `GetOrigin()`", means a mid-conversation `SetCamera` leaves the entity
  where the **first** shot put it; and the `"DialogDefault"` literal is verbatim
  (`s_DialogDefault_10587f04`). Its only caller is the Python native `FUN_10198070`
  (`PyArg_ParseTuple(args, "Os", …)`, `"bad args to SetCamera()"`, else
  `"SetShot needs to be called on a v..."`), with **115 shipped call sites**.
- **`CBasePlayer::StartPlayerDialog` `0x10178280`**: the dialogue-manager admission test
  (`FUN_10178170`, `FUN_101cebc0`, `FUN_100e05f0` — **RC6**); `FUN_10167fd0`; `SetDialogPartner(npc)`
  (writes `player+0xFE8`); **`SetImmobilized(true)`**; `+0x1e01 = (GetActiveWeapon() &&
  GetActiveWeapon()->+0x870) ? 1 : 0`; `vfunc0x724("item_w_unarmed", 0)` — holster; then, **unless the
  partner RTTI-casts to a payphone** (which runs `StartGrappleAttack(this, npc, 5)` and creates no
  camera at all), `FUN_10070470(npc->+0x64C4 /* default_camera */, NULL,NULL,NULL,NULL)` — **no anchor
  entities**, so every anchor comes from the shot file's own `Position` — and adopt; then
  `FUN_10147a60`, `FUN_100826b0(NULL)`. It writes no origin and no angles for either party.
  **`StartPlayerDialog` has no `DialogDefault` fallback**: a name that does not load leaves
  `cam == NULL` and the conversation runs cameraless.
- **`CBasePlayer::EndPlayerDialog` `0x10178400`**: **`UTIL_Remove(GetCineCamera())` unconditionally**
  — *not* gated on `+0x204 & 0x4` — then `SetCineCamera(NULL)`, `SetImmobilized(false)`, re-draw the
  holstered weapon if `+0x1e01`, `SetDialogPartner(NULL)`, `CPlayerEventsManager::vfunc4()`. **No
  blend, no fade, no dampening.**
- **No engine-side per-line camera.** `vampire.dll` parses no `.dlg` file (the string does not occur in
  the image) and `SetCamera` has exactly one caller, so a per-line shot reaches the engine only as an
  explicit `SetCamera(actor, "Shot")` in the dialogue's own `.py`. **RC6** closes the last hop by
  reading the bridge's dialogue-advance path.
- **`DialogPOV` (`flags & 0x10`)** has exactly one reader, `CAI_BaseNPC::FUN_1026b810`: when the
  partner's player has a live cine camera whose shot sets the bit, the NPC's eye-look target becomes
  the **camera's** `EyePosition()` — `CBaseCineCam::vfunc193` `0x1006d910`, which returns
  `m_vecCamOrigin (+0x5ec)` — instead of the player's eye. **Either choice is gated by `FUN_10325da0`,
  the head-turn feasibility test** (**RC5**); a refusal falls through to the ordinary target chain
  (`m_hTargetEnt`, enemy, navigator goal, hint, nearest-NPC scan).

**The design in the port.**

- `FElysiumCameraDirector::Push` (`ElysiumCameraShots.cpp:490-494`, which returns 0 on a load failure)
  gains the **`"DialogDefault"` fallback on both arms**, reached from the script native only
  (`ElysiumScriptNatives.cpp:61`, `:415`, `:419`) — and **not** from the dialogue opener
  (`ElysiumEntityWorldDialogue.cpp:521-568`), where retail has none. The asymmetry is asserted, not
  smoothed.
- `SetCamera` **does not immobilize**; once SC4 lands the immobilize pair this becomes assertable
  rather than accidental.
- The re-shot branch's "does not re-place the entity" is already the port's behaviour
  (`ElysiumCameraComponent.cpp:185-191` continues the tracker from its current pose); this slice
  records it as the retail reason and asserts it.
- `StartPlayerDialog` gains `SetImmobilized(true)`, the active-weapon latch and the
  `"item_w_unarmed"` holster with its restore at dialogue end, and the payphone arm that grapples
  instead of creating a camera.
- `EndPlayerDialog` becomes the **unconditional** remove and the **cut** (**M1, ruled**):
  `ElysiumDialogueCamera.cpp:95`'s `BlendOutSeconds = 0.25f` is deleted outright, not routed through a
  cvar, and the mobilize, the weapon restore and the HUD restore land on the same frame the camera
  dies.
- `DialogPOV`'s resolver (`ElysiumEntityWorldDialogue.cpp:706-733`, lens enum
  `Public/ElysiumDialogueCamera.h:35-40`, consumed at `Public/ElysiumPlayer.h:1604/1609`) gains the
  **feasibility gate**, named for `FUN_10325da0` and implemented from **RC5**'s recovery; a refusal
  falls through to the ordinary target chain rather than staring past the lens.
  The port's default-set modernization at `ElysiumEntityWorldDialogue.cpp:452-469` is re-stated beside
  it.

**Tests — `Elysium.Substrate.DialogueCamera.RetailChain`** (new, following the file's dotted
sub-path convention):

- `SetCamera` with an unknown name lands on `DialogDefault` on **both** arms (no camera yet, and a
  camera already present); `StartPlayerDialog` with the same name lands on **no camera**.
- A conversation opens immobilized and holstered and closes mobile with the weapon restored; a
  conversation opened with no weapon drawn restores nothing.
- A payphone partner takes the grapple arm and creates no camera.
- The dialogue-end cut leaves zero residual weight on the frame it fires.
- A mid-conversation `SetCamera` continues from the pose the first shot established.
- A `DialogPOV` shot whose feasibility gate **refuses** falls back to the player's eye, and one whose
  gate accepts aims at the camera origin.
- `SetCamera` does not immobilize (asserted against SC4's mechanism).

**Docs.** `camera-view-modes.md` §"How dialogue drives the camera" gains the fallback asymmetry, the
holster latch, the payphone arm and the dialogue-end cut as landed; the `DialogPOV` paragraph gains the
feasibility gate. `docs/project/roadmap.md`'s **RE46** row (the dialogue opener and camera boundary)
closes to `[x]` when this slice and RC5/RC6 land, because its remaining question is exactly the
opener's boundary.

**Rulings applied.** **M1** (its dialogue half) — the release is a same-tick cut and no release-blend
cvar exists. Ruled 2026-09-07 under the owner framework of §7. **Deps:** SC2, SC4, 11.13f, RC5, RC6.

**Closure record.**

---

## 7. Modernization register — ruled

**The owner's decision framework, 2026-09-07.** The port is a **VM host** for VtMB's authored
content. Every *transition* is contract and is reproduced: adopt, re-shot, snap, end, immobilize /
mobilize, the HUD and draw edges, and which camera is live at any instant. A **modernization is
allowed only when it is pixel-only and Unreal already has the algorithm** — it may change what a
frame looks like, never when a state changes, what state exists, or what the next state is.

Under that framework the register below is **ruled, not pending**. Five rows reverse the earlier
recommendation (M1, M2, M3, M5, M7), one is reclassified out of the register (M9), and three
Unreal-native rulings are made explicit (M13, M14, M15) so nothing sits unstated.

| # | Item | Retail evidence | Port today | **Ruling — 2026-09-07 (owner framework)** | Slice |
|---|---|---|---|---|---|
| **M1** | The scripted-shot release | every end path is a same-tick removal — `FUN_10070990`, `EndPlayerDialog` `0x10178400` (an **unconditional** `UTIL_Remove`), `InputRemoveCamera` `0x10171f10`, the four interaction closers, `FUN_10169660`, `FUN_10170090`; **no blend field exists anywhere on `C_BaseCineCamera`** | `Pop` ramps over `RampSeconds` (`ElysiumCameraSolve.cpp:502-520`, `:535-551`); `Resolve` invents `BlendSeconds = 0.5` (`ElysiumCameraShots.cpp:469`); the dialogue releases over 0.25 s (`ElysiumDialogueCamera.cpp:95`); the terminal already cuts (`ElysiumTerminal.cpp:763-773`) | **REPRODUCE THE CUT. No release blend, and no `elysium.CameraShotReleaseSeconds` cvar at all.** The release is a **transition**, not a look: retail returns control on the *same tick* the camera dies — `SetImmobilized(false)`, the weapon restore and the HUD restore all land on that frame. A blend-out composes a dead camera over a player who already has input, and it manufactures a "cine blending out **while** a track ramps in" state retail cannot reach, because `FUN_1017d280` starts with `SetCineCamera(NULL)` and makes the two channels exclusive. Corpus: all 168 shipped `camera_track` chains return with `ToPlayerTime 0` — the content is authored against the cut. The earlier "ease behind a cvar, default 0" recommendation is **reversed**; the cvar is not created | SC2, SC4, SC9 |
| **M2** | The goal-publish cadence | `_DAT_1044eb04 = 0.04165999963879585` (1/24 s), re-applied by `FUN_1006f7d0`, `FUN_1006e770` and `FUN_1006e850`; no cvar or keyvalue touches it. The client tracker runs every **rendered** frame off its own frame latch | `FElysiumCameraDirector::Tick` re-resolves every frame (`ElysiumCameraShots.cpp:604-629`) | **REPRODUCE THE 24 Hz THINK** as the substrate's goal-publish cadence, driven by a `_DAT_1044eb04` accumulator with `DeltaTime` as a parameter; the client-side tracker stays per rendered frame. **That split *is* retail's server/client split**, and the tracker chases a 24 Hz-stepped goal by construction — collapsing them changes what the tracker sees, which is state, not pixels. The think's internal order is kept **verbatim**: anchor cache → look-at (`FUN_1006f670`) → the `+0x594` origin selector → `AutoPositionFromTarget` → the `+0xd4` angle gate → publish → `point_player`. The earlier "every frame; 24 Hz is a 2004 artefact" recommendation is **reversed** | SC4 |
| **M3** | `AttachType` matching | `FUN_10071e00` uses an **exact byte compare including the NUL** (`Follow` 7 bytes, `FollowNoAngles` 15, `FollowEntAngles` 16) — case-sensitive, unlike every other key on the path; a mis-cased value silently becomes `None` | `ESearchCase::IgnoreCase` (`ElysiumCameraShots.cpp:57-64`) | **REPRODUCE THE CASE-SENSITIVE COMPARE.** The parse decides which anchor *state* a shot has — a mis-cased `follow` is `None`, which **latches** the anchor at shot start instead of following it. That is a behavioural fork, not a look, so the leniency is not available. A **parse warning** fires on a case-only mismatch, naming the exact retail spelling and stating that retail reads the value as `None`. The earlier "keep the leniency" recommendation is **reversed** | SC6 |
| **M4** | The replication encoding | `FUN_1018a730`: FOV 10 bits over `[0,180]`, roll 12 bits over `[−180,180]`, fade duration 10 bits over **`[−10,+10]`**, `m_iCameraOverrideIdx` 11 bits | one composed value shot, no wire format | **Skip the bit quantisation; KEEP the encoder clamps as contract.** The bit widths are pixel-only (a rounding on a value with no wire behind it) and Unreal has no equivalent. The **ranges** are not: FOV clamps to `[0,180]`, roll to `[−180,180]`, the fade duration to `±10 s`, and a value outside them is a state retail can never represent. Corpus: max `FromPlayerTime` 1.0 and `ToPlayerTime` 0, so the clamp never bites — it stays as the contract's boundary and warns when it does | SC3 |
| **M5** | The fade encoding | the direction is the **sign** of `m_flCameraOverrideFadeDuration` (`> 0` in, `< 0` out, `== 0` a hard cut in that stays); `FUN_1017d900` is also the **reaper**; `FUN_1017d0b0` re-times by **back-dating** the start by `w·dur`; `FUN_1017d6d0` raises `dur` to each end's minimum and treats `dur ≤ 0` as an **instant snap**; `+0x19d8` is an N-entry crossfade stack; `FUN_1017d280` begins with `SetCineCamera(NULL)` | `Push(Shot)` / `Pop(Id, BlendOutSeconds)`; only the symmetric reversal is reproduced (`ElysiumCameraTests.cpp:192-208`) | **PORT THE FADE STATE MACHINE VERBATIM.** The signed-duration fields, the lazy reap, the back-dated re-time, the per-entity minimums, `dur ≤ 0 ⇒ instant snap`, the N-entry stack and the `SetCineCamera(NULL)` exclusion are all **state**, and the sign is not a compression — it is the stored direction the getter reads. `Pop(Id, BlendOutSeconds)` survives only as a **façade that writes retail's fields**; the sign convention becomes the stored state, not a mapping documented beside a different encoding. The earlier "keep the port's explicit argument" recommendation is **reversed** | SC2, SC3 |
| **M6** | `RemainingTime`'s arithmetic | `FUN_100010f0`: the trapezoid arm's accel distance is `(vmax − v)²/2a`, correct only from `v = 0`; the triangle arm's radicand `2a − 0.5(d − R2)` is an acceleration minus a distance and goes negative whenever `d − R2 > 4a`, NaN-ing `vpeak` and the turn rate with it | `RemainingTranslationSeconds` (`ElysiumCameraSolve.cpp:288-303`) is a *correct* kinematic solve | **Unchanged: reproduce retail's arithmetic verbatim, clamp only the radicand at zero.** The value is a tuning constant shipped content is authored against — for `jack.txt` (500/250) closing 100 u from rest retail returns **0.17 s** and the correct solve **1.27 s**, so the port pans the game's most-seen camera ~7× slower, across 32 more `SyncRotateOnMove` shots. The clamp removes **only the NaN state**, which is unreachable on shipped content: the band needs `MoveSpeed > 2·MoveAccel` and **no shipped `SyncRotateOnMove` shot has it** | SC1 |
| **M7** | `MoveAccel == 0` | `stopDist = v²/(2·0)`; retail's `0/0` compare is **false**, so control takes the decel arm; `Approach(v, 0, 0, dt)` leaves the speed unchanged and `clamp(v, 1.0, MoveSpeed)` pins it at **1 u/s** — the camera crawls | an explicit `Speed = MoveSpeed` arm (`ElysiumCameraSolve.cpp:417-420`) | **REPRODUCE RETAIL.** `MoveAccel == 0` routes to the **decel** arm and the speed pins at the **1.0 u/s floor** (2.54 cm/s). Implemented as an **explicit branch — never a real divide** — because the behaviour is retail's, not the FPU's, and a hardware `0/0` is not a contract. Corpus: no shipped file authors `MoveAccel`, `TurnAccel` or `MoveSpeed` as 0, so nothing shipped changes; a mod that does gets retail's crawl. The earlier "keep `Speed = MoveSpeed`" recommendation is **reversed** | SC1 |
| **M8** | The terminal's camera handle | `CFuncMonitor::vfunc39`, `CPropHacking::vfunc39`, `CPropKeypad::vfunc39` and `FUN_10225070` all reach `FUN_10070470` then `FUN_1017cef0` — the same one-camera slot, with the same destroy-the-previous rule | the terminal owns a separate handle (`ElysiumTerminal.h:344-347`, pushed at `.cpp:644-651`) beside `SetScriptedCamera` (`ElysiumEntityWorld.cpp:997-1023`) | **Unchanged: one adoption slot with retail semantics.** The terminal moves onto it; "which camera is live" is a transition and cannot be arbitrated by a second stack. A terminal closer therefore drops to the **player view**, never to a previously stacked shot | SC4 |
| **M9** | `SimpleSpline` at compose and the point-lerp shape | `FUN_100ffb90`: `e = SimpleSpline(w)`, lerp the origin and the look-at *point* (`origin + fwd × 240 u` — `_DAT_1022b298` = 240.0f, read byte-exact by RC9; the earlier 100 was wrong), re-derive angles with `VectorAngles`, `roll = e × shotRoll`, lerp the FOV | the scripted channel gets the raw linear weight (`Public/ElysiumCameraComponent.h:57`, `ElysiumCameraModifiers.cpp:59`) and lerps `FRotator`s | **RECLASSIFIED — not a modernization and not a divergence to accept.** Retail's composition shape is *how the shot is solved*, and the authored `FromPlayerTime` values were tuned against it, so it is contract like everything else in the register. It stays here only as a **landing note**: it changes the arc of every scripted arrival, so it lands with the `uv run elysium debug shots` vantage baseline refreshed in the **same commit**, with no other change riding along | SC2 |
| **M10** | The vehicle arm | `C_BasePlayer::CalcView` `0x100a7770` runs a vehicle arm before the cine arm | absent | **Unchanged: ruled dead code.** VtMB ships no drivable vehicle and the arm's inputs (`m_bInVehicle`, `field_0x19c4`) have no writer in the image, so there is no transition to reproduce. The *ordering* fact — the cine camera beats a vehicle view — survives as a comment at the compose site | SC2 |
| **M11** | The `bOffCenter` rect's cvar names | `ClientModeShared::OverrideView` sets `v->bOffCenter = 1` and the rect `(−x·0.5, −y·0.5, x·0.5, y·0.5)` from two dev cvars whose names are unrecovered | absent | **Unchanged: the behaviour is ported**, onto `FMinimalViewInfo::OffCenterProjectionOffset`, under `elysium.CameraOffCenterX` / `…Y`. Only the *names* diverge, and only until **RC9** recovers retail's, at which point they win and this row closes | SC2 |
| **M12** | The cine-FOV guard's cvar name | `FUN_10001c20` short-circuits on `DAT_102de30c` and returns its value **without writing `m_flCurFov`**, so the rendered FOV **freezes**; the name and `_DAT_101e34f4` are unrecovered | absent | **Unchanged: the behaviour is ported including the freeze**, under `elysium.CameraShotFovOverride` until **RC9** recovers the retail name. Only the name diverges | SC1 |
| **M13** | The PVS replacement | an active cine camera runs `ResetPVS` + `AddOriginToPVS(m_vecCamOrigin)` and **skips the ordinary visibility pass entirely** (`0x10352120`) | not applicable | **Unreal-native ruling: not applicable.** Unreal culls from the **actual view**, which is what retail was approximating by hand-moving a PVS origin; the algorithm is native and the difference is pixel-only. The behavioural consequence — during a retail shot, entities near the player but far from the lens stop receiving updates — is **recorded as a retail artefact** and deliberately not reproduced | SC3 |
| **M14** | The HUD element set behind `0xa06d` | `CHudManager::HideHud(bits)` (slot 113, `0x10057f50`) walks the element list and `SetVisible(false)`s every element whose `GetHudBits()` (vfunc4) intersects the mask; slot 114 is the twin. Written in exactly three places, all **edge-triggered** on the shot index changing (never per frame), plus the destructor | `bShowHud` solved per frame from the top shot (`ElysiumCameraSolve.cpp:200`) | **Unreal-native ruling, made explicit.** The **element set** the mask names is retail's HUD, and the port's HUD is a new asset (owner rule: menu and HUD are not VtMB reproductions), so `0xa06d`'s membership is **not** reproduced — the port hides *its own* HUD. The **edge timing is contract and is reproduced**: hide on a shot change into a `ShowHud`-clear shot, show on a change into a `ShowHud`-set shot, show on going inactive after having been active and on destruction, and **no per-frame enforcement**, so a HUD-hiding shot replaced by another HUD-hiding shot does not re-issue the call | SC5 |
| **M15** | Hor+ FOV widening at apply time | there is **no** `ScaleFOVByWidthRatio` and no aspect arithmetic anywhere in `client.dll`; all three paths write one `viewsetup.fov` scalar and the engine widens the 4:3-referenced horizontal angle identically | ported as `ElysiumCam::WidenSourceFov` at apply time for all three paths (`ElysiumCameraComponent.cpp:261-262`, `:369-372`; `ElysiumCameraService.cpp:232-237`) | **Unreal-native ruling, made explicit: no change.** The widening is Source projection semantics that Unreal already expresses; applying it at the apply point keeps the parsed shot carrying the number its file wrote, and the weight then lerps two angles in one space. Pixel-only, algorithm native, already landed | — (landed) |

## 8. Recovery tasks

Each opens a named function or datum, lands the answer into `docs/vtmb/camera-view-modes.md`
(§"Not yet recovered" loses the entry) and unblocks the slice that consumes it. They are grouped for
the roadmap; the group's row closes when every task in it has landed.

### RG-A — The entity's own vtable, shot-start state and unshipped modes

| # | Open | Question it answers | Blocks |
|---|---|---|---|
| **RC2** | `CBaseCineCam::vfunc5 / 80 / 81 / 82 / 86 / 103 / 117 / 123`, **including its `KeyValue` handler**, and `camera_showdebug` (a cvar in the same file) | The keyvalue **names** are now read from `.rdata` (§SC4), but the handler is what pins **`spawnflags`** — values `0`, `1`, `3`, `5` and `7` are all shipped across the 51 entities and none of their bit meanings is known. Also: which slots are `Spawn`, `Precache`, `Activate` and `UpdateOnRemove`, since SC4's lifecycle has to land on the right ones | SC4 |
| **RC3** | `FUN_1006e8e0`'s **`+0x564` / `+0x570` / `+0x588` triple** — a saved local origin/angles pair written only when `+0x564` is already non-zero; nothing else in the image reads `+0x570`/`+0x588` | What the saved pose is for. The inference is "remember where an `End`-only shot started dollying from across a re-shot"; if that is right, SC4's shot-start placement must preserve it across `SetShot`, and SC9's "a mid-conversation `SetCamera` leaves the entity where the first shot put it" depends on the same state | SC4 |
| **RC4** | **`player+0x1d60`** — the bitfield `EndShot` clears bits `0x1` and `0x8` of, through `FUN_101815b0` (a plain `&= ~mask`). Chase its **readers** and its setters | What `EndShot` actually releases besides the camera and the immobilize flag. SC4 writes the clear through a *named* accessor, which needs the name | SC4 |
| **RC12** | `FUN_1006fe00` (mode 3 `FollowEntity`) and `FUN_1006f870` (mode 4 `Animated`) in full; `FUN_100705d0` and `FUN_10070690` (their factories); `CCameraAnimated::StartCamera` `0x10071550` and `camera_animated`'s datamap; and **who supplies `DeathCam`'s `Named` corpse anchor** (`grep -ril DeathCam` over the exported corpus returns only the shot file, so the caller is engine-side) | The two unshipped `CamMode` arms in full, so §4 can port them rather than paraphrase them; and the death camera's driver, which `special-case.txt`'s own comment says exists | SC4 |

### RG-B — The track channel's entity interface and the publish stack

| # | Open | Question it answers | Blocks |
|---|---|---|---|
| **RC7** | `CCameraTrack` / `CCameraKeyFrame`'s own classes (`FUN_100cb910`, `FUN_100cb580`) and `vfunc0xBC / 0xC0 / 0xC4 / 0xC8 / 0xCC / 0xD0 / 0xD4` — identified only by use (notify / roll / FOV / origin / target-from / minimum crossfade in / minimum crossfade out). Also **the kind-1 (target) setter**, the twin of `FUN_1017d280` that pushes a target entry onto `+0x19d8`, and `CBaseCombatCharacter::SetAsCameraTarget` `0x1000a2d6` / `GetCameraFadeOutTime` `0x10332240`'s callers | Whether the per-entity **minimum crossfade** is the authored `FromPlayerTime`/`ToPlayerTime` the port already routes or a second source; and the exact push conditions for the target channel. §C1 already settled the kind-0 pusher and the mark/duration writers | SC3 |
| **RC8** | `corpus asm 10352120` (`CHL2_Player::SetupVisibility`) — the per-entry lerp's factor register for each of the three components (the decompile prints `curtime` where the listing multiplies by the entry's fraction). **And the `+0x1f44` / `+0x1f48` FOV-vs-roll disagreement**: the server report reads `+0x1f44` as `m_flCameraFOVOverride` and `+0x1f48` as `m_flCameraRollOverride`; the client report transcribes them the other way round. The SendProp registration `FUN_1018a730` is the tie-breaker, confirmed against the listing | Which of the origin, roll and FOV is folded by the **entry's** fraction and which by the **channel's** accumulated weight; and which replicated slot carries FOV. SC3's stack cannot be written correctly without both | SC3 |

### RG-C — The image constants and the client-mode object

| # | Open | Question it answers | Blocks |
|---|---|---|---|
| **RC9** | **`_DAT_101e34f4`** (the threshold in `FUN_10001c20`'s cine-FOV guard) and the **name** of the ConVar `DAT_102de30c` (one referrer, no recovered constructor); **`_DAT_10235278`** (the near-zero guard on the ramp's negative-duration branch — its sign follows from the branch structure, its value is unread); and the two `bOffCenter` dev-cvar names in `ClientModeShared::OverrideView` | The FOV guard's real threshold and cvar name (M12), the ramp's near-zero boundary (SC2's third regime) and the off-centre cvar names (M11). All four are float/string reads out of the PE | SC1, SC2 |
| **RC10** | **`<DAT_104a57e4>->vfunc40()`** — the base FOV `SetUpView` seeds the view setup with — and **`vfunc38`** (intermission) and **`vfunc39`** (the spectator target) on the same object, whose class is unnamed in the corpus | Where the base FOV comes from before any override, and the exact spectator-replace test (`an entity index above the max client`) that is the death/observer view. SC2's chain seeds and its last `CalcView` arm both read them | SC2 |

### RG-D — The dialogue admission and the gaze gate

| # | Open | Question it answers | Blocks |
|---|---|---|---|
| **RC5** | **`FUN_10325da0`** — the NPC head-turn feasibility gate on `DialogPOV`, called by `CAI_BaseNPC::FUN_1026b810` for **both** the camera and the player-eye arm | Whether a `DialogPOV 1` shot actually redirects the NPC's gaze, and what refuses it. SC9 lands the gate; without the recovery it would be a guess about which axis limits apply | SC9 |
| **RC6** | **`FUN_10178120`** (the dialogue manager) and **`FUN_100e05f0`** (its "can this conversation start" test), plus `FUN_10178170`, `FUN_101cebc0`, `FUN_10167fd0`, `FUN_10147a60`, `FUN_100826b0`; and the payphone RTTI class with `StartGrappleAttack(this, npc, 5)`'s mode-5 semantics. **And the Python bridge's dialogue-advance path** | Closes the last hop on "no per-line camera reaches the engine" — the claim rests on `SetCamera` having exactly one caller and on `vampire.dll` containing no `.dlg` string, which is strong but is not a read of the bridge. Also gives SC9's opener its exact refusal path and the payphone arm's identity | SC9 |

### RG-E — The anchor kernels and the grapple role

| # | Open | Question it answers | Blocks |
|---|---|---|---|
| **RC1** | **`FUN_1013c940`**, the closest-point-on-line kernel behind `FUN_1013ca00` | Whether `t` is clamped to `[0,1]`. If retail clamps, `AutoPositionFromTarget`'s pull-back changes when the lower target point projects **behind** the camera — a one-line difference with a visible result on `centerfullview` | SC7 |
| **RC11** | **`client.dll FUN_10028a10`** — the client's mirror of the server anchor parser `FUN_10071e00` — and the client half of `m_bDrawPlayer` (`0x464`), which has **no server reader at all**, so its whole behaviour lives there | Whether the two parsers agree. Both halves must, because the client tracker reads the client record and the server think reads the server one; a divergence would mean two different grammars | SC5, SC6 |
| **RC13** | The **grapple role pair** `+0x1538` (partner EHANDLE) / `+0x153c` (role, `-1` none) — every writer, via `CBaseCombatCharacter::CanStartGrappleAttack` `0x103285a0`, `CPlayerMove::SetupMove` `0x10186120`, `CBasePlayer::GetSaveBlockedReason` `0x10174f80`, `CBaseCombatCharacter::ChooseMeleeAttackSequence` `0x10347180` and `StartGrappleAttack` | The exact transitions that set and clear the role, so SC6's substrate state lands on the same edges the feed, stealth-kill and melee-grapple paths already have rather than on an invented lifetime | SC6 |

**Settled by this pass, no task needed** (recorded so they are not re-opened): `FUN_10002200` is
confirmed **empty** (`RET`); `OffsetAngles` (`+0x20`, flag `0x40000`) has **no reader anywhere in
`vampire.dll`**; the `+0x19d8` kind-0 pusher and the `m_flCameraOverrideFadeMarkTime`/`Duration`
writers are `FUN_1017d280` / `FUN_1017d0b0` / `FUN_1017d6d0` / `FUN_1017d900` (§C1); the settle-flag
clearing is confirmed retail, not a port assumption; there is no hidden integration constant in the
position step; `_DAT_1044eb04` reads `0.04165999963879585`; the datamap key names for `+0x5d4`,
`+0x5d8`–`+0x5e4`, `+0x5e8` and `+0x640` are read, not inferred. The intent behind
`FUN_100010e0(a,b) = sqrt(2a − b)` and its `0.5*(d − R2)` argument stays unknown and needs no task —
the bytes are unambiguous and **M6** reproduces them.

---

## 9. Acceptance

### Substrate tier — per slice

`uv run elysium test Elysium.Substrate.Camera` and `…DialogueCamera` green, with the new tests of §6:
`CameraTracker` (SC1) · `CameraCompose` (SC2) · `CameraOverride` (SC3) · `CameraCinematic` (SC4) ·
`CameraShotStart` + the extended `CameraDraw` (SC5) · `CameraAnchors` (SC6) · `CameraAutoPosition`
(SC7) · `CameraFindBestShot` (SC8) · `DialogueCamera.RetailChain` (SC9). The existing
`Elysium.Substrate.Camera`, `.CameraDraw`, `.CameraRig`, `.CameraTrack`, `.CameraShots` and
`.ViewState` stay green throughout.

### Content tier — the whole shot corpus

**`Elysium.Content.CameraShotGrammar`** (new) parses **all 66 files** under
`$ELYSIUM_WORK_ROOT/exports/vdata/camerashots/`, the how-to included, and asserts:

- every `Position`, `AttachPos` and `AttachType` token the corpus writes has a **resolver**, so an
  unimplemented keyword fails loudly instead of silently becoming `World` or `None`;
- the shipped `CameraConstraints` census holds. **Measured 2026-09-07 (SC6): the six numbers below
  were labelled "files" and are in fact "shots that write the key", and two of them were wrong.**
  Written by 59 / 34 / 9 / 5 / **4** / **3** shots (`DialogPOV`, `SyncRotateOnMove`,
  `SnapOnShotChange`, `AutoPositionFromTarget`, `ShowHud`, `DrawViewmodel`); **set to 1** by
  50 / 33 / 7 / 3 / 2 / 1. The corpus is 66 files parsing to **72** shots;
- `GrappleAttacker` ×6 and `GrappleVictim` ×2, all in `stealth_kill.txt`; `AbsMin`/`AbsMax` once each
  in `centerfullview.txt`; exactly **one** `Start { AttachType None }` in the corpus
  (`special-case.txt`'s `Follow`); ten files author a `Start`;
- **no shipped `SyncRotateOnMove` shot reaches the `RemainingTime` NaN band** (`MoveSpeed >
  2·MoveAccel`), which is the evidence M6's clamp changes no shipped behaviour;
- the five shots in `special-case.txt` are read as five (§C2), and `DeathCam` / `Animated` parse.

Beside it, **`Elysium.Content.CineCameraDirectors`** (new) asserts the 51 shipped `camera_cinematic`
entities: the authored key counts of SC4, that **none** authors a `drawplayer`-shaped key, that
`shotname` appears both as a `.txt` path (43×) and bare (8×) and both normalize to one shot, and the
`spawnflags` value set `{0, 1, 3, 5, 7}` (which RC2 turns from a census into a meaning).

### Play tier — five witnesses

| Witness | Beats | What it proves |
|---|---|---|
| **`Elysium.Play.TutorialCamera`** — `sp_tutorial_1` | (a) `logic_scene_1` → `trackb00` as position and `focusb00` as target at +0.25 s, 38.05 s of paired authored track, `logic_shot_end` restoring control; (b) `sTalkguy_move.OnBeginSequence → feedcamera.StartShot`, `sTalkguy_die.OnEndSequence → feedcamera.EndShot`; (c) both Jack conversations — `Jack.StartPlayerDialogRemote` at `teleport_fade.OnBeginFade +4 s` and again on track completion at +1.0 s | (a) SC2's composition shape, the ease, the negative-duration blend-out and the channel split; SC3's crossfade stack under back-to-back owner replacement. (b) SC4's whole entity — the four targetname anchors, the adoption slot, the destroy rule, immobilize/mobilize, the hard-cut end and `point_player 0`; SC1's **one-shot** `SnapOnShotChange` (`LookAtTarget_Snap` sets it); SC5's `Named`-anchor draw policy. (c) SC1's 1.0° deadband, 1.0 u/s floor, dt clamp/floor and **M6**'s pan rate on `jack.txt` (500/250, `MaxTurnRate [60,60,60]`, `AngularTolerance [10,10,10]`, FOV 40, `DialogPOV 1`, `SyncRotateOnMove 1`); SC5's `End`-without-`Start` **live-view dolly-in**, which is exactly that shot's shape; SC9's immobilize + holster on open and the unconditional remove + cut on close |
| **`Elysium.Play.TheatreCamera`** — `sp_theatre` | the `courtroom_*` and `walk_out_*` chains (87 authored `MoveTime 0.03` edits) and the `embrace_*` chain (28 exact zeros) | SC2's composition shape does not turn the authored cuts into slews, and SC3's crossfade stack survives 115 folded edits and repeated owner replacement without drift. The `Activate` fold and the scheduler are already landed; this is the interaction between them and the new composition |
| **`Elysium.Play.DialogueCamera`** — a dialogue map with per-line `SetCamera` calls | a conversation whose `.py` sets a per-line shot, plus one line whose shot name does not load | SC9's `DialogDefault` fallback on the re-shot arm; the re-shot branch continuing from the pose the first shot established; `DialogPOV`'s feasibility gate; `SetCamera` **not** immobilizing while `StartPlayerDialog` does |
| **`Elysium.Play.StealthKillCamera`** — a map with a stealth kill | anim event **4050** with `Stealth_Kill` as its `options`, then **4051** | SC8's `FindBestShot` over `Stealth_Kill_1..4` (enumeration, both predicates, the seeded uniform pick); SC6's `GrappleAttacker`/`GrappleVictim` resolve through the live role pair; SC5's body gate driven by 4050's `m_bDrawPlayer = 1` — the **only** non-zero writer in the game; 4051's destroy + eye-level snap; neither event immobilizing |
| **`Elysium.Play.TerminalCamera`** — the Hacking and Intrusion terminals | opening a `func_monitor` / `prop_hacking` and a lockpick | **M8**: the terminal on the shared adoption slot with retail's destroy rule; `special-case.txt`'s `Hacking` (`ShowHud 1`, `DrawViewmodel 0`) and `Intrusion` (`ShowHud 1`, `DrawViewmodel 1`) driving SC5's HUD edge and the **speed-gated** viewmodel — the hands appear only after the dolly parks; both blocks take `AttachType Follow`, so the shipped terminal path is unaffected by SC6's latch |

Vantages are captured at weight 0, 0.5 and 1 on every Play-tier witness. **Landing SC2 changes the arc
of every scripted arrival in the game** — retail's composition shape is contract, not a modernization
(**M9**, reclassified), and the authored `FromPlayerTime` values were tuned against it — so the
`uv run elysium debug shots` vantage baseline is refreshed in the **same commit** that lands it, with
no other change riding along.

Two acceptance properties follow from the framework rather than from any one slice and are asserted
wherever a shot ends: **a released shot cuts, and the next frame is the player view**; and the
mobilize, the weapon restore and the HUD restore land on **that same frame**, never one later.

### Live checks

Owner-piloted, **last**, after every tier above is green: the tutorial's feed beat and both Jack
conversations, sp_theatre's opening, a stealth kill and a terminal session, driven from real input.
Per `docs/project/roadmap.md`'s standing rule, dev shortcuts are never acceptance evidence.

---

## 10. Progress ledger

One row per slice and per recovery task. **Rows carry no status marks** — `docs/project/roadmap.md`
owns status; these are the tracking keys the roadmap rows point at.

| id | name | roadmap row | evidence when landed |
|---|---|---|---|
| **SC1** | The client tracker's numerics and the frame contract | P11 · `camera_scripted.md#sc1--the-client-trackers-numerics-and-the-frame-contract` | `Elysium.Substrate.CameraTracker` green · `camera-view-modes.md` tracker section + `retail-defects.md` §7 updated · commit |
| **SC2** | The two channels, and the `CInput` composition shape | P11 · `#sc2--the-two-channels-and-the-cinput-composition-shape` | `Elysium.Substrate.CameraCompose` green, `.CameraTrack` extended · §"The scripted-shot channel" · shots baseline refreshed in the same commit (M9) · commit |
| **SC3** | The camera-override fade machinery and the crossfade stack | P11 · `#sc3--the-camera-override-fade-machinery-and-the-crossfade-stack` | `Elysium.Substrate.CameraOverride` green · §"The `camera_track` override channel" · commit |
| **SC4** | `camera_cinematic`: the director entity, the adoption slot, `CamMode`, the immobilize pair | P11 · `#sc4--camera_cinematic-the-director-entity-the-adoption-slot-cammode-and-the-immobilize-pair` | `Elysium.Substrate.CameraCinematic` + `Elysium.Content.CineCameraDirectors` green · §"The server shot lifecycle" + `computer-terminals.md` (M8) · the stub row deleted · commit |
| **SC5** | Shot start, the draw gates and the HUD edge | P11 · `#sc5--shot-start-the-draw-gates-and-the-hud-edge` | `Elysium.Substrate.CameraShotStart` green, `.CameraDraw` extended · the draw-gate section · commit |
| **SC6** | The anchor grammar completed | P11 · `#sc6--the-anchor-grammar-completed` | `Elysium.Substrate.CameraAnchors` + `Elysium.Content.CameraShotGrammar` green · the anchor table + `stealth.md` · commit |
| **SC7** | `AutoPositionFromTarget` and the origin selector's third arm | P11 · `#sc7--autopositionfromtarget-and-the-origin-selectors-third-arm` | `Elysium.Substrate.CameraAutoPosition` green · the `AutoPositionFromTarget` entry · commit |
| **SC8** | `FindBestShot` and the anim-event channel (4050 / 4051) | P11 · `#sc8--findbestshot-and-the-anim-event-channel-4050--4051` | `Elysium.Substrate.CameraFindBestShot` green · `animation_events.md` 4050 row corrected, both events off the unclaimed list · commit |
| **SC9** | The dialogue and script chain around the camera | P11 · `#sc9--the-dialogue-and-script-chain-around-the-camera` | `Elysium.Substrate.DialogueCamera.RetailChain` green · §"How dialogue drives the camera" · **RE46** closes · commit |
| **RC1** | `FUN_1013c940` — the closest-point-on-line kernel | RG-E | the clamp answered in `camera-view-modes.md`; SC7's assumption comment resolved |
| **RC2** | `CBaseCineCam`'s own vtable and `KeyValue` handler | RG-A | `spawnflags` bit meanings recorded; SC4's lifecycle on the right slots |
| **RC3** | `FUN_1006e8e0`'s `+0x564` / `+0x570` / `+0x588` triple | RG-A | the saved pose's purpose recorded; SC4/SC9's re-shot behaviour justified |
| **RC4** | `player+0x1d60`'s readers | RG-A | the bitfield named; SC4's clear written through a named accessor |
| **RC5** | `FUN_10325da0` — the head-turn feasibility gate | RG-D | the gate recorded; SC9's `DialogPOV` gate implemented, not stubbed |
| **RC6** | `FUN_10178120` / `FUN_100e05f0` and the Python dialogue-advance path | RG-D | "no engine-side per-line camera" closed by a read of the bridge; the payphone arm named |
| **RC7** | `CCameraTrack`/`CCameraKeyFrame` and `vfunc0xBC`–`0xD4`; the kind-1 pusher | RG-B | the minimum-crossfade source and the target push conditions recorded; SC3's stack unblocked |
| **RC8** | `corpus asm 10352120` lerp registers; the `+0x1f44`/`+0x1f48` identity | RG-B | the three fold factors and the FOV/roll slot settled in the doc's replication table |
| **RC9** | `_DAT_101e34f4`, `DAT_102de30c`'s name, `_DAT_10235278`, the two off-centre cvar names | RG-C | four values/names in the doc; **M11**/**M12** either close or stay named |
| **RC10** | `<DAT_104a57e4>->vfunc38 / 39 / 40` | RG-C | the base FOV seed and the spectator-replace test recorded; SC2's chain exact |
| **RC11** | `client.dll FUN_10028a10` and the client `m_bDrawPlayer` half | RG-E | parser parity recorded; SC5/SC6 land against both halves |
| **RC12** | `CamMode` 3 and 4 in full, `camera_animated`, and `DeathCam`'s driver | RG-A | the two arms and the death camera's entry point recorded; SC4 ports all five modes |
| **RC13** | The grapple role pair `+0x1538` / `+0x153c` — every writer | RG-E | the role transitions recorded; SC6's substrate state lands on real edges |

---

## 11. Doc debt this plan clears

`docs/vtmb/camera-view-modes.md` — §"The scripted-shot channel" (the two-channel split, the composition
shape, the director/runtime split, the read key names, the corpus counts, the immobilize chain, the
`SetCamera`/`StartPlayerDialog` fallback asymmetry, `FindBestShot` and the anim-event entry point);
§"The shot record, the anchor resolve, and the client tracker" (the settle-flag assumption and the
approach-constant caveat retired, the live-view arm, `AbsMin`/`AbsMax`, the grapple keywords, the
corrected `Top`/`Bottom`, the corrected `Follow` offset frame, the settled `OffsetAngles` question);
§"The client tracker's exact numerics"; §"The draw gates and the HUD mask"; §"The `camera_track`
override channel"; the **"Divergences from retail in the scripted camera"** table, every row of which
either resolves or becomes one of M1–M13; §"Not yet recovered", which loses every entry RG-A–RG-E
closes and gains none. · `docs/vtmb/retail-defects.md` §7 — the `RemainingTime` reproduce/clamp split,
the `Point2`-alone reproduction and the anchor-0 latch bug, all as landed. ·
`docs/vtmb/animation_events.md` — 4050's `options` corrected from "raw entity classname" to "the shot
base name"; 4050 and 4051 leave the unclaimed audit. · `docs/vtmb/computer-terminals.md` — the
terminal's move onto the shared adoption slot (M8). · `docs/vtmb/choreographed_scenes.md` — the
"Player pawn versus cinematic double" passage gains the pointer that the `camera_cinematic` lock now
exists in the port. · `docs/vtmb/stealth.md` — the stealth-kill shots read the grapple role pair. ·
`docs/architecture/camera-architecture.md` — the two-channel structure and the adoption slot. ·
`docs/project/roadmap.md` — **RE46** closes with SC9.
