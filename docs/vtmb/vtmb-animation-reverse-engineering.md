# VTMB Animation Reverse Engineering

## Evidence model, runtime-capture strategy, and investigation method

**Game:** *Vampire: The Masquerade — Bloodlines* (2004)  
**Primary target:** Troika's modified early Source engine and MDL version `2531`  
**Document role:** Exploration research brief; not a status tracker or fact owner  
**Last reviewed:** 2026-07-30

The capture instrument is retired to closure; the banked corpus under
`$ELYSIUM_WORK_ROOT/research` is the standing oracle, and a renewed capture is a scoped owner
call on a named divergence (status: `docs/project/roadmap.md`, the LIFE programme). Confirmed
VtMB behavior belongs in
`docs/vtmb/animation_and_movers.md`, `docs/vtmb/facial_animation.md`, and
`docs/vtmb/choreographed_scenes.md`. This document owns the programme's evidence
model and method, including the working rules and evidence gate in
§ "Programme method".

---

## Executive conclusion

VTMB animation is not an untouched black box, but it is not completely solved either.

The community has decoded enough of the model format to recover skeletons, bind poses, skin weights, ordinary existing bone-animation clips, sequence metadata, and shared animation-library relationships. Crowbar contains a dedicated open-source `SourceModel2531` implementation and can write bone-animation SMD files. Modders can replace and reweight character meshes while retaining a donor model's skeleton, procedural bones, jiggle bones, and existing animation set. The game’s scripting surface for selecting existing animations, gestures, expressions, scripted sequences, choreographed scenes, and lip-sync files is also documented and usable.

What has **not** been publicly demonstrated is a bit-accurate standalone evaluator that reproduces the complete pose produced by `vampire.exe`, including:

- exact clip decompression for every MDL 2531 variation;
- normalized-cycle and frame-boundary behavior;
- sequence selection and blending;
- layered gestures;
- pose parameters;
- root-motion policy;
- procedural and jiggle bones;
- inverse kinematics;
- head and eye control;
- facial flexes and named expressions;
- lip-sync combination;
- final skinning-matrix construction.

Native authoring is a separate problem. The current public Bloodlines SDK still states that it lacks correct multi-bone animation support and a full model compiler/decompiler. DDLullu's MDL Formatter works by preserving or copying animation-related data from an existing donor model; it does not prove that arbitrary new skeletons and multi-bone animations can be compiled into a fully native VTMB character model.

For an Unreal reimplementation, native VTMB compilation is not the immediate goal. The efficient target is:

```text
Decode existing VTMB animation data
        ↓
Reconstruct local and model-space poses
        ↓
Validate those poses against the original runtime
        ↓
Import verified clips and metadata into Unreal
        ↓
Rebuild runtime layering deliberately
```

The best overall strategy is therefore:

1. Treat Crowbar output as a valuable reference, not ground truth.
2. Build a deterministic offline comparison harness before changing the decoder.
3. Capture final bone matrices from the original game as the authoritative runtime oracle.
4. Trace backward into the decompressor only for mismatches that the final-pose comparison cannot explain.
5. Defer facial, procedural, and dialogue-layer reconstruction until ordinary body clips are proven.

---

## Scope

This document covers:

- what the community has actually decoded;
- what remains partial, unverified, or unknown;
- the current usefulness and limitations of relevant tools;
- a ranked reverse-engineering strategy;
- debugger, GPU-boundary, and CPU-matrix capture methods;
- a structured trace and comparison format;
- a Codex-friendly automation workflow;
- an evidence hierarchy and experiment discipline;
- an investigation method for faithful Unreal playback;
- legal, safety, reproducibility, and project-management cautions.

This document does **not** claim to be:

- a complete binary specification for MDL 2531;
- proof that any community decoder matches `vampire.exe`;
- a guide to bypass ownership checks, DRM, or security controls;
- legal advice;
- a promise that every model uses the same animation path.

---

## 1. The three problems that must not be conflated

Many contradictory community statements become consistent once the work is divided into three separate problems.

### 1.1 Existing-animation extraction

Question:

> Can existing VTMB skeleton and animation data be read and converted into a usable interchange representation?

Answer:

> Yes, for ordinary skeletal clips and common models, to a practically useful degree.

Evidence includes Crowbar's dedicated MDL 2531 structures, decompression code, and SMD writer, plus community workflows that export bone-animation SMD files.

### 1.2 Runtime-equivalent evaluation

Question:

> Can a standalone implementation reproduce the exact final pose that the original game renders for the same model, sequence, cycle, layers, and parameters?

Answer:

> Not publicly proven.

SMD extraction normally produces a sampled representation of a clip. It does not automatically reproduce the game's state machine, layer blending, procedural post-processing, IK, root-motion policy, facial system, or final skinning convention.

### 1.3 Native VTMB authoring and round-tripping

Question:

> Can a newly authored skeleton and arbitrary multi-bone animation be compiled back into a native VTMB character model and run reliably in the original game?

Answer:

> Not as a complete, general, publicly documented pipeline.

Replacement-model workflows are real, but they generally preserve a donor skeleton and copy donor animation structures. That is an important accomplishment, but it sidesteps the hardest compiler and runtime-compatibility questions.

### 1.4 Why the distinction matters for Unreal

An Unreal reimplementation primarily needs 1.1 and 1.2:

- read the original user's assets locally;
- reproduce their animation faithfully;
- convert the result into a modern runtime representation.

It does not need to solve 1.3 unless the project also intends to write new animation data for the original game.

---

## 2. Confidence language

Every conclusion, test, and code comment should use one of these statuses.

| Status | Meaning |
|---|---|
| **Verified** | Demonstrated by reproducible code, binary inspection, or a controlled capture from the original game. |
| **Strong evidence** | Supported by multiple independent observations or by a tool plus community reproduction, but not yet validated against the original runtime. |
| **Partial** | Some cases work, but coverage, semantics, or fidelity is incomplete. |
| **Hypothesis** | Plausible interpretation awaiting a controlled experiment. |
| **Unknown** | Evidence is insufficient or contradictory. |

Do not use “solved” to mean merely “a tool produced a file.” A decoded clip is only runtime-verified when its evaluated pose has been compared with the original game under controlled conditions.

---

## 3. Evidence hierarchy

When sources disagree, prefer evidence in this order:

1. **Original `vampire.exe` final rendered result under controlled inputs**
2. **Captured final CPU bone matrices or final GPU skinning data**
3. **Captured output from the original clip decompressor or pose evaluator**
4. **Two genuinely independent decoders that agree**
5. **One decoder plus a final skinned-vertex comparison**
6. **Crowbar's MDL 2531 implementation and emitted SMD**
7. **Bloodlines SDK source, documentation, and reproducible tool behavior**
8. **SourceIO's VTMB structures and importer**
9. **Later Valve Source SDK code used as a structural landmark**
10. **Community tutorials and experienced maintainer comments**
11. **Old forum statements and visual impressions**
12. **Assumptions based on “normal Source behavior”**

The later Valve Source SDK is useful because VTMB shares architectural ancestry with Source. It is not authoritative: VTMB is an earlier, modified branch with a distinct model version and Troika-specific changes.

---

## 4. Current capability matrix

| Area | Public status | Confidence | What still needs proof |
|---|---|---:|---|
| Identify VTMB studio model version `2531` | Verified | High | None for normal `IDST` headers |
| Read bone names and parent hierarchy | Working | High | Full corpus validation |
| Read bind/reference transforms | Working | High | Coordinate and basis conventions across all variants |
| Read skin weights and bone indices | Working | High for common models | Interaction with compressed vertex types |
| Read ordinary sequence descriptors | Working | Medium–high | Semantics of all flags and blends |
| Export ordinary existing bone clips to SMD | Working in Crowbar | High | Runtime equivalence |
| Read animation-only/shared animation models | Practically established | Medium–high | Complete include/link resolution rules |
| Invoke existing sequences in the game | Documented and working | High | Exact internal state transitions |
| Use scripted sequences and choreographed scenes | Documented and working | High | Exact layer-priority behavior |
| Edit LIP/VCD data | Working community workflow | High | Exact runtime facial blend behavior |
| Replace/reweight a mesh on a donor skeleton | Working | High | Coverage of unusual NPC rigs |
| Preserve donor procedural/jiggle bones | Working as data reuse | Medium–high | Independent reproduction of runtime algorithms |
| Extract facial flex/vertex data | Partial/approximate | Medium | Exact encoding and runtime evaluation |
| Decode compressed model vertex types 1 and 2 | Known trouble cases | Medium | Corpus-wide correctness |
| Exact clip timing and loop-boundary behavior | Not proven | Low | Runtime trace |
| Exact root-motion policy | Unknown/partial | Low | Entity plus bone trace |
| Exact sequence blending and animation layers | Blend axes and autolayer recursion decoded; weighted-layer path open | Medium | Runtime state and pose trace |
| Pose parameters | Descriptor, range, wrapping and blend mapping decoded; defaults and interpolation open | Medium–high | Controlled parameter sweeps |
| IK and procedural-bone evaluation | Not publicly complete | Low | Pre/post-stage captures |
| Exact facial expression, eye, and lip blend | Not publicly complete | Low | Dialogue capture |
| Compile arbitrary new multi-bone animation to native VTMB | Not generally supported | High confidence in limitation | A reproducible counterexample |
| Alter the native skeleton hierarchy safely | Not generally supported | High confidence in limitation | Compiler/runtime proof |
| Bit-accurate standalone runtime evaluator | Not found publicly | High confidence in absence of evidence | Direct implementation and validation |

---

## 5. What is known or substantially cracked

### 5.1 The MDL version and dedicated format support

VTMB models use the unusual version value `2531`. This is not merely a user-facing label: both Crowbar and SourceIO register or implement dedicated version-specific paths.

Crowbar includes:

- `SourceModel2531`;
- MDL 2531 header and structure classes;
- bone, attachment, sequence, pose-parameter, and include-model structures;
- compressed animation-value handling;
- quaternion-to-Euler conversion for version 2531;
- bone-animation SMD writing;
- VTA/flex-related code;
- vertex readers for types 0, 1, and 2.

This is strong evidence that the broad binary layout is understood well enough for practical decompilation.

### 5.2 Skeleton hierarchy and reference pose

The following are practically extractable for ordinary character models:

- bone count;
- bone names;
- parent indices;
- reference positions;
- reference rotations;
- position and rotation scales;
- bone flags;
- bone controllers;
- attachments;
- hitboxes;
- skinning weights and bone indices.

Replacement-model workflows could not function without this information. The remaining risk is not whether a hierarchy can be read at all, but whether every transform convention and uncommon record type is interpreted exactly.

### 5.3 Existing ordinary bone-animation clips

Crowbar's MDL 2531 SMD writer:

- iterates animation frames;
- calculates per-bone rotation and position;
- expands compressed animation-value runs;
- writes SMD `time` sections and per-bone transforms.

This establishes that ordinary existing skeletal clips have been decoded to a useful degree. It does **not** establish bit-perfect runtime equivalence.

Important conversion losses or ambiguities may include:

- quaternion-to-Euler conversion;
- Euler order;
- coordinate-system conversion;
- interpolation between stored samples;
- sequence-level blending;
- root-motion metadata;
- procedural post-processing;
- runtime layer composition.

### 5.4 Shared and animation-only model organization

Community documentation describes human-like NPCs sharing common skeleton families and linking to external animation model files. The commonly cited male and female shared libraries include:

```text
models/character/shared/female/npc_allsequences.mdl
models/character/shared/male/npc_allsequences.mdl
```

Those master animation models in turn refer to more specialized libraries. The important architectural conclusion is that animation ownership cannot be inferred from the visible character MDL alone.

A correct importer needs:

1. the visible model;
2. its base skeleton;
3. include/link relationships;
4. external animation-only MDLs;
5. stable bone compatibility checks;
6. sequence-name and activity resolution.

### 5.5 Existing animation control from scripts and map entities

The community has documented multiple ways to select or compose existing animations:

- direct animation selection;
- dispositions;
- gestures and interesting places;
- NPC schedules;
- `scripted_sequence`;
- `logic_choreographed_scene`;
- dynamic props used as animation stand-ins;
- VCD events;
- named facial expressions;
- LIP-driven mouth movement and subtitle timing.

This knowledge is valuable for reconstructing the **inputs** to the animation runtime. It is not a reconstruction of the runtime evaluator itself.

