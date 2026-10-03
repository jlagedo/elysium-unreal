# Brief B — the attack-position validator, the hint LOS check, the idle gate (0018/8, wave 1)

Read `CLAUDE.md` first. Arm for arm; retail's bugs stay. You edit ONLY:
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcHints.cpp`, `ElysiumNpcHints.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelBaseHelpers.cpp`, `ElysiumNpcKernelBaseHelpers.inl`
  (`HintLosCheck` only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelHintsTests.cpp`
Do NOT build or run tests. Do not touch the searches (brief A owns `ElysiumNpcBaseHints.*`,
`ElysiumHint.*`, the world) or any caller. Report ≤300 words.

Inputs, read first:
- `docs/vtmb/npc-ai/schedule-kernel.md` § "The three hint validators" (l. 1516-1558): `0x10296c40`
  is the body you port; `0x10295ed0` and `0x102961a0` are already ported (`IsHintCoverValid`,
  `IsHintCoverValidLoose`, `ElysiumNpcHints10.cpp:124-127`) and only forward into your seam.
- The new `docs/vtmb/npc-ai/shape.md` subsection "### The claim primitives, the hint LOS check and
  the idle gate (2026-09-30, 0018 story 8)" (just before "## `FValidateHintType`'s species
  half"): the walks of `0x102968f0` and `0x102b5de0`. (There is no findings-R1.md; the
  subsection holds it all.) If it marks an arm unrecovered, leave THAT arm a named seam and say so.
  It CORRECTS the port's comments twice: the LOS check traces FROM the hint (node position at the
  NPC's hull, raised by the NPC's collision maxs z) TO the target's eye, with a filter that
  refuses every combat character; and the idle gate's 3.0 is time-since-occluded failing ABOVE
  3.0, with arm 1 failing only on a world hit.
- Your set also includes `Source/ElysiumUE/Private/Map/ElysiumRetailMaskRecipe.cpp`: add
  `0x46804099` (the LOS mask) and `0x2000000` (the idle gate's monsters-only mask) to the listed
  set so they are not logged as unknown, with the recipe the header's rules give them
  (`ElysiumRetailMaskRecipe.h:150-156`), and say what each resolves to. The hint LOS filter
  refuses all combat characters, so use only the world half of `FElysiumRetailTraceResult` and
  ignore the character list, naming that. The gate's mask carries no brush bit, so a "world hit"
  is impossible for brushes in retail too — port the arm as written and say what it can meet.
- The port's seams: `ValidateHintCoverRange` (`ElysiumNpcHints.cpp:~75`, answers false, comment
  holds the recovered shape), `HintLosCheck` (`ElysiumNpcKernelBaseHelpers.cpp:~729`, answers true),
  `HintIdleActivityGate` (`ElysiumNpcHints.cpp:~100`, comment holds the shape).

## 1. `0x10296c40` — `ValidateHintCoverRange(const FHintWords& Hint, const FElysiumEntity* Enemy, float GoodRange, float BadRange) const`

Keep the signature. Arms in retail order (the oracle text is the spec):
1. PASS, not fail: my own hint (`Hint.HintIndex == BaseScheduleHost.HintNode`) while
   `m_bStayEntrenched` (the port's `bStayEntrenched`) stands, or a null enemy → true.
2. no active weapon → false.
3. height difference over `_DAT_1049ae28` = 64.0 (`ElysiumNpcKernelTunables` — grep for the cell;
   add a named constant only if none exists) → false.
4. hint-to-enemy 2-D distance `< TargetDistMin` → false.
5. unless entrenched: distance over EITHER the weapon's max range (`+0x8c0`, a NAMED SEAM in this
   port — `TroikaShootAtHintDefaultRadiusUnits` 1024 stands for it at `ElysiumNpcTroikaHelpers.cpp:809`;
   reuse that one stand-in, do not invent a second) or `TargetDistMax` → false.
6. a hint that is not mine: `dot(normalize2D(hint - enemy), normalize2D(me - enemy)) >= _DAT_10451ab4`
   (0.2, retail's own message gives it) else false.
7. facing projection against the two bounds, asymmetric: `<= GoodRange` fails ("outside of good
   range"), `>= BadRange` fails ("inside of bad range").
8. only under `m_bForceCoverLOSCheck` (`ScheduleHost.bForceCoverLosCheck`): `HintLosCheck` must
   pass else false.
Retail formats a reason string only when `ai_debug_npc` is this NPC (`IsHintDebugNpc()`, false
here); keep the strings as comments, not logs.

## 2. `0x102968f0` — `HintLosCheck(int32 HintNode, const FElysiumEntity* Target) const`

Port from the subsection: either argument null → false; start = the hint's position for this NPC
(`HintPositionCm`, the existing `0x102d1180` port in `ElysiumNpcBaseHelpers.cpp`, which already
takes the node position at the pathing hull) raised in Z by the NPC's collision maxs z (grep the
NPC's hull / collision maxs accessor); end = `Target->EyePosition()`; mask `0x46804099`; pass =
fraction >= 1.0 and not allsolid and not startsolid; it writes NOTHING (`m_iFailedCoverLOSChecks`
is only ever zeroed in retail). Use
`World->Embodiment()->TraceRetail(FElysiumRetailTrace, FElysiumRetailTraceResult)`
(`Public/ElysiumWorldServices.h:705`; pattern `ElysiumNpcBaseMotor.cpp:176-200`; positions in cm,
`ElysiumMove::U` converts). No embodiment → answer the PASS arm, as the seam does today, and say so.

## 3. `0x102b5de0` — `HintIdleActivityGate() const`

Port from the subsection, NOT the port comment (which is wrong twice). Arm 1: a live
`m_hShootTargetOverride` (+0x5ba8; grep the port's word) → trace WorldSpaceCenter (slot 192) →
its origin, mask `0x2000000`, ignore self; clear → true; blocked → false ONLY when the hit entity
is the world; else true. Arm 2: `HasCondition(0x48)` → false; elapsed-occluded time
(`m_flOccludedDelay + curtime − m_flOccludedReportTimeE`, 0 when the report time is 0) ABOVE
3.0 → false (≤ and unordered continue); no enemy → true; trace WorldSpaceCenter → enemy's
BodyTarget (slot 197), same mask; clear → true; no hit entity → true; relationship 3 or 4 to
the hit entity → false; else true. Use the NPC's existing condition / relationship / enemy /
occlusion-timer accessors; grep before adding one.

## 4. Tests — `ElysiumNpcKernelHintsTests.cpp`

The Seams case (l. ~716-795) asserts the OLD answers ("the 0x10296c40 validator seam itself
answers false", "HintLosCheck answers true"…). Rewrite those assertions as RULE tests over a
world the test builds with hints (`ElysiumHintTests::AddNode` in `ElysiumHintTests.cpp:30`;
`FElysiumNpcWorldBuilder` / `FElysiumNpcWorldFixture` in `Tests/ElysiumNpcTestFixture.h`; the
trace double `Fixture.Services.TraceRetailQuery`, see `ElysiumGeometryFixture.h:184`): one test
per arm of `0x10296c40` in order, the entrenched pass, the asymmetric bounds, the forced LOS; the
LOS check clear vs blocked; the idle gate's arms. Do not assert anything about the searches
(brief C covers them).
