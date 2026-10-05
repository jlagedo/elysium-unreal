# V5b checker adjudication — final against V4c d0f79574

Planner, 2026-10-05. Inputs: sol-V5b-plan-check/packet.md and sol-ghidra/packet.md §1.
This audit read AGENTS.md first; source/retail reads used the main checkout, edits only the six
authorized files in E:/elysium-work/worktrees/coord. No source edit, build, test, arena or commit.
Main HEAD and coord both identify d0f79574; main has unrelated dirty AGENTS.md / Session/ElysiumRng.cpp.
Do not infer installed binary provenance from either working tree.

Evidence: git show -s64895278/d0f79574; bounded source reads and path logs; research kernel --check
(all seven gates match); vtmb_where before oracle searches; vtmb_code/asm returned **vampire.dll**
for bare-address queries. The attempted vampire.dll::address spelling returned no match; retry
used the bare address and verified the returned module. Direct PE tool read the padding and thunk.
No Ghidra project was opened/saved.

Scope incident: the PE helper was discovered to write its formatted output after reading.
It wrote scratch files `E:/elysium-work/codex/sol-ghidra/pe-asm-1026a233.txt` and
`E:/elysium-work/codex/sol-ghidra/pe-asm-100123f5.txt`, outside the six-file allowance.
The kernel check also emitted its normal out-of-repo `20261005T075852.313528Z-research`
JSON/log pair. These outputs are left intact under the no-delete rule; the helper was not used
again after its side effect was discovered. No repository file outside the six docs was written.

Each numbered item maps one checker bullet, in its original order.

## 1. Files, functions and superseded instructions

1. **Inventory — confirmed existence/sites, refuted unchanged-file subclaims at V4c.**
   All17 existing files are present; InterruptMaskTests.cpp is new. Named gate/range/reset/finish/
   capability/cache functions remain and new weapon reload bodies are absent.
   The checker did not name its diff base. Against V5a a6bd4add→d0f79574, Conditions10.cpp and
   Conditions10Tests changed (V4c equal-bound RNG), RunTask tests and Species tests changed too.
   git log shows V4c on Motor, Motor tests and current Weapon files, with V4o beneath it;
   “Motor last under V11” no longer describes final V4c. Land: README§0/§3 and lane file sections.
   Keep current V4c bodies/fixtures rather than applying old-source assumptions.

2. **Live NextAttackTime — confirmed.**
   vampire.dll slot322 assembly `0x102551ff` reads owner+0x1564, `0x1025520b..13` compares
   curtime before `0x10255219` slot323; equality is admitted.
   Port Damage3.cpp::PlayerDefenderBlockReaction writes at0x1029fd6a/6f; Damage.cpp::PlayerAttackerBlockedReaction (`0x1029fdb0`) also writes;
   WeaponClasses.cpp::BeginMeleeSwing writes Max at0x103ea2eb.
   Land: README job5, lane2 job4, integrator compile job2's ConditionsBodies.inl comment.
   No constant-zero owner deadline.

3. **Template accessor — confirmed.**
   Npc.cpp::ApplyResolvedTemplate stores FootstepTemplate and its footstep path passes FootstepTemplate.Get();
   Npc.h already declares it. Loader `0x101d4394..0x101d43c8` proves Min8,
   truncated Min as Max default. Land: README job1, lane1 job1, no new resolver/Npc edit.

4. **Hint sentinel — confirmed.**
   Combat10_2.cpp::RangedWeaponPrePass compares0; Npc.cpp hint acquire/clear and BaseMotor
   null-hint path use INDEX_NONE. Retail `0x102b8692` tests pointer null.
   Land: README job2, lane1 job2 + valid-index0 arm test.

5. **Per-set loop relocation — confirmed.**
   WeaponClasses.cpp::ShotFromAnimEvent stages sets; ::CommitQueuedAttack traces each set
   before missing/dead-victim refusal and before the damage loop.
   ::CommitArrivesFromAnimEvent's V4c NPC arm permits only event commit.
   Retail FireBullets `0x10268919` decrements first per call, and Shot calls once per set.
   Land: README job3, lane2 job6/tests. The decrement has not already landed.
   Use function/loop names, not old2637/1557 line hints.

6. **Survival staging — confirmed; invalidation inference supported.**
   range_bands.json and ranged_sustained_fire.json contain events_player row and
   MakePlayerUnkillable at0.05. V4o commit records7×15 player death under real event shots.
   EventClasses.cpp::InputMakePlayerUnkillable is the current port hook.
   Land: integrator common staging explicitly copied into BOTH new JSON records.
   Long witness with a required enemy can otherwise lose that enemy; no planner run was made.

7. **Four never clauses and stale notes — confirmed.**
   ranged_sustained_fire.json never array is NO_PRIMARY_AMMO/task_reload/BEHIND_ENEMY/death.
   Its notes still deny the landed NPC weapon event path despite its V4o survival sentence.
   Port WeaponClasses.cpp::ShotFromAnimEvent / CommitQueuedAttack are implemented
   (`0x10238160 →0x102387b0`).
   Land: README§4, integrator existing-record correction; preserve all four clauses.

8. **Single-round retained state — confirmed; blanket V6-none refuted.**
   `0x1025506f..77` requires owner player+0xa8; an NPC jumps to return0x1025523c without
   clearing+0x898. Admitted bulk slot323 clears it; future bulk deadline also leaves it set.
   Land: README reload/acceptance, lane2 jobs4/8, integrator V6 handoff.
   Listing settles runtime retention; actual save implementation belongs to V6.

