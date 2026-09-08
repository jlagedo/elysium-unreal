# Niagara through the editor MCP — what cost a session to learn

Everything below was measured on this project (UE 5.8, R7.3 floor build). Nothing here is in the
engine docs.

## Scope — what this toolset is for

The NiagaraToolsets MCP is an **inspection and one-off tweak tool for authored assets**
(`Content/ElysiumAuthored/VFX/**`: the hero systems, the base emitters, the module scripts). It
is Experimental, it has no compile call and no batch, every call rebuilds a full system view
model, and it cannot touch lightweight emitters or Niagara Data Channels. It is **not** the way
shipping content is produced: the per-root `NS_<root>` systems are generated headlessly by the
bake (below), and nothing under `Content/ElysiumGenerated/VFX/` is ever touched through this
toolset. Owner call 2026-09-03.

## Headless lane — the generator

`pipeline/unreal/make_particle_systems.py` → `UElysiumParticleAssetBuilder` composes `NS_<root>`
without opening the Niagara editor. Two rules it must keep, both from the failures below:

- **Compile explicitly**: `UNiagaraSystem::RequestCompile(false)`, then
  `FAssetCompilingManager::Get().FinishAllCompilation()`. The toolset's "open the editor once"
  finalize is a workaround for exactly this and is not available headless.
- **Gate on `IsReadyToRun()`** before saving, and on a non-zero particle count after activation.
  Without the gate a system that never compiled looks identical to one that works: `Activate`
  defers silently with no log at any verbosity.

## Session rules

- One toolset call per MCP request. Never loop calls inside an in-editor Python command: the editor
  cannot tick or GC inside one, and the first attempt died at 22 GB that way.
- Every call rebuilds the whole `FNiagaraSystemViewModel` and lands in the undo buffer. Cost grows
  with the system: ~7 MB/call on one emitter, ~25 MB/call on twenty, and GC reclaims none of it.
  Save per milestone, read the working set, restart the editor near 13 GB. Lower `[Undo]
  UndoBufferSize` in `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini` for a build
  session.
- Edit editor settings with the editor closed; it rewrites its settings file on exit.
- The editor throttles to ~3 fps in the background
  (`[/Script/UnrealEd.EditorPerformanceSettings] bThrottleCPUWhenNotForeground=False`) and the
  editor world does not simulate particles unless the viewport is realtime. Witness in
  Simulate-in-Editor (`EditorAppToolset.StartPIE`, `bSimulate`). Saving is refused while it runs.
  The witness level's **World Settings must override the GameMode to `GameModeBase`**: the
  project's `ElysiumGameMode` travels to Boot on begin play, so Simulate-in-Editor leaves the
  witness level and you capture the wrong world.
- After a forced kill, delete `Saved/Autosaves/PackageRestoreData.json` or the next launch hangs
  on the recovery prompt at frame 0.
- Measure before looking: `fx.Niagara.DumpComponents` (particle counts per emitter),
  `elysium_log_tail` category `Niagara`, `GetSystemCompileState`, `GetStackIssues`.
  `EditorAppToolset.CaptureViewport` renders a fresh frame with no HUD; a black frame proves nothing.
- Build scripts: journal every step, run in the background so a shell timeout cannot kill an
  editor relaunch, raise the client timeout past 120 s once the system is large. A working client
  is `E:/elysium-work/scratch/r73/mcpclient.py` (reuses the proxy's `Downstream`).

## Toolset facts

- `AddUserVariables` silently drops data-interface parameters (engine: "not supported").
  Create a DI user parameter by linking an input to it
  (`StackInputData_Linked`, `linkedVariable.name = "User.X"`); the parameter appears.
- `describe_toolset` on the Niagara toolset is 280 KB; grep the saved file, never read it.
- Stack refs: `scriptName` is the `ENiagaraScriptUsage` name; Set Parameters modules are named
  `SetVariables_<guid>` and keep their names when an emitter is cloned.
- `AddEmitter` accepts an in-system emitter object as template
  (`refPath ".../NS_X.NS_X:Leaf00_0"`): a full clone, module names intact, so retargeting a clone
  is a fixed list of `SetStackInputData` calls.
- Inputs behind a static switch or edit condition refuse writes until the switch is set. Set the
  enum first, then the gated inputs (`Interpolate` → `Normalized Interpolation`, `Interpolation`).
- `SetRendererData` bindings need `registeredTypeIndex` numbers: float 99, int 101, vec2 103,
  color 106, texture 1 on this engine build. Read a fresh renderer's JSON for the rest.
- Module output names resolve in expressions as `Output.<ModuleName>.<Output>`.

## Niagara behaviour

- An expression can read any parameter but cannot call a data-interface function, and the toolset
  cannot author custom HLSL. Array reads go through
  `/Niagara/DynamicInputs/Arrays/SelectVectorFromArray`; its `Interpolate` mode with
  `Normalized Interpolation = false` takes an absolute fractional index. Hence lookup-table ramps.
- A bool an expression writes into `Emitter.*`/`Particles.*` does not read back true in a later
  expression. Carry gates as int (`? 1 : 0`, test `> 0`).
- A double `Vector` user parameter read in an expression asserts the translator
  (`IsLWCType`) and kills the editor. Use `Vector3f`.
- `SampleParticlesFromOtherEmitter` / `UpdateParticlesFromOtherEmitter` set
  `DataInstance.Alive = false` on an invalid sample. A Set Parameters entry may write
  `DataInstance.Alive`; reassert it after the module.
- A particle reader never resolves from a **user** parameter at runtime: the component copies
  every user DI onto itself and `ResolveHandle` looks the emitter up through the DI's outer
  system, so the log says `Source emitter 'X' not found` whatever the name. Make it an
  emitter-level value on a Set Parameters entry:
  `StackInputData_DataInterface`, `propertyValues = '{"EmitterBinding":{"BindingMode":"Other","EmitterName":"Leaf00"}}'`
  (the emitter **handle** name; `Self` is invalid outside a particle script). Per-instance
  parents do not exist — which is why the parent/child relation is written **into the generated
  asset** as an emitter name at compose time, not chosen per component at runtime.
- `Emitter.LoopCount`, `Emitter.NormalizedLoopAge` need `Loop Behavior = Multiple` with a
  `Loop Count`; `Infinite` hides `Loop Duration Mode`.
- **A system built through the toolset never activates until it is opened once in the Niagara
  editor.** Toolset calls run on a throwaway view model with auto-compile off; only
  `CreateNiagaraSystem` and `AddEmitter` request a compile, and `GetSystemCompileState` reads the
  stale cached status, so it says `UpToDate` while the bytecode predates the edits.
  `UNiagaraComponent::Activate` then defers silently (`bAwaitingActivationDueToNotReady`, no log
  at any verbosity). Finalize every build:
  `AssetEditorSubsystem.open_editor_for_assets([system])`, poll `GetSystemCompileState` until
  `bIsCompiling` is false, `close_all_editors_for_asset`, save. Probe with a spawned
  NiagaraActor: `activate(True); is_active()` is a reliable editor-world check.
- `SetModuleEnabled` off/on does not force that compile either.
- Sprite emissive near 0.2 is invisible under editor exposure. Witness with `User.Tint` boosted;
  intensity is the material lane's call.
- Niagara compile requests coalesce (all but the oldest active compile are aborted), so a burst
  of edits does not pile up compiles; the memory problem is the view model and undo, not compiles.