### 5.6 LIP and VCD workflows

The old Mod Developer Guide distinguishes:

- DLG files for dialog flow and text;
- VCD files for sequencing, gestures, animation, and spoken events;
- LIP files for timed mouth movement and some subtitle data.

Community tooling can edit these files, and the Bloodlines SDK exposes LIP/VCD editing. This makes lip-sync data operationally accessible.

What remains unknown is how the final face pose combines:

- base face state;
- named expression;
- phoneme or mouth motion;
- eye motion;
- head look;
- model-specific flex-controller rules.

### 5.7 Replacement meshes with donor animation data

DDLullu's MDL Formatter enables substantially more than old “move existing vertices only” workflows. A custom mesh can be formatted around a chosen donor model.

The documented constraint is critical:

> The donor skeleton cannot be freely altered; its procedural/jiggle bones and included animations are copied or preserved.

Therefore this workflow proves:

- skeleton and animation data can be preserved;
- a new visible mesh can be weighted to a compatible rig;
- existing animations can drive a replacement character.

It does not prove:

- arbitrary hierarchy changes;
- new engine-compatible procedural rules;
- arbitrary new multi-bone clip compilation;
- a complete native model compiler.

---

## 6. What is partial or remains unknown

### 6.1 Coverage of all MDL 2531 variants

A 2025 Crowbar issue from Bloodlines SDK maintainer Psycho-A reports:

- three VTMB model vertex-storage types: 0, 1, and 2;
- types 1 and 2 using compressed vertex data;
- incorrect positions in some Crowbar decompilations;
- a 90-degree orientation problem in some reference and animation SMD output.

That issue is about geometry and orientation, not proof that every animation channel is wrong. It still matters: a correct pose applied to incorrectly decoded vertices or an uncorrected basis will look like a bad animation decoder.

Every test report must therefore separate:

```text
clip decoding
skeleton/reference-pose decoding
mesh/vertex decoding
coordinate conversion
Unreal import/retargeting
```

### 6.2 Exact local-transform decoding

Crowbar has working code, but its source also contains commented alternatives and adaptations from later Source code. That is normal reverse-engineering history, not a defect by itself. It means the implementation should be treated as a strong hypothesis until compared with `vampire.exe`.

Potential failure modes:

- adding the reference rotation twice or not at all;
- wrong quaternion component order;
- quaternion sign normalization differences;
- incorrect conversion to Euler angles;
- wrong Euler rotation order;
- mishandling constant versus animated channels;
- wrong run-length animation-value boundary;
- frame-zero or final-frame off-by-one behavior;
- applying position scale or bias incorrectly.

### 6.3 Timing, sampling, and looping

An exported SMD contains sampled integer frames. The runtime may operate on normalized cycle and interpolate between samples.

**Duration divides by `frames - 1`.** The retail per-cell decoder computes
`floor((numframes - 1) * cycle)` and passes the remainder as the interpolation fraction,
so a clip's last sample sits at cycle `1.0` rather than one step short of it. Owned by
`docs/vtmb/animation_and_movers.md` A.4b.

Unknowns that need measurement:

- how a looping clip interpolates across the end boundary;
- whether cycle `1.0` aliases `0.0`;
- how playback rate and negative rate behave;
- whether sequence changes preserve normalized cycle;
- when animation events fire relative to sample interpolation;
- how save/load restores cycle and layer state.

### 6.4 Root motion

Visible displacement may come from:

- root-bone motion;
- pelvis motion;
- entity movement by AI/navigation;
- sequence movement metadata;
- scripted movement;
- a combination reconciled by the engine.

It is not enough to inspect the root track. A controlled trace must record both the entity transform and the evaluated bone pose.

Required experiment:

```text
same clip, same cycle progression
record entity transform
record root/pelvis model-space transforms
record reported or inferred velocity
record collision/nav movement
compare stationary playback with AI-driven playback
```

### 6.5 Sequence blending and animation layers

During dialogue or AI behavior the final body pose may combine:

- locomotion or idle base;
- transition sequence;
- upper-body gesture;
- additive or override layer;
- look-at/head aim;
- facial expression;
- lip sync;
- procedural bones;
- IK.

Community documentation explains how to request many of these behaviors, but no public, verified VTMB blend graph was found.

Unknowns include:

- number of active layers;
- layer priority;
- additive versus override semantics;
- bone masks;
- transition duration;
- conflict rules;
- gesture fade-in/fade-out;
- sequence interruption;
- whether dialogue systems write pose parameters or dedicated layer state.

### 6.6 Pose parameters

MDL structures expose pose-parameter descriptors, but that does not establish runtime semantics. Name, range, wrapping, normalization and the blend mapping are established — the descriptor
layout and the axis-resolution rule are owned by `docs/vtmb/animation_and_movers.md` A.3,
and a capture witnesses the resolved cell and weight rather than recomputing them.

What a correct reimplementation still needs to know:

- the default a parameter holds before anything writes it;
- interpolation policy between successive values;
- which Python/entity methods drive them.

The efficient method is a controlled parameter sweep while capturing final matrices.

### 6.7 Procedural, axis-interpolated, quaternion-interpolated, and jiggle bones

Crowbar reads relevant flags and data structures, and donor-model workflows preserve procedural and jiggle bones. Preservation is not the same as independent evaluation.

Possible runtime stages include:

- axis interpolation;
- quaternion interpolation;
- aim-at or look-at;
- secondary motion;
- cloth/hair/accessory jiggle;
- attachment correction;
- eye and head behavior.

Ordinary exported clip frames may contain the base bone state before these stages.

### 6.8 Inverse kinematics

Later Source code provides useful concepts and names, but VTMB-specific IK behavior has not been publicly proven.

Questions:

- Which sequences contain IK rules?
- Does VTMB run foot planting in ordinary gameplay?
- Are hand constraints used in dialogue or scripted scenes?
- Is the final palette captured before or after IK?
- Are some apparent procedural errors actually IK differences?

### 6.9 Facial flexes, eyes, and named expressions

This is one of the least certain areas.

Crowbar's author stated in a 2022 community discussion that its decompiled face-flex VTA data for Bloodlines includes approximations or guesses because the storage was not fully understood, with three data-storage forms affecting results.

Community reports are mixed:

- some models produce useful flex data;
- eyes can sometimes be reconstructed by restoring QC information;
- other models lose or corrupt facial behavior;
- named expressions can be model-specific;
- copying/renaming a model may change whether special expressions work.

Therefore:

- VTA output is a lead, not ground truth;
- facial work should have separate fixtures from body-animation work;
- final dialogue captures are required;
- a body-clip milestone must not be blocked on full face reconstruction.

### 6.10 Native multi-bone animation compilation

The current Bloodlines SDK page explicitly lists:

- no correct multi-bone animation support in the editor;
- no full-featured model compiler/decompiler;
- compilation limitations for basic Type 0, single-bone, one-frame prop models.

The SDK's formatter-based character workflow is a practical workaround based on donor data. Until a reproducible toolchain demonstrates arbitrary new clips and altered rigs running correctly in the original game, native character-animation authoring should be considered unsupported.

### 6.11 A complete standalone runtime

No public implementation was found that proves all of the following together:

```text
MDL 2531 parsing
external animation-library resolution
clip decompression
cycle-to-frame evaluation
sequence blending
pose parameters
root-motion policy
layers and bone masks
procedural bones
IK
facial flexes
eyes/head
lip sync
final skinning matrices
```

This is the opportunity: a verified runtime-oracle test suite would turn scattered community knowledge into an executable specification.

---

## 7. Tool and community status

### 7.1 Crowbar

**Current value:** Best public VTMB-specific skeletal-animation reference.

**What it provides:**

- dedicated version-2531 parsing;
- bone-animation SMD export;
- sequence and animation descriptor parsing;
- compressed animation-value expansion;
- skeleton and mesh decompilation;
- include-model and pose-parameter structures;
- partial flex/VTA handling;
- open source suitable for instrumentation.

**Limitations:**

- SMD output is not proven identical to the original runtime;
- converting quaternions to Euler angles can obscure the original representation;
- later runtime layers are outside a decompiler's scope;
- flex output is explicitly approximate in at least some cases;
- a current open issue reports orientation and compressed-vertex problems;
- the latest repository commit inspected for this document identifies as Crowbar 0.74, so behavior should always be pinned by executable hash and source commit.

**Recommended role:**

- baseline exporter;
- readable reference implementation;
- source of candidate structure layouts;
- offline differential oracle;
- never the sole authority for a disputed runtime pose.

### 7.2 Bloodlines SDK

**Current value:** Central community tool collection and documentation source.

**What it provides:**

- game-content extraction;
- model, map, texture, LIP, and VCD workflows;
- integrated Crowbar and MDL Formatter tooling;
- community-maintained VTMB-specific format knowledge;
- explicit documentation of current limitations.

**Limitations:**

- no correct general multi-bone animation authoring;
- no complete native model compiler/decompiler;
- older toolchain and environment constraints;
- some format code is acknowledged as incomplete;
- editor support is not equivalent to game-runtime behavior.

**Recommended role:**

- reproducible asset discovery and extraction;
- toolchain cross-check;
- source for known limitations;
- reference for community naming and workflows.

### 7.3 DDLullu's MDL Formatter

**Current value:** Proven path for custom visible meshes on an existing VTMB-compatible animation container.

**Strengths:**

- custom mesh replacement;
- reuse of a donor skeleton;
- preservation/copying of donor animations;
- preservation of procedural and jiggle bones;
- practical character-model modding.

**Limitation that must be kept visible:**

> It works by compatibility and data inheritance. It does not establish a general new-rig/new-animation compiler.

### 7.4 SourceIO

**Current value:** Useful independent parser and structural reference for MDL 2531 models, but not currently a VTMB animation oracle.

At commit `25b3978e366aeed1b4bdcf078394751b2d376c7a`, SourceIO:

- registers an `IDST`, version `2531` model importer;
- constructs a VTMB-specific model object;
- imports model, material, and optional physics data;
- contains a VTMB animation-import function whose body immediately returns;
- does not call a functioning VTMB animation import from the registered importer path.

This corrects an easy overstatement: SourceIO has explicit VTMB model support, but its current MDL 2531 Blender path does not provide a working animation reference.

**Recommended role:**

- independent structure-layout comparison;
- skeleton/mesh cross-check;
- possible future location for a second decoder;
- not a current frame-by-frame animation oracle.

### 7.5 Valve Source SDK 2013

**Current value:** Architectural vocabulary and structural landmarks.

Useful concepts include:

- studio headers;
- sequence and animation descriptors;
- compressed animation-value runs;
- bone setup;
- matrix conventions;
- pose parameters;
- procedural bones;
- IK;
- model inclusion.

**Caution:** VTMB predates this branch and contains Troika modifications. Names and broad algorithms can guide reverse engineering, but layouts, flags, initialization, and runtime policy must be confirmed against VTMB.

### 7.6 Mod Developer Guide and community discussions

**Current value:**

- shared animation-library paths;
- Python/entity animation methods;
- VCD/LIP relationships;
- named expressions;
- scripted-sequence fields;
- choreography behavior;
- historical workarounds.

**Caution:**

- it is old;
- it mixes verified behavior with author interpretation;
- some historical impossibilities became possible through later tooling;
- community claims should be converted into experiments, not copied into code as facts.

### 7.7 x32dbg/x64dbg

VTMB is a 32-bit process, so x32dbg is the appropriate debugger frontend.

Useful capabilities:

- software breakpoints;
- hardware breakpoints;
- memory breakpoints;
- conditional logging;
- hit counters;
- call-stack inspection;
- tracing from a final buffer write back to its producer.

Use the debugger to identify and validate the first hook. Move repetitive capture into a command-line probe after the target is understood.

### 7.8 MinHook

MinHook is a small x86/x64 Windows API hooking library and remains a useful
reference for detour behavior. The project already has one shared
instruction-aware backend, so replacing it is justified only by a concrete
retail target that the existing backend cannot relocate or manage safely.

Use it after debugger validation, not as the initial discovery mechanism.

### 7.9 RenderDoc

Do not plan around native RenderDoc capture for VTMB's Direct3D 9 path. RenderDoc's published API table lists D3D9 and D3D10 as unsupported.

For the GPU-boundary experiment, prefer:

- a focused `IDirect3DDevice9` proxy or vtable hook;
- a compatible D3D9 tracer;
- direct logging of shader-constant calls and draw calls.

