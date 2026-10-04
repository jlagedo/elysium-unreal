# V3b follow-up wave — N15, N16, H16 (the owner's go, 2026-10-04)

V3b landed at `7106f4c5` with its acceptance open: the place program runs as retail's, three
records stay red on causes outside its lanes (`stories/v1/triage.md`: N15, N16, H16). Three coders
on disjoint files, then one integrator. The rules of `README.md` here bind every agent ("Rules for
every agent of V3"): coders never build and never commit; retail first, cited by address; the query
budget (10 s warns, 60 s stops); text through Grep / Read / Glob; wait on a background command by
its completion notification, never a sleep or a polling loop.

## Lane F1 — N15, the place pick (game bug in landed work, 0018/4)

Files: `Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp` (`ClaimAmbientSpot` and what it calls
inside that file), `ElysiumNpc.h` if a declaration goes.

Retail's pick is `PickRandomInterestingPlace 0x102db590` → `BuildCandidates 0x102db470` →
`0x102dad60` (the per-place test) → `0x102da0d0`. The port's `ClaimAmbientSpot` adds a place-type
term and an `AcceptedClasses` term that `0x102dad60` does not have (the class lookup `0x102dd630`
has no caller in retail), so `thug_1` (`npc_VVampire`) never claims `pt1` (type `Idle`).

- Read `0x102dad60` and `0x102db470` in the listing first (`uv run elysium research where
  0x102dad60`, then the `vtmb-corpus` MCP tools). State every test retail's per-place check makes,
  in order.
- Make the port's pick make exactly those tests in that order: delete the terms retail does not
  have; add any retail term the port lacks (as a seam answering "nothing", named for the retail
  field, if its source is not in the substrate).
- Do not touch the release (`FinishAmbientUse`), the programs or the selector.
- Report: retail's tests with addresses, what you deleted and added with file:line, and which
  tests elsewhere pin the deleted terms (names; the integrator deletes or corrects them).

## Lane F2 — H16, the arena's places (harness bug)

Files: `Source/ElysiumUE/Private/Debug/ElysiumArenaSpec.cpp`, `.h`; `Arena/README.md` only if a
place name or type is documented there.

The arena's eight `intersting_place` anchors are typed `Stand`; the place-type table has no such
type, so the rows are removed at spawn and `places_pedestrian_visit` has no place to visit.

- Find the place-type table the runtime reads (the type names retail's maps use: see
  `docs/vtmb/` via `research where intersting_place` and the baked `DA_<map>_Places` contract in
  `docs/contracts/`) and how a row with an unknown type is treated.
- Give the arena's anchors a type the table has, choosing the one retail's own maps use for a
  plain stand-and-wait place (cite a map row that carries it), with the keys such a retail row
  carries. The arena adds nothing a retail row would not have.
- Do not change the type table or the spawn rule: an unknown type being dropped is the game's
  behaviour, not the harness's.
- Report: the type chosen and the retail row it copies, what changed with file:line, and whether
  any record names the old type.

## Lane F3 — N16, the crosswalk curb (game bug in landed work, 0018/7)

Files: `Source/ElysiumUE/Private/Substrate/ElysiumNpcCrosswalk.cpp`, `.h`; the pedestrian
NavMesh filter's file if the fault is its pricing of the crossing (name it in your report before
editing; if it is one of F1's or F2's files, report the line instead of editing).

Verified by the reader (`triage.md` N16): retail's chain is ported up to the route
(`ElysiumNpcSelect.cpp:592-611`, `ElysiumNpcDialogueBodies.cpp:337-459`,
`ElysiumNpcBaseAdvancePath.cpp:58-62`), but `NavLayPedestrianLegs`
(`ElysiumNpcCrosswalk.cpp:101-194`) lays no curb on any hub route although at least 5 of the 16
first routes cross the road between the crosswalk pairs. Which test in the splice drops the pair
is undetermined.

- **Diagnose first, without a build.** You may run `uv run elysium arena hub_crosswalk_wait` at
  most three times (it boots `sm_hub_1` alone, ~300 s of game time: let it block with a 10-minute
  timeout or wait for its completion notification). The built binary is V3b's: your source edits
  do not affect these runs. Raise the existing log categories with a temporary `console` action in
  a scratch copy of the record under a different name (delete the copy afterwards; do not edit
  `hub_crosswalk_wait.json`), and read the log for the route points against the six curb
  positions. If the existing logs cannot show which test refuses, say exactly which log line is
  missing; do not guess the cause.
- **Retail**: what puts a crosswalk on a pedestrian's route — the links / node types the route
  builder reads (`0x102f0400` → `0x102a0bc0` → `0x102a0b90`), 0018's story 7 text and
  `docs/vtmb/` for what is walked. The port lays curbs by a splice because Unreal's NavMesh
  replaces Source's node graph (a kept divergence, rule 2): the contract is the same waypoints a
  retail route would pass, in the same order.
- **Fix** the splice so a route that crosses the road between a crosswalk pair passes both curbs.
  If the cause is baked data (the pair or its link missing from the bake), do not edit the bake:
  report it with the table and row.
- Report: the diagnosis with the log lines that prove it, the fix with file:line, a content or
  arm test to add that pins a hub north→south route laying both curbs (describe it; put it in a
  test file only if it is inside your files), and what stays undetermined.

## The integrator

After the three report: apply their cross-lane lines; delete or correct the tests F1 names (a
test pinning the port's accepted-class term is a port-only test: deleted); one build
(`uv run elysium build`), the default tier, the arm families touched (hints, places, crosswalk,
maintain), the records `places_thug_pt1`, `places_pedestrian_visit`, `map_tutorial_sneak_past`,
`hub_crosswalk_wait`, `input_useinteresting`, `map_hub_idle`, `rollcall_vhuman`,
`rollcall_vhumancombatpatrol`, then the whole suite once. A green record loses its `known_red`; a
record whose program is right and whose only failure is a walk-length deadline is retargeted to
N13 (V4) with the trace lines, never loosened; `map_tutorial_sneak_past`'s hearing half stays on
V12. `kernel --check` regenerated if a generated source is stale. Commit once
(`fix(npc): V3b follow-up -- <what landed>`), ticking V3b in `spec.md` and `TRACKER.md` only if
every V3b record is green or red on another story's cause. At most two builds. Do not push.
