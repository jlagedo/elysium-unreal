# Step 1, packets 1b–1g — the dead species subset deleted

Start: `0019-5-class-tree`, `aa1c3c86`, after packet 1a. One C++ writer at a time.
Executor: Claude Code, Opus 5.5; packets 1b–1e ran as serial forked writers, each building and
passing its focused suite before the next started.
Outcome: port code serving only the 21 dead classes is gone; their census stays.
Record: [deletions-step1.tsv](../deletions-step1.tsv) (324 rows, each checked against the
accepted step-0 tree and the current tree). Decisions: [decisions-step1.json](../decisions-step1.json).
Evidence root: `E:/elysium-work/research/npc-kernel/story-5/step1/` (per-packet rows, tests, build
and test logs, `gate/`, `regen/`, `inventory-step1.json`).

| Packet | Classes | Removed |
|---|---|---|
| 1b | `CAI_BaseHumanoid`, `CAI_ExpressiveNPC`, `CAI_BaseActor` branch | look/eye/pose/expression island, `Slot584Species` prologue, humanoid arms and rows, expresser |
| 1c | `CGenericNPC`, `CGeneric_NPC`, `_bathack`, `CGenericSabbat_NPC`, `CNPC_VTest` | precache arms and bodies, sound interests, yaw ladder, vocalisation rows, VTest ideal-state arm |
| 1d | `CNPC_Crow`, `CNPC_Bullseye`, `CScriptedTarget` | debug/precache/sound/motor/hint/activity arms, Bullseye spawn and trace gate, ScriptedTarget save/restore/debug |
| 1e | the eleven dead vampire/human leaves | NPCInit, ideal-state, slot-609, squad, schedule and holster rows/arms |
| 1f | all | 192 overlay targets to `-` (189 no-instance rows, registry `Default` values stop being emitted, plus three dead helpers outside that scope); five generators regenerated |
| 1g | — | full gate, regression expectations, step-1 acceptance |

Kept: every live dispatcher (only dead arms went), `SpeciesWorldSpaceCenter` (live werewolf
caller), `RetailHeadDirection` (Rat), `SetViewtarget`, `Slot584`, `CreateComponents`, and the seam
`CharTemplateModelName`, which the live-definition guard restored: `FUN_10207e60` is also called by
the live `CAI_BaseNPCTroika::Spawn` `0x10298d30`, a call the port's `Spawn` does not make yet.
Retained census: the generated activity-table and hull-table rows (see decisions).

Gate: build green; **1,255 Substrate + 14 Content + 1 PlayerWorld**, zero failures.
`test_delta` against step 0's final gate: 13 dead-only tests removed, one renamed
(`CrowCentre` → `SpeciesCentre`), 15 diagnostic changes, all matched by the 29 expectations in
[expectations/step-1.json](../expectations/step-1.json). Nine of the diagnostic changes are a
run-global map-epoch number shifted by the removed tests; six are stubs and spawn warnings that
only removed dead fixtures produced. No live receiver's observation changed.
`kernel_migration --check step1` and `--check step0` pass; 100 Python tests pass; the live rule
identity `000873c0…838f` (11,375 contracts) is unchanged.

## Review follow-up (2026-09-25)

An independent Opus review of `ecfa9d82` found no blocker and no live loss. Fixed in the follow-up:
the checker now requires every removed definition and test to be recorded, checks edited rows
against the current tree, guards every body outside the no-instance rows by symbol and parameter
list, re-derives exemption caller sets from the ledger, matches kept rows exactly, and refuses an
overlay target naming a removed symbol; the record's test rows state the final re-pins; the false
`OverrideMove` comment now names the live ManBat and VampireBoss slot-525 arms this leaf does not
dispatch (a pre-existing divergence, not changed here); the restored `CharTemplateModelName` seam
is asserted in `NpcKernelPrecache10.TroikaPrecache`; stale comment counts and two evidence strings
are corrected. Gate re-run green with the same 29 expectations.

Disposition: accepted.
