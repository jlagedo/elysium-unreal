# Animation Roadmap — VtMB animation data on Unreal's animation system

## Goal and ownership

This tracker drives one decision and the work that follows from it:

> **Bake VtMB's animation data into native Unreal assets and let Unreal's animation system run
> it. Resolve every VtMB rule at bake; write only the stage that reads a live pose.**

It owns the **skeletal animation asset stack**: the character asset bake, shared skeleton, layer
masks and their bindings, blend spaces, and the extraction and bake of the action catalog a resolver
reads. It also owns the one custom composition stage, because it is the only part of retail's pose
pipeline that survives into the Unreal design.

**It does not own:** the runtime resolver seam, the player animation graph, or the locomotion that
drives them (`docs/project/three-cs-roadmap.md`); facial flex, eyes, the amplitude jaw or lipsync
(`docs/vtmb/facial_animation.md` owns the specification, `docs/project/roadmap.md` owns their
status); choreographed scene semantics (`docs/vtmb/choreographed_scenes.md`); secondary motion
(`docs/project/retail-capture-roadmap.md` CAP5.5); or any VtMB format fact. Facts belong in the
owning `docs/vtmb/` topic, the Unreal design belongs in
`docs/architecture/animation-architecture.md`, and project priority and roll-up status belong in
`docs/project/roadmap.md`. This file owns detailed status and task order for the programme, and
nothing else.

`docs/project/roadmap.md`, `docs/project/three-cs-roadmap.md` and
`docs/project/retail-capture-roadmap.md` are this tracker's siblings. The 3 C's slice consumes what
this tracker bakes; retail capture remains the oracle for VtMB behaviour and is not a gate on
anything here.

## The decision

Reading VtMB's own layer machinery — an autolayer binding table, a per-bone weight mask, and a
dispatcher that evaluates layers recursively — makes it possible to reproduce Source's animation
system inside Unreal. **The owner call is not to.** That path ends in a Troika simulator: a bespoke
pose evaluator, a hand-written per-bone blender, a hand-rolled grid resolver, and a research
programme to recover blending semantics that only matter if the goal is numerical equivalence.

The goal is not numerical equivalence. It is VtMB's animation *content*, running on Unreal 5.

The mapping is close to one-to-one, which is the evidence the decision is right:

`CCC*` rows are `docs/project/three-cs-roadmap.md`'s, which owns the resolver seam and the graph
this tracker's assets feed.

| VtMB | Unreal | Status |
|---|---|---|
| player/NPC state → base `ACT_*` → actor/weapon translation | one engine-neutral intent and shared activity resolver | ANM4 · CCC4 |
| Blend grid (9×1 `move_yaw`, 3×3 `aim_yaw`/`aim_pitch`) | `UBlendSpace1D` / `UBlendSpace` | ANM3 |
| `StudioAnimRecord.weight`@0 per-bone mask | `UBlendProfile` in `BlendMask` mode + `FAnimNode_LayeredBoneBlend` | ANM2 · CCC10 |
| `flags & 0x14` `_delta` clips | `AAT_LocalSpaceBase` additive, conjugated at bake, `ABPT_AnimFrame` naming the base the file declares | ANM1 · CCC10 |
| `autolayerindex`@664 | a binding table the graph consults — **data, not mechanism** | ANM2 |
| Shared animation banks, `Bip01` naming | one shared `USkeleton` — zero retargeting | ANM1 |
| Stance and locomotion transitions | state machine transitions | CCC5 |
| One-shot clips, gestures, disciplines | montage slots | CCC5 · ANM6 |
| `Flags & 0x2` split inheritance, ancestors readable | re-expressed against the parent at bake — ordinary FK, no runtime rule | ANM1 |
| `Flags & 0x2` split inheritance, ancestors masked out | `AAT_RotationOffsetMeshSpace` / `UAimOffsetBlendSpace` — the one mask that owns the split bone | ANM1 · CCC10 |
| `ProcType == 1` axis interpolation | **no Unreal equivalent — ours stays** | done |

