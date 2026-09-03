# Niagara authoring strategy — research synthesis (2026-09-02)

Owner call 2026-09-03: A+B adopted, with the review loop in `docs/project/effects_authoring.md`.
Four research passes (engine source at `D:\Epic\UE_5.8`, Epic docs, Unreal Fest / CEDEC decks, forums,
realtimevfx, GitHub) converge on the same answer; this note records the evidence and the
proposal. Real effect examples the reconstruction should copy live in `docs/examples/`.

## 1. What failed, and why (root causes, all in engine source)

The R7.3 floor is one `NS_ElysiumParticle` with 20 identical emitter slots, ~490 user
parameters and 37×32 lookup-table arrays per slot, built through Epic's experimental
NiagaraToolsets MCP toolset. Every failure of the week is structural:

| Symptom | Root cause | Where |
|---|---|---|
| Systems never activate until opened in the Niagara editor | The toolset builds its view model with `bCompileForEdit=false, bCanAutoCompile=false`; only asset *creation* requests a compile; `GetSystemCompileState` reads a stale cache; `UNiagaraComponent::Activate` then defers silently (`bAwaitingActivationDueToNotReady`, no log at any verbosity) | `NiagaraExternalSystemEditorUtilities.cpp:556-571`, `NiagaraAssetBuilder.cpp:194`, `NiagaraComponent.cpp:1366-1376` |
| Editor at 13 GB, ~25 MB per call | 42 call sites each construct a fresh `FNiagaraExternalEditContext` → full `FNiagaraSystemViewModel` + `FScopedTransaction`; no batch, no shared context | `NiagaraToolset_System.cpp` |
| Per-instance parent/child readers impossible | `FNiagaraDataInterfaceEmitterBinding::ResolveHandle` requires the DI's outer to be the `UNiagaraSystem`; a user-parameter DI is copied onto the component. Epic's own comment: "this may not be possible in all situations (i.e. user parameter)" | `NiagaraDataInterfaceEmitterBinding.cpp:105-111`, `.h:17` |
| Bool gates read false | Niagara `true` is `-1` (`BoolValues { True = INDEX_NONE }`); an expression yielding `1` is not a valid bool | `NiagaraTypes.h:49-77` |
| Expressions cannot call DI functions | A stack "HLSL expression" is a `UNiagaraNodeCustomHlsl` with no input pins; a real Custom HLSL *module* can take a DI on a pin and call it | `NiagaraNodeCustomHlsl.cpp:626`, `NiagaraHlslTranslator.cpp:8989-9010` |
| Nobody can read the asset | 660 modules and ~3,000 expression strings in one 37 MB LFS blob | `floor_journal.json` |
| Fidelity dropped by construction | The fixed 8-root / 12-child slot layout cannot draw 8 corpus roots, including the placed `blood_guardian_summon_emitter` (20 leaves, 12 never drawn) | `ElysiumEffectFamilies.h` |
| The API itself | `UNiagaraExternalEditUtilities` carries a triple "EXPERIMENTAL! DO NOT USE EXCEPT FOR TESTING!" banner; the toolset "doesn't support Lightweight Emitters or Niagara Data Channels" (Epic's own skill text) | `NiagaraExternalSystemEditorUtilities.h:42-47`, `NiagaraToolsets/Content/Python/skills/fundamentals.py` |

Two hard engine facts that bound every design: **runtime-built systems are impossible in a
packaged game** (`RequestCompile`, the translator, shader caching and the graph source are all
editor-only, five independent citations), and **CPU simulation is mandatory** for the corpus
(VtMB `collide {}` on 224 nodes needs `PerformCollisionQuerySyncCPU`, which is `bSupportsGPU =
false`; CPU sims cannot sample textures or run simulation stages).

## 2. What people who did this at scale say

- **CyberConnect2, Unreal Fest Tokyo 2025** (Takashi Matsuo): 4,300 Cascade assets converted
  1:1 into one `NS_` per effect; the stock converter "went worse than expected"; cost ~40 engine
  mods, ~30 converter extensions, ~30 module forks. Lesson: run the converter on real samples
  first, then decide the tooling. Strict path convention, forks in a suffixed plugin, never in
  place.
- **Partikel (Andreas Glad)**, on a 25-emitter toggle-by-int master system: "It's hell … get
  code to build you a system." **ifurkend**: "Niagara System isn't built around to be an
  'effects manager'"; a zero-spawn emitter still ticks Emitter State.
- **Epic** (Scalability and Best Practices): "There is an overhead associated with every
  emitter"; "Data Interfaces as User Parameters … a new UObject is created for every Niagara
  component" (verified: `NiagaraParameterStore.cpp:1611`). Epic's default validation budget is
  **8 emitters per system** (`NiagaraValidationRules.h`).
- **Michael Galetzka (Epic)**: an inherited child emitter costs "exactly the same" at runtime as
  a standalone one. Many generated assets are an editor/DDC cost, not a frame cost.
- **Simon Tovey (Epic, NDC owner)**: the "uber system with tons of emitters and conditions" is
  "an important issue"; NDCs are the spawn-layer answer, single-asset override landed in 5.7.
- **Hovl (Unity VFX author), realtimevfx 2026**: exported every Unity particle system to JSON,
  "the only thing I don't know is how to make a plugin that will automatically create a Niagara
  System" — zero replies. The generator is the scarce half; no Source-to-Unreal importer does
  particles.

## 3. The corpus, measured (all 1,698 units through the project's own `TreeBuilder`)

| Shape | Count |
|---|---|
| Roots with exactly one drawing leaf | 1,205 (71%) |
| Roots with ≤ 2 drawing leaves | 1,456 (86%) |
| 99th-percentile leaf count / max | 7 / 23 |
| Drawing-parent → drawing-child edges, whole corpus | **80** |
| Nodes with `collide {}` | 224 |
| Max keyframes on a ramp | 15 |

The generic machine pays full generality for the 1% on behalf of the 99%.

## 4. Options, ranked

| Option | Verdict |
|---|---|
| **A+B. Hand-authored module library + one generated system per root, inherited from base emitters** | **Recommended.** ~10 authored assets (LFS, ~1 MB); 155 placed roots now, 1,698 later, generated into `Content/ElysiumGenerated/VFX/` (gitignored, 0 bytes). Parent/child becomes a compile-time emitter name inside the asset, so the impossible problem disappears. One material per leaf natively. Readable. 13–18 days. |
| E. Lightweight (stateless) emitters for the simple majority | **Layer on A later.** `FNiagaraDistribution*` natively holds lo/hi ranges and curves over normalized age as plain `UPROPERTY` data, no compile; but the module set is closed (no collision, no reader, no HLSL, no spherical motion). A per-root generator decision for the 1,205 single-leaf roots if hub frame cost demands it. |
| C. One interpreter system (textures / sim stages / NDC) | **Dead on CPU.** Texture DI returns magenta on the VM; sim stages are GPU-only; one material per renderer defeats collapsing; per-instance readers forbidden. NDCs survive as the *cross-instance* spawn layer. |
| Current 20-slot floor | Sunk. Unreadable, drops fidelity, experimental API, 37 MB LFS per iteration. |
| D. Runtime-built systems | Impossible in a packaged build. Closed. |

### 4.1 The proposed pipeline (A+B)

Authored once by hand in the editor, under `Content/ElysiumAuthored/VFX/Modules/`:
`NM_VtMBRampSample` (Custom HLSL: Vector-array DI on a pin, ramp index, age, roll → float),
`NM_VtMBSpawnPlace`, `NM_VtMBMotion`, `NM_VtMBLook`, `NM_VtMBRootClock`, `NM_VtMBBurst`,
`NM_VtMBChildSpawn`, `NM_VtMBCollide`, and two base emitters `E_VtMBLeaf` /
`E_VtMBCollideLeaf`. Every VtMB-generic rule lives in ~8 readable graphs; nothing is an
expression string.

Generated by the bake: one `NS_<root>` per distinct root, one inherited emitter per drawing
node, module composition and parameter values only. Compile explicitly
(`RequestCompile(false)` + `WaitForCompilationComplete()`), gate on `IsReadyToRun()`, save.
The lane already exists in shape: `pipeline/unreal/make_particle_systems.py` +
`UElysiumParticleAssetBuilder` (headless, in the bake); only its API and granularity change.

Runtime: unchanged contract. `AElysiumEffectActor` loads `NS_<root>` by name and writes the
system-level user parameters (`RateScale`, `Tint`, `SizeScale`, spawn shape, fog); the ~210
lines of slot fitting go away. `DA_EffectFamilies`, `ET_ElysiumEffects`, the four
`MI_Particle*` children, dust / steam / beam: untouched.

### 4.2 Two viable composition APIs for the generator (spike decides)

1. **Conversion-context / clipboard path** (Epic's own Cascade converter substrate):
   `create_system_conversion_context` → `add_template_emitter` → `find_or_add_module_script`
   → `set_parameter` with `create_script_input_*` (incl. `create_script_input_di`) →
   `add_renderer` / `set_renderer_binding` → `finalize()` (one view model per *system*, one
   compile). Python-reachable. Beneath it, `UNiagaraClipboardEditorScriptingUtilities` +
   `UNiagaraStackItemGroup::Paste` are exported and reflected, so the beta plugin is a
   convenience, not a dependency.
2. **C++ editor module over `UNiagaraExternalEditUtilities`** (what
   `ElysiumParticleAssetBuilder.cpp` already does), ending in `RequestCompile` +
   `WaitForCompilationComplete`. Same surface as the MCP toolset without the per-call view
   model. Experimental banner applies.

Either way: `AddEmitter` needs a template emitter asset (empty-emitter path is commented out
with a TODO), nothing auto-saves, and Custom HLSL must avoid intrinsics the CPU VM lacks
(`smoothstep` silently diverges).

### 4.3 Verification without screenshots

Copy `NiagaraSystemAuditCommandlet`'s shape; attach `UNiagaraValidationRule_*` (EmitterCount,
BannedModules, UserDataInterfaces, Lightweight) to `ET_ElysiumEffects`; one automation test in
the `Elysium.Content.*` tier that iterates every generated `NS_`, asserts `IsReadyToRun()`,
activates, ticks, and asserts a non-zero particle count per emitter (`Activate` blocks on
compilation under `GIsAutomationTesting`).

## 5. The spike (≈2 days, before any commitment)

1. **Headless composition compiles and activates.** Build `NS_Spike` from a template emitter
   plus two stock modules under `-run=pythonscript`, `finalize`, wait, save. Accept only if the
   Niagara editor was never opened, the compile state is fresh, and a spawned `NiagaraActor`
   reports `is_active() == True`. This is the go/no-go on the whole generated-asset family.
2. **One readable module beats 37 expression strings.** Hand-author `NM_VtMBRampSample`
   (Custom HLSL, array DI on a pin), drop it on a CPU emitter fed `waterdrops_timer`'s size ramp,
   confirm the sampled value against a hand computation.
3. **Generation fixes parent/child and the dropped leaves.** Generate `drip` (collide → spawn
   child; emitter-level reader bound `Other → <parent name>`) and
   `blood_guardian_summon_emitter` (20 leaves); place in `sm_hub_1`; accept if the child lands
   at the collision, all 20 leaves draw, no "Source emitter not found", and hub frame cost stays
   inside rain's 1.0 ms discipline.

If step 1 fails on path 1, repeat it on path 2 before any fallback.

## 6. The agent's authoring protocol (what changes for Claude)

- Two lanes, never mixed: **generated** systems through the headless C++/Python bake;
  **authored** hero assets (`NS_ElysiumDust/Steam/Beam`, the module library) through the editor,
  by hand or by the MCP toolset one call at a time, finalized by opening once in the Niagara
  editor and saving.
- Never generate module scripts, scratch pads or Custom HLSL bodies; author them once, review
  them as code, version them.
- The toolset stays an inspection and one-off tweak tool. It is Experimental, has no compile
  or batch call, and cannot touch lightweight emitters or data channels.
- Read Epic's six shipped Niagara agent skills under
  `NiagaraToolsets/Content/Python/skills/` before authoring; they are the only Epic guidance.

## 7. Docs this overturns

`effects-architecture.md` §3.4 "One Niagara system per Troika file. 1,698 systems is
unmaintainable" counted assets rather than hand-authored artifacts; §5.3 (the slotted floor)
is replaced by the generated lane. The R7.3 contract (`effects[]`, `particleTrees{}`, the
actor, the families) stands.
