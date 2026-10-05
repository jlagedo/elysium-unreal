# Brief — V5b integrator and V5 close

Final against V4c **d0f79574**, after V4o **64895278**. Read AGENTS.md, README, all three
coder briefs/reports, packets-V5b-check.md, Arena/README.md, spec.md standing rules/bug protocol
and stories/v1/triage.md's red4/J12 entries. You own Arena edits and every exact owed line.

**Recorded baseline:** arena **113 pass / 1 fail rollcall_vzombie / 16 expected-fail /
2 unexpected-pass**; default **169 / 0**; arm **1,624 / 0**.
These are V4c's committed results. Identify the actual integration tree/build in your evidence;
an existing binary has no proven revision. Do not run a new record against an assumed baseline
binary or require a prebuild arena pass. The wave begins with compilation.

## 1. Compile pass first

1. Coordinator names the coder worktrees and integration branch. Bring in all three lane diffs
   onto the V4c prerequisite. Prove the six+six+six lists in README are disjoint; V4o/V4c were
   prior overlapping writers, not concurrent lanes. Audit shadowed locals/parameters/members
   (C4458/C4459), includes and shared declaration spellings.
   Template range +pre-pass reroll +per-set decrement land together.
   Keep V4c's live NextAttackTime writers, event-only NPC guard, shared NpcSchedule animation
   stream, equal-bound random fast path, team/dead-enemy rules, NPCInit state and zero stage clock.

2. Apply the coders' exact **owed lines** before the first build. Expected jobs:
   - **ConditionsBodies.inl**, NextAttackTime declaration comment: remove “nothing writes”;
     identify Damage3.cpp::PlayerDefenderBlockReaction `0x1029fd6a / 0x1029fd6f`,
     Damage.cpp::PlayerAttackerBlockedReaction, slot319 `0x1029fdb0` and WeaponClasses.cpp::BeginMeleeSwing `0x103ea2eb`.
     Slot322 `0x102551ff..0x10255219` reads this live deadline.
   - **Schedule.cpp::EffectiveInterrupts**: installed program positive local mask →virtual453
     `0x1026a211` →empty411 `0x1026a21b / 0x10280fd0` →unconditional freeze
     `0x1026a221 / 0x1026a225 →0x10269eb0 / 0x10269f02`; no program returns empty.
     Lane3's maintenance cache copies BOTH masks before its one virtual call; make its cached
     positive mask agree with this common effective result. Do not invoke virtual453 twice
     during one cache operation. Inverted bits remain separate and never feed HasInterruptCondition.
   - **Npc.cpp::BuildScheduleTestBits** and **NpcGuard1.cpp::BuildScheduleTestBits**:
     remove the folded post-cache freeze insertion; the new common path owns it.
     Guard1 retail `0x1037cdf0` calls empty base, not Troika `0x102ad140`.
     **Tests/ElysiumNpcKernelScheduleTests.cpp**, existing direct-slot overlay assertion:
     do not demand freeze from slot453 itself; test it through the completed mask/cache.
   - **Schedule.h**, MaskHasCondition contract: replace the outdated “normal mask only”
     statement; ScheduleText.h already stores InvertedInterrupts, Tick already evaluates it.
     Preserve the existing positive-only HasInterruptCondition contract `0x10269d30`.
   - Lane3's named reader comments, and any lane2 exact unowned declaration/test edits.
     Do not invent broader changes from a report.
   - **docs/vtmb/npc-ai/conditions-and-states.md**: correct the `0x102ae920` heading/identity
     to **CAI_BaseNPCTroika::PreSelectSchedule**; record fake-reload range/count and corrected
     cache order, padding, inverse copy and Guard1 scope.
     **docs/vtmb/npc-ai/schedule-kernel.md**: correct its empty-inverse cache statement.
     **docs/vtmb/combat-and-damage.md**: record the live slot322 deadline and retained
     single-round NPC flag, bulk outputs and no-owner arm.
   - Generated **`*Slots.cpp` are never hand-edited**. A needed hand body belongs in matching
     `*SlotBodies.cpp`. Integrator adds/updates the exact
     `research/tooling/ghidra/driver/kernel_verdicts.tsv` row, then
     `uv run elysium research kernel` regenerates. Update existing cache/slot453 target/evidence
     rows for the repaired ownership, and reload rows only where the table actually has coverage;
     do not invent an out-of-closure kernel row. No new slot411 body is required.

3. **Compile until it compiles:** `uv run elysium build --arm`, fixing compile/link/integration
   breaks. Allow **up to six builds, about two minutes each**; record actual wall times.
   At the sixth failed build stop with concrete errors and uncompleted work. Do not substitute
   an old binary or silently drop lane3. If a later runtime correction requires recompilation,
   rebuild within the same six-build wave budget and rerun affected acceptance work.

## 2. Default tests, then records

