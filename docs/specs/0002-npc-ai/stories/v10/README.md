# V10 + V12 — sound lifetime, then the reserved player stimulus

Final planner cut, 2026-10-05. Read AGENTS.md and [packets-V10.md](packets-V10.md).
This wave covers **V10 then V12**. It follows the committed V4c `d0f79574` and the
forthcoming V4d/V5b integrations. Planning writes only these six new Markdown
files in `E:\elysium-work\worktrees\coord`; no implementation or validation run
has been made. Coordinator names implementation worktrees and integration branch.

V4c recorded default 169/0, arm 1624/0, arena 113 pass / 1 fail / 16 expected-fail /
2 unexpected-pass of 132. Both N4 intermittents remain UP. Re-establish provenance
from the landed V4d/V5b commits when integrating; do not use an assumed old binary.

## Order and deliverable

1. **Diagnostic first:** coders deliver separately identifiable observation hunks
   and correction hunks. Integrator first compiles observation taps plus the new
   harness/diagnostic record against landed prerequisites, preserving old sound
   behavior. Default tests, then `hear_world_diagnostic` and the two N4 donors in
   three independent boot orders collect actual insert/expiry/listen/eviction and
   full-gather stamps. Record whether equal-time freshness, late sensing, bus
   removal, or neither caused a miss. A pass alone does not prove historical cause.
2. V10 corrections: no listener expiry predicate; active-list access uses timestamp
   freshness; cleanup at +1 initially, then current+0.3, finite expiry+4 <=now,
   sentinel excluded, whole-list pruning, no insertion-age cutoff. Preserve the
   actual QueryHearSound, list ordering, delayed-condition/RNG/output chain.
3. V12 corrections: one in-place reserved type-4 record, expiry -1, including
   silent frames; producer in existing post-move PostThink tail each frame;
   retained Jump button and integer per-frame decay. No animation-event AI insert.
   Correct tutorial's first heard-player program to 0x4c: Q-V3bf1 is settled.
4. Compile final sources, default tests, records by name (loop freely), arm,
   kernel check, final full arena once; record all moved verdicts and commit once.
   Six-build total budget across observation and correction passes, ~2 min each.

No new modernization is authorized. Equal-time freshness is retail, not an expiry
bug. If diagnostics show equal-time loss, do not change `<` into `<=`, reorder IO,
add epsilon or lengthen the sound. Establish whether the record deliberately
collides with retail freshness or a real port delivery divergence exists. Correct
only a proven record error with evidence; an unknown engine-order issue goes to
the judge and prevents an unearned N4 closure.

## Three coder manifests

Paths are repository-relative, exhaustive; write by absolute path in the worktree
the coordinator names. `S` below means `Source/ElysiumUE/Private/Substrate/`,
`T` means `Source/ElysiumUE/Private/Tests/`, `D` means `Source/ElysiumUE/Private/Debug/`.

| lane | exclusive files |
|---|---|
| V10-1 listener | `S/ElysiumNpcSenses.cpp`; `S/ElysiumNpcSenses.h`; `T/ElysiumNpcSensesTests.cpp`; `docs/vtmb/npc-ai/senses.md` |
| V10-2 lifetime/player | `S/ElysiumGameSound.cpp`; `S/ElysiumGameSound.h`; `S/ElysiumPlayerEntity.cpp`; `S/ElysiumFootsteps.cpp`; `S/ElysiumFootsteps.h`; `Source/ElysiumUE/Public/ElysiumPlayer.h`; `T/ElysiumGameSoundTests.cpp`; `T/ElysiumPlayerFootstepTests.cpp`; `docs/vtmb/footsteps.md` |
| V10-3 harness/records | `D/ElysiumArenaScenario.h`; `D/ElysiumArenaScenario.cpp`; `D/ElysiumArenaScenarioRunner.h`; `D/ElysiumArenaScenarioRunner.cpp`; new `D/ElysiumArenaSoundFixtures.h`; new `D/ElysiumArenaSoundFixtures.cpp`; `Arena/README.md`; `pipeline/tests/test_arena_suite.py`; the 18 JSON files enumerated below |

Lane 3 JSON manifest, all under `Arena/scenarios/`:

- Existing `perception/hear_world_investigate.json`, `perception/interest_mode_never.json`,
  `world/map_tutorial_sneak_past.json`.
- New `perception/hear_world_diagnostic.json`, `perception/sound_lifetime_grace.json`,
  `perception/sound_lifetime_long.json`, `perception/sound_lifetime_prune.json`,
  `perception/sound_freshness_equal.json`, `perception/sound_freshness_new.json`,
  `perception/sound_listen_empty_mask.json`, `perception/sound_listen_order.json`,
  `perception/sound_player_reserved.json`, `perception/sound_player_modes.json`,
  `perception/sound_player_decay.json`, `perception/sound_player_gates.json`,
  `perception/footstep_events_no_ai.json`, `world/map_tutorial_hearing_walk_radius.json`,
  `world/map_tutorial_hearing_sneak_radius.json`.