The last row is **correctness, not feel**: it reads a live control-bone orientation, so it is a rig
rule rather than a frame conversion and is the standing exemption to the root `CLAUDE.md` rule
"Poses are baked native". It is already built and verified three ways, and keeping it is not a step
toward a simulator. Everything above it is Unreal's job, **including split inheritance** — a
defect in how 2004's exporter stored one bone, resolved at bake in the frame the animators actually
authored in, never carried into the frame path.

**How this reads against the remaster charter.** Animation blending is the Feel layer, where
`docs/project/remaster-direction.md` calls for building the reverse-engineered original first and
polishing one delta at a time by explicit owner call. This decision is that owner call, made once
at the architecture level rather than per clip: the authored *content* — which clip, which mask,
which grid cell, which layer rides which base — is reproduced exactly, and the *interpolation
between* those authored values is Unreal's. Divergences that follow are listed at the end of this
file and each is recorded beside the faithful behaviour in the topic that owns it.

## What it supersedes

- **A bespoke layer stage as the destination.** The animation proxy composes both kinds of layer
  itself because there is no graph to hold a layered blend or an additive node yet. What is
  superseded is treating that stage as the design rather than as the stand-in; ANM4 says what
  replaces it and what that costs.
- **A hand-rolled grid resolver.** `ElysiumBlendGrids`'s nearest-cell pick computes interpolation
  fractions and discards them. Replaced by baked blend spaces; the parse survives for provenance
  and for the per-cell ground speed the motor consumes.
- **A capture and decompilation phase for reproducing pose-blend arithmetic.** An earlier design
  gated runtime work on recovering retail's autolayer weighting, evaluation order and delta
  reference frame. Those questions only need retail's answer if the blending is being matched
  numerically. **Dropped.** Capture returns as an oracle when a *visible* blend divergence needs
  explaining, never as a gate. Action selection is different: it decides authored gameplay
  content, so its extraction and reachability proof remain ANM4a.
- **CAP7.3, CAP7.4 and CAP7.5** in `docs/project/retail-capture-roadmap.md`. CAP7.1 (the rule
  table) and CAP7.2 (the composition-stage evidence) are complete; what CAP7.2 established about
  `Flags & 0x2` is what the bake now resolves, rather than what a runtime stage applies.

## Evidence

Measured read-only against the owner's install and the current export. Each row is reproducible and
names what it decides. **Rows marked ▶ are VtMB facts owed to `docs/vtmb/animation_and_movers.md`**,
which owns them; they appear here as the evidence for a project decision, not as their own record.

