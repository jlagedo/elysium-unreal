# Layers — the build order, measured from retail (2026-10-06)

The port is built in retail's own dependency order, one layer at a time, bottom-up. This replaces
the order of the beat specs (0001–0017), which cut through the layers and kept stopping on a
subsystem another spec owned (44 recorded stops in 0002 alone). The rules are in `AGENTS.md`
§ Project rules and § When a problem is reported.

| file | what |
|---|---|
| `README.md` | this: the layer map, how it was measured, the parked specs mapped to layers |
| `audit.tsv` | every retail function the 108 maps need: layer, subsystem, band (core / mid / tail), port status, the maps that reach it, and for the settled rows a one-line role, the port lines that carry it and what is missing |
| `hooks.tsv` | the upward hooks: a lower layer's function that calls into a higher layer (694 calls from 451 functions) |
| `L0-entity/` … | one spec per layer (L0 first) |

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
functions, a read-only Codex sweep (`gpt-6-luna`, `max`) decided carried / partial / not carried /
engine-replaced, the 958 trivial ones following their callers. A blind re-check of 72 rows by
`gpt-6.1-sol` at `high` agreed on done-or-open 78 % (the sweep leans optimistic); L0 and L1 are being
re-checked in full at that grade. Read the numbers as ±10–15 %.

| layer | retail code | done | partial | open | functions open or partial |
|---|---|---|---|---|---|
| L0 entity | 350 KB | 67 % | 10 % | 20 % | 459 |
| L1 animation | 36 KB | 52 % | 12 % | 36 % | 63 |
| L2 character | 263 KB | 61 % | 15 % | 23 % | 392 |
| L3 player | 128 KB | 60 % | 18 % | 20 % | 157 |
| L4 NPC | 334 KB | 91 % | 2 % | 6 % | 119 |
| L5 scripting | 44 KB | 55 % | 6 % | 38 % | 50 |

The port was built upside down: the NPC layer is 91 % done (its map-specific tail 95 %), the layers
it stands on 52–67 %. In L0 the largest gap is **physics and movetypes** (step, toss, push, fly, the
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
