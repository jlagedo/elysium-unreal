# WORLDLIGHTS (lump 15)

Lump 15 is VtMB's compiled light-*source* set: every point, spot, sun, sky-ambient, and
emit-surface (texlight) the artists placed, including texlights and skyambient that no entity
carries.

**It is not what the original engine rendered the world from.** At runtime lump 15 is read
by the model light cache, which lights dynamic models and static props, and by the live
`GetLightForPoint` service (`engine.dll 0x200a4e90`) used for player stealth; world surfaces are lit
by the baked lightmaps in lump 8 alone (below). Decompiled in RE-A3 —
`docs/vtmb/sky-ambience.md` → "K3 / K5". Driving a real-time light set from lump 15 is therefore a
reconstruction of the authored look, not a reproduction of a path the game had.

Source: `pipeline/src/elysium_pipeline/formats/bsp.py` (`read_worldlights`).

## The `dworldlight_t` struct

`bsp.read_worldlights` decodes the standard 88-byte `dworldlight_t`: `origin`@0,
`intensity`@12 (**linear RGB**, can exceed 1 — seen up to ~143 000), `normal`@24 (beam
direction), `cluster`@36 (signed int32), `type`@40, `style`@44, `stopdot`/`stopdot2`/`exponent`@48/52/56 (spot cone),
`radius`@60 (cutoff). Types: **0** emit_surface (texlight), **1** point, **2** spot,
**3** skylight (sun), **5** skyambient. (Type 4 quakelight exists in the enum but is unused on
the test maps.)

## Measured counts

Counts across the test set: point (1) 10–300, spot (2) 12–464, texlight (0) 0–32.
**Sky-pair presence, measured over all 108 maps:** 25 carry a skylight (3) + skyambient (5),
and the two types are never separated — the game's 33 of each sit in those same 25 maps, whose
19,197 worldlights are otherwise 8,523 point, 8,179 spot and 2,429 texlight. Several maps carry
multiple `light_environment` entities, which need not produce the same number of compiled
sun records. The current patch-first census has four maps with multiple type-3 records:
`sm_warehouse_1` 4, `ch_temple_1` and `sp_taxiride` 3, and `sp_observatory_2` 2;
patch `sp_soc_2` has one. The outdoor `sm_hub_1` carries none, so its sky glow is
sprayed fill. Full inventory + the RE plan: `docs/vtmb/sky-ambience.md`.

## Model-cache sun eligibility

`engine.dll 0x200abd90` offers style-0 worldlights to `0x200aa4b0` with the PVS bypass
argument zero. That function rejects negative source clusters and requires the light's
cluster bit in the receiver PVS. It then calls `0x200a9ee0`, whose type-3 branch traces
along `-normal` with mask `0x4191` and accepts a `SURF_SKY` hit. The first successful sun
sets the lighting state's `+0x20` flag; subsequent suns are rejected for that state.

This gate belongs to the model-light cache. The separate stealth `GetLightForPoint`
path (`0x200a4e90`) rejects negative source clusters but tests the first sun before the
local-light PVS gate. World surfaces display the precompiled lump-8 bake. Neither path
should be described as executing the model-cache sun PVS rule.

The 2026-09-13 patch-first census checked all 108 exported lighting units and matched
worldlight/visibility lump hashes for the 25 sun-bearing maps. Of 33 sun records, 31
have nonzero intensity, a valid cluster and eligible receivers. `hw_cemetery_1`'s sun
is zero; `sp_observatory_2` source 7 has cluster `-1`. Within each of the 24 remaining
maps, usable suns have identical direction, RGB intensity and style. Their first-success
model-cache contribution is therefore equivalent to one common sun after the union of
their PVS masks and the common sky trace; intensities are not summed.

In patch `sp_tutorial_1`, source 61 is at cluster 2627. It is PVS-eligible from 395 of
2,651 clusters, all inside area 13; the other 2,256 clusters reject it. Area 13 has 810
clusters, so area membership alone is broader than the measured PVS scope. PVS counts
are candidate eligibility before sky tracing, not counts of sunlit surfaces.

[Census and per-source cluster sets](E:/elysium-work/research/lighting-math-2026-09-07/sun-scope-census/census.json),
[read-only source probe](E:/elysium-work/research/lighting-math-2026-09-07/sun_scope_census.py).

## Animated lightstyles (1–11)

A light's `style` field (1–11) makes it flicker/pulse: the classic Quake/Source pattern
strings, each letter a brightness sample from `'a'` (0, dark) through `'m'` (1.0, normal) to
`'z'` (≈2.08, over-bright), advanced in **discrete 10 Hz steps** (`engine.dll 0x20076eb0`;
`letter × 22` in the 264-normalized light query). The port's rendering interpolation is a visual
treatment; gameplay uses the discrete value from the same pattern and clock. Style 0 and the
unanimated 12–31 are constant; 32+ are switchable (held at a fixed level until toggled by
entity I/O — see `docs/vtmb/entity_io.md` for the light-toggling inputs).