| Measurement | Value | What it decides |
|---|---|---|
| ▶ `StudioAnimRecord.weight`@0 value set | `{0.0, 1.0}` only, over **736,208** `(anim, bone)` records across **4,508** models | The field is a binary per-bone mask, so it is a blend mask rather than a weight curve |
| Distinct masks per bank | **5** over 722 animdescs — unmasked 396, root+pelvis+spine+legs 308, arms+props 12, orphans 4, `Bip01 Head` alone 2 | Five blend profiles per skeleton, not a per-clip table |
| Zero-weight records carrying a channel offset | **0 of 736,208** | The current export *drops* masked bones rather than corrupting them |
| ▶ `numautolayers`@660 histogram | `{0: 10,278, 1: 237, 2: 224}` over 4,249 models; the only carriers are `character/shared/{male,female}/move_and_ranged.mdl`; **0** out of range, **0** self-referencing, depth **1**, fan-out **≤ 2** | A fixed two-slot binding; retail's recursive dispatcher never recurses on shipped content |
| ▶ Autolayer targets | 111 distinct, every one a `_layer` or `_delta` sequence, **none carrying an activity** | Layers are composed, never selected as a base |
| ▶ A second binding mechanism | **46** masked sequences are selected by the game DLL through `ACT_*_LAYER_*` activities — weapon attack/reload/dryfire, the discipline casts, and both `lookback_*_layer` | A masked sequence reaching the base clip path is a defect no autolayer work fixes |
| Orphan layers | exactly **5 per bank**, byte-identical on male and female | Abandoned authoring — but orphanhood must be *measured* across the include DAG, not assumed |
| Blend grids in the export | **275** grids over 15 sidecars: 222 are 9×1 on `move_yaw`, 49 are 3×3 on `aim_yaw`/`aim_pitch`, 4 on `hit_yaw` | Directly `UBlendSpace1D` and `UBlendSpace` |
| Split-inheritance forward-kinematics check | with the rule applied, `walk` head-rise `+0.428` (upright); a masked layer standing alone `+0.000` rise / `+0.431` forward (flat) | The fold is a reference-pose fallback on the split bone, which a layered blend removes by construction |
| Layer masks containing the split bone | **1 of 4** — the 49-bone upper-body gate the `*_aim_layer`/`*_bobble_layer` families carry; the 24-bone right arm, 45-bone left arm and 1-bone head masks do not | Scopes the un-normalizable case to one family, which is why it gets a representation of its own rather than the frame path getting a rule |
| Chain above the split bone, deviation from bind | `Bip01` / `Bip01 Pelvis` / `Bip01 Spine`, and `Bip01` alone carries almost all of it — **63°** on a weapon idle, **82–99°** on walk and run | Kills normalising a masked overlay against the bind chain: retail declines `Bip01` too, so the overlay would arrive rotated by the character's own root rotation. The base has to be a real host pose, which the autolayer table names |
| Conjugation spread of one additive across the hosts that declare it | up to **19.65°** on the bobbles, **81.64°** worst (`throwing_star_attack_delta`, 6 hosts) | An additive bakes once per declaring host rather than once per delta; one asset per delta cannot serve them all |
| Morph target names across the cast | **54 distinct**; 86 of 166 models carry a flex rig, each with the same 53 FACS action units | One shared skeleton carries one 54-name curve set — the face costs the bake nothing |
| ▶ `numautolayers` bounds hazard | 7 single-sequence scenery and weapon models read **764**, the descriptor tail running past the file into the string table | A bounds gate is mandatory in the exporter, and its population is named |
| ▶ Player action path | `PostThink` `0x1016be10` → classifier `0x1016bb50` → mode router `0x10164240` → ordinary selector `0x10164870` → apply/select `0x101644f0` | The player is driven by realized state and an activity policy, not by a button-to-clip table |
| ▶ Player-body activity reach | **56** player models → **122** exact owner models → **3,330** sequence descriptors carrying **1,202** distinct non-empty activity literals case-insensitively (1,203 as spelled); one body's DAG of 33 models already reaches all of them | The model inventory is generated data; a manually maintained action/clip list cannot be the runtime contract |
| ▶ Weapon translation table | virtual `+0x5a4` translates; `+0x5a8`/`+0x5ac` expose 12-byte `{base, weapon, required}` rows; `activitydump` prints them | Weapon-specific locomotion/combat is extracted per class rather than hardcoded in the player graph |

## Phases

Numbered for dependency, not for date. ANM1 and ANM2 are independent and start together.

- [ ] **ANM1 Bake the character assets.** One shared biped `USkeleton` built from the `Bip01`
  hierarchy, with the outliers — animals, skeletal props, `wolf_form` — on their own; a
  `USkeletalMesh` per model with morph targets intact; a compressed `UAnimSequence` per clip, with
  the `_delta` family flagged `AAT_LocalSpaceBase` / `ABPRT_RefPose` at bake time.

  A new editor commandlet beside `pipeline/unreal/bake_map.py`, on the same `/ElysiumBaked` mount
  and the same gitignored, regenerable posture. **glTFRuntime stays** only as the bake-time reader rather
  than being replaced: the vendored multi-primitive morph-target patch is load-bearing and fails
  silently when lost, and keeping the same reader keeps it earning.

  *Retires:* the per-map-epoch retarget cache, the `RemoveTracks` bank filtering, and the constraint
  that resolved sequences cache on the map actor rather than the subsystem.
  *Acceptance:* every character loads from the baked mount with no glTFRuntime call at runtime — it
  is the only build of a character, so a stem the export misses fails by name rather than falling
  back — and the
  facial morph-target contract still holds.

