# Brief V10 integrator — observation, sound pool/lifetime, reserved player and restore

**Start fence:** V10 runs after committed V5b, V6 and V7. Its lanes share
`ElysiumNpcSenses.*`, `ElysiumEntityWorld.cpp`, `ElysiumPlayer.h` and arena
scenario files with V6/V7. Relocate functions by name on the committed code of
all preceding waves; never replace a landed file with the planner snapshot.
Fresh stages use V6's **1.0 before Load/entity initialization**; revisits use the
map's frozen clock and explicit load its saved clock. Preserve seed/reset order,
shared draws, state-ban rules and original behavioral bounds. Re-measure old
zero-tuned records alone and after another; change only proved epoch/staging
assumptions with per-record evidence, never replay RNG draws or widen windows.

Read AGENTS.md, README, packets-V10, three coder briefs/reports, Arena/README,
triage N4/Q-V3bf1 and the final judge-third-sitting. Coordinator names integration
branch/worktrees. The baseline is the **latest commit of the work branch when
you start**, after V5b/V6/V7, not an assumed planner binary. Today it is
**a5b58f37 (V4d)**: arena **114 pass / 1 fail rollcall_vzombie / 15 expected-fail /
2 unexpected-pass**; default **169/0**; arm **1,625/0**. Record subsequent closing
commits/results and actual installed binary provenance. Planner ran no build/test.
The final rulings are applied below.

## Numbered integration and acceptance jobs

1. **Baseline/ownership/epoch fence.** Verify literal coder sets: A4+B9+C32=45,
   empty pairwise intersections, no coder edits outside manifests. Three coders
   can start together only after prerequisites; one round suffices. Review
   C4458/C4459, includes/declarations, observation/API agreements and preceding
   wave diffs. Integrate explicit-path hunks serially, never overwrite whole
   older files. Audit V6 fresh1.0 before Load/entity initialization against engine
   **0x200f5bb4..0x200f5bc4**, frozen revisit and saved load **0x200975f0**.
   Check committed `ElysiumArenaStage.cpp::Stage`, session new-game/load functions
   by name read-only; V6 owns those writers, no blanket clock rewrite here.
   Record the apply/pre-entity fence stamps and epoch provenance for all records.

2. **Observation compile first.** Bring lane1 Listen accessors/taps, lane2 legacy
   bus Emit/Refresh/Evict/Retire observer, lane3 trace/schema/watch/diagnostic
   hunks, preserving old sound behavior. Keep correction-dependent fixture APIs
   and JSON inventory out of this first binary; no gameplay legacy toggle.
   Exact observation owed files/functions:
   - `S/ElysiumNpcThink.cpp::NPCThink/Think19NormalSet2`, **0x10292e8e /
     0x102934a9 / 0x1029357c**: disabled, normal/AI due, reduced/full and all
     last/next stamps, retaining every return/writer.
   - `S/ElysiumNpcBaseConditions2.cpp::GatherConditions`, **0x1026ecce /
     0x1026ecf1 / 0x1026ee04**: state/PVS/spawnflag/enable gates and actual
     Conditions19PerformSensing entry; no fake Listen on a refused gather.
   - Runner observer bridge uses real identity/revision, insert/duration/expiry,
     lastListenAtInsert and removal reason (legacy expiry/age/pressure included).
     Scoped callback lifetime on every exit; observation draws/advances nothing.
   Here `S=Source/ElysiumUE/Private/Substrate`, `T=.../Private/Tests`.
   Build `uv run elysium build --arm` to completion before any test/arena.
   **Six builds total across the wave, about two minutes each**, including
   correction/generated/fix builds; log wall time and first error. If exhausted
   with failure, report exact unfinished compile work; no old-binary acceptance.

3. **Default then first diagnostic.** On observation binary run
   `uv run elysium test`, then `uv run elysium arena hear_world_diagnostic` first.
   **V10.1**, `ElysiumArenaScenarioRunner.cpp::FireDueActions` and
   `S/ElysiumEntityWorld.cpp::Tick/RunThinks/ServiceEvents`, retail **0x1011abc0 /
   0x1003bdd0 / 0x100cfac0**: P7 verifies think-before-queue and same frame
   curtime, superseding coarse-order uncertainty. Record both real donors
   hear_world_investigate and interest_mode_never in **three independent boot
   orders each**: solo, after control_sequence, after the other donor (reverse
   pair for interest). Keep passes as well as misses; no forced sensing, seed
   change, timestamp offset or fixture on diagnostic/donor paths.

