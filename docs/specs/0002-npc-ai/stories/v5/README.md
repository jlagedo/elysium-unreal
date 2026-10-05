# V5b — combat interrupts, NPC reload and the weapon capability word

Final planner brief, 2026-10-05, against **V4c `d0f79574`**, after V4o `64895278`.
Implementation closed 2026-10-05 against the owner's V4d baseline: default 169/0,
arm 1636/0, kernel 7/7; arena 116 pass / 1 existing fail / 15 expected-fail /
2 unexpected-pass of 134. Both new records pass alone and after another.
This wave follows V5a / V5a-3 and V4d, and precedes V6.
The planning checkout is `spec-0002/coord`; the coordinator names each coder's worktree and the
integration branch. The checker audit and settling reads are in `packets-V5b-check.md`.

Baseline recorded by V4c: full arena **113 pass / 1 fail (`rollcall_vzombie`) /
16 expected-fail / 2 unexpected-pass**; default **169 / 0**; arm **1,624 / 0**.
These are commit results, not a claim about the binary currently installed. No planner build,
test or arena run was performed.

## 0. Prerequisites already carried out

V4o landed overlay layers, move-and-shoot and the 3031 event shot
(`0x10238160 → 0x102387b0`); V4c landed the NPC timer-exception guard, shared animation-pick
stream, dead-enemy removal, team registry/gates, admission retaining NPCInit's state, and a fresh
zero arena clock before each stage Load. Remove those implementation jobs from V5b.
Their port sites are `FElysiumWeapon::CommitArrivesFromAnimEvent` / `ShotFromAnimEvent`
(`ElysiumWeaponClasses.cpp`), `ElysiumAnimationPick.cpp`, the memory/team substrate, and
`ElysiumArenaStage.cpp`; the commit records carry their detailed citations.

V4c is a prerequisite, not a concurrent lane: WeaponClasses, Motor and tests overlap this wave.
Keep its live deadline writers, equal-bound random fast path, event guard and clock/seed reset.
Do not restore older fixture state or infer fixed 3 s / 4 s attack deadlines.

The S12 species HandleAnimEvent walk is complete. Do not assign it again. Retain the warning
tripwire for a ranged model/sequence missing 3030..3044; a concrete asset miss encountered during
integration gets its exact class/model/sequence and owner, rather than blocking on an obsolete
generic reading assignment. The four J11 species remain on the existing on-demand line; no new
port of their task-code attack paths belongs here.

## 1. Retail contract and remaining jobs

Paths in the following table are under `Source/ElysiumUE/Private/Substrate/`. Locate by function,
not line number. Every changed runtime line must carry its retail address.

| job | retail evidence | port site and required change |
|---|---|---|
| 1: template range | `0x101d4394..0x101d43c8` within loader `0x101d3f10`; reroll `0x102c54c0` | `ElysiumNpcConditions10.cpp::CharTemplateFakeReloadRange`: read existing `FootstepTemplate.Get()`, GeneralFloat Min default 8, Max default the truncated Min. No second resolver. Keep `ResetFakeReloadCount`'s V4c equal-bound fast path. |
| 2: fake-reload selection | `0x102b8626..0x102b86b5`, call `0x102b867e`, null hint `0x102b8692` | `ElysiumNpcCombat10_2.cpp::RangedWeaponPrePass`: use `SelectActiveWeaponWord() & 0x6000`, call `ResetFakeReloadCount()`, and test `HintNode == INDEX_NONE`; return `0xc4` without hint, `0xc6` with one. |
| 3: count per set | `FireBullets 0x10268900`, first NPC operation `0x10268919` | `ElysiumWeaponClasses.cpp::CommitQueuedAttack`: decrement NPC FakeReloadCount once per set in the trace loop before `TraceShotImpact` and before victim refusal; never in the damage loop. |
| 4: real-reload gate | `0x102b86ba..0x102b87b0`, slot 280 `0x10253ab0` | `ElysiumNpcCombat10_2.cpp::ActiveWeaponWantsReload`: call lane 2's `CanReloadMagazine(0)`. Preserve clip-positive decline before this gate, reserve >=1 and SEE_ENEMY/NEW_ENEMY priority. |
| 5: reload finish | task `0x1028918d / 0x1028919d`; slot 322 `0x10255050`; slot 323 `0x102552c0` | `ElysiumNpcBaseRunTask.cpp::WeaponFinishReload` sets `bInReload`, then calls new WeaponClasses `FinishReload` / `FinishReloadBulk`. Read the owner's live NextAttackTime; finish only at <=curtime. |
| 6: capability word | slot 360: character `0x1014f930`, weapon base `0x10149e80`, subclass word; slot 513 | `ElysiumNpcBaseMotor.cpp::ActiveWeaponCapabilityWord`: no weapon →0, else `CapabilityBits(WeaponCapability(*this))`. |
| 7: maintenance cache | `0x1026a1a2..0x1026a207`, then `0x1026a211`, `0x1026a21b`, `0x1026a221 / 0x1026a225` | `ElysiumNpcMaintain.cpp::CacheInterruptConditionsForMaintenance`: copy positive and inverted program masks, virtual slot 453, empty slot 411, then add freeze to positive test bits. Remove the ignored-condition call and current freeze clear. Lane 3 plus integrator's common-mask owed lines. |
| 8: Presence text | `0x1033d940 / 0x101e3f50`, status apply `0x101e3560`, AddDiscFlag `0x1033cfb0` | `ElysiumNpcCombat10_2.cpp::RangedDisciplineGate`, WeaponClasses `PresenceDoublesAttackRate` and declaration comments: name Presence id 10, `m_iDisciplineFlags2 +0xeb4`, owner spec 0006; retain false input until that substrate exists. |

