# The scripted-camera subsystem — `CBaseCineCam`, `C_BaseCineCamera` and the override channel

> **Open-items note**, owned by `docs/project/plans/spine.md` §11.13. It carries **no task status** —
> `docs/project/roadmap.md` owns that — and it is not a facts document: the recovered retail facts are
> in `docs/vtmb/camera-view-modes.md`, `docs/vtmb/retail-defects.md`,
> `docs/vtmb/animation_events.md` and `docs/vtmb/computer-terminals.md`. What is left here is the
> subsystem's scope, the owner's ruled register, and the work that is still open.
>
> The raw decompiles the recovery rests on are under `$ELYSIUM_WORK_ROOT/_camera_recovery/` and are
> the recovery record —
> [`server_cine_camera.md`](file:///E:/elysium-work/_camera_recovery/server_cine_camera.md) (the
> `vampire.dll` half), [`client_cine_camera.md`](file:///E:/elysium-work/_camera_recovery/client_cine_camera.md)
> (the `client.dll` half),
> [`plan_gap_ledger.md`](file:///E:/elysium-work/_camera_recovery/plan_gap_ledger.md) (the
> per-behaviour ledger and the corpus sweep) and the per-question reports beside them
> (`rc_living_fov.md` and the rest).

---

## Scope

The subject is VtMB's scripted-camera subsystem, entire — six things retail runs as one:
`camera_cinematic` / `CBaseCineCam` (the map entity, the director/runtime split, `SetShot`, the anchor
resolve, the five `CamMode` arms, the 24 Hz think, `AutoPositionFromTarget`, `point_player`,
`FindBestShot`, the immobilize pair, the same-tick teardown); `C_BaseCineCamera` (`OnDataChanged`,
the two shot-start arms, the frame-latched tracker, the one-shot snap, the three hard-cut exits); the
`CInput` override channel and the server-side fade machinery; the view-composition chain
`CViewRender::SetUpView` → `CalcView` → `ClientModeVampire::OverrideView` → `C_BasePlayer::CalcView` →
`ClientModeShared::OverrideView`; the drivers (the entity input, the `SetCamera` native, the dialogue
opener, the 4050/4051 anim-event channel and the interaction shots); and the
presentation gates (`m_bDrawPlayer`, the speed-gated `DrawViewmodel` predicate, the edge-triggered
`HideHud(0xa06d)` mask).

**The owner's framework, ruled 2026-09-07.** The port is a **VM host** for VtMB's authored content:
every *transition* — adopt, re-shot, snap, end, immobilize / mobilize, the HUD and draw edges, and
which camera is live at any instant — is contract and is reproduced. A modernization is admissible
only when it is **pixel-only and Unreal already has the algorithm**: it may change what a frame looks
like, never when a state changes, what state exists, or what the next state is. Nothing in this
subsystem is parked as a seam that answers "nothing".

---

## Ruled modernization register

Rulings made 2026-09-07 under that framework. They are standing decisions, not status: each is the
answer for anyone who later meets a divergence from retail in this subsystem.

| # | Item | Ruling — 2026-09-07 |
|---|---|---|
| **M1** | The scripted-shot release | **Reproduce the cut.** Every retail end path — `FUN_10070990`, `EndPlayerDialog` `0x10178400` (an unconditional `UTIL_Remove`), `InputRemoveCamera` `0x10171f10`, the four interaction closers, `FUN_10169660`, `FUN_10170090` — is a same-tick removal, and no blend field exists anywhere on `C_BaseCineCamera`. No release-blend cvar is created either. Corpus: all 168 shipped `camera_track` chains return with `ToPlayerTime 0` |
| **M2** | The goal-publish cadence | **Reproduce the 24 Hz think** (`_DAT_1044eb04 = 0.04165999963879585`) as the goal-publish cadence, with the tracker still advancing once per rendered frame. The two-clock split *is* retail's server/client split; the think's internal order is kept verbatim (anchor cache → look-at → the `+0x594` origin selector → `AutoPositionFromTarget` → the `+0xd4` angle gate → publish → `point_player`) |
| **M3** | `AttachType` matching | **Reproduce the case-sensitive exact byte compare** (`FUN_10071e00`). A mis-cased `follow` is `None`, which *latches* the anchor instead of following it — a behavioural fork, not a look. A parse warning fires on a case-only mismatch and names retail's spelling |
| **M4** | The replication encoding | **Skip the bit quantisation; keep the encoder clamps as contract.** The bit widths (`FUN_1018a730`) are pixel-only with no wire behind them; the ranges are state — FOV `[0,180]`, roll `[−180,180]`, fade duration `±10 s` — and warn when one bites. Corpus: max `FromPlayerTime` 1.0, `ToPlayerTime` 0, so the clamp never bites on shipped content |
| **M5** | The fade encoding | **Port the state machine verbatim.** The signed duration is the *stored* direction, not a compression; the lazy reap inside `GetCameraOverrideWeight`, the back-dated re-time (`FUN_1017d0b0`), the per-entity `max()` minimums, `dur ≤ 0 ⇒ instant snap`, the N-entry `+0x19d8` crossfade stack and `FUN_1017d280`'s `SetCineCamera(NULL)` channel exclusion are all state |
| **M6** | `RemainingTime`'s arithmetic | **Reproduce `FUN_100010f0` verbatim, clamping only the radicand at zero.** The value is a tuning constant shipped content is authored against (`jack.txt` at 500/250 over 100 u: retail 0.17 s, a correct solve 1.27 s). The clamp removes only the NaN state, which needs `MoveSpeed > 2·MoveAccel` and is unreachable on shipped content |
| **M7** | `MoveAccel == 0` | **Reproduce retail's crawl** — the decel arm and the 1.0 u/s (2.54 cm/s) speed floor — through an **explicit branch, never a real divide**: the behaviour is retail's, not the FPU's. No shipped file authors `MoveAccel`, `TurnAccel` or `MoveSpeed` as 0 |
| **M8** | The terminal's camera handle | **One adoption slot with retail semantics**; "which camera is live" is a transition and cannot be arbitrated by a second stack. A terminal closer therefore drops to the **player view**, never to a previously stacked shot |
| **M9** | `SimpleSpline` at compose and the point-lerp shape | **Reclassified out of the register — contract, not a modernization.** `FUN_100ffb90` (`e = SimpleSpline(w)`, lerp the origin and the look-at *point* at `origin + fwd × 240 u` (`_DAT_1022b298`), re-derive angles with `VectorAngles`, `roll = e × shotRoll`, lerp the FOV) is how the shot is solved, and the authored `FromPlayerTime` values were tuned against it. It changes the arc of every scripted arrival, so it lands with the vantage baseline refreshed in the **same commit**, nothing else riding along |
| **M10** | The vehicle arm in `C_BasePlayer::CalcView` `0x100a7770` | **Ruled dead code.** VtMB ships no drivable vehicle and the arm's inputs (`m_bInVehicle`, `field_0x19c4`) have no writer in the image, so there is no transition to reproduce. The *ordering* fact — the cine camera beats a vehicle view — survives as a comment at the compose site |
| **M11** | The `camortho` rect (originally read as an off-centre projection) | **Re-scoped by RC9.** The block is Source's *orthographic debug view*, not an off-centre rect, and both cvar names are recovered (`c_orthowidth` / `c_orthoheight`, `FCVAR_ARCHIVE`, default `100`). What remains is one **pixel-only** divergence: retail stretches the rect over whatever the window is; Unreal frames the same `w × h` of world undistorted and letterboxes, because `FMinimalViewInfo` carries one ortho extent plus an aspect |
| **M12** | The cine-FOV guard | **Closed by RC9.** The behaviour including the FOV freeze (`FUN_10001c20` returns the cvar value without writing `m_flCurFov`) is reproduced, and the retail name, default and threshold are recovered — `camera_fov`, default `"-1"`, threshold `_DAT_101e34f4 = 10.0f` — so retail's name governs and no placeholder remains |
| **M13** | The PVS replacement | **Not applicable.** Unreal culls from the **actual view**, which is what retail approximated by hand-moving a PVS origin (`ResetPVS` + `AddOriginToPVS(m_vecCamOrigin)`, `0x10352120`); the algorithm is native and the difference is pixel-only. The behavioural consequence — entities near the player but far from the lens stop receiving updates during a retail shot — is a **retail artefact, deliberately not reproduced** |
| **M14** | The HUD element set behind `0xa06d` | **Membership not reproduced; edge timing is contract.** The mask names retail's HUD elements and this project's HUD is a new asset (standing owner rule: menu and HUD are not VtMB reproductions). The **edges** are retail's: hide on a shot change into a `ShowHud`-clear shot, show on a change into a `ShowHud`-set shot, show on going inactive after having been active and on destruction, and **no per-frame enforcement** |
| **M15** | Hor+ FOV widening at apply time | **No change; Unreal-native.** There is no `ScaleFOVByWidthRatio` and no aspect arithmetic anywhere in `client.dll`; all three paths write one `viewsetup.fov` scalar and the engine widens the 4:3-referenced horizontal angle. Applying the widening at the apply point keeps the parsed shot carrying the number its file wrote. Pixel-only |
| **M16** | `camera_showdebug`'s geometry overlay | **The cvar stays declared and no overlay is drawn.** `camera_showdebug 1` does nothing on live retail — the draw path is dead in the shipped build — so there is no behaviour to reproduce and no diagnostic value in rebuilding one. The `ConVar` at `0x1006d5b0`, its two `!IsCommand() && GetInt() == 1` readers and the recovered geometry stay recorded with the retail facts. Dev-only: no state, no transition |

---

## What is left

### Recovery still open — outside the camera boundary

The camera work reached these and left them to the lane that owns them. Each names what would close
it; none of them is a camera-side unknown.

| Open | What it is in retail | Closes when |
|---|---|---|
| victim-side `CheckAndTranslateGrapplePosition` (`0x10328af0`) | the variant chosen for `m_GrapplePosition` (`+0x1544`) — the *placement* half of a grapple. The payphone grapple writes the facing but leaves both bodies where they stand | the feed / grapple lane opens the victim arm and lands it into `docs/vtmb/stealth.md` and `docs/vtmb/source_movement.md` |
| the lock HUD progress byte `+0x816`, and `CWeaponLockpick`'s attempt / success / botch feedback | slot 41's last-roll byte behind the Intrusion HUD packet, and the lockpick's player-facing feedback on each roll | the lock lane opens `CWeaponLockpick` and records both in `docs/vtmb/computer-terminals.md` |
| `PackDeadPlayerItems` (slot `0x6e4`) | what retail does with the player's items at death, called from the death chain | the death / inventory lane opens the slot |
| the client ragdoll, `EF_NODRAW` and the `MOVETYPE_NONE` death freeze | `CBaseCombatCharacter::Event_Killed` → `CreateCorpse` → `BecomeClientRagdoll`, the server model hidden, and the mover nailed down for the whole death | the **body lane** (ragdoll + `EF_NODRAW`) and the **movement lane** (the mover freeze). Until the freeze lands, a held movement key still walks the corpse between the kill and the game-over screen |
| a re-modelled `camera_animated` and `FElysiumAnimating::OnRuntimeModelChanged` | `CamMode 4` samples `cam_bone` on the entity's own model, so a runtime re-model must rebuild the bone lookup | the animation lane (`docs/project/plans/animation.md`) reaches the re-model hook |

### Owner actions

- **The M9 vantage baseline refresh.** `uv run elysium debug shots` on an RHI run, rebaselining every
  scripted-arrival vantage, landed in the same commit as the composition-shape change with nothing
  else riding along.
- **Live checks, owner-piloted, last.** The tutorial's feed glue; both Jack conversations;
  `sp_theatre`'s opening; a stealth kill; a terminal session — driven from real input. Per
  `docs/project/roadmap.md`'s standing rule, dev shortcuts are never acceptance evidence.
- **The payphone NPCs**, on each shipped map that places an `npc_payphone`: the grapple admission,
  the mode-5 yaw round trip and the cameraless conversation, confirmed live.
- **The 60° living lens.** Retail's play lens is the `m_iFOV == 0 ⇒ 60` latch, not `default_fov 75`;
  the change is global and wants an owner's eye on real play before it is treated as settled.

### Test seams a future witness needs

Not behaviour — access. Each is what a headless witness would have to reach to assert a transition it
cannot see today.

- `UElysiumCameraComponent::HudGate()` and `ShotTracker()` as const accessors, so the HUD edge and the
  tracker's settle state can be read without driving a render.
- A **director-tick seam** on the map actor, so the 24 Hz publish (M2) can be stepped from a test at a
  chosen `DeltaTime` instead of being driven by the world tick.
- `FElysiumMapSlice` closing over `NextKey`, so a slice can author successive shot changes and assert
  which frame each edge lands on.
