# Pipeline–Unreal integration review

**Review status:** validated rewrite, 2026-08-20

**Engine target:** Unreal Engine 5.8

**Scope:** `pipeline/src/elysium_pipeline`, `pipeline/unreal`, the Elysium editor-only C++ helpers, and their Unreal process boundaries

## Executive verdict

The integration is fundamentally sound. The pipeline has the right ownership boundary: offline Python recovers and normalizes source data; Unreal authors engine-native packages; plain-C++ runtime systems consume those packages. The current implementation also has several mature properties that should not be traded away for generic importer convenience:

- deterministic generated package names and mount ownership;
- explicit receipts and per-asset invalidation;
- bespoke skeletal, animation, Niagara, cloth, and map authoring where stock importers do not model the recovered contracts;
- verification after authoring, including a fresh-process character verifier where in-memory state is not accepted as proof;
- editor-only C++ at engine seams that are not reflected to Python.

The original proposals were directionally useful, but their gains were not all equally established. This review reaches four conclusions:

1. **Implement the correctness and orchestration changes.** Harden `AssetImportTask` result checking, centralize import policy, make every generator import-safe, share command-line parsing, and fold the player AnimBP generator into the existing content commandlet. These are low-risk changes with clear correctness or process-count gains.
2. **Run bounded spikes before changing package-authoring mechanics.** In-place static-mesh updates, batched package saves, in-process package reload verification, and commandlet font generation have plausible benefits, but their wall-time and lifecycle gains are not yet measured in this repository.
3. **Keep the specialist C++ paths.** The skeletal/animation, Niagara, and cloth builders exist at real Python-reflection or asset-model boundaries. Replacing them with Python or generic Interchange import would reduce control and would not address the measured bottleneck.
4. **Do not describe framework status too broadly.** Interchange is the normal import path for supported formats, but individual translators and workflows still carry different production/experimental statuses. Geometry Scripting remains documented as Beta even though the local plugin descriptor is not flagged Beta. Python editor scripting is documented as Experimental, while the local Python plugin descriptor is marked Beta.

The best near-term plan is therefore **harden first, remove one redundant Unreal launch, then benchmark four narrow authoring experiments**. Do not begin with a broad rewrite.

## 1. Evidence and confidence model

This document uses four labels:

- **Confirmed:** matched against the current Elysium repository and either local Unreal Engine 5.8 source/headers or official Epic documentation.
- **Observed:** demonstrated by an existing Elysium log, benchmark, test, or fresh-process verifier. Observations are tied to the tested corpus and machine; they are not universal engine guarantees.
- **Proposed:** compatible with the available APIs, but its benefit or lifecycle behavior still needs a focused spike.
- **Rejected:** factually incorrect for UE 5.8, incompatible with pipeline ownership, or unlikely to improve the measured problem.

Static inspection can confirm an API contract and an exact process reduction. It cannot establish a wall-time or peak-memory gain. Every performance proposal below therefore has an explicit acceptance gate.

## 2. Current integration, as built

### 2.1 Ownership boundary

The pipeline has two distinct halves:

1. Offline Python extracts retail data, resolves source formats, computes stable recipes, and writes normalized intermediates below the export root.
2. Unreal Python and editor-only C++ turn those intermediates into `.uasset` packages below Elysium-owned generated mounts.

That division is correct. Unreal should remain the authority for package construction, asset registry interaction, mesh descriptions, animation data models, compilation, and save/load verification. Offline Python should remain the authority for source decoding, naming, dependency recipes, and cache planning.

### 2.2 Process topology

`pipeline/src/elysium_pipeline/unreal.py::generate_policy_content` currently starts three Unreal phases for a full policy-content generation:

1. `UnrealEditor-Cmd.exe -run=pythonscript` for `pipeline/unreal/build_content.py`;
2. a full editor with `-ExecutePythonScript` for `pipeline/unreal/make_ui_fonts.py`;
3. another `-run=pythonscript` commandlet for `pipeline/unreal/make_player_anim_bp.py`.

The third phase is structurally redundant: the player AnimBP generator authors ordinary assets and can be invoked by the first content commandlet after it is made import-safe. Removing it changes the exact full-generation process count from **three Unreal launches to two**. The time saved is one commandlet cold start plus module/plugin initialization; the repository does not yet have a clean isolated timing for that launch, so no seconds-saved claim is made.

If the font spike later proves that the font factory works under the commandlet, the topology can fall from three launches to one. That is a separate acceptance gate because Slate/font initialization differs between the full editor and commandlet paths.

