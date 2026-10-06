# Layers — the build order, measured from retail (2026-10-06)

The port is built in retail's own dependency order, one layer at a time, bottom-up. This replaces
the order of the beat specs (0001–0017), which cut through the layers and kept stopping on a
subsystem another spec owned (44 recorded stops in 0002 alone). The rules are in `AGENTS.md`
§ Project rules and § When a problem is reported.

| file | what |
|---|---|
| `README.md` | this: the layer map, how it was measured, the parked specs mapped to layers |
| `L0-entity/` … `L4-npc/` | one spec per layer (`spec.md`: scope, gate, order, decisions), its stories in order (`stories-table.md`, `stories/`), its worker briefs (`briefs/`, ≤ 4 KB each), the briefs bundled into worker runs (`runs.md`), and its contract checks (`contract-checks.md`) |
| `harness.md` | the test instrument every layer's records use; built by L0's story 0 |
| `method.md` | how a run is executed (by any agent), gated, committed; how a layer closes |
| `schedule.md` | worker-hours per layer and elapsed time by number of lanes |
| `checks.md` | the plan's consistency checks (all pass) and the harness pieces the records ask for |
| `decisions.md` | everything only the owner can decide (D1–D6) |
| `baseline.md` | the build, tiers and arena before L0, with the stubs the arena fires |
| `beyond-core/` | after the cores: the shared band (11–53 maps) and the map-specific tails per hub group |
| `parked-map.md` | every open item of the parked specs 0002–0017 mapped to a layer story |
| `audit.tsv` | every retail function the 108 maps need: layer, subsystem, band (core / mid / tail), port status, the maps that reach it, a one-line role, the port lines that carry it, what is missing, and its plan (the story, or why it is not work) |
| `hooks.tsv` | the upward hooks: a lower layer's function that calls into a higher layer (694 calls from 451 functions) |

## The plan in numbers

| stage | stories | briefs | worker runs | worker-hours | stories ready now |
|---|---|---|---|---|---|
| L0 entity | 165 | 264 | 88 | ~78 | all 165 |
| L1 animation | 21 | 37 | 15 | ~13 | 16 of 21 |
| L2 character | 135 | 178 | 106 | ~89 | 106 of 135 |
| L3 player | 55 | 103 | 51 | ~45 | 24 of 55 |
| L4 NPC + L5 scripting | 102 | 154 | 77 | ~82 | 80 of 102 |
| **all** | **478** | **736** | **337** | **~307** (with layer closes) | |

"Ready now" = nothing the story calls in a lower layer is still open; under R1 as committed, a layer
still starts only after the one below it closes.

One lane: ~307 h of worker time; three lanes: ~111 h in strict layer order, ~105 h if a story may start
once what it calls below is done (`decisions.md` D1). The 478 stories include L0's test instrument and
two tooling stories, 28 stories written for the parked specs' uncovered items, and the parked items'
remainders as slices of the stories they belong to (`parked-map.md`). How a run is executed: `method.md`. Beyond the cores: ~1,700 more functions
(`beyond-core/`), settled and planned when their turn comes.

## How it was measured

From the Ghidra corpus of `vampire.dll` over all 108 maps (Unofficial Patch lane): 71,096 entities,
326 classnames, 320 resolved to their retail class through the factory → constructor → vftable chain.
A map's code is the call closure from every class it spawns (all vtable slots, the datamap inputs and
think functions found by name) plus a common base (player, movement, game rules, the game DLL, the
event queue, the dialogue system): 8,471 functions game-wide, 3,500–6,200 per map. Functions are
grouped into subsystems by class, by vtable slot range for the misattributed ones, and by method name
inside the broad base classes. The scripts live in `$ELYSIUM_WORK_ROOT/build-order/`
(`map_entities.py`, `classmap.py`, `game_graph.py`, `audit.py`, `settle_*.py`, `hooks_game.py`).

## The layers

Calls between layers over the whole game, consumer → provider, with the reverse in brackets: every
pair runs mostly one way, so the order is retail's.

| layer | retail | calls into lower layers [called back] |
|---|---|---|
| **L0 entity** | `CBaseEntity` + physics and movetypes, entity I/O and the event queue, triggers, movers and doors, the save framework, the sound list, sound emission, effects, ConVars and game rules | — |
| **L1 animation** | `CBaseAnimating`, `CBaseAnimatingOverlay`, `CBaseFlex` | L0 93 [66] |
| **L2 character** | `CBaseCombatCharacter` with VtMB's stats, disciplines, blood and feeding wired in; weapons; inventory | L0 489 [216], L1 79 [6] |
| **L3 player** | `CBasePlayer`, `CGameMovement` | L2 283 [36], L0 308 [31], L1 25 [0] |
| **L4 NPC + L5 scripting** | `CAI_BaseNPC` / `Troika`, species, senses, navigation, squads, hints and places; scripted sequences, dialogue, Python; the player's verbs on NPCs | L2 454 [142], L0 993 [134], L1 133 [9]; L4 ↔ L5 42 / 43 and L3 → L4 53 [21]: one stage |

