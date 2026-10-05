# Brief V10 integrator — diagnostic first, V10 then V12

Read AGENTS.md, README, packets-V10, three coder briefs/reports, Arena/README and
triage N4/Q-V3bf1. Coordinator names integration branch and each coder worktree.
Wait for V4d and V5b to land. Read their actual closing commits and preserve
their changes. V4c `d0f79574` recorded169/0 default,1624/0 arm, arena113 pass /
1 fail /16 EF /2 UP of132; these are historical commit results, not your binary.
The two UP are N4. No planner build/test/arena has been run.

## 1. Compile pass first — six builds total, about two minutes each

1. Verify the 4+9+26=39 coder manifests and empty pairwise intersections.
   Review all hunks for C4458/C4459 shadowed locals, includes/duplicate declarations,
   agreed observation/API names and landed V4d/V5b diffs. Keep V4c zero-before-Load
   stage clock, startup state trace/ban rule and shared RNG. Coders do not build,
   run or commit; integrate their file diffs serially by explicit path.

2. **Build1 is observation only.** Bring in lane1 stamp accessor and observational
   Listen/sense taps, lane2 legacy Emit/Evict observation callback, lane3 trace
   whitelist/observer/diagnostic record. Keep old listener/bus/player behavior
   intact. Do not yet apply fixture dispatch/helpers/probes that depend on the
   correction API, or put the twelve corrected-behavior fixture JSONs into the
   active arena inventory. Bring those in at step7. Coders identify these hunks
   separately; do not use a gameplay legacy-mode toggle to simulate the old port.
   Observation must capture a rejection/missing candidate without changing it.

3. Add these exact **observation-only owed lines** before first compile:
   - `Source/ElysiumUE/Private/Substrate/ElysiumNpcThink.cpp::NPCThink /
     Think19NormalSet2`: **0x10292e8e / 0x102934a9 / 0x1029357c** trace disabled,
     normal/update/AI due, reduced/full, LastNormal/NextNormal/NextAI and current
     clock. Preserve every return and writer. Do not infer full sensing from a
     normal tick or from OnHearWorld.
   - `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseConditions2.cpp::GatherConditions`:
     **0x1026ecce / 0x1026ecf1 / 0x1026ee04** trace actual state/PVS/spawnflag/
     enable gates and actual Conditions19PerformSensing entry. A no-sense pass
     emits a disposition with the same diagnostic identity but no fake Listen.
   - Bridge runner's optional bus observer to the real trace sink, with exact
     raw sound identity/revision/insert/expiry/duration and actual listener's
     last stamp at insertion. Every cleanup/eviction has a reason, including
     legacy expiry/age/pressure. Use callback lifetime guards on all exits.
   Runtime lines cite retail addresses; debug instrumentation says observation.

4. Run **`uv run elysium build --arm`** until compilation succeeds. Allow at most
   **six builds across the whole wave**, about2 minutes each, including correction
   and any later compile-required fixes. Record actual wall times. At sixth
   failed build stop with exact errors/uncompleted work; never use an old binary
   or omit a lane to claim acceptance. First build precedes any test/arena here.

## 2. Default, then the first diagnostic record

5. On the successful observation binary, **`uv run elysium test`**. Compare with
   actual landed prerequisites. Observation-only regressions belong to this wave.
   Then run **`uv run elysium arena hear_world_diagnostic`** first. Run existing
   hear_world_investigate/interest_mode_never in these separate boot orders:
   solo, after control_sequence, and after each other; no deliberate cadence/
   sense suppression, fixture, seed change or timestamp offset. Keep traces on
   passes as well as misses. Loop freely if needed to capture a miss; don't
   advertise repeated passes as proof of the historical cause.

6. Record for each relevant identity: actual insertTime, D, expiry, lastListenAtInsert,
   every entered Listen old/new stamp, next eligible full-gather stamp, admission/
   refusal, PVS/LOS/state/enable/due gates, removal time/reason, delayed deadline
   and promotion. **Equality** of insert and prior lastListen supports freshness
   candidate A; **strictly fresh** plus Listen later than expiry supports B only
   if legacy expiry removal/rejection is observed. No pending candidate plus
   observed bus eviction is its own failure site. The promotion's timestamp does
   not substitute for the first listen. If neither fits, recover the measured
   gate/chain before changing another runtime file. No new divergence is adopted.
   If A appears, retail also rejects it: preserve strict freshness and send
   unresolved engine queued-input ordering to README's judge item. A proven
   record phase error may use genuinely later real input timing after an
   observed Listen; never synthetic Time, epsilon, duration bump or IO reorder.
   Keep the unchanged donor trace in evidence alongside any justified correction.

