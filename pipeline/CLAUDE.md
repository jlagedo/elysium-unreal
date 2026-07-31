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

All standalone Python entrypoints run as modules through `dev/elysium.ps1`. Imports are
absolute `elysium_pipeline.*` imports. No module constructs a repository-relative output
path, mutates `sys.path`, or assumes a current directory.

## Path contract

`elysium_pipeline.paths` is the only offline path resolver:

- `ELYSIUM_VTMB_ROOT` is the read-only user install.
- `ELYSIUM_WORK_ROOT` contains exports, research output, cache, logs, and scratch state.
- `ELYSIUM_EXPORT_ROOT` optionally overrides `$ELYSIUM_WORK_ROOT/exports`.

VtMB and work roots have no repository-relative fallback. Resolution and user-facing
configuration are owned by `dev/elysium.ps1` and `.elysium.local.env`.

## Coordinate contract

`formats/bsp.py` is the sole owner of `source_to_unreal` and
`source_dir_to_unreal`. The Source-to-Unreal Y reflection is paired with winding reversal
at export time. Runtime readers consume Unreal-native files verbatim and perform no
coordinate conversion.

An exporter with a `UE_` filename emits centimetres, Z-up, left-handed Unreal data with
the required winding. A non-`UE_` exporter is not safe to consume as Unreal-native until
reviewed and renamed with all callers and documentation updated. `mdl_gltf.py` is the
standing exemption because standard glTF 2.0 is self-describing and glTFRuntime performs
its import transform.

## Products

`UE_bsp_to_scene.py` writes per-map geometry and sidecars. `export_all.py` orchestrates
maps and global mirrors; `--npc` adds skeletal meshes, animation banks, indexes, and
facial sidecars. These outputs are game-derived and never tracked.

`pipeline/unreal/` consumes pre-exported files in an editor process:

- `build_content.py` and `make_*.py` generate local `/Game/Elysium` and
  `/Game/VtMB/**` packages.
- `bake_map.py` generates local `/ElysiumBaked/<map>/**` packages.

The virtual package names are immutable contracts. The generated `.uasset` and `.umap`
files are ignored.

VtMB format and behavior facts belong in `docs/vtmb/`; Unreal designs belong in
`docs/architecture/`; task status belongs only in `docs/project/roadmap.md`.
