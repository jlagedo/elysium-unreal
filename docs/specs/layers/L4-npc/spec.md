# L4 — NPC with L5 scripting

**Standing rule for every story:** follow retail behaviour. This spec owns its layer's rows in
`../audit.tsv`; a story names the rows it closes. What it needs from a lower layer is built there
first (`AGENTS.md`: missing inputs); what only a higher layer runs is left as a listed upward hook.
A story closes on its records green on the arena and its rows re-read against the port.

## Scope

- **Retail:** `CAI_BaseNPC` / `CAI_BaseNPCTroika`, the species, senses and memory, the navigator and motor, squads and relationships, hints and places — with L5: scripted sequences and cine, dialogue, the Python bridge — and the player's verbs on NPCs.
- **Work:** 264 open or partial core functions (128 KB of retail code) in 102 stories and 154 briefs; npc_kernel 39, dialogue 11, npc_species 10, scripted 10, navigation 5, social 5, perception 5, entity_core 4, parked 4, camera_ui 2, audio 2, player 2, hints_places 1, python 1, physics 1.
- **Ready now:** 80 of 102 stories call nothing open below this layer (`schedule.md`).
- **Live paths first:** 1 stories hold stubs the baseline's arena fired (17 hits) — they lead the order.
- **Hooks:** 1 calls into higher layers stay as named hooks (`../hooks.tsv`).
- **Witnesses:** the arena, `sp_tutorial_1` and `sm_hub_1` (map records).

## The gate — the layer is done when

1. every story's records are green on one `uv run elysium arena` run, and a record that was
   intermittent is green in three boot orders;
2. every row of this layer in `../audit.tsv` is done, or a named divergence the owner accepted
   (`../decisions.md`);
3. the default and arm tiers have 0 failures, and every record that passed in `../baseline.md`
   still passes;
4. its upward hooks are listed, each with the layer that completes it;
5. its state-bearing engine-replaced rows ([`contract-checks.md`](contract-checks.md)) keep retail's
   inputs, outputs and event order, shown by a record, or are named divergences the owner accepted.

## Order

0. **The test instrument** — built once in L0 (`../harness.md`); this layer's stories add their
   own `retail_site` emit sites, `entity_field` names and `entity_call` entry points in the slice
   that ports them.
1. **The stories**, in [`stories-table.md`](stories-table.md): callees before callers, live paths
   first. Each has its file under `stories/` and its worker briefs under `briefs/`.

## Decisions that touch this layer

- Owner choice: expose the current Detour corridor corner for ordinary crowd routes so `0x102f1cf0` can measure it, or approve endpoint-only distance as a named Unreal modernization. The current body facts document the missing corridor data; the 0018 engine-replacement decision does not settle this behavior measurement.
- Decide whether the existing ground-door NavMesh route at 0x10303d10/0x10303850 remains a named modernization or is replaced by the retail local-route gates, acceptance, triangulation, and fly path. The packet describes contract-changing gaps; absent an owner decision to retain that modernization, implement the recovered retail behavior.
- Choose how Unreal carries the retail studio-header eye vector from 0x10428310: preserve the imported vector exactly or use an authored Unreal socket. A socket replacement can change NPC gaze and visibility geometry.
- Navigation modernization: decide whether to restore retail node-link search and local-route behavior for `0x102f1860`, `0x102f3270`, `0x10300700`, `0x103005f0` and `0x103007e0`, or keep the existing NavMesh replacement. This plan assumes retail behavior; keeping NavMesh makes those differences a named modernization.
- Wander modernization: decide whether to restore the visited-set behavior behind `0x102fe9a0` or retain the current no-visited-set wander behavior. The random walk and neighbor cursor are outside this packet, so include them if restoring the retail contract.
- Search scope: confirm the parent A* search `0x102fd240` and node-link graph are already owned by L4 work or expand the packet; helper-only implementation cannot restore route choice end to end.
- Squad scope: this story includes the `CAI_Squad` roster storage and writer path that the packet omits, because the current substrate has no squad object and the notifier cannot be wired with a null-squad seam.
- Assign the missing scent producer before closing `l4-nearest-sound-scent`. The current selector's input source is absent from the L0 sound path; the owner must place that producer in the earlier sound-emission work rather than accept a null GetBestScent seam.
- Decide whether `TArray` growth is an approved Unreal replacement for retail's five-entry expansion and allocation-failure behavior, or whether to model the explicit capacity, copy, zero and warning path.

## Stories added from the parked specs

- 0002-npc-ai:925 — R7. The hub's species rows. `CNPC_VCop`, `CNPC_VHuman`, `CNPC_VHunter`,: [Hub NPC species rule rows](stories/L4.scripted.l4-npc-species-hub-species-rule-rows.md)
- 0003-scripted-sequence:137 — 1. The kernel vocabulary.: [Scripted task and schedule vocabulary](stories/L4.npc_kernel.l4-npc-kernel-scripted-task-schedule-vocabulary.md)
- 0003-scripted-sequence:156 — 2. The NPC in `NPC_STATE_SCRIPT`.: [NPC_STATE_SCRIPT selection and cleanup](stories/L4.npc_kernel.l4-npc-kernel-npc-state-script-selection-cleanup.md)
- 0003-scripted-sequence:179 — 3. The cine.: [Scripted sequence cine lifecycle](stories/L5.npc_species.l5-scripted-cine-lifecycle.md)
- 0003-scripted-sequence:202 — 4. Targets retail never has.: [Scripted target task execution](stories/L5.entity_core.l5-scripted-cine-target-execution.md)
- 0004-jack-arrival:127 — 7. The reaction-score consumer (was 9.9, talk slice).: [Dialogue reaction-score consumer](stories/L5.parked.l5-dialogue-reaction-score-consumer.md)
- 0004-jack-arrival:135 — 8. Presentation completion.: [Conversation panel presentation](stories/L5.parked.l5-dialogue-conversation-panel-presentation.md)
- 0005-first-kill:126 — 8. The flying knockback chain.: [Flying knockback chain](stories/L4.npc_kernel.l4-npc-kernel-flying-knockback-chain.md)
- 0006-first-disciplines:54 — 4. The tutorial's offer.: [Tutorial discipline offer](stories/L5.parked.l5-scripted-tutorial-discipline-offer.md)
- 0009-elevator-final:129 — 13. The level script's delegated fills.: [Level-script barter native dispatch](stories/L5.parked.l5-python-level-script-native-dispatch.md)
- 0010-theatre-scene:122 — 8. Gesture and sequence un-collapsed.: [Scene gestures on overlay slots](stories/L5.scripted.l5-scripted-scene-gesture-overlay.md)
- 0010-theatre-scene:128 — 9. Paired actions.: **not planned yet** — Title: Paired scene action variants and claims. Port the role/size/side catalog arithmetic and dual-participant claim behavior, using L1 animation and L2 reaction/death-pose machinery.
- 0012-camera-director:69 — 4. The sequencer bridge and the composition call (was 11.13g).: **not planned yet** — Title: Level Sequence camera request bridge. Preserve the recovered scene-owner decision, let the Camera Cut Track own cuts and camera transforms, and release the request on stop, abort, skip, or trav