- [ ] **ANM2 Carry the two discarded MDL fields.** `autolayerindex`, unioned across the include DAG
  so orphanhood is measured rather than assumed, and the per-bone weight mask, deduped. The masks
  bake into blend profiles on the shared skeleton; the base→layer binding stays data the graph
  reads. Needs the bounds gate the evidence table names.
  *Acceptance:* every autolayer target resolves to a label, the orphan census reproduces, and the
  mask count per bank is 5.

  **The mask half is delivered.** The character container carries a de-duplicated mask table and a
  per-clip index into it, and the character bake turns every mask a non-additive clip references
  into a blend profile on the shared skeleton, named for the bones it owns, with the clip carrying
  the profile's name as its own metadata. The male `move_and_ranged` bank ships **4** masks over its
  826 exported clips plus the unmasked state, which is the evidence table's five; `misc` ships one
  more. Because a profile is named for the bones it owns *after* they resolve against the skeleton,
  masks that differ only in bones a family does not have become one asset: the baked male family
  skeletons carry **4** profiles and the female **5**.
  `Elysium.Content.BakedCharacterParity` asserts each baked layer's profile against the container's
  mask bone for bone, and asserts that a bone the mask owns and the clip does not animate holds the
  container's bind pose rather than the skeleton's reference pose.

  **The binding half is not**, and its blocker is gone rather than open: `numautolayers`@660 /
  `autolayerindex`@664 are decoded and censused in `docs/vtmb/animation_and_movers.md` A.3, so what
  remains is exporting the table across the include DAG, not reverse-engineering it. Until it ships,
  a layer is played by explicit request and nothing in gameplay selects one.

  The table exports beside the blend grids, in `blends/<stem>.json`, because it is read from the same
  764-byte sequence descriptor. Its verification splits three ways by what can answer each question.
  The **census** is answered offline against the install and measured once, since equality with
  retail's own `.mdl` cannot live in the repo. **Structure** is a content-tier assertion, and it is
  genuine only where it crosses the table against something another export declares independently —
  a target resolving to a clip its owner's `.glb` carries, a `_delta` target's `0x14` stamp, a
  `_layer` target owning a mask. **Composition order** is answered by the table itself: the
  dispatcher walks entries in index order, so entry 0 being the overlay and entry 1 the additive is
  a fact the file states, and it replaces the assumption the proxy's accumulation loop carries today.

  **The weight the accumulator receives from its caller is a separate open question with a different
  oracle.** The four-byte record carries no weight, ramp or flags, so the scalar lives in the game
  DLL and only retail can answer it. The first move is an analysis pass over the finalized captures
  rather than a new hook: the combine is closed arithmetic, so a host's decoded local, its layer's
  decoded local and the composed local determine the scalar per bone, and a value consistent across
  the mask's bones measures it while confirming the combine. Acceptance is a coverage argument, not
  a number — `StudioAnimRecord.weight`@0 read 1.0 across an entire cutscene corpus and settled
  nothing — so the recipe has to exercise weapon draw, an aim transition and a sequence crossfade,
  and constancy counts only when witnessed over conditions that would have varied it.

  *Acceptance for the binding half:* a weapon and action select their own `_aim_layer` and `_delta`
  through the runtime's own caller, at a weight that is either measured or named in the code as a
  stand-in; the green room displays what was selected, the selecting authority, and the declared
  entry order beside what is actually running, rather than being the thing that selects.

