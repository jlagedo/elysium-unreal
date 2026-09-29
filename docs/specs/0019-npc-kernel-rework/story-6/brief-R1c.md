# Lane R1c · stale citations of closed rows (small, alone)

Read `README.md` first. `kernel_lists --check` now fails when a closed `dead` row's retail
address is still cited anywhere under `Source/` outside a `Tests/` comment. After wave 1's
deletions these 23 rows are still cited; each cite is a comment, a doc string or a stale name
that survived the body. Your job: make each cite go away without changing behaviour.

You own every hand file under `Source/ElysiumUE/Private/` named below. Do not touch generated
files (`*Slots.inl`, `*Slots.cpp`, `ElysiumNpcKernelShape*`, `ElysiumNpcKernelTunables.*`), the
ledger, or `pipeline/` / `research/`. No build.

Rules for a cite:
- A comment that only names the dead function or address (a "see also", a cross-reference, a
  "moved from") → delete the sentence or the line.
- A comment that explains a LIVE body by contrast with the dead one → reword it so the address is
  not spelled (say "the dead debug twin (0019/6)" instead of the hex).
- A doc header on a file (`ElysiumNpcSounds10.cpp:8-10`, `ElysiumNpcLifecycleShared.h:18`,
  `ElysiumNpcWolfMorph.h:14-33`, `ElysiumNpcFrenzyShadow.h:53`, `ElysiumNpcMaker.h:71`) that lists
  the retail functions the file ported → drop the deleted ones from the list.
- A surviving declaration or constant whose only purpose was the dead body (e.g. a `GDebug…`
  label, `NewscasterNotPlayingText`, an enum row) → delete it; check with `rg` that nothing uses it.
- `ElysiumNpcFlags.h:24,160` cites `0x1028d990` (the condition/flag debug string): if these are
  the flag-name TABLES that the debug string read, the tables are data the ledger names as
  `dead=`… check the delete-list "Why" for 0x1028d990; if nothing but the dead formatter read
  them and no keyfield parser (`NPCFlag:` / `MiscFlag:` in `ElysiumNpcFlags.h`) shares them,
  delete; if the parser shares them, keep the table and reword the comment.
- A `Tests/` comment that RECORDS the removal is allowed; a test comment that still describes a
  live assertion against the dead body is not — look at each test cite and either reword or
  delete the assertion if it asserts a deleted thing (it would not compile anyway).
- Address comments the orchestrator asked lanes to leave (`StartTaskSpecies.cpp:2998`
  `KillTeleportBats`, `ElysiumNpcMaintain.cpp:147`, `ElysiumNpcLifecycle2.cpp:470`): reword so
  the dead address is not spelled; keep the live address on the line.

The list (address: cites):

```
0x1004fbb0, 0x1004fbf0: ElysiumNpcSounds10.cpp:8
0x1009ebb0: ElysiumNpcSounds10.cpp:10
0x1009eca0: ElysiumNpcSounds10.cpp:9
0x102775e0: ElysiumNpcKernelBaseHelpers.cpp:797
0x1028d990: ElysiumNpcKernelConditions10Tests.cpp:38, ElysiumNpcFlags.h:24, ElysiumNpcFlags.h:160
0x10294e70: ElysiumNpcSounds.cpp:140, ElysiumNpcSounds10.cpp:229, ElysiumNpcKernelSpeciesTests.cpp:176
0x102e1110: ElysiumNpcNavigatorMoveStepTests.cpp:246
0x102eea30: ElysiumNpcMaintain.cpp:147
0x10345460: ElysiumNpcBaseHelpers.cpp:90
0x1034bd30: ElysiumNpcMaker.h:71
0x10364550: ElysiumNpcKernelMiscTests.cpp:637
0x103681d0: ElysiumNpcKernelBaseHelpers.cpp:694
0x10375440: ElysiumNpcFrenzyShadow.h:53
0x103a0ff0: ElysiumNpcNewscaster.cpp:291, ElysiumNpcNewscaster.h:27
0x103b0560: ElysiumNpcStartTaskSpecies.cpp:2998
0x103b9270: ElysiumNpcLifecycle2.cpp:470
0x103d1ca0, 0x103d1d60: ElysiumNpcLifecycleShared.h:18
0x103d5130: ElysiumNpcWerewolf2Species.cpp:56, :67, ElysiumNpcKernelSpeciesMisc10Tests.cpp:1254
0x103dc770: ElysiumNpcWolfMorph.h:14
0x103dc8f0: ElysiumNpcWolfMorph.h:15
0x103dc950: ElysiumNpcWolfMorph.h:33
```

Line numbers are from the regeneration run; re-find each with `rg -n <address>`. Finish with
`rg -n "<addr>"` over `Source/` for all 23 showing nothing outside generated files.

Deliver `report-R1c.md` (≤200 words): per address what you did (one line), anything you kept and why.
