# VTMB Animation Reverse Engineering

## Consolidated status, evidence model, runtime-capture strategy, and implementation roadmap

**Game:** *Vampire: The Masquerade — Bloodlines* (2004)  
**Primary target:** Troika's modified early Source engine and MDL version `2531`  
**Document role:** Exploration research brief; not a status tracker or fact owner  
**Last reviewed:** 2026-07-30

Detailed work status and the executable capture program are tracked in
`docs/project/retail-capture-roadmap.md`. Confirmed VtMB behavior belongs in
`docs/vtmb/animation_and_movers.md`, `docs/vtmb/facial_animation.md`, and
`docs/vtmb/choreographed_scenes.md`.

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
- an implementation roadmap for faithful Unreal playback;
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
| Exact sequence blending and animation layers | Not publicly complete | Low | Runtime state and pose trace |
| Pose parameters | Structurally visible, behavior incomplete | Low–medium | Controlled parameter sweeps |
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

Unknowns that need measurement:

- whether duration is `frames / fps` or `(frames - 1) / fps`;
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

MDL structures expose pose-parameter descriptors, but that does not establish runtime semantics. A correct reimplementation needs to know:

- parameter name and range;
- wrapping behavior;
- default;
- sequence blend mapping;
- whether values are normalized before lookup;
- interpolation policy;
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

MinHook is a small x86/x64 Windows API hooking library. It is suitable for a persistent 32-bit probe DLL after the target function, calling convention, and lifetime have been established.

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

The first persistent probe should capture the final pose after the original function returns.

Minimum record:

```json
{
  "schema": 1,
  "capture_id": "controlled-idle-001",
  "game": {
    "exe_sha256": "...",
    "module": "studiorender.dll",
    "module_sha256": "..."
  },
  "frame": 1834,
  "time_seconds": 61.133333,
  "entity": {
    "runtime_id": "0x...",
    "name": "optional",
    "model": "models/character/...",
    "origin": [0.0, 0.0, 0.0],
    "angles": [0.0, 0.0, 0.0]
  },
  "animation": {
    "sequence_index": 12,
    "sequence_name": "idle_03",
    "cycle": 0.4382,
    "playback_rate": 1.0,
    "layers": [],
    "pose_parameters": []
  },
  "pose": {
    "space": "unknown",
    "layout": "3x4-row-major-candidate",
    "bone_count": 53,
    "matrices": [
      [1.0, 0.0, 0.0, 12.4, 0.0, 1.0, 0.0, -3.1, 0.0, 0.0, 1.0, 55.8]
    ]
  }
}
```

If names or sequence metadata are not known initially, store raw pointers and module-relative addresses. Never fabricate semantic fields.

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
- include a capture schema version.

MinHook is a reasonable detour implementation once the boundary is understood.

### 9.11 What not to do first

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

## 13. Roadmap

### Milestone 0 — Reproducible corpus discovery

**Deliverables**

- local game-install detector;
- file-hash manifest generator;
- animation-library dependency report;
- no copyrighted data committed.

**Exit criteria**

- one chosen character resolves to its skeleton and external animation libraries;
- all source files have hashes;
- missing corpus produces a clear skip, not a misleading test failure.

### Milestone 1 — Offline decoded-pose comparison

**Deliverables**

- project decoder JSON exporter;
- SMD parser;
- Crowbar runner or documented import;
- quaternion-preserving canonical pose format;
- per-bone comparison report.

**Exit criteria**

- one simple sequence can be compared for every frame;
- first mismatch is identified by bone and channel;
- coordinate transforms are explicit and tested.

### Milestone 2 — D3D9 boundary tracer

**Deliverables**

- 32-bit D3D9 probe;
- shader-constant and draw-call log;
- candidate matrix scorer;
- caller-RVA grouping report.

**Exit criteria**

- one candidate palette is reliably correlated with the chosen character;
- the same call path repeats across frames;
- trace overhead is acceptable.

### Milestone 3 — Final CPU pose hook

**Deliverables**

- stable signature for one supported executable/module version;
- final matrix capture;
- entity/model correlation;
- module and signature verification.

**Exit criteria**

- 300 consecutive frames captured without crash;
- matrix space is classified;
- repeated captures are deterministic within tolerance.

### Milestone 4 — Runtime metadata capture

**Deliverables**

- sequence index/name;
- cycle;
- playback rate;
- entity transform;
- relevant layer and pose-parameter state where discoverable.