## 3. Apply V10/V12 corrections and owed lines, then recompile

7. Bring in remaining lane1/2/3 correction/API/test/doc/record hunks. Apply exact
   out-of-lane lines, with addresses, before the next build:
   - `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorld.cpp::Activate`
     (actual activation method), **0x101ba6f0 / 0x101baf80**: initialize the bus
     sound-entity deadline at now+1, reserve player record once; fixture/reset
     isolation must not inherit earlier stage clocks. `::Tick`, **0x101ba890**,
     advances due cleanup at now+0.3 and prunes expiry+4<=now, sentinel excluded.
     Model this as the sound entity's think phase, no cleanup hidden in Emit.
     P1 settles callback timing, not coincident engine thinker priority; see
     measurement note below. Preserve ordinary world IO and NPC cadence order.
   - `ElysiumEntityWorld.cpp::RefreshGameSound` and
     `Source/ElysiumUE/Public/ElysiumEntityWorld.h` declaration,
     **0x1016b480**: type4 quiet updates keep reserved identity/volume0/time,
     explicit radius0 never falls back to table radius. Early FL_NOTARGET writes
     volume only without restamp. Remove player finite0.2 duration; explicit
     teardown still retires. Preserve other wrappers/callers and finite
     producer defaults; no blanket R1 producer edits.
   - `ElysiumEntityWorld.cpp::Tick`, **0x1016be10**, passes actual frame delta
     into the existing post-move PostThink tail after animation/UpdateCharacter
     and before NPC thinks. Lane2 removes UpdatePlayerSound from pre-move Think.
     GetPlayerButtons already carries raw Jump; no input-controller change.
   - `Source/ElysiumUE/Private/Tests/ElysiumFootstepSenseTests.cpp` and
     `ElysiumFootstepSeamTests.cpp`: apply only named obsolete finite0.2/cursor/
     fractional-decay assertions owed by coders. Keep existing other arms.
     No TestServices.h edit (V4d owns it), no archive/restoration patch.
   - `Source/ElysiumUE/Private/Substrate/ElysiumNpc.h` only if const accessor
     needed for full_investigate/alert probes; existing public words suffice
     at planner snapshot. Preserve V4d corpse/capability changes whole.
   - `docs/vtmb/npc-ai/programs.md`, INVESTIGATE-family section,
     **0x102b9060 / 0x102b8980 / 0x1034b7b0**: record separate mode/full flag,
     exact tutorial maker0 replay and first player-sound0x4c; no speculative
     claim that mode4 forces full investigation. Lane1/2 own senses/footsteps.
   - Integrator alone adds/updates **`research/tooling/ghidra/driver/kernel_verdicts.tsv`**
     rows for repaired covered functions/evidence, no invented out-of-closure
     player row. Generated `*Slots.cpp` never hand-edited: needed hand body goes
     in matching `*SlotBodies.cpp`; actual verdict row plus
     `uv run elysium research kernel` performs regeneration. Rebuild for generated
     compile-impacting changes within budget. No new virtual body is expected.

   **Coincident cleanup measurement:** read actual World tick/RunThinks ordering
   after prerequisite integrations and record cleanup vs Listen stamps at one
   due threshold. The listing proves initial/recurring deadline and callback
   predicate, but not engine entity scheduling priority. Prefer a sound-entity
   think participating in existing entity think order if needed to reproduce
   observed retail ordering; do not claim an arbitrarily chosen unconditional
   pre-Listen phase is verified. If equality ordering materially changes a
   gameplay verdict and retail priority remains unrecovered, report it to the
   judge instead of hiding it in a timer epsilon. Fixture prune tests explicitly
   call cleanup then Listen, and separately Listen before due cleanup, pinning
   both legal callback orders without inventing an engine order.

8. Recompile **`uv run elysium build --arm`**, then **`uv run elysium test`** for
   final implementation. Retain build1 observation evidence and actual new binary
   provenance. Correct wave failures against listing, not inherited assumptions.

## 4. Named records — loop freely, all new records alone and after another

