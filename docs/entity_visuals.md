# Entity visuals — VtMB entity-placed visuals: format facts & data contracts

Reference for the VtMB `.ents`-driven visuals the pipeline exports — glow sprites, dynamic-model
props, ropes/cables, particle emitters, and signage. Scope is **VtMB format facts and the export
data contracts**: what these entity classes carry, how the offline exporter
(`tools/UE_bsp_to_scene.py`) turns them into runtime intermediates, and what a renderer needs to
draw them. It does not own the Unreal-side entity substrate or spawn/behavior design — that is
`FElysiumEntityWorld`, documented in `docs/engine-core.md` (the entity object model, the two-phase
build plan, the classname registry) and `docs/runtime-architecture.md` (the substrate's place in
the map epoch, the spawn pass, `AcceptInput`, the event queue). Per-task build status lives in
`docs/roadmap.md`, never here.

The behavior surface these visuals eventually wire into (I/O dispatch, `ScriptHide`/`ScriptUnhide`,
output firing) is `docs/entity_io.md`; Python evaluation is `docs/python_bridge.md`.

---

## 1. What VtMB places, entity-side

Beyond the baked BSP geometry, a VtMB map carries a layer of entity-placed visuals the pipeline
must resolve separately: `env_sprite` glow coronas and volumetric-light billboards at lamps/bulbs,
`prop_dynamic`/`prop_physics` placed models (street lights, cop cars, …), `keyframe_rope`/
`move_rope` catenary cables strung between poles/buildings, `env_particle`/`func_particle`/
`params_particle`/`env_steam` emitters (steam, smoke, fire), and `game_sign`/`prop_sign` signage.
All of it is exported into `<map>.ents` (§3.1); heavy referenced assets (sprite/rope textures,
prop models) decode into separate asset sidecars keyed back to the entity (§3.2).

---

## 2. Fact base (measured across the 7 exported maps)

| Fact | Number | Consequence |
|---|---|---|
| Distinct entity classnames | 112 over 10,837 entities | A classname→handler registry is the right shape, not per-class special cases |
| `.ents` already carries identity + `keys{}` + `outputs[]` (7-field incl. Python) + `hulls` + `start_hidden` | — | Entity I/O needs zero export rework |
| Runtime visual toggles (`Hide`/`Show`/`TurnOn`/`TurnOff`) | ~250 outputs | All `logic_relay`/`timer`/`trigger`/`choreo` sourced → some visuals change *during play*, not just at load |
| `logic_auto` (only spawn-time firer) targeting a visual entity | **0 direct** (5 relays, 33 py at spawn) | Initial frame ≈ spawn flags; spawn-chains are a tiny deferred subset |
| `env_sprite` StartOn (`spawnflags` bit0) | 1113 on / 135 off / 3 flag=2 | Must filter by spawn flag; `start_hidden` is 0 on all sprites |
| `env_sprite` materials | glowa 900, volumelighta 96, candle 42, coplights 41, coplightsb 37, volumelighta_proxyfade 32 | Coronas **and** volumetric beams **and** candle flames **and** cop flashers |
| `prop_dynamic`/`prop_physics`/`_ornament` | 425, all `.mdl`; 242 named; 116 `start_hidden` | Reuse the prop decoder; skip hidden; keep identity for entity I/O |
| `keyframe_rope`/`move_rope` | 562; keys `NextKey`,`Subdiv`,`Width`,`Slack`,`RopeMaterial`(`cable/cable`),`TextureScale`,`Type` | Rest length is computable from chain + slack (both server and client halves apply, see §5); `Type` sets the node count |
| `env_particle` | 125; keys `particle_definition`,`active`,`attach_type`,`bounds`,`ramp_scale` | Needs particle-system resolution — open question, §6 |
| Switchable lights (have `targetname`) | 8 of 2841 | The rest are non-addressable, driven purely by lump-15 WORLDLIGHTS data |

Example env_sprite (pierlight street lamp, sp_tutorial_1): `model materials/sprites/glowa.vmt`,
`rendermode 5` (additive), `renderamt 40`, `rendercolor 255 255 255`, `scale .75`,
`disableshadows 1`, `spawnflags 1`. It sits ~3u from a `light_spot` `_light 247 207 115 3500`
(warm).

Source `rendermode` values that matter here: **3 = `kRenderGlow`** (camera-facing additive
corona, occlusion-**faded**, ~constant apparent size), **5 = `kRenderTransAdd`** (plain
additive, world-scaled). Both blend additive; glow adds the occlusion fade.

---

## 3. Data contracts

### 3.1 `.ents` (the entity source of truth)

`tools/UE_bsp_to_scene.py::write_entities` emits `{"map": <base>, "entities": [ … ]}`. Per entity:

