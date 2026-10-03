# Brief F — the record: spec, tracker, oracle, ledger (0018/8, wave 3)

Read `CLAUDE.md` first. You edit ONLY:
- `docs/specs/0018-world-ai-infrastructure/spec.md` — story 8's block (tick the box, add a
  "Landed 2026-09-30" paragraph in the style of story 4's / 7's)
- `docs/specs/TRACKER.md` — row 14 (tick; the same bullet shape as rows 07–13; the header line
  "What's next" → row 15)
- `docs/vtmb/npc-ai/shape.md` — § "The hint node's own words" ▸ "The live hint, stood": the
  "Caller audit" table's first row (the searches are no longer seams) and the port paragraph
- `docs/vtmb/npc-kernel/` — regenerate what `uv run elysium kernel --check` wants (seam-list /
  coverage) and run the check; report its result
Do NOT touch `Source/`. Report ≤300 words.

Inputs, read first: `docs/specs/0018-world-ai-infrastructure/story8/landing-numbers.md` (the
orchestrator's numbers: tests, live check, census) and the six briefs / reports in that folder
for what landed. The landing paragraph must state, in this order, like story 7's:
1. What retail has and what the port now runs, by address: the four searches (`0x102d1af0`,
   `0x102d24b0`, `0x102d2980` / `0x102d2940`, `0x102d1760`) over the world's list, cursor and
   count; the class word `+0x474`; claim / release / owner test / available-to-me
   (`0x102d1350`, `0x102d1420`, `0x102d1450`, `0x102d1540`); the attack validator `0x10296c40`,
   the hint LOS check `0x102968f0`, the idle gate `0x102b5de0`; the kick-hide walk `0x102d0910`;
   slot 550 `CoverRadius`; the `NPC_Cover_Distance_Scalar` replacement of `hint_rating`.
2. What was corrected on the way (the spec's own "no registry" line was stale; the two mask-search
   callers passed a type where retail passes a class mask; the port's comments on the LOS check
   and the idle gate were backwards; the NPC-side `bOwnsHint` / `HintReusableAt` stand-ins deleted;
   the duplicate `FUN_10296c40` deleted; the cover validators' facing frame).
3. Named divergences / crash guards: a null anchor in `0x102d24b0`; a non-hint index in the
   claim primitives; the `DefaultEyeOffsetCm` stand-in for `m_vecViewOffset`; `ClassMask`
   re-derived on restore (retail's restore behaviour unread); the idle gate's world hit on the
   `ECC_Pawn` fallback; the LOS filter's two unasked arms; the weapon `+0x8c0` seam.
4. Gate numbers and the live check, from landing-numbers.md.
5. Handed on: the seams NOT taken (weapon `+0x8c0`, `BodyTarget` stub for the player enemy),
   `0x102d0910`'s activator order, `m_iszUserData`'s reader; and anything landing-numbers.md
   lists under "Handed on".
Also add a RE-BACKLOG row if landing-numbers.md names one.
