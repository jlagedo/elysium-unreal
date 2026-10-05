# V10 + V12 — sound lifetime, then the reserved player stimulus

Judge-applied planner cut, 2026-10-05. Read AGENTS.md,
[packets-V10.md](packets-V10.md) and the final
[judge's third sitting](../v1/judge-third-sitting.md). This wave covers **V10 then
V12**, after committed **V5b -> V6 -> V7**. Planner edits only the existing six
Markdown files under `E:\elysium-work\worktrees\coord`; no source edit, build,
test or commit. Implementation worktrees/integration branch are coordinator-named.

Integrator's baseline is the **latest commit of the work branch when it starts**,
including V5b/V6/V7. Today that is **`a5b58f37` (V4d)**: arena **114 pass / 1 fail
(`rollcall_vzombie`) / 15 expected-fail / 2 unexpected-pass**; default **169/0**;
arm **1,625/0**. These are historical recorded results, not a new measurement.
V5b lands before V10; capture subsequent prerequisite commits/results at start.

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
   sentinel excluded, whole-list pruning, no insertion-age cutoff. Pull forward
   the 64-slot allocator/free-list/refusal slice (P5), preserving client reservations.
   Preserve the
   actual QueryHearSound, list ordering, delayed-condition/RNG/output chain.
3. V12 corrections: one in-place reserved type-4 record, expiry -1, including
   silent frames; producer in existing post-move PostThink tail each frame;
   retained Jump button and integer per-frame decay; save/rebind new pool/list/
   player-volume state through V6's common applier, with transient last-listen
   reconstructed at 0 (P6). No animation-event AI insert.
   Correct tutorial's first heard-player program to 0x4c: Q-V3bf1 is settled.
4. Compile final sources, default tests, records by name (loop freely), arm,
   kernel check, final full arena once; record all moved verdicts and commit once.
   Six-build total budget across observation and correction passes, ~2 min each.

No new modernization is authorized. Equal-time freshness is retail, not an expiry
bug. If diagnostics show equal-time loss, do not change `<` into `<=`, reorder IO,
add epsilon or lengthen the sound. Establish whether the record deliberately
collides with retail freshness or a real port delivery divergence exists. Correct
only a proven record error with evidence. Think-before-queue is recovered
(P7); a finer measured discrepancy requires its exact retail chain before a
runtime correction, and any unexplained miss keeps N4 open.

## Three coder manifests

Paths are repository-relative, exhaustive; write by absolute path in the worktree
the coordinator names. `S` below means `Source/ElysiumUE/Private/Substrate/`,
`T` means `Source/ElysiumUE/Private/Tests/`, `D` means `Source/ElysiumUE/Private/Debug/`.

| lane | exclusive files |
|---|---|
| V10-1 listener | `S/ElysiumNpcSenses.cpp`; `S/ElysiumNpcSenses.h`; `T/ElysiumNpcSensesTests.cpp`; `docs/vtmb/npc-ai/senses.md` |
| V10-2 lifetime/player | `S/ElysiumGameSound.cpp`; `S/ElysiumGameSound.h`; `S/ElysiumPlayerEntity.cpp`; `S/ElysiumFootsteps.cpp`; `S/ElysiumFootsteps.h`; `Source/ElysiumUE/Public/ElysiumPlayer.h`; `T/ElysiumGameSoundTests.cpp`; `T/ElysiumPlayerFootstepTests.cpp`; `docs/vtmb/footsteps.md` |
| V10-3 harness/records | `D/ElysiumArenaScenario.h`; `D/ElysiumArenaScenario.cpp`; `D/ElysiumArenaScenarioRunner.h`; `D/ElysiumArenaScenarioRunner.cpp`; new `D/ElysiumArenaSoundFixtures.h`; new `D/ElysiumArenaSoundFixtures.cpp`; `Arena/README.md`; `pipeline/tests/test_arena_suite.py`; the 24 JSON files enumerated below |

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
  `world/map_tutorial_hearing_sneak_radius.json`,
  `perception/sound_pool_pressure.json`,
  `perception/sound_pool_reuse.json`,
  `perception/sound_pool_reserved_survival.json`,
  `perception/sound_save_finite.json`,
  `perception/sound_save_reserved.json`,
  `perception/sound_cleanup_coincident.json`.

**Disjointness:** A has 4 files, B 9, C 32 (8 harness/schema files +24 records).
Literal path sets give A∩B=A∩C=B∩C=∅; 4+9+32=**45 unique files**. Dependencies are calls/owed lines, never
cross-lane edits. Each coder brief repeats the full manifest and numbered jobs.

Integrator alone owns exact owed lines in `S/ElysiumEntityWorld.cpp`,
`Source/ElysiumUE/Public/ElysiumEntityWorld.h`, `S/ElysiumNpcThink.cpp`,
`S/ElysiumNpcBaseConditions2.cpp`, `S/ElysiumNpc.h`, and, if old fixture assertions
need migration, `T/ElysiumFootstepSenseTests.cpp` / `T/ElysiumFootstepSeamTests.cpp`.
The bounded V6 handoff additionally owns `S/ElysiumEntityWorldPersistence.cpp`
(`Freeze/ApplySnapshot`), `Source/ElysiumUE/Public/ElysiumSaveTypes.h` (snapshot
state), and `Source/ElysiumUE/Private/Session/ElysiumSaveArchive.cpp` (codec),
only for new sound state/due placement and audited field policy. These are
integrator-owned, disjoint from all coder paths. It owns `docs/vtmb/npc-ai/programs.md`, `docs/specs/0002-npc-ai/stories/v1/triage.md`,
spec.md, TRACKER.md, `research/tooling/ghidra/driver/kernel_verdicts.tsv` and generated
ledger outputs. No unbounded “fix any file” coder authorization. Generated Slots
are never hand-edited; hand bodies go in SlotBodies, verdict row by integrator.

## Shared files with earlier waves and start order

**V10 waits for V7**, which itself follows V6; V5b must also have landed. V10's
lanes/integrator share `ElysiumNpcSenses.*`, `ElysiumEntityWorld.cpp`,
`ElysiumPlayer.h` and arena scenario files with V6/V7 lanes. All four briefs carry
this start fence: relocate functions by name in those waves' committed code.
Preserve their changes, shared RNG/draw order and state-establishment ban rule.
V6's fresh-map epoch is **1.0 before Load/entity initialization**; revisit uses
frozen map time and explicit load saved time. Re-measure zero-tuned records
alone and after another, with evidence for each staging/epoch correction; no
old-draw injection, seed change or widened behavioral bounds.

**One round, at most three coders.** After the prerequisite fence lanes 1, 2 and
3 may start together, agreeing API names; only dependency hunks wait for lane2's
pool/capture API and lane1's observations. The allocator fits lane2's existing
files; lane3 owns all six additional records. No lanes4–6 or new brief needed.
Integrate observation hunks first; compile/default/diagnose the donors before
applying corrections, then serially integrate correction APIs and consumers.
Integrator alone wires callback placement and the bounded V6 common-applier
handoff after coder integration. No simultaneous coder/integrator writes.

TestServices.h/WorldServices.h are outside this wave; helpers provide local
recording services. Keep V4d corpse/body changes and V5b interrupt-cache changes.
Never merge an older whole file over any landed wave.

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
The two save/load witnesses use landed V6 production transport/common applier;
the coincident-due witness runs the production world dispatcher. None is a
sound_fixture callback-order result. Fixed-case controls retain explicit
substrate stamps separate from the host's V6 fresh epoch.

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
| `sound_pool_pressure` / arena | Real-bus fixed case: 64 total slots, configured single client reserves one; allocate 63 finite, refuse next request with no insert/hear or displaced identity/order. Inspect full/free heads and chain, not just count. P5, 0x101baf80/0x101bab50/0x101bac90. |
| `sound_pool_reuse` / arena | Real-bus fixed case frees active head/interior/tail, proves exact unlink and free-head LIFO reuse; unchanged survivors and distinct new allocation identity vs revisions. Never oldest eviction. P5, 0x101ba9d0/0x101bab50. |
| `sound_pool_reserved_survival` / arena | Pressure plus finite cleanup and quiet in-place refresh retain same reserved identity/position, expiry=-1 and zero volume; no capacity bypass or replacement. P5/P3, 0x101baf80/0x101ba890/0x1016b480. |
| `sound_save_finite` / arena | Real ambient WORLD delivery, checkpoint after actual expiry but inside grace; V6 codec/storage/common applier restores pool/heads/links/TIME/owner/due state. Fence before first Listen has lastListen=0; first real Listen admits, then delayed HEAR_WORLD/OnHearWorld/program chain; no direct member-copy load/forced listen. P6, 0x101a2e40/0x1027ccda/0x1027c160/0x1027bf50/0x1030f940. |
| `sound_save_reserved` / arena | Actual post-move player producer, production checkpoint/apply, observe at fence before Listen/PostThink: saved target/current INT volume, rebased owner/client binding, same row/list position and sentinel policy; next real frame decays/restamps and hearing continues. Include zero-volume saved row and missing-owner refusal control; no fresh allocation or synthetic publisher. P6/P3, 0x1016b480/0x101a2e40/0x1027ccda. |
| `sound_cleanup_coincident` / arena | Real-world RunThinks dispatch: soundent cleanup and NPC due together at finite expiry+4 equality. Bounded staging hook sets due inputs, callbacks run through production dispatcher; trace stable identity, native fresh/restore placement, dispatch order and removal/admission. Assert mapped order, never fixture-only priority or privileged timer. Keep explicit before/after callback controls in sound_lifetime_prune. P7, 0x1023c020/0x100f9fc0/0x100f7060/0x1003bdd0/0x101ba890/0x101a2e40. |
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

## Judge's rulings applied

Final rulings from the third sitting; no open alternatives remain in this plan.

| item | ruling | where it landed / proof |
|---|---|---|
| V10.1 equal-time N4 | do now | V10-1 jobs1–4 retain strict freshness; V10-3 jobs6/13 and integrator jobs3–5 observe both donors first, restage only a proved phase error. P7 think-before-queue; equal-time negative plus genuinely later real delivery. |
| V10.2 allocator | pull forward | V10-2 job8, P5; V10-3 job10 and three pool records. Manifests include refusal/reuse/reserved survival. |
| V10.3 producer/restore | do now | V10-2 jobs4/5/9; V10-1 job6; V10-3 job11; integrator job8 bounded V6 handoff. P6 and two real save/load records before first Listen. Landing/other player arms filed below. |
| V10.4 callback order | do now | V10-2 jobs2/10; V10-3 job12; integrator job7. P7 native-list insertion/restore mapping gates acceptance; explicit order controls plus live coincident equality record. |
| V10.5 prospective maker/WAV work | refuse | Removed speculative maker/pipeline/re-bake lane and alternatives. V10-3 jobs7/13 and integrator job9 still prove installed child0/alert0, first PLAYER rung0x4c, actual WORLD duration/receipt; evidenced required payload alone permits narrow acceptance repair. |
| Standing Clock | do now | Start fence in all briefs; README order; integrator jobs1/9. Fresh1.0, frozen revisit, saved load; re-measure alone/after without seed/draw/window compensation. |
| Standing N4 closure | do now | V10-3 jobs6/13, integrator jobs4/5/10/13; final donor dispositions in three independent orders plus controls and unchanged out-of-range. Diagnostic green alone cannot close; historical cause remains unproved without causal evidence. |

### Filed seams and the integrator's close

- **0002/R1:** named `R1SoundProducer` and `VSound` producer seams; producer
  census, other sound sources, remaining memory families/investigation programs
  remain R1. Existing unproduced-sound live witnesses stay absent. V10 closes
  only this pool/free-list/lifetime/player slice, never all R1.
- **0015 player layer0:** `PlayerLayer0LandingState`, the true SetAnimation/
  landing-state publisher. `sound_player_landing_live` stays absent; fixed landing
  input proves the producer's consumer arm only.
- **Player story:** `PlayerPostThinkGameOver`, `PlayerPostThinkLocked`,
  `PlayerPostThinkObserver` named unavailable producer accessors, and
  `PlayerSoundLocomotion` for unavailable embodiment samples. Keep
  `sound_player_game_over_live`, `sound_player_locked_live`,
  `sound_player_observer_live` absent. Bind already landed sources by function
  name; leave genuinely unavailable accessors answering nothing with retail-field
  comments, rather than advertising fixtures as live publishers. Unrelated whole
  PostThink arms remain that owner.

Integrator job13 lists each seam, owning later story, absent record and exact
implemented boundary. New sound state restoration is **not filed back to V6**:
its common-applier handoff closes in this wave. Unknown donor rejection, callback
placement or save-load miss blocks the relevant acceptance, not a filed-red tick.
