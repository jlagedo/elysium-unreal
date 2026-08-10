# Offline pipeline (`pipeline/`)

The pipeline decodes the user's VtMB installation into engine-neutral files under
`ELYSIUM_EXPORT_ROOT`. It is never imported or invoked by the running game.
The embedded CPython runtime is a separate game-logic system.

## Layout

- `src/elysium_pipeline/formats/` owns file and container parsers.
- `src/elysium_pipeline/exporters/` owns map and whole-game export products.
- `src/elysium_pipeline/enhancement/` owns optional offline surface processing.
- `src/elysium_pipeline/validation/` owns deterministic comparators and synthetic checks.
- `src/elysium_pipeline/devtools/` owns fetched SDK and local editor helpers.
- `unreal/` contains editor-only generators and the `/ElysiumBaked` map bake.
- `tests/fixtures/synthetic/` is the only location for committed fixtures; fixtures must be
  game-independent.

`uv run elysium` is the public Python entrypoint. Internal exporters and Unreal editor
scripts are library or worker surfaces called by that command. Imports are absolute
`elysium_pipeline.*` imports. No module constructs a repository-relative output path,
mutates `sys.path`, or assumes a current directory.

## Tests

Python tests are `unittest`, and pytest is not installed. Run one module from the repo
root as `uv run python -m unittest pipeline.tests.<module>`; `unittest discover -s
pipeline/tests` fails because that directory is not an importable package.

`sqlite3.connect()` used as a context manager commits but does not close. On Windows the
open handle blocks `TemporaryDirectory` cleanup, so a test that opens a session database
closes it explicitly or fails in teardown with `PermissionError` rather than on the
assertion it was making.

## Path contract

`elysium_pipeline.paths` is the only offline path resolver:

- `ELYSIUM_VTMB_ROOT` is the read-only user install.
- `ELYSIUM_WORK_ROOT` contains exports, research output, cache, logs, and scratch state.
- `ELYSIUM_EXPORT_ROOT` optionally overrides `$ELYSIUM_WORK_ROOT/exports`.

VtMB and work roots have no repository-relative fallback. Resolution and user-facing
configuration are owned by `uv run elysium` and `.elysium.local.env`.

`elysium_pipeline.lanes` owns detached QA worktrees and their metadata;
`elysium_pipeline.workspace_lock` owns the cross-process generated-state lease. A lane's
ignored local environment points at `$ELYSIUM_WORK_ROOT/lanes/<lane>` so exports, logs,
reports, build output, generated packages, and dependency trees are never shared with the
development checkout. The UE and VtMB roots remain shared inputs.

## Coordinate contract

`formats/bsp.py` is the sole owner of `source_to_unreal` and
`source_dir_to_unreal`. The Source-to-Unreal Y reflection is paired with winding reversal
at export time. Runtime readers consume Unreal-native files verbatim and perform no
coordinate conversion.

An exporter with a `UE_` filename emits centimetres, Z-up, left-handed Unreal data with
the required winding. Every tracked coordinate-bearing OBJ/sidecar exporter follows this
rule; parsers and orchestration modules do not emit coordinate products. `mdl_gltf.py` is
the standing exemption because standard glTF 2.0 is self-describing and glTFRuntime
performs its import transform.

The skeletal **pose frame** is covered by the same contract, so `UE_mdl_skeletal.py` owes it
too: every emitted rotation is parent-relative and every additive names the clip it is a
difference from, leaving the runtime no VtMB rule to apply. The root `CLAUDE.md` rule
"Poses are baked native" states it and names its one exemption.

## Products

`UE_bsp_to_scene.py` writes per-map geometry and sidecars. The CLI task graph coordinates
the `grid` and `all` profiles, focused map or model exports, and the global mirrors.
Integrated NPC export adds skeletal meshes, animation banks, indexes, and facial sidecars.
These outputs are game-derived and never tracked.

`pipeline/unreal/` consumes pre-exported files in an editor process:

- `build_content.py` and `make_*.py` generate local `/Game/Elysium` and
  `/Game/VtMB/**` packages.
- `bake_map.py` generates local `/ElysiumBaked/<map>/**` packages.

The virtual package names are immutable contracts. The generated `.uasset` and `.umap`
files are ignored.

VtMB format and behavior facts belong in `docs/vtmb/`; Unreal designs belong in
`docs/architecture/`; task status belongs only in `docs/project/roadmap.md`.
