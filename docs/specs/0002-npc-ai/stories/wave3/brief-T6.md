# Brief T6 — the incremental build (wave 3, one agent, you hold the build)

Read `AGENTS.md`, `.claude/rules/cpp.md`, `docs/specs/0002-npc-ai/spec.md` (§ standing rules, § T6)
first. You are the only agent in this wave: you build, measure, change and build again. Rules that
bind you: no polling loops (`uv run elysium build` blocks and ends with a verdict; give the call a
long enough timeout or run it in the background and wait for its notification); text through the
built-in Grep / Read / Glob tools; every query under 60 s; nothing you do may change what the game
does. Do not commit until your acceptance run is green; then one commit.

## Measured

- Builds were the largest single wait: 20.5 agent-hours in 12 days, 667 builds, median 46 s, p90
  280 s, max 1,456 s (`consolidation/findings-E-time.md`).
- The game is one module, `Source/ElysiumUE`: about 780 `.cpp` files and 440k lines, of which
  `Private/Tests` is 225 files and 171k lines (39%) and `Private/Substrate` 334 files and 179k.
  `ElysiumUE.Build.cs`: `PCHUsage = UseExplicitOrSharedPCHs`, `IWYUSupport = Full`. The machine's
  `BuildConfiguration.xml` sets only `bAllCores`.
- Known costs: `Tests/ElysiumTestServices.h` (2,504 lines, included by 102 of 120 NPC test files,
  pulling skeletal-mesh, light and anim-instance headers into kernel tests); the generated
  `Tests/ElysiumNpcKernelOverrideCensus.cpp` (5,210 lines, 1,051 template checks); four test files
  with 111-include preambles (`consolidation/findings-A-tests.md`).
- Wave 2 moved the arm and unit tests into an opt-in tier by name (`Elysium.Arm.…`, run only at a
  story's close); the default tier is the census tests, the scenario tests and a small smoke set
  (`stories/wave2/brief-C-tiers.md` and its report in the wave-2 commit).

- Wave 2's five builds: 9 m 24 s for the near-full rebuild (a public header and ~225 test files),
  then 8–22 s for one- and two-file fixes. So a body edit is already fast; the p90 is header edits.
- **The boot is now the cost of every run.** Wave 2 measured: the default tier 35.5 s wall for
  2.1 s of tests (176 tests; budget 25 s); the arena suite 28.6 s warm and 37.1 s on the first boot
  after a build for 2.4 s of scenarios (target 30 s), of which the stage-world build at boot is
  15–22 s; a three-prefix test run 24.2 s warm, 46.5 s on the day's first boot. The editor's boot
  and shutdown were 19 s median over 536 runs (`consolidation/findings-C-tooling.md` § 1).

## Job

0. **The boot** (it multiplies every test and arena run, so it comes first). Measure where a
   headless boot's seconds go for `uv run elysium test` (default tier) and `uv run elysium arena`,
   warm and first-after-build, from the engine's own log timestamps: module and plugin load, the
   asset registry, startup packages, the project's subsystem initialisation, the stage-world build
   (what is still made resident with `bStageWithoutCatalogue` set — the whole wield catalogue is
   one candidate), shutdown. Then cut what a headless test or arena run does not need (plugins it
   never uses, a registry scan it can skip or cache, residency it never reads, a slow shutdown it
   can replace with an immediate exit once the report is written). Targets: the default tier ≤25 s
   wall warm; the arena host ≤30 s warm and first-after-build. Report the before / after split.
1. **Measure the build first**, one edit at a time, from a clean up-to-date build. Simulate an edit by
   touching the file (content unchanged). For each: wall time, how many compile actions ran, how
   long the link took (UBT's own log and timing output). The edit mix:
   - nothing changed (the null build);
   - one Substrate `.cpp` (`ElysiumNpcConditions.cpp`);
   - the kernel's hot header (`Substrate/ElysiumNpcBase.h`) and `Substrate/ElysiumNpc.h`;
   - `Public/ElysiumEntityWorld.h`;
   - one arm-tier test file; `Tests/ElysiumTestServices.h`;
   - the generated census file alone (its own compile time).
   Record the table before you change anything.
2. **Say where the time goes** for each row: how many translation units a header reaches and why
   (who includes it; read the include graph, do not guess), whether an edited file leaves its
   unity blob (is adaptive unity finding the git working set?), the PCH, the link.
3. **Cut it**, largest measured gain first. Candidates, each to be confirmed or dropped by your
   measurements:
   - **The opt-in test tiers compiled on demand.** 39% of the module is tests and most of them are
     now the arm tier, which a day's builds never run. One header, included only by test files,
     states whether the arm tier is compiled (a test file's arm cases sit inside that guard); it
     defaults to off; `uv run elysium test Elysium.Arm…` and `test --all` turn it on, build, run
     (and say so), so a story's close pays for it once. Toggling must recompile test files only —
     never a module-wide definition that invalidates every action.
   - **Hot-header fan-out.** Forward declarations and narrower includes so a kernel body edit
     recompiles its own file, and a kernel header reaches the files that use it and no others;
     `ElysiumTestServices.h` split by need (a kernel fixture without the skeletal-mesh, light and
     anim headers).
   - **Unity and the working set.** Whether edited files compile alone; the unity settings in the
     target or module rules that make them.
   - **The link.** What the editor DLL's link costs and whether a linker setting in the target
     rules cuts it.
   - **The census file**: kept in the default tier (it is a compile-time check), but out of the
     way of every other edit.
4. **Re-measure** the same table. Then prove nothing was lost: a full build with the arm tier on,
   `uv run elysium test --all` green once, `uv run elysium test` (the default tier) within its
   budget, `uv run elysium arena` as it was before you started.

5. **Two riders from wave 2** (you hold the build, so they land here):
   - `elysium_entity_get` on one NPC with neither `fields` nor `brief` still answers 82 KB (the cap
     is 20 KB). Make the default answer the brief one for an NPC (`brief` 3.1 KB today) and every
     field only on an explicit `full`; the reply says how to get the rest
     (`Debug/ElysiumMcpTools.cpp`). This closes T2: tick it.
   - The comment in `Tests/ElysiumFixtureNoise.h` no longer matches what the file does (two tests
     now declare their own "Stub fired" expectation first); correct it.

## Acceptance

- The boot targets of item 0.
- One Substrate `.cpp` edit rebuilds in ≤60 s; the edit mix's p90 ≤90 s; the hot-header rows
  reported before and after even where they miss.
- The before / after table in the commit body, in `docs/specs/0002-npc-ai/spec.md` § T6 (tick it)
  and `docs/specs/TRACKER.md`.

## Limits

Repository files only: target and module rules, headers, includes, the pipeline's `build` / `test`
commands. A machine-level setting (`BuildConfiguration.xml`, an antivirus exclusion, a faster
linker install) is the owner's: measure what it would give if you can, and propose it in your
report. No test is deleted here and no behaviour changes.

Report ≤300 words: the table before and after, what you changed, what you propose for the machine.