`CLight` (`vampire.dll`; the bare addresses `0x10130800` / `0x101308d0` also name unrelated
`client.dll` functions) implements the switched/pattern behaviour: `Spawn` `0x10130460`
(`START_OFF` -> pattern `"a"`, else the authored pattern, else `"m"`), `On` `0x10130610` (the
pattern only if it has >= 2 letters and does not start with `'a'`, else `"m"`), `Off`
`0x10130690` (`"a"`), `Toggle` `0x101306f0`, `SetPattern` `0x10130780`, `FadeToPattern`
`0x10130800`, `FadeThink` `0x101308d0` (one letter per step towards the target's first letter,
re-thinking every `fade_time` — `0.05` on every corpus light). `CDynamicLight::Spawn`
`0x10056a90` turns the light on.

Recovered in full by walk `docs/specs/layers/L0-entity/walks/L0-r012.md` (port:
`Source/ElysiumUE/Private/Substrate/ElysiumLightClasses.cpp`, record `l0_switchable_light`):

- **KeyValue** `CLight::vfunc110` `0x101303c0` (slot 110): `strcmpi(key, "pitch")` -> X of
  `GetAngles` (slot 221, the local word) := `atof(value)`, written back through `SetAbsAngles`
  (slot 218 `0x100b2510`), return 1; every other key is `CBaseEntity::KeyValue` `0x1009e430`.
  Keys apply in lump order (`ParseMapData` `0x1009e280`), and every light that authors `pitch`
  in `sp_tutorial_1` (211) and `sm_hub_1` (375) authors `angles` before it, so the pitch stands.
- **Spawn** `0x10130460`: `m_iName == 0` -> `UTIL_RemoveImmediate` `0x1000e255` -> `0x101cd970`
  (KILLME, `UpdateOnRemove`, the destructor; no publish, no floor); else `m_flFadeTime =
  max(0.05, m_flFadeTime)` with the literal at `0x10453b74` (not a cvar); `m_iStyle < 32` ->
  return; `START_OFF` -> `LightStyle("a")` and `m_iszPattern = "a"` (the authored pattern is
  lost); else the pattern if non-NULL; else `"m"` and `m_iszPattern = "m"`. `pattern ""` is NULL
  (`AllocPooledString` `0x1042bff0`). Only Spawn and **Use** test style 32: the inputs and the
  helpers never do.
- **Use** `0x10130580` (slot 173): style < 32 -> nothing; `ShouldToggle(useType, !START_OFF)`
  (`0x100a98f0`: OFF on an off light and ON on an on light refuse; SET and TOGGLE always pass)
  -> `Toggle`. The `Use` input is `CBaseEntity::InputUse` `0x100ac9f0` with USE_TOGGLE; the
  `Toggle` / `TurnOn` / `TurnOff` inputs (`0x10130760` / `0x10130720` / `0x10130740`) call the
  helpers directly and bypass Use.
- **Kill** `0x101306d0` (slot 119): `TurnOff` (publishes `"a"`, sets START_OFF, hidden or not),
  then `CBaseEntity::Kill` `0x100acf90` (the deferred `UTIL_Remove` `0x101cd940`). **ScriptHide**
  `0x101305d0` is `TurnOff` then the base; **ScriptUnhide** `0x101305f0` is `TurnOn` then the base.
- **SetPattern** `0x10130780` / **FadeToPattern** `0x10130800` are FIELD_STRING rows (datamap
  `0x105754f8`, rows 6 / 7): `AcceptInput` `0x100abc90` refuses a float / int / bool / vector /
  color parameter through `variant_t::Convert` `0x100d05d0` before either runs, converts a VOID
  one to a NULL string, and passes an EHANDLE unconverted. SetPattern stores the string (NULL
  allowed), publishes it (NULL as `""`), clears START_OFF. FadeToPattern stores the OLD pattern's
  first byte as `m_iCurrentFade`, the target's first byte as `m_iTargetFade` (a NULL target gives
  0: a parameterless fade from `"m"` steps 109 times down to `""`), the new pattern (NULL for a
  non-string), installs `FadeThink` (`ThinkSet` `0x100ac4e0`), sets `m_flNextThink = curtime`,
  clears START_OFF; it publishes nothing itself.
- **FadeThink** `0x101308d0`: signed-byte compare, one step towards the target, the single
  letter published and `m_flNextThink = m_flFadeTime + curtime` (curtime rebound by the
  dispatcher `0x10033de0` to `max(scheduled, curtime)`); at `cur == tgt` the whole pattern and
  `m_flNextThink = 0.0`, which the dispatcher treats as no think; `m_pfnThink` stays installed.
- **LightStyle** is `VEngineServer014` slot 62 (`0x1070b22c` +0xf8): engine.dll `0x201096d0`
  stores the pattern pointer in `lightstyles[style]` and sends message `0x0c` to every client.
  Unrecovered: the client's handling of an empty pattern; which slot-173 callers other than
  `InputUse` reach a light; the class name of the `+0x2d4` sub-object base Kill marks.

## The `$envmap` reflection term

World and prop materials carry a `$envmap` reflection term composited pre-lightmap. Full
derivation, the `vertexlitgeneric` counterpart, and the whole-game authoring survey:
`docs/vtmb/reflections.md`.

## Baked lighting (lump 8)

VtMB itself is DX8/LDR with no post-processing. Lump 8 (the LIGHTING lump) is present in every
BSP but is not decoded for runtime rendering; it **is** decoded offline for lighting
analysis/calibration — `research/tooling/probes/lightmap.py`, `research/tooling/probes/probe_light_calibration.py`.