---

## 8. Ranked reverse-engineering methods

Ranking criterion:

> Expected reduction in uncertainty per engineering hour, with ground-truth quality considered more important than theoretical completeness.

| Rank | Method | Payoff | Difficulty | Primary question answered |
|---:|---|---:|---:|---|
| 1 | Offline own-decoder vs Crowbar SMD differential harness | Very high | Low–medium | Where do ordinary clips first diverge? |
| 2 | Final CPU bone-matrix capture from `vampire.exe` | Definitive | Medium–high | What pose did the original runtime actually produce? |
| 3 | D3D9 shader-constant and draw-call trace | High | Medium | Where is the rendered palette and who submitted it? |
| 4 | Hook the original clip decompressor/evaluator | Definitive for raw channels | High | How are specific bytes decoded? |
| 5 | Runtime animation-state capture | Very high | Medium–high | Which sequence, cycle, layers, and parameters produced the pose? |
| 6 | Final skinned-vertex comparison | High | Medium | Are matrix order, bind pose, and transposition correct? |
| 7 | Controlled parameter and layer sweeps | High | Medium | How do pose parameters and dialogue layers combine? |
| 8 | Synthetic or minimally patched animations | Very high if feasible | High | What does one channel/flag mean? |
| 9 | Corpus-wide structural inference and property tests | Medium–high | Medium | Which layouts and flags are globally consistent? |
| 10 | Multi-angle video/pose observation | Low precision | Medium | Is the failure gross timing, handedness, or root motion? |

### 8.1 Why offline comparison ranks first

It is cheap, deterministic, and immediately separates:

- parser failures;
- channel-decompression failures;
- coordinate conversion;
- frame indexing;
- Unreal-only problems.

It also provides infrastructure needed for every later experiment.

### 8.2 Why final CPU matrices outrank deep disassembly

The final palette bypasses the need to understand every intermediate stage immediately. It answers the most important binary question:

> Does the reconstructed final pose match the original?

Once the first mismatching bone and frame are known, disassembly becomes targeted.

### 8.3 Why the GPU boundary is a locator, not necessarily the final oracle

GPU constants can reveal the palette that was rendered, but may already contain:

- inverse-bind multiplication;
- model/world transforms;
- transposition;
- shader-specific packing;
- only a subset of bones.

The GPU trace is excellent for finding the CPU caller and validating final skinning. A CPU pose capture with entity metadata is easier to interpret.

### 8.4 Why synthetic files rank lower

Single-variable clips are ideal scientifically, but VTMB's incomplete native compiler makes creating valid synthetic MDL 2531 animation payloads difficult. Use them after a working corpus and patcher exist.

---

## 9. Recommended runtime-capture strategy

### 9.1 Principle

Do not begin by dumping the whole process or reverse engineering the whole animation engine.

Start at an observable boundary:

```text
animation state
    ↓
clip decode and blending
    ↓
procedural/IK stages
    ↓
final CPU pose
    ↓
skinning-palette conversion
    ↓
D3D9 constant upload
    ↓
animated draw
```

Work backward only as far as necessary.

### 9.2 Stage A — establish one controlled scene

Choose:

- one ordinary humanoid;
- one simple visible body clip;
- a fixed map and camera;
- a stable entity position and orientation;
- no combat;
- no dialogue;
- no visible gesture or look-at;
- a repeatable way to select the sequence.

Record:

- game executable hash;
- relevant DLL hashes;
- patch/mod configuration;
- model path and hashes;
- map;
- sequence name/index;
- expected duration;
- launch command and settings.

Do not start with Jeanette/Therese dialogue, combat, or a model with elaborate hair/facial behavior.

The first controlled case is also the seed for breadth. After one player clip
has clean live-to-authored alignment, enumerate every raw sequence descriptor,
animation descriptor, and blend-grid entry resolved through the installed player
models' include graphs. The `local_sequences` convenience view is not a coverage
oracle because it deduplicates labels and selects only blend cell `[0][0]`.
Replay each unambiguously addressable identity through the same unattended
recipe. Deduplicate identical data across clan and armor targets, but retain
duplicate names that resolve to different bytes. Every raw inventory row
receives a validated capture or exact failure evidence; unsupported selection,
interruption, parameterization, and ambiguity are inputs to the next focused
experiment rather than silent coverage gaps.

For each clean capture, resolve the exact patch-first owner, source sequence and
animation bytes, and exported animation; evaluate the project decoder at the
captured times; normalize entity/root placement; and compare local transforms,
composed matrices, and the final skin palette per frame and bone. Mismatch
clusters can suggest timing, flag, missing-channel, blend, remap, or hierarchy
rules, but a rule is confirmed only when it predicts held-out captures or a
focused repeat. This breadth pass does not require decoding every animation rule
first and does not pretend a finite list of base clips exhausts a continuous
pose-parameter or layered state space.

### 9.3 Stage B — locate large vertex-shader constant uploads

`IDirect3DDevice9::SetVertexShaderConstantF` receives:

```cpp
HRESULT SetVertexShaderConstantF(
    UINT StartRegister,
    const float* pConstantData,
    UINT Vector4fCount
);
```

A 3×4 matrix commonly occupies three `float4` registers. A 50-bone palette would therefore require approximately 150 vectors if every bone were uploaded in that form, though real engines may upload fewer bones, partition draws, or use a different layout.

Log, for every candidate call:

- frame number;
- timestamp;
- caller return address;
- module-relative caller RVA;
- `StartRegister`;
- `Vector4fCount`;
- a bounded copy of the float data;
- current vertex shader identity if obtainable;
- the immediately following draw-call metadata.

Filter candidates by:

- sufficiently large vector count;
- repeating groups compatible with affine transforms;
- temporal smoothness;
- proximity to a character draw;
- stable caller stack.

Do not assume that the first large upload is bones. Camera matrices, lighting constants, and other state can also be large or matrix-like.

### 9.4 Stage C — score candidate matrix palettes

For each group of 12 floats interpreted as a 3×4 affine transform, calculate:

```text
row lengths near 1
pairwise row dot products near 0
determinant magnitude near 1
finite translation values
smooth change across adjacent frames
consistent block count
```

Possible classifications:

- rotation plus translation;
- transposed rotation plus translation;
- model-space bone transforms;
- world-space bone transforms;
- skinning matrices (`pose × inverse_bind` or equivalent);
- unrelated shader data.

### 9.5 Stage D — trace the CPU caller

Once a candidate upload is correlated with the character draw:

1. Inspect the call stack.
2. Find the highest useful caller inside the material/studio-render/game modules.
3. Identify the CPU source pointer.
4. Set a hardware write breakpoint on one distinctive matrix component.
5. Resume the controlled animation.
6. Stop at the writer.
7. Repeat until the final pose-construction function is identified.

The goal is a stable hook boundary with arguments resembling some subset of:

```text
renderable/entity pointer
model or studio-header pointer
output matrix pointer
bone count or mask
current time/cycle
sequence/layer state
```

Function names such as `SetupBones` or `BuildTransformations` are conceptual landmarks, not assumed VTMB symbols.

### 9.6 Stage E — capture final CPU matrices

The first persistent probe should capture the final pose after the original
function returns. Its record is raw and self-bounded, not a decoded model of
what the structure is believed to mean.

Minimum evidence:

- exact executable and module hashes plus the hook's module-relative address;
- entry or exit, QPC, thread ID, record length, and sequence number;
- the x86 register snapshot and a bounded stack window;
- each requested memory span's original address, requested length, copied
  length, status, and unmodified bytes;
- the output matrix pointer and a bounded copy large enough for the observed
  bone count;
- drop, truncation, unreadable-span, and incomplete-tail accounting.

The capture recipe records how registers, stack slots, and pointer expressions
selected those spans. Optional labels such as entity, sequence, bone count, or
matrix space are analyzer hypotheses and may change without migrating old
captures. If names or sequence metadata are not known, retain raw pointers,
module-relative addresses, and neighboring unknown bytes. Never fabricate
semantic fields or discard unexplained flags and padding.

### 9.7 Stage F — determine matrix space

Run controlled transformations.

**Move the entity without changing the animation**

- If every bone changes by the entity transform, matrices are probably world-space or final skinning matrices containing world placement.
- If the pose remains unchanged, matrices are probably model-space.

**Rotate the entity**

- Observe whether every bone receives the same outer rotation.

**Freeze animation time**

- The pose should remain stable while the camera moves.

**Compare with reference pose and inverse bind**

Test candidate formulas:

```text
model_pose
model_pose × inverse_bind
inverse_bind × model_pose
entity_world × model_pose
model_pose × entity_world
transpose variants where justified
```

Reject a formula based on numeric error over many bones, not one visual guess.

### 9.8 Stage G — correlate runtime pose with offline output

For the same model, sequence, and cycle:

1. Evaluate the project's decoder.
2. Evaluate Crowbar's sampled SMD.
3. Convert both into the candidate captured space.
4. Compare every bone.

Report:

- local translation error;
- local rotation-angle error;
- model-space translation error;
- model-space rotation-angle error;
- final-matrix Frobenius norm;
- first mismatching bone in hierarchy order;
- worst bone;
- whether descendants inherit the first mismatch.

### 9.9 Stage H — trace backward only for the first unexplained mismatch

If a final-pose mismatch remains:

- identify the first bad bone and frame;
- determine whether its parent already differs;
- locate the original decompressed local transform;
- hook the lowest function that converts compressed bytes to position/quaternion;
- record input bytes, offsets, flags, scale/bias, output vector/quaternion;
- feed the same data to the project decoder;
- add a regression test before changing production code.

This is far more efficient than reading the entire animation disassembly without labeled runtime values.

### 9.10 Persistent hook design

After debugger validation:

- build the probe as 32-bit;
- use module RVA plus signature verification, not a fixed absolute address;
- verify expected instruction bytes before enabling a hook;
- record module hashes;
- preserve the exact calling convention;
- copy capture data into probe-owned memory immediately;
- avoid allocations, blocking I/O, or locks in the render/animation callback;
- use a lock-free or bounded queue to a writer thread;
- fail closed when signatures do not match;
- never continue with an “almost matching” signature;
- record the exact capture recipe and tool commit with the session;
- keep durable records length-delimited and recoverable after a partial tail;
- let writer, reader, and recipe evolve together without a compatibility
  surface.

Use the existing shared hook backend while it handles the validated boundary.
Replace it only when a concrete retail target demonstrates a relocation or
lifecycle failure.

### 9.11 Joining the draw and skeletal streams

The armed hooks express actor identity in two different terms. The draw hook on
`CStudioRender::DrawModel` records the field the render info carries at `+0x18`.
The skeletal hooks on `resolve_virtual_model_pose` and
`C_BaseAnimating::BuildTransformations` record their own `this`. The two sets
never share a value, so a naive join finds nothing.

They are nevertheless **one pointer space** (Verified). The render-info field is
the `C_BaseAnimating` instance pointer **plus 4**: the draw side holds an
interface subobject four bytes into the instance, and subtracting four recovers
the instance. The `studio_hdr` pointers coincide across the streams already,
because both read the same model-cache header.

A third term settles which subobject that is (Verified). The engine reaches
`C_BaseAnimating::SetupBones` through `IClientRenderable` slot `+0x3c`, so the
`this` it receives is the interface subobject rather than the instance, and the
function adjusts down to the instance itself before using it. That adjustment is
visible in the prologue, and the captured value matches the draw field exactly
on every draw whose frame built a pose. The relation is therefore one object
seen from three places: the renderable interface, the draw record's entity
field, and `SetupBones` all carry the same address, while the skeletal
evaluators carry that address minus four.

Establish a relation like this from **identity, never from time**. A per-frame
stream makes almost any pairing look plausible in time, so a timestamp
correlation cannot distinguish a real relation from a coincidence. The method
that does:

- group both streams by model checksum and keep the checksums present in both;
- take the pairwise differences within each group and keep only the differences
  that hold across *every* shared group;
- require the surviving difference to resolve *every* skeletal instance to a
  drawn entity;
- repeat over independent runs, and check that the runs do not share instance
  addresses — a difference that repeats while addresses do not is a fixed offset
  in the object, whereas one that repeats because the heap did is nothing.