9. Run these names, retaining result/first_unmet/trace, not merely exit code:

   ```text
   uv run elysium arena hear_world_diagnostic hear_world_investigate interest_mode_never hear_world_out_of_range sound_lifetime_grace sound_lifetime_long sound_lifetime_prune sound_freshness_equal sound_freshness_new sound_listen_empty_mask sound_listen_order sound_player_reserved sound_player_modes sound_player_decay sound_player_gates footstep_events_no_ai map_tutorial_sneak_past map_tutorial_hearing_walk_radius map_tutorial_hearing_sneak_radius
   ```

   JSON is tracked data: edit/loop freely without a build for record-only changes.
   Every **15 new records** runs alone and after another. For each of the13 new
   Green Room records use `uv run elysium arena <name>` then
   `uv run elysium arena control_sequence <name>` in the same boot. For the2 new
   map records use alone and launcher-order after control_sequence; map host
   must boot separately (no shares_map) because it cannot rebuild its map.
   Existing N4 donors each need three independent boots/orderings; reverse the
   pair and include solo/after control. Run original tutorial after its two
   controls as well, each on a fresh map boot.

   Apply README's exact expect/never/staging contracts. Fixture words derive
   from actual bus/senses/player transactions, not hardcoded success answers.
   Real map records must sample actual post-move radius/type/time and child
   full_investigate0/initial alert0. Stated coordinates are a data-based lead:
   verify collision and ear distance at the (180,240]u band; correct a disproved
   seat, never threshold/cone/LOS behavior. Crouched negative requires settled
   old volume0 and a >=1.5s reaction tail, otherwise stale standing volume
   confounds it. Standing positive cannot pass on visual enemy acquisition.

   Actual outputs can precede cond+ trace in one gather; reorder JSON clauses
   with that reason, not looser deadlines. Preserve full tutorial's later
   SEE_PLAYER/NEW_ENEMY/OnFoundPlayer leg; first heard-player program is exact0x4c.
   Remove stale V12/Q known_red with this recovered record correction. Remove
   N4 known_red only when diagnostics plus three green boot records justify
   closure. Historical causation can remain unproved even with repaired
   semantics/stable measured acceptance; state that explicitly.

   **A record this wave writes or breaks is this wave's.** Fix wave regressions;
   no new known_red parking without concrete retail evidence and named actual
   later owner. For later spec/content/bake work, use README's judge list,
   not a new lane or an unrequested bake. Preserve unchanged out-of-range and
   unrelated combat/corpse/interrupt records. If acceptance cannot be reached
   without unknown engine order, report incomplete closure, not a false tick.

## 5. Close in this exact order

10. After final default and named records, **`uv run elysium test arm`**.
    All wave arms plus retained footprint must pass. Appropriate families are
    NpcSenses, GameSound (use actual registered prefix from the test file),
    Footsteps/PlayerHearing; arm-only results supplement, never replace arena.
    Run the modified discovery/schema Python test file with
    `uv run pytest pipeline/tests/test_arena_suite.py -q -n 0` after compilation;
    new C++ parser refusal arms in the debug fixture helper run in the arm tier.
11. **`uv run elysium research kernel --check`**. If stale, correct owed verdict
    rows, regenerate with `uv run elysium research kernel`, check again.
    Compilation-impacting output returns to build/affected validation within
    six-build total; final arm and kernel check describe final sources.
12. **Full arena once**, **`uv run elysium arena`**, after final code/build/record
    change. Compare with actual prerequisite baseline, each moved verdict:
    record, before, after, first_unmet/trace, retail reason and remaining owner.
    Existing H11/lifecycle reds are neither new wave fixes nor places to park
    regressions. A later change invalidates final acceptance and needs renewed
    affected/default/arm/kernel evidence plus a fresh final full run.
13. Update triage N4/Q-V3bf1 with measured timeline and exact scope; tick spec V10,
    V12 and TRACKER boxes only after their acceptance. Name current cause vs
    historical-unproved separately. Record inherited R1 pool divergence, player
    gate/landing/restore seams and any concrete content receipt issue. Do not
    tick all of R1 or claim a complete footstep SetAnimation/pool port.
14. **One commit**, staged by **explicit file paths** only (39 lane paths plus
    actual owed/doc/ledger paths and six planner files); no git add -A or dot.
    Coordinator integration branch, **never push**. Message includes verdict
    table, baseline/final default/arm/full arena totals, six-build wall-time
    ledger, observation/after diagnosis, migrated assertions and retail causes,
    unmeasured historical cause and named seams/judge owners. Suggested subject:
    `fix(npc): V10 sound lifetime and V12 reserved player stimulus`.
    No file named **`report*.md`**. Do not commit/tick a claimed green wave while
    records you wrote/broke remain unexplained. No cleanup/reset/stash or
    worktree deletion; leave assets/worktrees for their owner.

Final report under350 words: actual files/addresses, builds, default/arm/kernel/
arena results, diagnostic A/B/other verdict, record changes and judge leftovers.
Wait for actual process completion; do not report a spawned process as success.
