# Brief A — the hint store's globals, the four searches, the claim primitives (0018/8, wave 1)

Read `CLAUDE.md` first. This is a port, arm for arm; retail's bugs stay. You edit ONLY:
- `Source/ElysiumUE/Private/Substrate/ElysiumHint.h`, `ElysiumHint.cpp`
- `Source/ElysiumUE/Public/ElysiumEntityWorld.h`, `Source/ElysiumUE/Private/ElysiumEntityWorld.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseHints.cpp`, `ElysiumNpcBaseHints.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseHelpers.cpp` — `IsHintAvailableToMe` only
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseLifecycle.cpp` — `HintSpawn` only (the class word)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelBaseHelpersTests.cpp` — line ~551 only
Do NOT build or run tests; the orchestrator builds once per wave. Do not touch any caller (wave 2
does). Report ≤300 words: what landed, every divergence you had to name, anything unresolved.

Oracle, read in full before coding: `docs/vtmb/npc-ai/shape.md` § "The hint node's own words"
(l. 688-778) and § "The hint list and its four searches" (l. 780-912). Everything below is a
summary of that text; where they differ, the oracle wins and you say so in the report.

## 1. The store's globals — `FElysiumEntityWorld`

Retail: head `DAT_10925450` (exists: `Hints`, head first), cursor `DAT_10925454`, count
`DAT_10925458`. Add to the world (private words + accessors beside `HintList()`):
- `int32 HintCursor = INDEX_NONE;` — the rotating cursor, an entity index. Session-only (retail's
  is a global, not saved): zeroed with `Hints.Reset()` / `Empty()`, and zeroed again by every hint
  creation (the factory `0x102d2f30` zeroes it on every hint it makes) — at both `Hints.Insert`
  sites (`ElysiumEntityWorld.cpp:252`, `:605`). Accessors `HintCursor() const` / `SetHintCursor(int32)`.
- The count is `Hints.Num()`; add `HintCount() const` for the readers that name `DAT_10925458`.
- `FElysiumHint` needs `ClassMask` (`+0x474`, session-only, NOT in the datamap, not saved): add the
  field to `FElysiumHint` and to `FHintWords` (`int32 ClassMask = 0; // +0x474`), carried by
  `ToWords` / `FromWords`, and WRITTEN by `HintSpawn` from the existing `CategoryBits` column of
  `GHintSpawnRows` (`ElysiumNpcBaseLifecycle.cpp:51-83`; a type outside the rows leaves 0). Confirm
  against the oracle's table (1 for 100 / 101 / 0x27d8; 4 for 0x283c; 8 for 0x283d; 0x10 for 0x28a0).

## 2. The four searches — `FElysiumNpcBase`, in `ElysiumNpcBaseHints.{cpp,inl}`

Keep the existing signatures (callers depend on them) and add the retail-complete forms:

```
int32 FindHintNear(int32 HintType, uint8 SearchFlags, float RadiusUnits) const;            // 0x102d1af0 (origin NULL, outScore NULL)
int32 FindHintNear(int32 HintType, uint8 SearchFlags, float RadiusUnits,
                   const FVector* OriginCm, float* OutScore) const;                           // 0x102d1af0, full
int32 FindHintOfTypeNear(const FElysiumEntity* Near, int32 HintType, uint8 SearchFlags,
                         float RadiusUnits) const;                                            // 0x102d24b0
int32 FindHintByClassMask(uint8 SearchFlags, int32 ClassMask, float RadiusUnits,
                          const FVector* OriginCm = nullptr, float* OutScore = nullptr) const; // 0x102d2980
int32 FindHintByClassMask1(uint8 SearchFlags, float RadiusUnits) const;                       // 0x102d2940 = mask 1
int32 FindHintRandom(int32 HintType, uint8 SearchFlags, float RadiusUnits) const;             // 0x102d1760
```
All `const` but they WRITE the world's cursor: `World` is a non-const pointer on the NPC; write
through it, as the existing const bodies do for other world words. Answers are hint ENTITY
indices (`FHintWords::HintIndex`), `INDEX_NONE` on a miss.

