# Lane R1 · the wave-1 residue sweep (alone; every hand file under `Source/` is yours)

Read `README.md` first, then `report-A.md`, `report-B.md`, `report-C.md` (their "Needs another
owner" and "Kept because a live caller sits in another lane's file" sections are your list). Lanes
A, B and C are finished; no one else edits `Source/` now. You still may not edit generated files
(`*Slots.inl`, `*Slots.cpp`, `ElysiumNpcKernelShape*`, `ElysiumNpcKernelShapeMap*`,
`Tests/ElysiumNpcKernelOverrideCensus.cpp`, `ElysiumNpcKernelTunables.*`, `ElysiumNpcKernelBindings.*`,
`Visual/ElysiumNpcActivityTables.cpp`), the ledger, or anything under `pipeline/` / `research/`.
Do not build.

## Decisions already taken (apply them)

1. **`FElysiumScriptedSequence::Touch` / `Blocked`** (0x101a75a0 / 0x101a7580, dead, bare `RET 4`):
   delete both overrides (`ElysiumScriptedSequence.cpp:248-258`, `.h:49-51`).
2. **`FElysiumNpcBase::GetStateName`** (0x1027e740, dead name table): delete the body
   (`ElysiumNpcBaseClosure.cpp:54…`) and the slot-406 row in `Tests/ElysiumNpcKernelClosureTests.cpp:292-296`.
   The generated declaration goes with the regeneration.
3. **The four think-stamp getters** `LastUpdateThink` / `LastNormalThink` / `LastMoveThink`
   (`ElysiumNpcLifecycle.cpp:42-60`) and `LastAiThink` (`ElysiumNpcEntityChain.cpp:282…`), dead
   (slots 412–414 and 0x101aa730 have no dispatch site): delete the bodies and their declarations
   in `ElysiumNpcLifecycle.inl:59-70` and the EntityChain `.inl`. Rewrite the Lifecycle test
   (`LifecycleTests.cpp:~260-280`) so the slot-614 `ResetAllThinkStamps` assertion reads
   `ScheduleHost.LastUpdate/LastNormal/LastMove/LastAI` directly (slot 614 is a live PORT row and
   its test stays); delete the EntityChain test at `:1180-1195` and its `IMPLEMENT_…` line.
4. **`KillTeleportBats`** (0x103b0560, dead: `npc_VBatSwarm` is instantiated by nothing, story 1
   `population.md`): delete the call at `ElysiumNpcStartTaskSpecies.cpp:2998`, leaving the line's
   address as a comment `// 0x103af014 KillTeleportBats 0x103b0560: dead, the swarm class is never
   instantiated (0019/6)`; delete the body (`SheriffMan.cpp:640…`), the declaration
   (`SheriffMan.h:88-89`) and the test lines `PositionsTests.cpp:~810-820`.
5. **`FUN_103a0ff0`** (Newscaster debug line builder, dead): delete the debug `if` block in
   `ElysiumNpcThinkSpecies.cpp:286-291` (keep `FElysiumNpc::NPCThink()` and
   `PlayNextNewscasterStory()`; leave a one-line comment with the addresses), delete
   `Think19NewscasterDebugConVar` if nothing else calls it, the body (`Newscaster.cpp:201…`), the
   declaration (`Newscaster.h:36`) and the four tests in `SpeciesTests.cpp:~1205-1260`.
6. **`StartSearchTimer` / `ReportSearchTimer`** (0x103d1ca0 / 0x103d1d60, dead `rdtsc` statics):
   in `ElysiumNpcMisc2Species.cpp` delete the `StartSearchTimer();` statement at `:881` and turn the
   three `return ReportSearchTimer(X);` into `return X;` keeping each line's address comment; delete
   the two statics (`Werewolf.cpp:1354-1370`, `Werewolf.h:207-214`), `SearchTimerElapsedCycles`
   (`Lifecycle.inl:197-199`, `Lifecycle.cpp:292-295`), `GSearchTimerCycles` and `LifecycleCycles()`
   if they have no other user, and the test at `LifecycleTests.cpp:~800-816`.
7. **Orphan declarations** (report C's list and any others): a member-function declaration in a
   hand `.h` / `.inl` under `Substrate/` whose definition no longer exists anywhere under `Source/`
   is deleted with its doc comment. Find them with a script: collect `Class::Name(` definitions
   from every `.cpp` / `.inl`, collect declarations from the hand `.h` / `.inl` (skip generated
   files), and diff. Named already: `BaseHelpers.inl`, `BaseMisc.inl`, `Schedule.inl`
   (`FScheduleLoadFlag`), `Sounds10.inl` (`TroikaSlot506`), `BaseSquad.inl`, `Squad.inl`,
   `Animating.h` / `.inl` (rebound and formatter declarations), `Combat10.inl`, `Conditions10.inl`,
   `BaseLifecycle.inl`. A `virtual` declaration without a definition is a link error; a plain one is
   debris; both go. Inline-defined members and templates are not orphans. Report the count per file.
8. **Includes and stale `using`s**: `rg` for `#include` lines naming a file that no longer exists
   under `Source/` and delete them.

## Not yours

- `GetUsedHullBits` (three rows) and `PreTranslate_Stalker`: the orchestrator re-verdicts / exempts.
- Anything a `mechanism` row owns (wave 3).

## Deliver

`report-R1.md` (≤300 words): each numbered item done / not done with the reason, orphan
declarations removed per file, includes removed, anything you found that still names a deleted
symbol (`rg` proof, file:line), `targets-R1.tsv` for any dead address whose body you removed that
`targets-A/B/C.tsv` do not already carry.