- [x] **ANM3 Bake the blend spaces.** A `UBlendSpace1D` per 9×1 `move_yaw` fan and a `UBlendSpace`
  per 3×3 aim grid, samples placed at the axis values the grid declares. The −180/+180 endpoint
  cells are already duplicates in the authored data, which is how a wrapping axis is authored.
  *Acceptance:* a walk grid blends across its cells instead of playing the base cell — met, and
  witnessed on a stood body: dragging `move_yaw` sweeps the stride continuously rather than snapping
  between the eight authored directions.

  This also moves the runtime *toward* retail rather than away from it. Retail blends: across two
  full `sp_theatre` captures every one of the 3,440 multi-blend contributions fired exactly two
  adjacent cells (`docs/vtmb/animation_and_movers.md` §A.4b). Selecting a single nearest cell was
  the simplification; what remains a divergence is the interpolation curve between two cells, which
  is Unreal's, and it is recorded below.

  **The assets.** The character bake turns every grid a clip owner declares into a
  `UBlendSpace` beside the sequences it samples, reading the same sidecar and through the same
  reader the runtime uses. A sample sits at `paramstart + k·(paramend − paramstart)/(groupsize − 1)`,
  which is the grid's own range and owes the pose parameter nothing — the descriptor's `start`/`end`
  cancel out of retail's axis resolution, and only the wrap consults it. Axis wrapping is left off
  even on `move_yaw`: the fan duplicates its clip at both ends, so clamped interpolation already
  reproduces retail across the seam, and enabling it would put two samples on one point where the
  engine rejects the second. `Elysium.Content.BakedCharacterParity` asserts each baked grid's axis
  ranges and divisions against the sidecar, each sample's position and animation, and that the
  triangulation was built.

  **A grid's cells agree on their bone mask, measured rather than assumed** — 135 grids over the six
  male banks the parity slice reaches, with the bake refusing to write one that disagrees. That is
  what lets a whole grid sit behind one layered-blend node in ANM4, since no Unreal blend node masks
  per sample. The fact itself belongs to `docs/vtmb/animation_and_movers.md` §A.4.

  **Nothing in gameplay drives one**, which is `docs/project/three-cs-roadmap.md` CCC5's. The runtime
  carries a base blend-space slot in the proxy under `elysium.BlendSpaces`, with the green room as its
  only caller: a grid row stands the whole fan and a slider per declared axis steers it. That slot is
  the same kind of scaffolding as the layer slots below, and CCC9 retires it the same way. An aim
  grid is baked but not standable — its cells are masked overlays, so it is a layer's grid and the
  base slot refuses it for the reason a masked sequence must never reach the base clip path.

