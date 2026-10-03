# Brief E — content tests over the baked witnesses (0018/8, wave 2)

Read `CLAUDE.md` first. You create ONE file:
`Source/ElysiumUE/Private/Tests/ElysiumHintContentTests.cpp`. Nothing else. Do NOT build or run.
Report ≤300 words: test names, what each pins, and any number that did not match the census.

Inputs:
- The census: `docs/specs/0018-world-ai-infrastructure/story8/census.md` (rows, list order, the
  expected pick). Every expected number in your assertions comes from it; cite the row.
- The search API (landed, wave 1): `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseHints.inl`
  (`FindHintNear`, `FindHintOfTypeNear`, `FindHintByClassMask`, `FindHintRandom`, `ClaimHint`,
  `OwnsHint`, `ReleaseHintNode`, `IsHintUnusable`), `ElysiumNpcBaseHelpers.inl`
  (`IsHintAvailableToMe`), `Public/ElysiumEntityWorld.h` (`HintList`, `HintCursor`, `HintCount`),
  `Substrate/ElysiumHint.h` (`ClassMask`, `HintRating`, `Disabled`, `HintOwner`, `NextUseTime`).
- The template: `Elysium.Content.Places.WanderHub` in `Tests/ElysiumPlaceSeamTests.cpp:~700-733`
  (`BakedMapPlaces` + `ElysiumEntityDefSource::Load` + `B.Places = Asset->Rows`) — a content test
  that loads a baked map's entity defs and places into an entity world; and the tutorial hint pin
  in `Tests/ElysiumInfraContentTests.cpp:~185` (49 hints). Test names
  `Elysium.Content.Hints.<Case>`; same flags and skip-when-not-baked guard as the template.

Cases:
1. `TutorialList` — 49 hints on the list; head is BSP row 1764 (`zz1`), then 1763, 1760, 1759,
   1717 (census § 1 "first five"); the 12 mask-1 rows are list positions 34–45 = rows 465, 464,
   463, 462, 461…454; every patrol point (type 10000) has `ClassMask` 0 and every cover row 1;
   `HintCursor()` is `INDEX_NONE` after load. `HintRating` on a cover row: the rulebook
   replacement makes the authored 3 → 2.5 when the fixture has a rulebook — check whether this
   fixture has one (`GameState->Rulebook()`); pin whichever value the fixture yields and SAY in
   the message which case it is.
2. `TutorialTacticalSearch` — an NPC of the Troika human line placed at `thug_3`'s origin
   (-1725, 471, 0) units (convert to cm with `ElysiumMove::U`), `hint_groups` all:
   `FindHintByClassMask(8, 1, 1024.0f)` answers row 465 (the first admitted from the head with no
   cursor; census § 3 step 3), and the cursor is now that hint; a second identical call answers
   the next admitted row (464) because the walk starts after the cursor; `FindHintByClassMask(8,
   8, 1024)` (the melee mask) and `FindHintNear(10100, 2, 4096)` (the cower type) answer
   `INDEX_NONE` — census-correct misses, not defects (say so in the message).
3. `TutorialClaimAndCooldown` — claim row 465 for NPC A: `HintOwner` is A; a second NPC B at the
   same origin searching skips it and gets 464; release with delay 5.0 → B's search at `Now + 4.9`
   still skips it, at `Now + 5.0` admits it (strict `<`); `IsHintAvailableToMe` from A vs B.
4. `TutorialDisableHint` — `World.AcceptInput` `DisableHint` on the entity at row 465 (hints are
   unnamed on the tutorial; address it by index) → the search answers 464; `EnableHint` → 465
   again; `Kill` on it → skipped and `HintCount()` unchanged. This is the same path Python's
   `DisableHint()` takes (state that).
5. `HubListAndNamedDisable` — on `sm_hub_1`: 274 hints, head row 2399; `cover_front_10` (row 190,
   type 100) found by name; `DisableHint` by name makes an NPC at (-1173, 583, -111) + 100 units
   east searching `FindHintByClassMask(8, 1, 1024)` skip it (assert it was the answer before and
   is not after).
6. `StartHiddenRows` — the tutorial's 4 `StartHidden 1` hint rows: pin what the port does today
   (are they `Disabled` after load? is `Kill`/hide applied?) with a message that names it as an
   OBSERVATION the census left unwalked (census § "Unrecovered"), not a retail claim.

Keep each test short; share one loader helper. Units: authored origins are Source units in the
census; the world is centimetres.
