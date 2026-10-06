# Worker runs — L2-character

Each run is one worker session (any agent, `../method.md`): the slice briefs listed, in order, in one go (`briefs/`). Bundled from
consecutive slices of one subsystem up to ~1.5 KB of retail code or 8 functions. Estimated minutes =
25 fixed + 20 per KB of retail code.

| run | subsystem | briefs | functions | retail B | est. min | ready | after |
|---|---|---|---|---|---|---|---|
| L2-r001 | combat | `L2.combat.template-registry-1`, `L2.combat.string-group-bounds-1`, `L2.combat.character-classification-1` | 7 | 1033 | 45 | yes | L0-r082 |
| L2-r002 | combat | `L2.combat.character-startup-sync-1`, `L2.combat.character-restore-1` | 4 | 620 | 37 | no | L0-r023, L0-r078, L0-r082, L1-r011 … |
| L2-r003 | combat | `L2.combat.discipline-flag-metadata-1`, `L2.combat.misc-flag-toggle-1`, `L2.combat.discipline-flag-removal-1`, `L2.combat.active-armor-1`, `L2.combat.inventory-admission-1` | 7 | 900 | 43 | yes | L0-r082 |
| L2-r004 | combat | `L2.combat.expression-pipeline-1` | 4 | 835 | 41 | yes | L0-r082 |
| L2-r005 | combat | `L2.combat.expression-pipeline-2`, `L2.combat.blood-explosion-1` | 4 | 1092 | 46 | yes | L0-r082 |
| L2-r006 | combat | `L2.combat.discipline-visuals-1` | 3 | 812 | 41 | yes | L0-r082 |
| L2-r007 | combat | `L2.combat.discipline-visuals-2` | 3 | 3431 | 92 | yes | L0-r082 |
| L2-r008 | combat | `L2.combat.discipline-targeting-1`, `L2.combat.discipline-targeting-2` | 4 | 972 | 44 | yes | L0-r082, L2-r001 |
| L2-r009 | combat | `L2.combat.dice-roll-1`, `L2.combat.soak-index-1`, `L2.combat.reaction-records-1` | 4 | 1299 | 50 | yes | L0-r082 |
| L2-r010 | combat | `L2.combat.history-effects-1`, `L2.combat.take-health-1`, `L2.combat.take-health-2` | 5 | 745 | 40 | yes | L0-r082 |
| L2-r011 | combat | `L2.combat.damage-resolution-1` | 1 | 66 | 26 | no | L0-r014, L0-r067, L0-r082, L2-r003 |
| L2-r012 | combat | `L2.combat.damage-resolution-2`, `L2.combat.damage-resolution-3`, `L2.combat.trace-attack-1` | 6 | 1504 | 54 | no | L0-r014, L0-r065, L0-r067, L0-r082 … |
| L2-r013 | combat | `L2.combat.trace-attack-2`, `L2.combat.trace-attack-3` | 4 | 1198 | 48 | no | L0-r065, L0-r082, L2-r011, L2-r012 |
| L2-r014 | combat | `L2.combat.life-progression-1`, `L2.combat.damage-flinch-1` | 4 | 570 | 36 | yes | L0-r082, L2-r011, L2-r012 |
| L2-r015 | combat | `L2.combat.damage-effects-1` | 2 | 2224 | 68 | no | L0-r082, L1-r006, L1-r007, L1-r008 … |
| L2-r016 | combat | `L2.combat.corpse-death-flow-1`, `L2.combat.corpse-death-flow-2` | 4 | 617 | 37 | yes | L0-r082, L2-r011, L2-r012 |
| L2-r017 | combat | `L2.combat.corpse-gib-1` | 5 | 637 | 37 | no | L0-r013, L0-r018, L0-r019, L0-r020 … |
| L2-r018 | combat | `L2.combat.corpse-gib-2` | 2 | 1157 | 48 | no | L0-r013, L0-r018, L0-r019, L0-r020 … |
| L2-r019 | combat | `L2.combat.torpor-entry-1`, `L2.combat.activity-translation-1`, `L2.combat.activity-translation-2` | 8 | 1114 | 47 | yes | L0-r082, L2-r016 |
| L2-r020 | combat | `L2.combat.player-block-reactions-1` | 3 | 15 | 25 | yes | L0-r082 |
| L2-r021 | combat | `L2.combat.body-look-direction-1`, `L2.combat.body-look-direction-2` | 8 | 1206 | 49 | no | L0-r082, L1-r009 |
| L2-r022 | combat | `L2.combat.pose-smoothing-1` | 1 | 509 | 35 | no | L0-r082, L1-r005 |
| L2-r023 | combat | `L2.combat.model-hull-size-1` | 4 | 781 | 40 | yes | L0-r082 |
| L2-r024 | combat | `L2.combat.impact-collision-1` | 4 | 659 | 38 | no | L0-r067, L0-r082, L1-r010 |
| L2-r025 | combat | `L2.combat.transmit-gate-1` | 1 | 236 | 30 | yes | L0-r082 |
| L2-r026 | combat | `L2.combat.character-precache-1` | 1 | 313 | 31 | no | L0-r005, L0-r082, L2-r023 |
| L2-r027 | combat | `L2.combat.can-talk-1`, `L2.combat.handle-interaction-1`, `L2.combat.highlight-material-1`, `L2.combat.dialog-reaction-1` | 6 | 1277 | 50 | yes | L0-r082 |
| L2-r028 | combat | `L2.combat.hot-healing-1`, `L2.combat.hot-healing-2` | 3 | 747 | 40 | yes | L0-r082 |
| L2-r029 | inventory_ui_items | `L2.inventory_ui_items.quest-catalogue-load-1`, `L2.inventory_ui_items.quest-catalogue-load-2` | 2 | 944 | 43 | no | L0-r002, L0-r082 |
| L2-r030 | inventory_ui_items | `L2.inventory_ui_items.quest-index-fallback-1`, `L2.inventory_ui_items.quest-index-fallback-2`, `L2.inventory_ui_items.inventory-section-routing-1`, `L2.inventory_ui_items.keyring-records-1`, `L2.inventory_ui_items.item-trait-groups-1` | 7 | 1171 | 48 | yes | L0-r082, L2-r029 |
| L2-r031 | inventory_ui_items | `L2.inventory_ui_items.item-trait-groups-2`, `L2.inventory_ui_items.base-fighting-items-1`, `L2.inventory_ui_items.autopickup-refusal-1` | 5 | 624 | 37 | yes | L0-r082 |
| L2-r032 | inventory_ui_items | `L2.inventory_ui_items.passkey-inspection-1`, `L2.inventory_ui_items.passkey-inspection-2` | 2 | 81 | 27 | no | L0-r034, L0-r082 |
| L2-r033 | inventory_ui_items | `L2.inventory_ui_items.generic-item-deploy-1`, `L2.inventory_ui_items.generic-item-kick-1`, `L2.inventory_ui_items.intrusion-check-1`, `L2.inventory_ui_items.hacking-check-1`, `L2.inventory_ui_items.hacking-check-2`, `L2.inventory_ui_items.feed-heal-amount-1`, `L2.inventory_ui_items.terminal-health-guard-1`, `L2.inventory_ui_items.barter-session-inputs-1` | 8 | 1407 | 52 | yes | L0-r082, L2-r009 |
| L2-r034 | inventory_ui_items | `L2.inventory_ui_items.barter-pricing-1`, `L2.inventory_ui_items.money-object-vector-1` | 3 | 631 | 37 | yes | L0-r082 |
| L2-r035 | rpg | `L2.rpg.rpg-strings-1` | 6 | 830 | 41 | yes | L0-r082 |
| L2-r036 | rpg | `L2.rpg.stat-catalog-1` | 6 | 726 | 39 | yes | L0-r082, L2-r035 |
| L2-r037 | rpg | `L2.rpg.stat-catalog-2` | 6 | 2185 | 68 | yes | L0-r082, L2-r035 |
| L2-r038 | rpg | `L2.rpg.stat-catalog-3` | 6 | 2738 | 78 | yes | L0-r082, L2-r035 |
| L2-r039 | rpg | `L2.rpg.stat-catalog-4` | 1 | 460 | 34 | yes | L0-r082, L2-r035 |
| L2-r040 | rpg | `L2.rpg.stat-values-1` | 6 | 1199 | 48 | yes | L0-r082, L2-r009, L2-r036, L2-r037 … |
| L2-r041 | rpg | `L2.rpg.stat-values-2`, `L2.rpg.stat-values-3`, `L2.rpg.stat-values-4` | 5 | 679 | 38 | yes | L0-r082, L2-r009, L2-r036, L2-r037 … |
| L2-r042 | rpg | `L2.rpg.feat-effect-catalog-1` | 5 | 1476 | 54 | yes | L0-r082, L2-r035, L2-r040, L2-r041 |
| L2-r043 | rpg | `L2.rpg.effect-math-1` | 5 | 748 | 40 | yes | L0-r082, L2-r040, L2-r041, L2-r042 |
| L2-r044 | rpg | `L2.rpg.effect-math-2` | 4 | 1684 | 58 | yes | L0-r082, L2-r040, L2-r041, L2-r042 |
| L2-r045 | rpg | `L2.rpg.damage-packets-1`, `L2.rpg.damage-packets-2` | 7 | 1236 | 49 | yes | L0-r082, L2-r035 |
| L2-r046 | rpg | `L2.rpg.discipline-fx-1` | 6 | 1184 | 48 | no | L0-r005, L0-r082, L2-r035 |
| L2-r047 | rpg | `L2.rpg.discipline-fx-2` | 3 | 712 | 39 | no | L0-r005, L0-r082, L2-r035 |
| L2-r048 | rpg | `L2.rpg.discipline-targets-1` | 6 | 1999 | 64 | yes | L0-r082, L2-r035, L2-r042, L2-r046 … |
| L2-r049 | rpg | `L2.rpg.discipline-targets-2` | 5 | 1196 | 48 | yes | L0-r082, L2-r035, L2-r042, L2-r046 … |
| L2-r050 | rpg | `L2.rpg.critter-scopes-1` | 3 | 685 | 38 | yes | L0-r082, L2-r035 |
| L2-r051 | rpg | `L2.rpg.reaction-catalog-1` | 5 | 1193 | 48 | yes | L0-r082, L2-r035, L2-r050 |
| L2-r052 | rpg | `L2.rpg.dice-tables-1`, `L2.rpg.history-catalog-1` | 5 | 1445 | 53 | yes | L0-r082, L2-r035, L2-r042, L2-r050 |
| L2-r053 | rpg | `L2.rpg.leveling-templates-1`, `L2.rpg.inventory-sections-1` | 5 | 1271 | 50 | yes | L0-r082, L2-r035, L2-r040, L2-r041 |
| L2-r054 | rpg | `L2.rpg.activation-types-1` | 2 | 641 | 38 | yes | L0-r082, L2-r035, L2-r053 |
| L2-r055 | rpg | `L2.rpg.botch-tables-1` | 4 | 1635 | 57 | yes | L0-r082, L2-r035, L2-r045, L2-r052 |
| L2-r056 | rpg | `L2.rpg.starting-equipment-1` | 3 | 878 | 42 | yes | L0-r082, L2-r053, L2-r054 |
| L2-r057 | rpg | `L2.rpg.multiplayer-equipment-1` | 3 | 1240 | 49 | yes | L0-r082, L2-r053, L2-r054 |
| L2-r058 | rpg | `L2.rpg.vendor-catalog-1` | 3 | 1051 | 46 | yes | L0-r082, L2-r053, L2-r054 |
| L2-r059 | rpg | `L2.rpg.quotes-1` | 2 | 621 | 37 | yes | L0-r082, L2-r035 |
| L2-r060 | rpg | `L2.rpg.sound-slots-1` | 5 | 1433 | 53 | no | L0-r005, L0-r082, L2-r035 |
| L2-r061 | rpg | `L2.rpg.weapon-sounds-1`, `L2.rpg.rpg-bootstrap-1` | 2 | 985 | 44 | yes | L0-r082, L2-r035, L2-r036, L2-r037 … |
| L2-r062 | rpg | `L2.rpg.stat-lists-1`, `L2.rpg.experience-awards-1` | 7 | 1395 | 52 | yes | L0-r082, L2-r040, L2-r041, L2-r043 … |
| L2-r063 | rpg | `L2.rpg.trait-purchases-1` | 5 | 660 | 38 | yes | L0-r082, L2-r040, L2-r041, L2-r043 … |
| L2-r064 | rpg | `L2.rpg.trait-purchases-2` | 4 | 1011 | 45 | yes | L0-r082, L2-r040, L2-r041, L2-r043 … |
| L2-r065 | rpg | `L2.rpg.feeding-policy-1`, `L2.rpg.feeding-policy-2`, `L2.rpg.melee-rolls-1` | 5 | 1016 | 45 | yes | L0-r082, L2-r009, L2-r045, L2-r052 … |
| L2-r066 | rpg | `L2.rpg.stat-sequences-1` | 3 | 765 | 40 | no | L0-r082, L1-r002, L2-r062 |
| L2-r067 | rpg | `L2.rpg.prayer-1`, `L2.rpg.skill-spawn-1` | 3 | 397 | 33 | yes | L0-r082, L2-r033, L2-r061, L2-r062 |
| L2-r068 | rpg | `L2.rpg.discipline-events-1` | 6 | 1823 | 61 | yes | L0-r082, L2-r040, L2-r041, L2-r043 … |
| L2-r069 | rpg | `L2.rpg.discipline-events-2` | 4 | 1549 | 55 | yes | L0-r082, L2-r040, L2-r041, L2-r043 … |
| L2-r070 | rpg | `L2.rpg.discipline-events-3`, `L2.rpg.trait-group-lifecycle-1` | 6 | 998 | 44 | yes | L0-r082, L2-r003, L2-r040, L2-r041 … |
| L2-r071 | rpg | `L2.rpg.trait-group-lifecycle-2` | 5 | 788 | 40 | yes | L0-r082, L2-r003, L2-r042, L2-r043 … |
| L2-r072 | rpg | `L2.rpg.template-transition-1` | 6 | 1994 | 64 | yes | L0-r082, L2-r040, L2-r041, L2-r053 … |
| L2-r073 | rpg | `L2.rpg.template-transition-2` | 3 | 3300 | 89 | yes | L0-r082, L2-r040, L2-r041, L2-r053 … |
| L2-r074 | rpg | `L2.rpg.template-transition-3` | 1 | 207 | 29 | yes | L0-r082, L2-r040, L2-r041, L2-r053 … |
| L2-r075 | rpg | `L2.rpg.radius-damage-1` | 1 | 1373 | 52 | yes | L0-r082, L2-r045 |
| L2-r076 | rpg | `L2.rpg.bullet-triggers-1`, `L2.rpg.discipline-damage-1` | 2 | 1196 | 48 | yes | L0-r082, L2-r045, L2-r048, L2-r049 … |
| L2-r077 | rpg | `L2.rpg.discipline-visuals-1` | 2 | 3815 | 100 | yes | L0-r082, L2-r046, L2-r047, L2-r070 … |
| L2-r078 | rpg | `L2.rpg.shaky-hands-1` | 2 | 438 | 34 | yes | L0-r082, L2-r062 |
| L2-r079 | rpg | `L2.rpg.celerity-1`, `L2.rpg.celerity-2`, `L2.rpg.protean-1`, `L2.rpg.bloodshield-1` | 3 | 443 | 34 | no | L0-r074, L0-r075, L0-r082, L2-r045 … |
| L2-r080 | rpg | `L2.rpg.obfuscate-1`, `L2.rpg.obfuscate-2` | 1 | 452 | 34 | yes | L0-r082, L2-r046, L2-r047, L2-r070 … |
| L2-r081 | rpg | `L2.rpg.grapple-1` | 3 | 1262 | 50 | yes | L0-r082, L2-r061, L2-r062 |
| L2-r082 | weapons | `L2.weapons.weapon-item-data-1`, `L2.weapons.ammo-capacity-gate-1` | 7 | 1205 | 49 | yes | L0-r082, L2-r061 |
| L2-r083 | weapons | `L2.weapons.weapon-magazines-1`, `L2.weapons.weapon-record-word-1`, `L2.weapons.weapon-mode-fallback-1` | 4 | 926 | 43 | yes | L0-r082, L2-r082 |
| L2-r084 | weapons | `L2.weapons.weapon-create-spawn-1` | 4 | 1527 | 55 | no | L0-r013, L0-r042, L0-r043, L0-r045 … |
| L2-r085 | weapons | `L2.weapons.weapon-slot-array-1`, `L2.weapons.weapon-slot-array-2` | 2 | 346 | 32 | yes | L0-r082 |
| L2-r086 | weapons | `L2.weapons.weapon-equip-1` | 2 | 1173 | 48 | no | L0-r013, L0-r082, L2-r084, L2-r085 |
| L2-r087 | weapons | `L2.weapons.weapon-pickup-gates-1` | 3 | 475 | 34 | yes | L0-r082, L2-r019, L2-r082 |
| L2-r088 | weapons | `L2.weapons.weapon-sound-1` | 4 | 1480 | 54 | no | L0-r004, L0-r082, L2-r082, L2-r083 |
| L2-r089 | weapons | `L2.weapons.weapon-acquisition-1` | 4 | 1718 | 59 | no | L0-r013, L0-r082, L2-r003, L2-r082 … |
| L2-r090 | weapons | `L2.weapons.weapon-selection-1` | 5 | 1570 | 56 | yes | L0-r082, L2-r003, L2-r030, L2-r031 … |
| L2-r091 | weapons | `L2.weapons.weapon-drop-1` | 2 | 425 | 33 | no | L0-r020, L0-r082, L2-r084, L2-r086 |
| L2-r092 | weapons | `L2.weapons.weapon-kill-delay-1`, `L2.weapons.weapon-respawn-1` | 2 | 289 | 31 | yes | L0-r082, L2-r084 |
| L2-r093 | weapons | `L2.weapons.weapon-save-restore-1`, `L2.weapons.weapon-save-restore-2` | 2 | 128 | 28 | no | L0-r082, L1-r011, L2-r084, L2-r086 |
| L2-r094 | weapons | `L2.weapons.weapon-price-cache-1` | 1 | 300 | 31 | yes | L0-r082, L2-r034 |
| L2-r095 | weapons | `L2.weapons.weapon-animation-idle-1` | 5 | 2046 | 65 | no | L0-r082, L1-r002, L2-r094 |
| L2-r096 | weapons | `L2.weapons.weapon-animation-idle-2`, `L2.weapons.weapon-animation-idle-3` | 0 | 0 | 25 | no | L0-r082, L1-r002, L2-r094 |
| L2-r097 | weapons | `L2.weapons.weapon-reload-1`, `L2.weapons.weapon-melee-busy-frame-1`, `L2.weapons.weapon-spread-default-1`, `L2.weapons.weapon-aim-cache-1` | 5 | 1182 | 48 | yes | L0-r082, L2-r019, L2-r082, L2-r083 … |
| L2-r098 | weapons | `L2.weapons.weapon-pvs-successor-1` | 1 | 697 | 39 | no | L0-r042, L0-r082 |
| L2-r099 | weapons | `L2.weapons.weapon-discipline-flag-index-1`, `L2.weapons.weapon-stat-derived-value-1`, `L2.weapons.weapon-held-target-expiry-1`, `L2.weapons.weapon-projectile-create-1` | 4 | 564 | 36 | yes | L0-r082, L2-r082, L2-r086 |
| L2-r100 | weapons | `L2.weapons.weapon-ranged-victim-damage-1`, `L2.weapons.weapon-ranged-victim-damage-2` | 2 | 1315 | 51 | yes | L0-r082, L2-r045, L2-r083, L2-r099 |
| L2-r101 | weapons | `L2.weapons.weapon-melee-damage-envelope-1`, `L2.weapons.weapon-blood-decals-1` | 3 | 452 | 34 | no | L0-r011, L0-r012, L0-r065, L0-r082 … |
| L2-r102 | entity_core | `L2.entity_core.l2-rpg-discipline-amount-resolution-1` | 1 | 58 | 26 | yes | L0-r082 |
| L2-r103 | entity_core | `L2.entity_core.l2-rpg-discipline-target-acquisition-1` | 1 | 1228 | 49 | no | L0-r034, L0-r042, L0-r082 |
| L2-r104 | entity_core | `L2.entity_core.l2-rpg-discipline-target-acquisition-2` | 1 | 808 | 41 | no | L0-r034, L0-r042, L0-r082 |
| L2-r105 | combat | `L2.combat.l2-combat-particle-removal-1` | 1 | 58 | 26 | yes | L0-r082 |
| L2-r106 | parked | `L2.parked.l2-rpg-discipline-magnitudes-1` | 0 | 0 | 25 | yes | L0-r082, L2-r036, L2-r037, L2-r038 … |
