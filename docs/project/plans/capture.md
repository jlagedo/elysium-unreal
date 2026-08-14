# Retail capture plan (CAP) — open-task specifications

Specifications for **open** CAP tasks only. Status, order and the landed one-liners live solely
in `docs/project/roadmap.md`; a task that lands is deleted here, its durable facts written to
the owning `docs/vtmb/` topic. This file carries no status marks.

**The programme.** One private instrument against one exact owner-controlled retail build (the
Unofficial Patch launcher over the original `client.dll`/`engine.dll`/`StudioRender.dll`, plus
the owner-install `vampire.dll` image — the wrapped `vampire.dll.12` carries none of the
recovered addresses), and one loop: run `sp_theatre` → capture → decode and index → inspect
against the export and the decoder → close the difference in the export and the runtime.
**Capture is the oracle, not the gate**: a rule that passes the evidence gate carries into the
export and runtime immediately, and a system whose format is already closed is built from that
specification and captured only where the build diverges. Method — working rules, evidence
gate, mismatch vocabulary, triggered improvements, non-goals, risks:
`docs/vtmb/vtmb-animation-reverse-engineering.md` § "Programme method". Hook addresses and
specs: `research/cases/animation-pose/specs/`. Game-derived binaries, captures and reports stay
under `$ELYSIUM_WORK_ROOT/research`.

**Finish line:** engine-neutral code consumes the owner's original resources and reproduces the
retail outputs the theatre corpus needs. The delivered outcome is `sp_theatre` playing with
every animation live; the skeletal-asset half is the ANM plan's, the scene/camera/audio/subtitle
halves are the theatre plan's.

**Critical path:**

| # | What | Owner | State |
|---:|---|---|---|
| 1 | The two composition rules reach the runtime | CAP7.1, CAP7.2 | done; visual isolation still owed |
| 2 | The shipped clip decoder differenced against retail | CAP4.3 | done — the decoder is clean |
| 3 | The difference instrument models the shipped multi-biped path | CAP5.7 | done — no correspondence error |
| 4 | The face built from its closed spec | 12.3–12.5 | 12.3 pending a clean visual; 12.4, 12.5 open |
| 5 | Blend grids + the animation weight carried out of the model | CAP5.3 → ANM | CAP5.3 done; the bake is ANM's |
| 6 | Include-model remap route | CAP5.8 | done — 6,927 → 2,452 |
| 7 | Frame interpolation — is the host's LINEAR retail's | CAP5.10 | open |
| 8 | Secondary motion — hair and cloth | CAP5.5 | solves located; replays open |
| 9 | The persistent partial update adjudicated | CAP5.4 | open; the measuring replay exists |
| 10 | A visual-acceptance harness isolating one body | pipeline green-room path | open; blocks rows 1 and 4 |

### CAP2.8 The fourth pose-build frame

Melee (`baseball.mdl`/`tireiron.mdl`) runs 3,926 pose evaluations on the render thread outside
the three bracketed frames, with `generation` and `generation_entity` both zero; the callers are
the ordinary `dispatch_model_pose`/`evaluate_sequence_pose` sites, so the unhooked frame is the
one above them. Find it by walking the callers of `resolve_virtual_model_pose` that reach none
of the three, then bracket it beside them. *Acceptance:* the same scene records zero unassigned;
the 595 out-of-interval records and the unresolved skeletal entity close with it or are counted
apart with a reason. Off the `sp_theatre` path; blocks nothing.

### CAP2.9 Complete the `sp_tutorial_1` corpus

A longer combat run: whether it reaches the 2×1 grids, what the melee frame changes once CAP2.8
brackets it, and whether the run's own request population closes the way the theatre's does.
Deferred behind the theatre track; blocks nothing.

### CAP4.4 Missing-work report

Consolidates, no longer gates: the ranking *across* passes and the decode-stage result CAP4.3
has not produced, so CAP5's working order is measured rather than chosen. Never claims that
unobserved animations or continuous blend space were covered.

### CAP5.1 One mismatch at a time

Take the highest-ranked cluster, trace backward from the first mismatching bone or frame, make
the smallest change that explains the evidence, add a game-independent regression, and write the
confirmed behavior into the owning `docs/vtmb/` topic.

### CAP5.2 Deeper stage capture on demand

