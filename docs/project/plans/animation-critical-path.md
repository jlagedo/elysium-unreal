# The animation critical path

**One programme, ordered, closed before anything else moves.** Every animation defect this project
has measured against retail is here, as a task an agent can execute without rediscovering the
traps that cost the session that found them. The acceptance is the same everywhere: an instrument
that is **red today** goes green **on numbers**, never on a screenshot or a live session.

**This document carries its own status and its own findings**, against the general rule for a plan
file, because the programme is ordered and a later task reads the earlier one's measurements to
know what it is standing on. A landed task keeps its entry; nothing here is deleted on landing.

- A task heading carries **DONE** once it has landed and its gate has been read. **An unmarked
  heading is open** — absence of a mark is the status, so no task needs an edit to stay true.
- A landed task gains an **As landed** block: the shape that shipped, the numbers its gate now
  reads, and every place the task text turned out to be wrong, each correction carrying the
  measurement that decided it. A task text is never rewritten to match what landed — the original
  stands and the block says how it differed, because the next task's author needs to know which
  assumptions did not survive contact.
- An open task may carry a **Findings** block for a measurement taken before it lands.
- The roadmap (`docs/project/roadmap.md`) still owns master sequencing and project priority, and
  the owner adds the row there.

---

## 0. Rules of engagement

These override defaults for every task below. They are the owner's calls, made in the session that
produced this plan, and they are not re-litigated per task.

1. **The bake-everything rule is demoted for this programme.** Root `CLAUDE.md` → "Poses are baked
   native" stays the default for everything static. Four retail rules are **runtime rules by
   construction** here — the post-multiply additive (T-B1), the overlay weight envelope (already
   runtime), the previous-sequence cross-fade chain (T-C7) and the aim-pitch slew (T-C6). Each is
   named where it lives, with the retail address beside it. No fifth is added without an owner
   call.
2. **The owner's observation is ground truth.** A test, log or harness that reads green against a
   reported defect is the first suspect. Reproduce at the seam the owner sees, then name the
   measurement that would falsify the code and run that. Do not hedge on whether the owner's build
   is current.
3. **A green test in this scope is a failure until it was red first.** Every task ships its
   instrument before its fix, and the instrument is shown red on the unfixed build.
4. **No live session as evidence.** No MCP-driven green room, no "looks right" over a screenshot.
   The headless harness (§3.4) and the automation tiers are the only acceptance surfaces.
5. **No cvar, feature flag, A/B toggle or state-enabling switch.** A task lands as a complete
   change; the previous behaviour stays recoverable through git.
6. **Docs follow the task that changes the fact.** Each task names the owning document it updates.
   Nothing else touches docs, except this file: a landing task marks its own heading **DONE** and
   writes its own **As landed** block here. The roadmap is the owner's.
7. **Verification is scoped to what changed**: source → `uv run elysium build` then the named tier;
   pipeline → the Python suite and a re-bake only where the task says so; prose → nothing.

---

## 1. Reading list — what each document owns

Read the sections named before touching the task that cites them. Every one was rewritten or
verified against the binary in the mapping sweep; where two once disagreed, the one cited here is
the one the disassembly sided with.

| document | owns | read for |
|---|---|---|
| `docs/vtmb/animation_and_movers.md` | the pose pipeline as recovered: A.2 bone record, A.3 sequences, A.4 RLE decode and the **additive combine**, A.4a `Flags & 0x2`, A.4c transitions and layer ramping, the gait ladder, the fan duration rule, the aim latch | T-B1, T-B2, T-C4, T-C6, T-C7 |
| `docs/vtmb/animation_rig_resolution.md` | global sequence numbering, include-model resolution, the bone remap, **the overlay contract** (all five former open questions resolved), the capture procedure | T-C3, T-C4, T-C5, §3.1 |
| `docs/vtmb/animation_events.md` | `mstudioevent_t`, **the 0.1 s look-ahead**, per-layer dispatch, the 58-code table | T-C1, T-C5 |
| `docs/vtmb/activity_enum.md` | the 4,460-entry `ACT_*` enum, the registry, `SelectWeightedSequence`, the translation chain | T-C2, T-C3 |
| `docs/vtmb/player-entity.md` | the frame position of the animation update, the dead weapon lookahead, combat stance, idle variation | T-C2, T-C6 |
| `docs/vtmb/wielded_weapons.md` | the two bone merges, the bone flags, the attachment census, `hands box` | context only — the wield defect is closed |
| `docs/vtmb/procedural_bones.md` | axis-interp (client-only, three call sites, the topology census) | context only |
| `docs/vtmb/mdl_v2531.md` | struct offsets, the dead-slot map, the modelgroup pose-param remap | T-B1 (delta channel decode), T-B3 |
| `docs/vtmb/facial_animation.md` | the server expression subsystem | out of scope; listed so no task strays into it |
| `docs/architecture/animation-architecture.md` | the Unreal integration: bake contract, graph generator, §2.5 / §4.1 engine gotchas | every T-B and T-C task |
| `docs/project/plans/animation.md` → LIFE10 | the overlay subsystem contract and **the gap table this plan expands** | all |
| `Source/ElysiumUE/CLAUDE.md` → Engine gotchas | Live Coding, `FAnimNode_LayeredBoneBlend` masks, `GetCurrentRefToLocalMatrices` | T-B1, T-C5 |

---

## 2. The traps

Each one was fallen into during the session that produced this plan. Each has the rule that
prevents it. An agent that recognises a situation below stops and applies the rule before
continuing.

**T1 — A harness that records a body that never moved.** `-ElysiumCompose` printed
`OK: median 0.404 cm` over 41 frames on a body that had strafed 93 cm into a wall and then stood
still, because the pickup notification's input scope gated every replayed command to zero.
*Rule:* every harness records position, velocity and the input gate per frame and **errors** on a
run whose farthest travel is under 25 cm. Read `farthest_travelled_cm` and `motion.gated` before
reading any pose number.

**T2 — Reading the wrong cohort's attribution.** `RigCompose` printed per-bone error for the
*layered* cohort only, where the pushed slot's 49-bone mask overwrites the arm outright, so the arm
could not show error there and the legs dominated by construction. The defect was in the
*control* cohort, which had no attribution. *Rule:* attribution is printed for every cohort, and a
cohort assertion names which cohort it is.

**T3 — All-pairs distance over one rigid subtree is isometry-blind.** Rotating a 49-bone subtree
47° and translating it 250 cm scored 5.7e-15 cm — exactly the defect class under investigation.
*Rule:* compose in **local** space, compare the **whole body**, and add a scalar the defect must
move (for the arm: right-hand excursion against `Bip01 Spine1` through a cycle, retail 1.89 cm).