Three complete `sp_theatre` captures each yield the same −4 across 39 shared
models, resolving all 49 skeletal instances, while sharing zero instance
addresses between runs. Address alignment corroborates it with no pairing at
all: every draw-side value sits four above an eight-aligned address, and every
skeletal value is eight-aligned. No `DrawModel` argument and neither the
render-info nor the studio-header pointer carries the instance pointer, so the
+4 relation is the only route between the streams.

Two bounds travel with the join, and both are properties of the engine rather
than of the capture:

- **A skeletal actor need not be drawn under the model it animates under.** One
  theatre doppelganger animates two instances under its own model and draws the
  second under a different one, so a per-model bijection fails where the global
  pointer relation holds. Coverage is the test; per-model symmetry is not.
- **An instance address can serve more than one model inside a single run.** Ten
  addresses do so per theatre run, so a raw-address join is valid only inside an
  address's lifetime. Scoped construction/destruction records and a generation
  identity are what make it safe across reuse; see §9.10.

### 9.12 Grouping records by pose build

A pointer relation says which records concern the same actor. It does not say
which of that actor's records belong to the *same* construction of its pose, and
one final pose can hide several contributors. The grouping key is a generation:
a counter allocated when a bracketed frame is entered, held on a per-thread
stack, and stamped on every record produced inside it.

Three frames are bracketed: the client's `SetupBones`, and both engine frames
that submit a studio draw. A draw frame and the pose build inside it are
**siblings rather than parent and child** (Verified). An engine draw frame
obtains the bone buffer, calls the client's `SetupBones` through the renderable
interface, and only then submits the draw, so the pose build has already closed
by the time `CStudioRender::DrawModel` runs. Decoded locals and composed locals
nest inside the pose build exactly; a draw does not nest inside it at all, and
must be related to it rather than contained by it.

Bracketing one draw frame is not enough. Two engine frames submit studio draws —
`CModelRender::DrawModel` and `CModelRender::DrawModelShadow` — and they are
siblings, so the same actor is drawn on both within a frame. The two are
enumerated exhaustively in `animation_and_movers.md` → "Exactly two engine frames
submit a studio draw"; that the set is closed is what lets an unenclosed draw be
read as an instrument fault rather than an unknown path.

What one complete `sp_theatre` capture establishes:

- **Every record carries a generation.** All 1,693,202 of them, with none
  unassigned, across 820,827 generations.
- **A pose build covers exactly one entity.** Across 419,700 pose builds, no
  generation spans several entities and none carries an evaluation naming
  another entity, so a composed pose and its contributing evaluations share one
  generation.
- **Brackets are well formed.** No record names a missing bracket, no bracket
  has a missing parent, no span is inverted, no evaluation falls outside its
  bracket's span, and none crosses a thread.
- **Both engine frames carry real traffic.** 236,041 ordinary draw frames against
  165,086 shadow frames.
- **`SetupBones` runs far more often than it builds a pose.** 181,751 of 419,700
  calls produce no evaluation at all, which is consistent with a bone cache
  satisfying a second call for an actor already posed this frame.
- **A draw frame does not always build a pose.** 146,247 of 395,785 draws are
  submitted by a frame that produced no pose build, for the same reason. Such a
  draw is reported as having no pose build rather than being attributed to an
  earlier one.

Attribution is confirmed twice over: the enclosing generation and the
independently carried instance identity agree on all 249,538 draws whose frame
built a pose, with no disagreement. Two independently derived attributions that
never contradict each other is what makes the grouping evidence rather than
convention; a single derivation could not detect its own failure.

The instrument states the same result before any query runs — the hook's own
counters report zero unbracketed records, zero bracket overflows and zero drops —
and the byte-closure self-check reproduces both stream sizes exactly from record
kind and bone count, with a dense global sequence.

Bracketing is not free. Bracket records are 48.5% of all records for roughly 1.4%
of the bytes, and the drain queue's high-water mark rises from 82 to 178 against
a mean of 8.3 MB/s (baseline 8.1) and a peak second of 22.1 MB (baseline 21.4).
Nothing was dropped, but the queue is the headroom that would go first.

### 9.13 Censusing the models and skeletons a run used

A grouped record still names its model only by a pointer, a checksum and a
64-byte name. Nothing about the bones behind that pointer survives the run, so a
decoded pose can be compared against ours while the bytes it was decoded *from*
remain unavailable. The census closes that: once per distinct studio header it
records the header's identity and, once per checksum, the model image the header
sits at the front of.

The split is the method's whole content. **Identity is per sighting; bytes are
per checksum.** A model loaded at two addresses is two observations and one
image, so the same immutable bytes are never stored twice, and a per-call record
keeps only a pointer and offsets. Storing the image rather than a header prefix
is what makes the rest possible: every `*Index` in the header is an offset from
the header base, so a prefix retains pointers into bytes that were discarded,
while the image makes a captured runtime pointer resolvable by subtraction.

Nothing is decoded in the process. The probe validates `IDST`/2531 and a
plausible `Length`, then copies; bones are read offline by the same decoder the
export uses, where a wrong field hypothesis costs a re-run of a script rather
than a re-run of the game.

Three observation reasons are distinguished, and the third is a boundary worth
stating plainly. **First** is a header this run has not seen at this address.
**Replacement** is an address that served one checksum and now serves another —
the only free this capture can witness. **Resident at stop** is a sweep that
re-reads every recorded header while the game still runs. There is no unload
event: no hooked target sees a model-cache free, and no case specification
declares one, so the capture reports residency and counts headers that no longer
read as their own header rather than presenting either as an unload. A real
unload event needs a Ghidra pass on the model cache first.

Two checks make the census falsifiable rather than merely self-consistent.
Offline, each captured image must decode to a skeleton whose composed bind
transform and stored `poseToBone` return the identity. Against the install, each
image is compared byte for byte with the patch-first file carrying the same
checksum; ranges that differ are what the loader fixes up in place, and those are
exactly the ranges a pointer-to-offset conversion may not treat as
image-relative. A checksum that resolves to no installed file is reported as
unresolved, never as a mismatch — the two would otherwise be indistinguishable on
a machine without the install.

### 9.14 Bounding an actor's identity and lifetime

A censused model says what bytes a pose came from. It does not say how long the
pointer that named the actor meant *that* actor. Ten entity addresses serve more
than one model inside a single run, so an address join is only valid inside a
window nothing was recording.

Two windows are recorded, and they catch different failures.

The **identity interval** opens where an address first evaluates a model and
closes where the same address evaluates a different one. It needs no new hook:
the skeletal evaluators already hold the entity, the studio header and the
checksum, so an actor observation is one hash, one interlocked compare and a
return on a path already copying bone matrices. It catches an address that
changed model under a join.

The **witnessed lifetime** opens at `C_BaseEntity::C_BaseEntity` and closes at
`C_BaseEntity::~C_BaseEntity`. It catches what the interval cannot: an address
freed and rebuilt into a *different actor under the same model*, where the
checksum never changes and only the destructor separates the two.

Three rules make the pair cheap and honest:

- **Read nothing off a half-built or dying object.** A constructor has not given
  the entity a model and a destructor is taking it away, so a lifetime record
  keeps an address, a generation and a time, and nothing else. Identity is
  snapshotted later, from the evaluator, where the object is whole.
- **Filter offline, not live.** Construction fires for every client entity
  because the hook is the shared base; narrowing it to skeletal actors is a join
  against the actor census, and the entities that were constructed but never
  posed are counted rather than hidden. Destruction is cheap to filter live,
  because by then the census either published that address or did not.
- **Record both addresses, never one and an offset.** The observation stores the
  entity and the renderable subobject as separately observed values, so whether
  they differ by four stays a measurement rather than an assumption the capture
  was supposed to test.

What one complete `sp_theatre` capture establishes:

- **Every actor is identified before it is used.** 54 identities across 49
  addresses, none unobserved and none observed late. The 49 is the same count
  CAP1.2 and CAP1.3 measured for skeletal client entities, reached independently.
- **Every reuse is witnessed.** Five addresses serve more than one model and five
  identity changes account for all five, so no address changed actor unseen.
- **Nothing is attributed outside its window.** No evaluation record falls
  outside the interval in which its address meant the model it names, nor outside
  a witnessed lifetime of that address.
- **The lifetime catches what the interval cannot.** 615 constructions and 49
  destructions, of which three addresses were rebuilt into a new actor — invisible
  to a checksum comparison.
- **The `+4` holds a fourth way.** The renderable sits a constant four bytes above
  the entity across all 49, measured from two independently recorded addresses,
  and the constructors declare the same layout statically.

The cost is negligible and was measured rather than estimated: 0.089 MB of actor
records against a 3.5 GB database, at an unchanged mean throughput. The
construction hook's volume was the one real risk, and one run answered it.

Two populations must not be conflated in a report like this. The addresses an
identity-bearing observation names are actors; the addresses only ever seen
constructed are ordinary client entities. Counting them together turns 49 actors
into 611.

A reuse rule stated as "every extra identity at a reused address has an identity
change" is too narrow, and a later cutscene with more churn shows why. An address
may instead be destroyed and rebuilt as a different actor, which the census
records as construct/first/destruct twice. Both are witnessed separations; a
check that accepts only the first faults a census that recorded the second
exactly.

### 9.15 Attributing a contribution to the bytes it was decoded from

A pose that matches proves nothing unless the source it came from is named, and
the sequence index an entity carries is not that name. A character resolves its
clips through include models, so the index is meaningful only beside the header
it was resolved against.

Capture the frames below the virtual-model resolver rather than the resolver
itself. They receive the owning `studiohdr` as argument zero, so the owner is
witnessed rather than inferred; capturing higher up yields a target-local index
that joins to nothing. Three frames are enough: the sequence evaluator for
identity, cycle, pose parameters and the selected-bone mask; the per-cell decoder
for the animation descriptor of each cell that actually fired; and the blend-axis
resolver, which emits no record of its own but stashes its result for the
sequence frame to fold in, so witnessed weights cost no record volume.

Four rules keep the evidence honest.

**Store identity, not bytes.** The model census already holds the whole owner
image and the runtime header is that image at offset 0, so a descriptor pointer
is stored raw and converted to a file offset offline. Only live state no image
carries — the pose parameters — travels on the record.

**Observe the owner from the contribution path.** A bank model is nobody's entity
model, so an earlier census keyed on entity models never sees it. Half the owners
in a theatre run are in that population; without observing them the attribution
joins to an image that was never captured.

**Scope before the frame runs.** The cells a sequence decodes reach the queue
while the sequence is still on the stack, so the scope is opened before the
original call and carried by the nested records, exactly as the pose-build
generation is.

**Refuse a foreign stash.** The blend resolver has callers other than the one
being captured, so the stashed axis is keyed by descriptor pointer and a
mismatch is recorded as a fault rather than read.

The check that makes this evidence rather than convention is arithmetic the
capture cannot fake: a captured descriptor pointer minus the owner's header base
must equal the declared array index times the declared stride. Two complete
cutscenes satisfy it on 961,518 of 961,518 contributions, which verifies the
displacements, the strides and the owner attribution together.

Measured cost, per complete cutscene: 89 MB of contribution records against a
4.2 GB database — 3.4% — at 8.60 MB/s mean against a baseline of 8.42, with the
writer queue peaking at 239 and 357 against the 368 the actor census already
reached. The records are numerous and small, so they cost record count rather
than volume.

Two bounds travel with a run like this. An idle prefix fires no multi-blend
sequence at all, so a short probe does not exercise the blend path and must not be
read as covering it. And the caller of a contribution is an address: resolving
which builder asked for it is a lookup against the case specification by nearest
preceding seed, so an address inside an unlisted function is reported unresolved
rather than attributed to the seed below it.

### 9.15a Establishing which bytes an evaluation dereferenced

A pose that matches proves nothing about a field the decoder silently skipped, so the
byte ranges retail read are first-class evidence rather than a by-product. The method
that produces them without the answer being circular has three parts, and the split is
the point of it.

**The probe witnesses and decodes nothing.** Hooks on the two channel decoders emit no
record at all: each folds one bone into a per-thread accumulator that the enclosing cell
frame reads back and stamps onto its own record — the trade the blend resolver already
makes, and the reason a witnessed per-bone decode costs two bitmaps rather than a record
each. What travels is what the decoder was handed: the first animation record and bone
it saw, the frame and fraction it was given, and one bit per bone it ran for. No format
field is interpreted on the live path.