The walk (one private helper, parameterised): an empty list answers `INDEX_NONE` and leaves the
cursor alone. Start at `cursor->next` (the list element AFTER the cursor's position in `Hints`),
or at the head when the cursor is `INDEX_NONE` / not in the list / has no next. `0x102d1af0` wraps
to the head unconditionally and stops when it returns to its START element, so every element is
visited once and the cursor's element LAST. `0x102d24b0` and `0x102d2980` wrap only while the
cursor is set and stop on reaching the CURSOR's element, which is therefore never examined; with
no cursor they walk head to tail once. On exit the cursor is written: the returned hint, or
`INDEX_NONE` on a miss. Retail's list is a linked list; ours is `TArray<int32> Hints` head first,
so "next" is index + 1. Say that in a comment.

Admission, in this order, per element (resolve the words with `HintWords(Index, Words)`; an
element that is not a live hint is skipped):
1. not unusable — `IsHintUnusable(Words, Now, owner alive)`; "owner alive" is the owner handle
   resolving to a live entity in `World` (`World->Entities()` valid index, not dead).
2. type: `HintType == 0` or `Words.HintType == HintType` (`1af0`, `24b0`); mask: `(ClassMask &
   Words.ClassMask) != 0` (`2980`), so mask 0 admits nothing.
3. distance: 3-D squared distance STRICTLY `< RadiusUnits²`, in SOURCE UNITS (convert with
   `ElysiumMove::U`; NaN rejects — write the compare so NaN fails). From `OriginCm` when given, else
   the NPC's origin; in `24b0` from `Near`'s origin (a null `Near` — say what retail does: the
   oracle's signature dereferences it; guard with a named crash guard answering `INDEX_NONE`).
4. slot 566: `const_cast<FElysiumNpcBase*>(this)->FValidateHintType(const_cast<FHintWords*>(&Words))`,
   as `ElysiumNpcHints10.cpp:151` does.
5. scoring (`1af0`, `2980` only): flags bit 3 (`8`) scores `sqrt(d²) × HintRating`, else bit 1
   (`2`) scores `d²`, both from the NPC's OWN origin (not `OriginCm`); a candidate is dropped only
   when STRICTLY worse than the best so far, so an equal score replaces (the later element wins a
   tie).
6. flags bit 0 (`1`): a line trace from the NPC's `EyePosition()` (`Near`'s in `24b0`; `Near` must
   be an NPC-like entity — if it has no eye, use its origin and say so) to the hint's origin plus
   THAT SAME entity's eye offset (`DefaultEyeOffsetCm()`, the port's `m_vecViewOffset`), retail mask
   `0x2400b`, `Ignore = {that entity}`, `Filter = Simple`, through
   `World->Embodiment()->TraceRetail(Trace, Result)` (pattern: `ElysiumNpcBaseMotor.cpp:176-200`);
   admitted only on `Result.Fraction == 1.0f`. No embodiment (tests without one) → treat as clear
   and count it in a named seam counter, as the motor does.

Stop rules: `1af0` stops at the first admitted node unless bit 1 or bit 3 is set (`TEST AL,0xa`),
then it keeps the best. `24b0` always returns the first admitted. `2980` stops at the first
admitted iff bit 0 is CLEAR and then leaves `*OutScore` at `FLT_MAX`; with bit 0 set and no
scoring bit the LAST admitted wins. `*OutScore` (when non-null) receives the winner's score, or
`FLT_MAX`.

Bit 2 (`4`) diverts `1af0` and `24b0` — NOT `2980` — to `FindHintRandom(HintType, Flags, Radius)`,
dropping anchor, origin and score. That body walks head to tail with no cursor and no wrap,
applies steps 1–4 and 6 (no scoring), collects every admitted element, draws ONE
`ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, Count - 1)` only when `Count >= 1`,
and the cursor becomes the pick, or `INDEX_NONE`.

## 3. The claim primitives — `FElysiumNpcBase`

```
bool ClaimHint(int32 HintNode);                       // 0x102d1350(hint, npc)
bool OwnsHint(int32 HintNode) const;                  // 0x102d1450(hint, npc): owner handle == my handle
void ReleaseHintNode(int32 HintNode, float ReuseDelaySeconds);  // 0x102d1420: owner = invalid, NextUseTime = Now + delay
bool IsHintAvailableToMe(int32 HintNode) const;       // 0x102d1540 (in ElysiumNpcBaseHelpers.cpp)
```
Claim: refused (false) ONLY when the owner handle resolves to a live entity that is not the
requester; a stale owner, no owner, or the requester itself succeed and write
`hint->HintOwner = Handle`. Release: the two stores, nothing else (no owner gate — the gate is the
Troika caller's, wave 2). IsHintAvailableToMe: owner is me → true; `Now < NextUseTime` → false;
live owner → false; else true. All write the LIVE `FElysiumHint` (cast via `FElysiumHint::Cast`
on `World->Entities()[Index]`), never a copy. `0x102d1450` / `0x102d1540` may be re-read by R1
(`docs/specs/0018-world-ai-infrastructure/story8/findings-R1.md`, if present when you start);
if it names a different compare, follow it.

Update the doc comments in `ElysiumNpcBaseHints.inl` (they say SEAM); update the test at
`ElysiumNpcKernelBaseHelpersTests.cpp:~551` if `IsHintAvailableToMe(0)` on a non-hint index must
now answer differently (a non-hint index: retail would dereference a NULL `CAI_Hint*` — choose
`true`, the free answer, as a named crash guard, and keep the test).

Units: `FHintWords::OriginCm` and every port position are centimetres; radii and scores are
retail units. Compare in units. Cite the address at every arm as the surrounding code does.
