# Worker runs — L4-npc

Each run is one Codex worker: the slice briefs listed, in order, in one go (`briefs/`). Bundled from
consecutive slices of one subsystem up to ~1.5 KB of retail code or 8 functions. Estimated minutes =
25 fixed + 20 per KB of retail code.

| run | subsystem | briefs | functions | retail B | est. min | ready | after |
|---|---|---|---|---|---|---|---|
| L4-r001 | hints_places | `L4.hints_places.interesting-place-visitor-eviction-1` | 1 | 514 | 35 | yes | — |
| L4-r002 | navigation | `L4.navigation.current-waypoint-distance-1` | 1 | 120 | 27 | yes | — |
| L4-r003 | npc_kernel | `L4.npc_kernel.hit-group-records-1`, `L4.npc_kernel.targeted-discipline-table-1` | 7 | 843 | 41 | yes | — |
| L4-r004 | npc_kernel | `L4.npc_kernel.discipline-mask-removal-1`, `L4.npc_kernel.discipline-clear-sweep-1`, `L4.npc_kernel.discipline-schedule-interrupt-1` | 4 | 335 | 32 | yes | L4-r003 |
| L4-r005 | npc_kernel | `L4.npc_kernel.discipline-projectile-launch-1`, `L4.npc_kernel.discipline-trigger-1`, `L4.npc_kernel.npc-base-restore-1` | 5 | 1120 | 47 | no | L0-r026, L2-r002, L2-r008, L2-r045 … |
| L4-r006 | npc_kernel | `L4.npc_kernel.npc-troika-save-restore-1`, `L4.npc_kernel.task-running-1`, `L4.npc_kernel.state-change-flags-1`, `L4.npc_kernel.condition-46-gate-1` | 5 | 1217 | 49 | yes | L4-r005 |
| L4-r007 | npc_kernel | `L4.npc_kernel.eye-direction-1` | 1 | 1847 | 61 | no | L0-r034 |
| L4-r008 | npc_kernel | `L4.npc_kernel.expression-event-queue-1`, `L4.npc_kernel.flex-timeline-1` | 5 | 692 | 39 | yes | — |
| L4-r009 | npc_kernel | `L4.npc_kernel.npc-keyvalue-sound-1` | 1 | 114 | 27 | no | L0-r015, L0-r016 |
| L4-r010 | npc_kernel | `L4.npc_kernel.activity-translation-1`, `L4.npc_kernel.reload-base-1`, `L4.npc_kernel.task-facing-random-range-1`, `L4.npc_kernel.motor-construction-1`, `L4.npc_kernel.model-eye-position-1`, `L4.npc_kernel.squad-member-lookup-1`, `L4.npc_kernel.squad-enemy-eluded-notify-1` | 8 | 1288 | 50 | yes | — |
| L4-r011 | npc_kernel | `L4.npc_kernel.valid-entity-handle-1`, `L4.npc_kernel.interesting-place-death-activity-1` | 3 | 111 | 27 | yes | — |
| L4-r012 | npc_kernel | `L4.npc_kernel.kickable-prop-selection-1`, `L4.npc_kernel.kickable-prop-selection-2` | 2 | 1124 | 47 | no | — |
| L4-r013 | npc_kernel | `L4.npc_kernel.navigator-target-offset-1`, `L4.npc_kernel.approach-goal-refresh-1` | 3 | 767 | 40 | yes | — |
| L4-r014 | npc_kernel | `L4.npc_kernel.witness-global-act-maxima-1` | 1 | 868 | 42 | no | L0-r022, L0-r023 |
| L4-r015 | npc_kernel | `L4.npc_kernel.directed-path-point-1` | 3 | 1177 | 48 | yes | — |
| L4-r016 | npc_kernel | `L4.npc_kernel.back-away-path-1` | 2 | 1763 | 59 | yes | — |
| L4-r017 | npc_kernel | `L4.npc_kernel.shooting-position-1` | 2 | 1915 | 62 | no | — |
| L4-r018 | npc_kernel | `L4.npc_kernel.shooting-position-2`, `L4.npc_kernel.local-route-1` | 2 | 447 | 34 | no | L4-r017 |
| L4-r019 | npc_kernel | `L4.npc_kernel.local-route-2` | 3 | 1392 | 52 | no | — |
| L4-r020 | npc_kernel | `L4.npc_kernel.local-route-3` | 1 | 198 | 29 | no | — |
| L4-r021 | npc_kernel | `L4.npc_kernel.jump-velocity-1`, `L4.npc_kernel.floor-drop-1`, `L4.npc_kernel.two-leg-move-limit-1` | 3 | 1078 | 46 | yes | — |
| L4-r022 | npc_species | `L4.npc_species.node-search-primitives-1`, `L4.npc_species.node-search-primitives-2`, `L4.npc_species.random-node-visited-bitset-1` | 3 | 260 | 30 | yes | — |
| L4-r023 | npc_species | `L4.npc_species.node-route-endpoint-legs-1`, `L4.npc_species.node-route-endpoint-legs-2` | 3 | 550 | 36 | no | L4-r022 |
| L4-r024 | npc_species | `L4.npc_species.path-waypoint-finalization-1`, `L4.npc_species.squad-memory-miss-notifier-1`, `L4.npc_species.sight-cache-first-handle-1` | 3 | 258 | 30 | yes | L4-r023 |
| L4-r025 | perception | `L4.perception.l4-look-tail-1`, `L4.perception.l4-listen-tail-1`, `L4.perception.l4-nearest-sound-scent-1`, `L4.perception.l4-enemy-lkp-chase-anchor-1` | 4 | 1452 | 53 | yes | L4-r024 |
| L4-r026 | social | `L4.social.relationship-table-growth-1`, `L4.social.relationship-answers-1`, `L4.social.relationship-answers-2`, `L4.social.boss-template-flag-1`, `L4.social.base-follower-boss-1` | 5 | 1524 | 55 | yes | — |
| L4-r027 | social | `L4.social.squad-construction-1`, `L4.social.squad-construction-2` | 2 | 191 | 29 | no | — |
| L4-r028 | camera_ui | `L5.camera_ui.sign-use-transaction-1`, `L5.camera_ui.game-text-color-parse-1` | 2 | 233 | 30 | yes | — |
| L4-r029 | dialogue | `L5.dialogue.dialogue-row-storage-1`, `L5.dialogue.dialogue-row-storage-2` | 5 | 1536 | 55 | yes | — |
| L4-r030 | dialogue | `L5.dialogue.dialogue-source-stream-1` | 5 | 885 | 42 | yes | — |
| L4-r031 | dialogue | `L5.dialogue.dialogue-row-parser-1`, `L5.dialogue.dialogue-row-parser-2` | 2 | 1230 | 49 | yes | L4-r029, L4-r030 |
| L4-r032 | dialogue | `L5.dialogue.dialogue-row-lookup-1`, `L5.dialogue.dialogue-clan-cache-1`, `L5.dialogue.dialogue-dependency-test-1`, `L5.dialogue.dialogue-dependency-test-2`, `L5.dialogue.dialogue-speech-asset-name-1` | 7 | 1161 | 48 | yes | L4-r031 |
| L4-r033 | dialogue | `L5.dialogue.dialogue-script-report-1` | 3 | 2353 | 71 | yes | L4-r030, L4-r031, L4-r032 |
| L4-r034 | dialogue | `L5.dialogue.dialogue-script-report-2` | 3 | 3649 | 96 | yes | L4-r030, L4-r031, L4-r032 |
| L4-r035 | dialogue | `L5.dialogue.dialogue-script-report-3` | 2 | 2446 | 73 | yes | L4-r030, L4-r031, L4-r032 |
| L4-r036 | dialogue | `L5.dialogue.dialogue-ellipsis-report-1`, `L5.dialogue.dialogue-autoend-report-1`, `L5.dialogue.dialogue-script-problem-audit-1` | 4 | 1460 | 54 | yes | L4-r030, L4-r031, L4-r032 |
| L4-r037 | dialogue | `L5.dialogue.dialogue-script-problem-audit-2` | 1 | 680 | 38 | yes | L4-r030, L4-r031, L4-r032 |
| L4-r038 | python | `L5.python.python-bridge-init-1`, `L5.python.python-bridge-init-2` | 2 | 1056 | 46 | yes | — |
| L4-r039 | scripted | `L5.scripted.expression-table-lifecycle-1`, `L5.scripted.expression-index-lookup-1`, `L5.scripted.flex-setting-groups-1` | 7 | 994 | 44 | yes | — |
| L4-r040 | scripted | `L5.scripted.flex-setting-groups-2`, `L5.scripted.scene-event-intensity-1`, `L5.scripted.scene-raw-duration-1`, `L5.scripted.gesture-timing-tags-1`, `L5.scripted.flex-track-sampling-1` | 7 | 758 | 40 | yes | — |
| L4-r041 | scripted | `L5.scripted.flex-track-sampling-2`, `L5.scripted.flex-track-sampling-3` | 3 | 1106 | 47 | yes | — |
| L4-r042 | navigation | `L4.navigation.l4-navigation-node-network-initialization-1` | 1 | 943 | 43 | yes | — |
| L4-r043 | navigation | `L4.navigation.l4-navigation-node-network-initialization-2`, `L4.navigation.l4-npc-species-node-connectivity-1` | 3 | 1035 | 45 | yes | — |
| L4-r044 | audio | `L4.audio.l4-npc-species-node-route-waypoint-chain-1`, `L4.audio.l4-npc-species-node-route-waypoint-chain-2`, `L4.audio.l4-npc-species-path-waypoint-simplification-1` | 7 | 1200 | 48 | no | L4-r002 |
| L4-r045 | audio | `L4.audio.l4-npc-species-path-waypoint-simplification-2` | 2 | 866 | 42 | no | L4-r002, L4-r044 |
| L4-r046 | entity_core | `L4.entity_core.l4-npc-kernel-floor-trace-selection-1` | 1 | 520 | 35 | yes | — |
| L4-r047 | navigation | `L4.navigation.l4-npc-kernel-npc-movement-completion-1`, `L4.navigation.l4-npc-kernel-npc-movement-completion-2` | 2 | 129 | 28 | yes | — |
| L4-r048 | entity_core | `L4.entity_core.l4-npc-kernel-default-schedule-initialization-1` | 1 | 53 | 26 | yes | — |
| L4-r049 | physics | `L4.physics.l4-npc-kernel-entity-forget-broadcast-1` | 1 | 233 | 30 | yes | — |
| L4-r050 | player | `L4.player.l4-perception-sound-occlusion-1` | 1 | 734 | 39 | yes | — |
| L4-r051 | player | `L4.player.l4-navigation-police-spawn-node-search-1` | 1 | 1402 | 52 | yes | — |
| L4-r052 | navigation | `L4.navigation.l4-npc-kernel-horizontal-direction-normalization-1` | 1 | 87 | 27 | yes | — |
| L4-r053 | npc_species | `L4.npc_species.l4-npc-kernel-npc-script-sound-emission-1`, `L4.npc_species.l4-npc-kernel-npc-script-sound-emission-2`, `L4.npc_species.l4-npc-species-root-owner-trace-filter-1` | 3 | 483 | 34 | no | L0-r006, L0-r033 |
| L4-r054 | npc_species | `L4.npc_species.l4-npc-species-human-combatant-classification-1` | 1 | 6 | 25 | yes | — |
| L4-r055 | perception | `L4.perception.l4-perception-stealth-deaf-zone-rules-1`, `L4.perception.l4-perception-stealth-deaf-zone-rules-2` | 5 | 1257 | 50 | no | L2-r040, L2-r041 |
| L4-r056 | entity_core | `L5.entity_core.l5-python-global-state-reset-1` | 1 | 101 | 27 | yes | — |
| L4-r057 | scripted | `L5.scripted.l5-scripted-scripted-map-change-1` | 1 | 405 | 33 | yes | — |