**T4 — `"male" in "malkavian_female_armor_0"` is True.** A whole cohort was scored against the
wrong bank. *Rule:* test `female` first; use `body_gender()` from the scratch tooling or its
equivalent.

**T5 — The export's `activities` array is an intern table, not the enum.** `ACT_RUN_M37` is
**1065**, not 174. *Rule:* activity numbers come from `docs/vtmb/activity_enum.md`; a clips sidecar
gives names, never values.

**T6 — The answer was already in the docs.** `animation_and_movers.md` → "The additive combine
accumulates, and `0x10` picks which side the delta lands on" recorded the post-multiply rule with
function addresses, and the LIFE10 plan deferred it as out of scope. *Rule:* before designing a
bake or a node, grep the owning document for every flag bit the clip carries. A deferred rule is a
defect with a date on it.

**T7 — Vtable slot collisions.** `vtmb_slot` merges hierarchies: slot 289/290 is a weapon
follow-up on `CBaseCombatWeapon` and an IK walker on `CAI_BaseNPC`; slot 333 is an eye maintainer
on characters and an activity request on weapons; 267/268 differ between `CBaseAnimating` and
`CBaseAnimatingOverlay`. *Rule:* filter a slot census by class before reading a body.

**T8 — Off-by-one dwords.** `+0x5dc` is the *pre*-translate, `+0x5e0` the class translate;
slot 410 is the stance *setter*, 411 the predicate. *Rule:* cite the address of the body, not the
slot, and confirm with `vtmb_callers`.

**T9 — An invented rationale.** Two source comments and a test header claimed `w_f_m37` roots its
geometry at a `hands box` bone off `Bip01`. The bone carries **zero** skin influence; every vertex
hangs from `Box02` under `Bip01 R Hand`. *Rule:* a claim about where geometry roots is a skin-weight
count (`mdl_skel.decode_skinned(d, v)` → `joints`/`weights`), never a bone-tree read.

**T10 — Ghidra prints a double's low half as a float.** `_DAT_1044c398` reads `2.0` as a float
and is `(double)0.01f`; `_DAT_101e34e0` reads `0.0` and is the double `1.0`. *Rule:* read the
instruction width (`FCOMP m64fp` vs `m32fp`) and the raw bytes at the VA before trusting a constant.
`vampire.dll .rdata` is `0x10445000..0x10532d12`; `.data` starts `0x10533000` (file offset equals
RVA there).

**T11 — A caller census that resolves only the thunk.** `UpdateCharacter` read as NPC-only until
the disassembly showed `CBasePlayer::PostThink` dispatching slot 312 at `0x1016c316`. *Rule:* a
virtual has dispatch sites, not callers; enumerate by slot and confirm in the caller's listing.

**T12 — Live Coding patches exist only in the editor that compiled them.** A headless run, a
commandlet or a fresh process loads the on-disk module. New `UPROPERTY`s are the sharp edge.
*Rule:* anything proven live is proven again after `uv run elysium build`, and a graph re-export
follows a real build.

**T13 — There is no feathering anywhere in the autolayer path.** The composition weight is the
constant `1.0f`; the per-bone mask is binary `{0, 1}` over 736,208 records; the mask comes from
blend cell `[0][0]` only. *Rule:* a blend-profile weight that is not exactly 0 or 1 is a bake defect.

**T14 — Autolayer order is data.** `[delta, overlay]` and `[overlay, delta]` compose differently
because an overlay is a hard replace on its mask. Two shipped hosts invert the order. *Rule:*
compose in declaration order, never "overlay first".

**T15 — `FReferenceSkeletonModifier::FindBoneIndex` inside an open modifier returns
`INDEX_NONE`**, making every non-root a second root. *Rule:* keep a local name→index map while
building a synthetic skeleton.

**T16 — Two docs disagreed and the older one was trusted.** A.4c said "layers do not ramp";
the overlay contract said they do. The binary sided with the contract. *Rule:* when two documents
disagree, the one citing an instruction address wins, and the other is corrected in the same task.

**T17 — The decompiler loses x87 multiply order.** The pre/post additive bodies differ only in
operand order at one `CALL`. *Rule:* read `vtmb_asm` for every quaternion multiply, blend or scale.

**T18 — The capture's `aim_yaw` is not the pose parameter.** It is `CBasePlayer + 0x2070`, the yaw
of `pl.v_angle`, ±180. The pose parameter is a literal `0.0f`. *Rule:* a capture field is named by
its offset; check the recipe (`research/tooling/capture/frida/recipes/*.json`) before binding it.

**T19 — An unmapped include pose-parameter slot is `0.0` normalised = `−180°`**, not 0°. *Rule:*
resolve pose parameters through the modelgroup remap (`mdl_v2531.md`), never by local index.

**T20 — "Unread" constants.** `_DAT_1045001c` was recorded as unread; it is the `0.95f` envelope
guard with one referrer. *Rule:* a global with any referrer is read; list the referrer.

**T21 — String-table bleed.** Seven scenery models read `numautolayers == 764`. *Rule:* bound every
count and every array against the image; the decoder's `_MAX_AUTOLAYERS = 16` is a repo guard, not
a retail rule.

**T22 — The three additive conventions are not interchangeable.** `ABPT_AnimScaled` (base at
matching phase) fixed the arm's mean and destroyed the grip (hand-to-hand p2p 17.12 cm vs 0.99).
`ABPT_AnimFrame 0` reproduces our defect to three digits. Only the pose the delta actually lands
on is right, and for a masked delta over a masked overlay that pose is the **overlay cell**, not the
host. *Rule:* T-B1's node makes this moot; do not reach for `RefPoseType` again.

---

## 3. Ground truth and instruments

### 3.1 The retail capture