- Always: `classname`, `targetname`, `origin` (**Unreal centimetres**, `source_to_unreal`
  applied), `start_hidden` (bool), `keys` (every remaining keyvalue verbatim, last-wins — so
  `spawnflags`, `model`, `rendermode`, `rendercolor`, `renderamt`, `scale`, `angles`,
  `particle_definition`, `NextKey`, `Slack`, `use_icon`, … all live here).
- Brush entities only (`model "*N"`): `model` (int index), `hulls` (list of flat
  `[x,y,z, …]` **entity-local Unreal-centimetre** convex hulls), `contents`, `blocks_player`.
- If any `On*/Out*` key: `outputs[]`, each `{name, target, input, param, delay, times,
  python}`. Retained regardless of whether the visual it targets is addressable yet.
- Entities carrying a static `.mdl` `model` key (`prop_dynamic`/`prop_physics` and the
  `prop_button`/`prop_doorknob(_electronic)`/`prop_sign`/`prop_switch`/`prop_hacking`/
  `item_container(_animated/_lock)` family) additionally get `model_mesh` (the decoded OBJ
  stem, shared with GAME_LUMP static props) and `model_quat` (the Unreal-space placement
  rotation). Skeletal `npc_*` models are excluded — they belong to the glTFRuntime NPC track,
  not this static-geometry path.
- `phys_hinge` (and the `phys_*` constraint family) additionally get `hinge_axis` = the
  normalized Unreal-space hinge direction; the pivot is the entity's own `origin`.
- An entity inside the 3D-skybox miniature's BSP area is annotated `"sky": true`.

`classname`/`targetname`/`origin` are popped from `keys`; `StartHidden` stays in `keys` **and**
is surfaced as `start_hidden`. This schema is stable — entity I/O reads it as-is.

### 3.2 Asset decodes (assets only, keyed by the entity's own references)

Exporter passes decode the heavy assets an entity references; the entity record itself stays
in `.ents`:

- Sprite materials → `tex/spr_<name>.png` (Sprite VMT `$basetexture` decoded through the same
  TTH/TTZ path as world textures) — the `.sprites` sidecar names the png per sprite
  (`write_sprites`).
- Rope material → `tex/rope_<name>.png` (+ `tex/rope_<name>_n.png` for `$bumpmap`) — the
  `.ropes` sidecar (§5).
- Dynamic-prop `.mdl` → `props/<safename>.obj` + `props/tex/` (the same decoder as GAME_LUMP
  static props, deduped by model).

No new sidecar is required for entity records (the runtime reads `.ents` directly); a derived-
geometry sidecar (ropes) is permitted where runtime computation is undesirable, but it must be
keyed back to the entity, never a replacement for it.

### 3.3 Coordinate conventions (reference)

Source→Unreal: `(sx,sy,sz) → (sx, -sy, sz) × 2.54` — inches to centimetres, Z-up both spaces, Y
negated to flip handedness (a reflection, so triangle winding is reversed once at export time).
Directions use the same negation with no scale. `.ents` `origin` is **already** Unreal space;
`keys.origin`/`keys.angles` are raw Source, for a consumer that needs the original values. Full
rule (single source of truth): `tools/bsp.py::source_to_unreal` / `source_dir_to_unreal`.

---

## 4. `env_sprite` — coronas and light billboards

Every non-`start_hidden`, StartOn (`spawnflags` bit0 set) `env_sprite` needs a camera-facing
additive billboard. The material name selects the visual family (§2): `glowa` (900, plain
coronas), `candle` (42, should flicker/animate — `framerate` key), `coplights`/`coplightsb` (78,
cop-car flashers — cycle frame/colour), `volumelighta`/`volumelighta_proxyfade` (128, light
**shafts** — elongated, not round halos; distinct geometry from a flat billboard).

Two `env_sprite`-specific facts a renderer needs:

- **StartOn filtering.** 135 of 1,251 sprites are StartOff (`spawnflags` bit0 clear) and must
  not draw on load; only entity I/O turns them on later. This is separate from `start_hidden`
  (0 sprites carry it).
- **`rendermode` drives the blend, not just the tint.** `kRenderGlow` (3) sprites should fade
  their alpha by the unoccluded fraction and hold roughly constant apparent size regardless of
  distance; `kRenderTransAdd` (5) is plain world-scaled additive with no occlusion fade. Both
  blend additive.
- Orientation: `parallel_upright` in the sprite's VMT constrains the billboard to the world
  Y-axis only; its absence means a full camera-facing (`vp_parallel`) billboard.
- World size is `scale × textureSize` (Source inches → Unreal cm).

---

## 5. `keyframe_rope` / `move_rope` — cables

562 catenary cables, strung between poles/buildings. Elysium-Unreal already builds these
(roadmap 8.7): `write_ropes` emits a `<map>.ropes` per-segment sidecar
carrying the RE'd rest length, and the runtime stands one Verlet `UCableComponent` per segment
(no offline catenary sampling needed). The facts below are the RE reference that implementation
was built from, and they generalize to any renderer.

