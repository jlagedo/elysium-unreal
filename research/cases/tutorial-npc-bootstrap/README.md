# VtMB tutorial NPC bootstrap research case

RE47 separates retail `sp_tutorial_1`, the Unofficial Patch replacement map/script, and the
installer-selected Basic/Plus profile before recovering how direct NPCs and maker children enter
their initial native state.

The installed target is Unofficial Patch 11.5 with the Plus `user.cfg` selector. The data probe
pins both original and overriding BSP/script pairs; it never writes their game-derived contents
inside the checkout.

## Questions

- Which characters are direct BSP entities, which are maker templates, and which arrive only after
  an authored event?
- Which origin, facing, model, sheet, disposition, equipment, visibility, dialogue and ambient
  fields exist before any output fires?
- In what order are BSP entities created, spawned, activated, initialized by `NPCInitThink`, and
  admitted to the first AI/schedule pass?
- Which `logic_auto.OnMapLoad`, `unhidePlus`, `setBasic`, `setPlus`, level-script, dialogue and
  trigger actions mutate those initial states?
- Which character and bootstrap differences belong to retail, the shared Unofficial Patch map, or
  the selected Plus profile?

## Reproduction

Generated evidence stays below `ELYSIUM_WORK_ROOT`:

```powershell
uv run elysium research tutorial_npc_bootstrap --json <work>/research/tutorial-npc-bootstrap.json
uv run elysium research tutorial-npc-bootstrap --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_tutorial_npc_bootstrap_<task>" --kinds funcs,grep
```

The data probe preserves BSP entity index, repeated output order, direct `npc_*` records,
`npc_maker` templates, `logic_auto` rows, and every statically named input targeting a direct
character. The native specification pins the common retail server DLL and recovers only the
engine-owned boot sequence. It does not treat Source SDK code as proof of VtMB behavior.

## Consumers and boundary

- Map-specific character ledger and profile join: `docs/vtmb/sp_tutorial_1-event-surface.md`
- Generic native NPC initialization and AI ownership: `docs/vtmb/npc-ai-reverse-engineering.md`
- Level-script/Basic/Plus ignition: `docs/vtmb/python_bridge.md`
- Status: `docs/project/roadmap.md` RE47

Static evidence establishes authored initial state and server ordering. Exact first-render timing,
the engine partition's initial ground/touch reconciliation, and any task behavior hidden behind an
unrecovered virtual remain capture questions rather than inferred state.

## Hash-gated capture recipe

The capture is admissible only when `vampire.dll`, the UP BSP/script pair, `user.cfg`, and the patch
readme match the hashes recorded by this case. Run two fresh Plus starts without console mutation.
Begin recording before `LevelInit`, and end after the first porch dialogue acquires its camera.

At map-entity construction, Spawn, Activate, `NPCInitThink`, the first ordinary AI pass, every Jack
motor/activity/schedule change, the porch `OnEndTouch`, dialogue acquisition, and the first rendered
dialogue frame record raw:

- player, controller and Jack origin/angles/velocity plus render visibility;
- Jack activity, ideal activity, sequence, NPC state, schedule, task, navigator goal and motor yaw;
- interesting-place candidate, claim/release, path request and authored group;
- view origin/angles/FOV, camera weights, active source shot, fallback and `DialogPOV` gaze point;
- the accepting input, command suppression, dialogue owner/partner/line and release lifetime.

Repeat comparison starts with `use_interesting` disabled before admission and with the three opener
forms in a disposable map. Comparisons may identify an owner, but they do not authorize an activity,
turn or placement unless the trace identifies the writer and its order. Store raw captures and a
machine-readable event ledger under `$ELYSIUM_WORK_ROOT/research/tutorial-npc-bootstrap/capture/`.

## Closed static findings

- Original retail contains 13 direct characters, 13 makers and one `logic_auto`; the UP replacement
  contains 20 direct characters, 14 makers and five autos. All tutorial makers begin disabled, so
  their children are not load-time population.
- UP moves Jack from `-221 -258 -40`, angles `0 90 0`, to `144 7352 -199`, angles `0 190 0`.
  Jack remains a visible direct `npc_VVampire`; the change is BSP authoring, not dialogue placement.
- `LevelInit` performs construct/keyvalue/Spawn, deferring parented entities. `ServerActivate` is a
  later complete live-entity pass.
- The recovered Jack class Spawn → Activate → concrete `NPCInitThink` chain contains no face-player
  write and no direct `SetActivity`/`SetIdealActivity` call. Ground repair, optional target resolution,
  ordinary AI-think installation and spawnflag-driven readiness occur in `NPCInitThink`.
- UP profile selection runs after the direct population exists. `setPlus()` hides/unhides wildcard
  cohorts and starts the player idle monitor; it does not initialize Jack's animation.