`E:\elysium-work\research\frida\20260825T053323.219310Z-attach-life_rig_pose\` — the **only**
session carrying both halves:

- `pose_oracle.json` — 2,722 frames, each `{curtime, stem, is_player, bone_count, bones{name:
  matrix3x4 row-major, translation at [3],[7],[11]}, player_state{sequence, cycle, velocity,
  aim_yaw, active_weapon, activity}}`. Bodies: `malkavian_male_armor_0` (1,468),
  `malkavian_female_armor_0` (1,254). **Source units; multiply by 2.54 for cm.**
- `layer_oracle.json` — 1,718 frames, `{curtime, stem, sequence, cycle, aim_yaw,
  channels[{label, owner_stem, weight, additive}]}`.
- Join key: `(round(curtime, 5), stem)`. Later sessions have no `layer_oracle.json`.

Retail's own numbers for the M37 aggressive carry, female, moving (the target for T-B1):

| scalar | retail | ours before T-B1 |
|---|---|---|
| hand-to-hand, mean / p2p | 40.14 / **0.99** cm | 40.02 / 0.71 |
| R Hand ← Spine1, mean / sd / p2p | 33.49 / 0.49 / **1.89** cm | **29.48** / 2.14 / **6.76** |

### 3.2 The exports

`$ELYSIUM_EXPORT_ROOT = E:\elysium-work\exports`. `npc/banks/*.eskm` (container `ESKM` v8: header
`<4sIII>`, directory `<4sQQ>`, sections `SKEL ATCH DYNM BDYN MATL MESH MORF MASK ANIM`; a clip is
`name, base, frameCount, fps, flags, maskIndex, trackCount`, then per track `<I2B>` and
`12*frames` translation + `16*frames` rotation). `npc/blends/*.json` (`pose_parameters`, `grids`
with per-cell `motion`, `autolayers`, `events`). `npc/clips/*.json` (`seq` label→raw index; the
`activities` array is an **intern table**, T5). Masks in the shared banks: `[1, 24, 49, 45]` bones,
index 2 the 49-bone upper body from `Bip01 Spine1`.

### 3.3 The automation tests

`uv run elysium test <name>` takes a full test name; `uv run elysium build` first, editor closed.

| test | tier | today | what it proves |
|---|---|---|---|
| `Elysium.Content.RigCompose` | Content | **RED** — control 4.45 cm, layered 3.38 | composed pose vs the capture, whole body, local-space composition, three cohorts |
| `Elysium.Content.RigPose` | Content | green | one clip on an unlayered frame |
| `Elysium.Content.RigLayers` | Content | green | channel reachability, four slots suffice |
| `Elysium.Substrate.OverlayStack` | Substrate | green | the four stack rules |
| `Elysium.Substrate.WieldAttach`, `Elysium.Content.WieldBinding` | both | green | the wield resolver; closed |
| `Elysium.Substrate.AnimationIntent` (`ElysiumAnimationActionTests.cpp:2579–2610`) | Substrate | green | the envelope at 0, 0.1 and 1.0 |
| `Elysium.Substrate.AnimEventWindow`, `.AnimEventDispatch`, `.AnimEventVisibility` (`ElysiumAnimEventTests.cpp`) | Substrate | green | the event window as a catch-up — **asserting the wrong upper bound** (T-C1) |

### 3.4 The headless compose harness

`uv run elysium debug compose` → `-ElysiumCompose` (`Source/ElysiumUE/Private/Debug/
ElysiumComposeRun.{h,cpp}`) → `$ELYSIUM_EXPORT_ROOT/_compose/<map>-<weapon>.json`, then
`pipeline/src/elysium_pipeline/validation/compose_diff.py`. It stands the movement gym up, seats
the body on the flat lane facing across it, waits for gameplay input to be ungated for 30
consecutive frames, grants and wields the weapon, replays 60 relaxed strafe + 60 fire + 150
aggressive carry, then the same to the left. Per frame: component-space bones, the selection record
(`label, layers, move_yaw, aim_yaw, aim_pitch`), the four slot rows with `mask` and
`masked_bones`, and `motion{x, y, z, vx, vy, vz, speed, travelled, facing_yaw, gated}`. It errors
on `farthest_travelled_cm < 25`. Default weapon `item_w_ithaca_m_37`; `-ComposeWeapon=` and
`-ComposeHz=` override.

### 3.5 Offline tooling

`E:\elysium-work\scratch\life10\` (gitignored, outside the checkout): `compose_probe.py` (the
`.eskm` reader — `_rd`, `skel`, `masks`, `clips(want)`, `fk`, `qmul`, `qrot`), `sway.py` /
`sway2.py` (retail scalars per state), `sway_ours.py` (the same off a compose run), `armsway.py`
(compose a fan cell + layers offline). Every number in this plan is reproducible from them.

### 3.6 Reverse engineering

The corpus is the `mcp__vtmb-corpus__*` MCP server; load schemas with ToolSearch
(`select:mcp__vtmb-corpus__vtmb_grep,…_func,…_code,…_asm,…_callers,…_callees,…_fields,
…_vtable,…_slot,…_readers,…_string,…_globals`). `vampire.dll` is the server, `client.dll` the
client; both at image base `0x10000000`; shipped binaries under
`E:\dev_game\Vampire The Masquerade - Bloodlines\Vampire\{dlls,cl_dlls}\`. Apply T7, T8, T10,
T11, T17 to every reading.

---

## 4. The tasks, in order

Dependencies run downward. A task's **Gate** is the instrument that must be red before and green
after. **Docs** names the owning document updated when it lands.

### Phase A — instruments

#### T-A1 `RigCompose` attributes every cohort and fails on a missing asset — **DONE**

- **Where:** `Source/ElysiumUE/Private/Tests/ElysiumRigComposeTests.cpp` (`ComposeClosure` at
  ~458, the asset load at ~505–522, the cohort report at ~1003–1090).
- **Change:** per-bone attribution for `AllError`, `BaseOnly` and `CrossFaded` alike, each block
  labelled with its cohort name. A layer asset that does not exist is an **`AddError`** naming
  owner, label and host, not a `continue` (today `A_katana_bobble_layer_katana_aggressive_run` is
  skipped silently). Add the arm scalar from §3.1 (right hand ← `Spine1` peak-to-peak over the
  moving frames of each cohort) as a printed line and, for the control cohort, an assertion at
  **≤ 2.5 cm** (retail 1.89; the threshold leaves room for the 9-bit pose-parameter quantisation).
- **Gate:** red on today's build with the arm scalar at ~6.8 cm.
- **Traps:** T2, T3, T4.
- **Docs:** none — the test is the record.

**As landed.** The three cohorts are an `FCohort` each, carrying their own error distribution,
per-bone ranking and arm scalar; every printed block and every assertion names its cohort.
`FDistribution` gained `Mean`, `StdDev` and `PeakToPeak`. `Elysium.Content.RigCompose` is red on
the gate: control cohort median **4.449 cm**, arm scalar **3.46 cm** on the female `seq 239` state
and **6.98 cm** on `seq 412`. The control ranking, which did not exist before, is legs-first —
`Bip01 R Calf 6.77`, `R Toe0 5.72`, `L Calf 5.46` — which is T-C7's residual showing up where T2
said it would.

Three things the task text got wrong, each corrected in the code and each carrying a number:

- **The scalar is grouped per `(body, base sequence)`, never pooled over a cohort.** Pooled, the
  peak-to-peak measures the difference *between* states — aiming, walking, reloading each hold the
  hand at a different distance — and the cycle disappears into it: on **the capture itself** the
  control cohort pools to **21.33 cm**, and the capture is retail. Grouped, the capture side
  reproduces §3.1 to the digit (female `seq 239`, n=27: mean **33.49**, sd **0.49**, p2p **1.89**).
  That is `sway2.py`'s grouping, which is where §3.1's row actually came from; `sway.py`, which
  buckets by `(stem, moving)` and pools every M37 frame, gives 30.60 / 4.38 / 16.09 and is not the
  source. A pooled assertion is a T3-class metric — a number that cannot move with the defect.
- **The bound is `max(2.5 cm, the capture's own p2p for that same state)`.** Retail's *male* M37
  carry swings **4.19 cm** (`seq 211`) and **4.48 cm** (`seq 383`), so a flat 2.5 cm reads red
  where retail reads red, which is a threshold measuring itself rather than the code. The floor
  masks nothing: both male states still fail, at 8.31 and 9.89. On every state where retail holds
  inside 2.5 cm this is exactly the task's assertion.
- **`A_katana_bobble_layer@katana_aggressive_run` is not skipped silently — it is *substituted*.**
  The loader falls back to the raw `A_katana_bobble_layer`, which does exist on the mount, so the
  frames it scores are scored against a clip retail did not draw. A genuinely absent layer is now
  an `AddError`; this case is a separate named warning. **It is the only substitution in the whole
  corpus** — every other declared autolayer has its `@host` derived form baked, including the
  sibling `A_bushhook_bobble_layer_bushhook_aggressive_{run,walk}`. That asymmetry is a bake gap,
  and the lead belongs to T-B1's re-bake.

Our arm p2p reads **lower** than the ~6.8 cm the Gate line predicts because the two numbers come
from different instruments: §3.1's "ours" is `sway_ours.py` over a compose-harness run, while this
test searches the layer phase and the aim cell for the best fit and so reports a **lower bound** on
the error. Both are red; T-A2 is the one that scores the harness run directly.

#### T-A2 `compose_diff.py` scores the whole body in local space — **DONE**

- **Where:** `pipeline/src/elysium_pipeline/validation/compose_diff.py`.
- **Change:** replace the rigid-subtree all-pairs metric with the whole-body, local-space
  composition `RigCompose` uses, plus the same arm scalar. Join on `(round(curtime,5), stem)`;
  gate the moving cohort on `motion.speed > 20` and refuse a run with `farthest_travelled_cm < 25`.
- **Gate:** red on the current `_compose` run (the arm scalar is 6.76 cm there).
- **Traps:** T1, T3.
- **Docs:** none.

**As landed.** The comparator scores two cohorts — `control` (no slot standing) and `layered`
(exactly one at full weight) — each carrying its own error distribution, per-bone ranking and arm
scalar, and each asserted. On `sp_tutorial_1-item_w_ithaca_m_37.json` it is **red on every one of
them**: control **n=338, median 3.990 cm** (p90 4.676, max 5.451), layered **n=81, median 3.995**
(p90 5.482, max 7.481), `m37_attack_layer` 3.995, and the arm scalar **6.757 cm peak-to-peak
against a bound of 2.500** — the Gate line's 6.76 to the digit. Both cohorts' per-bone rankings are
feet-and-calves first (`L Toe0` 5.42, `R Toe0` 5.20, `L Foot` 4.95, `L Calf` 4.91 on the control),
which is the same shape T-A1's ranking has and the same residual T-C7 owns.

**It reproduces §3.1's table on both sides.** The control state (female `m37_aggressive_run`,
moving) reads ours **29.48 / 2.14 / 6.76** and the capture **33.49 / 0.49 / 1.89 over n=27** —
which is the §3.1 row exactly, from a completely different instrument than the `sway_ours.py` /
`sway2.py` pair that produced it. Two independent readings of both halves now agree.

Five things the task text got wrong or left unsaid, each carrying the measurement that decided it:

- **There is no composition to do, so "in local space" does not name a step here.** The harness's
  frame is the pose the running graph already composed, published in Unreal-native component
  space; `RigCompose` composes in local space because it *rebuilds* the frame from clips, and this
  scorer rebuilds nothing. What was actually ported is the part that mattered — the **whole-body
  extent** of the pairwise set (T3), the per-cohort attribution (T2) and the per-state arm scalar.
  Naming a space here would have been a claim about a stage the file does not have.
- **"The whole body" is 59 bones of 88, and the other 29 must go.** The set is the bones the
  committed clip states a track for, read from the playing bank's own `ANIM` track headers
  (`eskm.clip_track_bones`) rather than from a name list. What that excludes on the female
  Malkavian is the axis-interpolated twist chain (`Bicep`, `Ulna`, `Elbow`, `Knee`, `Hip`,
  `Quadricep`, `Femoris`, `Shin`, `Ankle`, `Wrist`, `Shoulder`, both sides) and the `Bone01..09`
  hair chain — things retail drives by a rig rule or secondary motion that this run does not
  perform, so leaving them in scores a simulation instead of a composition.
- **A captured frame taken mid cross-fade is not a candidate, and excluding it is what makes the
  bound honest.** 377 of the 866 joined female frames carry a plain channel below full weight,
  which is a previous base still ramping out (A.4c). Left in the candidate pool the min-search
  preferred them, which *lowered* our reported error (control median 3.432 rather than 3.990) and
  *raised* the capture's own arm excursion to 2.32 cm over n=44. Removed, the capture returns
  1.89 over n=27 — §3.1's number — and our own figures do not move at all, because the defect is
  ours and not the corpus's. Detected the way `RigCompose` detects it: non-additive, weight < 0.999.
- **The bound is `max(2.5 cm, the capture's own p2p for that same state)`**, the same correction
  T-A1 landed and for the same reason. On this state retail holds inside 2.5, so the floor is what
  fires and this is exactly the task's assertion.
- **The retail frame is matched on `(stem, base clip, overlay set, moving)`, and matching on the
  full channel set is impossible today.** On all 81 layered frames the capture also accumulates
  **`m37_attack_delta`**, which our published record does not name — an exact channel-set match
  would drop the entire layered cohort. It is reported as a named channel gap per state and never
  gated on, because our record enumerates what the driver published rather than everything the
  graph composed; whether the delta reaches the frame is T-B1's to settle.

Two smaller shapes worth knowing before the next task reads this: the moving gate is
`motion.speed > 20` and it is part of the **state key** rather than a cohort filter, so a
standing frame is still scored but never carries the arm scalar; and the exit code now separates
**2 — the run cannot support a verdict** (travelled < 25 cm, per T1, or no stem) from **1 — the
composed pose is wrong**, so a chained `debug compose` cannot report a harness fault as a
composition defect. 60 relaxed-gait frames and 1 `m37_ready` frame are unscored and named: the
capture never stood those states.

#### T-A3 A fan-duration assertion — **DONE**

- **Where:** new `Elysium.Content.FanDuration` beside `ElysiumRigComposeTests.cpp`.
- **Change:** for every baked `UBlendSpace` with cells that disagree on length (99 of 126 gait
  grids), sample the blend at nine `move_yaw` positions including three between spokes and assert
  the blended length equals `Σ wᵢ · (numframesᵢ − 1) / fpsᵢ` from the `blends/*.json` cell
  `motion.cycle_seconds`, within 1e-3 s. Print the harmonic-mean value beside it so the divergence
  is visible.
- **Gate:** red today — at `move_yaw = −120°` on the female `walk` fan the engine answers 1.000 s,
  retail 1.111 s.
- **Docs:** none.

**As landed.** `Elysium.Content.FanDuration`
(`Source/ElysiumUE/Private/Tests/ElysiumFanDurationTests.cpp`) sweeps every owner in
`npc_index.json` that declares a blends sidecar and scores every gait fan the bake wrote. **It is
red on the Gate to the digit**: on the female `walk` fan and its whole carry family the asset
answers **1.0000 s at `move_yaw = −120°` against retail's weighted mean of 1.1111 s**, and the
printed row carries the harmonic mean beside it at 1.0000 — so the divergence is not merely
present, it is identified as *exactly* the harmonic branch. Corpus-wide: **176 of 207 scored fans
diverge, over 395 of 1,863 sampled headings**, worst 0.111 s.

The measurement is taken through `UBlendSpace::UpdateBlendSamples` for the weights and
`GetAnimationLengthFromSampleData` for the answer — the pair `FAnimNode_BlendSpacePlayer` reaches
through `TickAssetPlayer`, so the number asserted is the one the graph reads. It confirms the
plan's reading of `BlendSpace.cpp:2037` from the other side: the legacy branch is `Σ wᵢ · lenᵢ` and
the default is `1 / Σ (wᵢ / lenᵢ)`, and every shipped asset is on the second.

Four things the task text got wrong or left unsaid, each carrying the number that decided it:

- **The census is 207 of 225, not 99 of 126, and every one of them is on the mount.** A gait fan is
  a grid whose axis-0 pose parameter is `move_yaw` and whose every cell carries
  `motion.cycle_seconds` — 225 of them across **84 owners**, all 9×1 over −180..180 without
  exception, **207 with cells of differing length and 18 without**, and **0 not on the mount**. The
  `@host` derived form does not arise here: no gait fan is declared as an autolayer *target*, so
  every one has exactly one asset, unlike the layer grids T-A1 found. The 18 uniform fans are
  counted and skipped rather than asserted — both means return the shared length there, so an
  assertion on one would read as coverage it is not.
- **`Σ wᵢ (numframesᵢ − 1)/fpsᵢ` is not recomputed from frame counts; it is the sidecar's own
  column, and the frame count is a separate assertion.** `blends/*.json` states
  `motion.cycle_seconds` per cell already reduced, and the expectation is built from that and the
  engine's own blend weights — never from the baked sequences, which would let a bake that wrote
  every clip at the wrong length agree with itself. The two are cross-checked per sample as a
  **separately named failure**: `(frames − 1)/fps` in the `.eskm` matches `cycle_seconds` on all
  918 cells of the female `move_and_ranged` bank offline, and the test reports **0 baked cells
  disagreeing** across the sweep. So a length defect cannot be misread as a blend defect, in either
  direction.
- **Nine fixed positions cannot discriminate on every fan, so the third interior one is placed on
  the fan's own widest span.** Six spokes and three between them, in cell coordinates rather than
  literal degrees; the first interior sample lands at exactly −120° on a 9×1 −180..180 fan, which
  is the Gate. The third is aimed at the adjacent pair whose lengths differ most, because the two
  means agree *exactly* between two cells of equal length and a sample landing there passes for
  either rule.
- **31 of the 207 still cannot be discriminated on, and that is the tolerance rather than the
  sampling.** They are the `*_run` weapon carries on both shared banks plus the three
  `pcidles_allsequences` `run` fans, whose cells are `[0.6, 0.6, 0.567, 0.6, 0.6, 0.567, 0.567,
  0.6, 0.6]`: the largest possible `|weighted − harmonic|` **anywhere** on such a fan is
  **4.8e-4 s**, below the 1e-3 s bound, so no position could fail whatever the asset declares. The
  test names all 31 in its report rather than folding them into the pass, because the count alone
  would read as coverage. **176 discriminating fans, and all 176 diverge** — the instrument fails
  on every fan it can see.

One trap for T-B2's author. `GetAnimationLengthFromSampleData` reads `SamplePlayRate` off the
sample-data list, not `RateScale` off the asset, and `FBlendSampleData`'s default for it is
**`0.0f`** — on the harmonic branch that makes `SampleNormalizedSpeed` zero and the whole function
return **0 s**. It is seeded from `FBlendSample::RateScale` inside `GetSamplesFromBlendInput`, so
the length is only meaningful when read off a list that call produced. A hand-built list scores
every fan at zero and reads as a catastrophic failure rather than a wiring one.

#### T-A4 The compose harness runs both bodies — **DONE**

- **Where:** `ElysiumComposeRun.cpp` (`BuildGym` path, `-ComposeBody=`), `cli.py` `debug compose`.
- **Change:** a `-ComposeBody=<stem>` override defaulting to the female Malkavian, and `debug
  compose --body` in the verb; the summary prints the arm scalar per cohort.
- **Gate:** both runs red on T-A2's scorer.
- **Docs:** none.

**As landed.** `-ComposeBody=<stem>` stands the body through the shipping `BuildPlayerVisual`,
defaulting to `malkavian_female_armor_0`; `debug compose` runs **both** bodies by default, one
launch each, and `--body <stem>` (repeatable, comma-splittable) runs a subset. Both reports go to
one `compose_diff` call — `--run` is now repeatable — whose closing summary prints each run's
per-cohort median and arm scalar beside the other's. **Both runs are red on the Gate:**

| | control median | control arm p2p | bound | layered median | layered arm p2p |
|---|---|---|---|---|---|
| `malkavian_female_armor_0` | **3.990** cm (n=338) | **6.757** cm | 2.500 | **3.995** (n=81) | 13.086 |
| `malkavian_male_armor_0` | **1.887** cm (n=370) | 1.404 cm | 4.191 | **3.660** (n=49) | 4.720 |

The female run reproduces T-A2's landed figures **to the digit** — 480 frames, 1782.03 cm
travelled, control 3.990 / p90 4.676 / max 5.451, layered 3.995, arm 6.757 — which is the check
that the explicit body build changed nothing on the path that was already standing that body.

Four things the task text got wrong or left unsaid, each carrying the measurement that decided it:

- **The male's control-cohort arm scalar does not fail, and that is the finding, not a pass.** It
  reads **1.404 cm against a bound of 4.191** (the capture's own male excursion for the same state,
  `m37_aggressive_run` — which is T-A1's `seq 211` to the digit), while the female reads 6.757
  against 2.500 on the same weapon, the same map and the same stream. So the arm defect T-B1 owns
  is **not visible on the male body through this instrument at all**, and a programme that had run
  only the male would have read the whole arm scalar green. The whole-body medians invert the
  ranking — male 1.887, female 3.990 — so neither body is the strictly worse one and neither can
  stand for the other.
- **The two instruments disagree about the male arm by 6×, and the disagreement is a lead for
  T-B1.** `RigCompose` reads the male's `seq 211` carry at **8.31 cm** (T-A1's As landed); this
  harness reads the same body, the same state and the same retail bound at **1.404**. They measure
  different objects — `RigCompose` rebuilds the frame from baked clips and searches the layer phase
  and aim cell for the best fit, while this records what the running graph published — so the gap
  says the male's error lives in something the graph does that the rebuild does not, or the reverse.
  It is named here and settled by neither.
- **The report is named per body, and a run's `body` is written beside its `stem`.** Two bodies
  through one map and one weapon are two runs, so `<map>-<weapon>.json` became
  `<map>-<weapon>-<body>.json`; T-A2's landed record cites the old name, and
  `sp_tutorial_1-item_w_ithaca_m_37.json` is now an orphan no command writes. The report carries
  both the body **asked for** and the stem the driver **published**, and the comparator refuses
  (exit 2) when they disagree — an override that silently did not take records a full skeleton
  under a gait selection belonging to the wrong body, which is T1's failure one field further out
  and equally invisible in the pose data. Both runs came back with the two fields equal.
- **The body is built before the weapon is granted, not in the gym block.** `BuildPlayerVisual`
  tears the visual down and rebuilds it, so a body stood after the wield throws the wield
  attachment away; and `Begin` is retried while the world settles, so the build is guarded and a
  failure to build is **terminal** rather than retried — every further attempt would stand the map's
  own body and record it under the requested name. The entity's `Visual` and `Model` are synced the
  way the green room's drive body syncs them, because `ModelStem()` reads the `model` field and the
  weapon's attack activity — the thing this run exists to compose — resolves through it.

Two smaller shapes for the next reader. The male half of the capture is the more fragmented one:
852 frames of `malkavian_male_armor_0` join across both oracles and **616 do not**, against the
female's 866 joined and 388 unjoined, and **433 of the male's join only to be set aside as mid
cross-fade** where 377 of the female's are — leaving 419 male candidates against 489 female. That is
why the male's layered cohort is n=49 where the female's is n=81, and a per-layer bound reads on
fewer frames there. And the exit code over several runs is the worst of them with **1 outranking
2**, so a body whose capture cannot support a verdict never masks a body that failed.

### Phase B — the bake

#### T-B1 The post-multiply additive

The live M37 defect. Retail: `q = normalize(q ⊗ scale(D, s))`, `pos += D.pos · s`
(`vampire.dll 0x100c12b0` / `client.dll 0x10088d60`, selected by `flags@8 & 0x10`, set on all 118
shipped `_delta` sequences). Unreal's `AAT_LocalSpaceBase` is `D ⊗ q`. No `RefPoseType` setting
reaches the right answer (T22).

- **Bake — `Source/ElysiumUE/Private/Editor/ElysiumSkeletalBuild.cpp:1440–1480`.** For a clip
  with `flags & 0x4`, stop stamping `AdditiveAnimType`/`RefPoseSeq`/`RefFrameIndex`. Emit the
  clip as an ordinary sequence whose tracks are the **raw delta as decoded** (rotation
  `sample × rotscale`, translation `bind + sample × posscale`, exactly what retail's `SlerpBones`
  adds — `animation_and_movers.md` A.4), with every **untracked bone at identity rotation and
  zero translation**, not bind. Tag the asset (a `UAssetUserData` or a name suffix the graph
  generator recognises) so the graph binds it to the new node. Keep the `@host` derived-overlay
  emission for masked overlays untouched.
- **Pipeline — `pipeline/src/elysium_pipeline/exporters/UE_mdl_skeletal.py` ~1410.** The fold
  (`fold_deltas`) stays for the case it already handles; it is not extended to fanned hosts. The
  node makes the fold optional, not wrong.
- **Runtime — new `FAnimNode_ElysiumPostAdditive`** in
  `Source/ElysiumUE/Private/Visual/ElysiumAnimNodes.{h,cpp}` (beside `FAnimNode_ElysiumAxisInterp`),
  ~40 lines. Inputs: the base pose, the delta pose (evaluated from the sequence), the blend profile
  (the mask, binary), the weight `s`. Per bone with mask bit set and `s > 0`:
  `Out.Rot = (Base.Rot * FQuat::Slerp(FQuat::Identity, Delta.Rot, s)).GetNormalized()`;
  `Out.Pos = Base.Pos + Delta.Pos * s`. Unreal's quaternion `*` is Hamilton order
  (`UnrealMathSSE.h` → "C = A * B applies B then A"), so the expression above **is** retail's
  post-multiply; do not reverse it. `s` is `1.0` for an autolayer, the envelope weight for an
  overlay slot.
- **Graph — `Source/ElysiumUE/Private/Editor/ElysiumAnimGraphLibrary.cpp:760–970` and
  `pipeline/unreal/graphs/ABP_ElysiumBiped.t3d` via `pipeline/unreal/make_player_anim_bp.py`.**
  Replace every `ApplyAdditive` the generator emits for a `_delta` with the new node, in the
  base closure and in each of the four slot branches. Re-run the bootstrap after a real build
  (T12) and re-export the T3D.
- **Re-bake:** `uv run elysium export characters` (whole cast). One re-bake covers T-B2.
- **Gate:** T-A1's control-cohort arm scalar ≤ 2.5 cm; `RigCompose` layered `m37_attack_layer`
  median moves from 5.18 cm to under 1.5; T-A2 on a fresh compose run agrees. Offline expectation
  from `armsway.py`: hand-to-hand p2p 0.28 cm, R Hand ← Spine1 p2p 0.93.
- **Traps:** T6, T13, T14, T17, T22, T12.
- **Docs:** `docs/architecture/animation-architecture.md` — the additive contract section states
  the node, the raw-delta emission and the retail address; the "bake everything" rule's exemption
  list gains this entry beside axis interpolation. `docs/project/rebuild-strategy.md`'s register
  of deliberate reproductions gains the row.

#### T-B2 Fan duration and ground speed

Retail blends a fan's **durations** (`Studio_Duration`, `Σ wᵢ (nᵢ−1)/fpsᵢ`) and its **movement
vectors**; `animation_and_movers.md` → "A fan's cycle is the weighted mean of its cells'
DURATIONS".

- **Bake — `ElysiumSkeletalBuild.cpp:1894`** where the `UBlendSpace` is constructed: set
  `bUseLegacySamplePointAnimationLengthCalculations = true`. UE 5.8's legacy branch
  (`BlendSpace.cpp:2037`) is the weighted mean of sample lengths — retail's formula exactly; the
  default branch is the harmonic mean. No runtime rule.
- **Runtime — the mover's ground speed.** Where `FElysiumGaitSpeeds` / `GaitFrom(Speeds)`
  (`ElysiumAnimationIntent.h:1444`) derives a blended speed, blend the per-cell **displacement
  vectors** from `blends/*.json` `motion` and divide the length by the blended duration, instead
  of lerping per-cell speeds. Retail: 5–8 % below the speed lerp between spokes.
- **Gate:** T-A3 green; a movement baseline under `_move/baseline` shifts and is re-committed
  with the number stated.
- **Docs:** `docs/architecture/movement-architecture.md` gait-speed section cites the rule.

#### T-B3 The mask is binary

- **Where:** `ElysiumSkeletalBuild.cpp:1096–1106, 1171–1182` (`SetBoneBlendScale`).
- **Change:** a Content assertion that every blend-profile entry the bake writes is exactly `0`
  or `1` and that a masked clip's owned set equals the bank's `MASK` row (49/24/45/1 bones), with
  `bush hook` accounted for on the female skeleton.
- **Gate:** expected green; it exists so T-B1 cannot be undone silently.
- **Docs:** none.

### Phase C — runtime rules

#### T-C1 Event look-ahead

Retail: `flEnd = m_flCycle + 0.1 · cycleRate · playbackRate`, the cursor stores `flEnd`, and the
wrap sweeps `[0, flEnd − 1)` when looping (`animation_events.md` → "The server dispatcher").

- **Where:** `Source/ElysiumUE/Private/Substrate/ElysiumAnimEvents.cpp:17–105`.
  `FElysiumClipPhase` (`ElysiumAnimationIntent.h:1395`) already carries `Length` (:1418) and
  `PlayRate` (:1422), so the look-ahead needs no new field.
- **Change:** `Hi = Cycle + 0.1 · Phase.PlayRate / Phase.Length`; store `Hi` as `LastCycle`;
  non-looping clamps `Hi` to `1.0` and raises finished; looping adds the `[0, Hi − 1)` sweep. Keep
  the closed-bottom, open-top window. The `≥ 5000` band: retail's client has **no** look-ahead and
  a wrap defect that drops events once per lap; reproduce the window, not the defect — that is a
  divergence, recorded as one with the owner's name, in `animation_events.md`.
- **Gate:** `Elysium.Substrate.AnimEventWindow` (extend; it currently asserts the catch-up shape
  and must be made red first): an event at cycle 0.35 on a 1 s clip fires when the pose reaches
  0.25 at rate 1.0; the wrap case fires once.
- **Docs:** `docs/vtmb/animation_events.md` → "Against this repo's runtime" rewritten to match.

#### T-C2 Combat-stance stamps

Retail stamps `m_flLastCombatAnimTime` on **four** paths (`player-entity.md` → "Combat stance is a
player-only concept"): `PLAYER_ATTACK1` with a weapon, taking damage from an entity with
`+0x9c != 0`, `CWeaponMelee::RequestActivity`, and `IsInCombatStance` itself off a live melee
opponent. Ours models the first.

- **Where:** `Source/ElysiumUE/Public/ElysiumPlayer.h:1157–1171` (`StampCombatAnim`,
  `IsInCombatStance`), `ElysiumAnimationDriver.cpp:438`, the melee request arm in
  `Substrate/ElysiumWeaponClasses.cpp`, the damage arm in `ElysiumPlayer`.
- **RE sub-task first:** `+0x9c` on the attacker/inflictor is unrecovered. `vtmb_fields
  CBaseEntity` / `vtmb_readers` at `0x9c`; until named, treat it as "the attacker is a combat
  character" and mark it `[inferred]`.
- **Change:** add the three paths; make `IsInCombatStance` take the clock and refresh it when the
  melee opponent is live and in its attack window.
- **Gate:** new Substrate assertions: a body taking damage 4 s after its last swing is still
  aggressive at 8 s; a body with a live opponent never relaxes.
- **Docs:** `ElysiumPlayer.h` comment; `animation_and_movers.md` already carries the four paths.

#### T-C3 `AddGesture` re-uses a live layer

- **Where:** the cast arm in `ElysiumAnimationDriver.cpp` around line 114; `FElysiumOverlayStack`.
- **Change:** before `AllocateLayer`, `FindGestureLayer(activity)` over live slots; on a hit
  return that slot untouched (no cycle reset). Sequence index `0` is also a refusal in retail.
- **Gate:** `Elysium.Substrate.OverlayStack`: two identical pushes hold one slot.
- **Docs:** `animation_rig_resolution.md` overlay contract already states it.

#### T-C4 Envelope arithmetic

- **Where:** `Source/ElysiumUE/Public/ElysiumAnimationIntent.h:369` (`WeightForCycle`).
- **Change:** two independent `if`s (blend-out wins in the overlap), the whole spline under
  `if (Blend.In < 0.95f || Blend.Out < 0.95f)`, else weight `1.0`. `3.0` is confirmed byte-exact.
- **Gate:** the existing assertions at `ElysiumAnimationActionTests.cpp:2579–2610` stay green;
  add `In = Out = 0.6, Cycle = 0.5` → the out-ramp's value, and `In = Out = 0.95` → `1.0`.
- **Docs:** none.

#### T-C5 Slot timelines

Retail dispatches each overlay layer's own events with its own `m_flLastEventCheck`, no weight
gate, no clamp, no finished notification; autolayers never dispatch (`animation_events.md` →
"Overlay layers dispatch their own timelines").

- **Where:** `Source/ElysiumUE/Private/Substrate/ElysiumAnimatingImpl.cpp:315` and the comment at
  `ElysiumBipedAnimInstance.cpp:1584` ("One PUBLISHED timeline per body, because retail has exactly
  one") — that comment is wrong and comes out.
- **Change:** one cursor per slot beside the base cursor; the slot's `Phase` comes from
  `FElysiumOverlaySlotRecord.Cycle`; a pushed `_attack_layer`'s `3031`/`5003` fire from the slot.
- **Gate:** `Elysium.Substrate.AnimEventDispatch`: a slot timeline fires while the base is idle.
- **Docs:** none beyond the comment.

#### T-C6 The aim-pitch slew

Retail latches the view pitch (`pl.v_angle.x`, one-sided fold `v ≥ 180 ? v − 360 : v`) and
approaches it at **180°/s** in `UpdatePoseParameters` before it reaches the model; `aim_yaw` is a
literal `0.0f` on the player (`animation_and_movers.md` → "The aim pair is latched and slewed").

- **Where:** `ElysiumAnimationDriver.cpp:826–827` (`Selection.AimYaw/AimPitch = Intent…`).
- **Change:** latch `Intent.AimPitch`, `ApproachAngle` at `180 · dt` toward it, publish the
  approached value; publish `AimYaw = 0` for the player. Do **not** feed the capture's `aim_yaw`
  (T18).
- **Gate:** a Substrate assertion on the slew; `RigCompose` gains an aim-frame cohort once the
  aim fan is bound (the capture has no pose-parameter value for it — T18 — so that cohort scores
  the pitch derived from `player_state` velocity-free frames only).
- **Docs:** none.

#### T-C7 The cross-fade chain

Retail's client keeps a list of surviving previous sequences; each frame every survivor is
`SlerpBones`'d toward the **old** pose at `SimpleSpline(1 − (now − start)/dur)`, newest index
first, `dur = max(fadeIn(in), fadeOut(out))` from the descriptor's 612/616/620 triple (`0.2` on
5,762 of 5,836 sequences), refused outright (hard cut) when the incoming clip carries
`flags & 0x2`, with no cap on concurrency (`animation_and_movers.md` A.4c). This is the
`RigCompose` control residual's leg component.

- **Where:** the blend stack in `ElysiumAnimSubsystem` / `ElysiumBipedAnimInstance`; the graph's
  base-channel transition.
- **Change:** reproduce the list and the ramp as a runtime rule; the set-aside cohort in
  `RigCompose` (610 frames, 3.97 cm) becomes a scored cohort with the same thresholds.
- **Gate:** `RigCompose` set-aside cohort ≤ 1.0 cm median; control cohort whole-body ≤ 1.0 cm.
- **Docs:** `docs/architecture/animation-architecture.md` names the rule and its address.

#### T-C8 Between-key interpolation — measure, then decide

Retail nlerps between authored frames (`QuaternionBlend`, `animation_and_movers.md` A.4); Unreal
samples baked keys through its own curve at 60 Hz, which on an 18 fps clip is every drawn frame.

- **Change:** an offline comparison (extend `armsway.py`): retail's nlerp at sub-frame `s` vs
  Unreal's evaluation of the baked sequence at the same time, over every gait cell. If the p90
  exceeds 0.5 cm, bake at the sample rate (60 Hz keys) — a representation change, not a rule.
- **Gate:** the number decides; record it in `animation-architecture.md` either way.

### Phase D — closure

#### T-D1 The played acceptance, on numbers

Both bodies through the compose harness with the M37 and a supershotgun; `RigCompose` fully green
across all three cohorts; T-A3 green; the movement baselines re-committed. Then, and only then, a
played session — which is the owner's, not an agent's, and is not evidence for any task above.

#### T-D2 Docs and the register

`docs/project/plans/animation.md` → LIFE10 gap table struck through per task as it lands (that
document carries no status; the row is removed, and the roadmap is the owner's).
`docs/project/rebuild-strategy.md`'s reproduction register lists the four runtime rules from §0.

---

## 5. Retired by evidence — no task exists

Do not open work on any of these; the corpus proves them inert.

| item | evidence |
|---|---|
| bone controllers (`CalcBoneAdj`) | `NumBoneControllers == 0` on all 4,255 retail + 339 patch models |
| IK | 976 chains on 240 models, `numikrules == 0` on all 14,315 animdescs, no solver in any module |
| `STUDIO_AUTOPLAY` | 0 of 14,012 sequences, 0 of 4,445 headers; its cycle is a literal `0.0f` |
| `QUATINTERP` procedural bones | decoded at `client.dll 0x1008ae50`, 0 of 3,126 rules declare it |
| the 1×N blend-grid branch (blends with `s0`) | no 1×N grid exists in any bank |
| shaky-hands weapon sway | `SetShakyHands` has zero callers |
| the weapon activity lookahead (`m_aCur/NextWpnActivity`) | slot 289/290 return constants `-1`/`1` on every weapon but the frag grenade |
| the `hands box` root-drift defect | zero skin influence on the chain; `Box02` under `Bip01 R Hand` carries every vertex |
| the server's second bone merge (`flags & 0x0C` filter) | affects server attachment queries only; the drawn frame is the client merge, which we reproduce |

---

## 6. Definition of done

- Every instrument in §3.3 green, including the three cohorts of `RigCompose` at ≤ 1.0 cm median
  and the arm scalar at ≤ 2.5 cm on both bodies.
- T-A3 green on every gait fan.
- A fresh `uv run elysium debug compose` on each body scoring green under T-A2.
- The four runtime rules of §0 each named in `animation-architecture.md` with their retail
  address, and no fifth.
- `uv run elysium doctor --repo-only` clean; the Substrate and Content tiers green in full.
