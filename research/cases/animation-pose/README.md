# Reproducible Ghidra research specifications

These tracked JSON files are the durable index for multi-session VtMB binary
research. The Ghidra project and generated dumps are derived from the user's game
and remain under gitignored `$ELYSIUM_WORK_ROOT/research/ghidra/`; a specification preserves enough
context to reconstruct an investigation after that project is lost or re-imported.

Each specification records:

- the Ghidra program and pinned source-binary size, hashes, and image base;
- seed functions with working aliases, confidence, roles, and dump limits;
- confirmed findings and deliberately retained eliminated leads;
- direct and interface relationships between functions;
- field-displacement probes and unresolved questions.

The wrapper validates the specification, optionally verifies the source DLL, runs
one address per headless invocation, waits for Ghidra's project lock to clear, and
builds a local `INDEX.md` beside the generated decompile, assembly, xref, and field
dumps.

```powershell
uv run elysium research animation-pose research/cases/animation-pose/specs/animation_pose.json --dry-run
uv run elysium research animation-pose research/cases/animation-pose/specs/animation_pose.json `
  --binary "E:\path\to\Vampire\cl_dlls\client.dll"
uv run elysium research animation-pose research/cases/animation-pose/specs/animation_pose.json `
  --address 10091110 --kinds funcs,asm,xrefs
uv run elysium research animation-pose research/cases/animation-pose/specs/animation_pose.json --index-only
```

The animation specifications divide the call chain at binary boundaries:

| Specification | Program | Scope |
|---|---|---|
| `scene_requests.json` | `vampire.dll` | scene playback lifecycle, choreographed event dispatch, actor binding, animation-set application |
| `gameplay_actions.json` | `vampire.dll` | player action classification/mode routing, common NPC task/activity resolution, class/weapon translation, direct-label routes and final sequence selection |
| `animation_pose.json` | `client.dll` | local decode, blends, transitions, hierarchy, entity/root composition, render submission |
| `animation_skinning.json` | `engine.dll` | `VEngineModel006`, `SetupBones` callback, `TStudioRender012` bridge |
| `animation_studiorender.json` | `StudioRender.dll` | inverse bind, skin palette, vertex deformation, mesh submission |

## Gameplay-action working set

The gameplay-action investigation reuses this case because its result must join the server request
to the same client pose records. It has three complementary inputs:

1. The static player-body/model inventory preserves exact owner model, raw sequence/animation index,
   activity literal, weight, flags, grids and every descriptor byte:

   ```powershell
   uv run elysium research inventory_player_animations
   ```

2. The exported producer survey inventories map keys, I/O, schedules, direct sequence labels and
   executable Python across the current corpus, then joins exact labels to the character manifest:

   ```powershell
   uv run elysium research action_animation_survey `
     --json "$env:ELYSIUM_WORK_ROOT/research/gameplay-actions/action-animation-surface.json"
   ```

3. The native schedule survey decodes every compiled schedule into ordered task arguments and
   interrupts, then joins explicit activity arguments to current model availability without
   confusing direct absence with final translation/fallback:

   ```powershell
   uv run elysium research native_schedule_survey `
     --json "$env:ELYSIUM_WORK_ROOT/research/gameplay-actions/native-schedule-surface.json"
   ```

4. The combat-weapon survey extracts the complete pinned PE32 RTTI surface and each table/count
   virtual pair, preserving shared blobs and duplicate-base row order, then joins the result to
   current-map equipment:

   ```powershell
   uv run elysium research weapon_activity_survey `
     --json "$env:ELYSIUM_WORK_ROOT/research/gameplay-actions/weapon-activity-surface.json"
   ```

5. The NPC translation survey collapses every RTTI descendant to its effective pre/class and
   cover/reload bodies, expands paired-action base activities, and joins direct NPC plus maker
   class demand:

   ```powershell
   uv run elysium research npc_translation_survey `
     --json "$env:ELYSIUM_WORK_ROOT/research/gameplay-actions/npc-translation-surface.json"
   ```

6. The player paired-action survey decodes the nine-entry initial activity table, validates every
   direct `StartGrappleAttack` producer and inventories current Python `SeductiveFeed` use:

   ```powershell
   uv run elysium research player_grapple_survey `
     --json "$env:ELYSIUM_WORK_ROOT/research/gameplay-actions/player-grapple-surface.json"
   ```

7. The NPC task-override survey walks all NPC RTTI task slots, decodes the shared and class-local
   task registries, and materializes every custom StartTask/RunTask animation policy separately
   from custom handlers that only perform state, movement, spawning or damage work:

   ```powershell
   uv run elysium research npc_task_override_survey `
     --json "$env:ELYSIUM_WORK_ROOT/research/gameplay-actions/npc-task-override-surface.json"
   ```

