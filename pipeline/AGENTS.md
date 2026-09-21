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
uv run elysium export_v2 nav-graph-glb <map>
uv run elysium bake map --maps <map>
uv run elysium verify
```

**`bake map` is one command and it yields a loadable level** (0018 story 21-2). In one editor
session, per map: the geometry, the level's actors, `DA_<map>_Entities`, `DA_<map>_Environment`,
the cooked `DA_<map>_Collision`, the world-collision actor, the nav-area marks, the Recast meshes
for the agents the map's own graph names, the prune -- and one save carrying all of it. Then
`verify nav` judges the meshes it just built against retail's graph, automatically, because a lane
that builds navigation must not be able to finish having built a wrong one; `--skip-nav-verify` is
for iterating on the lane itself, and `uv run elysium verify nav --maps <map>` asks again on
demand. The three `import map-*` commands this replaced are gone.

For lane work, `--from <stage>` re-runs one stage and every one after it:

```
textures  materials  world  sky  entities  environment  collision  level
```

It FORCES rather than skips. Every stage always runs -- the level is authored from tables the
earlier stages fill on their reuse paths as well as their build paths, so a skipped stage would
stamp a level against a recipe computed from nothing. `--force` is `--from textures`. There is no
`nav` stage: the marks must precede the meshes, the meshes must precede the save, and the save is
`level`'s.

The map's nav graph must be exported first -- it decides `UsedHullBits`, so the agent set, and
which doors are cut.

The runtime reads a map's entities, collision and environment from its three baked
`DA_<map>_*` assets and nowhere else (0018 story 21-1 retired the sidecar arms and the per-map
flag that used to select between them), so a map that has not been through this procedure fails
the load with an error naming the one command that fixes it.

**The running game opens no file under `$ELYSIUM_EXPORT_ROOT/<map>/`** (0018 story 21-3). The
overhead cables are baked actors in the level, not `<map>.ropes`; the travel gate and the map list
are the baked level plus its three assets, answered from the asset registry, not the `<map>.ready`
marker and a directory listing; and `elysium.ents` reads `DA_<map>_Entities`. The producer no longer
writes `.ready` at all.

**Neither does the bake** (0018 story 21-4), which is why `export map --intermediate-only` left the
procedure above. `bake map` runs `UE_map_sidecars.write_sidecars` itself, into the producer's own
`$ELYSIUM_EXPORT_V2_ROOT/_sidecars/<map>/`, and names that root to both commandlets as
`-BakeMapSidecars=`; the producer reads only the published units, so there is nothing to decode
first. The two products that had a live reader and no producer are ported and staged as geometry
manifest blocks beside `ropes`: the `infodecal` projectors (`UE_map_sidecars.decal_rows` through
`importers.map_decals`, verified by `bake_verify.verify_decals`) and the rain cover
(`importers.map_weather`, which rasterises `rain_height.png` beside the manifest because the
editor's Python carries neither numpy nor Pillow). `<map>.materials.json` is read by nothing:
one map in the whole export tree ever had one.

**There is no BSP decoder** (0018 story 21-5). `UE_bsp_to_scene.py`, `UE_extract_corpus.py`, the
two-pass `export map`, the `CorpusBake` that authored `/ElysiumBaked/Shared`, and the mount itself
are deleted. A map is published as `export_v2` units and goes to a level through `bake map`; a
texture, a material and a model are each a unit with their own import lane, so `shared/` is owed
no replacement and `shared_corpus.py` keeps only the key rules those lanes join on. `export map`
survives as "import this map's model dependencies, then bake it"; `export all` / `export grid`
are bundle-only runs. The differences the two ported producers could not match the decoder on
were measured while it still existed and are recorded in `docs/contracts/seam_map_map.md` --
that measurement cannot be repeated, which is why it is written down rather than re-runnable.
