# RE backlog — what 0019, 0018 and 0002 still owe a read

Built 2026-09-21 from every `Unrecovered:` line on an OPEN story of the three specs, the open
questions they raise, and the port's `CHOSEN, NOT RECOVERED` admissions in the NPC files. One row
per work package; each package is one Codex worker (`gpt-5.6-luna`, effort `max`, the `vtmb-corpus`
MCP), at most five at a time, its answer reviewed by the lead before anything is banked. Briefs and
raw answers: `$ELYSIUM_WORK_ROOT/codex/re2/<wp>/` (`task.md`, `last.md`).

**All 19 packages banked 2026-09-21.** Status: `queued` · `running` · `banked` (oracle + spec updated) · `stale` (the spec line was
already answered in the oracle; only the spec was corrected).

| WP | Owner story | The question | Status | Banked in |
|---|---|---|---|---|
| 01 | 0019/3 | The schedule parser `0x1030d850`: the full operand grammar (beyond the six forms; `DIST:` is a seventh), failure behaviour, name resolution | banked | `schedule-kernel.md` § "The schedule-text parser `0x1030d850`, walked" (17 prefixes, not 6; conditions are global ordinals; `State:` names `0xd` / `0xe`); 0019/3 |
| 02 | 0019/3 | The registration call-site shape in every owner's `InitCustomSchedules`; the owners enumerated; the 691 texts attributed | banked | `schedule-kernel.md` § "The schedule owners and their registrations" (56 init bodies / 48 feeding owners, not 20; base feeds 64 texts, not 63; 691 attributed); 0019/3 |
| 03 | 0018/5 | Goal types: names, issuers and `DoFindPath` arms — 5, 7 and 9 above all; the goal struct and its flag bits | banked | `navigation-jump-links.md` § "The goal types, their issuers and the goal record" (5 has no issuer; 7 = best unknown; 9 = the animal's interesting place); 0018/5 |
| 04 | 0002/25, 10e, 10f | How a bare-`SetGoal` path task completes; `WAIT_FOR_MOVEMENT`; `PLAY_COWER`'s run arm; `LOOK_AT_BEST_UNKNOWN` `0x102a55e3`; the shared look run arm `0x102ab76a` and its head-turn virtual | banked | `schedule-kernel.md` § "`SetGoal` DOES complete the task" (an oracle paragraph reversed); `programs.md` § "The look arms, walked"; 0002/25, 10e, 10f |
| 05 | 0002/10d | `TASK_RUN_DISPOSITION 0xba`: start `0x102a49bc`, run `0x102ab351`, the stance machine and its tables | banked | `programs.md` § "`TASK_RUN_DISPOSITION 0xba` and `_RANDOM 0xbb`, walked"; 0002/10d |
| 06 | 0002/28, 26 | The producer of `COND_PLAYER_ON_HEAD 0x3b`; `TASK_ATTEMPT_DIVE_SIDE` / `_FORWARD`; the mode ConVar | banked | `conditions-and-states.md` § "`COND_PLAYER_ON_HEAD 0x3b`: the producer and the two dive tasks" (the producer is the PLAYER's `PostThink`); 0002/28 |
| 07 | 0002/10i, 10j | The `_INTO` → `_IDLE` → `_OUTOF` activity transition rows (and which function owns them — `conditions-and-states.md` and `shape.md` disagree about `0x10272650`); goal flag 4; `0x100113d8`; the comfort sweep's slot 217 vs 220 | banked | `conditions-and-states.md`, the comfort tasks (no transition table exists: the chain is `SetIdealActivity(m_Activity + 1)` — the worker missed the `INC`, the lead read it off the image); slot 220; `0x100113d8` = goal type; 0002/10i, 10j |
| 08 | 0002/10k | `FIND_COVER_FROM_SAVEPOSITION`, `GET_PATH_TO_SAVEPOSITION`, `_LOS_NOATTACK`, `FACE_PATH`; `m_vecLastDamagePosition`'s writer | banked | `programs.md` § "The saved-position arms and the damage position, walked" (`+0x5b9c` is `m_vecLastDamageAttackPos`: the inflictor's origin); 0002/10k |
| 09 | 0002/10h | `GET_PATH_TO_LASTENEMY_LKP`; `m_flHuntExpireTimer`'s writers; `WALK_PATH_HUNT`; the hunt builders' remaining constants, caller variants and endpoint filters | banked | `programs.md` "The hunt leftovers, closed" (one timer writer; `+0x6598` is a pointer; the endpoint filter reads no hint / cooldown / zone); 0002/10h |
| 10 | 0002/16a | The five `DIST:` tasks, the accumulator's offset, the interrupt-distance producers, `TASKS_FACE_TARGET`'s bit and readers | banked | `social.md` "The distance tasks and `TASKS_FACE_TARGET`, walked"; `schedule-kernel.md` slot 418 (its four rows were reversed in the oracle — fixed from the jump table); 0002/16a |
| 11 | 0002/26 | HitGroup keys behind record bytes `+0x33` / `+0x34`; `TASK_RUN_DIALOG`; `TASK_MELEE_KNOCKBACK` | banked | `conditions-and-states.md` § "The bump and interrupt keys, `TASK_RUN_DIALOG`, `TASK_MELEE_KNOCKBACK`" (`+0x34` is derived, not a key; `COND_KNOCKBACK` has no producer); 0002/26 |
| 12 | 0002/26 | The `ON_FIRE_*` trio; `TASK_JUMP` / `TASK_LAND` | banked | `conditions-and-states.md` § "The burning trio, `TASK_JUMP` / `TASK_LAND`, and who arms `FINISH_JUMP`" (a schedule is uninterruptible mid-jump); 0002/26 |
| 13 | 0002/21b, 6b · 0018/11 | Small reads: `FLIP_NEXT_IDEAL_YAW 0x106`'s field and its reader; the absent-key default of `CAI_Hint +0x46c`; `m_bInPlayerLOS`'s live producer; `SET_COWER`'s arms; whether base schedule `0x43` is one of the 63 base texts | banked | `programs.md` (`FLIP_NEXT_IDEAL_YAW` = motor byte `+0x28`; `SET_COWER`; `+0x46c` default 0; base `COWER` = `0x1e`), `senses.md` (`SetPlayerLOS 0x10291610`); 0002/21b, 6b, 27 · 0018/11 |
| 14 | 0002/12a · 0018/16 | Data: what authors hint type 800 (census + every code use of `0x320`); the 36 / 150 template pin as a patch-first resolved set, and the template loader's enumeration | banked | `programs.md` (nothing authors hint type 800 — the arm is dead by content; kick-at is 10301); `population.md` § "Templates and inheritance" (36 / 150 = 29 patch + 7 VPK-only; the loader walked); 0002/12a, 0018/16 |
| 15 | 0002 (mind) | `CHOSEN, NOT RECOVERED` in `ElysiumNpc.cpp` / `ElysiumNpcConditions.*` / `ElysiumNpcEnemy.cpp`: the idle and alert selection arms, the attack-range edges, the occlusion trace origin, the heavy-damage threshold | banked | `conditions-and-states.md` § "The port's NPC-core guesses, settled" — 7 of 8 guesses wrong (alert selector, attack bands 64 / 256 / 100 / 200 / 1024, four LOS traces, heavy `> 20`, repeated `> 30 %`, sound type bits, `ChangeSchedule` ≠ `StartSchedule`); `combat-and-damage.md` 15 % → 30 % |
| 16 | 0002 (witness) · 0018/17 | `CHOSEN, NOT RECOVERED` in `ElysiumNpcWitness.*` / `ElysiumLaw.*`: the four `pl_*` threshold keyfields' defaults and the `-1` reading, record lifetimes, who publishes an act, the re-raise guard, "nearby", conditions 32 / 34 and attack vs flee, state `0xe`, every `pl_*` ConVar | banked | `population.md` § "The law transaction's guesses, settled" — `Spawn` rewrites `< 1` to 6; `pl_min_act_timer` = 5; scare records live 180 s, oldest evicted; acts do NOT publish; the global list has no cursor; `nosferatu_tolerrant` has no reader; flee is tested before attack; `0xe` = `CRIMINAL_SUSPICION`; 0018/17 |
| 17 | 0019/3 · 0003 | `aiscripted_schedule`: the mode → program switch (1 vs 2, 4 vs 5), `forcestate` / `interruptability`, the `goalent` chain (goal type 3), the corpus census | banked | `authored-control.md` "The modes, settled" (1 / 2 and 4 / 5 are `ACT_WALK` / `ACT_RUN` over the ONE program `IDLE_WALK`; no `interruptability` key exists in retail; 30 shipped rows, not 13); `StartSchedule` ≠ `ChangeSchedule` |
| 18 | 0019/4 | Every `.rdata` cell the NPC oracle still calls unrecovered (about 30), typed by its reading instruction and read from the image | banked | new oracle page `docs/vtmb/npc-ai/rdata-cells.md` (32 cells, typed by instruction; 8 are doubles); the paragraphs that named them annotated inline; input to 0019/4 |
| 19 | 0019/4 | Every ConVar-shaped global the NPC oracle leaves unnamed: name, default, bounds, readers, shipped overrides | banked | new oracle page `docs/vtmb/npc-ai/convars.md`: 42 ConVars named with defaults, bounds, readers; no shipped override of any; input to 0019/4 |

Not RE, recorded so nobody briefs them: 0018/4's open question is the owner's decision (the read is
done); 0018/21-7's one owed read is a measurement over the six maps, part of its job 1.

Stale spec lines found while building this list (the oracle already had the answer): 0002/12b's hint
arms and four cells (`programs.md`, closed 2026-09-19/20); 0002/17's six squad wrappers (`social.md`,
2026-09-19); 0002/27's node keys (`programs.md`, 2026-09-21); 0002/29e's damaged bodies (all eleven
stand); 0002/21a's `0x1042fde0` transform (SafeDisc's scrambler — `dead` under 0019 § Witness data)
and its bytes `+0x6360` / `+0x6361` (uninitialised stack, a retail defect — `senses.md`); 0002/21b's
cower-node query (slot 418 `+0x688` resolves sentinel operands — `navigation-jump-links.md`); 0002/16a's
accumulator offset (`m_flSpecialDistanceAccum +0x5bac`, same section).