**Disjointness:** A has 4 files, B 9, C 26 (8 harness/schema files +18 records).
A∩B=A∩C=B∩C=∅, union size **39**. Dependencies are calls/owed lines, never
cross-lane edits. Each coder brief repeats the full manifest and numbered jobs.

Integrator alone owns exact owed lines in `S/ElysiumEntityWorld.cpp`,
`Source/ElysiumUE/Public/ElysiumEntityWorld.h`, `S/ElysiumNpcThink.cpp`,
`S/ElysiumNpcBaseConditions2.cpp`, `S/ElysiumNpc.h`, and, if old fixture assertions
need migration, `T/ElysiumFootstepSenseTests.cpp` / `T/ElysiumFootstepSeamTests.cpp`.
It owns `docs/vtmb/npc-ai/programs.md`, `docs/specs/0002-npc-ai/stories/v1/triage.md`,
spec.md, TRACKER.md, `research/tooling/ghidra/driver/kernel_verdicts.tsv` and generated
ledger outputs. No unbounded “fix any file” coder authorization. Generated Slots
are never hand-edited; hand bodies go in SlotBodies, verdict row by integrator.

## Shared files with earlier waves

No coder-manifest file overlaps V4d's listed three lanes or V5b's three lanes.
Integrator **does** share `S/ElysiumNpcThink.cpp` with the earlier V4c work,
`S/ElysiumNpc.h` with V4d (read-only numeric/accessor additions only if needed),
and triage/spec/TRACKER/kernel_verdicts/generated npc-kernel docs with both prior
integrations. V5b's integrator owns Npc.cpp, Guard1, Schedule and their tests;
this wave reads those but does not plan edits. V4d owns TestServices.h,
WorldServices.h, bodies/map embodiment, lifecycle and corpse records; this wave
does **not** edit them. The fixture helper implements its own minimal recording
services instead of changing TestServices.h. Preserve V4c zero stage clock,
state-establishment ban rule, shared RNG and all V5b interrupt-cache changes.
Never merge an older whole file over either landed wave.

## Arena contracts — all new support belongs to lane 3

Every row below is a required arena record. `arena` is the Green Room. On a
witness map use existing real input/locomotion; exact cleanup/freshness boundaries,
run-band, landing/gate arms and simultaneous list ordering lack deterministic
map staging, so lane 3 adds a strict `sound_fixture` action that invokes the
actual bus/senses/player functions in an **isolated substrate fixture** hosted
by the Green Room runner. It has a finite named `case`, no arbitrary expression
or blanket “pass” result. Fixtures publish observed words through sound trace
events for JSON expect/never/probes to judge; never bake expected answers into
the helper. Real-map records prove the actual producer/consumer wire. Diagnostic
record always uses the real staged world, no forced clock or suppressed sensing.

New trace kinds: `sound_insert`, `sound_refresh`, `sound_cleanup`, `sound_listen`,
`sound_candidate`, `sound_word`, `sense_gate`. Text identifies `id`, `revision`,
raw `type`, owner, world stamp and phase; insert includes duration/expiry, cleanup
deadline/reason, listen old/new stamp, candidate admitted/reject reason and
delay deadline. Runner uses existing synchronous EmitAiTrace and keeps its sink
observational. Bus observer installed/restored with runner lifetime; no shipping
logs, RNG draw, clock advance or gameplay state change from instrumentation.
Optional `sound_watch` names the listeners to snapshot at actual insert/refresh:
arena_listener in donors/diagnostic, thug_1 in map records. A late-spawned child
is observed only once it resolves. This gives ordered actual full/alert/listen
snapshots without unsupported dynamic probe times.