**One core, then map-specific leaves.** The core — every function at least half the maps reach — is
reached in full by `sp_tutorial_1` and `sm_hub_1` together, in every layer: they are every layer's
witnesses. Code reached by ten maps or fewer (~2,300 functions: bosses, unique creatures and their
weapons, one-off entities) is self-contained per map — of 936 calls between such functions, 935
stay inside the caller's own maps — and is built per map after the core. 271 retail classes are
spawned: 20 of them are 74 % of all entities, 50 are 94 %; 100 appear on one map only.

## Where the port stands (core, retail code bytes)

Status per function: cited by address in the port or named there; for the 2,289 unnamed, uncited
functions, a read-only model sweep (Codex `gpt-6-luna`, `max`) decided carried / partial / not carried /
engine-replaced, the 958 trivial ones following their callers. A blind re-check of 72 rows by
a stronger model (`gpt-6.1-sol`, `high`) agreed on done-or-open 78 % (the sweep leans optimistic), so every open,
partial or uncertain core row of every layer was then re-checked at that grade (1,127 rows; `audit.tsv`
column `checked`), except 281 rows already known open by hard evidence (generated stubs, functions the
port neither cites nor names).

| layer | retail code | done | partial | open | functions open or partial |
|---|---|---|---|---|---|
| L0 entity | 350 KB | 67 % | 14 % | 17 % | 424 |
| L1 animation | 36 KB | 56 % | 14 % | 29 % | 54 |
| L2 character | 263 KB | 58 % | 13 % | 27 % | 397 |
| L3 player | 128 KB | 55 % | 15 % | 28 % | 178 |
| L4 NPC | 334 KB | 90 % | 1 % | 8 % | 127 |
| L5 scripting | 44 KB | 55 % | 3 % | 42 % | 56 |

The port was built upside down: the NPC layer is 90 % done (its map-specific tail 95 %), the layers
it stands on 55–67 %. In L0 the largest gap is **physics and movetypes** (step, toss, push, fly, the
VPhysics shadow: 37 % done, 54 % open) — the movement NPCs and items run on.

## The parked specs, mapped to layers

Specs 0002–0017 are parked, not deleted. Their open stories are done by the layer that owns them;
each spec's witness (a tutorial beat, V8's maps live) becomes an acceptance scenario, run when every
layer it crosses is finished.

| spec | open work | layer(s) |
|---|---|---|
| 0001 scripted-camera | closed | — |
| 0002 npc-ai | V6 clock, session, `map_load` teardown, save resume | L0 (clock, entity world, save framework), L4 (NPC resume) |
| | V7 the 19 NPC inputs; V8 maps live; V9 test cut; R2–R5, R7 places, cover, social, flee, hub species | L4 |
| | V10 a sound's life; R1 the sound list and its producers | L0 (the list, emission), producers in their own layers, L4 (listening) |
| | V12 the player's footstep producer | L3 |
| | R6 makers and templates | L0 (spawn, templates), L4 (maker behaviour) |
| 0003 scripted-sequence | the cine entity's lifecycle, bodiless targets | L5 |
| 0004 jack-arrival | conversation panel, turn / pick engine | L5 (dialogue) |
| 0005 first-kill | health commit, damage spine, death, reaction | L2; death poses L1; the NPC side L4 |
| 0006 first-disciplines | discipline kernel, vitals, activation | L2; player activation L3 |
| 0007 tuna-distraction | physics props, `phys_*`, impulse, impact sound; grab / carry / throw | L0; the player's hands L3 |
| 0008 ranged-bottles | firearms, pickup, breakables | L2 (weapons, inventory); breakables L0 |
| 0009 elevator-final | movers and doors, the level transition | L0 |
| 0010 theatre-scene | scenes, face, eyes, lipsync | L5; face and flex L1 |
| 0011 theatre-audio | audio tier 0, typed events | L0 (emission); the line clock L5 |
| 0012 camera-director | camera director | presentation, after L5 |
| 0013 first-person-viewmodel | first-person body | presentation (L3 player view) |
| 0014 ragdoll | physics asset, death impulse, `prop_ragdoll` | L0 |
| 0015 weapon-overlays | the overlay stack | L1; weapon side L2 |
| 0016 input-pads | pad input | L3 |
| 0017 save-game | run persistence, the retail-save importer | L0 (framework, transitions); each layer saves its own words with its stories |

## The sequence

L0 entity → L1 animation → L2 character → L3 player → L4 NPC + L5 scripting → the map-specific
tails, per hub group (`la`, `hw`, `sm`, `sp`, `ch`). What is next = the first unfinished layer, then
its first open story (`TRACKER.md`).

## Known gaps (2026-10-06)

- 3 parked items have no story yet: the physics constraint entities (0007:112), paired scene actions
  (0010:128), the Level Sequence camera bridge (0012:69) — listed in their layer's `spec.md`.
- 6 open core rows the planners placed in no layer (each says another layer, none names a story):
  `0x10190810`, `0x10104730`, `0x10245a40`, `0x1017f900`, `0x101b4170`, `0x101b0c10` — `audit.tsv` column `plan`.
- Non-core statuses (the shared band and the tails) are inferred, not settled: each beyond-core spec starts
  by settling its rows.
- The datamap inputs and think functions were matched by name only (217 of 669), so the input surface
  is under-counted in the closure; a story that ports an input reads its class's datamap first.