8. The sequence-event survey validates every old 76-byte model event record, then walks hash-pinned
   server and client RTTI to disposition all animation, weapon-operator, `FireEvent` and viewmodel
   weapon-hook bodies:

   ```powershell
   uv run elysium research animation_event_survey `
     --json "$env:ELYSIUM_WORK_ROOT/research/gameplay-actions/animation-event-surface.json"
   ```

9. The layer survey validates every model autolayer binding and hash-pins the full caller weight
   separately from the four-slot combat-layer initializer:

   ```powershell
   uv run elysium research animation_layer_survey `
     --json "$env:ELYSIUM_WORK_ROOT/research/gameplay-actions/animation-layer-surface.json"
   ```

10. The player-action survey decodes the complete compiled `PLAYER_*` vocabulary, dispositions every
    `+0x704` call, proves the player RTTI classes share one policy surface, and inventories effective
    discipline `Player_Anim` data:

   ```powershell
   uv run elysium research player_action_survey `
     --json "$env:ELYSIUM_WORK_ROOT/research/gameplay-actions/player-action-surface.json"
   ```

   The paired client/server input survey closes the block producer separately from animation
   selection and records the composite `+wpn_secondaryatk` button path:

   ```powershell
   uv run elysium research input_action_survey `
     --json "$env:ELYSIUM_WORK_ROOT/research/gameplay-actions/input-action-surface.json"
   ```

11. The hash-pinned server context pack preserves the located policy call graph and exact
    one-shot completion/restart rule:

   ```powershell
   uv run elysium research animation-pose research/cases/animation-pose/specs/gameplay_actions.json --dry-run
   uv run elysium research animation-pose research/cases/animation-pose/specs/gameplay_actions.json `
     --binary "E:\path\to\Vampire\vampire.dll"
   ```

12. The live capture harness supplies branch reachability and ordering. `ELGACT1` currently records
   five stable server boundaries: player compact-code classification, ideal-activity assignment,
   active-weapon translation, and weighted/heaviest activity-to-sequence selection. Each row carries
   the server entity handle plus state snapshots; finalization joins its decoded index/serial to the
   existing actor census and keeps it beside `SEQC`/pose evidence in the same SQLite database. The
   paired-action router and compact/form vocabulary are statically closed but not yet exercised in
   full by this stream; pose-parameter writes and active-layer requests remain capture slices, not
   fields inferred into this first stream. The 1.791 s unarmed-crouch slice proves repeated requests
   reuse sequence 8 before completion; the pinned `StudioFrameAdvance` and player apply bodies close
   the later boundary by restarting that same sequence after its finished flag is set.

   The first controlled corpus is the Santa Monica hub:

   ```powershell
   uv run elysium research capture_theatre --map sm_hub_1 --install-config --allow-operator-stop
   ```

   After `ELYSIUM_RE37_MAP_SM_HUB_1` appears, F6 marks player locomotion, F7 NPC locomotion,
   F8 dialogue, F9 a reaction/gesture, and F12 flushes and stops retail. The capture finalizes
   `capture.sqlite` and writes `gameplay-actions-report.json`; the report can be regenerated with
   `uv run elysium research verify_gameplay_actions <session>`.

Generated inventories, decompiles, activity dumps and captures remain under
`$ELYSIUM_WORK_ROOT/research`. The normalized, engine-neutral action artifacts produced for the
game remain under `$ELYSIUM_EXPORT_ROOT`; neither kind is committed. The extraction and Unreal
contract are `docs/architecture/animation-architecture.md` §3, and confirmed retail facts are
`docs/vtmb/animation_and_movers.md` A.3.

Closure is a reachability join, not a hand-written checklist: every player/NPC producer must resolve
through its translations to an exact model sequence or a named fallback, and every activity/direct
sequence observed in retail must be attributable to a static producer or retained as an unexplained
row. Static extraction proves tables are complete; controlled captures prove which branches execute.

Working aliases and hypotheses remain in the specification. Once assembly and
decompile establish a VtMB format or behavior fact, write it into the owning
`docs/` topic in the same change; the generated context pack is evidence, not
project documentation.