### Interrupts and the new cache defect

`HasInterruptCondition 0x10269d30` requires an installed schedule and the bit in both current
conditions (+0x5c5c) and the positive cached mask (+0x5c74); it does not read the inverse word.
`IsScheduleValid 0x10280ff0` evaluates positive intersection OR absent inverse conditions.

Retail `CacheInterruptConditions 0x1026a0f0` stamps time at `0x1026a16b`. Without a schedule it
zeros positive (`0x1026a173..0x1026a183`) and inverse (`0x1026a185..0x1026a196`) masks and returns.
With one it copies positive schedule +0x28..3c at `0x1026a1a2..0x1026a1d2`, then inverse +0..14
at `0x1026a1d8..0x1026a207`. Tail, in order:

1. `0x1026a211 CALL [EAX+0x714]`: slot 453 BuildScheduleTestBits.
2. `0x1026a21b CALL [EDX+0x66c]`: slot 411, body `0x10280fd0 RET` across the NPC hierarchy.
3. `0x1026a221 PUSH 0x75`, `0x1026a225 CALL 0x100123f5` → `0x10269eb0`;
   `0x10269f02 OR [EAX],EDX` ADDS COND_NPC_FREEZE to the positive mask.

The function returns at `0x1026a232`. Direct pinned-PE disassembly settles the disputed interval:
`0x1026a233..0x1026a23f` = 13 NOP; `0x1026a240..0x1026a29f` = 96 INT3.
`0x1026a267 / 0x1026a274` are padding, not operations. Slot 459 RemoveIgnoredConditions is
`0x1026d7f0` and is not this tail's slot 411. The condition itself must survive caching.

Troika slot 453 `0x102ad140` adds its law/comfort overlay and clears SQUAD_SEE_ENEMY under
flags2 0x40; HumanCombatant `0x10387520` adds SEE_CORPSE_FRIEND under its virtual gates.
Neither adds NOT_FACING_ATTACK, WEAPON_THROUGH_WALL, TOO_CLOSE_FOR_RANGED or TOO_CLOSE_TO_ATTACK.
Guard1 `0x1037cdf0` calls empty base `0x10280fb0` and REPLACES Troika's body.
HumanCombatant (and Cop/Hunter/ProneDialog/GhoulCroucher/HumanCombatPatrol/SabbatGunman),
Pedestrian `0x103a2980`, and TzimisceHeadClaw `0x103c16f0` add to Troika; other Troika subclasses
inherit it. The unconditional freeze insertion belongs after the virtual for every species,
including base/cine bodies. HEAD already inserts freeze inside Troika AND Guard1's override;
the integrator moves those insertions to the common effective-mask path, with tests of direct
slot 453 distinct from tests of the completed cache.

The three deployed Troika schedule texts establish:

