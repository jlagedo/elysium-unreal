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

QA starts with the smallest owning test method or module and the smallest export/bake selector.
An exporter or Unreal-generator change does not authorize a complete profile as validation. If
the requested change affects export or bake products and the owner has not named the scope, ask
for the exact map, model, placed model, NPC/body stem, bundle, or generator before launching it.

Never invoke `reconstruct`, `export grid|all`, unscoped `export characters`, a full policy/cast
bake, broad `--force`/`--clean`, or the complete Python suite without first stating the command,
scope, reason, and expected cost and receiving explicit owner acceptance. Use focused commands and
receipt-backed no-op checks during iteration; broader release acceptance is a separately approved
step.

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

`elysium_pipeline.task_worktrees` owns mutable detached agent-task worktrees and their
primary-checkout command boundary. `elysium_pipeline.lanes` owns immutable detached QA
worktrees and their evidence metadata; `elysium_pipeline.workspace_lock` owns the
cross-process generated-state lease. Their ignored local environments point at dedicated
children of `$ELYSIUM_WORK_ROOT/worktrees/` or `$ELYSIUM_WORK_ROOT/lanes/`, so exports, logs,
reports, build output, generated packages, and dependency trees are never shared with the
primary checkout. The UE and VtMB roots remain shared inputs.

## Coordinate contract

`formats/bsp.py` is the sole owner of `source_to_unreal` and
`source_dir_to_unreal`. The Source-to-Unreal Y reflection is paired with winding reversal
at export time. Runtime readers consume Unreal-native files verbatim and perform no
coordinate conversion.

An exporter with a `UE_` filename emits centimetres, Z-up, left-handed Unreal data with
the required winding. Every tracked coordinate-bearing OBJ/sidecar exporter follows this
rule; parsers and orchestration modules do not emit coordinate products. `mdl_gltf.py` is
the standing exemption: standard glTF 2.0 is self-describing and its `.glb` output is an
inspection product that nothing the game loads reads. Every sidecar is Unreal-native,
including the `eyes/` and `procedural/` tables, which `UE_mdl_skeletal.py` states in the
body's own frame from the Source-space records `mdl_skel.py` parses.

The skeletal **pose frame** is covered by the same contract, so `UE_mdl_skeletal.py` owes it
too: every emitted rotation is parent-relative and every additive names the clip it is a
difference from, leaving the runtime no VtMB rule to apply. The root `CLAUDE.md` rule
"Poses are baked native" states it and names its one exemption.

## Products

`UE_bsp_to_scene.py` writes per-map geometry and sidecars. The CLI task graph coordinates
the `grid` and `all` profiles, focused map or model exports, and the global mirrors.
Integrated NPC export adds skeletal meshes, animation banks, indexes, and facial sidecars.
`UE_extract_items.py` decodes every `vdata/items` ground model into the shared `items/props/`
corpus with a `ground_models.json` manifest; `UE_extract_wield.py` writes the wield-model corpus —
`items/wield_models.json`, one `.eskm` per real wield model with its reference pose at the model's
own clip frame 0, and the corpus's role-typed texture closure — with `wield_corpus.py` owning the
decisions both halves share. The `items` bundle covers all of it. These outputs are game-derived
and never tracked.

`pipeline/unreal/` consumes pre-exported files in an editor process:

- `build_content.py` and `make_*.py` generate local `/Game/Elysium` and
  `/Game/VtMB/**` packages.
- `bake_map.py` generates local `/ElysiumBaked/<map>/**` packages.
- `bake_wield.py` (`uv run elysium export wield`) generates local `/ElysiumBaked/Items/Wield/**`
  and `/ElysiumBaked/Items/DA_WieldModels` from the wield manifest, instancing from the
  `M_Wield*` masters `make_wield_materials.py` generates, and verifies each mesh's reference
  pose against its `.eskm` in the same run.

`make_player_anim_bp.py` calls `main()` at module scope with no `__main__` guard, so importing it
— to reuse a helper, or to inspect it — rebuilds and saves the Animation Blueprint as a side
effect of the import.

The virtual package names are immutable contracts. The generated `.uasset` and `.umap`
files are ignored.

VtMB format and behavior facts belong in `docs/vtmb/`; Unreal designs belong in
`docs/architecture/`; task status belongs only in `docs/project/roadmap.md`.
