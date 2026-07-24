# Committed content (`Content/`)

**Nothing game-sourced is committed.** Every asset here is hand-authored and game-agnostic;
converted VtMB content lives only in the gitignored `tools/out/` (see the repo-root
`CLAUDE.md` → "Bring-your-own-game"). Five files, and that is the whole list:

| Asset | Role |
|---|---|
| `Elysium.umap` | the empty boot persistent level |
| `VtMB/Materials/M_VtMB_World.uasset` | the world master material — albedo + alpha-masked `$selfillum` emissive (`map_Ke` → `Emissive`/`EmissiveScale`), usable with ISMs |
| `VtMB/Materials/M_Sky.uasset` | the 2D-skybox cube master material (samples a runtime `UTextureCube` by view direction) |
| `VtMB/Materials/M_Gizmo.uasset` + `M_Gizmo_XRay.uasset` | the entity-gizmo ISM masters — unlit, two-sided, translucent, colour + opacity from per-instance custom data; `_XRay` disables the depth test |

## Why these are committed at all

A UMaterial shading graph and a `.umap` can only be compiled by the **editor** — the
standalone export cannot produce them. They are the accepted exception to "build everything in
code": authored offline by generators under `tools/`, then committed.

## Regenerating

Never hand-edit these in the editor and commit the result — edit the generator instead.
`content.bat` → `tools/build_content.py` rebuilds all of them in one headless editor session,
in dependency order:

1. `add_world_emissive.py` — `M_VtMB_World`, the `$selfillum` emissive path
2. `set_world_material_usage.py` — `M_VtMB_World`, "Used with Instanced Static Meshes"
3. `make_sky_material.py` — `M_Sky`
4. `make_gizmo_material.py` — `M_Gizmo` + `M_Gizmo_XRay`
5. `make_boot_map.py` — `Elysium.umap`

Every generator is idempotent and also runnable standalone as a `-script`.
`python tools/export_all.py` invokes the umbrella at the end of a run (skip with
`--no-content`), so a generator cannot be forgotten and silently go stale.

**Registering a new committed asset** = add its generator filename to `GENERATORS` in
`tools/build_content.py`, and add a row to the table above.

The rest of the master-material set (masked/translucent world variants, water, etc.) is not
built yet — see `docs/roadmap.md` P7.
