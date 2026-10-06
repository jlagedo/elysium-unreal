# Worker runs — L0-entity

Each run is one Codex worker: the slice briefs listed, in order, in one go (`briefs/`). Bundled from
consecutive slices of one subsystem up to ~1.5 KB of retail code or 8 functions. Estimated minutes =
25 fixed + 20 per KB of retail code.

| run | subsystem | briefs | functions | retail B | est. min | ready | after |
|---|---|---|---|---|---|---|---|
| L0-r001 | audio | `L0.audio.ambient-radius-level-1`, `L0.audio.keyvalues-lexer-1`, `L0.audio.keyvalues-tree-1` | 7 | 1117 | 47 | yes | — |
| L0-r002 | audio | `L0.audio.scheme-file-load-1`, `L0.audio.keyvalues-access-1` | 7 | 1076 | 46 | yes | L0-r001 |
| L0-r003 | audio | `L0.audio.ambient-init-1` | 3 | 2179 | 68 | yes | L0-r001 |
| L0-r004 | audio | `L0.audio.ambient-init-2`, `L0.audio.ambient-init-3`, `L0.audio.ambient-source-resume-1`, `L0.audio.ambient-pitch-1`, `L0.audio.ai-sound-owner-1`, `L0.audio.sound-emission-origin-1`, `L0.audio.sound-folder-index-1` | 6 | 1259 | 50 | yes | L0-r001, L0-r003 |
| L0-r005 | audio | `L0.audio.voice-table-index-1`, `L0.audio.voice-table-index-2`, `L0.audio.sound-channel-parse-1`, `L0.audio.sound-script-defaults-1` | 3 | 565 | 36 | yes | — |
| L0-r006 | audio | `L0.audio.sound-script-resolve-1`, `L0.audio.sound-script-resolve-2`, `L0.audio.sound-script-resolve-3` | 4 | 1262 | 50 | yes | L0-r005 |
| L0-r007 | audio | `L0.audio.sound-scheme-switch-1`, `L0.audio.sound-scheme-switch-2` | 5 | 1007 | 45 | yes | L0-r002 |
| L0-r008 | effects_world | `L0.effects_world.entity-parenting-1`, `L0.effects_world.entity-parenting-2`, `L0.effects_world.entity-parenting-3`, `L0.effects_world.entity-parenting-4`, `L0.effects_world.particle-attachment-1`, `L0.effects_world.particle-attachment-2` | 8 | 1482 | 54 | yes | — |
| L0-r009 | effects_world | `L0.effects_world.particle-emitter-spawn-1`, `L0.effects_world.particle-rate-ramp-1`, `L0.effects_world.world-sweep-query-1`, `L0.effects_world.runtime-decal-1`, `L0.effects_world.runtime-decal-2`, `L0.effects_world.runtime-decal-3` | 6 | 1265 | 50 | yes | L0-r008 |
| L0-r010 | effects_world | `L0.effects_world.switchable-light-1`, `L0.effects_world.switchable-light-2`, `L0.effects_world.switchable-light-3` | 6 | 633 | 37 | yes | — |
| L0-r011 | effects_world | `L0.effects_world.sprite-lifecycle-1`, `L0.effects_world.illusionary-brush-spawn-1`, `L0.effects_world.blood-effects-1` | 8 | 1133 | 47 | yes | — |
| L0-r012 | effects_world | `L0.effects_world.blood-effects-2` | 1 | 462 | 34 | yes | — |
| L0-r013 | entity_core | `L0.entity_core.eflags-word-1`, `L0.entity_core.collision-touch-1` | 7 | 618 | 37 | yes | — |
| L0-r014 | entity_core | `L0.entity_core.bounds-size-1`, `L0.entity_core.base-construction-1` | 3 | 1310 | 51 | yes | L0-r013 |
| L0-r015 | entity_core | `L0.entity_core.datamap-keyvalues-1` | 4 | 671 | 38 | yes | L0-r014 |
| L0-r016 | entity_core | `L0.entity_core.datamap-keyvalues-2` | 5 | 2414 | 72 | yes | L0-r014 |
| L0-r017 | entity_core | `L0.entity_core.data-object-registry-1` | 6 | 1549 | 55 | yes | — |
| L0-r018 | entity_core | `L0.entity_core.target-name-predicates-1`, `L0.entity_core.toggle-acceptance-1`, `L0.entity_core.base-perception-1`, `L0.entity_core.base-perception-2`, `L0.entity_core.monster-template-1` | 8 | 819 | 41 | yes | — |
| L0-r019 | entity_core | `L0.entity_core.entity-visual-state-1`, `L0.entity_core.angle-motion-1`, `L0.entity_core.angle-motion-2` | 7 | 1454 | 53 | yes | L0-r013, L0-r014 |
| L0-r020 | entity_core | `L0.entity_core.parented-transforms-1` | 4 | 1661 | 57 | yes | L0-r019 |
| L0-r021 | entity_core | `L0.entity_core.recursive-teleport-1`, `L0.entity_core.recursive-teleport-2` | 2 | 972 | 44 | yes | L0-r019, L0-r020 |
| L0-r022 | entity_core | `L0.entity_core.think-contexts-1`, `L0.entity_core.base-spawn-1`, `L0.entity_core.removal-cleanup-1` | 7 | 1388 | 52 | yes | L0-r013, L0-r014 |
| L0-r023 | entity_core | `L0.entity_core.removal-cleanup-2`, `L0.entity_core.removal-cleanup-3`, `L0.entity_core.deferred-removal-1`, `L0.entity_core.deferred-removal-2`, `L0.entity_core.postconstructor-registration-1` | 7 | 1338 | 51 | yes | L0-r013, L0-r014, L0-r022 |
| L0-r024 | entity_core | `L0.entity_core.map-ordering-1`, `L0.entity_core.map-ordering-2`, `L0.entity_core.map-entity-spawn-1`, `L0.entity_core.map-entity-spawn-2`, `L0.entity_core.named-target-removal-1`, `L0.entity_core.system-callbacks-1`, `L0.entity_core.gameframe-simulation-1` | 8 | 1474 | 54 | yes | L0-r014, L0-r015, L0-r016, L0-r019 … |
| L0-r025 | entity_core | `L0.entity_core.gameframe-simulation-2`, `L0.entity_core.gameframe-simulation-3`, `L0.entity_core.server-shutdown-1`, `L0.entity_core.server-shutdown-2`, `L0.entity_core.sound-volume-table-1`, `L0.entity_core.sound-volume-table-2` | 5 | 807 | 41 | yes | L0-r002, L0-r023, L0-r024 |
| L0-r026 | entity_core | `L0.entity_core.global-initialization-1`, `L0.entity_core.global-initialization-2`, `L0.entity_core.instant-hit-effects-1`, `L0.entity_core.landmark-lookup-1` | 7 | 1041 | 45 | yes | L0-r025 |
| L0-r027 | entity_core | `L0.entity_core.save-block-framework-1`, `L0.entity_core.save-block-framework-2`, `L0.entity_core.save-block-framework-3`, `L0.entity_core.save-block-framework-4` | 4 | 1186 | 48 | yes | — |
| L0-r028 | entity_core | `L0.entity_core.transition-state-1`, `L0.entity_core.transition-state-2`, `L0.entity_core.transition-state-3`, `L0.entity_core.transition-state-4`, `L0.entity_core.transition-state-5` | 5 | 1400 | 52 | yes | L0-r027 |
| L0-r029 | entity_core | `L0.entity_core.player-map-restore-1`, `L0.entity_core.player-map-restore-2`, `L0.entity_core.world-lifecycle-1`, `L0.entity_core.world-lifecycle-2`, `L0.entity_core.world-lifecycle-3`, `L0.entity_core.world-impact-policy-1` | 6 | 1366 | 52 | yes | L0-r015, L0-r016, L0-r019, L0-r022 … |
| L0-r030 | entity_core | `L0.entity_core.floating-query-1`, `L0.entity_core.point-contents-cache-1`, `L0.entity_core.activity-copy-owner-1`, `L0.entity_core.aisound-base-slots-1`, `L0.entity_core.activity-metadata-1`, `L0.entity_core.activity-metadata-2`, `L0.entity_core.activity-metadata-3` | 7 | 1242 | 49 | yes | L0-r013, L0-r022, L0-r023 |
| L0-r031 | entity_core | `L0.entity_core.view-node-links-1` | 1 | 319 | 31 | yes | — |
| L0-r032 | entity_core | `L0.entity_core.view-node-links-2` | 4 | 2257 | 69 | yes | — |
| L0-r033 | entity_core | `L0.entity_core.entity-view-pick-1`, `L0.entity_core.entity-sight-traces-1`, `L0.entity_core.entity-sight-traces-2` | 4 | 1312 | 51 | yes | L0-r020 |
| L0-r034 | entity_core | `L0.entity_core.client-pvs-selection-1`, `L0.entity_core.sphere-entity-query-1`, `L0.entity_core.body-queue-1`, `L0.entity_core.inspection-node-1` | 4 | 660 | 38 | yes | L0-r008, L0-r014, L0-r022, L0-r023 |
| L0-r035 | entity_io | `L0.entity_io.variant-convert-1`, `L0.entity_io.python-payload-1`, `L0.entity_io.timer-arm-1`, `L0.entity_io.timer-arm-2`, `L0.entity_io.logic-auto-first-fire-1`, `L0.entity_io.logic-auto-restore-1`, `L0.entity_io.timer-spawn-1` | 7 | 1467 | 54 | yes | L0-r013 |
| L0-r036 | entity_io | `L0.entity_io.timer-fire-1`, `L0.entity_io.timer-refire-1`, `L0.entity_io.input-effect-position-1`, `L0.entity_io.global-state-table-1`, `L0.entity_io.global-state-table-2`, `L0.entity_io.doorknob-activation-1` | 7 | 713 | 39 | yes | L0-r035 |
| L0-r037 | entity_io | `L0.entity_io.event-queue-drain-1`, `L0.entity_io.event-queue-drain-2`, `L0.entity_io.world-area-policy-1` | 2 | 1040 | 45 | yes | L0-r035 |
| L0-r038 | movers_doors | `L0.movers_doors.door-sound-stimulus-1`, `L0.movers_doors.rotating-door-axis-spawn-1` | 3 | 1327 | 51 | yes | L0-r013, L0-r019 |
| L0-r039 | movers_doors | `L0.movers_doors.angular-move-kinematics-1`, `L0.movers_doors.angular-move-kinematics-2`, `L0.movers_doors.angular-move-kinematics-3`, `L0.movers_doors.rotating-door-open-time-1`, `L0.movers_doors.doorknob-spawn-state-1`, `L0.movers_doors.doorknob-registration-1`, `L0.movers_doors.door-activation-target-cache-1`, `L0.movers_doors.door-open-unlocks-knobs-1` | 7 | 1354 | 51 | yes | L0-r038 |
| L0-r040 | movers_doors | `L0.movers_doors.door-use-icon-resolution-1`, `L0.movers_doors.door-use-icon-resolution-2`, `L0.movers_doors.door-restore-seating-1`, `L0.movers_doors.door-restore-seating-2`, `L0.movers_doors.door-blocked-response-1`, `L0.movers_doors.door-blocked-response-2` | 7 | 1031 | 45 | yes | L0-r019, L0-r020, L0-r038, L0-r039 |
| L0-r041 | movers_doors | `L0.movers_doors.door-blocked-response-3`, `L0.movers_doors.door-blocked-response-4`, `L0.movers_doors.door-blocked-response-5`, `L0.movers_doors.door-blocked-response-6` | 1 | 706 | 39 | yes | L0-r019, L0-r020, L0-r038, L0-r039 |
| L0-r042 | physics | `L0.physics.entity-bounds-1`, `L0.physics.entity-bounds-2`, `L0.physics.physics-object-contract-1` | 5 | 882 | 42 | yes | L0-r020 |
| L0-r043 | physics | `L0.physics.physics-object-contract-2`, `L0.physics.physics-object-contract-3`, `L0.physics.physics-object-contract-4`, `L0.physics.physics-object-contract-5`, `L0.physics.physics-object-contract-6`, `L0.physics.physics-object-contract-7`, `L0.physics.collision-query-1` | 7 | 1276 | 50 | yes | L0-r020, L0-r042 |
| L0-r044 | physics | `L0.physics.collision-query-2` | 5 | 1629 | 57 | yes | L0-r042, L0-r043 |
| L0-r045 | physics | `L0.physics.parent-link-lifecycle-1`, `L0.physics.trigger-volume-bounds-1`, `L0.physics.trigger-volume-bounds-2`, `L0.physics.touch-lifecycle-1`, `L0.physics.touch-lifecycle-2` | 8 | 1466 | 54 | yes | L0-r013, L0-r017, L0-r042, L0-r043 … |
| L0-r046 | physics | `L0.physics.touch-lifecycle-3`, `L0.physics.physics-shadow-relations-1` | 6 | 1092 | 46 | yes | L0-r017, L0-r042, L0-r043, L0-r044 … |
| L0-r047 | physics | `L0.physics.physics-shadow-relations-2`, `L0.physics.motion-primitives-1` | 5 | 1378 | 52 | yes | L0-r042, L0-r043, L0-r045, L0-r046 |
| L0-r048 | physics | `L0.physics.conveyor-base-velocity-1`, `L0.physics.entity-water-state-1`, `L0.physics.entity-push-sweep-1` | 3 | 1418 | 53 | yes | L0-r042, L0-r043, L0-r044, L0-r045 … |
| L0-r049 | physics | `L0.physics.entity-four-bump-move-1` | 1 | 2173 | 67 | yes | L0-r020, L0-r043, L0-r044, L0-r045 … |
| L0-r050 | physics | `L0.physics.pushed-entity-transaction-1` | 4 | 581 | 36 | yes | L0-r020, L0-r042, L0-r043, L0-r044 … |
| L0-r051 | physics | `L0.physics.pushed-entity-transaction-2` | 3 | 1735 | 59 | yes | L0-r020, L0-r042, L0-r043, L0-r044 … |
| L0-r052 | physics | `L0.physics.pushed-entity-transaction-3` | 2 | 334 | 32 | yes | L0-r020, L0-r042, L0-r043, L0-r044 … |
| L0-r053 | physics | `L0.physics.pushed-entity-transaction-4` | 6 | 2517 | 74 | yes | L0-r020, L0-r042, L0-r043, L0-r044 … |
| L0-r054 | physics | `L0.physics.linear-push-1` | 4 | 1862 | 61 | yes | L0-r020, L0-r042, L0-r043, L0-r047 … |
| L0-r055 | physics | `L0.physics.pusher-blocked-reconciliation-1` | 5 | 2616 | 76 | yes | L0-r019, L0-r020, L0-r030, L0-r045 … |
| L0-r056 | physics | `L0.physics.noclip-movetype-1` | 1 | 597 | 37 | yes | L0-r019, L0-r020, L0-r042, L0-r043 |
| L0-r057 | physics | `L0.physics.parented-entity-motion-1` | 2 | 1128 | 47 | yes | L0-r020, L0-r030, L0-r045, L0-r047 … |
| L0-r058 | physics | `L0.physics.custom-movetype-1` | 1 | 997 | 44 | yes | L0-r004, L0-r019, L0-r020, L0-r043 … |
| L0-r059 | physics | `L0.physics.step-movetype-1` | 1 | 1006 | 45 | yes | L0-r004, L0-r020, L0-r022, L0-r043 … |
| L0-r060 | physics | `L0.physics.fly-collision-resolution-1` | 1 | 892 | 42 | yes | L0-r045, L0-r046, L0-r047, L0-r048 |
| L0-r061 | physics | `L0.physics.toss-movetype-1` | 1 | 1290 | 50 | yes | L0-r004, L0-r019, L0-r043, L0-r044 … |
| L0-r062 | physics | `L0.physics.entity-simulation-dispatch-1` | 2 | 1444 | 53 | yes | L0-r020, L0-r048, L0-r055, L0-r056 … |
| L0-r063 | physics | `L0.physics.entity-simulation-dispatch-2`, `L0.physics.physics-prop-construction-1`, `L0.physics.physics-prop-construction-2` | 4 | 1346 | 51 | yes | L0-r020, L0-r042, L0-r043, L0-r044 … |
| L0-r064 | physics | `L0.physics.ornament-physics-shape-1`, `L0.physics.physics-object-pickup-limit-1`, `L0.physics.impact-damage-eligibility-1` | 5 | 714 | 39 | yes | L0-r042, L0-r043, L0-r046, L0-r047 … |
| L0-r065 | physics | `L0.physics.damage-record-packet-1`, `L0.physics.collision-damage-queue-1` | 7 | 582 | 36 | yes | L0-r042, L0-r043 |
| L0-r066 | physics | `L0.physics.collision-damage-queue-2`, `L0.physics.physics-attacker-attribution-1` | 6 | 1288 | 50 | yes | L0-r042, L0-r043, L0-r065 |
| L0-r067 | physics | `L0.physics.impact-damage-resolution-1` | 5 | 2669 | 77 | yes | L0-r064, L0-r065, L0-r066 |
| L0-r068 | physics | `L0.physics.surface-impact-feedback-1` | 6 | 1878 | 62 | yes | L0-r042, L0-r043, L0-r066 |
| L0-r069 | physics | `L0.physics.surface-impact-feedback-2`, `L0.physics.friction-feedback-1`, `L0.physics.breakable-collision-lifecycle-1`, `L0.physics.breakable-collision-lifecycle-2` | 7 | 1260 | 50 | yes | L0-r042, L0-r043, L0-r064, L0-r066 … |
| L0-r070 | physics | `L0.physics.breakable-debris-1` | 5 | 1906 | 62 | yes | L0-r013, L0-r020, L0-r042, L0-r043 … |
| L0-r071 | physics | `L0.physics.breakable-debris-2` | 2 | 1340 | 51 | yes | L0-r013, L0-r020, L0-r042, L0-r043 … |
| L0-r072 | physics | `L0.physics.rope-runtime-1` | 6 | 1469 | 54 | yes | L0-r015, L0-r016, L0-r042, L0-r045 |
| L0-r073 | physics | `L0.physics.rope-runtime-2`, `L0.physics.rope-break-release-1` | 4 | 734 | 39 | yes | L0-r015, L0-r016, L0-r042, L0-r045 … |
| L0-r074 | rules_cvars | `L0.rules_cvars.named-target-dispatch-1`, `L0.rules_cvars.named-target-dispatch-2`, `L0.rules_cvars.ammo-definition-table-1`, `L0.rules_cvars.ammo-definition-table-2`, `L0.rules_cvars.ammo-definition-table-3`, `L0.rules_cvars.timed-law-channel-1` | 5 | 1228 | 49 | yes | — |
| L0-r075 | rules_cvars | `L0.rules_cvars.timed-law-channel-2` | 1 | 399 | 33 | yes | — |
| L0-r076 | save_restore | `L0.save_restore.save-field-headers-1`, `L0.save_restore.on-save-transform-prep-1`, `L0.save_restore.physics-save-eligibility-1`, `L0.save_restore.physics-save-eligibility-2`, `L0.save_restore.base-datamap-save-1`, `L0.save_restore.base-datamap-save-2`, `L0.save_restore.restore-buffer-readers-1`, `L0.save_restore.restore-buffer-readers-2`, `L0.save_restore.restore-field-clear-1` | 8 | 1081 | 46 | yes | L0-r020 |
| L0-r077 | save_restore | `L0.save_restore.restore-basic-types-1`, `L0.save_restore.restore-extended-types-1`, `L0.save_restore.restore-datamap-block-1` | 3 | 1331 | 51 | yes | L0-r076 |
| L0-r078 | save_restore | `L0.save_restore.base-entity-restore-1`, `L0.save_restore.base-entity-restore-2` | 1 | 723 | 39 | yes | L0-r019, L0-r077 |
| L0-r079 | sound_list | `L0.sound_list.client-sound-index-1`, `L0.sound_list.client-sound-index-2`, `L0.sound_list.client-sound-index-3`, `L0.sound_list.sound-record-lookup-1`, `L0.sound_list.sound-record-lookup-2`, `L0.sound_list.sound-record-lookup-3`, `L0.sound_list.sound-record-lookup-4` | 2 | 264 | 30 | yes | — |
| L0-r080 | triggers | `L0.triggers.teleport-trigger-spawn-1`, `L0.triggers.multiple-trigger-spawn-1`, `L0.triggers.trigger-class-admission-1`, `L0.triggers.trigger-class-admission-2`, `L0.triggers.trigger-class-admission-3`, `L0.triggers.trigger-endtouch-output-1`, `L0.triggers.lock-key-use-priority-1`, `L0.triggers.lock-key-use-priority-2`, `L0.triggers.lock-key-use-priority-3` | 8 | 1291 | 50 | yes | L0-r013, L0-r019 |
| L0-r081 | movers_doors | `L0.movers_doors.l0-entity-core-func-brush-spawn-1` | 1 | 365 | 32 | yes | L0-r013, L0-r019 |
| L0-r082 | harness | `L0.harness.test-instrument-1`, `L0.harness.test-instrument-2`, `L0.harness.test-instrument-3` | 0 | 0 | 25 | yes | — |
| L0-r083 | tooling | `L0.tooling.ledger-port-citations-1`, `L0.tooling.default-stubs-1` | 0 | 0 | 25 | yes | — |
| L0-r084 | physics | `L0.physics.l0-physics-constraint-entity-family-1` | 7 | 952 | 44 | yes | L0-r024, L0-r042, L0-r043, L0-r063 … |
| L0-r085 | physics | `L0.physics.l0-physics-constraint-entity-family-2` | 3 | 1137 | 47 | yes | L0-r024, L0-r042, L0-r043, L0-r063 … |
| L0-r086 | physics | `L0.physics.l0-physics-constraint-entity-family-3` | 4 | 447 | 34 | yes | L0-r024, L0-r042, L0-r043, L0-r063 … |
| L0-r087 | movers_doors | `L0.movers_doors.l0-entity-io-prop-switch-sound-and-reset-1`, `L0.movers_doors.l0-movers-doors-door-arrival-binding-1` | 5 | 1234 | 49 | yes | L0-r006, L0-r024, L0-r037, L0-r039 … |
| L0-r088 | parked | `L0.parked.l0-physics-ragdoll-physics-asset-import-1`, `L0.parked.l0-save-restore-reliable-native-slot-storage-1`, `L0.parked.l0-save-restore-content-compatibility-preflight-1`, `L0.parked.l0-save-restore-semantic-save-diagnostics-1`, `L0.parked.l0-save-restore-retail-save-roster-match-1`, `L0.parked.l0-save-restore-retail-save-roster-match-2`, `L0.parked.l0-save-restore-retail-save-native-slot-import-1` | 0 | 0 | 25 | yes | L0-r027, L0-r028, L0-r077, L0-r078 … |