4. **Disposition evidence before correction.** For each donor identity capture
   actual insertion/old-listen stamps, backend D/expiry, Listen entry/exit,
   next eligible full-gather stamp, state/PVS/LOS/enable/due gates, candidate
   admission/refusal, removal time/reason, delayed deadline and promotion/output/
   program chain, **0x1030f7b0 / 0x1030f940 / 0x1026a5e0 / 0x102b9060**.
   Delayed promotion stamp is not first Listen. Equality is retail rejection;
   strictly fresh plus late Listen implicates old expiry only if actual rejection
   or removal proves it. Classify every miss/rejection; a pass cannot prove a
   historical cause. If neither freshness nor observed removal fits, recover
   that measured chain before any runtime correction.

5. **Only proved phase correction.** Lane1 retains startTime>lastListen exactly
   (**0x1030f7b0**). If a donor expects hearing at equal insertion/old-listen,
   prove its record-phase error against P7, retain original trace, and let lane3
   stage a genuinely later real delivery after observed Listen; retain separate
   sound_freshness_equal negative and sound_freshness_new positive. Any finer
   runtime discrepancy needs its exact retail chain first. No epsilon, invented
   timestamp, seed change, duration bump or blanket IO reorder. Diagnostic pass
   is observation coverage only. N4 final closure uses job10, even if historical
   intermittent does not reproduce; any unexplained miss keeps it open.

6. **Integrate corrections/API consumers.** Bring lane1 strict active Listen/
   transient-senses work, lane2 allocator/cleanup/player/capture work and lane3
   helper/records. Retail **0x101baf80 / 0x101bab50 / 0x101ba9d0 / 0x101ba890 /
   0x1016b480 / 0x1016be10**; P1/P3/P5/P6. Exact owed lines:
   - `S/ElysiumEntityWorld.cpp::RefreshGameSound` and public
     `ElysiumEntityWorld.h` declaration: in-place type4, explicit radius0,
     no finite0.2/normal-silence Retire; early FL_NOTARGET volume-only arm;
     reserve/bind configured client pool once, not per Emit/refresh.
   - `::Tick` at existing post-move `PostThinkAnimation` call: actual frame delta,
     producer after animation/UpdateCharacter, before RunThinks. Raw Jump already
     comes from GetPlayerButtons; do not rewrite PlayerController or heartbeat.
   - `T/ElysiumFootstepSenseTests.cpp` / `ElysiumFootstepSeamTests.cpp`: only exact
     named obsolete finite/cursor/fractional-decay assertions reported by coders.
     Preserve other tests; no TestServices.h/WorldServices.h edit.
   - `S/ElysiumNpc.h` only if const FullInvestigate/AlertLevel probe requires it;
     existing public words suffice at planner snapshot; preserve corpse changes.
   - `docs/vtmb/npc-ai/programs.md` INVESTIGATE-family: maker replay
     **0x1034b7b0**, selector/ladder **0x102b9060 / 0x102b8980**; full flag0 is
     separate from mode4, first PLAYER rung0x4c, preserve later sight/enemy leg.

7. **V10.4 soundent dispatch/placement gate, before boundary acceptance.**
   In `S/ElysiumEntityWorld.cpp::Load/Activate/RunThinks` and public declarations,
   finish bounded P7 native registration/fresh/restore mapping from **0x1023c020 /
   0x100f9fc0 / 0x100f7060 / 0x1003bdd0 / 0x101a2e40**. Read any remaining
   soundent factory/world-Precache/apply edge before coding its placement.
   Document which soundent survives, stable identity, insertion position and
   predecessor/successor in native and port sequences; no duplicate on restore.
   Schedule lane2 cleanup as this entity's **due callback in existing RunThinks**:
   initial now+1 **0x101ba6f0**, recurring now+0.3, finite expiry+4<=now with -1
   excluded **0x101ba890**. No independent globally privileged pre-Listen timer,
   arbitrary first/last position, emission-triggered cleanup or catch-up loop.
   Preserve RunThinks->ServiceEvents. If a lightweight callback registration is
   required, keep it in these exact owned world files; no general scheduler lane.
   **Proof:** sound_lifetime_prune retains both explicit callback-order controls;
   sound_cleanup_coincident uses production world dispatch at equality, records
   actual order/candidate disposition and fresh/restored placement. Fixture-only
   order never closes the live wire. Supply oracle mapping lines to lane1 senses.md.

