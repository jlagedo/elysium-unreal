# VtMB exported-map event-resolution research case

This case expands the `sp_tutorial_1` event-resolution pass across every map currently present in
`ELYSIUM_EXPORT_ROOT`. It diffs authored producers, resolved receivers, field-5 Python and
map-referenced VCD event types against the tutorial baseline, then recovers only the retail leaf
behaviours not already covered by RE43.

## Questions

- Which trigger classnames occur outside `sp_tutorial_1`, and which introduce genuinely new
  producer or world-state semantics rather than another authored use of generic entity I/O?
- What admission, enabled-state, actor-class and occupancy guards govern `trigger_teleport`,
  `trigger_push`, `trigger_player_activity_level`, `trigger_discipline_context`,
  `trigger_bomb_site`, `trigger_checkvolume` and `trigger_electric_bugaloo`?
- Which leaves run only on touch/use transitions, which install a think, and which mutate state for
  every world tick?
- Does brush teleport preserve velocity, write view/controller state, check destination clearance,
  and rely on engine-owned touch reconciliation like `point_teleport`?
- What event-layer work do VCD `expression` and `gesture` events produce, if any?
- Which novel producer and receiver names are ordinary datamap I/O, and which require separate
  behavior recovery?

## Reproduction

Use a workstream-private copy of the analyzed Ghidra project. Generated reports, decompilation and
game-derived evidence remain below `ELYSIUM_WORK_ROOT`.

```powershell
uv run elysium research event_surface_survey --baseline sp_tutorial_1 --json "<work>/research/event-surface/exports-vs-sp_tutorial_1.json"
uv run elysium research exported-event-resolution research/cases/exported-event-resolution/specs/exported_event_resolution.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_exported_events_<task>" --kinds funcs,xrefs,fields,vtables,grep
```

The survey report carries the sorted relative-path/size/SHA-256 manifest of every consumed `.ents`
and VCD file. That manifest, rather than a remembered map list, defines the evidence snapshot.

## Findings on the recorded snapshot

- The manifest contains 23 `.ents` and 30 referenced VCD files; its combined SHA-256 is
  `B1B986F7C67EDAAA0E9DDF6DA8FB5F79EF1F99BAFDE8591497E4F1368960C7EC`.
- Relative to `sp_tutorial_1`, it adds 96 entity classnames, seven trigger classnames, 127 authored
  producer pairs, 207 case-folded resolved receiver pairs, 113 static-unresolved target/input pairs,
  209 field-5 Python identifiers, two special-target inputs and two VCD event types.
- Static-unresolved is not synonymous with dead: maker-created `patrol_cop_*` and `hunter_*`
  targets can exist by service time, while the absent streetlight triplets are established dangling
  graphs. Residual rows require producer-time lifetime classification.
- The novel trigger leaves are collision/use/input producers over the existing RE43 queue. Only
  `trigger_push.SetSpeed` installs a leaf think, and no current map wires that input.
- `trigger_player_activity_level.OnTrigger` is authored twice but absent from the retail datamap;
  the rows are silently dropped. Its actual leaf refreshes player levels from `Touch` and applies
  exact-match cleanup at `EndTouch`.
- `prop_sign.OnReadBegin` is real at `+0x734` and fires from `0x10211db0`; `use_icon` is consumed
  separately at `+0x76c`.
- `trigger_electric_bugaloo` compares game time against the `300.0f` constant at `0x1048e708` and
  latches its downstream player action once per entity lifetime. Its generic use-edge outputs still
  refire per accepted session.
- The new VCD tokens are `expression` and `gesture`. Relative to the tutorial, the manifest adds no
  additional `firetrigger` value and no VCD Python payload.

## Boundaries

This case closes the seven novel trigger leaves, not every one of the 127 new producer pairs.
Known changelevel, maker, camera, mover and damage events join existing owning documents. Exact NPC
perception/lifecycle guards, landmark/interesting-place transitions, visibility, security-camera,
HUD-timer, slash and pickup producers remain inventoried demand for their owning leaf
investigations. Exact transform contact reconciliation and partition containment remain
`engine.dll` boundaries. Controlled retail and Unreal acceptance remain open.

## Consumers

- Corpus join and delta: `docs/vtmb/exported-map-event-surface.md`
- Generic entity and trigger facts: `docs/vtmb/entity_io.md`
- Queue and frame facts: `docs/vtmb/game_runtime.md`
- Python boundary: `docs/vtmb/python_bridge.md`
- VCD boundary: `docs/vtmb/choreographed_scenes.md`
- Tutorial baseline: `docs/vtmb/sp_tutorial_1-event-surface.md`
- Status: `docs/project/roadmap.md` RE44