| program | authored interrupts |
|---|---|
| 0xef STEP_BACK_RANGE_ATTACK1 | NEW_ENEMY ENEMY_DEAD LIGHT_DAMAGE HEAVY_DAMAGE ENEMY_OCCLUDED NO_PRIMARY_AMMO |
| RANGE_ATTACK1 | the same six + TOO_CLOSE_TO_ATTACK |
| 0xf0 FORCED_RANGE_ATTACK1 | ENEMY_DEAD |

Red 4's 99 seconds were TASK_RANGE_ATTACK1 failing to finish, already handled by earlier waves,
not these texts failing to list 0x61 or 0x3c. Close that claim with `ranged_step_back_holds`.
The separately confirmed cache defect is this wave's combat-interrupt repair.

### Fake and real reload

The pre-pass checks fake reload BEFORE clip-positive decline and real reload. Fake reload:
debug_allow_fake_reload enabled, live ranged weapon (&0x6000), count <1 → reroll, then hint choice.
The reroll takes char-template +0x34/+0x38, stored by __ftol toward zero: Min default 8.0,
Max default the parsed integer Min. TutorialThug authors Min 6, no Max.
No template in the port → explicit loader-default stand-in 8/8, false return.

`0xc4` tries cover and sets fail schedule `0xc5`; `0xc5` faces enemy then PLAY_SEQUENCE
ACT_RELOAD_FAST; `0xc6` plays it in cover. Heavy damage interrupts; none runs TASK_RELOAD
or touches the magazine. The count is per FireBullets call/set, not per event, pellet or victim.

TASK_RELOAD runs AutoMovement and its turn arm, waits on slot 251, sets weapon +0x898 to 1,
calls slot 322, clears conditions 0x40 and 0x41, then completes. Without a weapon it only completes.
Slot 322 bulk requires owner combat pointer, bInReload and LIVE owner +0x1564 <=curtime,
then slot 323, then both weapon +0x730/+0x734 stamps =curtime. Damage/block reactions already
write this owner deadline; V4c melee BeginMeleeSwing writes it at `0x103ea2eb`.
The port declaration's no-writer comment is owed to the integrator.

Slot 323 bulk loops magazines 0..1 which use clips: Add=min(Size-clip, reserve), adds it to clip,
debits reserve only for player under DAT_1088aef4 (IsCommand or int<1), then clears in-reload,
jammed and interrupt-reload. No owner → no writes. Single-round slot 322 requires owner PLAYER
pointer +0xa8, so NPC returns retaining bInReload=1. Do not describe this as always cleared in
one RunTask: only an admitted bulk finish clears it. V6 owns retained weapon-word persistence.
Magazine 1 has no representation in the current item surface: name slot 277 answering no clip
and ammo type absent. Player single-round continuation belongs to the player weapon story.

## 2. Shared declarations

Lane 2 adds in `FElysiumWeapon` (`ElysiumWeaponClasses.h`):

```cpp
bool CanReloadMagazine(int32 MagazineIndex) const; // slot 280, 0x10253ab0
void FinishReload();                              // slot 322, 0x10255050
void FinishReloadBulk();                          // slot 323, 0x102552c0
bool bInReload = false;                           // +0x898
```

Do not hand-edit generated `*Slots.cpp`. A newly needed hand virtual body belongs in the matching
`*SlotBodies.cpp`; if outside a lane it is an exact owed line in the coder report.
Only the integrator adds/updates `research/tooling/ghidra/driver/kernel_verdicts.tsv` and regenerates
via `uv run elysium research kernel`. No new generated virtual body is required by this plan.

## 3. Lane file ownership and disjointness

All paths below are under `Source/ElysiumUE/Private/`; they are exhaustive.

| lane | six files |
|---|---|
| V5b-1 | `Substrate/ElysiumNpcCombat10_2.cpp`; `Substrate/ElysiumNpcCombat10.inl`; `Substrate/ElysiumNpcConditions10.cpp`; `Substrate/ElysiumNpcConditions10.inl`; `Tests/ElysiumNpcKernelCombat10Tests.cpp`; `Tests/ElysiumNpcKernelConditions10Tests.cpp` |
| V5b-2 | `Substrate/ElysiumWeaponClasses.h`; `Substrate/ElysiumWeaponClasses.cpp`; `Substrate/ElysiumNpcBaseRunTask.cpp`; `Substrate/ElysiumNpcBaseRunTask.inl`; `Tests/ElysiumWeaponTests.cpp`; `Tests/ElysiumNpcKernelRunTaskTests.cpp` |
| V5b-3 | `Substrate/ElysiumNpcBaseMotor.cpp`; `Substrate/ElysiumNpcBaseMotor.inl`; `Substrate/ElysiumNpcMaintain.cpp`; `Tests/ElysiumNpcKernelMotorTests.cpp`; `Tests/ElysiumNpcKernelSpeciesTests.cpp`; new `Tests/ElysiumNpcKernelInterruptMaskTests.cpp` |