After a successful compile pass, run **`uv run elysium test`**. Resolve wave-owned failures
against retail, then proceed to the JSON record work below. A record is data: edit/loop freely;
JSON corrections alone do not require a rebuild. No arbitrary one-run limit on these iterations.

Write these two new files (paths relative to repository root):

- `Arena/scenarios/combat/ranged_step_back_holds.json`
- `Arena/scenarios/combat/ranged_fake_reload.json`

### Common staging, required in BOTH new records

Copy the donor's complete cast, player seat, seed **and survival row/script**, not just cast/seat:

```json
"rows": [
  {"classname": "events_player", "name": "arena_player_events", "at": "start"}
],
"script": [
  {"t": 0.05, "do": "fire", "target": "arena_player_events", "input": "MakePlayerUnkillable"}
]
```

Merge each record's later script actions after this first action. Donors range_bands and
ranged_sustained_fire already use these. V4o's real event bullets can kill a 100-health player
on the seventh 15-point set. This is retail's existing unkillable input
(EventClasses.cpp::InputMakePlayerUnkillable), not a damage-semantic change.
V4o resets its session latch; V4c resets arena clock BEFORE Load and seeds all streams at stage
creation. Reuse those mechanisms. Retain initial NONE→IDLE trace if state expectations are added.

### ranged_step_back_holds — red4

About: 0xef's authored six interrupts and slot453 add none of NOT_FACING_ATTACK0x61,
WEAPON_THROUGH_WALL0x3c or TOO_CLOSE_FOR_RANGED0x08; HasInterruptCondition `0x10269d30`
over the correctly completed cache `0x1026a0f0`. The old99s were TASK_RANGE_ATTACK1 not finishing.

Staging: range_bands donor, arena_shooter npc_VHumanCombatant, TutorialThug,
regular_cop, item_w_thirtyeight, hint_groups2, player cover_seat, seed1, duration30.
Copy survival staging above. At t6 player_teleport to [587,683,0], face far_ne
(the existing96cm front position). After steps_back +delay0.3, put player behind the shooter's
current facing and MORE THAN100 Source units (254cm) away; choose the arena position from the
trace/places and record exact coordinates/distance in notes. This avoids slot365
`0x1024f670`'s earlier d<100 arm winning before dot<0.5.
After reselects +delay0.5, `fire arena_shooter TakeDamage 1` as input_takedamage does.

Expect in order, on arena_shooter:

1. steps_back: schedule `^SCHED_TROIKA_STEP_BACK_RANGE_ATTACK1 \(`, regex, by12.
2. not_facing: cond+ NOT_FACING_ATTACK (0x61), within2.
3. runs_on: taskdone of the program's LAST task `^task_wait_attack_time1$`, regex, within6.
   A task_step_back completion alone is not proof that 0xef reached its end.
4. reselects: next ranged program, within3.
5. hurt_breaks: break LIGHT_DAMAGE, within1.5.

Never break on0x61/0x3c/0x08 during the 0xef hold witness, and never shooter death.
Pin the next ranged program with a LIGHT_DAMAGE interrupt before staging the hurt; 0xf0 lists
only ENEMY_DEAD. One-point positive damage raises base LIGHT_DAMAGE
(`0x10266630`, comparison `0x10266634` against0); no arbitrary amount escalation.
If the donor seed selects0xf0 (dodge `0x102b7f40`), choose and document a deliberate0xef witness
seed from the trace, not a broader schedule regex or a suite-order RNG fix.
Measure task deadlines with V4c's shared stream; each final record uses one documented seed.

### ranged_fake_reload — whole fake reload

About: template loader `0x101d3f10` Min8 default, MaxMin; TutorialThug authors6.
`0x102c54c0` rerolls; `0x10268919` decrements once per set. Below1, pre-pass
`0x102b8620` chooses0xc4 without hint,0xc6 with hint; no-cover0xc4 fails to0xc5,
FACE_ENEMY then PLAY_SEQUENCE ACT_RELOAD_FAST. Fake reload spends no clip and has no TASK_RELOAD.

Staging: ranged_sustained_fire donor, complete cast/player/seed plus survival row/script;
duration60. Expect in order:

1. engages START_COMBAT by3.
2. shot_1..shot_6: animevent `^3031( |$)`, regex, within8 each.
3. fake_reload: schedule `\(0xc4\)$`, regex, within6.
4. reload_in_place: schedule `\(0xc5\)$`, regex, within8
   (hint_groups2 provides no usable cover).
5. reload_clip: task `task_play_sequence (`, within2.
6. reload_done: taskdone `^task_play_sequence$`, regex, within6.
7. resumes: animevent `^3031( |$)`, regex, within10.

