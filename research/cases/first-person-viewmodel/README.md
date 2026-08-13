# First-person viewmodel research

This case closes the first-person viewmodel boundary across the three retail
modules that own it. Generated decompilation, indexes, the deterministic asset
survey and live captures remain below
`$ELYSIUM_WORK_ROOT/research/first-person-viewmodel/`; only the reproduction
specifications and instruments are tracked.

The static pass is reproduced independently for each pinned binary:

```powershell
uv run elysium research first-person-viewmodel research/cases/first-person-viewmodel/specs/vampire_viewmodel.json --dry-run
uv run elysium research first-person-viewmodel research/cases/first-person-viewmodel/specs/vampire_viewmodel.json `
  --binary "$env:ELYSIUM_VTMB_ROOT/Vampire/dlls/vampire.dll"

uv run elysium research first-person-viewmodel research/cases/first-person-viewmodel/specs/client_viewmodel.json --dry-run
uv run elysium research first-person-viewmodel research/cases/first-person-viewmodel/specs/client_viewmodel.json `
  --binary "$env:ELYSIUM_VTMB_ROOT/Vampire/cl_dlls/client.dll"

uv run elysium research first-person-viewmodel research/cases/first-person-viewmodel/specs/engine_viewmodel.json --dry-run
uv run elysium research first-person-viewmodel research/cases/first-person-viewmodel/specs/engine_viewmodel.json `
  --binary "$env:ELYSIUM_VTMB_ROOT/Bin/engine.dll"
```

The patch-first data census is a separate deterministic input:

```powershell
uv run elysium research viewmodel_surface_survey
```

It writes `viewmodel-surface.json` under the case's work-root directory and
asserts 21 hand models, both Tremere shield variants, 17 packed viewmodels and
12 firearm families. Missing authored references remain diagnostics; in
particular the Gangrel female clandoc spelling is not silently repaired.

The live pass uses the hash-gated native harness and raw `ELGVM1` records:

```powershell
uv run elysium research retail_capture_native test
$scenario = "tremere-normal-glock"
uv run elysium research capture_first_person_viewmodel --scenario $scenario --prepare
# Start the pinned retail build in the controlled test save, then:
uv run elysium research capture_first_person_viewmodel --scenario $scenario --attach
uv run elysium research capture_first_person_viewmodel --scenario $scenario --recipe
# Perform the printed actions, then flush and unload the hook:
uv run elysium research capture_first_person_viewmodel --scenario $scenario --stop

# After every scenario printed by --list-scenarios has been captured:
uv run elysium research verify_first_person_viewmodel `
  "$env:ELYSIUM_WORK_ROOT/research/first-person-viewmodel/capture"
```

The required capture matrix records Tremere normal and shield hands with Glock and M37,
including draw, idle, fire, dry-fire, ordinary reload and M37's three reload
segments. Projection slices vary player FOV, `viewmodel_fov` and 4:3/16:9, and
include one `DrawViewmodel 0` transition. A capture is evidence only when the
verifier closes handle joins, paired draw submission, `Camera01` alignment,
projection parameters and server/client event ordering from the raw stream.
Screenshots are orientation aids, never the oracle.