### 2.3 Commandlet versus full-editor execution

Epic documents two Python execution modes:

- `-ExecutePythonScript` starts the editor, loads the normal startup environment, and runs after the editor is ready.
- `-run=pythonscript` uses a commandlet-oriented path intended for unattended scripting and does not automatically load a level.

See [Scripting the Unreal Editor using Python](https://dev.epicgames.com/documentation/unreal-engine/scripting-the-unreal-editor-using-python).

The local UE 5.8 commandlet path also explains an important garbage-collection distinction:

- `unreal.SystemLibrary.collect_garbage()` calls `UKismetSystemLibrary::CollectGarbage`, which queues collection for the end of the frame. Epic's [Python `SystemLibrary` reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/SystemLibrary) explicitly describes it as queued. A non-ticking `-run=pythonscript` commandlet does not consume that request.
- module-level `unreal.collect_garbage()` is a different Python-plugin binding. In the local 5.8 source it directly invokes CoreUObject collection, corresponding to the engine's synchronous [`CollectGarbage`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/CoreUObject/CollectGarbage) function.
- Elysium's `UElysiumSkeletalBuildLibrary::ReleaseBakedPackages` does more than either generic call: it finds only saved, clean packages under the owned mount, clears `RF_Standalone` on their objects, and then performs a full collection. This targeted behavior is necessary because editor-created assets otherwise remain rooted by standalone flags.

Therefore, the blanket statement “Python garbage collection is a no-op in commandlets” is false. The precise statement is: **`SystemLibrary.collect_garbage()` is ineffective on this non-ticking path, and generic synchronous collection alone does not replace Elysium's scoped standalone-flag release.**

### 2.4 Current authoring patterns

The repository uses several appropriate integration patterns:

- `AssetImportTask` for image-backed texture imports and other factory-backed asset imports;
- Geometry Script dynamic meshes and `create_new_static_mesh_asset_from_mesh` for map static meshes;
- explicit material, material-instance, level, placeholder, Niagara, and cloth construction;
- a T3D-backed AnimBP flow where graph shape is maintained as source and installed into an engine asset;
- editor-only C++ for skeletal mesh descriptions, skeleton merging, animation data-controller writes, resampling, package release, Niagara compilation, and cloth seams;
- registry scans, deterministic asset paths, receipts, and explicit saves;
- a fresh Unreal verifier for character packages, plus in-process verification where a fresh load is not required.

These are not signs that the pipeline “failed to use Interchange.” They reflect different asset contracts. Interchange is a good fit for supported file translators; it is not a universal replacement for authored engine objects assembled from recovered data.

## 3. Fact-check results

| Claim or proposal | Verdict | Correct UE 5.8 interpretation | Action |
|---|---|---|---|
| `AssetImportTask` is the supported Python import driver. | Confirmed | With no explicit factory, `AssetTools` routes supported sources through Interchange. A synchronous task is selected with `async_ = False`. | Keep it and harden its contract. |
| Interchange is “Production” as one undifferentiated framework. | Needs qualification | Interchange is the default path for supported imports, but translator and workflow status varies. FBX Interchange and some scene/level workflows remain experimental; UE 5.8 calls USD Interchange production-ready for asset import but experimental for level import. | State status per translator/workflow. |
| `FImportAssetParameters.bRunSynchronous` should be set from Python. | Incorrect | [`FImportAssetParameters`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/InterchangeEngine/FImportAssetParameters) keeps `bRunSynchronous` as an internal C++ field, not a reflected Python property. Python should call the synchronous `ImportAsset`/`ImportAssetWithResult`, or use `AssetImportTask.async_ = False`. | Do not design a Python API around this field. |
| `FImageUtils::ImportFileAsTexture2D` is Python-visible. | Incorrect | `FImageUtils` is not a reflected `UCLASS`, and the function is not a `UFUNCTION`. It also creates a transient texture without the normal persistent source/import contract. | Reject it for the bake. |
| `SystemLibrary.collect_garbage()` performs immediate collection. | Incorrect | It queues end-of-frame collection. The current commandlet does not tick that request. | Retain scoped C++ release; optionally probe module-level `unreal.collect_garbage()` only for non-owned transient cleanup. |
| Geometry Script is no longer Beta because the plugin descriptor has no Beta flag. | Incorrect | Epic's UE 5.8 Geometry Scripting documentation still labels the feature Beta. The local descriptor flags and public documentation do not use identical status vocabulary. | Treat it as Beta and keep focused regression coverage. |
| `CopyMeshToStaticMesh` can update an existing static mesh in place. | Confirmed | The UE 5.8 Geometry Script API accepts an existing `UStaticMesh`, copy/build options, a target LOD, material options, and optional Nanite settings. | Run an identity/performance spike before replacing delete-and-create. |
| `EditorAssetLibrary` must be replaced by `EditorAssetSubsystem`. | No material gain | `EditorAssetLibrary` is an editor scripting wrapper over subsystem behavior. A mechanical swap does not change package lifecycle or avoid import/build work. | Migrate only when a needed subsystem API is absent from the library wrapper. |
| `EditorLoadingAndSavingUtils` can batch save and reload packages. | Confirmed API; unproven fit | `save_packages`, `reload_packages`, and `unload_packages` are first-party editor APIs. Reload may still fail to establish the same guarantee as a fresh verifier for complex skeletal packages. | Spike first; keep the fresh process until parity is proven. |
| `JsonObjectGraph` can replace custom asset serialization. | Incorrect fit | UE 5.8's graph utility is explicitly experimental and is oriented to object graph stringification. Its class/CDO helper filters editor-only data; it is not a general persistent package authoring API. | Use only for diagnostic snapshots if helpful. |
| Niagara external edit utilities should be called directly from Python. | Incorrect | The required `UNiagaraExternalEditUtilities` entry points are not exposed as Python-callable `UFUNCTION`s in the local 5.8 headers. | Keep the C++ wrapper and explicit compilation barrier. |
| All generators can be guarded by adding one line. | Needs qualification | Several files already have `main()`, while others execute their whole body at module import. The latter need a small callable refactor before adding `if __name__ == "__main__"`. | Refactor generator-by-generator with import-safety tests. |
| Unreal's Batch Process framework should parallelize this bake. | Rejected for current shape | UE 5.8 adds a framework for very large, independent asset batches. The current map/character bake shares mounts, packages, registry state, receipts, and native compilation barriers. It is not embarrassingly parallel. | Keep current lane/lock discipline; reconsider only for isolated, disjoint packages. |

Official status references:

- [Importing Assets Using Interchange](https://dev.epicgames.com/documentation/unreal-engine/importing-assets-using-interchange-in-unreal-engine)
- [Interchange Project Settings](https://dev.epicgames.com/documentation/unreal-engine/interchange-settings-in-the-unreal-engine-project-settings)
- [Unreal Engine 5.8 Release Notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes)
- [Geometry Scripting Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/geometry-scripting-reference-in-unreal-engine)

## 4. API contracts to standardize

These specifications are intentionally narrower than the full Unreal APIs. They define the Elysium contract that helper functions and tests should enforce.

### 4.1 Texture import contract

Use `AssetImportTask` as the public driver. It provides the repository's required synchronous, automated, named-destination import surface and allows an Interchange pipeline override through `options`.

Required task fields:

```python
task = unreal.AssetImportTask()
task.filename = source_filename
task.destination_path = destination_path
task.destination_name = destination_name
task.automated = True
task.async_ = False
task.replace_existing = force
task.replace_existing_settings = force
task.save = False                 # the bake owns save/checkpoint timing
task.options = texture_pipeline_override

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
objects = list(task.get_objects())
```

[`UAssetImportTask::GetObjects`](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/UnrealEd/UAssetImportTask/GetObjects) is the supported result accessor. It waits for an asynchronous import if needed; this pipeline still sets `async_ = False` because following stages immediately consume the texture and need deterministic ordering.

The shared helper must fail the current unit with a diagnosable warning/error when any of these is true:

- the source file is absent;
- the task yields no object;
- the expected object path is absent from the returned objects;
- the returned object is not `Texture2D`;
- the expected asset cannot be loaded after import;
- the tracked package cannot be saved.

Do not silently accept “an import task was submitted” as success. Import completion and correct target identity are the contract.

### 4.2 Interchange texture-pipeline override

UE 5.8 exposes `UInterchangePipelineStackOverride` as a transient override container. It accepts Python, Blueprint, or native pipeline objects. The texture helper can use a transient `InterchangeGenericTexturePipeline` without creating a project asset:

```python
pipeline = unreal.InterchangeGenericTexturePipeline()
pipeline.allow_non_power_of_two = True
pipeline.detect_normal_map_texture = False

override = unreal.InterchangePipelineStackOverride()
override.add_pipeline(pipeline)
task.options = override
```

This is a **proposed Elysium policy**, not a universal texture rule:

- `allow_non_power_of_two = True` is appropriate for recovered VtMB dimensions that must remain faithful.
- `detect_normal_map_texture = False` avoids filename/content heuristics changing compression or sRGB policy behind the receipt recipe. Normal-map classification should come from recovered material semantics.
- other texture settings that affect cooked output must remain explicit in the bake or recipe. Do not rely on a user's saved import dialog settings.

Acceptance requires a focused test with at least one non-power-of-two albedo, one normal map whose role comes from the material table, a forced reimport, and a cached no-op. Verify object path, dimensions, sRGB/compression policy, source hash/receipt, and reload behavior.

Direct `UInterchangeManager` use is a fallback only if `AssetImportTask` cannot express a requirement. The reflected manager surface includes synchronous `ImportAsset`/`ImportAssetWithResult`, asynchronous import, and `CreateSourceData`; see [`UInterchangeManager`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/InterchangeEngine/UInterchangeManager), [`ImportAssetWithResult`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/InterchangeEngine/UInterchangeManager/ImportAssetWithResult), and [`CreateSourceData`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/InterchangeEngine/UInterchangeManager/CreateSourceData). Moving to it now would add lower-level state without a demonstrated gain.

### 4.3 In-place static-mesh update contract

The current map path deletes its owned static mesh and recreates it with `create_new_static_mesh_asset_from_mesh`. This is deterministic, but it discards object/package identity and can create redirector or reference churn if references exist outside the generator's immediate working set.

UE 5.8's [`Copy Mesh to Static Mesh`](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/GeometryScript/StaticMesh/CopyMeshtoStaticMesh) can write a `DynamicMesh` into an existing static mesh. The spike must set all build-affecting options explicitly:

```python
options = unreal.GeometryScriptCopyMeshToAssetOptions()
options.enable_recompute_normals = False
options.enable_recompute_tangents = True
options.enable_remove_degenerates = False
options.use_original_vertex_order = True
options.replace_materials = True
options.new_materials = materials
options.new_material_slot_names = slot_names
options.clean_assigned_materials = True
options.generate_lightmap_uvs = generate_lightmap_uvs
options.apply_nanite_settings = True
options.new_nanite_settings = nanite_settings
options.emit_transaction = False
options.defer_mesh_post_edit_change = False

target_lod = unreal.GeometryScriptMeshWriteLOD()
target_lod.lod_index = 0
target_lod.write_hi_res_source = False
```

The call must target the already-loaded mesh, use section materials deliberately, and require a success outcome. The exact Python return tuple should be locked by a UE 5.8 API-surface test rather than inferred in production code.

Validated gain: in-place update preserves the asset object and package path and avoids delete/recreate semantics. **Not yet validated:** faster wall time. The API still performs render-thread synchronization, mesh commit/build work, and `PostEditChange` unless deferred. Accept the change only if:

- material slot order, section bindings, collision, Nanite settings, lightmap UVs, and vertex-order-sensitive checks match;
- the saved package survives a fresh load and existing map references remain valid;
- a representative dirty-mesh batch improves median authoring time or materially reduces registry/reference churn;
- peak memory does not regress.

Do not enable deferred `PostEditChange` until there is an explicit, guaranteed finalization barrier.

### 4.4 Package save contract

The current map `flush` saves tracked asset paths one at a time through `EditorAssetLibrary.save_asset`. UE 5.8 exposes [`EditorLoadingAndSavingUtils.save_packages`](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/UnrealEd/UEditorLoadingAndSavingUtils/SavePackages), which accepts a package array and an `only_dirty` flag.

Proposed helper contract:

1. Gather packages only from the bake's tracked, successfully authored objects.
2. Deduplicate packages by long package name.
3. Reject transient, external, or unowned packages.
4. Call `save_packages(packages, only_dirty=True)` at the same existing checkpoint boundaries.
5. Treat a `False` result as unit failure and keep the existing per-asset diagnostics/receipt association.
6. Perform the current load/registry verification after the batch.

Potential gain: fewer Python-to-editor calls and fewer repeated save/registry transitions. The engine still serializes every dirty package, so the gain may be small. Benchmark a representative checkpoint before changing the whole bake. Preserve checkpoint frequency; one enormous final save would increase failure blast radius and memory residency.

### 4.5 Package reload and verification contract

[`EditorLoadingAndSavingUtils`](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorLoadingAndSavingUtils) exposes first-party `save_packages`, `reload_packages`, and `unload_packages`. [`Reload Packages`](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/EditorScripting/EditorLoadingAndSaving/ReloadPackages) returns success plus an error message and accepts an interaction mode suitable for unattended execution.

Before adding another custom `ResetLoaders`/`LoadPackage` C++ helper, spike the first-party reload API:

```python
ok, error = unreal.EditorLoadingAndSavingUtils.reload_packages(
    packages,
    unreal.ReloadPackagesInteractionMode.ASSUME_POSITIVE,
)
```

The spike must run only after successful save. `reload_packages` needs the currently loaded, clean `UPackage` objects so it can perform its own reload workflow; do **not** collect those packages with Elysium's scoped release first. If a separate release-and-load-by-name path is evaluated, treat it as a different experiment. The verifier must compare the same skeletal mesh, skeleton, animation, morph, material, and reference invariants as `bake_verify_characters.py`.

The fresh verifier remains authoritative until the in-process path demonstrates parity on:

- a forced one-character rebuild;
- a cached/no-op character run;
- at least one morph-bearing body;
- an animation-bank owner and compatible skeleton references;
- a full representative character slice without package-residency growth;
- failure injection proving that reload/verification catches a corrupt or missing dependency.

If parity is demonstrated, the character bake can remove one Unreal process from the bake-plus-verify workflow. The exact process reduction is real; the wall-time and memory benefit remain to be measured.

### 4.6 Garbage collection and package release contract

Use the APIs for different jobs:

| API | Semantics in this pipeline | Allowed use |
|---|---|---|
| `unreal.SystemLibrary.collect_garbage()` | Queues end-of-frame GC; current commandlet does not service it. | Do not use as a commandlet memory barrier. |
| `unreal.collect_garbage()` | Immediate general collection in the local Python plugin implementation. | Optional for ordinary unrooted transient objects after a focused test. |
| `ElysiumSkeletalBuildLibrary.release_baked_packages(path)` | Skips dirty packages, clears `RF_Standalone` below the owned path, performs full GC. | Keep for high-volume baked package release. |
| `EditorLoadingAndSavingUtils.unload_packages(...)` | Editor package-unload workflow with editor safeguards. | Spike for low-volume reload verification; do not substitute blindly during active authoring. |

Never release a dirty package. Any new package lifecycle helper must log a diagnosable warning on failure and must be restricted to an explicit Elysium generated mount.

### 4.7 Generator entry-point contract

Every generator should be safe to import and callable from an orchestrator:

```python
def main() -> None:
    ...


if __name__ == "__main__":
    main()
```

For files that currently execute at top level, move the executable body into `main()` without moving constants or reusable helpers unnecessarily. Tests should import each generator with a stubbed `unreal` module and assert that no asset mutation, save, editor exit, or command submission occurs.

The guard is not cosmetic. Epic's UE 5.8 Batch Process examples also call out main guards as necessary to prevent worker/import recursion. Elysium needs the same property for composition even though it should not adopt Batch Process for this bake.

### 4.8 Command-line parsing contract

The `-key=value` parser is duplicated in several bake scripts, and both verifier paths implement equivalent parsing. Move it to a small dependency-free `pipeline/unreal` helper with tests for:

- case-insensitive key matching;
- quoted values and paths with spaces;
- absent/default values;
- empty values;
- duplicate keys, with a documented first- or last-wins rule;
- Unreal's full command line rather than `sys.argv`, because execution modes differ.

This is a maintainability and correctness gain, not a performance optimization.

## 5. Validated gains and remaining hypotheses

### 5.1 Existing measured baseline

The strongest measured pipeline gain is already implemented: per-asset SHA recipe caching. A prior local run over roughly 2,100 assets measured:

- **10.246 seconds** for a no-op run;
- **47.932 seconds** when one texture was dirty.

Those measurements validate dependency-level invalidation, not any new proposal in this document. They should be refreshed on the current binary/hardware before being used as a release target.

A separate `sp_theatre` profile measured:

- **250.5 seconds** total;
- **170.4 seconds** in the Unreal bake;
- **190 static meshes** authored;
- **106 wait/barrier log lines**.

This supports investigating static-mesh authoring, compilation barriers, and save cadence. It does **not** prove that an in-place copy or batched save will be faster; those are the smallest APIs capable of testing the hypothesis.

### 5.2 Gain ledger

| Action | Exact validated gain | Performance status | Confidence |
|---|---|---|---|
| Fold player AnimBP generation into `build_content.py` | Full content generation drops from 3 Unreal launches to 2. | Cold-start seconds unmeasured. | High after import-safety tests. |
| Harden `AssetImportTask` and use `get_objects()` | Import success is tied to the expected object/type rather than task submission. | Not intended as a speed gain. | High. |
| Centralize Interchange texture policy | Recovered texture semantics no longer depend on interactive/default heuristics. | Not intended as a speed gain. | Medium until NPOT/normal/reimport test. |
| Add generator main guards | Generators become composable without import-time asset mutation. | Enables process consolidation; no direct bake speedup. | High. |
| Share CLI parsing | One tested interpretation across bake and verifier entry points. | No direct speedup. | High. |
| In-place static-mesh copy | Preserves asset/package identity and removes delete/recreate behavior. | Wall-time gain unproven. | Medium pending spike. |
| Batch tracked package saves | Reduces API call count. | Serialization-time gain unproven. | Medium pending benchmark. |
| In-process reload verification | Can remove one verifier process if parity holds. | Wall-time/memory gain unproven. | Medium-low until fresh-load-equivalent tests. |
| Commandlet font generation | Could reduce full generation from 2 remaining launches to 1. | Initialization compatibility and seconds unproven. | Low-medium pending spike. |
| Rewrite Niagara/cloth in Python | None. Required APIs are not reflected. | Likely negative engineering return. | Rejected. |
| Adopt Batch Process broadly | None for current shared-state bake. | Lock/package contention risk. | Rejected. |

### 5.3 Benchmark protocol

Every performance spike should record the same evidence:

- current Git commit, UE 5.8 build identifier, command, corpus/scope, and whether the run is cold, warm, forced, or cached;
- total process time and per-stage timings already emitted by the bake;
- authored/skipped asset counts, compile/wait barrier counts, save calls, and verifier results;
- peak working set if the proposal changes package residency;
- median of three comparable warm runs, with the first cold run reported separately;
- output package hashes or semantic verification where byte-identical serialization is not expected;
- `git status`/generated-mount cleanliness and no receipt advancement on failed output.

Accept a performance change only when the improvement exceeds run-to-run noise and all semantic checks remain equal. A useful default gate is at least **10% median improvement in the targeted stage** or a clearly demonstrated lifecycle/correctness benefit. Do not require 10% when the purpose is correctness, identity preservation, or removing an entire failure mode.

## 6. Ranked action plan

### P0 — Correctness and composition

#### P0.1 Consolidate import-task construction and validate results

**Files:** `pipeline/unreal/bake_lib.py` plus current direct task users in particles, rain/world placeholders, and fonts.

Actions:

1. Add one shared task constructor and one result validator.
2. Set `automated`, `async_`, replace policy, `save`, destination, and options explicitly.
3. Use `get_objects()` and require expected object path/type.
4. Convert import/save failures to diagnosable unit failures; never advance a receipt after failure.
5. Add stub unit tests plus one focused Unreal texture test.

Expected gain: deterministic failure behavior and one import policy surface. No performance claim.

#### P0.2 Make all generators import-safe

**Files:** `pipeline/unreal/make_*.py`, `build_content.py`, and focused tests.

Actions:

1. Refactor top-level generator bodies into `main()`.
2. Add the standard main guard to every executable generator.
3. Add import-safety tests that detect asset mutation and editor exit.
4. Preserve direct-script behavior exactly.

Expected gain: safe orchestration and removal of accidental work at import time.

#### P0.3 Share Unreal command-line parsing

**Files:** `build_content.py`, `bake_map.py`, `bake_characters.py`, `bake_wield.py`, and both verifier scripts.

Actions:

1. Add the dependency-free parser described in §4.8.
2. Replace duplicates only after its focused tests pass.
3. Preserve each caller's current defaults and failure messages.

Expected gain: consistent CLI behavior; no speed claim.

### P1 — Remove a redundant Unreal launch

#### P1.1 Invoke the player AnimBP generator from `build_content.py`

Prerequisite: P0.2.

Actions:

1. Add the player AnimBP callable to the ordered content generator list at its dependency-safe position.
2. Remove only the separate third launch from `generate_policy_content`.
3. Preserve log attribution so a failing generator names its stage.
4. Test direct execution, orchestrated execution, forced rebuild, cached/no-op behavior, and failure exit status.

Acceptance: the same AnimBP package and graph verification pass, while full policy generation uses two Unreal processes instead of three.

### P2 — Bounded performance/lifecycle spikes

Run these independently. Do not combine them into one branch or benchmark, because that would make the gain unattributable.

#### P2.1 In-place static-mesh update

Use one representative map slice with reused assets, multiple material slots, collision, Nanite, and lightmap UVs. Compare current delete/create with `CopyMeshToStaticMesh` under the contract in §4.3.

Promote only if semantic parity holds and either targeted-stage time improves beyond noise or reference/registry churn is materially reduced.

#### P2.2 Batched tracked-package save

Replace one existing map checkpoint with `save_packages`, not the entire bake. Record save-call count, checkpoint time, failures, memory, and subsequent verification. Promote checkpoint-by-checkpoint.

#### P2.3 First-party package reload for character verification

Test `reload_packages` before writing new C++. Compare every current fresh-verifier invariant and deliberately inject one bad dependency. Keep the second process until parity is proven.

#### P2.4 Font generation under `-run=pythonscript`

Run only the current font generator in a commandlet, with the same factory settings and post-save validation. Verify face loading, composite font data, cooked/runtime rendering, and no hidden Slate requirement. If it passes, fold it into `build_content.py` and reduce full content generation from two launches to one.

### P3 — Revisit only with new evidence

- Direct `UInterchangeManager` orchestration.
- Interchange replacement for bespoke OBJ/glTF/recovered-geometry construction.
- Deferred `PostEditChange` with a shared finalization barrier.
- Parallel Unreal authoring through Batch Process.
- Broad `EditorAssetLibrary` to subsystem migration.
- JsonObjectGraph as anything beyond diagnostic output.

## 7. APIs and approaches to keep

### 7.1 Skeletal meshes and animation data

Keep the editor-only C++ builder. Its use of mesh descriptions, skeleton merge/compatibility, animation data controllers, and resampling matches Unreal's native asset model and is not replicated by the generic import path for `.eskm` plus recovered sidecars.

The existing fresh-process verification boundary is also valuable: it distinguishes successful in-memory construction from a package that can be loaded and traversed after serialization. Remove it only after §4.5 parity is demonstrated.

### 7.2 Niagara and cloth

Keep the current C++ seams around non-reflected Niagara external edit utilities, explicit compilation completion, and cloth asset construction. Python should orchestrate these helpers and validate their outputs, not reproduce private/editor-only engine mechanics.

### 7.3 T3D AnimBP source

Keep the T3D graph as a source-controlled representation while it remains the most reviewable and stable way to express the graph. The process change proposed here concerns orchestration, not a rewrite of the graph authoring format.

### 7.4 Explicit registry and save boundaries

Keep synchronous registry scans where a later stage must immediately discover newly written packages. Remove a scan only after proving that the producer already registers the asset and every consumer sees it without timing dependence.

Keep explicit save checkpoints. Optimize their implementation or batching only inside the existing receipt/failure boundary.

## 8. Default target shape

After P0 and P1, the default policy-content path should look like this:

```text
offline export / recipes
        |
        v
one Unreal commandlet
  build_content.py
    - import-safe ordered generators
    - centralized import task policy
    - explicit result/type validation
    - player AnimBP generation
    - tracked save/checkpoint failures
        |
        v
full-editor font phase
  retained until commandlet spike passes
        |
        v
fresh verification where package reload parity is not proven
```

The longer-term one-process content path is conditional, not the default recommendation:

```text
one Unreal commandlet
  all policy generators, player AnimBP, and fonts
```

Character/map bakes remain separately scoped workflows with their existing lane, lock, receipt, and verifier rules. Process consolidation does not authorize concurrent authorship of the same generated mount.

## 9. Smallest experiments and pass/fail gates

| Experiment | Scope | Must pass | Promote when |
|---|---|---|---|
| Texture pipeline override | Four focused textures: NPOT albedo, semantic normal, forced reimport, cached no-op | Path/type, dimensions, color/compression policy, save/reload, receipt behavior | All semantics match and no user/default import state leaks in. |
| AnimBP process merge | Policy content plus player AnimBP only | Same package/graph verification and failure status | Process count is 3 → 2 with no output difference. |
| In-place mesh | One representative map slice | Sections/materials, collision, Nanite, UVs, references, fresh load | Identity benefit is real and stage time/memory is equal or better. |
| Batch save | One existing checkpoint | Same saved package set, failure diagnostics, receipts, reload | Checkpoint time improves beyond noise without larger failure/memory cost. |
| Package reload verify | One forced character, one morph body, one animation bank | All fresh-verifier invariants plus injected-failure detection | Full representative slice has parity and stable residency. |
| Commandlet fonts | Font generator only | Font faces, composite data, save/reload, runtime render smoke | No Slate/full-editor-only dependency; then process count is 2 → 1. |

Do not use a full corpus run as the first test for any proposal. Begin with the smallest selector that exercises the relevant contract, then expand only after it passes.

## 10. Repository implementation map

Primary orchestration and shared code:

- `pipeline/src/elysium_pipeline/unreal.py` — Unreal launch topology.
- `pipeline/unreal/build_content.py` — ordered policy-content commandlet runner.
- `pipeline/unreal/bake_lib.py` — texture import, static-mesh authoring, save and registry helpers.
- `pipeline/unreal/bake_map.py` — map stage ownership, receipts, checkpoints, and verification.
- `pipeline/unreal/bake_characters.py` — character units, package release, caches, and authoring orchestration.
- `pipeline/unreal/bake_wield.py` — wield package authoring and scoped release.
- `pipeline/unreal/bake_verify.py` and `pipeline/unreal/bake_verify_characters.py` — verification boundaries.
- `pipeline/unreal/make_ui_fonts.py` — full-editor font exception.
- `pipeline/unreal/make_player_anim_bp.py` — currently separate, proposed for orchestration merge.

Editor-only native seams:

- `Source/ElysiumUE/Private/Editor/ElysiumSkeletalBuild.cpp`
- `Source/ElysiumUE/Public/ElysiumSkeletalBuild.h`
- current Niagara and cloth editor builder implementations under `Source/ElysiumUE/Private/Editor`

Tests to extend rather than replace:

- focused pipeline `unittest` modules under `pipeline/tests`;
- existing character cache/worker tests;
- focused Unreal commandlet or editor verification for asset APIs that cannot be proven with stubs.

## 11. Final recommendation

Approve P0 and P1 as the validated implementation program. They improve determinism, composability, and process topology without changing asset semantics.

Authorize P2 only as four independent, measured spikes. The APIs exist and are appropriate candidates, but the document should not book their wall-time or memory gains in advance. Preserve the current implementation as the comparison path until each gate passes.

Do not spend pipeline time on the rejected rewrites. The measured map profile points at static-mesh authoring, compilation waits, and save cadence—not at the presence of Python, the absence of generic Interchange import, or the editor-only C++ seams.

The resulting strategy is conservative in the useful sense: keep recovered contracts and verification boundaries stable, remove redundant orchestration, make import behavior explicit, and promote engine API changes only with targeted evidence.

## 12. Official Epic references consulted

The API and status claims in this review were checked against the UE 5.8 documentation and the installed UE 5.8 headers/source. The primary public references are:

- [Scripting the Unreal Editor using Python](https://dev.epicgames.com/documentation/unreal-engine/scripting-the-unreal-editor-using-python)
- [Importing Assets Using Interchange](https://dev.epicgames.com/documentation/unreal-engine/importing-assets-using-interchange-in-unreal-engine)
- [Interchange Project Settings](https://dev.epicgames.com/documentation/unreal-engine/interchange-settings-in-the-unreal-engine-project-settings)
- [`UInterchangeManager` C++ API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/InterchangeEngine/UInterchangeManager)
- [`FImportAssetParameters` C++ API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/InterchangeEngine/FImportAssetParameters)
- [`UAssetImportTask::GetObjects`](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/UnrealEd/UAssetImportTask/GetObjects)
- [Geometry Scripting Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/geometry-scripting-reference-in-unreal-engine)
- [`Copy Mesh to Static Mesh`](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/GeometryScript/StaticMesh/CopyMeshtoStaticMesh)
- [`EditorLoadingAndSavingUtils` Python API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorLoadingAndSavingUtils)
- [`SavePackages` C++ API](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/UnrealEd/UEditorLoadingAndSavingUtils/SavePackages)
- [`Reload Packages` editor scripting API](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/EditorScripting/EditorLoadingAndSaving/ReloadPackages)
- [`SystemLibrary` Python API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/SystemLibrary)
- [CoreUObject `CollectGarbage`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/CoreUObject/CollectGarbage)
- [Unreal Engine 5.8 Release Notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes)

Where the public documentation does not describe reflection or implementation details precisely—module-level Python garbage collection, `UNiagaraExternalEditUtilities`, Interchange task routing, and `RF_Standalone` package release—the installed UE 5.8 source is the deciding evidence. Those details must be rechecked when the engine version changes.