- [ ] **ANM4 Extract and bake the action catalog.** The generated corpus a resolver reads: the
  activity registry, the player and NPC selection rules, every weapon translation table, and the
  reachability join that proves the set is complete. **The resolver and the graph that consume it are
  `docs/project/three-cs-roadmap.md` CCC4 and CCC5**, which run on six activities read out of the
  model by hand and therefore do not wait on this phase; what waits is every action family beyond
  them.

  Ordered work inside the phase:

  - [x] **ANM4a Decode and extract gameplay action selection (RE37).** The player core path,
    common NPC translation/sequence/transition chain, activity registry, final selectors,
    forced-sequence bypass and complete weapon-table surface are located.
    The hash-gated `ELGACT1` instrument and `sm_hub_1` controlled recipe capture classifier,
    ideal-activity, weapon-translation and weighted/heaviest selection boundaries into the shared
    SQLite evidence database. The first retail hub corpus passes all verifier gates with 72,440
    action records: it joins compact codes `0`/`1`/`2` through the ordinary player translation order
    to exact idle/walk/run/sneak/crouch/leap/falling/land sequence identities, and proves that the
    ordinary jump does not use `ACT_LEAP_ASCEND`/`ACT_LEAP_DESCEND`. Its NPC rows now form 1,232
    complete caller-delimited translation resolutions: 795 ordinary weighted choices and 437
    translation-only queries, joined by serial-bearing entity handle to avoid index reuse. The
    reproducible demand survey now covers all 22 exported maps and their script/dialogue surfaces:
    all 198 live map-authored exact-label requests and 18 `SetAnimation` wires have an explicit
    manifest answer, retail fallback or bounded content-miss disposition. It also inventories all
    three `MorphModel` fields and 356 Python `SetModel` calls. The weapon survey extracts all 169
    combat-weapon subclasses, 61 non-empty class tables and 9,214 ordered rows; it proves the stored
    `required` flag is inert in the server translator and joins 269 of 270 current-map equipment
    references. The sole `item_w_sw_m64` reference has no retail factory string, item definition or
    acttable and remains an explicit content miss. The NPC-class survey collapses all 77
    `CAI_BaseNPC` descendants to 10 pre-translation, five class-translation and two cover/reload
    delegate bodies; all 29 paired-action bases and their 232 role/size/side variants are resolved.
    It joins 425 of 426 current-map class demands; the Night Watchman's `npc_BaseVampAI` is absent
    from the retail DLL and remains an explicit content miss. The native schedule survey decodes
    691 schedules / 4,139 task invocations. The complete RTTI task-slot survey adds all 184
    class-local registrations across 20 owners to the 330 shared task IDs, collapses the 77 NPC
    subclasses to 29 distinct StartTask and 24 RunTask bodies, and dispositions all 49 custom
    bodies. Its 100 policy rows cover 111 task routes: 31 custom bodies request animation and 18
    add no direct animation policy. Every custom animation request is an activity route; none looks
    up an exact label or creates a layer. Sequence-event RE is closed separately: the 76-byte
    record validates across all 4,445 retail MDLs (**1,150 event-bearing sequences / 1,872 events /
    58 IDs**), server dispatch and all **300/20** animating plus **169/7** weapon-handler
    class/body surfaces are decoded, and client dispatch collapses **237** animating classes to
    three bodies plus all **214** weapon classes to one hook. The player paired-action router is
    closed separately: all nine initial activities, the six stateful
    continuation leaves, five producer sites and the two-actor translation/commit order are
    recovered; modes `1`/`4`/`7` have no pinned-binary caller. Model autolayer composition and the
    separate combat-layer/sequence-transition interruption order are also closed: autolayers enter
    at full caller weight in authored order, while combat layers start at 0.1 and compose by slot.
    The player compact/form surface is closed too: the compiled table names all 17 `PLAYER_*`
    codes, the complete `+0x704` pass dispositions 15 genuine player-animation calls separately
    from 14 NPC task-name calls, both player RTTI classes share one protected/router/ordinary
    implementation, and patch-first vdata contributes only two `PLAYER_VOMIT` fields. Thirteen
    codes are reachable; `PLAYER_SUPERJUMP`, legacy `PLAYER_FEED` and the two aiming-transition
    codes have no pinned producer. One-shot completion is closed too: an unchanged activity reuses
    its sequence until `StudioFrameAdvance` marks it finished, then the next request reselects the
    same sequence and `ResetSequenceInfo` restarts it. Thus a sustained unarmed crouch repeats
    sequence 8 after its 61@30 completion; the 1.791 s trace independently proves it is not restarted
    early. The tracked working set is
    `research/cases/animation-pose/specs/gameplay_actions.json`; facts land in
    `docs/vtmb/animation_and_movers.md` A.3, never in this tracker.
  - [ ] **ANM4b Export and bake the action catalog.** Extend the existing clip/grid/index sidecars
    with events, autolayers and the per-model sequence-transition graph — the NPC resolver's
    `AdvanceToIdealActivity` traverses an intermediate transition sequence between the current and
    ideal sequences, so the graph is a catalog input `docs/project/three-cs-roadmap.md` CCC11
    depends on — emit activity/player/NPC/weapon rule artifacts, join them into a
    reachability report, and bake the resolved catalog beside the native character assets. Game
    data remains below `$ELYSIUM_EXPORT_ROOT` and `/ElysiumBaked`.

  *Acceptance:* every producer in the accepted action families resolves to an exact model/sequence
  identity and baked asset or to a named fallback; a controlled retail/remake trace agrees on base
  activity, each translation and final sequence; missing required mappings fail content tests;
  player and NPC records use one trace schema.

- [ ] **ANM6 Migrate the cinematic path.** Choreographed scene playback from the sequence player's
  absolute-time seek to a montage position. **Deliberately last.** The theatre is the project's
  proven ground and its seek path is verified; there is no reason to put it at risk before the
  stack underneath it is established.

## Interface notes

Bounded, because this tracker does not own these systems.

### The face

**The face and the body meet at a curve interface, not a pose interface.** The facial rig's entire
output is named float curves flagged as morph-target curves on the output pose; it writes no bones.
The eyes read an eyeball bone's transform to build an aim basis and then write shader parameters,
so they write no bones either. Nothing in the bake changes either path, and the two systems keep
the property that a crossfade does not touch the face and a blink does not touch the pose.

The bake contract is therefore narrow: morph targets preserved, the duplicate-target merge strategy
retained — a face spans several material primitives and glTF weights are mesh-level, so the pieces
must be stitched rather than the first kept — and morph-target curve metadata authored at bake.