Chain resolution: each node's `NextKey` → the `targetname` of the next node. `keyframe_rope` and
`move_rope` construct the **same** class — stock Source **`CRopeKeyframe`** (`vampire.dll`
factories `0x1019d680` / `0x1019d6f0`, both `operator new(0x49c)` + the one ctor `0x1019dc80`) —
so the two classnames carry no behavioural difference and chain roles are topological, not
classname-derived. `NextKey` resolves the engine way, `FindEntityByName(NULL, name)` = the
**first** entity of that name in spawn/entity order (= entity-lump order), which matters because
a map can reuse rope names across installations (`sp_tutorial_1` reuses `tele4..tele9` twice,
~200 m apart). A `NextKey` naming no rope node is a mapper typo the engine warns about and draws
nothing for — there are **122** of them across the 108 maps.

**The rope's shape comes from three fields, and only one of them is the one the FGD name
suggests:**

- **`Slack`** feeds the sag, but the rest length is **not** `span + Slack` — it is computed in two
  halves that live in different DLLs, and reading only the server half overstates every rope's sag:

  | step | where | expression |
  |---|---|---|
  | `RecalculateLength` | vampire.dll `0x1019e5d0` | `m_RopeLength = (int)\|B − A\|` |
  | `RopeThink` (gated on `m_RopeFlags & 1`, every 0.1 s) | vampire.dll `0x1019efb0` | `m_RopeLength = (int)\|B − A\| + m_Slack` |
  | `RecomputeSprings` (the `m_Slack`/`m_RopeLength` RecvProxy `0x100be290` tail-jumps here) | client.dll `0x100bf1a0` | `springDist = (m_RopeLength + m_Slack − 100) / (nodes − 1)` |
  | `CBaseRopePhysics::ResetSpringLength` | client.dll `0x10128ae0` | `m_flSpringDist = max(0, springDist)` |

  Three things fall out and none are guessable from the FGD: `Slack` is applied **twice** (server-side
  into `m_RopeLength`, then again client-side); a flat **−100 units** is subtracted; and the divide is
  an *integer* one (`CDQ`/`IDIV`), so the per-segment length truncates. The rest length is therefore
  `springDist × (nodes − 1)`, i.e. roughly `(int)|B − A| + 2·Slack − 100`.

  The −100 dominates at VtMB's scale — authored `Slack` runs 0..100 across the whole game — so **most
  ropes come out at or below their straight span and hang taut**, and the `max(0, …)` floor lets a
  short rope collapse to a dead-straight chord. Only the short, high-slack links (chophouse meat-hook
  chains at `Slack` 60–100 over ~1 m spans) keep a real loop. Both endpoints are locked by default
  (`m_fLockedPoints = 3` in the ctor), so whatever surplus survives is forced into a catenary.

  The sag is simulated, not authored: `C_RopeKeyframe::Init` (`0x100c04d0`) lerps the nodes evenly
  along the straight A→B chord, then — when `m_RopeFlags & 0x40` is set, which the ctor default `0x48`
  does — runs `RunRopeSimulation(5.0f)` (`0x100bf360`) so the strand has settled before it is first
  drawn.
