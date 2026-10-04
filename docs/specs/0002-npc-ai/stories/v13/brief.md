# V13 + the two senses bugs — one wave (2026-10-04)

Two coders on disjoint files, then one integrator who builds, bakes `sm_hub_1`, tests and commits.
The rules every brief carries bind all three: retail first, cited by address; coders never build,
bake, run the arena or commit; the query budget (10 s warns, 60 s stops; never read a file over
~200 KB whole); text through Grep / Read / Glob; a build, a bake or a run waited on by blocking or
by its completion notification, never a sleep or a polling loop.

## Lane G1 — V13, the pedestrian nav area (N16; the judge's ruling: implement now)

Files: `Source/ElysiumUE/Private/Map/ElysiumNavAreaActor.cpp` (and `.h` if needed),
`Source/ElysiumUE/Private/Tests/ElysiumNavAreaTests.cpp`.

Evidence (`stories/v1/triage.md` N16): on `sm_hub_1`'s baked Recast meshes no roadway polygon
carries `UElysiumNavArea_Pedestrian`, so the pedestrian filter's price applies to nothing and no
route passes a curb. The bake lays the nine mark convexes; each spans z −298..−39 while the road
surface is at z −303. Retail's `0x2000` contents test runs between node positions about 21 cm
above the ground (`102fbbaa`; slabs z −118..−16, nodes z ≈ −111,
`docs/vtmb/` `navigation-jump-links.md:895`); Recast tests the ground surface, and the engine
lowers a convex's floor by one cell height only (`RecastNavMeshGenerator.cpp:4885`; the Human
cell height is 5 cm, `ElysiumNavBakeLibrary.cpp:401`).

- In `GetNavigationData`, make each `FAreaNavModifier` include the agent height
  (`SetIncludeAgentHeight(true)`), so the volume reaches the surface Recast tests. Comment it as
  the frame difference with `102fbbaa`, under 0018/6's existing NavMesh divergence. Verify the
  call and its effect in the engine source (`$ELYSIUM_UE_ROOT` in `.elysium.local.env`) before
  relying on it; if it does not lower the floor, say what does, and do not widen the volume by a
  made-up margin.
- Leave `pipeline/.../importers/map_collision.py` alone: the staged hulls stay byte-identical to
  the brushes.
- Extend `Elysium.Content.NavArea.Hub` (it only counts the convexes today): on the Human mesh
  `NavAreaAt` answers the pedestrian area at (−1700, −760, −303); the crosswalk gaps stay unpriced
  (the radius growth must not close them — probe a point inside the 258–259 gap); a ×8 pedestrian
  route from (−2294, −1071) to (−776, 205) passes within 48 units of curb 258 and then 259. These
  are red until the integrator's bake.
- Report: what changed with file:line, the engine facts you verified, the probe points and how
  you chose the gap point, what could still fail after the bake.

## Lane G2 — the two senses bugs (Q-H3, verified against the listing)

Files: `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseSenses10.cpp`,
`ElysiumNpcConditions.cpp`, `ElysiumNpcBaseConditions2.cpp`, `docs/vtmb/npc-ai/senses.md`, and
the arm test files that pin these two bodies (name them before editing).

1. **`OnLooked 0x1026a2c0` never clears the SEE family.** Retail clears six conditions
   (`0x105c979c`, 6) at its head, on every sensing pass, before walking the kept player list. The
   port's `BaseOnLooked` (`ElysiumNpcBaseSenses10.cpp:71-89`) and `GatherSight`
   (`ElysiumNpcConditions.cpp:309-447`) clear nothing, though the comment at `:74` says they do.
   Read the listing (`uv run elysium research where 0x1026a2c0`, then the `vtmb-corpus` MCP
   tools): which six, where in the body, and what re-sets each in the same pass. Port the clear at
   retail's place.
2. **The last-known position reads the wrong field.** `GetLastKnownPosition 0x102dfed0` reads
   the memory record's `+0xc`; the port's `Conditions19LastKnownPosition` returns `LastPosition`
   (retail `+0x0`) at `ElysiumNpcBaseConditions2.cpp:328` and `:345`. `UpdateMemory 0x102df700`
   writes both; `RefreshMemories 0x102df320` (free knowledge) writes `+0x0` only. Return the
   `+0xc` field (`Anchor`), and check every other reader of the two fields against the listing
   (`research where 0x102dfed0`); list any you did not change and why.
3. Correct `senses.md`'s record table where it labels `+0x0` and `+0xc` misleadingly.
- A test that pinned the wrong behaviour is corrected to retail's with the address in its name
  or comment; no new test of a port mechanism.
- Report: the six conditions and the listing lines, what changed with file:line, the readers
  checked, the tests touched, and which arena records you expect to move (any record that relied
  on SEE_* staying set, or on the LKP following free knowledge).

## The integrator

1. `git status`: only the two lanes' files and this brief should be modified.
2. One build (`uv run elysium build`); fix only integration breaks.
3. `uv run elysium bake map --maps sm_hub_1` (confirm the verb with `--help`; the last hub bake
   took about 4 minutes with `verify nav`). At most two bakes. If the pedestrian area still does
   not appear, stop: read how the component registers with the navigation system, report, do not
   widen the fix.
4. `uv run elysium test Elysium.Content.NavArea.` and the default tier; the arm families for
   senses, conditions and enemy memory (find the prefixes).
5. Records: update `memory_occluded_kept` as Q-H3's reader specified — append to `about` the
   kept-list explanation; after `combat` insert `cond- SEE_HATE (0x43)` by 1.25 and
   `cond- SEE_ENEMY (0x46)` by 2.0 — check both times against the listing's cadence before
   writing them. Run it, `hub_crosswalk_wait`, `map_hub_idle`, `input_useinteresting`,
   `sense_cone_enter`, then the whole suite once. A green record loses its `known_red`;
   `hub_crosswalk_wait` still red only on a walk-length or light-timing miss is retargeted to N13
   (V4) with the trace lines, never loosened. Every verdict that moved is explained.
6. `stories/v1/triage.md`: Q-H3 settled (the LKP is retail's; the two reds fixed here), N16
   closed or re-stated, the six other maps' slabs filed to R2. `kernel --check` clean.
7. Commit once (`fix(nav,senses): V13 -- <what landed>`), ticking V13 in `spec.md` and
   `TRACKER.md` if `hub_crosswalk_wait` is green or red only on another story's cause and the
   content tests are green. The verdict table goes in the commit message. Branch
   `spec-0002/step-2`; do not push.
