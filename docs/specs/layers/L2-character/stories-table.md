| # | story | subsystem | size | rows | records | depends on | ready |
|---|---|---|---|---|---|---|---|
| 1 | [Body, head, and eye direction](stories/L2.combat.body-look-direction.md) · live (3,828 stub hits) | combat | L | 8 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | waits for 1 lower-layer rows |
| 2 | [Template record defaults](stories/L2.combat.template-registry.md) | combat | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 3 | [Quest catalogue loading](stories/L2.inventory_ui_items.quest-catalogue-load.md) | inventory_ui_items | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | waits for 3 lower-layer rows |
| 4 | [RPG string table](stories/L2.rpg.rpg-strings.md) | rpg | M | 6 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 5 | [String group bounds](stories/L2.combat.string-group-bounds.md) | combat | S | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 6 | [Quest index lookup](stories/L2.inventory_ui_items.quest-index-fallback.md) | inventory_ui_items | S | 4 | 1 | Quest catalogue loading, The test instrument: fixtures, entity_call, entity_fiel | yes |
| 7 | [Ammo index and capacity gate](stories/L2.weapons.ammo-capacity-gate.md) | weapons | M | 4 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 8 | [Blood and creature classification](stories/L2.combat.character-classification.md) | combat | M | 3 | 1 | Template record defaults, The test instrument: fixtures, entity_call, entity_fie | yes |
| 9 | [Inventory section routing](stories/L2.inventory_ui_items.inventory-section-routing.md) | inventory_ui_items | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 10 | [Keyring records](stories/L2.inventory_ui_items.keyring-records.md) | inventory_ui_items | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 11 | [Combat character restore](stories/L2.combat.character-restore.md) | combat | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | waits for 2 lower-layer rows |
| 12 | [Item trait group lifecycle](stories/L2.inventory_ui_items.item-trait-groups.md) | inventory_ui_items | M | 3 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 13 | [Base fighting items](stories/L2.inventory_ui_items.base-fighting-items.md) | inventory_ui_items | S | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 14 | [Damage packet lifecycle](stories/L2.rpg.damage-packets.md) | rpg | L | 7 | 1 | RPG string table, The test instrument: fixtures, entity_call, entity_field, reta | yes |
| 15 | [Miscellaneous character flag toggle](stories/L2.combat.misc-flag-toggle.md) | combat | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 16 | [Autopickup refusal](stories/L2.inventory_ui_items.autopickup-refusal.md) | inventory_ui_items | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 17 | [Discipline sound and particle data](stories/L2.rpg.discipline-fx.md) | rpg | L | 9 | 1 | RPG string table, The test instrument: fixtures, entity_call, entity_field, reta | waits for 1 lower-layer rows |
| 18 | [Weapon slot assignment and clearing](stories/L2.weapons.weapon-slot-array.md) | weapons | S | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 19 | [Discipline flag removal](stories/L2.combat.discipline-flag-removal.md) | combat | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 20 | [Active armor selection](stories/L2.combat.active-armor.md) | combat | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 21 | [Critter scope data](stories/L2.rpg.critter-scopes.md) | rpg | M | 3 | 1 | RPG string table, The test instrument: fixtures, entity_call, entity_field, reta | yes |
| 22 | [Generic item kick](stories/L2.inventory_ui_items.generic-item-kick.md) | inventory_ui_items | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 23 | [Reaction catalogue](stories/L2.rpg.reaction-catalog.md) | rpg | M | 5 | 1 | Critter scope data, RPG string table, The test instrument: fixtures, entity_call | yes |
| 24 | [Character expression updates](stories/L2.combat.expression-pipeline.md) | combat | L | 6 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 25 | [Dice table data](stories/L2.rpg.dice-tables.md) | rpg | S | 1 | 1 | RPG string table, The test instrument: fixtures, entity_call, entity_field, reta | yes |
| 26 | [Blood boil explosion](stories/L2.combat.blood-explosion.md) | combat | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 27 | [Discipline particle registry](stories/L2.combat.discipline-visuals.md) | combat | L | 6 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 28 | [Feed heal amount](stories/L2.inventory_ui_items.feed-heal-amount.md) | inventory_ui_items | M | 1 | 2 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 29 | [Discipline target selection](stories/L2.combat.discipline-targeting.md) | combat | L | 4 | 1 | Blood and creature classification, Template record defaults, The test instrument | yes |
| 30 | [Terminal health guard](stories/L2.inventory_ui_items.terminal-health-guard.md) | inventory_ui_items | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 31 | [Inventory section IDs](stories/L2.rpg.inventory-sections.md) | rpg | S | 2 | 1 | RPG string table, The test instrument: fixtures, entity_call, entity_field, reta | yes |
| 32 | [Barter session inputs](stories/L2.inventory_ui_items.barter-session-inputs.md) | inventory_ui_items | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 33 | [Item activation types](stories/L2.rpg.activation-types.md) | rpg | S | 2 | 1 | Inventory section IDs, RPG string table, The test instrument: fixtures, entity_c | yes |
| 34 | [Soak feat index](stories/L2.combat.soak-index.md) | combat | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 35 | [Botch tables](stories/L2.rpg.botch-tables.md) | rpg | M | 4 | 1 | Damage packet lifecycle, Dice table data, RPG string table, The test instrument: | yes |
| 36 | [Reaction table lookup](stories/L2.combat.reaction-records.md) | combat | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 37 | [Money object vector](stories/L2.inventory_ui_items.money-object-vector.md) | inventory_ui_items | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 38 | [Starting equipment](stories/L2.rpg.starting-equipment.md) | rpg | M | 3 | 1 | Item activation types, Inventory section IDs, The test instrument: fixtures, ent | yes |
| 39 | [Character history effects](stories/L2.combat.history-effects.md) | combat | M | 3 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 40 | [Multiplayer equipment](stories/L2.rpg.multiplayer-equipment.md) | rpg | M | 3 | 1 | Item activation types, Inventory section IDs, The test instrument: fixtures, ent | yes |
| 41 | [Health restoration entry points](stories/L2.combat.take-health.md) | combat | S | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 42 | [Vendor item catalogue](stories/L2.rpg.vendor-catalog.md) | rpg | M | 3 | 1 | Item activation types, Inventory section IDs, The test instrument: fixtures, ent | yes |
| 43 | [Damage scaling and resolution](stories/L2.combat.damage-resolution.md) | combat | L | 6 | 1 | Active armor selection, The test instrument: fixtures, entity_call, entity_field | waits for 2 lower-layer rows |
| 44 | [RPG quote data](stories/L2.rpg.quotes.md) | rpg | S | 2 | 1 | RPG string table, The test instrument: fixtures, entity_call, entity_field, reta | yes |
| 45 | [Sound slot registration](stories/L2.rpg.sound-slots.md) | rpg | M | 5 | 1 | RPG string table, The test instrument: fixtures, entity_call, entity_field, reta | waits for 1 lower-layer rows |
| 46 | [Weapon sound selection](stories/L2.rpg.weapon-sounds.md) | rpg | S | 1 | 1 | Sound slot registration, The test instrument: fixtures, entity_call, entity_fiel | yes |
| 47 | [Damage flinch](stories/L2.combat.damage-flinch.md) | combat | M | 1 | 1 | Damage scaling and resolution, The test instrument: fixtures, entity_call, entit | yes |
| 48 | [Weapon PVS target successor](stories/L2.weapons.weapon-pvs-successor.md) | weapons | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | waits for 1 lower-layer rows |
| 49 | [Discipline flag index lookup](stories/L2.weapons.weapon-discipline-flag-index.md) | weapons | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 50 | [Weapon stat-derived value](stories/L2.weapons.weapon-stat-derived-value.md) | weapons | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 51 | [Held-target expiry stamp](stories/L2.weapons.weapon-held-target-expiry.md) | weapons | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 52 | [Activity selection and translation](stories/L2.combat.activity-translation.md) | combat | L | 7 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 53 | [Base player block reactions](stories/L2.combat.player-block-reactions.md) | combat | M | 3 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 54 | [Pose parameter smoothing](stories/L2.combat.pose-smoothing.md) | combat | M | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | waits for 1 lower-layer rows |
| 55 | [Character model and hull size](stories/L2.combat.model-hull-size.md) | combat | M | 4 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 56 | [Impact damage and collision policy](stories/L2.combat.impact-collision.md) | combat | M | 4 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | waits for 2 lower-layer rows |
| 57 | [Character transmit gate](stories/L2.combat.transmit-gate.md) | combat | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 58 | [Combat character precache](stories/L2.combat.character-precache.md) | combat | S | 1 | 1 | Character model and hull size, The test instrument: fixtures, entity_call, entit | waits for 1 lower-layer rows |
| 59 | [Radius damage](stories/L2.rpg.radius-damage.md) | rpg | S | 1 | 1 | Damage packet lifecycle, The test instrument: fixtures, entity_call, entity_fiel | yes |
| 60 | [Character talk gate](stories/L2.combat.can-talk.md) | combat | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 61 | [Bullet trigger damage](stories/L2.rpg.bullet-triggers.md) | rpg | M | 1 | 1 | Damage packet lifecycle, The test instrument: fixtures, entity_call, entity_fiel | yes |
| 62 | [Combat character interaction handler](stories/L2.combat.handle-interaction.md) | combat | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 63 | [Highlight material selection](stories/L2.combat.highlight-material.md) | combat | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 64 | [Dialog reaction modifier](stories/L2.combat.dialog-reaction.md) | combat | M | 3 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 65 | [Discipline amount resolution](stories/L2.entity_core.l2-rpg-discipline-amount-resolution.md) | entity_core | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 66 | [Discipline target acquisition and affects sets](stories/L2.entity_core.l2-rpg-discipline-target-acquisition.md) | entity_core | M | 2 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | waits for 2 lower-layer rows |
| 67 | [Timed particle removal](stories/L2.combat.l2-combat-particle-removal.md) | combat | S | 1 | 1 | The test instrument: fixtures, entity_call, entity_field, retail_site | yes |
| 68 | [Damage hit effects](stories/L2.combat.damage-effects.md) · live (61 stub hits) | combat | M | 2 | 1 | Damage scaling and resolution, Damage packet lifecycle, The test instrument: fix | waits for 1 lower-layer rows |
| 69 | [Death and corpse damage flow](stories/L2.combat.corpse-death-flow.md) · live (19 stub hits) | combat | M | 4 | 1 | Damage scaling and resolution, The test instrument: fixtures, entity_call, entit | yes |
| 70 | [Life-taking progression](stories/L2.combat.life-progression.md) · live (4 stub hits) | combat | M | 3 | 1 | Damage scaling and resolution, The test instrument: fixtures, entity_call, entit | yes |
| 71 | [Weapon item records](stories/L2.weapons.weapon-item-data.md) | weapons | M | 3 | 1 | Weapon sound selection, The test instrument: fixtures, entity_call, entity_field | yes |
| 72 | [Stat definitions and costs](stories/L2.rpg.stat-catalog.md) | rpg | L | 19 | 1 | Discipline sound and particle data, RPG string table, The test instrument: fixtu | yes |
| 73 | [Stat values and bounds](stories/L2.rpg.stat-values.md) | rpg | L | 11 | 1 | Reaction table lookup, Stat definitions and costs, The test instrument: fixtures | yes |
| 74 | [Indexed magazine readers](stories/L2.weapons.weapon-magazines.md) | weapons | S | 2 | 1 | Weapon item records, The test instrument: fixtures, entity_call, entity_field, r | yes |
| 75 | [Feat and trait effect catalogue](stories/L2.rpg.feat-effect-catalog.md) | rpg | M | 5 | 1 | RPG string table, Stat values and bounds, The test instrument: fixtures, entity_ | yes |
| 76 | [Weapon record word accessor](stories/L2.weapons.weapon-record-word.md) | weapons | S | 1 | 1 | Weapon item records, The test instrument: fixtures, entity_call, entity_field, r | yes |
| 77 | [Trait effect arithmetic / Presence pulse](stories/L2.rpg.effect-math.md) | rpg | L | 9 | 2 | Feat and trait effect catalogue, Stat values and bounds, The test instrument: fi | yes |
| 78 | [Weapon mode fallback copy](stories/L2.weapons.weapon-mode-fallback.md) | weapons | M | 1 | 1 | Weapon item records, The test instrument: fixtures, entity_call, entity_field, r | yes |
| 79 | [Discipline flag metadata](stories/L2.combat.discipline-flag-metadata.md) | combat | S | 2 | 1 | Discipline flag index lookup, The test instrument: fixtures, entity_call, entity | yes |
| 80 | [Create and spawn a loose weapon](stories/L2.weapons.weapon-create-spawn.md) | weapons | M | 4 | 1 | Weapon item records, Indexed magazine readers, The test instrument: fixtures, en | waits for 5 lower-layer rows |
| 81 | [Passkey inspection node](stories/L2.inventory_ui_items.passkey-inspection.md) | inventory_ui_items | M | 2 | 1 | Create and spawn a loose weapon, The test instrument: fixtures, entity_call, ent | waits for 1 lower-layer rows |
| 82 | [Discipline target tables](stories/L2.rpg.discipline-targets.md) | rpg | L | 11 | 1 | Discipline sound and particle data, Feat and trait effect catalogue, RPG string  | yes |
| 83 | [Equip and attach a weapon](stories/L2.weapons.weapon-equip.md) | weapons | M | 2 | 1 | Create and spawn a loose weapon, Weapon slot assignment and clearing, The test i | waits for 1 lower-layer rows |
| 84 | [Generic item deploy](stories/L2.inventory_ui_items.generic-item-deploy.md) | inventory_ui_items | M | 1 | 1 | Create and spawn a loose weapon, The test instrument: fixtures, entity_call, ent | yes |
| 85 | [Weapon use and pickup gates](stories/L2.weapons.weapon-pickup-gates.md) | weapons | M | 3 | 1 | Activity selection and translation, Weapon item records, The test instrument: fi | yes |
| 86 | [Inventory capacity and item admission](stories/L2.combat.inventory-admission.md) | combat | M | 2 | 1 | Create and spawn a loose weapon, The test instrument: fixtures, entity_call, ent | yes |
| 87 | [Weapon sound selection and routing](stories/L2.weapons.weapon-sound.md) | weapons | M | 4 | 2 | Weapon item records, Indexed magazine readers, The test instrument: fixtures, en | waits for 1 lower-layer rows |
| 88 | [Acquire a loose weapon](stories/L2.weapons.weapon-acquisition.md) | weapons | L | 4 | 2 | Inventory capacity and item admission, Ammo index and capacity gate, Equip and a | waits for 1 lower-layer rows |
| 89 | [Character history catalogue](stories/L2.rpg.history-catalog.md) | rpg | M | 4 | 1 | Critter scope data, Feat and trait effect catalogue, RPG string table, The test  | yes |
| 90 | [Leveling templates](stories/L2.rpg.leveling-templates.md) | rpg | M | 3 | 1 | RPG string table, Stat values and bounds, The test instrument: fixtures, entity_ | yes |
| 91 | [Drop carried weapons](stories/L2.weapons.weapon-drop.md) | weapons | M | 2 | 1 | Create and spawn a loose weapon, Equip and attach a weapon, The test instrument: | waits for 1 lower-layer rows |
| 92 | [Schedule weapon removal](stories/L2.weapons.weapon-kill-delay.md) | weapons | S | 1 | 1 | Create and spawn a loose weapon, The test instrument: fixtures, entity_call, ent | yes |
| 93 | [Weighted dice roll](stories/L2.combat.dice-roll.md) | combat | M | 1 | 1 | Stat values and bounds, The test instrument: fixtures, entity_call, entity_field | yes |
| 94 | [Respawn a weapon](stories/L2.weapons.weapon-respawn.md) | weapons | S | 1 | 1 | Create and spawn a loose weapon, The test instrument: fixtures, entity_call, ent | yes |
| 95 | [Barter prices](stories/L2.inventory_ui_items.barter-pricing.md) | inventory_ui_items | M | 2 | 1 | Stat values and bounds, The test instrument: fixtures, entity_call, entity_field | yes |
| 96 | [Save and restore weapon state](stories/L2.weapons.weapon-save-restore.md) | weapons | M | 2 | 1 | Create and spawn a loose weapon, Equip and attach a weapon, The test instrument: | waits for 1 lower-layer rows |
| 97 | [Refresh weapon trade prices](stories/L2.weapons.weapon-price-cache.md) | weapons | M | 1 | 1 | Barter prices, The test instrument: fixtures, entity_call, entity_field, retail_ | yes |
| 98 | [Weapon animation and idle cycle](stories/L2.weapons.weapon-animation-idle.md) | weapons | M | 5 | 1 | Refresh weapon trade prices, The test instrument: fixtures, entity_call, entity_ | waits for 1 lower-layer rows |
| 99 | [Admit and start reload](stories/L2.weapons.weapon-reload.md) | weapons | M | 1 | 1 | Weapon animation and idle cycle, Indexed magazine readers, Weapon sound selectio | yes |
| 100 | [Melee combo busy frame](stories/L2.weapons.weapon-melee-busy-frame.md) | weapons | M | 2 | 1 | Activity selection and translation, Weapon item records, The test instrument: fi | yes |
| 101 | [Trace attack and multi-damage](stories/L2.combat.trace-attack.md) | combat | L | 5 | 1 | Damage scaling and resolution, Life-taking progression, The test instrument: fix | waits for 1 lower-layer rows |
| 102 | [Default weapon spread vector](stories/L2.weapons.weapon-spread-default.md) | weapons | S | 1 | 1 | Weapon item records, The test instrument: fixtures, entity_call, entity_field, r | yes |
| 103 | [Frame-cached weapon aim trace](stories/L2.weapons.weapon-aim-cache.md) | weapons | M | 1 | 1 | Equip and attach a weapon, The test instrument: fixtures, entity_call, entity_fi | yes |
| 104 | [RPG data bootstrap](stories/L2.rpg.rpg-bootstrap.md) | rpg | L | 1 | 1 | Item activation types, Botch tables, Critter scope data, Damage packet lifecycle | yes |
| 105 | [Stat list recomputation](stories/L2.rpg.stat-lists.md) | rpg | M | 5 | 1 | Trait effect arithmetic / Presence pulse, RPG data bootstrap, Stat values and bo | yes |
| 106 | [Experience awards](stories/L2.rpg.experience-awards.md) | rpg | S | 2 | 1 | RPG data bootstrap, Stat list recomputation, The test instrument: fixtures, enti | yes |
| 107 | [Gibbed corpse path](stories/L2.combat.corpse-gib.md) | combat | L | 7 | 1 | Death and corpse damage flow, Template record defaults, The test instrument: fix | waits for 7 lower-layer rows |
| 108 | [Trait purchase costs](stories/L2.rpg.trait-purchases.md) | rpg | L | 9 | 1 | Trait effect arithmetic / Presence pulse, Leveling templates, RPG data bootstrap | yes |
| 109 | [Torpor death entry](stories/L2.combat.torpor-entry.md) | combat | S | 1 | 1 | Death and corpse damage flow, The test instrument: fixtures, entity_call, entity | yes |
| 110 | [Feeding eligibility](stories/L2.rpg.feeding-policy.md) | rpg | M | 2 | 1 | RPG data bootstrap, Stat list recomputation, The test instrument: fixtures, enti | yes |
| 111 | [Create weapon projectile and effect](stories/L2.weapons.weapon-projectile-create.md) | weapons | M | 1 | 1 | Equip and attach a weapon, Weapon item records, The test instrument: fixtures, e | yes |
| 112 | [Melee damage rolls](stories/L2.rpg.melee-rolls.md) | rpg | M | 3 | 1 | Soak feat index, Damage packet lifecycle, Dice table data, Stat list recomputati | yes |
| 113 | [Ranged damage per victim](stories/L2.weapons.weapon-ranged-victim-damage.md) | weapons | L | 2 | 1 | Damage packet lifecycle, Weapon mode fallback copy, Create weapon projectile and | yes |
| 114 | [Stat-driven sequences](stories/L2.rpg.stat-sequences.md) | rpg | M | 3 | 1 | Stat list recomputation, The test instrument: fixtures, entity_call, entity_fiel | waits for 1 lower-layer rows |
| 115 | [Melee damage and impact force](stories/L2.weapons.weapon-melee-damage-envelope.md) | weapons | M | 2 | 1 | Equip and attach a weapon, Weapon item records, The test instrument: fixtures, e | waits for 1 lower-layer rows |
| 116 | [Prayer pulse](stories/L2.rpg.prayer.md) | rpg | M | 2 | 1 | Stat list recomputation, The test instrument: fixtures, entity_call, entity_fiel | yes |
| 117 | [Blood decal policy](stories/L2.weapons.weapon-blood-decals.md) | weapons | M | 1 | 1 | Ranged damage per victim, The test instrument: fixtures, entity_call, entity_fie | waits for 1 lower-layer rows |
| 118 | [Vampire skill entity spawn](stories/L2.rpg.skill-spawn.md) | rpg | S | 1 | 1 | Terminal health guard, RPG data bootstrap, The test instrument: fixtures, entity | yes |
| 119 | [Discipline event queue](stories/L2.rpg.discipline-events.md) | rpg | L | 10 | 1 | Trait effect arithmetic / Presence pulse, RPG data bootstrap, Stat list recomput | yes |
| 120 | [Trait effect group lifecycle](stories/L2.rpg.trait-group-lifecycle.md) | rpg | L | 11 | 1 | Inventory capacity and item admission, Discipline event queue, Discipline target | yes |
| 121 | [Trait template transitions](stories/L2.rpg.template-transition.md) | rpg | L | 10 | 1 | Leveling templates, RPG data bootstrap, Stat list recomputation, Stat values and | yes |
| 122 | [Discipline damage](stories/L2.rpg.discipline-damage.md) | rpg | M | 1 | 1 | Damage packet lifecycle, Discipline target tables, RPG data bootstrap, The test  | yes |
| 123 | [Shaky hands](stories/L2.rpg.shaky-hands.md) | rpg | S | 2 | 1 | Stat list recomputation, The test instrument: fixtures, entity_call, entity_fiel | yes |
| 124 | [Vampire healing over time](stories/L2.combat.hot-healing.md) | combat | L | 3 | 1 | Trait effect arithmetic / Presence pulse, The test instrument: fixtures, entity_ | yes |
| 125 | [Celerity effect](stories/L2.rpg.celerity.md) | rpg | S | 1 | 1 | Stat list recomputation, Trait effect group lifecycle, The test instrument: fixt | waits for 1 lower-layer rows |
| 126 | [Protean effect](stories/L2.rpg.protean.md) | rpg | S | 1 | 1 | Discipline sound and particle data, Trait effect group lifecycle, The test instr | waits for 1 lower-layer rows |
| 127 | [Blood shield](stories/L2.rpg.bloodshield.md) | rpg | M | 1 | 1 | Damage packet lifecycle, Stat list recomputation, Trait effect group lifecycle,  | waits for 1 lower-layer rows |
| 128 | [Obfuscate effect](stories/L2.rpg.obfuscate.md) | rpg | M | 1 | 1 | Timed particle removal, Discipline sound and particle data, Trait effect group l | yes |
| 129 | [Grapple action and exit position](stories/L2.rpg.grapple.md) | rpg | M | 3 | 1 | RPG data bootstrap, Stat list recomputation, The test instrument: fixtures, enti | yes |
| 130 | [RE53 retail discipline magnitudes](stories/L2.parked.l2-rpg-discipline-magnitudes.md) | parked | S | 0 | 1 | Feat and trait effect catalogue, Stat definitions and costs, Stat values and bou | yes |
| 131 | [Choose and switch weapons](stories/L2.weapons.weapon-selection.md) · live (1,211 stub hits) | weapons | M | 5 | 1 | Active armor selection, Inventory capacity and item admission, Item trait group  | yes |
| 132 | [Combat character startup sync](stories/L2.combat.character-startup-sync.md) · live (67 stub hits) | combat | M | 2 | 1 | Template record defaults, Stat list recomputation, The test instrument: fixtures | waits for 1 lower-layer rows |
| 133 | [Intrusion skill check](stories/L2.inventory_ui_items.intrusion-check.md) | inventory_ui_items | M | 1 | 1 | Weighted dice roll, The test instrument: fixtures, entity_call, entity_field, re | yes |
| 134 | [Hacking skill check](stories/L2.inventory_ui_items.hacking-check.md) | inventory_ui_items | M | 1 | 1 | Weighted dice roll, The test instrument: fixtures, entity_call, entity_field, re | yes |
| 135 | [Discipline visual effects](stories/L2.rpg.discipline-visuals.md) | rpg | M | 2 | 1 | Blood shield, Celerity effect, Discipline sound and particle data, Obfuscate eff | yes |
