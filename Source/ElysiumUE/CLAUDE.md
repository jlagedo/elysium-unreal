# Elysium — the C++ runtime (`Source/ElysiumUE/`)

## Gotchas

- **`Cog` and `glTFRuntime` are the vendored plugins** (`Plugins/External/`). Cog is an MIT debug-UI shell, stripped from Shipping via `ENABLE_COG`.
- The **CPython 2.7.18 SDK is fetched, not committed** — `pipeline/src/elysium_pipeline/devtools/fetch_cpython27.py` writes `ThirdParty/CPython27/`, which is gitignored and gated by `ELYSIUM_WITH_CPYTHON`, Win64 only.
- `Private/` is subfoldered **by layer**; `Public/` stays flat, because it is the module's API surface rather than a layering. Types are named `FElysium*` (plain-C++ substrate and value types), `UElysium*`/`AElysium*` (the Unreal half) and `IElysium*` (the service seam) — grep is the index.
- **`Config/DefaultInput.ini` holds engine-side input settings and the GameInput device layer only** — no action or axis mappings live there. `UElysiumInputSubsystem` loads and applies the `UInputMappingContext` assets and `UElysiumInputRouter` binds the actions, so grepping the ini for a binding finds nothing.
- `FElysiumContentPaths::Root()` is the pipeline's `$ELYSIUM_EXPORT_ROOT` mount point.

## Tests

- Every tier runs `-nullrhi`, so **no test covers a rendered frame**; the pixel comparison is `validation/shots_diff.py` against a baseline under `$ELYSIUM_EXPORT_ROOT/_shots/_baseline/`, which is a local instrument outside the automation run.

## Engine gotchas

Hard-won, non-obvious, and easy to undo. The ones belonging to the animation bake, its readers and the graph are documented beside their design instead:

- **A Live Coding patch exists only in the editor process that compiled it.** `CompileLiveCoding` links a `UnrealEditor-ElysiumUE.patch_N.dll` into the running editor, and the on-disk module is untouched — so a commandlet, a headless test run or a fresh editor launched alongside it all load code that predates the patch. New `UPROPERTY`s are the sharp edge: a graph or asset authored live against them regenerates against a class that does not have them, and the properties resolve to nothing while the run still reports success. Anything proven live is proven again after `uv run elysium build`, which needs the editor closed because it holds the module open.
- **The vendored Cog shell carries two local interaction patches** — one in `FCogImguiContext::SetEnableInput`, one in `UCogSubsystem::RenderMenuItem`. Both are marked in the vendored source and both are lost on a Cog update; reapply them.
- **Cog boots dormant** (`elysium.CogPersist 0` deletes its layout ini before the dependency brings Cog up) — restored input capture makes the game's own UI unclickable otherwise.
- **An `APawn`'s owner is not permanent.** `AController::Possess` overwrites the pawn's owner with the controller, and `AElysiumNpcBody` possesses lazily on its first accepted travel order — so anything read back off `GetOwner()` answers only for a body that has never moved. The body holds its `AElysiumMapActor` directly (`SetOwningEntity`).
- **Activatable screens enter through `UElysiumPlayerUISubsystem` containers.** Adding one straight to a viewport bypasses CommonUI activation, Back routing and focus restoration; `FElysiumUICompositionPolicyTest` greps the tree and rejects direct insertion outside the one root owner.
- **The player hull is a box, not a capsule** — `StepMove` depends on a flat bottom, and `ACharacter` will not take a box root.
- **A `UCharacterMovementComponent` starts in `MOVE_None` and only a controller ever changes that.** `MovementMode` has no constructor initializer, so it is zero-initialised, and the walking mode is set by `ACharacter::Restart()` on possession. `AElysiumNpcBody` disables auto-possession and spawns its controller lazily on the first accepted `MoveTo`, so a body that only ever stands never gets one. `IsMovingOnGround()` reads the mode alone — not `IsActive()` — so such a body reports itself airborne for its whole life while standing on the floor, and anything reading the locomotion sample's grounded flag believes it. Note the asymmetry that makes this easy to misdiagnose: `Deactivate()` never touches `MovementMode`, so a body that has moved even once stays correctly grounded forever after, and only the never-moved background cast is affected.
- **An NPC movement tick already depends on the map actor while its character stands on map-owned collision** — CharacterMovement wires the primary tick of the movement base's owner, which is `AElysiumMapActor::PrimaryActorTick`. GameFrame is therefore a separate node (`GameplayTickFunction`) hanging off that barrier, which is what leaves room for the motors between them; adding a motor tick as a prerequisite of `PrimaryActorTick` closes a cycle and floods `LogTick`.
- **A sheet recompute needs BOTH the rules and the effect layer** — `RecomputeCurrent(Stats)` still compiles (the effect argument defaults to null) and silently drops every clan bane and gift. On a character, go through `FElysiumCombatCharacter::RecomputeSheet()`, which passes both and re-derives the `health` keyfields after.
- **`IElysiumNpcMotor::SetEnabled(false)` also hides the body, so it cannot immobilise a cutscene actor.** A choreographed scene's `position_start` cast has to stop moving while staying on camera, which is what `SetFrozen` is for; `SetIgnoreCharacterCollision` is the separate character-vs-character switch a `scripted_sequence` beat borrows. Collapsing any of the three into the others makes a scene's cast vanish. Each setter resolves the whole state across `ApplyEnabledState` (hidden), `ApplyCollisionState` (solidity and the pawn channel) and `ApplyCrowdState` (avoidance), so a new caller goes through a setter rather than touching the capsule directly.

- **A live run is proposed, never assumed** — ask the owner first, with a recommendation.
not a console variable.
