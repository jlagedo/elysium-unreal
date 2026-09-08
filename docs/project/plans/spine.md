# Runtime spine plan — open-task specifications

Specifications for **open** P11 tasks plus the travel/packaging infrastructure. Status lives
solely in `docs/project/roadmap.md`; a task that lands is deleted here. No status marks in this
file. Design: `docs/architecture/runtime-architecture.md`,
`docs/architecture/camera-architecture.md`, `docs/architecture/save-architecture.md`,
`docs/architecture/map-architecture.md`.

### 10.4 Async travel state machine

The `docs/architecture/map-architecture.md` design (fade → unload → task-thread parse → spawn →
fade in), built on the 10.8 OpenLevel foundation. **Trigger: when synchronous hitches start to
matter, not before.** *Deps:* 10.8.

### 10.5 Packaged-build content path

`content/` next to the exe, the packaging story, Shipping config sweep (debug layer compiled
out), the `GameInputRedist.msi` prerequisite (Windows 10 19H1 floor). The gitignored
`/ElysiumBaked` mount joins this story: a package ships a bake-on-first-run path or the
user-side bake tooling. *Deps:* none until first package.

### 11.10 Play test tier

A fourth automation tier driving a real headless world: the beat-script driver
(`do`/`wait`/`assert`/`shot` over the command registry, injected input, `G`/quest/entity
predicates and the shot baseline), command-stream replay, the save round-trip, and matching MCP
tools (`input_inject`, `beat_run`, `save`/`load`, `time`). *Acceptance:*
`uv run elysium test Play` walks the tutorial opening unassisted and fails loudly on a beat
regression — P9's slice acceptance becomes a CI run. *Deps:* 11.6, 2.7, 2.9.

### 11.13 Reconstruction camera director — remaining

11.13a–c are landed via CCC2. Remaining:

- **11.13d Input, settings and presentation** — camera commands enter the action catalog;
  inspect/dialogue/cinematic scopes stay owned by `UElysiumInputSubsystem`; resolved reticle,
  HUD, body/viewmodel and letterbox state enters `FElysiumViewState`; accessibility covers
  separate FOV, recenter, shake/head-motion/recoil response and motion blur. The
  `UElysiumCameraProfile` asset and user-settings surface land here/8.10. *Deps:* CCC2, 8.10
  for the final options surface.
- **11.13e Prop focus, map triggers and public API** — focusable target specs, soft-focus and
  inspect requests, collision/framing fallback, trigger component/volume, C++ value API,
  embedded Python opaque handles, map-epoch teardown, compatibility-safe
  `SetCamera`/`RemoveCamera` ownership. The camera never moves or rotates the player to frame
  an item. *Deps:* CCC2, 11.13d; real inventory/interaction coverage joins 9.8 and B6.
- **11.13f Dialogue director — remaining** — the scoped request, source-shot-first selection,
  deterministic grammar, body-owner transaction, save refusal, diagnostics and
  headless/content coverage are landed; controlled UP Plus capture and played
  resolution/input acceptance remain. *Deps:* 11.13d, 9.2; played first-conversation coverage
  joins 9.9.
- **11.13g Sequencer bridge** — project-authored Level Sequences and Cine Cameras acquire one
  `Sequence` request; Camera Cut Track owns authored transforms/lenses/cuts/blends with no
  second interpolation; stop, abort, skip and travel release cleanly. Original VCD/Worldcraft
  timing stays in the legacy evaluator. *Deps:* CCC2.
- **11.13h Integration acceptance** — migrate feed/death and every remaining direct producer;
  retain the theatre's 12.1 camera acceptance; request/focus/dialogue/Python/Sequencer
  automation over the whole director; every scoped camera returns to the exact chosen view and
  no camera path rotates or navigates the character. The player-view half is CCC8's. *Deps:*
  11.13d–g, 9.8, 9.9, 11.10.

#### The scripted-camera subsystem — the recovered `CBaseCineCam` surface (was 11.13i–p)

The whole scripted-camera subsystem — `camera_cinematic` / `CBaseCineCam`, `C_BaseCineCamera`, the
`CInput` override channel, the view-composition chain, the dialogue / script / anim-event / terminal
drivers and the draw gates — has its own **open-items note**,
[`docs/project/camera_scripted.md`](../camera_scripted.md). It carries the subsystem's scope, the
owner's ruled modernization register M1–M16, and what is still open: the recovery rows that fall
outside the camera boundary, the owner actions, and the test seams a future witness needs. The
recovered retail facts are in `docs/vtmb/camera-view-modes.md` and its sibling `docs/vtmb/` files;
the raw decompiles are at `$ELYSIUM_WORK_ROOT/_camera_recovery/`. Status lives in
`docs/project/roadmap.md`.
