# WORLDLIGHTS (lump 15)

Lump 15 is VtMB's compiled light-*source* set: every point, spot, sun, sky-ambient, and
emit-surface (texlight) the artists placed, including texlights and skyambient that no entity
carries.

**It is not what the original engine rendered the world from.** At runtime lump 15 is read only
by the model light cache, which lights dynamic models and static props; world surfaces are lit
by the baked lightmaps in lump 8 alone (below). Decompiled in RE-A3 —
`docs/sky-ambience.md` → "K3 / K5". Driving a real-time light set from lump 15 is therefore a
reconstruction of the authored look, not a reproduction of a path the game had.

Source: `tools/bsp.py` (`read_worldlights`).

## The `dworldlight_t` struct

`bsp.read_worldlights` decodes the standard 88-byte `dworldlight_t`: `origin`@0,
`intensity`@12 (**linear RGB**, can exceed 1 — seen up to ~143 000), `normal`@24 (beam
direction), `type`@40, `style`@44, `stopdot`/`stopdot2`/`exponent`@48/52/56 (spot cone),
`radius`@60 (cutoff). Types: **0** emit_surface (texlight), **1** point, **2** spot,
**3** skylight (sun), **5** skyambient. (Type 4 quakelight exists in the enum but is unused on
the test maps.)

## Measured counts

Counts across the test set: point (1) 10–300, spot (2) 12–464, texlight (0) 0–32.
**Sky-pair presence, measured over all 108 maps:** 25 carry a skylight (3) + skyambient (5),
and the two types are never separated — the game's 33 of each sit in those same 25 maps, whose
19,197 worldlights are otherwise 8,523 point, 8,179 spot and 2,429 texlight. Five maps carry
several `light_environment`s (`sm_warehouse_1` 4, `ch_temple_1` and `sp_taxiride` 3,
`sp_observatory_2` and `sp_soc_2` 2); the outdoor `sm_hub_1` carries none, so its sky glow is
sprayed fill. Full inventory + the RE plan: `docs/sky-ambience.md`.

## Animated lightstyles (1–11)

A light's `style` field (1–11) makes it flicker/pulse: the classic Quake/Source pattern
strings, each letter a brightness sample from `'a'` (0, dark) through `'m'` (1.0, normal) to
`'z'` (≈2.08, over-bright), advanced at 10 Hz and lerped between keyframes. Style 0 and the
unanimated 12–31 are constant; 32+ are switchable (held at a fixed level until toggled by
entity I/O — see `docs/entity_io.md` for the light-toggling inputs).

## The `$envmap` reflection term

World and prop materials carry a `$envmap` reflection term composited pre-lightmap. Full
derivation, the `vertexlitgeneric` counterpart, and the whole-game authoring survey:
`docs/reflections.md`.

## Baked lighting (lump 8)

VtMB itself is DX8/LDR with no post-processing. Lump 8 (the LIGHTING lump) is present in every
BSP but is not decoded for runtime rendering; it **is** decoded offline for lighting
analysis/calibration — `tools/lightmap.py`, `tools/probe_light_calibration.py`; see
`docs/rendering-perf.md`.