**A separate walker derives the same spans from the image**, transcribing the confirmed
decompilation and importing nothing from the exporter's own decoder. That independence
is not fastidiousness: CAP4.2 compares the ranges retail read against the ranges we
read, and one walker producing both sides would make the difference a tautology.

**The check is what makes the two evidence.** The accumulator indexes its bitmaps off
the pointer it latched, so which bone that was is recorded nowhere — bit zero means "the
first one". The verification therefore solves for that bone independently from each of
the two pointers, `(record base − studiohdr − animdesc − animindex) / 32` and
`(bone base − studiohdr − BoneIndex) / 160`, and requires both to divide exactly, land
inside the owner's bone count, and agree. A single-sided check would accept a pointer
that merely happened to be on the stride.

Two complete cutscenes place **231,747 of 231,747** and **231,649 of 231,649** such
pairs, so the animindex indirection, both strides and both base fields are verified
together. The same runs confirm `floor((numframes − 1) × cycle)` on every witnessed
frame, and show every decoded-bone set equal to the mask of its enclosing sequence.

**Storage is bounded by the images, not the evidence.** Keying a span set by its frame
looks natural and is wrong at scale: a cutscene samples one clip at thousands of frames
while only the track walk depends on the frame, so 122 genuinely distinct shapes become
143,610 and a 4 GB capture becomes 13 GB. Sets are keyed without the frame; the
per-model union is accumulated as one bit per byte of each image, which makes merging an
OR and double-counting impossible; and a cell's exact spans regenerate from its shape
and its frame on demand. What is stored is the union, the shapes and the link — never
the same bytes a thousand times.

Three bounds travel with a report like this. The walk *inside* a track is transcribed
rather than witnessed: the run-skipping and key-selection rules come from the
decompilation and are checked only for staying inside the image and terminating, while
the pointers above them are what a capture can refute. The sequence descriptor is only
partly claimed, so its byte totals are a floor and the unclaimed remainder is recorded
as unknown rather than inert. And a cross-run comparison must not assert that two runs
consume the same bytes: which frames a run samples depends on where the operator reached
the trigger, so two correct runs differ there. The claim that holds is that one shape
produces one answer — checked by digest, and true of all 176 shapes the two runs share.

### 9.15b Attributing a pose group to the request that caused it

A pose group says which sequence an entity evaluated, never who asked for it. The request
side is in the server game DLL, so answering it means observing a fifth module and joining
its records to the client's.

**Hook the frame that fires per transition, not the one that runs per frame.** A scene's
processor classifies every event of every playing scene on every frame; at twelve
concurrent scenes of roughly forty events over a five-minute cutscene that is on the order
of four million classifications. The start-dispatch below it fires once per event
transition instead, and because the map's scene class and the dialogue scene class reach
the same body, one inline observation point covers both populations. The same trap sits
one level down: the actor resolver looks like a per-scene binding and is really a
per-frame one, because a scene that owns its cast's transforms re-pins and re-resolves
them every frame. Measured, that is 281,353 resolutions against 40 animation-set
applications in one run. Record the ones an enclosing request scope covers and count the
rest; two runs reproduced the uncovered count exactly, which is what makes it a property
of the content rather than of the run.

**Scope requests the way pose builds are scoped, but on a separate stack.** A request
frame opens a scope before it runs, because the work it causes reaches the queue while it
is still live. That stack must be distinct from the pose-build stack: the scene system
runs outside every pose-build bracket, so a request record asking for the enclosing
generation would count itself unbracketed and move a total the grouping work measured at
zero. Read the enclosing generation without counting, and record its absence as a fault of
the request's own.

**Join by an identity both modules independently carry.** An entity's handle packs an
index and a serial, both modules store one, and the encoding is shared — so recording the
raw handle on each side and deriving the index offline joins a server request to a client
pose group without either side deriving the other. Three checks make that evidence rather
than convention: an index must name one entity per side inside a witnessed lifetime; the
renderable-to-entity offset must reproduce from this population, which is one the join
work never measured; and every actor a scene bound must have evaluated under the animation
set that scene applied. The last is the decisive one, because its four values come from
four different observation points and none is computed from another.

Three things will look like disagreements and are not. The animation set names the clip's
owner, so a scene actor keeps its own character model and the match is against the
evaluator's owning header, not the drawn model. The two names are rooted differently — a
scene keyvalue is authored from the game root and a runtime studio header's name is
relative to the model directory — so a comparison has to normalise before it can match. And
an evaluation is scoped by a bracket entered on the renderable subobject, so a lookup keyed
by the entity address finds nothing; use the renderable the census recorded rather than
adding the offset and calling that evidence.

**What a capture of the request side cannot claim.** The client change is an effect: the
sequence index is networked, so a scene asks on the server and the client acts on it some
frames later. The honest statement is that a scene asked and an entity changed inside its
interval, never that one call caused the other. And an activity change is only observable
as the sequence change it produces, because the selection data is recovered while the
selector is not.

### 9.15c Joining a run's identities to the sources offline

A capture that only describes itself cannot say what an offline decoder gets wrong. The join
that turns it outward runs entirely offline and answers one question per fired identity:
which installed bytes is this, what did our export make of them, and what does an
independently produced descriptor inventory say about the same span.

**Resolve on checksum, never on the name.** A runtime studio header names its model in a
128-byte field that a draw record truncates to 64, and shipped names vary in case, separator
and leading slash. The path is therefore a lookup key that must be normalized, and identity
is `Checksum`@8 of the file the lookup returned. A name that resolves to a file carrying a
different checksum is an unresolved identity, not a match.

**The predicate has to be answered by bytes the probe never saw.** Checking that a captured
descriptor pointer's displacement equals `LocalSeqIndex + index × 764` — or
`LocalAnimIndex + index × 72` — against the *captured image* verifies the arithmetic and
nothing about the source. Reading `LocalSeqIndex`, `NumLocalSeq` and their animation
counterparts out of the **installed file** and answering the same predicate is a second
witness produced by a different mechanism, so the two agreeing is evidence rather than one
reader agreeing with itself. Comparing the descriptor bytes at that position between the two
copies is the content half of the same check: a position check alone would pass on different
content of the same shape. One cutscene places all 98 of its identities, with 58 descriptors
byte-identical and 40 differing only in `StudioSeqDesc`+0xc, the loader-written activity
dword `mdl_v2531.md` names — and none differing elsewhere.

**A third producer is worth more than a third check.** Where a descriptor inventory built for
an unrelated question covers the same identity, its digest over its own recorded span must
equal the digest of that span in the installed file. That agreement crosses tools, seeds and
purposes, which is the kind a self-consistent reader cannot manufacture.

**Coverage is two numbers, and the denominator is the corpus.** Distinct identities and fired
records answer different questions: one cutscene's 34 owners and 98 identities carry 481,683
contributions, and a one-record identity is one row beside a thirty-thousand-record one.
Beside them belongs what the run did *not* reach — 45 of the 2,046 sequences its own owners
declare — because a coverage claim without that denominator reads as though the run exercised
the model.

**Measure the decoder under test; do not repair it in the same pass.** Our exporter keys
clips by label, keeps the first sequence to claim a lowercased one, skips a sequence whose
base blend cell falls outside `NumLocalAnims`, and bakes cell `[0][0]` alone. Replaying those
rules over the installed image with the index preserved attributes each missing clip to the
rule that dropped it, so the output is a ranked list of what is missing rather than an
unexplained absence. Changing the exporter belongs to the task the list authorizes.

**Separate what would invalidate a comparison from what a comparison found.** An identity
reaching no installed bytes, or landing outside the array the installed header declares,
means every later stage would read the wrong bytes and is a defect. An identity our export or
an inventory cannot name is the finding. Folding the second into the first makes a known
bound read as a fault on every run; folding the first into the second lets a broken join pass.
A count belonging to neither category is itself a defect, because a population no declaration
covers must not quietly become coverage.

### 9.15d Differencing a decoder's byte coverage against a capture

A pose that matches proves nothing about a field the decoder silently skipped, so the
question is which bytes the runtime read that an offline decoder does not. Answering it
takes three sets — read by retail alone, read offline alone, read by neither — and the
method is mostly about keeping them from being the same measurement twice.

**Measure the decoder; do not describe it.** The retail side is a transcription of the
decompilation, so writing a second walk from the same understanding would produce a
difference that agrees with itself by construction. The offline side therefore runs the
real decoder unmodified and observes it. Observation costs nothing structural when every
read goes through a module-global `struct` and a small number of raw-indexing sites: swap
that one global for a recorder and wrap the image for the rest, and the decoder is
measured without a line of it changing.

**An unmodelled read must fail, not return.** A wrapper that quietly delegates whatever
it does not model shrinks the offline side the moment a decoder learns a new route, and a
byte we stop reading then reads as a byte retail requires. Raising instead turns that into
a stopped run rather than a wrong finding.

**Attribute by position, and split by role first.** A difference is a set of offsets, and
naming what they are is a walk of the model's own declared arrays — which the image
answers, so the same classifier serves both sides and can be checked against the roles the
retail walker already recorded. One correction is needed before it can be trusted: a byte
retail read *as part of a track walk* belongs to the track walk wherever it landed, and
reporting it by the structure it fell into names the wrong thing. Two bytes in one capture
are exactly that.

**One containment is worth checking rather than declaring.** An offline walk that
materialises a whole track reads every key of every run it enters, and retail reads two;
retail's look-ahead sits in the run after the one it stopped in, which the offline walk
also enters whenever frames remain. So the only track byte retail can reach that an
offline walk does not is the look-ahead running past a track's last run — and that is
falsifiable: a retail-only track span wider than one two-byte key is a run the offline
walk skipped, which is a defect rather than a bound.

**Produce the retail side twice and require the two to agree.** The per-model union the
span dictionary already carries came from one pass over the records; re-walking it from
the dictionary that pass wrote is a second production of the same set. Reproducing it byte
for byte is what says neither drifted; a disagreement invalidates every set below it.
Measured, both are 3,430,830 bytes over 34 owners across 143,612 span-set frames.

**The bound is the corpus, and the third set says so.** A byte is only in the retail set
if this run fired the identity that reads it, so a range read by neither means unread by
this run. It is recorded as unknown; 35,858,024 bytes of the theatre's owner images are,
and calling them inert would be the claim the whole method exists to avoid making.

**What the report may not be.** The comparison covers the frame the capture covers — the
animation evaluation hooks — so geometry, material, skin and flex reads are outside it on
both sides rather than measured and found absent. And the difference authorizes nothing on
its own: repairing what it names is separate work, because a pass that measures a decoder
and repairs it in the same breath can no longer say which it did.

### 9.15e Differencing a decoder's transform output against a capture

Byte coverage says which bytes an offline decoder never reads; it says nothing about the
values that come out of the bytes it does. Answering that means evaluating our rules at
the captured identity and comparing per bone against what the runtime produced — and the
method is mostly about making sure each number measures one thing.

**Feed every stage the runtime's own input for that stage.** A chain compares a stage
against an input its own earlier stage produced, so one wrong rule makes everything below
it wrong too and the report names the last stage instead of the first. Composing the
captured composed-locals rather than our decoded ones, and skinning the captured
bone-to-world rather than our composed one, keeps each stage's number attributable to its
own rule. Chaining is then a separate, later measurement whose subject is propagation.

**Carry a second candidate for every rule that is not shipped code.** A transcribed rule
has no implementation to be a difference from, so a report that carries only the
transcription answers "is the decompilation right" and leaves "what does the current export
cost" unasked. Running the ordinary hierarchy beside the split one, and the regenerated
inverse bind beside the stored one, answers both from the same pass — and the contrast is
what turns a confirmation into evidence: a rule that matches where the alternative is 100°
wrong is established in a way that a rule which merely matches is not.

**Divide the placement out and report both frames.** Composition and entity placement are
separable exactly, because `boneToWorld = rootToWorld · modelSpace` holds through the split
branch as well as the ordinary one. Reporting only world space folds an entity standing
somewhere unexpected into every bone's error; reporting only model space hides a wrong
placement entirely. Both cost one multiply.

**Not every slot is a comparison.** Three exclusions are load-bearing and each has to be
counted rather than scored. A bone outside the composed pose's selected mask was never
written and holds whatever the bone cache left there. A bone whose captured matrix is
singular holds zeros — and two zero matrices are *identical*, yet a trace-based rotation
metric reports `acos(-0.5)` = 120° for them while translation reports a perfect match, so
both numbers are artefacts of comparing nothing. And a composed pose handed a root that is
not a frame has nothing to be placed by and no inverse to divide out.