Proof: A∩B=A∩C=B∩C=∅; |A|=|B|=|C|=6, |A∪B∪C|=18. All 17 existing files are present at d0f79574;
the eighteenth is new. Lane 1's range body/tests changed under V4c, as did RunTask/Species tests:
the checker's blanket unchanged-file inventory is superseded. None of the named deliverable
functions was deleted; slots 280/322/323 new hand bodies remain absent.

Lane 1 calls lane 2's declaration; all count jobs land together. Lane 1 reads SelectActiveWeaponWord
directly, lane 2's trace loop does not read ActiveWeaponCapabilityWord; lane 3's common-mask owed
lines are integrator-owned. No coder acquires another lane's file through an include/dependency.
Arena, Schedule.{h,cpp}, Npc.cpp, Guard1.cpp, ConditionsBodies.inl, schedule tests and oracle/ledger
updates belong to the integrator, not a coder lane.

## 4. Acceptance and records

Integrator writes JSON `Arena/scenarios/combat/ranged_step_back_holds.json` and
`ranged_fake_reload.json`, with the donor's events_player row and MakePlayerUnkillable at t=0.05
copied into BOTH. Correct `ranged_sustained_fire.json`'s fake-reload description, measured shot_7
window and obsolete world-tick-poll notes; preserve ALL FOUR never clauses, including shooter death.
Details and exact wave order are in `brief-V5-integrator.md`.

Arm coverage: fake/real selection, loader defaults/truncation/equal-bound RNG, count per set
(including misses/dead or team-refused victims), live deadline equality/future, reload writes,
capability word, ranged masks/testers, and CacheTail order/inverse/freeze preservation on
HumanCombatant and Guard1. Weapon family prefix is **Elysium.Arm.Weapons.** (plural).

The judge's fourth sitting supersedes the earlier event/content fallback.
The stock and patch flame attack timelines are correctly empty; ordinary
humanoid NPC ranged tasks do not reach specialized Attack `0x103e2f30`.
The bounded live reload witness therefore explicitly stages clip0/reserve250/
fake8 after ordinary equip's clip1, then uses native condition0x40, selection
0xc2, authored reload animation and weapon slots322/323.

V6 closing build1 standalone205510.095024 passes `ranged_real_reload`: real
finish/bulk commit at2.800 scenario seconds, clip0→250, NPC reserve250 retained,
all three flags0 and both stamps=world3.800; final30.000 remains250/250/0.
The loaded silent control also passes. Current-binary pair/full evidence is
recorded at V6 close in `../v6/proof-V6.json`; until those runs finish this
paragraph claims only the standalone measurement. No event, firing loop, clip
debit, asset change or bake was introduced. Presence production remains0006;
retained NPC single-round bytes are V6, player continuation0008 follow-up.

## 5. Integration risks and limits

A TutorialThug now fake-reloads after six SETS. Measure the actual 0xc4/0xc5 interval rather than
predicting 2–4 seconds. A 3031 may fire zero or multiple sets; pair arena chronology with the arm
count test. Keep V4c's shared NpcSchedule stream; equal bounds return Min without consuming a draw.
No seed/cooldown repair to hide suite ordering: each new record must pass alone and after another.

If ACT_RELOAD_FAST resolves no sequence under the held weapon, capture the exact sequence ladder
and model. The asset/pipeline owner supplies a missing imported activity/event and any re-bake;
do not alter task completion or manufacture an event. This plan requests no import or re-bake.

No maintenance byte interval remains undecided. Slot 411's descriptive name is not needed to port
its verified empty behavior; vtmb_slot and the RET listing settle it. Listing alone cannot establish
the currently installed binary revision or future animation availability/timings; these are
integrator provenance/trace checks, not deferred retail recovery. No new modernization is adopted.