| name / stage | staging and expect / never, with retail citation |
|---|---|
| `hear_world_diagnostic` / arena | Copy hear_world donor, neutral listener, level3 WORLD/owner player, seed1, PlaySound t2, **no teleport**, duration8. Expect input then actual sound_insert, then actual sound_listen or sense_gate and a disposition line naming this identity; never fixture script or sound_word error. Publish last listen at insert even when first subsequent pass has no pending serial; record evictions. Do not assert OnHearWorld to keep cause collection available on a miss. P2, 0x101ad470/0x101bac90/0x1030f940. |
| `hear_world_investigate` / arena, existing | Retain real ambient row, investigation mode3, teleport and full return-home chain. Expect HEAR_WORLD/OnHearWorld/break0x25/ALERT/0x51/path/return; never taskfail/SEE_PLAYER. 0x1030f940/0x1026a5e0/0x102b3270/0x102b9060. Remove N4 known_red only on measured closure and three boots. |
| `interest_mode_never` / arena, existing | Same sound with mode0. Expect HEAR_WORLD plus OnHearWorld; never INVESTIGATE_SOUND/investigation schedule/subsequent state edge; final Idle. 0x102b3270/0x102b9060. Initial NONE->IDLE retains V4c exception. |
| `sound_lifetime_grace` / arena | Fixture finite WORLD t2,D1, lastListen1, first Listen3.2, retained through cleanup3.2. Expect admitted beyond expiry and delayed HEAR_WORLD/OnHearWorld by4.5; never reject expiry. 0x101ba890/0x1030f7b0/0x1026a5e0. No witness scripted sense-disable input stages an exact first listen. |
| `sound_lifetime_long` / arena | Fixture insert WORLD t2,D10 and short WORLD t2.1,D1; cleanup7.2. Expect short gone, long still present/admitted (lastListen1); never age-prune long. Entire-list removal must reach short behind long. 0x101ba890/0x101bac90. |
| `sound_lifetime_prune` / arena | Fixture initial nextCleanup1, recurring now+0.3; insert t2,D1. Observe before threshold6.999, equality7.0 and later7.001 in separate fresh fixtures. Expect retained before, removed at equality/after **on due cleanup**, otherwise retained until due. Never finite expiry removal at3.0 nor extra Emit required. 0x101ba6f0/0x101ba890. |
| `sound_freshness_equal` / arena | Fixture Listen2, insert2, Listen2.1: expect reject freshness, lastListen2.1; never admitted/HEAR_WORLD/OnHearWorld for it. Equal-time rejection is the retail control. 0x1030f7b0/0x1030f940. |
| `sound_freshness_new` / arena | Same but insert2.001; expect one admit then delayed output; next Listen rejects old time; never a second admit/delay draw for unchanged stamp. 0x1030f7b0/0x1026a5e0. |
| `sound_listen_empty_mask` / arena | Fixture camera interest0: Listen2 advances last stamp and runs empty OnListened; fresh WORLD from2.001 is mask-refused next pass. Also test no-active-sound pass and sensing-disabled gate (no Listen/stamp). Expect observed gate/stamp words, never accepted. 0x1030f940/0x10310710. |
| `sound_listen_order` / arena | Fixture two fresh WORLD/DANGER insert2.01/2.02 plus reserved PLAYER allocated first; refresh reserved2.03. Expect query order newest allocation first, delayed draws oldest admitted first, stable slot position on refresh; never refresh relocation/double dispatch. Include wrong-mask/self/outside radius controls and equality-radius admission, unchanged slot467/occlusion refusals. 0x101bab50/0x1030f940/0x1030f7b0/0x1026a5e0. |
| `sound_player_reserved` / arena | Fixture reserve/refresh a type4 record at2, refresh2.1, quiet zero-volume restamp2.2; unrelated finite cleanup after10. Expect stable identity, changing revision/time, expiry=-1, one record including zero; never retire/age/expiry-evict it. Listen during positive frame admits; zero never raises new player hearing. 0x101baf80/0x1016b480/0x101ba890. |
| `sound_player_modes` / arena | Fixture actual UpdatePlayerSound with recording locomotion: grounded sneak at positive speed=180; standing 128=walk240; standing129=run240; 3-D vertical speed participates; jump bit wins duck/air=240; ground anim8 soft180, 10/11 hard240; stationary/air no jump target0; priority overlaps. Expect exact target/category/type/occlusion words, never category from animation footfall. 0x1016b4c9..0x1016b5db + authored table. |
| `sound_player_decay` / arena | Fixture post-move frame dt1/60, old240,target0 ->235 (truncated), next230; target180 clamps; increase jumps to240. Expect once-per-frame post-animation sound_refresh and integer words, then zero; never second pre-move refresh/0.1-heartbeat-only update. Preserve observed timestamp and ordinary frame delta. 0x1016be10/0x1016b5ee..0x1016b610. |
| `sound_player_gates` / arena | Fixture separates FL_NOTARGET (volume0, previous owner/type/time preserved), m_fNoPlayerSound (volume0 but ordinary restamp), missing reserved lookup handling, no live body (named locomotion seam), and dead PostThink gate. Expect exact observed writes/no writes; never free the slot or dispatch post-death refresh. 0x1016b4b8/0x1016b610/0x1016be10. Unknown game-over/locked/observer inputs remain named, not claimed implemented. |
| `footstep_events_no_ai` / arena | Fixture sends all2050..2053 to real NPC/player handlers, valid source; recording audio proves NPC handler reached. Expect NPC audio-only and player swallowed; never sound_insert/refresh attributable to either event. Do not run PostThink movement in this fixture. 0x10274e30/0x1026d460/0x10178a10. |
| `map_tutorial_sneak_past` / map:sp_tutorial_1, existing | Keep maker.Spawn, pt1 place leg and later sight/enemy/output. Probe child full_investigate=0 and initial alert0 before walking; expect player sound refresh/type4, OnHearPlayer/HEAR_PLAYER, break INVESTIGATE_SOUND, **exact0x4c first rung**, then original SEE_PLAYER/NEW_ENEMY/OnFoundPlayer. Never SEE_PLAYER/OnFoundPlayer before deliberate sight teleport or death. 0x1034b7b0/0x1016b480/0x102b9060/0x102b8980. Correct known_red/prose, not child keys. |
| `map_tutorial_hearing_walk_radius` / map:sp_tutorial_1 | Same real maker/place setup; after at_place+3 walk from [-1280,1000,-96] toward [-1280,747,-96], behind pt1. Standing movement crosses240u and remains beyond180u; expect type4 radius240 and OnHearPlayer/HEAR_PLAYER/0x4c. Never SEE_PLAYER/OnFoundPlayer in hearing window. Probe actual eye-to-sound distance in (180,240] Source units; correct geometry if trace disproves seats, not radius. 0x1016b480/0x1030f7b0. |
| `map_tutorial_hearing_sneak_radius` / map:sp_tutorial_1 | Same path, crouch before movement, settle old volume to0 before walk. Expect crouched real sample, sneak180 refresh and completed walk; never HEAR_PLAYER/OnHearPlayer/SEE_PLAYER/OnFoundPlayer on path and through reaction tail (>=1.5s). All points stay >180u. Probe actual range and crouch; no light-based sound attenuation. 0x1016b480/0x1030f7b0, table180. |