**A rotation metric must not measure scale.** `arccos` near one is violently sensitive: a
rotation row 3e-4 short moves `(trace − 1)/2` to 0.9991 and reads as 2.4° of rotation that
is not there. 3e-4 is the ordinary orthonormality of a captured `matrix3x4_t` — retail's
own float32 pipeline sits there, above §11.3's 1e-4 excellent ceiling for row length — so
an un-normalized angle reports the engine's precision as a disagreement, on every bone, at
a magnitude that swamps the real ones. Normalizing the rotation blocks first and reporting
orthonormality as itself separates the two.

**Label a mismatching bone by what it is, not by how large its error was.** A bone a
procedural rule drives after the hierarchy composes and a bone the hierarchy composes
wrongly produce the same numbers and are different work. `StudioBone.ProcType` is in the
image already, so carrying it on every cluster classifies the cluster from the model rather
than from the residual. Measured over one cutscene, that single field separates the two
completely: procedural bones carry 748,666 of 968,910 over-band rotation observations while
ordinary bones are 99.3% excellent, and every cluster the run ranks is procedural.

**Fold, never retain.** Hundreds of thousands of records over up to 96 bones will not fit
as values. Maxima and per-band counts fold exactly; a quantile folds into a log-spaced
histogram and is then an upper bound whose resolution the report states. What must not fold
together is a bone count and a record count: summing bands over bones reports one wrong
pose ninety-six times, so a record is counted once at its worst band and the two are named
apart.

**Deduplicate on payload identity.** A cutscene draws the same actor several times a frame
from the same buffers, and a comparison is a function of the bytes and the skeleton and of
nothing else, so a distinct payload can be compared once and multiplied by its record
count. It is exact rather than a sample, and it is what puts a 397,796-draw corpus inside
a minute.

**What the report may not be.** It authorizes nothing: a pass that measures a decoder and
repairs it in the same breath can no longer say which it did. Its denominators are plural
and stated, because only a draw whose consumed pose build produced a composed pose for the
same entity and model can reach a composition comparison. And the bound is the corpus — a
bone this run never posed is unmeasured, not correct.

### 9.16 What not to do first

Avoid:

- manually mapping `vampire.exe` as if it were a DLL;
- recreating the original engine initialization environment;
- full-process dumps with no temporal markers;
- unrestricted scans for “matrix-looking” memory;
- permanent static addresses;
- injecting a complex Unreal module into the 32-bit process;
- judging correctness from one screenshot;
- rewriting the decoder before a failing comparison exists.

The original process is most useful as a controlled oracle, not as a library to host inside Unreal.

---

## 10. Offline comparison pipeline

### 10.1 Recommended repository layout

```text
tools/
  vtmb-animation/
    crowbar-runner/
    decoder-export/
    smd-parser/
    trace-reader/
    pose-compare/
    reports/
    signatures/
    probe/
      d3d9-trace/
      pose-hook/
    schemas/
      decoded-pose.schema.json
      runtime-trace.schema.json
tests/
  vtmb-animation/
    manifests/
    synthetic/
    expected/
docs/
  reverse-engineering/
    vtmb-animation.md
    experiments.md
```

Do not commit original game assets. Test manifests may reference user-supplied files by path-independent logical name and hash.

### 10.2 Canonical decoded-pose representation

Avoid using SMD itself as the project's canonical internal representation. SMD uses Euler rotations and can hide quaternion differences.

Use:

```json
{
  "schema": 1,
  "source": "project-decoder",
  "model_sha256": "...",
  "model_logical_path": "models/character/...",
  "sequence": {
    "name": "idle_03",
    "index": 12,
    "fps": 30.0,
    "frame_count": 31,
    "cycle": 0.4
  },
  "bones": [
    {
      "index": 0,
      "name": "Bip01",
      "parent": -1,
      "local_translation": [0.0, 0.0, 0.0],
      "local_rotation_xyzw": [0.0, 0.0, 0.0, 1.0],
      "model_matrix_3x4": [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]
    }
  ],
  "unknown_fields": []
}
```

### 10.3 Comparison rules

Quaternion comparison must account for:

```text
q and -q representing the same rotation
```

Use angular difference:

```text
angle_error = 2 × acos(clamp(abs(dot(normalize(q1), normalize(q2))), 0, 1))
```

Compare both local and model space.

Interpretation:

- local mismatch at one bone, matching parent: channel or basis problem for that bone;
- local matches, model space differs: hierarchy multiplication/order problem;
- all bones differ by the same transform: coordinate or entity-space problem;
- only descendants differ: first mismatching ancestor is the likely cause;
- local/model match but Unreal looks wrong: bind pose, skinning, import, or retargeting problem;
- body matches but hair/accessories differ: procedural or jiggle stage;
- ordinary gameplay matches but dialogue differs: layer/facial/choreography stage.

### 10.4 Crowbar normalization

When comparing Crowbar SMD:

- pin Crowbar version and executable hash;
- retain original decompile log;
- record whether stricter formatting was enabled;
- record any `$origin` correction;
- parse node hierarchy and frame data without Blender;
- convert Euler values using an explicit, tested order;
- document the SMD coordinate basis;
- never silently “fix” orientation during parsing;
- emit both raw and normalized values.

### 10.5 Regression-fixture policy

Each regression case should specify:

```text
logical model path
SHA-256 of every source file
sequence index and name
frame or normalized cycle
bone subset or full skeleton
reference source
tool/source commit
expected tolerance
known unsupported stages
```

Where game-derived golden data cannot be committed, support:

- local fixture generation from a user-owned installation;
- hash-only manifests;
- synthetic non-copyrighted fixtures;
- CI skips with clear diagnostics when the local corpus is unavailable.

Do not let “fixture missing” appear as “decoder regression.”

---

## 11. Experiment design

### 11.1 Experiment record template

```markdown
## EXP-YYYY-NNN — Short question

- Date:
- Investigator:
- Status: planned | running | completed | inconclusive
- Confidence before:
- Confidence after:

### Question

One falsifiable question.

### Inputs

- Game executable hash:
- Module hashes:
- Patch/mod configuration:
- Model logical path and hashes:
- Sequence:
- Map/entity:

### Procedure

Exact reproducible steps or command.

### Observations

Raw facts only.

### Interpretation

What the facts suggest.

### Competing explanations

- ...

### Result

Verified | Strong evidence | Partial | Hypothesis | Unknown

### Artifacts

- Trace:
- Report:
- Log:

### Next experiment

Smallest test that reduces the remaining uncertainty.
```

### 11.2 Initial controlled experiment matrix

| Experiment | Base | Single changed variable | Expected use |
|---|---|---|---|
| Idle progression | Same model/scene | Cycle/time | Frame interpolation |
| Entity translation | Frozen cycle | Entity origin | Matrix-space classification |
| Entity rotation | Frozen cycle | Entity yaw | Matrix-space and handedness |
| Sequence change | Same cycle | Sequence index | Clip selection |
| Playback rate | Same sequence | Rate | Time integration |
| Loop boundary | Same sequence | Cycle near 0/1 | Wrap behavior |
| Head aim | Frozen body | Look target | Procedural head/eye stage |
| Gesture | Same base idle | One gesture | Layer and bone-mask behavior |
| Expression | Same body/dialog | Named expression | Face-only behavior |
| Lip sync | Same expression | LIP time | Phoneme/flex combination |
| AI locomotion | Same walk clip | Navigation enabled | Root/entity policy |

Change one variable at a time.

### 11.3 Numerical acceptance thresholds

Thresholds should begin as reporting bands, not claims of semantic equivalence.

Suggested starting bands:

| Metric | Excellent | Investigate | Definite mismatch |
|---|---:|---:|---:|
| Local translation | ≤ 0.01 source units | 0.01–0.1 | > 0.1 |
| Model-space translation | ≤ 0.02 source units | 0.02–0.2 | > 0.2 |
| Rotation angle | ≤ 0.05° | 0.05–0.5° | > 0.5° |
| Orthonormal row-length error | ≤ 1e-4 | 1e-4–1e-3 | > 1e-3 |

Adjust only after observing real floating-point and capture noise. Do not loosen thresholds to make a test pass without recording why.

Two observations from the theatre capture bear on them, and neither loosens a band.
Retail's own bone-to-world matrices sit at a **median row-length error of 3e-4**, above the
1e-4 excellent ceiling, so that row measures the engine's float32 pipeline rather than a
difference from ours and no comparison against a captured matrix can claim to be tighter.
And the rotation row is only meaningful on normalized rotation blocks: at 3e-4 of row
scale an `arccos`-based angle reads 2.4°, which would consume the whole 0.05–0.5° band
before any real disagreement arrived. §9.15e carries the method that follows from both.

---

## 12. Codex workflow

### 12.1 What Codex can drive well

Codex is well suited to:

- inspect parser and runtime code;
- build 32-bit probe tooling;
- generate signature scanners;
- compile the hook DLL;
- launch repeatable experiments;
- parse SMD, JSON, and binary traces;
- score candidate matrix blocks;
- correlate caller RVAs and module hashes;
- produce per-bone error reports;
- minimize failing fixtures;
- add regression tests;
- maintain the experiment log;
- compare Ghidra/decompiler output exported as text;
- update signatures for known binary versions.

### 12.2 What still benefits from human confirmation

Initially:

- selecting a visually simple scene;
- confirming that a candidate draw is the intended character;
- confirming the first valid hook boundary in x32dbg;
- checking that probe instrumentation does not destabilize the game;
- evaluating final presentation in Unreal.

The goal is to make these confirmation points rare, explicit, and reproducible.

### 12.3 Make the workflow machine-readable

Prefer:

```text
probe launch --experiment idle-001
probe capture --frames 300 --output traces/idle-001
decode export --manifest idle-001.json
crowbar export --manifest idle-001.json
pose-compare run --experiment idle-001
report render --experiment idle-001
```

Avoid a workflow whose only record is:

```text
click instruction
follow pointer
look at character
remember what changed
```

### 12.4 Codex operating rules for animation work

Before changing parsing, skeleton conversion, or runtime animation code:

1. Read this document and the current experiment log.
2. Inspect existing code and tests.
3. Identify the exact failing model, sequence, frame/cycle, and bone.
4. State the evidence level.
5. Add or generate a reproducible fixture.
6. Make the smallest change that explains the evidence.
7. Compare both local and model-space transforms.
8. Record unsupported or unknown fields.
9. Preserve raw input values.
10. Update the experiment record.

Never:

- treat later Source behavior as automatically correct for VTMB;
- use Crowbar disagreement alone as proof that project code is wrong;
- discard an unknown flag silently;
- mix coordinate conversion into binary decompression without tests;
- broadly refactor the importer while diagnosing one channel;
- commit copyrighted game assets;
- modify unrelated user changes to make validation pass.

### 12.5 Suggested automated report

```text
Experiment: idle-001
Model: models/character/...
Sequence: idle_03 (12)
Cycle: 0.438200

Reference sources:
  Crowbar 0.74 / SHA-256 ...
  VTMB runtime trace schema 1 / vampire.exe SHA-256 ...

Summary:
  53 bones compared
  49 within strict tolerance
  4 mismatched

First hierarchy mismatch:
  Bone 17 Bip01_L_UpperArm
  Parent 16 matches
  Local translation error: 0.000001
  Local rotation error: 1.842°
  Model rotation error: 1.842°

Descendant propagation:
  L_Forearm, L_Hand, L_Finger0 differ consistently

Likely class:
  local rotation decode or coordinate conversion for bone 17

Not consistent with:
  global basis error
  parent multiplication error
  Unreal retargeting

Next experiment:
  Hook original local quaternion output for bone 17 at cycles
  0.40, 0.4382, and 0.4667.
```

---

## 13. Investigation order

This document owns the evidence model and method, not task status. Status is
`docs/project/roadmap.md`'s. The sequence below remains the method for any renewed,
owner-called capture scope.

The durable investigation sequence is:

1. choose one ordinary humanoid, one simple clip, and one repeatable scene;
2. prove unattended reset, settle, selection, capture, and cleanup on that clip;
3. enumerate the raw sequence descriptors, animation descriptors, and blend-grid
   entries resolved by the installed player models;
4. capture every unambiguously addressable identity with final CPU matrices and
   correlated draw data, retaining explicit evidence for the rest;