When a mismatch cannot be explained from the retained spans, add the smallest editable raw hook
recipe at the site it names, preserving registers, bounded stack/pointed-to spans, addresses,
lengths and failures. First named site: the post-composition writer inside
`C_BaseAnimating::BuildTransformations` (CAP5.5's chains).

### CAP5.4 Adjudicate the persistent partial update

Retail's bone-to-world array is persistent and each build refreshes only masked bones, so a
drawn skeleton is an accumulation across builds; two theatre models never refresh a complete
skeleton. The accumulation replay reproduces 54,742/54,742 draws; the same pass with every bone
refreshed measures what a compose-everything evaluator gets wrong, on which bones and by how
much — the number the owner call needs. The alternative to diverging is recomputing the
loader-written mask bits offline (`0x10` from LOD0 vertex references, `0x4` from the procedural
declaration), costed from the same measurement. *Acceptance:* one measured divergence population
with worst bone/error, a recorded owner call, the divergence written beside the faithful
behaviour in `docs/vtmb/procedural_bones.md` with the number in
`docs/architecture/animation-architecture.md`, and the replay promoted into
`research/tooling/capture/`.

### CAP5.5 Secondary motion — remaining

Both mechanisms are located, solved and fully accounted (28-byte bone-chain records at
`MDLHeader` +396/+400; StudioRender particle cloth behind header flag `0x400`); facts and the
confidence ledger: `docs/vtmb/secondary_motion.md`. *Remaining:* game-independent numeric
replays of both decoded solves; compare the bone chain against a controlled retail series; for
cloth, first add the smallest post-skin particle capture around the StudioRender step/reset
boundary, then compare Sheriff and Jeanette series including delta, wind and collision inputs.
Bounded follow-ups: the nonnegative bone-record +4 branch, `bc_ground`, the single-body `money`
special case, `r_cloth`'s exact runtime gating.

The AnimDynamics calibration slice is deliberately smaller than that reproduction work: female
Malkavian armour 0 (`Bone05` → `Bone09`) and Jeanette (`Bone01` → `Bone07`, `Bone09` → `Bone13`)
only. Breast records, female Malkavian armours 1–3, and Jeanette's renderer-cloth skirt are outside
the slice. *Acceptance:* green-room and live walk/turn/stop/teleport series against the controlled
retail captures; no reset explosion or overlapping-chain flail; record intersections and LOD cost; fit the native
gravity, damping, spring, cone and motion clamps against the controlled retail series; keep the
result labelled as an Unreal presentation approximation unless the retail numeric replay itself
matches. Expansion to another body is an explicit owner call after those measurements.

### CAP5.9 The `Prince_Escort_Male` cluster

1,061 records the include transform does not explain, 65% on one two-actor cinematic bank (351
`brujah_Male_Armor_0`, 342 `Lacroix`), surviving the correction that closed everything else.
The discriminating shape is the **actor**, not the bank or cell — same grid, same 3→4 cell
pair, different residual rates per actor — which is where diagnosis starts. Undiagnosed; named
rather than attributed.

### CAP5.10 Frame interpolation

Only 15,336 of 242,561 cells fire at a whole frame, so between-key behaviour is most of the
corpus; the export defers interpolation to the host loader, and the `frame_key` candidate
(interpolation removed) sits at 50,538 over the band. Open: whether the host's LINEAR is what
retail does.

### CAP5.6 Theatre-corpus closure

The engine-neutral evaluator matches the joined theatre corpus within the recorded bands,
including layered, transition and included-model cases. Closure is stated against **CAP5.4's
adjudicated baseline**, not against retail unconditionally; unknown fields stay preserved and
explicitly unresolved unless they change covered output.

### CAP6 Face and lips

Fires only on a named divergence in the built face — 12.3–12.5 do not wait here. CAP6.1: follow
one controlled theatre line from expression/VCD/audio/`.lip`/facial bytes through load, object
construction, timing identity and the pointers facial evaluation uses. CAP6.2: capture raw
state before and after the contributing stages plus flex weights and representative deformed
vertices — the eyelid stage is the renderer's eye pass, so the capture straddles
`SetFlexWeights`. CAP6.3: reproduce controller values, lip timing, flex weights and selected
final vertices for that line; expand only to a line or model exposing a new mismatch.

### CAP8.1 Final trim

Delete probes, readers, fixtures, controls and dependencies no retained evidence path or active
investigation uses. The community-decoder harnesses are a retained evidence path —
`docs/vtmb/procedural_bones.md` and `docs/vtmb/vtmb-animation-reverse-engineering.md` rest on
measurements only their runs produce.