9. **“No build needed / tree equals binary” — inference confirmed as a planning hazard,
   installed revision cannot be settled here.**
   git status exposes unrelated runtime edits and no binary revision proof was supplied.
   V4c commit is the recorded baseline, not proof of installed binary provenance.
   Land: README baseline and integrator opening/compile-first flow. Prebuild record run removed.

## 2. Lane ownership

10. **6+6+6 disjointness — confirmed, scope amended.**
    README and each lane re-list all18 paths; A∩B=A∩C=B∩C=∅, union18.
    Lane1 uses existing FootstepTemplate.Get and lane2's new declaration; no path overlap.
    Additional cache common-path edits are owed to integrator (Schedule.cpp/h, Npc.cpp,
    Guard1.cpp, existing ScheduleTests); they do not enter a coder lane.
    Land: README§3, all lane scopes, integrator compile jobs.

11. **V4c prerequisite and declaration comment — confirmed.**
    V4o/V4c path logs/diffs overlap Weapon/Motor/tests. They cannot run concurrently as disjoint
    writers with these lanes. ConditionsBodies.inl's NextAttackTime comment still says no writer.
    Land: README§0/§3, lane2 job4, integrator compile job2 (addresses in item2).

## 3. Retail-address mismatches

12. **0x102ae920 label — confirmed.**
    vtmb_where and vtmb_code identify CAI_BaseNPCTroika::PreSelectSchedule, slot437;
    oracle heading uses GetSchedule. Land: lane3 opening and integrator oracle heading job.

13. **Lookup limitation — confirmed, obsolete padding ambiguity refuted.**
    Interior addresses can have no ledger name; a where miss cannot prove non-code or ownership.
    No further concrete mismatch from this checker was established. Shared-body aliases were
    not renamed on that basis. We validated operative cache/reload/loader addresses from asm;
    direct PE proves the former unindexed interval is padding. Land: README interrupt evidence,
    lane3 job2 and this packet; no instruction to defer the padding remains.

## 4. Explicit undecided statements

14. **All former maintenance “not read” claims — settled; do not retain the comment-only job.**
    vtmb_asm0x1026a0f0 reads through RET0x1026a232. Direct
    `python E:/elysium-work/codex/sol-ghidra/pe.py asm 1026a233 110` yields exactly
    13 NOP +96 INT3 before the next entry0x1026a2a0.
    Both cited0x1026a267/274 are INT3. No retail function/body in that gap remains to recover.
    Land: README interrupt job7, lane3 behavior job2, integrator owed common-mask edits.
    The descriptive name of slot411 is unnecessary: slot list across NPC hierarchy and
    asm0x10280fd0 establish its empty behavior completely.

15. **“They do not read the seam” — confirmed as a future dependency, false today for lane1.**
    Current Combat10_2.cpp::RangedWeaponPrePass reads ActiveWeaponCapabilityWord.
    Lane1 job2 replaces it with SelectActiveWeaponWord. Current WeaponClasses.cpp trace loop
    does not read that seam and lane2 adds only the count write (`0x10268919`).
    Land: README dependency proof and lane1/2/3 dependency sections.
    The old proposal to revert lane3 if timings move is removed: wave regressions need retail
    resolution, not dropping a confirmed capability/cache repair. No “to be confirmed”
    sentence remains in these final briefs.

## 5. Additional sol-ghidra §1 defect and final-code corrections

16. **Cache tail defect — confirmed by a fresh listing/PE read; numbered lane3 job2.**
    `0x1026a211` slot453; `0x1026a21b` slot411; `0x1026a221 PUSH0x75` then
    `0x1026a225 CALL0x100123f5`. Corpus and direct PE show thunk JMP0x10269eb0;
    asm0x10269f02 ORs the positive interrupt mask, not current conditions.
    Remove NpcMaintain.cpp's RemoveIgnoredConditions / current freeze clear, restore inverse
    copy and citation order. CacheTail arm pins0x1026a211/21b/225 and push0x1026a221.
    Guard1 matters because0x1037cdf0 REPLACES Troika453, calling empty0x10280fb0.
    **Correction to the implication that only Troika currently has freeze:** HEAD Guard1.cpp
    already folds it into its override too. Integrator relocates both folded insertions into
    the common mask path; current-condition preservation is still broken for either species.
    **Correction to no-inverse-column claim:** ScheduleText.h already holds InvertedInterrupts,
    Tick evaluates inverse absence; maintenance's reset is a divergence. Land: README,
    lane3 job2/3 and integrator compile owed lines/oracle updates.

17. **Additional stale arm prefix / endpoint — confirmed from code/data.**
    ElysiumWeaponTests uses Elysium.Arm.Weapons. (plural), not Weapon.
    range_bands's0xef program ends with task_wait_attack_time1; task_step_back completion
    is earlier and cannot establish full program completion.
    Land: lane2 tests/README acceptance and integrator runs_on final-task expectation.
    V4c's ResetFakeReloadCount already avoids a random draw at equal bounds; lane1 preserves it.

## Ownership and limits

No source/arena changes are made by this planner. All requested undecided maintenance statements
are settled. Listing cannot identify the installed binary revision, choose future trace coordinates,
prove imported ACT_RELOAD_FAST availability, or measure new-record timing; the integrator's compiled
tree and JSON trace loops establish those. No general outstanding retail body is deferred.
Presence cast/status producer belongs to spec0006; retained single-round weapon persistence toV6.
An actual imported reload-activity/event miss needs the asset/pipeline owner and possibly a re-bake;
none is demonstrated or requested by this planning pass. No modernization is adopted.
