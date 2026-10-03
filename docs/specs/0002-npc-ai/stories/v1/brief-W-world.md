# Brief W — the class roll call, scripts and I/O, patrols and places, makers, the two maps

Read `README.md` here first. Your records go under `Arena/scenarios/world/`; your files are those
records, `stories/v1/inventory-W.md`, `stories/v1/triage-W.md` and
`stories/v1/divergences.md` (below).

## The landed stories you prove

| Story | What it claims |
|---|---|
| 0019/5 the class tree | 56 live retail classes are C++ classes; every live classname is registered and resolves to its retail class |
| 0018/2 baked infrastructure actors | hint, place, maker and placed-NPC rows baked and adopted by index |
| 0018/3 contents, NavMesh, agents | per-agent baked NavMesh; an NPC of each used hull paths on its own mesh |
| 0018/4 the place set | `DA_<map>_Places`, node binding by the `CNodeEnt` counter, the cooldown and the attached hint |
| 0019/8's map smokes | tutorial and hub idle: pedestrians walk, the monk loops its five nodes, `sentry2` patrols under `0x67`, 0 ensure / assert |
| 0002/10g, 27 (landed parts) | `TASK_NEXT_PATROL_POINT` and `NextPoint 0x10307b80`; the patrol-point interest roll |
| 0002/11 (landed parts) | the interesting-place selector arms and task bodies (the live path is still the port's ambient executor: known red 6) |
| the 14 registered NPC inputs | each bound input does what its retail handler does (`ElysiumNpcClasses.cpp`; the datamap replay names the handlers) |
| the scripted sequence and the dialogue hold, as landed | a `scripted_sequence` walks an NPC to a mark, plays and returns it; a dialogue holds an NPC and releases it |
| makers | an `npc_maker` spawns its children through the runtime door; `OnDeath` re-spawn |

## The families, each at least one record

- **The roll call.** For every live NPC classname (`docs/vtmb/npc-ai/population.md`, the factory
  map): one record standing one row of that class in the arena and expecting it to activate, select
  a schedule and run it for ten seconds with no `taskfail` storm. Take each row verbatim from a
  baked map that places the class (`from_map`; `uv run elysium research` and the reach lists say
  which map; a class no baked map places gets a hand-written row and a note). A class whose row
  cannot stand (no baked model, a brush dependency) is listed with the reason, not skipped
  silently.
- **Patrols.** `from_map` the tutorial's `thug_1` with `pt1`..`pt3`, and the monk with its five
  nodes: the patrol program, each point reached in order, the wait at each, the loop or the
  ping-pong as the path's keys say.
- **Places.** A `use_interesting` pedestrian row with two `intersting_place` rows (the arena's
  anchors are such rows): retail selects `0xff` through its idle selector, walks, uses, releases
  on the schedule change. The port's ambient executor answers instead today: write what retail
  does; expect known red 6.
- **Scripts.** A `scripted_sequence` row (`m_fMoveTo` walk) and its target NPC, started by
  `fire BeginSequence`: the NPC walks to the mark, plays, `OnEndSequence` fires, the NPC is idle
  again. Read `docs/specs/0003-scripted-sequence/spec.md` § Witness and `docs/vtmb/choreographed_scenes.md`
  for retail's order of events (`SCHED_SCRIPTED_*`, the tasks). The arbiter stands in today: write
  retail's order; expect a red that V3 owns.
- **Inputs and outputs.** One record per registered NPC input, grouped where one stage serves
  several: fire it, expect the `input` event and its retail effect (a schedule, a flag, a
  relationship, a death). `OnFoundPlayer`, `OnDeath` and the other outputs a placed tutorial NPC
  carries: fired when retail fires them.
- **Makers.** An `npc_maker` row from a baked map with its template: the child spawns, thinks,
  and a second spawns after the first dies, per the maker's keys.
- **The two maps, idle** (`"stage": "map:sp_tutorial_1"`, `"map:sm_hub_1"`, no script): sixty
  seconds of game time each: named NPCs run the programs their rows give them (the patrols above,
  the pedestrians' moves arriving), `never` a `taskfail` storm on any NPC.
- **The tutorial's sneak-past**, as a map-stage record: `thug_1` idles at `pt1`; the player
  placed where the lesson's sound reaches it: the investigation; the player placed in its cone
  inside the light scalar: the commit, `OnFoundPlayer`. Read
  `docs/vtmb/sp_tutorial_1-event-surface.md` and the old spec's witness section.

## The divergence table

`stories/v1/divergences.md`: the 22 "named modernizations" that change state or event order
(`consolidation/findings-D-landed.md` § 2, `findings-B-port-only.tsv`), one row each: where it is
(`file:line`), what retail does (address), which record shows it or why none can, and the story of
the spec that owns its end (V3–V7, R1–R8, or "kept: named divergence" with the reason). No code is
edited: each is re-labelled at its line by the story that touches it.

Known reds you should expect to meet: 1, 5 (a `map_load` of the same map keeping entities is not
yours to stage), 6, 10 (19 inputs unregistered: list them, they are V7's), 11.
