# Elysium — the C++ runtime (`Source/ElysiumUE/`)

## Layout

- `Private/` is subfoldered by layer; `Public/` is flat.
- Types: `FElysium*` (substrate and value types), `UElysium*`/`AElysium*` (Unreal half), `IElysium*` (service seam).
- `FElysiumContentPaths::Root()` is the pipeline's `$ELYSIUM_EXPORT_ROOT` mount point.
- `Cog` (MIT debug UI, stripped from Shipping via `ENABLE_COG`) and `glTFRuntime` are vendored in `Plugins/External/`.
- The CPython 2.7.18 SDK is fetched by `pipeline/src/elysium_pipeline/devtools/fetch_cpython27.py` into gitignored `ThirdParty/CPython27/`, gated by `ELYSIUM_WITH_CPYTHON`, Win64 only.

## Gotchas

- `Config/DefaultInput.ini` holds engine-side input settings and the GameInput device layer only. Action and axis bindings live in `UInputMappingContext` assets, loaded by `UElysiumInputSubsystem` and bound by `UElysiumInputRouter`.
- A Live Coding patch exists only in the editor process that compiled it; commandlets, headless tests and fresh editors load the on-disk module. New `UPROPERTY`s resolve to nothing there while the run still reports success — reprove anything live after `uv run elysium build` (needs the editor closed).
- The vendored Cog shell carries two local patches, in `FCogImguiContext::SetEnableInput` and `UCogSubsystem::RenderMenuItem`; both are marked in source and lost on a Cog update.
- Cog boots dormant (`elysium.CogPersist 0` deletes its layout ini) — restored input capture makes the game's own UI unclickable.
- An `APawn`'s owner is not permanent: `AController::Possess` overwrites it, and `AElysiumNpcBody` possesses lazily on its first travel order. Read the map actor from `SetOwningEntity`, not `GetOwner()`.
- A `UCharacterMovementComponent` starts in `MOVE_None`; only `ACharacter::Restart()` on possession sets walking. A never-moved `AElysiumNpcBody` has no controller, so `IsMovingOnGround()` reports it airborne while it stands on the floor. `Deactivate()` leaves `MovementMode` alone, so a body that moved once stays grounded.
- CharacterMovement wires an NPC's movement tick to `AElysiumMapActor::PrimaryActorTick`. GameFrame is a separate node (`GameplayTickFunction`) off that barrier; making a motor tick a prerequisite of `PrimaryActorTick` closes a cycle and floods `LogTick`.
- `RecomputeCurrent(Stats)` compiles with the effect layer defaulted to null and silently drops every clan bane and gift. Use `FElysiumCombatCharacter::RecomputeSheet()`.
- `IElysiumNpcMotor::SetEnabled(false)` also hides the body. `SetFrozen` immobilises a cutscene actor on camera; `SetIgnoreCharacterCollision` is the character-vs-character switch. Each setter resolves the whole state via `ApplyEnabledState`/`ApplyCollisionState`/`ApplyCrowdState` — never touch the capsule directly.
- Activatable screens enter through `UElysiumPlayerUISubsystem` containers; adding one to a viewport bypasses CommonUI activation, Back routing and focus restoration, and `FElysiumUICompositionPolicyTest` rejects it.
- The player hull is a box, not a capsule — `StepMove` needs a flat bottom and `ACharacter` will not take a box root.

## Tests

- Every tier runs `-nullrhi`, so no test covers a rendered frame. Pixel comparison is `validation/shots_diff.py` against a baseline under `$ELYSIUM_EXPORT_ROOT/_shots/_baseline/`, run locally outside automation.
