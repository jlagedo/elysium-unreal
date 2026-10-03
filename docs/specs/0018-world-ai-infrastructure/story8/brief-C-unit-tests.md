# Brief C — unit tests for the hint searches and claim primitives (0018/8, wave 1)

Read `CLAUDE.md` first. You create ONE file:
`Source/ElysiumUE/Private/Tests/ElysiumHintSearchTests.cpp`. Nothing else. Do NOT build or run;
the orchestrator builds once per wave and hands compile errors back to you. Report ≤300 words:
the test names and what each pins.

The bodies under test are being written in parallel by brief A
(`docs/specs/0018-world-ai-infrastructure/story8/brief-A-store-searches.md` — read it: the
signatures in its §2 and §3 are FIXED, code against exactly those). The retail contract is
`docs/vtmb/npc-ai/shape.md` l. 780-912; every assertion must cite the sentence it pins in its
message (address + rule), as `ElysiumHintTests.cpp` and `ElysiumNpcKernelHintsTests.cpp` do.

Conventions: test names `Elysium.Substrate.HintSearch.<Case>`; flags as
`GElysiumHintTestFlags` in `ElysiumHintTests.cpp:22`; build a world with
`FElysiumNpcWorldBuilder` + hints via the same shape as `ElysiumHintTests::AddNode`
(`ElysiumHintTests.cpp:30` — it is file-local; copy the 8-line helper into your namespace) and an
NPC via `Builder.AddNpc` (see `Tests/ElysiumNpcTestFixture.h:56-123`); `FElysiumNpcWorldFixture
F(MoveTemp(Builder))` exposes `F.World`, `F.Npc()`, `F.Services`, `F.Advance()`. Hints prepend on
creation, so author them in the order that gives the list order you want and SAY the resulting
list order in a comment. Hint keys: `HintType`, `group_id`, `hint_rating`, `StartHintDisabled`,
`Group`; a hint-carrying node is `info_node` with `HintType`; retail class masks come from the
type (100/101/10200 → 1, 10300 → 4, 10301 → 8, 10400 → 0x10, else 0). The NPC must be of the
Troika line (base slot 566 answers 0 and finds nothing — pin that too), and its `hint_groups`
must admit the hint's `group_id` (an empty string = all groups).

Cases (one automation test each, or grouped where cheap):
1. `EmptyList` — no hints: `INDEX_NONE`, cursor untouched.
2. `BaseNeverFinds` — a base-class NPC (non-Troika) finds nothing: slot 566 refuses.
3. `CursorWalk1af0` — three hints, cursor set to the middle one: `0x102d1af0` with bit 1 clear
   returns the first admitted AFTER the cursor, wraps, and examines the cursor's element last;
   cursor written to the result; a miss writes `INDEX_NONE`.
4. `CursorWalk24b0And2980` — same layout: `24b0` / `2980` never examine the cursor's own element
   (make it the only admissible hint and expect a miss), and with no cursor walk head to tail.
5. `AdmissionOrder` — a disabled hint, a cooling hint (NextUseTime in the future; equal time is
   USABLE), an owned hint (live owner), a wrong type, one just outside the radius (d² == r² is
   OUT, strict), then the admitted one.
6. `Scoring` — bit 1: nearest d² wins over walk order; bit 3: `sqrt(d) × hint_rating`; an equal
   score REPLACES (later in walk order wins); bit 1 clear: first admitted wins regardless of
   distance.
7. `MaskSearch` — `FindHintByClassMask(0, 8, r)` admits only 10301; mask 0 admits nothing;
   `FindHintByClassMask1` admits 100/101/10200; with bit 0 set and no scoring bit the LAST admitted
   wins and `*OutScore` is left `FLT_MAX` when it stopped at the first.
8. `LineOfSightBit0` — `F.Services.TraceRetailQuery` set to block one hint (fraction < 1) and
   clear another; the blocked one is skipped; the recorded trace's mask is `0x2400b`, its start is
   the NPC's eye and its end the hint origin + the eye offset.
9. `RandomPick` — bit 2 on `1af0` / `24b0` (not `2980`): every admitted hint collected, ONE draw
   on `EElysiumRngStream::NpcSchedule` (seed the fixture; assert the pick is deterministic across
   two runs with the same seed and that `2980` with bit 2 is unaffected); cursor = the pick; zero
   admitted → no draw (assert the stream's state is unchanged, if the RNG exposes a call count;
   else skip that half and say so).
10. `ClaimReleaseOwner` — claim by A succeeds; claim by B while A is alive fails; A re-claims
    fine; kill A (`World.Kill` or mark dead) → B's claim succeeds (stale owner); release writes
    owner invalid and `NextUseTime = Now + delay`; `IsHintAvailableToMe`'s four arms;
    `OwnsHint`.
11. `DisableHintHidesFromSearch` — `Fire(World, Hint, "DisableHint")` → the search skips it;
    `EnableHint` → found again; `Kill` on the hint → skipped (still on the list, count unchanged).
12. `SaveRoundTripClaim` — claim a hint, freeze / restore the world the way
    `Elysium.Substrate.PlaceSet.Restore` does (`ElysiumPlaceSetTests.cpp:~265`) or the
    `NpcKernelBindings.SaveRoundTrip` pattern; after restore the hint's `HintOwner` and
    `NextUseTime` are what was saved; the cursor is `INDEX_NONE` (not saved).

Keep each test short; a helper that builds "N hints on a line at given distances" avoids
repetition. Units: author origins in cm (`ElysiumMove::U` = 2.54 cm per unit); radii in units.