## What the pass overturned

Findings that contradict the oracle or the port as they stood — each is fixed in the oracle; the
port changes ride with the story named.

| Was believed | Retail | Rides with |
|---|---|---|
| "`SetGoal` does not complete tasks"; a base `RunTask` path arm exists (oracle) | `SetGoal`'s find wrapper `0x102f1dc0` calls `TaskComplete` itself; no such arm; the task sits RUNNING through the retry window | 0002/10e, 10f; 0018/5 |
| The parser has six operand forms; conditions resolve per class (0019/3) | Seventeen prefixes + four boolean words + a number; interrupt masks are in GLOBAL condition ordinals | 0019/3 |
| 20 schedule owners; base feeds 63 texts; base programs are not text (0019/3, oracle) | 56 init bodies, 48 feeding; base feeds 64 from a static table, `FAIL` is cell 0; four class pairs share a space | 0019/3 |
| `SetIdealActivity` holds an INTO → IDLE → OUTOF switch (oracle); "no mapping exists" (the worker) | The comfort start arm is `SetIdealActivity(m_Activity + 1)` over consecutive ids — the lead read the `INC` off the image | 0002/10i |
| Slot 418's four follower rows (oracle) | Reversed: −1000005 back-away … −1000008 overlap, from the jump table | 0002/16a |
| `COND_PLAYER_ON_HEAD` is an NPC-side contact test | The PLAYER's `PostThink` raises it on its ground entity, every 2 s, no gate | 0002/28 |
| A burning NPC runs; the loop ends when the fire does | `0x151` has no movement and no interrupts; the loop ends with its clip, then slot 616 re-arms the burn 15 s out | 0002/26 |
| Knockback is a condition | `COND_KNOCKBACK 0x28` has NO producer; knockback is always a forced schedule | 0019/1 |
| `+0x34` "interrupt schedule" is a discipline key | Derived: set when any HitGroup has an `OnInterruptSchedule` block (Presence only) | 0006 |
| `aiscripted_schedule` modes 1/2, 4/5 differ by schedule id 9 / 19; 13 rows ship; it has `interruptability` | One program `IDLE_WALK`; they differ by `ACT_WALK` / `ACT_RUN`; 30 rows ship; no such key exists | 0019/3, 0003 |
| Repeated damage is 15 % of max health (oracle) | 30 % (`0.3` double at `0x1047b868`) | 0002/29e |
| Seven port guesses in `ElysiumNpc*` / `ElysiumNpcConditions*` | See the oracle section; the alert selector, attack bands, LOS traces, damage thresholds, sound types, the weapon search, the two inputs | 0002/29e |
| Witness records live 4 s, scare records 5 s, acts publish, a global cursor exists, attack outranks flee, `pl_min_act_timer` 2 | None of those; see `population.md` | 0018/17, 0002 |
| `_DAT_10449280` is a float `0.0` (oracle) | A double `1.0`; eight such cells | 0019/4 |

