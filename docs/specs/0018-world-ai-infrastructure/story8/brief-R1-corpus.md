# Brief R1 — corpus reads for 0018/8 (Hint nodes)

Role: retail reader. You read the VtMB decompilation and record what the port needs. You write
NOTHING under `Source/`. Your outputs are (1) `docs/vtmb/npc-ai/shape.md` — add ONE new
subsection under § "The hint list and its four searches" (before § "`FValidateHintType`'s species
half") titled `### The claim primitives, the hint LOS check and the idle gate (2026-09-30, 0018 story 8)`,
and (2) `docs/specs/0018-world-ai-infrastructure/story8/findings-R1.md`, the arm lists below.
Report back ≤300 words: what you recorded, what stays unrecovered. No listings in the report.

Tools: the `vtmb-corpus` MCP (`vtmb_func`, `vtmb_code`, `vtmb_asm`, `vtmb_callers`, `vtmb_slot`,
`vtmb_fields`, `vtmb_string`). Read `docs/vtmb/npc-ai/shape.md` lines 688-912 first: the four
searches, claim `0x102d1350`, release `0x102d1420`, unusable `0x102d14c0` are ALREADY walked; do not
re-walk them, but confirm any detail you touch against the listing and correct the doc if wrong.

Rules (CLAUDE.md): cite addresses; every arm in order; what a failure writes; bugs kept. Say
"unrecovered" rather than guess.

## Read and record, each as an ordered arm list with addresses

1. `0x102968f0` — "the hint LOS check", called by `0x10295ed0` and `0x10296c40`. Arguments, trace
   start / end (which eye, which offset words), mask, filter, the pass condition (fraction? hit
   entity test?), anything it writes (e.g. `m_iFailedCoverLOSChecks +0x6404`). The port's seam is
   `FElysiumNpcBase::HintLosCheck` (`Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelBaseHelpers.cpp:729`).
2. `0x102d1450` — the owner test (hint, npc) → bool. Exact compare (handle vs entity? live?).
3. `0x102d1540` — `IsHintAvailableToMe` (hint, npc): confirm the three arms in the port comment at
   `ElysiumNpcBaseHelpers.cpp:392`.
4. Slot 550 (vtable `+0x898`) on `CAI_BaseNPCTroika` and every species override: what it answers
   (a field? a constant? a ConVar?). Use `vtmb_slot 550`. The port's seam
   `IdealHintSearchRangeUnits` answers 0.0.
5. `0x102b5de0` — the gate `PlayHintIdleActivity 0x102aaa60` runs before each hint type. The port's
   comment (`ElysiumNpcHints.cpp` ~:100) has a recovered shape; confirm arm for arm and name
   `_DAT_10449258`'s value (`docs/vtmb/npc-ai/rdata-cells.md` may have it).
6. `0x102d1fe0` — the "fifth cursor writer". Who calls it (`vtmb_callers`)? If a live caller exists
   (an NPC class the census lists live, `docs/vtmb/npc-ai/population.md`), walk it fully;
   otherwise record caller and one-line shape only.
7. The list walkers `0x102d0910`, `0x102d31c0`, `0x103cb4b0`: callers only, and whether any is
   reached from a live class. One line each.
8. The `+0x474` class word: confirm `CAI_Hint::Spawn 0x102d0b60`'s type → mask table in shape.md
   line 819-821 against the listing (1 for 100 / 101 / 0x27d8; 4 for 0x283c; 8 for 0x283d; 0x10
   for 0x28a0; else 0), and whether anything else writes `+0x474`.
9. The `m_flHintRating` clamp through `0x1006caa0` (`NPC_Cover_Distance_Scalar`): one attempt; if
   the cvar helper is readable, record the clamp; else leave "unrecovered".

## Format of findings-R1.md

One `##` per item above; under each, a numbered arm list (`address — what it tests — what it
writes / returns`), then `Unrecovered:` if any. ≤120 lines total.