8. **V10.3 bounded V6 common-applier handoff.** Exact additional integrator files:
   `S/ElysiumEntityWorldPersistence.cpp::Freeze/ApplySnapshot`, public
   `ElysiumSaveTypes.h` snapshot state and
   `Source/ElysiumUE/Private/Session/ElysiumSaveArchive.cpp` codec operators.
   Reuse committed V6 shared construction/identity/fixup/apply fences and typed
   time policy. P6 audit/retail **0x101a2e40 / 0x101baf80 / 0x1016b480 /
   0x1027cc10 (asm0x1027ccda) / 0x1027c160 / 0x1027bf50**: save all64 rows,
   free/active/list links, owner EHANDLE, player target INT/current CSound volume,
   client binding and audited callback/due state; fix owners after construction.
   Apply saved heads/links without fresh reserve/reordering. Use audited TIME
   bases/sentinel policy for finite start/expiry; -1 semantics must survive.
   Restore genuinely transient senses via lane1 construction/restore, lastListen0,
   never saved-listen TIME. Preserve landed saved memory/conditions; no archive
   compatibility/migration needed, saves are disposable. Extend schema only where
   required by new shape, within these exact files; no unrelated persistence work.
   **Proof:** sound_save_finite and sound_save_reserved execute codec/storage/common
   applier and observe pool/owner/volume/due/placement plus lastListen0 **before
   first Listen/PostThink**, then real hearing/decay/cleanup continuation. Direct
   member-copy fixtures cannot close. Restore state is this wave's obligation,
   not a new filed regression handed backward to V6.

9. **Compile final, default, epoch/content admission.** Build correction binary
   within six-build total, then `uv run elysium test`. Retain observation evidence
   and final binary provenance. V10-3's records probe deployed tutorial child
   full_investigate0/initial alert0 and actual WORLD duration/receipt,
   **0x1034b7b0 / 0x101ad470 / 0x101bac90 / 0x102b8980**. V10.5 refuses a
   prospective maker/pipeline/re-bake lane: no child-key rewrite or fake success.
   If measurement proves required payload missing/wrong, classify exact data/
   asset/receipt defect and repair only that acceptance prerequisite; retain
   admission evidence. Re-measure all zero-tuned records at V6 epoch alone and
   after another, with per-record proof for changed staging; original predicates,
   shared draw order and bounds remain. Unexplained reds block closure.

10. **Named records and N4 final gate.** Run every README contract by exact name:

    ```text
    uv run elysium arena hear_world_diagnostic hear_world_investigate interest_mode_never hear_world_out_of_range sound_lifetime_grace sound_lifetime_long sound_lifetime_prune sound_freshness_equal sound_freshness_new sound_listen_empty_mask sound_listen_order sound_player_reserved sound_player_modes sound_player_decay sound_player_gates footstep_events_no_ai sound_pool_pressure sound_pool_reuse sound_pool_reserved_survival sound_save_finite sound_save_reserved sound_cleanup_coincident map_tutorial_sneak_past map_tutorial_hearing_walk_radius map_tutorial_hearing_sneak_radius
    ```

    Retain result/first_unmet/trace, not just exit code. **21 new records**:
    19 Green Room records each alone and after control_sequence in same boot;
    two map controls alone and launcher-after-control on separate fresh map
    boots, no shares_map. Existing N4 donors each need the three independent
    boot orders in job3 on **final binary/V6 epoch**; run tutorial again after
    its controls. JSON-only corrections need no build, but final evidence must
    describe final records. Ordered expectations follow actual output-before-
    cond+ emission; never widen deadlines. Map controls observe real post-move
    volume180/240, type4/ear distance, settle old volume0 before crouched walk,
    >=1.5s reaction tail, no premature SEE_PLAYER/OnFoundPlayer; preserve later
    deliberate sight leg. Correct only disproved geometry, not hearing thresholds.
    **N4 closure** requires verified contract, final actual fresh donor stimulus
    reaching delayed condition/output/program, classified dispositions in all
    three orders, green lifetime/freshness/order controls and unchanged
    hear_world_out_of_range. Diagnostic green/nonreproduction is insufficient.
    Remove known_red only with that evidence; historical cause may remain
    explicitly unproved, never assert expiry caused it. Any unexplained miss
    keeps N4 open. A record this wave writes/breaks belongs to this wave.

