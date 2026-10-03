# Brief A — the test corpus, audited (NPC / world-AI scope)

Scope: `Source/ElysiumUE/Private/Tests/` — every file whose name starts `ElysiumNpc`, `ElysiumSchedule`,
`ElysiumHint`, `ElysiumNav`, `ElysiumPlace`, `ElysiumGeometry`, `ElysiumMover`, `ElysiumSequence`,
`ElysiumAiScriptedSchedule`, `ElysiumStance`, `ElysiumMovement`, `ElysiumMelee`, `ElysiumStealth`,
`ElysiumSenses`, `ElysiumReaction`, `ElysiumInfra`, `ElysiumRetailHull`, `ElysiumRetailMaskRecipe`,
`ElysiumScene`, `ElysiumWitness`. ~170k lines total across the folder: do NOT read whole files;
`grep -c` the test macros, sample 20–40 lines of each file's opening and 2–3 test bodies, read
fixtures (`ElysiumNpcTestFixture.h`, `ElysiumTestServices.h`, `ElysiumGeometryFixture.h`,
`ElysiumNpcTestHooks.h`, `ElysiumNpcTestCensus.h`) fully.

Deliver `findings-A-tests.tsv` (one row per file: file | lines | tests | fixture used | category |
retail addresses cited (count) | port-only mechanisms pinned | seam-asserting tests | judgement) and
`findings-A-tests.md` (≤2 pages) answering:

1. Categories, with counts: (a) generated census / shape / bindings (`KernelShape`, `Bindings`,
   `ClassTree`, `SpeciesBindings`, `ChainSlots`, `Closure`, `OverrideCensus`); (b) rule tests that
   cite a retail address and assert an arm's outcome; (c) seam tests that assert a seam answers
   "nothing" / false / INDEX_NONE (list them — each must flip when the seam lands); (d) tests pinning
   PORT-ONLY mechanisms with no retail counterpart: `EElysiumBodyOwner` / `FElysiumNpcMind` arbiter,
   `ThinkAmbient` / `EAmbientPhase`, `ThinkPatrol`, `Follower` owner, `bMoveIssued`,
   `bWalkingAnimation`, `bReturnToExternalExecutorAfterSchedule`, `FailedSpotIndices`,
   `ElysiumStub::Fired` / `FireAnimatingSlot` stub tallies; (e) integration / scene tests that run
   the real think loop over a built world (`FElysiumNpcWorldBuilder`, `ElysiumScheduleIntegrationTests`,
   `ElysiumNpcWitnessTests`, `ElysiumSceneTests`).
2. Duplication: the same retail function asserted in two or more files (grep the `0x10......`
   addresses in test names/comments; list addresses appearing in ≥2 files with the files).
3. Which tests would have caught the defect we just found and did not: a schedule with a path task
   followed by an activity task never plays the activity because the `Schedule` body owner holds the
   body and `PlaySequenceClip` (`ElysiumNpcAnim.cpp:408`) plays only under `None`/`Dialogue`. Is
   there ANY test that runs a full schedule (walk then animate) through `Think` and asserts the
   activity completes? Name the nearest ones and what they stop short of.
4. Cost: which files are the slowest to compile (line count, template use) and which tests are
   likely slow at run time (world builders, geometry fixtures, map slices).

Report ≤300 words.