Ordered expectations must follow **actual emission order**. In current TickHearing
output precedes the gather's cond+ tap; use output then cond+ if both are asserted,
not an impossible reverse pair. Deadlines are seed/staging bounds, not new retail
constants. Exact fixture boundary times are independent of rendered frame rounding.
Witness map records cannot share a rebuilt map; run alone, then run the same
record after an unrelated record in the launcher (separate map boot). GR records
must also run after `control_sequence` in the **same boot**. Three N4 acceptance
orders are solo, after control_sequence, and after interest_mode_never (reverse
pair for interest). Include hear_world_out_of_range as an unchanged negative.

## For the judge

1. **If N4 is equal-time loss:** retail 0x1030f7b0 requires strict freshness;
   port RunThinks->ServiceEvents makes equality possible. Listing/scouts do not
   establish the retail engine's queued-input vs NPC frame order at that delivery.
   Alternatives: prove a record-phase error and stage the real input after a
   completed Listen with a genuinely later clock; or recover the engine delivery
   chain before a runtime change. No epsilon, synthetic timestamp or blanket IO
   reorder. If failure does not reproduce, close only measured lifecycle parity
   and report historical cause unproved; N4 acceptance still needs three green boots.
2. **R1 allocator/other producers:** 0x101baf80 has64 entries, 0x101bab50 refuses
   exhaustion; current128/oldest-eviction is inherited divergence. Alternatives:
   R1's complete allocator/producer story, or owner explicitly pulls that separate
   scope forward after its full contract is recovered. This wave stays below
   pressure and implements no replacement pool policy. Other sound types, memory
   words and investigation programs remain R1; no producer census lane.
3. **Player landing/whole PostThink and restore:** the existing landing latch lacks
   the full retail SetAnimation publisher; unused game-over/locked/observer inputs
   are named seams. V6 owns save/restore of transient senses/player sound state;
   no archive migration or fresh restore semantics here. Alternatives: respective
   player/lifecycle stories supply true fields; fixtures pin already recovered
   producer arms without claiming those missing sources. Raw Jump has a source now
   and **is** wired in this wave.
4. **Coincident sound cleanup/NPC think priority:** 0x101ba890 settles its
   callback and recurrence, not engine thinker order at the expiry+grace boundary.
   Alternatives: participate in existing entity think order with recovered retail
   priority, or measure/recover the exact boundary ordering before a parity claim.
   Integration traces both phases; fixtures pin both explicit callback orders.
   No arbitrary pre-Listen timer priority is adopted as a modernization.
5. **Only if measured content differs:** both retail BSPs author full_investigate0;
   deployed maker/volume receipt or missing WAV is an import/pipeline issue.
   Alternatives: correct record provenance to current baked map if legitimate,
   or explicit content owner/import/re-bake. No content/pipeline/bake coder lane;
   no expected missing content is currently evidenced.
