# L0 — entity

**Standing rule for every story:** follow retail behaviour. This spec owns its layer's rows in
`../audit.tsv`; a story names the rows it closes. What it needs from a lower layer is built there
first (`AGENTS.md`: missing inputs); what only a higher layer runs is left as a listed upward hook.
A story closes on its records green on the arena and its rows re-read against the port.

## Scope

- **Retail:** `CBaseEntity` and physics (movetypes, VPhysics objects, touch, push), entity I/O and the event queue, triggers, movers and doors, the save framework, the sound list, sound emission, effects, ConVars and game rules.
- **Work:** 372 open or partial core functions (92 KB of retail code) in 153 stories and 203 briefs; entity_core 46, physics 36, audio 16, entity_io 13, movers_doors 12, save_restore 10, effects_world 10, triggers 5, rules_cvars 3, sound_list 2.
- **Ready now:** 153 of 153 stories call nothing open below this layer.
- **Live paths first:** 2 stories hold stubs the baseline's arena fired (1,064 hits) — they lead the order.
- **Hooks:** 51 calls into higher layers stay as named hooks (`../hooks.tsv`).
- **Witnesses:** the arena, `sp_tutorial_1` and `sm_hub_1` (map records).

## The gate — the layer is done when

1. every story's records are green on one `uv run elysium arena` run, and a record that was
   intermittent is green in three boot orders;
2. every row of this layer in `../audit.tsv` is done, or a named divergence the owner accepted
   (`../decisions.md`);
3. the default and arm tiers have 0 failures, and every record that passed in `../baseline.md`
   still passes;
4. its upward hooks are listed, each with the layer that completes it.

## Order

0. **The test instrument** — L0's story 0 builds the four general pieces every layer's records
   use (`../harness.md`). Nothing else in L0 starts before it.
1. **The stories**, in [`stories-table.md`](stories-table.md): callees before callers, live paths
   first. Each has its file under `stories/` and its worker briefs under `briefs/`.

## Decisions that touch this layer

- Owner decision: classify/register the 0x1022b590 -> 0x10175360 write to player+0x1e04 as an L3 upward hook, or explicitly reassign its ownership; hooks.tsv has no entry, so the SoundScheme story cannot close as an unlisted hook.
- Owner decision: retain the existing single-origin and angle transform simplification as a named modernization, or restore retail local/absolute frame state and parent-inverse conversion at 0x100b2150, 0x100b1ac0, 0x100b2300, and 0x100b2510. The current port explicitly collapses these values.
- Owner decision: define the Unreal replacement boundary for Source networking and edict services used by these rows, including ForceTransmit, dirty-state notification, and edict-backed PostConstructor paths; retain their game-visible flags and lifecycle effects either way.
- Owner decision: choose whether to reproduce 0x101d1960's cyclic multi-client selection and cluster cache or keep the current one-player PVS seam as an explicit single-player scope cut.
- Owner decision: 0x1023fd00 is marked engine-replaced in the layer audit; decide how Unreal collision queries preserve its strict squared-distance '< radius squared' boundary before accepting that replacement. 
- Scope decision: keep 0x1013dea0 CFuncBrush::Spawn in L0 brush/solidity work rather than widening this movers_doors packet to generic func_brush behavior.
- Modernization decision: retain and name the existing renderer replacement for Source visibility-portal updates at 0x100ef990, 0x100ef630 and 0x100f21d0 as a visual-only modernization; implement each row's remaining handle or restore semantics.
- Owner decision: name whether UE/Chaos collision may replace Source movement simulation. If chosen, preserve retail trace results, callback order, blocked-time rollback, and contact gates as a named modernization boundary.
- Owner decision: confirm the existing contact-tolerance and all-solid mappings as named geometry modernizations, or require retail parity before accepting the trace story.
- Owner decision: accept UE asset/material loading as the rope Precache modernization while retaining retail base Spawn reset and endpoint initialization order.
- Owner scope decision: keep physcannon acquisition/drop rows in L3, ragdoll/physics-chain rows in L1, player spawn placement in L3, NPC forget cleanup in L4, and dynamic-prop skin input outside L0 physics.
- Scope: keep the stock single-player cut or explicitly add a Counter-Bite/multiplayer restoration. docs/vtmb/multiplayer.md says maxplayers is 1 and no multiplayer rules class ships; the rows below stay unplaced under the current scope despite the packet's static 108-map reach.
- Law divergences: decide whether the 2-second floor, low-level 0..5 clamp, and extra PublishLawEvent are intentional named modernizations. Without that decision, restore the retail cvar and field-write behavior.
- Choose whether exact Source token/size words from `0x101a05f0` are required, or whether the existing Unreal archive is the named wire-format replacement; save files are disposable, so this is a behavior-versus-format boundary.
- Choose the Unreal physics-state serializer that preserves the `ShouldSavePhysics == true` admission and the save handler's timing; the current base slot returns false.
- Choose the Unreal asset-loading replacement for `MODELNAME` and `SOUNDNAME` precaching in `0x101a2820`, preserving its restore ordering.
- The owner must choose whether the 128-event serial/cursor bus is a named modernization for the retail 64-slot and reserved-client-index behavior, or whether the port will expose that retail lookup contract. The current bus does not establish equivalent slot identity, bounds or null-pool lifetime.