Never: cond+ NO_PRIMARY_AMMO(0x40), task_reload, real reload schedules0xc2/0xc3, shooter death.
End probe: shooter alive. Notes distinguish events from bullet sets: 3031 can emit zero/multiple
sets, so exact six-set accounting is pinned by Elysium.Arm.Weapons.FakeReloadCountPerSet;
the ordered labels alone cannot exclude an extra3031. Read LastSets/commit trace if labels drift.

Do not pre-label either new record known_red. A record this wave writes or breaks is the wave's.
No known_red without a cited retail reason AND an identified later story which actually owns it.
A missing reload sequence gets exact class/model/sequence and pipeline/activity-ladder evidence;
do not invent completion or a re-bake. Escalate an asset owner if required for acceptance.

### Existing records and named runs

Correct `Arena/scenarios/combat/ranged_sustained_fire.json`:
replace “no reload is ever scheduled” with “no REAL reload arises from clip spend; after six
sets an NPC fake-reloads” (`0x102b8620 / 0x102c54c0 / 0x10268919`).
Replace its obsolete notes “world-tick poll / no NPC shot yet” with the landed
ShotFromAnimEvent→CommitQueuedAttack route (`0x10238160 →0x102387b0`), retaining survival staging.
Set shot_7's window from the measured0xc4→0xc5→resume trace, stating the times and retail cause.
Preserve **all four never clauses**: NO_PRIMARY_AMMO, task_reload, BEHIND_ENEMY, shooter death.
Any further window change needs its own trace/retail cause; never loosen to conceal a regression.

Loop these by name until wave-owned behavior agrees with retail:
`uv run elysium arena ranged_fake_reload ranged_step_back_holds ranged_sustained_fire ranged_open_fire range_bands cover cover_armed cover_move_shoot ranged_friend_in_line_of_fire control_sequence`.
Run EACH NEW RECORD both **alone** and **after another record** in the same boot, e.g.
`uv run elysium arena ranged_fake_reload`, then
`uv run elysium arena control_sequence ranged_fake_reload`; likewise for ranged_step_back_holds.
Record final verdict/first_unmet/trace evidence, not just a successful command.
Do not revert the capability/cache repair to preserve an old assumption.

## 3. Close in this exact order

1. After default tests and named-record loops, **`uv run elysium test arm`**.
2. **`uv run elysium research kernel --check`**. If stale, amend owed verdicts and regenerate
   via `uv run elysium research kernel`, then check again. Generated compile-impacting changes
   return to compile/affected validation; final arm and gate must describe the final sources.
3. **Full arena ONCE**, `uv run elysium arena`, after the last code/build/record change.
   Every moved verdict gets record/before/after/retail reason/later-owner evidence.
   The baseline red rollcall_vzombie remains H11; do not transfer new wave regressions to it.
   Any subsequent change invalidates that final acceptance run and requires renewed validation.
4. Update stories/v1/triage.md red4 with the 0xef witness (“not a mask defect; texts omit the
   conditions; old99s were TASK_RANGE_ATTACK1”), and separately record this wave's true cache
   repair. Amend J12: NPC fake reload after template sets; firing still cannot raise0x40.
   Tick spec.md V5 with V5a/V5a-3/V5b records and arm-only real reload proof.
   File Presence with0006 and retained single-round bInReload persistence withV6.
   Close only with actual acceptance; no blanket “V6 owes nothing.”
5. **One commit**, on coordinator's integration branch, staged by **explicit path** only;
   never git add -A / ., never push. The message contains the verdict table, baseline/final
   tier totals, build wall times, changed tests and their retail cause, and named seams/owners.
   Suggested subject: `fix(npc): V5b reload and combat interrupt cache`.
   **No file named report*.md**; evidence goes in commit message and final report.

Report under350 words: files/addresses, compilation wall times, default/arm totals, record
before/after table, exact remaining owner work. Real reload is arm-only; Presence0006 and retained
single-round stateV6 are explicit. No padding mystery remains. Wait for actual build/run completion.

## 4. The four dangling citations (owed since the pytest scout; docs only)

`uv run pytest pipeline/tests/test_oracle_citations.py -q -n 0` fails on four citation values (five
occurrences). Fix each in the same commit, then re-run that test:

- `docs/specs/0002-npc-ai/stories/v1/triage-K.md:66` cites `lifecycle.md` § "A hidden maker" (an
  italic run-in, not an anchor): cite § "The NPC makers".
- `docs/specs/0002-npc-ai/stories/v1/triage.md:715` cites § "Judge's rulings … (continued)": cite
  § "Judge's rulings filed by the coordinator".
- `docs/specs/0002-npc-ai/stories/v4/packets-S1.md:396` cites `brief-D-ragdoll.md` § "Not in V4d":
  cite § "Not yours and rules".
- `docs/specs/0002-npc-ai/stories/v4/README.md:17` and `:757` cite § "After the spike": cite
  § "First step — one controlled frame measurement".

Confirm each target heading still exists before editing (V4d may have moved them).
