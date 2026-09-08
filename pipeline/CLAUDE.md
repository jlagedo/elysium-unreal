# Offline pipeline (`pipeline/`)

The pipeline decodes the user's VtMB installation into engine-neutral files under
`ELYSIUM_EXPORT_ROOT`. It is never imported or invoked by the running game.
The embedded CPython runtime is a separate game-logic system.

No module constructs a repository-relative output path or assumes a current directory.

Python tests run from the repository root as `uv run pytest`.

## Path contract

`elysium_pipeline.paths` is the only offline path resolver, and `ELYSIUM_EXPORT_ROOT` optionally
overrides `$ELYSIUM_WORK_ROOT/exports`.

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

`pipeline/unreal/` consumes pre-exported files in an editor process.

`make_player_anim_bp.py` calls `main()` at module scope with no `__main__` guard, so importing it
— to reuse a helper, or to inspect it — rebuilds and saves the Animation Blueprint as a side
effect of the import.

## V2 pipeline map bake procedure

```
uv run elysium export_v2 map-glb <map>
# add <map> to MapsOnV2Models and MapsOnNewTransport in Config/DefaultElysium.ini
uv run elysium export map <map> --intermediate-only   # once: the sidecars the lane still reads
uv run elysium bake map --maps <map>
uv run elysium import map-entities    --maps <map>
uv run elysium import map-collision   --maps <map>
uv run elysium import map-environment --maps <map>
uv run elysium verify
```

`MapsOnV2Models` selects `MapBakeV2` over the legacy `Bake`; `bake map` refuses a map that is
not listed. `export map <map>` (no flag) reaches the same bake through the legacy decode plus
an unconditional re-verification of the character and catalogue corpora, ~20 minutes.