5. match each capture to patch-first source bytes and exported animation, then
   compare the engine-neutral evaluator per frame and bone;
6. use corpus failure and mismatch clusters to select a concrete case;
7. attach raw resource bytes and runtime-object identity to that output path;
8. trace backward only from the first mismatching bone, vertex, or frame;
9. add layers, included-model remaps, root motion, procedural work, scenes,
   facial/lip processing, and secondary motion one demonstrated mismatch at a
   time;
10. keep source equivalence separate from Unreal basis conversion, retargeting,
   and presentation acceptance.

Capture/storage work follows the same rule: reuse the existing bounded final-pose
capture for the corpus, add the smallest raw record only when a selected
mismatch needs an earlier stage, measure it in retail, and add queue, blob,
index, process-reuse, or compression machinery only for an observed limit.

## 14. Task ownership and programme method

Completed capabilities, open research slices, priorities, and acceptance gates
are not duplicated here. Status lives in `docs/project/roadmap.md` (the LIFE
programme and the `RE32`/`RE33` rows).

### 14.1 Working rules

1. **Capture what the runtime reads, not only what it emits.** A pose that matches proves
   nothing about a field the decoder silently skipped. Consumed byte spans are first-class
   evidence.
2. **Immutable source bytes are a dictionary, not a stream.** Model images, skeletons, and
   animation descriptors are stored once by content hash; per-call records keep identity
   and offsets only.
3. **Start from a visible output.** Use the final matrix, flex, vertex, or draw state as an
   oracle, then trace backward only to the first unexplained stage.
4. **Capture raw evidence before decoding it.** A record keeps registers, stack bytes,
   pointer values, bounded pointed-to spans, original addresses, and copy outcomes. Field
   names are analyzer hypotheses.
5. **Keep the live path bounded.** The game callback validates and copies bounded spans,
   accounts for failures and drops, and returns. It does not decode, index, compress, or
   retain a run in memory.
6. **Measure before optimizing.** Rates, counts, and joins replace estimates.
7. **Evolve the tool freely.** No schema registry, migration system, or public
   compatibility promise. A finalized run is nevertheless one self-contained, queryable
   evidence file whose raw payloads stay readable by the current research tools.
8. **Do not build ahead, and do not capture behind.** Later systems justify no capture
   infrastructure before a difference report names them; a rule that passes the evidence
   gate carries into the export and runtime without waiting for its phase; a system whose
   format is already closed is built from that specification, not re-derived by capture.
9. **Delete dead machinery.** A hook, reader, control, dependency, or test stays only while
   it protects the exact build, preserves useful evidence, or answers a current question.

### 14.2 Evidence gate for a research conclusion

A task that claims retail behavior closes only when: (1) the executable/module hashes, tool
commit, and capture recipe are recorded; (2) captured, written, dropped, truncated,
unreadable, unjoined, and incomplete counts are reported; (3) the relevant input and output
spans are preserved as raw bytes; (4) an offline analyzer can use the database without
reading the live process; (5) a repeat capture or independent byte comparison supports the
conclusion; (6) the fact is written in its owning `docs/vtmb/` document; and (7) the
recovered rule has a game-independent regression or a local hash-gated retail comparison.
Synthetic tests establish probe safety and recorder mechanics only; they never close a task
that claims game behavior.

### 14.3 Candidate causes for a transform mismatch

A classification vocabulary, not a work plan — a row becomes work only when a difference
report points at it: sampling and time (frame selection, interpolation, loop/clamp boundary,
rate, seek, first-frame reset); selection, blends and layers (sequence/activity choice,
blend inputs, pose parameters, overlays, gestures, transitions, masks, missing-channel
defaults); included-model remapping; controllers and procedural order; root and entity
motion; hierarchy composition (split inheritance, multiplication order, inverse-bind
convention); post-composition writes (a bone-to-world slot the composed locals do not
explain — secondary motion, a follow constraint, or any stage between the pose build and
the draw).

### 14.4 Triggered capture and storage improvements

Responses to measurement, never scheduled prerequisites: fixed-size slots for measurable
callback cost; narrow spans or filters before queue-cap tuning; content-hash blob storage
when immutable bytes dominate; offline content-deduplication when exact payloads dominate
(compression is not the response — measured, it buys 5.4% after deduplication and costs an
inflate per read); an index only for a demonstrated slow query; writer-thread batching only
for a measured disk bottleneck; per-experiment filters for a noisy hook. No triggered
improvement becomes a general subsystem unless more than one real experiment needs it.

### 14.5 Explicit non-goals

Hot-unloading the probe; multiple game builds, schema versions, or public consumers; a
general hook SDK, remote collector, or live capture service; a pre-planned ladder for scene
lifecycle, save/load, ragdoll, or teardown (each is a CAP5 case only when a difference
report names it); a complete process dump or pointer-graph crawler; forcing or enumerating
every possible animation; duplicate captures for clan/armor models resolving to the same
owner and data; explaining every unknown bit before useful behavior is reproduced; native
VtMB authoring or writing animation back into the original game.

### 14.6 Programme risks

The standing responses: bracket a cutscene on its own events, not wall clock; keep the game
window focused (`host_framerate` pins the step but an unfocused window halves the rate with
no cvar to stop it); the draw/skeletal streams join on the measured fixed +4 with CAP2.1's
generations scoping every record; filter first and measure before optimizing; unread bytes
stay visible rather than inferred correct; validate pages, cap reads, catch faults, fail
closed; scoped generation identities rather than raw addresses; record every evaluator call
under its pose-build generation; distinguish fired evaluation, completed pose build, and
draw coverage; keep records self-bounded with disposable indexes; infrastructure grows only
inside the two tracks (the evidence loop and the delivery) with one current task each;
runtime carry-over is authorized by the evidence gate, not phase completion; a closed-format
system is never re-derived by capture; and closure is stated against the adjudicated baseline —
the recorded compute-every-bone divergence in `docs/architecture/animation-architecture.md` —
rather than against retail unconditionally.

---

## 15. Unreal-specific recommendations

### 15.1 Keep format decoding engine-agnostic

The binary decoder should produce:

```text
skeleton hierarchy
reference transforms
raw sequence metadata
local pose samples
events
root-motion candidate data
unknown raw fields
```

It should not directly create Unreal assets while interpreting binary fields.

### 15.2 Separate four transformation stages

Use explicit modules:

```text
VTMB binary decode
    ↓
VTMB canonical pose
    ↓
VTMB-to-Unreal basis/unit conversion
    ↓
Unreal asset construction/retargeting
```

Every stage needs its own tests.

### 15.3 Validate before retargeting

Retargeting can hide or create errors. First import a character with its original skeleton and compare raw transforms. Retarget to a modern mannequin only after source-pose fidelity is established.

### 15.4 Prefer Unreal-native runtime systems after behavior is understood

The goal is behavioral fidelity, not necessarily reimplementation of every old internal algorithm.

Examples:

- import verified body clips as native animation sequences;
- map verified root motion into Unreal's root-motion system;
- implement gestures with animation layers or montages;
- use Control Rig/IK for behavior shown to be procedural;
- map facial controls to curves or a modern rig;
- retain source sequence names and semantic metadata for scripting compatibility.

### 15.5 Preserve provenance

Every generated Unreal animation asset should have a sidecar or metadata record containing:

- source logical path;
- source hashes;
- importer and decoder commit;
- sequence name/index;
- conversion basis and scale;
- verification status;
- runtime trace or reference report ID;
- unsupported fields.

---

## 16. Cautions

### 16.1 Legal and distribution caution

Reverse engineering for interoperability and local conversion can be treated differently across jurisdictions. Requiring a user-owned installation and generating derived content locally may reduce some distribution risks, but it does not guarantee immunity from copyright, contract, trademark, or anti-circumvention claims.

Practical safeguards:

- inspect only user-supplied, legally owned binaries and assets;
- do not redistribute original or converted VTMB art/audio/model data;
- do not bypass ownership checks, DRM, authentication, or unrelated security controls;
- obtain qualified legal advice before public distribution.

### 16.2 Tool-policy caution

RenderDoc states that its official support channels are for debugging software users created, and its current API table does not support D3D9. Do not depend on RenderDoc for this workflow or seek official assistance for capturing a commercial game.

### 16.3 Debugger safety

- Work on a separate game installation.
- Disable network-facing or unrelated overlays where practical.
- Keep saves backed up.
- Verify signatures before hooking.
- Fail closed on unknown binaries.
- Bound all reads and writes.
- Never trust a pointer solely because it looked valid in one frame.
- Avoid heavy work inside render callbacks.

### 16.4 Reproducibility caution

Always record:

- executable and DLL hashes;
- model and animation-library hashes;
- exact capture recipe and tool commit;
- coordinate conversion.

An address without module hash and RVA is not durable evidence.

### 16.5 Project-change caution

Reverse-engineering experiments often coexist with unrelated animation work. Preserve user changes and report unrelated build failures separately. A missing local animation export corpus is an environment/content-pipeline gap, not automatically a runtime-decoder regression.

---

## 17. Claims ledger