**This deletes a gotcha.** Curve metadata registration transacts by default and reaches the editor
transaction buffer, which does not exist when the game runs under the editor executable in game
mode; the runtime path works around it. Metadata authored at bake and serialised removes both the
hazard and the workaround.

### Lipsync

Phoneme tracks are authored *timed* data and nothing consumes them yet, so there is nothing to
port. Designing them onto **montage curve tracks** rather than per-frame hand-driven weights lands
them on the same mechanism ANM4 introduces, at no migration cost, and hands timing and blending to
Unreal instead of to the substrate clock. **Recorded here as a forward note only** — the decision
belongs to `docs/vtmb/facial_animation.md`'s owner and to `docs/project/roadmap.md`'s status row.

### Secondary motion

A second post-process stage, beside axis interpolation, on disjoint bones. It is undesignable until
the solve beneath its proved angular limit is decoded;
`docs/project/retail-capture-roadmap.md` CAP5.5 owns that.

## Risks

- **The theatre is the thing to protect.** Choreographed playback shares the animation instance with
  everything the graph replaces (`docs/project/three-cs-roadmap.md` CCC5, CCC9). ANM6 is last for
  this reason, and the theatre stays on the verified seek path until the stack under it is
  established.
- **Keep an A/B per stage.** Every composition and blending stage carries a console toggle. This is
  not ceremony: the split-inheritance fold that started this programme was diagnosed by turning the
  stage off and watching the pose change. Once a rule moves into the bake its A/B moves with it —
  the comparison is between two exports, not between two frame paths.
- **The animation iteration loop grows a bake step.** Export already requires an editor build, so
  the prerequisite is not new, but the edit-to-see-it cycle lengthens for character work.
- **Unreal's blending is not Source's.** Where a shipped pose looks wrong, the first question is
  whether the authored data was carried correctly, and only then whether the interpolation differs.
  The A/B toggles and the retail capture database are what separate those two.

## Open defects

- **`A_dance01` leaves a residual arm seam on some bodies.** With the skeleton reference pose
  authored rotation-flat (`docs/architecture/animation-architecture.md` § 2.4),
  `A_Stance_Joy_Idle_1` measures clean on every body sampled, but `A_dance01` still stretches 4 edges
  past 5× their reference length on `goth_female` and on `tremere_female_armor_0`, and the two report
  the same maximum to nine figures. `tremere_female_armor_0` carried those edges before the reference
  pose changed and was treated as correct throughout, so the cause is independent of the retarget
  path. On `female_dancer_2` the same clip lands at 3.590× on the tracked seam pair against 1.353×
  for Joy. Whether any of it is visible is unmeasured — the metric is edge stretch under CPU skinning,
  not an observed frame.

  The instrument already exists: `Elysium.Content.DancerDecodeProbe3` reports per-bone remap state and
  per-edge deformation for both clips on four bodies. The open question is which bones carry the
  stretch, and then whether their translations, rotations or skin influences differ from Joy's on the
  same body.

## Divergences to record when settled

Each is recorded beside the faithful behaviour in the topic that owns it, and marked as a
divergence, per the house rules.

| Divergence | Owning document |
|---|---|
| Layer blend-in and blend-out times are ours — VtMB's four-byte autolayer record carries no ramp, so whatever weight the dispatcher passes lives in the game DLL and not in the file | `docs/vtmb/animation_and_movers.md` |
| Unreal blend-space interpolation replaces the authored grid's own cell selection | `docs/architecture/animation-architecture.md` |
| Baking a per-clip rule leaves a residual across a crossfade, since blend-then-evaluate and evaluate-then-blend differ for a non-linear rule. Bounded and measured: none above 10° over a real body's crossfadeable locomotion | `docs/architecture/animation-architecture.md` |
| An overlay or additive composed over a host that does **not** declare it is approximate, because the asset was written against a declared host's pose. Retail's own rule is that a layer only means something over a base that declares it | `docs/architecture/animation-architecture.md` |

**Not divergences**, and recorded as defect fixes with their numbers rather than as choices: keeping
masked sequences out of the base clip path, resolving `Flags & 0x2`'s model-space storage at bake
instead of reproducing it, and the autonomous walk paths using a hardcoded ground speed where the
authored clip cell states a different one.