11. **Arm then kernel.** After final default/named records run
    `uv run elysium test arm` and
    `uv run pytest pipeline/tests/test_arena_suite.py -q -n 0`.
    ArenaSoundFixtures.Schema and actual NpcSenses/GameSound/PlayerHearing arm
    registrations supplement arena, not replace it. Then
    `uv run elysium research kernel --check`; if stale, update exact verdicts,
    regenerate/check again. Integrator alone owns
    `research/tooling/ghidra/driver/kernel_verdicts.tsv` and generated ledger.
    No invented out-of-closure row; generated Slots never hand-edited. Any
    compile-impacting regeneration/fix returns to build/affected validation
    within six builds and renews final evidence. No new virtual body is expected.

12. **Final full arena once after last change:** `uv run elysium arena`.
    Compare each moved verdict with actual prerequisite baseline: before/after,
    first_unmet/trace, retail reason and remaining owner. Preserve unrelated
    combat/corpse/interrupt records; rollcall_vzombie is the named historical
    fail, not a place to park regressions. A later change invalidates acceptance
    and requires affected/default/arm/kernel plus renewed final full run.

13. **Close rulings and filed seams.** Update triage N4/Q-V3bf1, spec V10/V12
    and TRACKER only for accepted boundaries. Record verified behavior vs
    historical-unproved cause separately. Explicit close lines:
    - V10.1/N4: donor timeline/three orders/controls and known_red disposition;
      equality alone remains retail. Unclassified rejection keeps N4 open.
    - V10.2: accepted64/free-list/refusal/reuse/reserved survival; later
      `R1SoundProducer`/`VSound` census, other producers, memory families and
      investigation programs -> **0002/R1**, unproduced live witnesses absent.
    - V10.3: accepted raw Jump/per-frame/int producer and production save handoff;
      `PlayerLayer0LandingState` -> **0015 player layer0**, landing live record
      absent. `PlayerPostThinkGameOver/Locked/Observer` -> **player story**,
      corresponding live records absent; `PlayerSoundLocomotion` -> **player
      embodiment** if unavailable. Bind already landed fields by name; truly
      absent inputs answer nothing with comments naming retail fields. Fixtures
      prove consumer arms, never missing live publishers or whole PostThink.
    - V10.4: native fresh/restore mapping and actual coincident dispatch proof;
      unread mapping or unexplained boundary verdict blocks that acceptance.
    - V10.5: prospective content work refused; deployed child0/alert0, first
      PLAYER rung0x4c and actual WORLD D/receipt evidence; any concretely repaired
      required payload listed narrowly. No broad import/maker/key changes.
    - Standing Clock: committed pre-entity1.0/frozen/saved bases and per-record
      epoch evidence; unchanged predicates/draw order/bounds. No new modernization.
    Put recovered oracle facts in lane-owned senses.md/footsteps.md and owned
    programs.md, not only triage. Do not tick all R1/player animation/persistence.

14. **One implementation integration commit**, explicit paths only: 45 coder
    paths plus actual enumerated owed/doc/ledger paths and six planner files.
    Never git add -A/dot, never push. Include baseline/final totals, moved
    verdicts, six-build wall-time ledger, observation/correction evidence,
    assertion migrations, historical cause unproved and named later seams.
    Suggested subject: `fix(npc): V10 sound pool/lifetime and V12 player stimulus`.
    No report*.md, no clean/reset/stash/checkout/worktree/delete. Do not commit
    a claimed green wave with unexplained records it wrote/broke.

Final report under350 words: files/addresses, build/default/arm/kernel/arena
results, observed dispositions/record corrections, filed owners/absent records.
Wait for actual process completion; spawned work is never reported as success.