**Exit criteria**

- runtime and offline evaluators can be sampled at the same semantic time;
- sequence changes and loop boundaries are visible in the trace.

### Milestone 5 — Ordinary body-clip equivalence

**Deliverables**

- runtime-vs-project pose comparison;
- minimized mismatch fixtures;
- verified fixes for ordinary clips.

**Exit criteria**

- representative idle, walk, run, turn, and one scripted body sequence meet agreed tolerances;
- no Unreal retargeting is involved in the equivalence test.

### Milestone 6 — Root motion and locomotion policy

**Deliverables**

- entity/root/pelvis trace;
- motion-source classification;
- Unreal root-motion policy.

**Exit criteria**

- stationary and AI-driven playback are both explained;
- no double application of movement occurs.

### Milestone 7 — Layers, gestures, and pose parameters

**Deliverables**

- controlled layer sweeps;
- inferred priority and bone masks;
- transition timing;
- runtime state representation.

**Exit criteria**

- one base idle plus one upper-body gesture matches the original;
- one pose-parameter-driven blend matches over a sweep.

### Milestone 8 — Procedural bones and IK

**Deliverables**

- pre/post-stage capture if obtainable;
- behavior classification;
- Unreal-native replacement or faithful evaluator.

**Exit criteria**

- representative head aim and accessory/jiggle case are understood;
- unsupported cases are explicit.

### Milestone 9 — Facial expression, eyes, and lip sync

**Deliverables**

- dialogue capture protocol;
- flex/eye/head/LIP layer isolation;
- model-specific expression inventory;
- Unreal facial mapping.

**Exit criteria**

- one controlled spoken line matches timing and broad deformation;
- named expression plus lip sync can be composed without abrupt invalid state;
- approximation is labeled where exact recovery is not possible.

### Milestone 10 — Unreal integration

**Deliverables**

- verified animation-sequence import;
- skeleton-family mapping;
- event and root-motion metadata;
- dialogue-layer runtime;
- non-retargeted equivalence tests plus retargeted presentation tests.

**Exit criteria**

- raw imported pose matches the verified decoder;
- retargeting is tested separately;
- final in-game playback passes both numerical and visual acceptance.

---

## 14. Concrete next steps

The first implementation sprint should do only the following:

1. Select one simple humanoid model and one idle sequence.
2. Generate a manifest of the model and all linked animation files.
3. Pin Crowbar 0.74 by source commit, executable version, and hash.
4. Export bone-animation SMD files and preserve the decompile log.
5. Add a strict SMD parser that retains raw Euler values.
6. Export the project's own per-frame local quaternion/translation data.
7. Define the canonical decoded-pose JSON schema.
8. Implement local-space and model-space comparisons.
9. Produce a first-mismatch report.
10. Do **not** change the decoder unless the report exposes a concrete mismatch.

Then:

11. Build a minimal 32-bit `SetVertexShaderConstantF` logger.
12. Group candidate uploads by caller RVA and vector count.
13. Correlate one candidate with the character draw.
14. Trace backward in x32dbg to the CPU pose buffer.
15. Capture final matrices for the same idle sequence.
16. Classify matrix space using frozen-cycle entity translation/rotation.
17. Compare original runtime, project decoder, and Crowbar output.
18. Fix only the first experimentally isolated discrepancy.

Do not start face, dialogue layering, or native animation compilation during this sprint.

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
- importer version;
- decoder schema version;
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

- Steam/GOG or other installation family;
- official patch level;
- Unofficial Patch version;
- other mods;
- executable and DLL hashes;
- Crowbar/SDK versions;
- model and animation-library hashes;
- capture schema;
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
| Root motion is just the root-bone track | Hypothesis only | No VTMB-specific runtime proof | Entity plus pose trace |
| Later Source `SetupBones` behavior applies unchanged | Hypothesis only | Architectural similarity | VTMB disassembly/capture |
| Final D3D9 constants can help locate the palette | Strong technical basis | D3D9 API and common skinning layout | VTMB-specific trace |
| Final CPU matrices are the best runtime oracle | Recommendation | They collapse many unknown intermediate stages | Implement and validate hook |

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

Once that chain works, scale it across models, clips, layers, and facial systems. Until then, broad rewrites, deep unrestricted disassembly, and visual-only debugging will produce far more uncertainty than knowledge.