| Claim | Status | Evidence | Verification still needed |
|---|---|---|---|
| VTMB uses MDL version `2531` | Verified | Crowbar and SourceIO version-specific implementations | None for ordinary header detection |
| Skeleton hierarchy and bind data are readable | Strong evidence | Crowbar, SourceIO, working replacement workflows | Full corpus/property validation |
| Crowbar exports ordinary bone-animation SMD | Verified | Public code and documented workflow | None for existence of feature |
| Crowbar SMD exactly matches `vampire.exe` | Unknown | No public runtime matrix proof found | Runtime capture |
| SourceIO is a working VTMB animation oracle | False at inspected commit | VTMB `import_animations` returns immediately | Re-check future commits |
| Shared humanoid animation libraries exist | Strong evidence | Mod Developer Guide and model include structures | Automated dependency inventory |
| DDLullu Formatter supports replacement meshes | Verified operationally | Current tutorial and SDK integration | More NPC-rig coverage |
| Formatter permits arbitrary skeleton changes | Not supported by documented workflow | Tutorial explicitly keeps donor skeleton | Reproducible counterexample |
| Bloodlines SDK compiles arbitrary multi-bone character animations | Not supported | SDK's published limitations | Reproducible counterexample |
| Facial flex extraction is exact | Contradicted | Crowbar author describes approximation/guesses | Model-by-model runtime capture |
| The shipped flex rules are recovered | Verified, two independent routes | This project's opcode decode and a community decompiler's published QC expressions agree operand for operand on all 60 (`docs/vtmb/facial_animation.md`) | None for the rules; the vertex-animation encoding they drive is this project's alone |
| VTMB procedural bones are AxisInterp authored from a `.vhb` | Verified for the rule; community-reported for the authoring file | 3,123 compiled `ProcType == 1` records replay against capture; modders decompiling VTMB characters name the `.vhb` and report it invariant between models | None for reproduction — the compiled record, not the authoring file, is what a port carries |
| Root motion is just the root-bone track | Hypothesis only | No VTMB-specific runtime proof | Entity plus pose trace |
| Later Source `SetupBones` behavior applies unchanged | Hypothesis only | Architectural similarity | VTMB disassembly/capture |
| Final D3D9 constants can help locate the palette | Strong technical basis | D3D9 API and common skinning layout | VTMB-specific trace |
| Final CPU matrices are the best runtime oracle | Recommendation | They collapse many unknown intermediate stages | Implement and validate hook |
| Render info `+0x18` is the `C_BaseAnimating` instance plus 4 | Verified | Three complete `sp_theatre` captures: the same −4 across 39 shared models resolving all 49 instances, with no instance address shared between runs, plus draw-side values four above an eight-aligned address; a fourth capture measures a constant +4 between the entity and renderable addresses the actor census records separately | None — instance reuse is now scoped by the actor interval and the witnessed lifetime |
| The interface subobject at `this+0x4` is a declared layout, not a heap artefact | Verified | `C_BaseEntity::C_BaseEntity` and `~C_BaseEntity` install and restore five subobject vtables at `+0x0`, `+0x4`, `+0x8`, `+0xc`, `+0x10`; the second is the interface both the draw field and `SetupBones` name | None |
| Every skeletal actor is identified before it is used | Verified | Three complete `sp_theatre` captures, the latest two carrying 54 identities across 48 addresses, none unobserved, none observed late, and no evaluation record outside the interval in which its address meant the model it names | None |
| An address that changes actor is always witnessed | Verified | Two transitions witness it, not one. A later capture reuses six addresses under five identity changes: the sixth records construct/first/destruct twice, so a destruction separates two actors where no in-place change occurs. Across two complete cutscenes every extra identity at a reused address is accounted for by an identity change or a destruction | None |
| A vtable carrying the pose slots identifies a shared construction path | Contradicted | `0x101e3cfc` has one data reference and its writer `0x10002d80` one caller; a capture hooked there records no construction while skeletal actors are posed | None — the shared pair is `C_BaseEntity`'s constructor and destructor |
| The composed-pose stage receives the frame the pose is placed into | Verified | Its third argument is retained on every composed pose: 237,903 of 237,903 carry one, and 256 sampled decode as a rotation and a translation with worst determinant error 5.57e-08 | Whether that translation is the entity's own origin field, which needs a pinned displacement |
| `SetupBones` receives the same interface subobject the draw field stores | Verified | Its prologue adjusts `this` down four bytes to reach the instance, and the captured bracket entity equals the draw entity on every draw whose frame built a pose | None |
| A pose build covers exactly one entity | Verified | One complete `sp_theatre` capture: across 237,929 composed poses no generation spans several entities or carries an evaluation naming another | None |
| One draw frame encloses every draw of an actor | Contradicted | The same entity is drawn 793 times inside a `CModelRender::DrawModel` frame and 7,632 times outside one; 31 of 141 drawn models appear on both paths | None — the second path is `CModelRender::DrawModelShadow`, and two frames are now bracketed |
| Exactly two engine frames submit a studio draw | Verified | Only `engine.dll` and `StudioRender.dll` hold `TStudioRender012`; its single global `0x20d63ef0` is read 72 times in `engine.dll`, and following each read to the vtable register it feeds finds slot `+0x58` called at `0x200a6221` and `0x200a6ce4` only | None |
| Every studio draw is enclosed by a bracket | Verified | Reproduced on a second and third cutscene carrying contribution records too: all 2,170,793 records of one carry a generation, with the hook's own counters reporting zero unbracketed records | None |
| The shadow frame builds its own pose | Verified | `CModelRender::DrawModelShadow` calls the same renderable slot `+0x3c` at `0x200a6c7b`, and the capture records 165,086 shadow frames | None |
| `SetupBones` argument five is a required output pointer | Contradicted | The shadow frame passes `NULL` where `CModelRender::DrawModel` passes the address of a stack local | What the callee writes through it when non-null |
| The runtime `studiohdr` is the whole `.mdl` image at offset 0 | Verified | One census capture retained all 141 runtime images and compared each against the patch-first installed file of the same `Checksum`: every `Length`@140 equals the installed file size, and 42 images are byte-identical | None |
| A runtime pointer minus its `studiohdr` base is a model-image offset | Verified | Follows from the above, and 99 of 141 images differ from disk only inside ranges that are themselves at known array strides | None for the conversion; which ranges are unreliable is the next row |
| A fired contribution names the model it was decoded from | Verified | The three frames below `resolve_virtual_model_pose` take the owning `studiohdr` as argument zero on one seven-dword `__cdecl` contract. Two complete cutscenes record 238,529 and 238,793 sequence contributions naming 34 and 35 owner identities, all observed at or before first use with an image each, and 17 and 18 of them are owners no actor animates under | None |
| A captured descriptor pointer agrees with the index and stride it claims | Verified | Across two cutscenes, 961,518 of 961,518 contributions satisfy `pointer - studiohdr == LocalSeqIndex + index * 764` or `LocalAnimIndex + index * 72`, with every index inside the owner's own declared count | None |
| The sequence blend grid is a `groupsize[0] × groupsize[1]` space | Verified | `groupsize[0] * groupsize[1] == numblends` on 294 of 294 multi-blend sequences in the installed character tree; two captures fire 3,440 and 3,434 multi-blend contributions, every one a 9×1 grid evaluating two adjacent cells, with no contribution in either run decoding more than two | Whether the four-cell path evaluates as decoded. The population that would exercise it is bounded: all 49 3×3 sequences are `*_aim_layer` entries in `move_and_ranged.mdl`, driven by pose parameters 2 and 3 over −45°..+45° on both axes and carrying no activity name, so only off-center ranged aiming reaches them |
| A witnessed decoder pointer agrees with an independently derived one | Verified | Two cutscenes place 231,747 of 231,747 and 231,649 of 231,649 record and bone pointers, each solved separately for the bone it implies and required to agree, verifying the animindex indirection and both strides together | None |
| The channel decoders walk the whole track | Contradicted | Both stop at the run holding the sampled frame, reading two header bytes of each run stepped over and the keys bracketing the frame. The quaternion decoder reaches the following run's first key when the frame after it leaves the run; the position decoder does not | The exporter walks whole clips, so its coverage is a superset by construction rather than by defect |
| The selected-bone mask is remapped across an include-model boundary | Contradicted | 231,747 and 231,649 decoded-bone sets equal the mask of their enclosing sequence, with no cell decoding a bone its mask does not select, over 34 and 35 owners of which 17 and 18 are banks no actor animates under | Whether it holds for a scene exercising the four-cell blend path, which no cutscene reaches |
| A cell always decodes at least one bone | Contradicted | 10,311 cells in each of two runs and 10,353 in a third are enclosed by a mask selecting no bone; each reads `NumBones`@240, `BoneIndex`@244, `numframes`@12 and `animindex`@48 and decodes nothing | None on the behaviour. The count is not authored: two runs agreeing exactly read as invariant until a third landed 0.4% away, so per-run totals are a sample |
| The unindexed region before `LocalAnimIndex` is unreachable | Contradicted | It is the include-model bone remap array, addressed by `StudioModelGroup`+0x10 relative to the group entry. Over 27 include-carrying models the offset is byte-identical on disk in 41 of 41 groups and every array base lands inside the region, which the arrays span completely | None |
| A hook signature can be a fixed byte sequence | Contradicted | `evaluate_sequence_pose` begins `MOV AL,[0x104902c9]`, whose absolute operand the loader rewrites; `client.dll` loaded at three different bases across three runs. A declaration must name the relocated span and compare it after adding the load delta | None |
| The loader patches the model image in place | Verified | 26,958 differing bytes in one run, all inside `StudioBone.Flags`, `StudioMesh.VertexData`, `StudioSeqDesc`+0xc, the include-model records, `MDLHeader.Flags`/`NumLocalNodes`, and one unindexed gap | What each rewritten field becomes; layout is owned by `mdl_v2531.md` |
| `StudioBone.Flags & 0x2` on disk is what the runtime uses | Verified | Comparing all 2,286 captured bones against their disk bytes: 627 flag words change, every change only *sets* bits and none is ever cleared, and the same 27 bones carry `0x2` on disk and at runtime with none disagreeing | None; the split-inheritance rule and the exported `split_bones` inventory are unaffected |
| The rest of `StudioBone.Flags` on disk is what the runtime uses | Contradicted | The loader sets `0x4`, `0x8` and `0x20`–`0x8000` on 27 models, so any bit but `0x2` read from disk is a pre-load value | What each set bit means; they look like usage flags computed from hitboxes, attachments and vertex LODs |
| The shared-animation remap is built inside the model image | Strong evidence | The gap between the include-model array and `LocalAnimIndex` is written on exactly the 27 models with include models and on no other, 24,234 bytes, with zero exceptions either way | Identifying the records; the spec's open question on nested remap records inside `0x10089c40` is the same question |
| A model image stays resident for the whole capture | Contradicted for a run that reaches the map change | A capture stopping on the `sp_tutorial_1` transition swept 143 recorded headers and found 38 resident and 103 no longer reading as their own header, with no address serving a second checksum | None — the mechanism is the cache slot at `model+0xb0` being nulled, owned by `animation_and_movers.md` |
| A single hookable function frees a model from the cache | Contradicted | `CModelLoader::UnloadModel` (`0x200b8e60`, the interface slot `+0x18`) only clears reference bits; the standalone `Cache_Free` (`0x201146f0`) is bypassed by the inlined evictions in `Cache_Alloc` (`0x201147f0`) and `Cache_Flush` (`0x201143a0`), which are the paths a map change takes | Whether the three allocator variants carrying the same inlined body ever evict a studio model |

---

## 18. Source index

Sources are ordered by direct relevance. Access dates for live pages: 2026-07-30.

### VTMB-specific tools and community evidence

1. **Crowbar source repository**  
   <https://github.com/ZeqMacaw/Crowbar>

2. **Crowbar MDL 2531 SMD writer** — dedicated animation sampling and SMD output  
   <https://github.com/ZeqMacaw/Crowbar/blob/0d46f3b6a694b74453db407c72c12a9685d8eb1d/Crowbar/Core/GameModel/SourceModel2531/SourceSmdFile2531.vb>

3. **Crowbar issue #86: Vampire Bloodlines MDL (v2531) decompiling issues** — orientation and compressed vertex-type report  
   <https://github.com/ZeqMacaw/Crowbar/issues/86>

4. **Bloodlines SDK current page and limitations**  
   <https://www.moddb.com/mods/vtmb-unofficial-patch/downloads/bloodlines-sdk>

5. **DDLullu MDL Formatter custom player-model tutorial** — donor skeleton and copied animation constraints  
   <https://www.moddb.com/games/vampire-the-masquerade-bloodlines/tutorials/compiling-a-custom-pc-model-with-ddlullus-mdl-formatter>

6. **Mod Developer Guide** — animation libraries, Python animation control, VCD/LIP, expressions, scripted sequences  
   <https://gamefaqs.gamespot.com/pc/914819-vampire-the-masquerade-bloodlines/faqs/54295>

7. **Crowbar author discussion of Bloodlines facial-flex approximation**  
   <https://steamcommunity.com/app/2600/discussions/0/3275816470990402214/>

### SourceIO

8. **SourceIO repository**  
   <https://github.com/REDxEYE/SourceIO>

9. **SourceIO MDL 2531 importer registration at inspected commit**  
   <https://github.com/REDxEYE/SourceIO/blob/25b3978e366aeed1b4bdcf078394751b2d376c7a/blender_bindings/models/mdl2531/__init__.py>

10. **SourceIO MDL 2531 import code; animation function currently returns**  
    <https://github.com/REDxEYE/SourceIO/blob/25b3978e366aeed1b4bdcf078394751b2d376c7a/blender_bindings/models/mdl2531/import_mdl.py#L299-L305>

### Structural and runtime-tool references

11. **Valve Source SDK 2013** — later structural reference, not VTMB authority  
    <https://github.com/ValveSoftware/source-sdk-2013>

12. **Valve Developer Community: Source MDL overview**  
    <https://developer.valvesoftware.com/wiki/MDL_(Source)>

13. **Valve Developer Community: StudioMDL and interchange formats**  
    <https://developer.valvesoftware.com/wiki/StudioMDL_(Source)>

14. **Microsoft: `IDirect3DDevice9::SetVertexShaderConstantF`**  
    <https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setvertexshaderconstantf>

15. **Microsoft: Direct3D 9 constant float registers**  
    <https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-vs-registers-constant-float>

16. **x64dbg conditional breakpoint documentation**  
    <https://help.x64dbg.com/en/latest/introduction/ConditionalBreakpoint.html>

17. **MinHook** — x86/x64 Windows hooking library  
    <https://github.com/TsudaKageyu/minhook>

18. **RenderDoc API support table** — D3D9/10 currently unsupported  
    <https://github.com/baldurk/renderdoc#api-support>

---

## Final recommendation

Do not spend the next phase trying to “fully understand VTMB animation” in the abstract.

Build one executable proof chain:

```text
one model
one sequence
one exact cycle
        ↓
project local pose
Crowbar SMD pose
original runtime final pose
        ↓
per-bone numerical comparison
        ↓
first unexplained mismatch
        ↓
targeted hook or disassembly
        ↓
regression test
```

Once the seed chain works, scale the same unattended capture across the raw
resolved player-animation inventory and compare every clean result with the
patch-first export/decoder stack before deepening the hooks. Let duplicate-name
and selection failures plus per-frame/per-bone mismatch clusters choose the next
resource, blend, layer, procedural, scene, or facial experiment. Until then,
broad rewrites, deep unrestricted disassembly, and visual-only debugging will
produce far more uncertainty than knowledge.
