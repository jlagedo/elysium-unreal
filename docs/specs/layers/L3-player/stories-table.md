| # | story | subsystem | size | rows | records | depends on | ready |
|---|---|---|---|---|---|---|---|
| 1 | [Player movement collision and velocity](stories/L3.player.move-solve.md) | player | M | 5 | 1 | — | yes |
| 2 | [Player ground and surface changes](stories/L3.player.ground-surface.md) | player | M | 5 | 1 | Player movement collision and velocity | waits for 1 lower-layer rows |
| 3 | [Player stuck recovery and duck hull repair](stories/L3.player.stuck-duck.md) | player | M | 6 | 1 | Player movement collision and velocity | yes |
| 4 | [Player water movement and drowning](stories/L3.player.water-drowning.md) | player | M | 4 | 1 | Player ground and surface changes, Player movement collision and velocity | waits for 1 lower-layer rows |
| 5 | [Player noclip and fly or toss movement](stories/L3.player.special-movetypes.md) | player | S | 2 | 1 | Player ground and surface changes, Player movement collision and velocity | yes |
| 6 | [Player walking and air movement](stories/L3.player.walk-air.md) | player | M | 4 | 1 | Player ground and surface changes, Player movement collision and velocity, Playe | yes |
| 7 | [Player jump input and encumbrance speed](stories/L3.player.jump-encumbrance.md) | player | S | 2 | 1 | Player ground and surface changes, Player walking and air movement | waits for 1 lower-layer rows |
| 8 | [Player vehicle role entry and exit](stories/L3.player.vehicle.md) | player | M | 3 | 1 | Player noclip and fly or toss movement | waits for 1 lower-layer rows |
| 9 | [Player eye, aim, and ear queries](stories/L3.player.eye-geometry.md) | player | L | 8 | 1 | Player ground and surface changes, Player stuck recovery and duck hull repair | yes |
| 10 | [Player view roll and punch response](stories/L3.player.view-motion.md) | player | M | 5 | 1 | Player walking and air movement | yes |
| 11 | [Player body illumination](stories/L3.player.illumination.md) | player | S | 1 | 1 | Player eye, aim, and ear queries | yes |
| 12 | [Player weapon selection and equip](stories/L3.player.weapon-selection.md) | player | L | 6 | 1 | — | waits for 2 lower-layer rows |
| 13 | [Player animation action and ownership](stories/L3.player.animation-arbiter.md) | player | M | 3 | 1 | Player jump input and encumbrance speed, Player walking and air movement, Player | yes |
| 14 | [Player model and owned simulated entities](stories/L3.player.postthink-entities.md) | player | S | 2 | 1 | Player animation action and ownership | waits for 2 lower-layer rows |
| 15 | [Player give remove and drop transactions](stories/L3.player.item-transactions.md) | player | M | 3 | 1 | Player eye, aim, and ear queries, Player weapon selection and equip | waits for 4 lower-layer rows |
| 16 | [Player inventory limits and ammo switching](stories/L3.player.inventory-policy-ammo.md) | player | M | 4 | 1 | Player give remove and drop transactions, Player weapon selection and equip | waits for 2 lower-layer rows |
| 17 | [Player inventory reset](stories/L3.player.inventory-reset.md) | player | S | 2 | 1 | Player give remove and drop transactions, Player weapon selection and equip | waits for 1 lower-layer rows |
| 18 | [Player feed admission and opponent memory](stories/L3.player.feed-admission.md) | player | M | 5 | 1 | Player eye, aim, and ear queries, Player weapon selection and equip | waits for 1 lower-layer rows |
| 19 | [Grapple partner clearance](stories/L3.player.feed-clearance.md) | player | S | 2 | 1 | Player eye, aim, and ear queries, Player feed admission and opponent memory | yes |
| 20 | [Player rat feeding phases](stories/L3.player.feed-rat.md) | player | S | 1 | 1 | Player animation action and ownership, Player feed admission and opponent memory | waits for 1 lower-layer rows |
| 21 | [Player seductive feeding phases](stories/L3.player.feed-seductive.md) | player | S | 1 | 1 | Player animation action and ownership, Player feed admission and opponent memory | waits for 1 lower-layer rows |
| 22 | [Player zombie feeding phases](stories/L3.player.feed-zombie.md) | player | S | 1 | 1 | Player animation action and ownership, Player feed admission and opponent memory | waits for 1 lower-layer rows |
| 23 | [Player discipline gates and effects](stories/L3.player.discipline-effects.md) | player | L | 7 | 1 | Player jump input and encumbrance speed | waits for 5 lower-layer rows |
| 24 | [Player Obfuscate rules and movement time](stories/L3.player.obfuscate.md) | player | S | 2 | 1 | Player discipline gates and effects, Player feed admission and opponent memory | yes |
| 25 | [Player status bits and observer caches](stories/L3.player.status-cache.md) | player | L | 6 | 1 | Player animation action and ownership, Player discipline gates and effects, Play | yes |
| 26 | [Player discipline targeting and nested casts](stories/L3.player.discipline-target-cast.md) | player | M | 5 | 1 | Player discipline gates and effects, Player eye, aim, and ear queries, Player st | yes |
| 27 | [Player vomit activity ladder](stories/L3.player.vomit.md) | player | S | 2 | 1 | Player animation action and ownership, Player discipline gates and effects, Play | yes |
| 28 | [Player law and response timers](stories/L3.player.law-pass.md) | player | M | 5 | 1 | Player status bits and observer caches | waits for 1 lower-layer rows |
| 29 | [Player PreThink services](stories/L3.player.prethink.md) | player | M | 3 | 1 | Player jump input and encumbrance speed, Player law and response timers, Player  | waits for 1 lower-layer rows |
| 30 | [Player whisper sound selection](stories/L3.player.whisper.md) | player | S | 2 | 1 | — | yes |
| 31 | [Player event listener registry](stories/L3.player.player-events.md) | player | M | 4 | 1 | — | waits for 1 lower-layer rows |
| 32 | [Player classification and touch](stories/L3.player.class-touch.md) | player | M | 3 | 1 | Player ground and surface changes | yes |
| 33 | [Player damage commit and knockback gate](stories/L3.player.damage.md) | player | M | 6 | 1 | Player view roll and punch response, Player weapon selection and equip | waits for 8 lower-layer rows |
| 34 | [Player death cleanup](stories/L3.player.death-cleanup.md) | player | M | 3 | 1 | Player damage commit and knockback gate, Player discipline gates and effects, Pl | waits for 1 lower-layer rows |
| 35 | [Humanity loss on player kills](stories/L3.player.kill-humanity.md) | player | M | 3 | 1 | Player death cleanup | waits for 1 lower-layer rows |
| 36 | [Player experience and score awards](stories/L3.player.awards.md) | player | S | 2 | 1 | — | waits for 2 lower-layer rows |
| 37 | [Player save stream](stories/L3.player.player-save.md) | player | S | 2 | 1 | Player inventory reset, Player event listener registry, Player status bits and o | waits for 1 lower-layer rows |
| 38 | [Player spawn selection and placement](stories/L3.player.spawn-selection.md) | player | M | 4 | 1 | Player ground and surface changes, Player stuck recovery and duck hull repair | yes |
| 39 | [Player restore and keyring reconciliation](stories/L3.player.player-restore.md) | player | L | 7 | 1 | Player inventory reset, Player give remove and drop transactions, Player save st | waits for 5 lower-layer rows |
| 40 | [Player spawn initialization](stories/L3.player.spawn-initialization.md) | player | L | 6 | 1 | Player discipline gates and effects, Player spawn selection and placement, Playe | waits for 1 lower-layer rows |
| 41 | [Player world removal](stories/L3.player.entity-removal.md) | player | S | 1 | 1 | Player event listener registry, Player model and owned simulated entities | waits for 1 lower-layer rows |
| 42 | [Player VPhysics shadow and save policy](stories/L3.player.physics-shadow.md) | player | M | 2 | 1 | Player movement collision and velocity | waits for 3 lower-layer rows |
| 43 | [Player head tracking](stories/L3.player.head-turn.md) | player | S | 1 | 1 | Player animation action and ownership, Player eye, aim, and ear queries | yes |
| 44 | [Player client command dispatch](stories/L3.entity_core.l3-player-client-commands.md) | entity_core | S | 2 | 1 | — | waits for 3 lower-layer rows |
| 45 | [Player camera shot registry and indexing](stories/L3.combat.l3-player-camera-shot-registry.md) | combat | M | 2 | 2 | — | waits for 1 lower-layer rows |
| 46 | [Player Hands physics-cannon interaction](stories/L3.physics.l3-player-physcannon.md) | physics | L | 9 | 2 | — | waits for 10 lower-layer rows |
| 47 | [Player use and impulse dispatch](stories/L3.player.use-impulses.md) | player | M | 5 | 1 | Player eye, aim, and ear queries, Player give remove and drop transactions, Play | waits for 5 lower-layer rows |
| 48 | [Player payphone animation state](stories/L3.player.payphone.md) | player | S | 1 | 1 | Player animation action and ownership, Player use and impulse dispatch | yes |
| 49 | [Player command lifecycle](stories/L3.player.command-pipeline.md) | player | L | 8 | 1 | Player animation action and ownership, Player jump input and encumbrance speed,  | waits for 1 lower-layer rows |
