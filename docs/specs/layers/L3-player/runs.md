# Worker runs — L3-player

Each run is one Codex worker: the slice briefs listed, in order, in one go (`briefs/`). Bundled from
consecutive slices of one subsystem up to ~1.5 KB of retail code or 8 functions. Estimated minutes =
25 fixed + 20 per KB of retail code.

| run | subsystem | briefs | functions | retail B | est. min | ready | after |
|---|---|---|---|---|---|---|---|
| L3-r001 | player | `L3.player.move-solve-1`, `L3.player.move-solve-2` | 5 | 1182 | 48 | yes | L0-r082 |
| L3-r002 | player | `L3.player.ground-surface-1` | 3 | 1404 | 52 | no | L0-r020, L0-r082, L3-r001 |
| L3-r003 | player | `L3.player.ground-surface-2` | 2 | 222 | 29 | no | L0-r020, L0-r082, L3-r001 |
| L3-r004 | player | `L3.player.stuck-duck-1` | 3 | 1727 | 59 | yes | L0-r082, L3-r001 |
| L3-r005 | player | `L3.player.stuck-duck-2` | 3 | 1580 | 56 | yes | L0-r082, L3-r001 |
| L3-r006 | player | `L3.player.water-drowning-1` | 4 | 2583 | 75 | no | L0-r020, L0-r082, L3-r001, L3-r002 … |
| L3-r007 | player | `L3.player.special-movetypes-1` | 2 | 1739 | 59 | yes | L0-r082, L3-r001, L3-r002, L3-r003 |
| L3-r008 | player | `L3.player.walk-air-1` | 2 | 2118 | 66 | yes | L0-r082, L3-r001, L3-r002, L3-r003 … |
| L3-r009 | player | `L3.player.walk-air-2` | 2 | 1046 | 45 | yes | L0-r082, L3-r001, L3-r002, L3-r003 … |
| L3-r010 | player | `L3.player.jump-encumbrance-1`, `L3.player.vehicle-1` | 5 | 981 | 44 | no | L0-r020, L0-r042, L0-r043, L0-r082 … |
| L3-r011 | player | `L3.player.eye-geometry-1` | 4 | 316 | 31 | yes | L0-r082, L3-r002, L3-r003, L3-r004 … |
| L3-r012 | player | `L3.player.eye-geometry-2` | 3 | 1515 | 55 | yes | L0-r082, L3-r002, L3-r003, L3-r004 … |
| L3-r013 | player | `L3.player.eye-geometry-3`, `L3.player.view-motion-1` | 3 | 1110 | 47 | yes | L0-r082, L3-r002, L3-r003, L3-r004 … |
| L3-r014 | player | `L3.player.view-motion-2`, `L3.player.view-motion-3`, `L3.player.illumination-1` | 4 | 482 | 34 | yes | L0-r082, L3-r008, L3-r009, L3-r011 … |
| L3-r015 | player | `L3.player.weapon-selection-1`, `L3.player.weapon-selection-2` | 6 | 1329 | 51 | no | L0-r082, L2-r084, L2-r086 |
| L3-r016 | player | `L3.player.animation-arbiter-1`, `L3.player.animation-arbiter-2`, `L3.player.animation-arbiter-3` | 3 | 791 | 40 | yes | L0-r082, L3-r008, L3-r009, L3-r010 … |
| L3-r017 | player | `L3.player.postthink-entities-1` | 2 | 693 | 39 | no | L0-r013, L0-r062, L0-r063, L0-r082 … |
| L3-r018 | player | `L3.player.use-impulses-1` | 2 | 4107 | 105 | no | L0-r014, L0-r023, L0-r082, L2-r017 … |
| L3-r019 | player | `L3.player.use-impulses-2`, `L3.player.use-impulses-3`, `L3.player.item-transactions-1` | 6 | 1103 | 47 | no | L0-r014, L0-r023, L0-r034, L0-r082 … |
| L3-r020 | player | `L3.player.inventory-policy-ammo-1`, `L3.player.inventory-reset-1` | 6 | 583 | 36 | no | L0-r004, L0-r082, L2-r085, L2-r090 … |
| L3-r021 | player | `L3.player.feed-admission-1` | 2 | 2076 | 66 | no | L0-r082, L2-r062, L3-r011, L3-r012 … |
| L3-r022 | player | `L3.player.feed-admission-2` | 3 | 339 | 32 | no | L0-r082, L2-r062, L3-r011, L3-r012 … |
| L3-r023 | player | `L3.player.feed-clearance-1` | 2 | 553 | 36 | yes | L0-r082, L3-r011, L3-r012, L3-r013 … |
| L3-r024 | player | `L3.player.feed-rat-1`, `L3.player.feed-seductive-1`, `L3.player.feed-zombie-1`, `L3.player.feed-zombie-2` | 3 | 1138 | 47 | no | L0-r082, L2-r065, L3-r016, L3-r021 … |
| L3-r025 | player | `L3.player.discipline-effects-1`, `L3.player.discipline-effects-2` | 7 | 1419 | 53 | no | L0-r082, L2-r062, L2-r070, L2-r071 … |
| L3-r026 | player | `L3.player.obfuscate-1` | 2 | 1596 | 56 | yes | L0-r082, L3-r021, L3-r022, L3-r025 |
| L3-r027 | player | `L3.player.status-cache-1` | 2 | 980 | 44 | yes | L0-r082, L3-r015, L3-r016, L3-r024 … |
| L3-r028 | player | `L3.player.status-cache-2`, `L3.player.status-cache-3` | 4 | 1484 | 54 | yes | L0-r082, L3-r015, L3-r016, L3-r024 … |
| L3-r029 | player | `L3.player.discipline-target-cast-1`, `L3.player.discipline-target-cast-2`, `L3.player.vomit-1`, `L3.player.payphone-1` | 8 | 1027 | 45 | yes | L0-r082, L3-r011, L3-r012, L3-r013 … |
| L3-r030 | player | `L3.player.law-pass-1` | 4 | 851 | 42 | no | L0-r074, L0-r075, L0-r082, L3-r027 … |
| L3-r031 | player | `L3.player.law-pass-2`, `L3.player.law-pass-3` | 1 | 1390 | 52 | no | L0-r074, L0-r075, L0-r082, L3-r027 … |
| L3-r032 | player | `L3.player.prethink-1`, `L3.player.prethink-2` | 3 | 736 | 39 | no | L0-r020, L0-r082, L3-r006, L3-r010 … |
| L3-r033 | player | `L3.player.whisper-1` | 2 | 608 | 37 | yes | L0-r082 |
| L3-r034 | player | `L3.player.player-events-1`, `L3.player.player-events-2` | 4 | 699 | 39 | no | L0-r022, L0-r023, L0-r082 |
| L3-r035 | player | `L3.player.class-touch-1` | 3 | 103 | 27 | yes | L0-r082, L3-r002, L3-r003 |
| L3-r036 | player | `L3.player.damage-1` | 3 | 971 | 44 | no | L0-r011, L0-r012, L0-r014, L0-r020 … |
| L3-r037 | player | `L3.player.damage-2`, `L3.player.damage-3`, `L3.player.damage-4`, `L3.player.death-cleanup-1` | 6 | 929 | 43 | no | L0-r011, L0-r012, L0-r014, L0-r019 … |
| L3-r038 | player | `L3.player.kill-humanity-1`, `L3.player.awards-1` | 5 | 1224 | 49 | no | L0-r082, L2-r001, L2-r062, L3-r037 |
| L3-r039 | player | `L3.player.command-pipeline-1` | 4 | 1426 | 53 | no | L0-r042, L0-r043, L0-r082, L3-r004 … |
| L3-r040 | player | `L3.player.command-pipeline-2`, `L3.player.player-save-1`, `L3.player.player-save-2`, `L3.player.player-save-3` | 6 | 1373 | 52 | no | L0-r042, L0-r043, L0-r082, L2-r002 … |
| L3-r041 | player | `L3.player.spawn-selection-1`, `L3.player.spawn-selection-2`, `L3.player.spawn-selection-3` | 4 | 1455 | 53 | yes | L0-r082, L3-r002, L3-r003, L3-r004 … |
| L3-r042 | player | `L3.player.player-restore-1` | 4 | 1050 | 46 | no | L0-r082, L2-r002, L2-r030, L2-r084 … |
| L3-r043 | player | `L3.player.player-restore-2`, `L3.player.spawn-initialization-1` | 7 | 1308 | 51 | no | L0-r026, L0-r082, L2-r002, L2-r030 … |
| L3-r044 | player | `L3.player.spawn-initialization-2`, `L3.player.spawn-initialization-3`, `L3.player.spawn-initialization-4`, `L3.player.spawn-initialization-5`, `L3.player.entity-removal-1` | 3 | 586 | 36 | no | L0-r026, L0-r042, L0-r043, L0-r082 … |
| L3-r045 | player | `L3.player.physics-shadow-1` | 2 | 1126 | 47 | no | L0-r020, L0-r042, L0-r043, L0-r082 … |
| L3-r046 | player | `L3.player.head-turn-1` | 1 | 534 | 35 | yes | L0-r082, L3-r011, L3-r012, L3-r013 … |
| L3-r047 | entity_core | `L3.entity_core.l3-player-client-commands-1`, `L3.entity_core.l3-player-client-commands-2` | 2 | 665 | 38 | no | L0-r024, L0-r033, L0-r082 |
| L3-r048 | combat | `L3.combat.l3-player-camera-shot-registry-1`, `L3.combat.l3-player-camera-shot-registry-2`, `L3.combat.l3-player-camera-shot-registry-3`, `L3.combat.l3-player-camera-shot-registry-4` | 2 | 420 | 33 | no | L0-r002, L0-r082 |
| L3-r049 | physics | `L3.physics.l3-player-physcannon-1` | 6 | 2072 | 65 | no | L0-r064, L0-r082, L2-r099 |
| L3-r050 | physics | `L3.physics.l3-player-physcannon-2`, `L3.physics.l3-player-physcannon-3`, `L3.physics.l3-player-physcannon-4` | 3 | 254 | 30 | no | L0-r064, L0-r082, L2-r099, L3-r049 |
| L3-r051 | parked | `L3.parked.l3-player-discipline-client-disable-1`, `L3.parked.l3-player-discipline-client-disable-2`, `L3.parked.l3-physics-l3-player-physcannon-icons-1`, `L3.parked.l3-player-input-keyboard-mouse-1`, `L3.parked.l3-player-input-keyboard-mouse-2`, `L3.parked.l3-player-input-reserved-keys-1`, `L3.parked.l3-player-input-reserved-keys-2`, `L3.parked.l3-player-input-ds4-edge-combat-stick-1`, `L3.parked.l3-player-input-ds4-edge-combat-stick-2`, `L3.parked.l3-player-input-user-settings-1`, `L3.parked.l3-player-input-user-settings-2` | 0 | 0 | 25 | yes | L0-r082, L3-r006, L3-r008, L3-r009 … |
