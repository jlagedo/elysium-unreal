# Offline pipeline (`pipeline/`)

The pipeline decodes the user's VtMB installation into engine-neutral files under
`ELYSIUM_EXPORT_ROOT`. It is never imported or invoked by the running game.
The embedded CPython runtime is a separate game-logic system.

No module constructs a repository-relative output path or assumes a current directory.

## Tests

Python tests are `unittest`, and pytest is not installed. Running one module, choosing a scope,
and the approvals a broad export/bake/reconstruct run needs are the **`elysium-testing`** skill.

`tests/fixtures/synthetic/` is the only location for committed fixtures; fixtures must be
game-independent.

`elysium_pipeline.formats.install` resolves `ELYSIUM_VTMB_ROOT` at **import time**, so importing
any format or exporter module needs a configured install — and `uv run python` does not load
`.elysium.local.env`; only the `elysium` CLI does. A test module on that chain fails at collection
with `RuntimeError: ELYSIUM_VTMB_ROOT is not configured`, which reads as a broken test rather than
an unconfigured shell.

`sqlite3.connect()` used as a context manager commits but does not close. On Windows the
open handle blocks `TemporaryDirectory` cleanup, so a test that opens a session database
closes it explicitly or fails in teardown with `PermissionError` rather than on the
assertion it was making.

## Path contract

`elysium_pipeline.paths` is the only offline path resolver, and `ELYSIUM_EXPORT_ROOT` optionally
overrides `$ELYSIUM_WORK_ROOT/exports`. VtMB and work roots have no repository-relative fallback.
Resolution and user-facing configuration are owned by `uv run elysium` and `.elysium.local.env`.

`elysium_pipeline.workspace_lock` owns the cross-process generated-state lease and the
Unreal-process liveness check that guards it.

## Coordinate contract

`formats/bsp.py` is the sole owner of `source_to_unreal` and
`source_dir_to_unreal`. The Source-to-Unreal Y reflection is paired with winding reversal
at export time. Runtime readers consume Unreal-native files verbatim and perform no
coordinate conversion.

An exporter with a `UE_` filename emits centimetres, Z-up, left-handed Unreal data with
the required winding. Every tracked coordinate-bearing OBJ/sidecar exporter follows this
rule; parsers and orchestration modules do not emit coordinate products.

Standard glTF 2.0 is self-describing and carries the exemption. A GLB seam — a
`formats/<seam>_glb/` package with its `exporters/<seam>_glb.py` and a per-unit plus a
whole-corpus `export_v2` command — writes below its own isolated `$ELYSIUM_EXPORT_ROOT/glb/`
root and takes no `UE_` prefix; `formats/mdl_gltf.py` writes the single-clip model inspection
product. Nothing the game loads reads a GLB product and no profile export writes one.

Every sidecar is Unreal-native, including the `eyes/` and `procedural/` tables, which
`UE_mdl_skeletal.py` states in the body's own frame from the Source-space records
`mdl_skel.py` parses.

The skeletal **pose frame** is covered by the same contract, so `UE_mdl_skeletal.py` owes it
too: every emitted rotation is parent-relative and every additive names the clip it is a
difference from, leaving the runtime no VtMB rule to apply.

## Products

Export products are game-derived and never tracked. `pipeline/unreal/` consumes pre-exported
files in an editor process.

`make_player_anim_bp.py` calls `main()` at module scope with no `__main__` guard, so importing it
— to reuse a helper, or to inspect it — rebuilds and saves the Animation Blueprint as a side
effect of the import.

The virtual package names are defined once in `elysium_pipeline.mounts`, the single source of truth
every generator and bake resolves its packages against; renaming one is a deliberate change guarded
by `pipeline/tests/test_profile_package_contract.py::test_virtual_package_roots_remain_immutable`.
