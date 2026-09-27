# 0019 story 5 — execution plan: the class tree

Status: **steps 0–6 accepted** (last: 6r, `9f52fbad`, recorded `a00cd11b`); **commit A landed
2026-09-27** (the four folds, on wip checkpoints `0231934c`, `81c696cc`, `779622a2`, `fe26d16d`,
`aa2b65d9`, `275834a8`, `8941aa9a`; gate 1,320 / 14 / 1, zero failures; `--unported` 987 → 809).
**Commit B (closure) landed 2026-09-27** (`Elysium.Substrate` 1,321 / 0 failed, zero C4263/C4264,
`--unported` 809 → 778; the outcome is the spec's story-5 landing paragraph and §6 below). **Story 5
is closed.** Revised 2026-09-27 after the owner's review of the first six steps;
the revision and its reasons are §1. Cross-cutting findings for B and story 8 are collected in
`$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/briefs/commit-B-handoff-notes.md`.
Records: the commits themselves (§1). Scope: [spec.md, story 5](spec.md). Tracker:
[06b](../TRACKER.md). Tree at revision: `a00cd11b`, clean.

## Start here

- The finished product is the spec's story-5 *Job*. This document says how the rest of it lands.
- Read §3 (what is left, measured on the tree), §4 (the gate), then the commit you are on (§5, §6).
- Retail evidence first, as `CLAUDE.md` § "When a problem is reported" requires. Every retail
  fact the remaining folds need is already cited in §5; recover more only where a body is walked
  and found to differ.
- One writer on the integration checkout; one commit per unit in §5/§6; every commit passes §4.
- Do not tick story 5 or tracker 06b before commit B's gate.

## 1. The 2026-09-27 revision

Steps 0–6 landed the hard part: 46 of the 56 live classes stand as C++ classes with overrides,
qualified direct calls, retail factories and generated bindings. The owner's review found the
*process* around each step, not the C++, to be the cost:

| Machinery (all of it transitional) | Measured at `a00cd11b` |
|---|---:|
| `story-5/**` records: per-step expectations, moves/slots TSVs, decisions, acceptance receipts | 36,163 lines |
| `kernel_migration_step1..6.py` + inventory/audit/replay + their pytest | ≈4,100 + ≈1,100 lines |
| `--check stepN` re-verifying steps 0..N-1 against historical commits (`historical_source`) | one `git show` chain per check |
| `test_delta` step 6: 142 expectation rows, 119 of them the same owner-prefix rename | one rule written 119 times |
| Per step: preflight packet → records → new checker → checker tests → C++ → gate → expectations → acceptance → "r" review packet → docs commit | 25 commits for 7 steps in 4 days |

The checkers guard the *process* (that a transitional tree matches its own record). The *product* is
already guarded by `gen_kernel_shape --check`, `gen_kernel_bindings --check`, the C++ census tests
(`NpcKernelShape.SlotOwners`, `NpcKernelShape.FieldOwners`, `NpcKernelClass.RegistryMatchesFactories`,
`NpcKernelSlots.Defaults`) and the runtime gate. So, from here:

1. **Steps 7–11 collapse into two commits.** Commit A folds the ten deferred classes; commit B deletes
   the compatibility surface and closes. Each is the same mechanical operation steps 2–4 performed
   44 times, with its retail evidence already recovered.
2. **The migration machinery is gone with this revision**, not at commit B: `kernel_migration*.py`
   (core, inventory, audit, replay, step1–6), `test_delta.py`, their pytest modules, and the
   `story-5/` records only they read — `manifest.json`, `schema.md`, the per-step `decisions`,
   `moves`, `slots`, `fields`, `consumers`, `overrides` tables, `expectations/`,
   `acceptance-*.json`, `packets/`, `progress.md`. Six files stay because a surviving tool or the
   oracle reads them: `classes.tsv` (`kernel_shape`), `factories.tsv` (`kernel_ledger`; the C++
   factory tests cite it), `registrations-step2.tsv` and `deletions-step1.tsv` (`population.md`),
   `decisions-step3.json` (`shape.md`'s direct-call list), `unported-step6.tsv` (the 987 pin
   commit B reconciled; renamed `unported.tsv` by commit B, the story-8 pin, 778 rows). The reviewed **stub-fired diff** (§4.3) replaces `test_delta`. C++
   comments that cite a retired record cite git history at `a00cd11b`.
3. **The commit is the record.** Its message carries the outcome, each retail correction (address,
   before/after, the test), the stub-fired diff summary, and the gate results with report paths.
   No packet files; retail facts go to `docs/vtmb/` as always.
4. **Review happens before the commit, not as a separate "r" step.** Findings fold into the commit.
5. **The plan carries no model, harness or effort assignments.** Those were governance for a
   multi-harness handoff a single-writer branch does not have.

One claim in the review is corrected here. The rebuild cost of touching `ElysiumNpc.h` (217
includers, 40 `.inl`; `ElysiumNpcBase.h` 36 `.inl`) is mostly inherent: 162 of the 217 are NPC
substrate files that need the complete class, 81 are tests, 28 are elsewhere. No header refactor is
scheduled. The lever is fewer header-touching commits, which the two-commit shape gives; a test that
only spawns by classname may drop to `ElysiumNpcBase.h` when B touches it, and no more than that.

## 2. What landed (steps 0–6)

| Step | Landed | Commit |
|---:|---|---|
| 0 | Manifest, factory identities from allocation/constructor/vtable evidence, inventory, rehearsals | `aa1c3c86` |
| 1 | The established dead species subset deleted; dead census rows kept | `ecfa9d82`, `896c9f37`, `b2261eca` |
| 2 | 44 species classes, 45 typed classname factories, abstract refusal, fixture migration | `870360e9` |
| 3 | Species dispatch as overrides; 187 qualified direct calls; 7 retail corrections | `21bb56cf` |
| 4 | Species bodies, words and bindings on their classes, one commit; 4r follow-up | `bb690d7a`, `61aa2cd8`, `03c0d030` |
| 5 | `FElysiumNpcBase` (`CAI_BaseNPC`) separated from `FElysiumNpc` (`CAI_BaseNPCTroika`) | `e0a71ee3` |
| 6 | Every generated slot body on its retail owner (entity chain included); census in C++; 6r | `7d63e7fa`, `bdcfc197`, `9f52fbad` |

Details, corrections and evidence paths: the step commits' messages and, for the retired per-step
records (`packets/`, `acceptance-*.json`, `progress.md`), git at `a00cd11b`. Last full gate: **1,281 Substrate + 14 Content + 1 PlayerWorld, zero failures**
(6r). `kernel_shape --unported` pins **987** live unported rules (`unported-step6.tsv`).

## 3. What remains, measured on `a00cd11b`

**Ten deferred classes and where they live today:**

| Retail class | Today | Final (Appendix A) |
|---|---|---|
| `CNPC_VPlayerController` | `FElysiumPlayerControllerNpc final : FElysiumScriptedCharacter`, `ElysiumNpc.h:1251`; built only by `events_player.CreateControllerNPC` (`ElysiumEventClasses.cpp:92`); registered `ElysiumNpcClasses.cpp:522` | `FElysiumNpcPlayerController : FElysiumNpcVampire` |
| `CNPC_VFrenzyShadow`, `CNPC_VWolfMorph` | no class; test fiction via `SetRetailClassForTests` (2 test sites) and `IsRetailClass` arms in ≈10 substrate files | own classes below the controller |
| `CNPCMaker`, `CNPCMaker_Fleshpile` | one `FElysiumNpcMaker final : FElysiumEntity` (`ElysiumNpcMaker.h:14`, 1,383 lines) | `FElysiumNpcMaker : FElysiumNpc`, `FElysiumNpcMakerFleshpile` |
| `CNPCMaker_Zombie` | registered separately (`ElysiumNpcClasses.cpp:531`) on the same class | `FElysiumNpcMakerZombie : FElysiumNpcMaker` |
| `CCineNPC`, `CCineAI` | one `FElysiumScriptedSequence final : FElysiumEntity` for both classnames (`ElysiumScriptedSequence.cpp:125`) | `FElysiumScriptedSequence : FElysiumNpcBase`, `FElysiumAiScriptedSequence` below it |
| `CCineAISchedule` | `FElysiumAiScriptedSchedule final : FElysiumEntity` (`ElysiumAiScriptedSchedule.cpp:108`) | `FElysiumAiScriptedSchedule : FElysiumScriptedSequence` |
| `CAI_TestHull` | no class; `IsRetailClass(TEXT("CAI_TestHull"))` ×3 (`ElysiumNpcMotor.cpp:194`, `ElysiumNpcBaseMotor.cpp:218/236`), a jump-tunable row in `ElysiumNpcMotorShared.h`, a caps row in `ElysiumEntityCaps::SpeciesRows` | `FElysiumNpcTestHull : FElysiumNpcBase` |

**Compatibility surface still in the runtime** (`decisions-step6.json` `surviving_sites`, 50 sites):

- `IsRetailClass` — 10 non-test sites: TestHull ×3 above; ChangBros RTTI type tests ×3
  (`ElysiumNpcChangBros.cpp:550/725/1016`); the cine motor arm (`ElysiumNpcMotor.cpp:468`); the
  maker's runner cast (`ElysiumNpcMaker.cpp:606`); the lookup's own walk
  (`ElysiumNpcKernelClassLookup.cpp:147`).
- `ElysiumNpcKernelClassLookup` (`Find`, `OfClassname`, `DerivesFrom`, `OverrideOf`, `BodyOf`) —
  almost all remaining callers are tests (`BodyOf` 156 occurrences, 22 in one test file).
- `FElysiumNpc::SpeciesSlotRows` (`ElysiumNpcSpecies.cpp:41`) — the FrenzyShadow 599/600 and
  Fleshpile 139/617 rows.
- `FVocalization` / `GSoundsVocalizations` — feed `SpeciesVocalize` for Camera (19), SabbatLeader (2),
  Tzimisce (2).
- `ElysiumEntityCaps::SpeciesRows` — the `CAI_TestHull` row (A) and the `CAI_Hint` `ObjectCaps`
  row (story 8).
- `SetRetailClassForTests` (`ElysiumNpcSpecies.cpp:93`; latch `ElysiumNpcBase.h:438`) — 2 test sites.
- Accepted C4263/C4264 hides at slots 66, 67, 86, 123, 133, 153, 158 (107 ended with fold A4: the
  maker's `ParseMapData` is the slot's override).
- `m_pSenses` (`+0x5cdc`) stored on the Troika; a `CAI_BaseNPC` word (`decisions-step5.json`
  `transitional.Senses`).

**Carried to story 8, not this story:** `Weapon_Switch 0x1032dde0` (callers Ming Xiao, Bach), the
`CAI_Hint` `ObjectCaps` override `0x102d2ee0`, reachability of the unverdicted chain stubs, Camera's
`NPCInit` write of `DesiredMoveYaw`, the slot-8 `GetModelIndex` seam.

## 4. The gate — one gate, both commits

### 4.1 Before the build

```text
uv run elysium research kernel_shape --check
uv run elysium research kernel_ledger --check
uv run elysium research kernel_lists --check
uv run elysium research gen_kernel_shape --check
uv run elysium research gen_kernel_bindings --check
uv run pytest pipeline/tests/test_kernel_ledger.py pipeline/tests/test_kernel_shape.py pipeline/tests/test_gen_kernel_shape.py
```

Generators must not re-emit a moved declaration; never green a gate by hand-editing generated code.

### 4.2 Runtime

```text
uv run elysium build
uv run elysium test Elysium.Substrate
uv run elysium test Elysium.Content
uv run elysium test Elysium.PlayerWorld
```

Zero failures. Then map smoke on `sp_tutorial_1`, `ch_fishmarket_1`, `sp_giovanni_2b`,
`hw_warrens_4`, `sm_pawnshop_1`, watching every Elysium log category, not one; commit A adds a
controller event (cemetery, downtown, e32004 or the vamputil path) and a maker-driven encounter in
the tutorial. Record any path that could not be exercised and why.

### 4.3 The stub-fired diff

From the Substrate reports before and after: the set of `(surface, address, receiver class)` rows
`LogElysiumStub` fired. Review it as three lists — **new** rows, **gone** rows, rows whose **receiver
class changed** — and paste them into the commit record with one line of reason each. An owner
prefix that changed because the body moved is one line for the whole rename, not a row per test.
A new receiver or a new count is a behaviour change until explained. No expectations file.

### 4.4 Observables

A structural move keeps its covered observables. A **retail correction** found on the way lands in
the same commit with its address, the reaching caller or content, before/after, a focused assertion
and its `docs/vtmb/` line. `kernel_shape --unported` must not rise; row identity is (class, slot,
address), so a fold that moves a row is not a rise. A one-line fix is acceptable only where the
retail chain is already reproduced and the divergence is single (`CLAUDE.md`).

## 5. Commit A — the four folds

Order: test hull, controller line, directors, makers. Smallest and most self-contained first; the
makers last because they change world participation. Land as **one commit**; split only at the
maker boundary, and only if the gate finds a regression it cannot attribute across it.

### A1. `CAI_TestHull` → `FElysiumNpcTestHull : FElysiumNpcBase`

- Six own mechanism bodies. Step height 40 and jump speed 40 (`0x102d72b0`, `0x102d72d0`), the
  1024/1024/1024 jump-legal limits (slots 521–523).
- The three `IsRetailClass(TEXT("CAI_TestHull"))` motor arms become the class's overrides; the
  `ElysiumNpcMotorShared.h` row and the `ElysiumEntityCaps::SpeciesRows` row become its own bodies
  (`ObjectCaps` override).
- No retail classname factory: tests construct the C++ type. If port navigation never constructs
  it, say so in the record and keep the contract.

### A2. The controller line

`FElysiumNpcPlayerController : FElysiumNpcVampire`; `FElysiumNpcFrenzyShadow` and
`FElysiumNpcWolfMorph` below it; three classnames registered; `FElysiumPlayerControllerNpc` deleted
from `ElysiumNpc.h`.

- `NPCThink 0x103a4700`: direct Troika think, then virtual slot 614 `ResetThinkTimers`. The
  decompilation is damaged; the listing establishes CALL then tail JMP.
- `PreSelectSchedule 0x103a46b0`: idle answers `0x6b`, otherwise a direct Troika call. Each
  descendant's own overrides are audited; inherited controller behaviour is not assumed.
- `Spawn 0x103a4510` and `NPCInit 0x103a4580` against today's non-solid motor (`BuildOwnMotor`) and
  collision opt-out: retail sequencing and state kept; the non-solid duplicate, if kept, is a named
  modernization.
- The deferred rows move with their classes: slot 404 (`0x103a48b0`), the slot-420 `NPCInit` rows,
  slot 546 id spaces, FrenzyShadow 478 (`0x103766d0`), 440 (`0x10375f20`), 599/600 (from
  `SpeciesSlotRows`), slot 300 `TookLifeSpeciesOf`; the silent vocalisation and view-cone overrides;
  initialization and restore.
- `CreateControllerNPC` constructs the new type; the two `SetRetailClassForTests` test sites become
  real spawns, and the hook, its latch and `OwnRetailClassDerivesFrom`'s test path go.
- Acceptance: creation, schedule/clock behaviour idle and non-idle, a shipped controller event
  through creation, beat completion and removal.

### A3. The directors

`FElysiumScriptedSequence` (`CCineNPC`) under `FElysiumNpcBase`; `FElysiumAiScriptedSequence`
(`CCineAI`) and `FElysiumAiScriptedSchedule` (`CCineAISchedule`) beneath it. Siblings below
`CCineNPC`, not Troika subclasses. Today's two entity classes are rewritten as these three.

- The two sequence classnames split into their actual classes; each takes its own words, bodies and
  bindings; the branch-specific 583–586 signatures are checked.
- Audit before inheriting: construction, Spawn/Activate, target acquisition, possession/release,
  installed think, interruption, chaining, removal, restore. Base-NPC datamap attached without
  Troika-only accessors or scheduling assumptions.
- `m_pSenses` (`+0x5cdc`) moves to `FElysiumNpcBase` with the base-only senses object; the
  `NpcKernelShape.FieldOwners` exception goes.
- The cine motor arm (`ElysiumNpcMotor.cpp:468`) and `BlockedIsNoOp` become overrides.
- Acceptance: existing ScriptedSequence, AiScriptedSchedule and ScheduleWitness coverage; all three
  factories; the CCineAI-specific bodies; interruption; restore of a possessed NPC through the
  map-teardown/revisit path (`NpcKernelDirector.RevisitMidBeat`); explicit saves stay refused
  mid-beat per [spec 0003](../0003-scripted-sequence/spec.md) (lines 184–185), lifting with its
  stories 1–2; a played tutorial sequence.

### A4. The makers

`FElysiumNpcMaker : FElysiumNpc`, `FElysiumNpcMakerFleshpile` and `FElysiumNpcMakerZombie` below it.

- Storage first: solidity, police thresholds and every other duplicate of a base word get one home;
  bindings and maker code read that home.
- Variant behaviour as overrides: Fleshpile `MakeNPC 0x1034c2d0`, `DeathNotice 0x1034c8e0` (the
  runner test becomes a typed test on the tree, not `IsRetailClass`), the spawn contracts
  `0x1034afe0`, `0x1034c020`, `0x1034cc60`, slot 123 (`0x1034bd30`), Fleshpile 139/617.
- Lifecycle participation audited before inheriting callbacks: Spawn, Activate, the installed think
  (distinct from overriding `NPCThink`), AI-enable/wake timer resets, dormancy, kill/removal,
  body/model admission, restore. `AsNpc()` and combat-character membership in world and sense
  consumers checked against retail flags and casts; prior non-participation is not kept by default.
- `ParseMapData(const FString&)` stops hiding slot 107: override or rename.
- Acceptance: enabled/disabled, frequency, depletion, child inheritance, death notice, save/restore;
  inherited inputs write the state the maker reads; global wake/AI-toggle paths do not replace maker
  cadence; tutorial and hub maker witnesses green; a staged tutorial encounter played.

### A — done when

All ten classes stand at their Appendix A place; every classname in Appendix A is registered;
`SpeciesSlotRows`, `SetRetailClassForTests` and the deferred `IsRetailClass` arms have no caller
left except the ChangBros type tests (B); the C++ census tests list no deferred class; §4 green.

## 6. Commit B — closure

- **Delete:** `ElysiumNpcKernelClassLookup` (`Find`, `OfClassname`, `DerivesFrom`, `OverrideOf`,
  `BodyOf`) and its tests' reliance on it — tests assert on the tree; `IsRetailClass` and
  `RetailClass()` where they stand for dispatch (the ChangBros RTTI tests become `AsSpecies<T>()`
  or an equivalent typed test); `FVocalization` / `GSoundsVocalizations`, each row inlined into its
  Camera/SabbatLeader/Tzimisce override; the remaining C4263/C4264 hides; any census query helper
  without a documented non-dispatch consumer.
- **Generator:** `gen_kernel_shape` emits the census only; the obsolete generated slot files go
  with their last dependency; generator tests and source checks updated together.
- **Census tests, final form:** 56 live classes; the exact final factory and descriptor chains;
  class-qualified fields; one override per ported (class, slot) `rule` row; the dead census rows
  still listed; no deferred entry.
- **Residue:** regenerate `kernel_shape --unported`; explain every removed or added row against the
  987 pin by implementation, corrected evidence or scope change. This set and the class map are
  story 8's handoff, with the final report paths.
- **Records:** regenerate the pin as the story-8 pin — done: `story-5/unported.tsv` (was
  `unported-step6.tsv`); `classes.tsv` and `factories.tsv` stay as tooling inputs. The migration machinery
  itself was retired with the 2026-09-27 revision (§1).
- **Docs:** `shape.md`, `population.md`, `coverage.md` and affected oracle topics; the spec's
  story-5 tick and tracker 06b, only after §4 is green on this commit.

Done when the compatibility list is empty, §4 is green, and story 8 has its handoff.

**Landed 2026-09-27.** The §3 compatibility list is empty: the lookup and `IsRetailClass` are deleted
(the typed test is `AsSpecies<T>()` over `IsNpcClass`; the hull words are constructor stores; the
pickup pairs are the classes' own bodies, and the slot-546, jump-tunable and slot-117 row tables and
the name-keyed schedule lookups are gone — the slot-580/452 row tables stay as test-read records of
the recovered bodies, the stated exemption), the vocalisation table is inlined, and the seven hides
are real overrides (66, 67, 86, 123, 133, 153, 158). `m_pSenses` moved in A3. The
generator's outputs are the census, the chain slot bodies (one-constant bodies and counting stubs)
and the new override census; its `accepted` kind is gone. `m_hControllerNPC` has one home.
`--unported` 987 → 778, every row explained (176 implemented and 2 false positives in A, 31
hook-named overrides recognised in B; +2 / −2 for `CNPC_VHengeyokai` 599/600, found carried by the
wrong body and ported; `CNPC_VMingXiao` 166 wired where it had been counted off a comment). Appendix A's own-body column is the ledger's vtable diff. Hand-off: the
spec's story-5 landing paragraph and `story-5/handoff-story-8.md`.

## 7. Session contract, short

Repository state is authoritative; chat is not. At session start read the active commit's section
here, then `git log` and `git status`. A session may end before a commit can land: commit the tree
as a `wip(npc-kernel): story 5 commit A -- <what>` checkpoint whose message states what is complete,
what is edited but unverified, whether it builds, and the next action (the step-5 precedent,
`826f4bb1`); the landed commit follows it. Do not reset, stash or clean as a handoff operation. Independent read-only review
may run beside the build; nobody else edits the integration checkout while its tree is under test.

## Appendix A — final live class inventory (56)

Reviewed 2026-09-24 and verified by step 0 against pinned retail inputs. The 21 dead classes stay in
`population.md` and the census, without port classes. *Port base* is the final base. Own bodies
counts ledger slots filled by the class; own words its top-level layout dispositions; the two base
rows use the 135/253 declaring-layer counts. Implementation and binding counts derive from the
generated census, not from this table. Rows marked **A** land in commit A; the rest stand.

| Retail class | Port class | Port base | Classnames (retail factories) | Own bodies | Own words | Lands |
|---|---|---|---|---:|---:|:---:|
| `CAI_BaseNPC` | `FElysiumNpcBase` | `FElysiumScriptedCharacter` | none (abstract, or built by code) | 215 | 135 | 5 |
| `CAI_BaseNPCTroika` | `FElysiumNpc` | `FElysiumNpcBase` | none (abstract, or built by code) | 185 | 253 | 2 |
| `CAI_TestHull` | `FElysiumNpcTestHull` | `FElysiumNpcBase` | none (abstract, or built by code) | 6 | 0 | **A** |
| `CCineNPC` | `FElysiumScriptedSequence` | `FElysiumNpcBase` | `scripted_sequence` | 20 | 25 | **A** |
| `CNPCMaker` | `FElysiumNpcMaker` | `FElysiumNpc` | `npc_maker` | 21 | 19 | **A** |
| `CNPC_VAnimal` | `FElysiumNpcAnimal` | `FElysiumNpc` | `npc_VAnimal` | 20 | 5 | 2 |
| `CNPC_VBaseBoss` | `FElysiumNpcBaseBoss` | `FElysiumNpc` | none (abstract, or built by code) | 6 | 1 | 2 |
| `CNPC_VCamera` | `FElysiumNpcCamera` | `FElysiumNpc` | `npc_VCamera` | 54 | 0 | 2 |
| `CNPC_VHuman` | `FElysiumNpcHuman` | `FElysiumNpc` | `npc_VHuman` | 26 | 0 | 2 |
| `CNPC_VMingXiaoTentacle` | `FElysiumNpcMingXiaoTentacle` | `FElysiumNpc` | `npc_VMingXiaoTentacle` | 41 | 16 | 2 |
| `CNPC_VNewscaster` | `FElysiumNpcNewscaster` | `FElysiumNpc` | `npc_VNewscaster` | 27 | 6 | 2 |
| `CNPC_VPlaceholder` | `FElysiumNpcPlaceholder` | `FElysiumNpc` | `npc_VPlaceholder` | 24 | 0 | 2 |
| `CPayphone` | `FElysiumNpcPayphone` | `FElysiumNpc` | `npc_payphone` | 27 | 0 | 2 |
| `CCineAI` | `FElysiumAiScriptedSequence` | `FElysiumScriptedSequence` | `aiscripted_sequence` | 5 | 0 | **A** |
| `CCineAISchedule` | `FElysiumAiScriptedSchedule` | `FElysiumScriptedSequence` | `aiscripted_schedule` | 6 | 3 | **A** |
| `CNPCMaker_Fleshpile` | `FElysiumNpcMakerFleshpile` | `FElysiumNpcMaker` | `npc_maker_fleshpile` | 15 | 0 | **A** |
| `CNPCMaker_Zombie` | `FElysiumNpcMakerZombie` | `FElysiumNpcMaker` | `npc_maker_zombie` | 14 | 3 | **A** |
| `CNPC_VDog` | `FElysiumNpcDog` | `FElysiumNpcAnimal` | `npc_VDog` | 19 | 0 | 2 |
| `CNPC_VScurrying` | `FElysiumNpcScurrying` | `FElysiumNpcAnimal` | `npc_VScurrying` | 14 | 9 | 2 |
| `CNPC_VZombie` | `FElysiumNpcZombie` | `FElysiumNpcAnimal` | `npc_VZombie` | 30 | 10 | 2 |
| `CNPC_VMingXiao` | `FElysiumNpcMingXiao` | `FElysiumNpcBaseBoss` | `npc_VMingXiao` | 53 | 29 | 2 |
| `CNPC_VTzimisce` | `FElysiumNpcTzimisce` | `FElysiumNpcBaseBoss` | `npc_VTzimisce` | 71 | 17 | 2 |
| `CNPC_VTzimisceHeadClaw` | `FElysiumNpcTzimisceHeadClaw` | `FElysiumNpcBaseBoss` | `npc_VTzimisceHeadClaw` | 31 | 5 | 2 |
| `CNPC_VTzimisceRunner` | `FElysiumNpcTzimisceRunner` | `FElysiumNpcBaseBoss` | `npc_VTzimisceRunner` | 36 | 5 | 2 |
| `CNPC_VWerewolf` | `FElysiumNpcWerewolf` | `FElysiumNpcBaseBoss` | `npc_VWerewolf` | 52 | 36 | 2 |
| `CNPC_VCameraSecurity` | `FElysiumNpcCameraSecurity` | `FElysiumNpcCamera` | `npc_VCameraSecurity` | 5 | 2 | 2 |
| `CNPC_VGuard1` | `FElysiumNpcGuard1` | `FElysiumNpcHuman` | `npc_VGuard1` | 15 | 2 | 2 |
| `CNPC_VHumanCombatant` | `FElysiumNpcHumanCombatant` | `FElysiumNpcHuman` | `npc_VHumanCombatant` | 15 | 0 | 2 |
| `CNPC_VPedestrian` | `FElysiumNpcPedestrian` | `FElysiumNpcHuman` | `npc_VDialogPedestrian`, `npc_VPedestrian` | 20 | 4 | 2 |
| `CNPC_VTaxiDriver` | `FElysiumNpcTaxiDriver` | `FElysiumNpcHuman` | `npc_VTaxiDriver` | 16 | 1 | 2 |
| `CNPC_VVampire` | `FElysiumNpcVampire` | `FElysiumNpcHuman` | `npc_VVampire` | 8 | 0 | 2 |
| `CNPC_ProneDialog` | `FElysiumNpcProneDialog` | `FElysiumNpcHumanCombatant` | `npc_VMercurio`, `npc_VProneDialog` | 2 | 0 | 2 |
| `CNPC_VCop` | `FElysiumNpcCop` | `FElysiumNpcHumanCombatant` | `npc_VCop` | 22 | 6 | 2 |
| `CNPC_VGhoulCroucher` | `FElysiumNpcGhoulCroucher` | `FElysiumNpcHumanCombatant` | `npc_VGhoulCroucher` | 23 | 9 | 2 |
| `CNPC_VHumanCombatPatrol` | `FElysiumNpcHumanCombatPatrol` | `FElysiumNpcHumanCombatant` | `npc_VHumanCombatPatrol` | 7 | 0 | 2 |
| `CNPC_VHunter` | `FElysiumNpcHunter` | `FElysiumNpcHumanCombatant` | `npc_VHunter` | 16 | 1 | 2 |
| `CNPC_VSabbatGunman` | `FElysiumNpcSabbatGunman` | `FElysiumNpcHumanCombatant` | `npc_VSabbatGunman` | 8 | 0 | 2 |
| `CNPC_VYukie` | `FElysiumNpcYukie` | `FElysiumNpcHumanCombatant` | `npc_VYukie` | 18 | 1 | 2 |
| `CNPC_VRat` | `FElysiumNpcRat` | `FElysiumNpcScurrying` | `npc_VRat` | 18 | 0 | 2 |
| `CNPC_VBach` | `FElysiumNpcBach` | `FElysiumNpcVampire` | `npc_VBach` | 28 | 23 | 2 |
| `CNPC_VBrujah` | `FElysiumNpcBrujah` | `FElysiumNpcVampire` | `npc_VBrujah` | 6 | 0 | 2 |
| `CNPC_VGargoyle` | `FElysiumNpcGargoyle` | `FElysiumNpcVampire` | `npc_VGargoyle` | 40 | 5 | 2 |
| `CNPC_VHengeyokai` | `FElysiumNpcHengeyokai` | `FElysiumNpcVampire` | `npc_VHengeyokai` | 40 | 14 | 2 |
| `CNPC_VLasombra` | `FElysiumNpcLasombra` | `FElysiumNpcVampire` | `npc_VLasombra` | 8 | 1 | 2 |
| `CNPC_VManBat` | `FElysiumNpcManBat` | `FElysiumNpcVampire` | `npc_VManBat` | 22 | 21 | 2 |
| `CNPC_VPlayerController` | `FElysiumNpcPlayerController` | `FElysiumNpcVampire` | `npc_VPlayerController` | 26 | 0 | **A** |
| `CNPC_VVampireBoss` | `FElysiumNpcVampireBoss` | `FElysiumNpcVampire` | `npc_VVampireBoss` | 15 | 9 | 2 |
| `CNPC_VFrenzyShadow` | `FElysiumNpcFrenzyShadow` | `FElysiumNpcPlayerController` | `npc_VFrenzyShadow` | 26 | 2 | **A** |
| `CNPC_VWolfMorph` | `FElysiumNpcWolfMorph` | `FElysiumNpcPlayerController` | `npc_VWolfMorph` | 11 | 2 | **A** |
| `CNPC_VAndreiBlood` | `FElysiumNpcAndreiBlood` | `FElysiumNpcVampireBoss` | `npc_VAndreiBlood` | 21 | 10 | 2 |
| `CNPC_VAsianVampire` | `FElysiumNpcAsianVampire` | `FElysiumNpcVampireBoss` | `npc_VAsianVampire` | 22 | 6 | 2 |
| `CNPC_VChangBros` | `FElysiumNpcChangBros` | `FElysiumNpcVampireBoss` | `npc_VChangBros` | 23 | 12 | 2 |
| `CNPC_VSabbatLeader` | `FElysiumNpcSabbatLeader` | `FElysiumNpcVampireBoss` | `npc_VSabbatLeader` | 36 | 15 | 2 |
| `CNPC_VSheriffMan` | `FElysiumNpcSheriffMan` | `FElysiumNpcVampireBoss` | `npc_VSheriffMan` | 25 | 12 | 2 |
| `CNPC_VChangBrosBlade` | `FElysiumNpcChangBrosBlade` | `FElysiumNpcChangBros` | `npc_VChangBrosBlade` | 7 | 0 | 2 |
| `CNPC_VChangBrosClaw` | `FElysiumNpcChangBrosClaw` | `FElysiumNpcChangBros` | `npc_VChangBrosClaw` | 7 | 0 | 2 |

Own bodies are the ledger's primary-vtable diff against the direct base (`kernel_ledger`
`own_bodies`, commit B), which is what `classes.md` and the census's `OwnBodies` carry. The column
first held a count of bodies Ghidra had *named* on the class, which missed unnamed fills and bodies
Ghidra filed under another class (`CCineAISchedule::FUN_*` bodies only `CCineNPC`'s table holds; the
makers' 617–620); folds A2–A4 counted their classes by hand from the vtables (26/26/11, 20/5/6,
21/15/14), and commit B made the ledger count every class that way, which corrected 26 rows
here (the controller line's three among them). The two base rows count the same way (215 / 185).
