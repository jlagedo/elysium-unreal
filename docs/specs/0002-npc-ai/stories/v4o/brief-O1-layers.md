# Brief O1 — V4o: the NPC's four overlay layers (coder; no build)

Read `README.md` here (§1 "The layer", §2 P1–P4, P7, §4 "Shared names", "From V4a", §6, §7),
`docs/vtmb/animation_events.md` § "Overlay layers dispatch their own timelines…" (look it up:
`uv run elysium research section 0x10098cd0`), `docs/vtmb/animation_and_movers.md` § "The layer
table's own bookkeeping" (`research section 0x100991b0`). After V4a's commit **and V4b's**
(amended after settling packet S3, 2026-10-04; `../v4/packets-S3.md` items 1 and 6 are the
listing reads behind item 3 below). Re-locate every site by Grep on the function name.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumAnimatingOverlaySlotBodies.cpp`
- `Source/ElysiumUE/Public/ElysiumAnimatingOverlaySlotBodies.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnim.cpp` (`FElysiumNpcBase::StudioFrameAdvance` only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnimEvents.cpp` (the four-layer seam only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcAnim.cpp`, `ElysiumNpcAnim.inl` (the row words and
  the draw hook only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelOverlayTests.cpp` (new)

## The job

1. **The four slot bodies**, hand-written in `ElysiumAnimatingOverlaySlotBodies.cpp` under their
   slot names (the generated empty bodies in `ElysiumAnimatingOverlaySlots.cpp` are removed by the
   integrator's regeneration — do not touch that file): `AllocateLayer` `0x10099470` (lowest slot
   with `Weight == 0`, else −1, from `GetFirstGestureLayer()`); `SetLayer(i, activity, sequence,
   autokill)` `0x10099020` (the eleven writes in the listing's order, the `flags & 2` snap zeroing
   both blends, `Flags` untouched; then the hook `OnOverlayLayerSet(i)`); `HasLayer` `0x10099540`
   (`FindLayerByOwner != -1`); `RemoveLayer` `0x10099660` (weight 0, then sequence 0; nothing else).
2. **`AddGesture(int32 Activity, bool bAutoKill)`** `0x100991b0` with `0x100990f0` inlined as
   retail's two calls: held → its index; `SelectWeightedSequenceForActivity(Activity)`; **`< 1`
   refuses** with −1; allocate; `SetLayer(i, -1, seq, bAutoKill)`; `Activity` written after.
3. **`AdvanceOverlayLayers(float Interval)`** — the layer half of `0x10098bb0` and
   `CAnimationLayer::StudioFrameAdvance 0x10098830`, from the listing (`vtmb_asm 10098830`): only
   layers with `Weight != 0`; cycle, the ends (below 0: looping drops the integer part, else 0,
   no flag; at or above 1: `SequenceFinished = 1` on both, looping drops the integer part, else
   1.0), then the envelope, **as read** (S3 items 1 and 6.1):
   - **the gate**: the envelope is computed only when `BlendIn < 0.95 || BlendOut < 0.95` — the
     compare is against the **float 0.95** at `0x1045001c` (`FCOMP float ptr`, twice,
     `0x100988d6` / `0x100988ed`). Otherwise the weight stays **1.0**.
   - inside, two `!= 0` tests: `BlendIn != 0 && Cycle < BlendIn` → `w = Cycle / BlendIn`;
     `BlendOut != 0 && Cycle > 1 − BlendOut` → `w = (1 − Cycle) / BlendOut` (the later write
     wins); then `w = 3w² − 2w³` (double 3.0 at `0x10450010`). A snapped layer (both blends 0)
     keeps weight 1.
   - then the clamp to `WeightMax`; then finished and autokill → `Weight = 0` and slot 112 `(i,
     Activity)` through the existing slot.
   Call it from `FElysiumNpcBase::StudioFrameAdvance` with **the interval that function
   returns**, on every return path. **The zero-interval rule** (S3 item 6.2):
   `CBaseAnimating::StudioFrameAdvance 0x1008f120` returns `0.0` when `dt <= 0.001` (double
   `0x1044f020`, `0x1008f1e9..0x1008f209`) and the owner still calls the layer body with that 0.
   The cycle does not move, **but the weight is recomputed**: a layer at cycle 0 with `BlendIn
   0.2` gets `0 / 0.2 = 0` and **frees itself** (occupancy is `Weight != 0`); a layer past cycle
   0 recomputes to its old weight. Its dispatch (item 4) still runs, so a 3031 at cycle 0.0
   still fires once. Reproduce that; do not skip the layer body on a zero interval and do not
   guard the fresh layer. ("A second advance in one think moves no layer" — an earlier text of
   this brief — was false for a fresh layer.)
4. **`DispatchOverlayLayerEvents(FElysiumEntity* Handler)`** — `0x10098cd0` for layers 0..3, in
   order, no weight test: replace A1's "no layer" seam with it. If A1 already wrote a per-layer
   body over its words struct (Grep `0x10098cd0` in `ElysiumAnimEvents.{h,cpp}`), fill the words
   from the layer and call it; otherwise write the body here, citing each line, and say so in your
   report. The `animevent` tap fires for a layer's event exactly as for the base's.
5. **The row words** on `FElysiumNpc` (`ElysiumNpcAnim.cpp`): `SequenceSnaps(int32)` (the clip's
   baked snap bit — `FElysiumActivityClip::bSnap` at resolve time, kept on `FSequenceRow`) and
   `SequenceCycleRateOf(int32)` (1 / the row's length; a row never played takes its length from the
   draw in item 6; none → 0 and one Warning per (model, clip)). `SequenceFlagsOf`'s seam
   (`ElysiumAnimatingOverlaySlotBodies.cpp`) answers from these two and `SequenceLoops`.
6. **The draw** (visual-only): `FElysiumNpc::OnOverlayLayerSet(int32)` plays the row's clip through
   `PlayAnimSegment` with `Channel = EElysiumAnimChannel::UpperBody`, `bLoop = false`,
   `bSnap = SequenceSnaps(seq)`, the activity's registration name, and stores the answered length
   on the row. The kernel's layer words never read anything back from the body but that length.
7. **Tests**: `Elysium.Arm.NpcKernelOverlay.AddGesture`, `.LayerAdvance`, `.LayerDispatch` as
   README §6, on fixture rows, each assertion naming its address. `.LayerAdvance` asserts the
   0.95 gate (`0x1045001c`: both blends at or above 0.95 → weight 1.0; a snapped layer keeps 1)
   and **the zero-interval rule instead of "moves no layer"**: an interval of 0 moves no cycle;
   a cycle-0 layer with `BlendIn 0.2` ends with weight 0 (freed) and its 3031 at cycle 0.0 still
   dispatches once; a layer past cycle 0 keeps its weight.

Wave check (O1 / O2 / O3, re-checked after S3): none of your files is in O2's list
(`ElysiumNpcBaseMaintain.cpp`, `ElysiumNpcBaseSenses10.{cpp,inl}`,
`ElysiumNpcBaseMoveAndShoot.cpp`, `ElysiumNpcMotor.cpp`, its tests) or O3's
(`ElysiumWeaponClasses.{h,cpp}`, `ElysiumWeaponTests.cpp`).

## Not yours

`RunTaskOverlay` and the move-and-shoot body (O2: it calls your `AddGesture` and `HasLayer`); the
weapon (O3); slots 265, 273, 275 and every other pusher (README §3); the base dispatcher and the
base clock's words (V4a); `ElysiumAnimatingOverlaySlots.cpp` and the verdict table (integrator).

## Rules

README § "Rules for every agent of V4o". Retail first: read the listing before the port; cite the
address at every line. Never build, launch or run. Only your files; a line another file needs goes
in your report, exact, with its place. A divergence is recorded, not adopted. Query budget: 10 s
warns, 60 s stops. Never read a file over ~200 KB whole. Grep / Read / Glob. Do not commit.
Report ≤300 words: what you ported (addresses), tests added, the cross-lane lines, what stayed
unrecovered.