- **`Type`** — not `Subdiv` — sets the simulated node count. `CRopeKeyframe::KeyValue` (`0x1019f2b0`)
  maps `Type` 0 → `m_nSegments` 10, 1 → 4, **anything else → 2**, and `Activate` (`0x1019e310`) clamps
  to `[2, 10]`. With no `Type` key at all the ctor default 5 stands. A `Type 2` rope therefore has
  **two** nodes: one span between two locked points, which cannot sag at all — that is how the taut
  steel cables and rigid hanging chains read. **677 of the game's 2,688 rope nodes (25%) are `Type
  2`.**
- **`Subdiv`** is the *client-side render* tessellation between physics nodes (capped by client.dll's
  `rope_subdiv` cvar, alongside `rope_collide` / `rope_shake` / `r_drawropes` / `rope_drawlines`). It
  is not the simulation resolution.

The remaining `KeyValue` branches: `Dangling` 1 clears `ROPE_LOCK_END_POINT` in `m_fLockedPoints`, so
the far end hangs free (**51** nodes game-wide); `Collide` 1 → `m_RopeFlags |= 0x04`; `Barbed` → `0x02`;
`Breakable` → `0x10`; `RopeShader` 0/1/2 selects `cable/cable.vmt` / `cable/rope.vmt` /
`cable/chain.vmt`, overriding `RopeMaterial`. `MoveSpeed` / `MoveTime` / `Tension` /
`PositionInterpolator` are keyframe-path fields the rope renderer ignores → render at rest.

Ctor defaults (`0x1019dc80`): `Slack` 0, `Width` 2, `TextureScale` 4, `m_nSegments` 5, `Subdiv` 2,
`m_RopeLength` 20, `m_fLockedPoints` 3, `m_RopeFlags` 0x48, `m_bCreatedFromMapFile` 1. `DT_RopeKeyframe`
(`0x1019d8d0`) offsets: `m_RopeFlags` 0x454, `m_Slack` 0x45c, `m_Width` 0x460, `m_TextureScale` 0x464,
`m_nSegments` 0x468, `m_iRopeMaterialModel` 0x46c, `m_Subdiv` 0x470, `m_RopeLength` 0x474,
`m_fLockedPoints` 0x478, `m_flScrollSpeed` 0x484, `m_hStartPoint` 0x490, `m_hEndPoint` 0x494,
`m_iStartAttachment` 0x498, `m_iEndAttachment` 0x49a. `SetupHangDistance` (`0x1019e110`) is the
code-created-rope path only: `CalcRopeStartingConditions(v1, v2, ROPE_MAX_SEGMENTS = 10, hangDist)`.

`C_RopeKeyframe`'s own offsets differ — the recv table (`0x100be3d0`) puts `m_flScrollSpeed` 0x448,
`m_RopeFlags` 0x44c, `m_iRopeMaterialModel` 0x450, `m_nSegments` 0x6b8, `m_hStartPoint` 0x6bc,
`m_hEndPoint` 0x6c0, `m_iStartAttachment` 0x6c4, `m_iEndAttachment` 0x6c6, `m_Subdiv` 0x6c8,
`m_RopeLength` 0x6cc, `m_Slack` 0x6d0, `m_TextureScale` 0x6d4, `m_fLockedPoints` 0x6d8, `m_Width` 0x6dc.
The embedded `CRopePhysics<10>` starts at 0x458, so its node count is 0x464 and its node array 0x460.

**The `.ropes` sidecar** (`write_ropes`) emits one line per segment, 14 whitespace-separated tokens:
`tex ax ay az bx by bz width_cm rest_cm nodes texscale flags bump matflags`. `tex`/`bump` are the
decoded rope material PNGs or `-` when absent/undecodable; `a`/`b` are the two node origins (Unreal
cm); the segment parameters (`width_cm`, `rest_cm`, `nodes`, `texscale`, `flags`) come from the
*start* node A, computed per the RE above rather than passed through raw. `matflags` carries the rope
VMT's shader mode (1 = `$alphatest`, 2 = `$translucent`, 4 = `$envmap`) — load-bearing for
`cable/chain`/`cable/chainb`, which are `$alphatest 1` over a texture ~47% cut out (the gaps between
the links); rendering them opaque turns a chain into a solid tube with a chain painted on it.

---

## 6. `env_particle` / `func_particle` / `params_particle` / `env_steam`

125 `env_particle` emitters, keys `particle_definition`, `active` (start state),
`attach_type`, `bounds`, `ramp_scale`. **Open question:** what `particle_definition` names is
unresolved — VtMB is early Source, so it could name either the Troika KeyValues particle scripts
(`particles/*.txt`) or Source `.pcf` systems; this needs to be settled before an emitter can be
drawn at all.

---

## 7. `game_sign` / `prop_sign` — signage

Not a world-geometry visual — both classes render a **full-screen VGUI window**, not world
geometry: each carries a `definition_file` naming a `vdata/Signs/*.txt` KeyValues panel
(`SignData` → `BackgroundImage` + `TextBlock`/`Label`, fonts from `resource/TrackerScheme.res`),
opened by the `OpenWindow` input or a `+use` on the prop. `game_sign` (73 across the patch map
set) is bodiless — the tutorial's `popup_*` help windows; `prop_sign` (100) is a world `.mdl`
(note, bus-stop sign, newspaper) whose model decodes through the ordinary static-mesh path and
whose `use_icon` resolves through the use-cursor system. The window itself is tracked in
`roadmap.md` **4.10** (entity classes + panel) and **8.8** (VGUI-fidelity panel), with **PL5c**
exporting the definitions — nothing about signs is a quad/decal rendering problem.

---

## 8. Related docs

`docs/entity_io.md` (the 7-field I/O, `use_icon`, `StartHidden`/`ScriptHide` — the runtime
behavior surface this data feeds), `docs/python_bridge.md` (name→delegate namespace, the five
Python call paths), `docs/engine-core.md` (`FElysiumEntityWorld`, the classname registry, the
spawn pass — the Unreal substrate that consumes `.ents`), `docs/runtime-architecture.md` (the
substrate's place in the map epoch and frame), `docs/lighting.md` (real-time lighting),
`docs/mdl_v2531.md` (prop model decode), `tools/CLAUDE.md` ("BSP format", "Static props /
models", "Collision models").