## Still unrecovered after the pass

Small, named, and none on a story's critical path: the retail name of goal flag `0x4` and any
writer of it; what reads path byte `+0x00` (set by goal type 7) and the `0x8` `0x102f1dc0`
receives; the blocked-move rule's `0x8` (0018/5 correction *(c)*) against goal word `+0x24`;
`TASK_ATTEMPT_DIVE 0x107`'s arm; where a weapon instance's `m_fMinRange1` / `m_fMaxRange1` are
filled from; condition `0x67`'s and `0x78`'s names; the body-fire particle's `+0x484`; the fourth
dialogue word `+0x6554`; `CAI_Motor +0x28`'s member name; two words of the scare record; the
type-7 action dispatcher `0x101f8620`; the seven base schedule names `0x102cadd0` does not
register; the run-time `SOUND:` table; `TASK_WAIT_PVS`'s base arm against Troika's.

## How the pass was run

Nineteen `codex exec` workers, `gpt-5.6-luna` at effort `max`, five at a time, each with the
`vtmb-corpus` MCP and a self-contained brief (`task.md`) that named the spec line, what was
already known, and numbered questions; about 15–25 minutes each. Every answer was reviewed by
the lead against the decompile or the image before banking (`va.py` reads a VA from the pinned
DLL, `disx.py` disassembles one), which caught one wrong worker conclusion (WP07's missed `INC`),
one wrong detail (WP17's slot 217 for the chain waypoint — it is slot 220), one miscount
(WP11's "seven authors" — six) and one over-reach (WP01's "nonfatal unknown flag" — it is a
tier0 `Error`). The `bank_wp*.py` scripts beside the briefs are the exact edits applied.
