# Brief P — perception and the mind's conditions

Read `README.md` here first. Your records go under `Arena/scenarios/perception/`; your files are
those records, `stories/v1/inventory-P.md` and `stories/v1/triage-P.md`.

## The landed stories you prove (rows of `consolidation/findings-D-landed.tsv`; old 0002 text in `record-2026-09-30.md`)

| Story | What it claims |
|---|---|
| 0002/15 think cadence | the four think stamps and laws, the reduced mode, the 0.8 s gate (0018/5), `NPCInit`'s late arm |
| 0002/5 enemy memory | `CAI_Memory` records, `UpdateEnemyMemory`, `BestEnemy`, `ChooseEnemy`'s null-schedule rule |
| 0002/6a sight and hearing | the prefilter, the cadences, the cone, the outer band, the hearing delay, `ambient_generic sound_event` |
| 0002/6b the Troika cone | `FInViewCone` slot 363 arms 1–3 |
| 0019/4 tunables | view-cone apex 40, melee range 100, health 10, hint height 64 — observed live for the first time |
| 0018/6 geometry services (sight half) | `FVisible`'s four arguments: the player and solid props block, every NPC is transparent |
| 0002/1, 2, 4 the light query, the gauge, the observer snapshot | sight inside the light scalar; the HUD reads committed state |
| 0002/7 obliviousness | `TASK_MAKE_OBLIVIOUS` and its consumers |
| 0002/8 the NPC flag word | the SET / CLEAR tasks, slot 435's masks |
| 0002/9 the interest predicate | `ShouldInvestigate 0x102b3270` |
| 0002/10a, 10b, 10c the sweeps | the sound sweep `0x102b1cd0` and `CommitBestSound`; the see-unknown sweep `0x102b15c0` and the initial-response roll; the comfort sweep `0x102b1a20` |
| 0002/13, 14, 25 | `TaskFail` (base and Troika, the bounded leak); `SET_ACTIVITY` on a miss; the failure route and `TASK_WAIT_RANDOM`'s floor |
| old rows "done, record only" | 10e the sound-investigation task arms; 10k the saved-position arms; 10j `CheckTarget`; 21b cower / disoriented / lost; 13b |
| 0018/4 the place set (the wander) | `TASK_GET_PATH_TO_RANDOM_NODE` (`0x1f`) by the capped point pick |

## The families, each at least one record

- **Idle and cadence.** A neutral NPC idles: its idle program installs, its random wait elapses
  and the program runs again; nothing fails. A second record for the wander pick if any program a
  placed human runs issues `0x1f` (find one in the corpus; if none is reachable from an arena
  cast, say so in the inventory).
- **Sight.** The player walks from behind the block into a hostile NPC's cone: `cond+ SEE_ENEMY`
  (and the conditions the ladder raises before it), the state change, the combat schedule. The
  reverse: the player teleports behind the block: `ENEMY_OCCLUDED`, then `LOST_ENEMY` on retail's
  timer. Outside the cone at the same distance: nothing, for the whole run (`never`). Beyond the
  vision distance of the row's `npc_perception`: nothing.
- **Occlusion by bodies.** A second NPC standing between the two does not block sight; the block
  does (0018/6).
- **Memory.** The player seen, then hidden: the enemy stays committed for the memory's life
  (`enemy` probe), then is dropped; two candidate enemies and `BestEnemy`'s choice if the arena
  can stage two.
- **Hearing and the sound sweep.** An `ambient_generic` with `sound_event` (as a `rows` entry,
  fired by `script`) inside and outside hearing distance: the sweep's conditions, the
  investigation program, the walk to the sound, the look-around, the return to the saved position
  (10e, 10k). Read `docs/vtmb/npc-ai/senses.md` for what the landed producer is; footsteps and
  weapons are not produced yet (step 3, R1) and are not yours.
- **See-unknown and comfort.** The see-unknown ladder on a neutral NPC and its initial-response
  roll (seed the record; state which roll the seed draws); the comfort sweep if two NPCs can stage
  it.
- **Obliviousness, the flag word, the interest predicate.** One record each where a program a
  placed human runs reaches them.
- **The failure route.** A goal nothing can reach (a `rows` entity placed inside the block, or an
  enemy in a sealed spot): the path task fails with retail's code, the fail schedule installs, the
  random wait runs, the program is re-selected — and no `taskfail` storm (`never` more than the
  count retail's route produces in the window).
- **The light scalar** needs world lights the arena does not have: a map-stage record on
  `sp_tutorial_1` (`"stage": "map:sp_tutorial_1"`, cast `thug_1`): the player placed in the dark
  at a distance where retail's scalar hides it, then in the light at the same distance. Read
  `docs/vtmb/stealth.md` and `docs/vtmb/sp_tutorial_1-event-surface.md` for the lesson's geometry.

Known reds you should expect to meet: 3 (`BEHIND_ENEMY` set on every gather; the 13 s face), 5 (an
open dialogue blinds perception; a `SetRelationship`-flipped NPC never enters the sighted list —
the flip itself is lane K's record).
