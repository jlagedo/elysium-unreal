# Elysium-Unreal

Elysium-Unreal is *Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine) rebuilt
as a playable game — **modernized** — on **Unreal Engine 5.8 + C++**.

## Build & run (Windows)

- `.elysium.local.env` at the repository root holds this machine's roots;
- `ELYSIUM_UE_ROOT` — the Unreal Engine 5.8 install.
- `ELYSIUM_VTMB_ROOT` — the VtMB game install; the source corpus, read and never written.
- `ELYSIUM_WORK_ROOT` — the out-of-repo scratch tree for exports, bakes and logs.
- `uv run elysium` is the only public command surface;
- `uv run elysium build` — incremental editor build.
- `uv run elysium test` — the default tier: the census tests, the arena scenarios and a small smoke
  set (the groups in `DEFAULT_TEST_GROUPS`, `pipeline/src/elysium_pipeline/unreal.py`). A test's tier
  is its name's prefix: `Elysium.Arm.` the per-function arm and unit tests (opt-in, run at a story's
  close: `uv run elysium test arm`), `Elysium.Content.` baked content, `Elysium.Slow.`; `--all` runs
  `Elysium.`. During a story run only the scenario and the family prefix you touch.
- `uv run elysium test <prefix>` — automation test name prefix (`Elysium.Arm.NpcKernelSelect19.`);
  `test A B C` runs several in one boot; results in
  `$ELYSIUM_WORK_ROOT/reports/tests/<timestamp>-<slug>/index.json`.
- Arena scenario results → `$ELYSIUM_WORK_ROOT/reports/arena/<UTC-stamp>/arena/index.json`
  (`.scenarios[]` with `name`, `result`, `first_unmet`) plus `<scenario>.trace.tsv` beside it.
- Build failed on C4458/C4459 (a local shadowing a member or global) → these are errors here;
  check changed functions for shadowed names before building. Incremental build 13–18 s,
  `--arm` build ~130 s.
- `uv run elysium import <lane>` — deploy a corpus lane into `Content/ElysiumCorpus/`; the
  retail Python scripts land at `Content/ElysiumCorpus/scripts/`.
- `$ELYSIUM_WORK_ROOT/research/ghidra/types/datamap_records-vampire.dll.json` — datamap replay (field flags).
- The running game takes no content argument and opens no file outside the project: what it
  reads is baked package content plus the deployed `Content/ElysiumCorpus/`.

## Project layout

- `Source/ElysiumUE/` — C++ runtime
- `Source\ElysiumUEAnimGraph` - The editor-only module holding the graph-node faces (title, colour, tooltip) of the project's two custom animation nodes, split out because UAnimGraphNode_Base cannot link into a packaged game.
- `pipeline/` — Python pipeline; owns build, asset management, VtMB asset decoders and `.uasset` bakes

### Content - Unreal

- `Content/` — Main unreal folder
  `Content/ElysiumAuthored/**` - Manual authored assets git tracked
  `Content/ElysiumGenerated/**` - Generated content from pipeline (vtmb based or generated helpers)
- `Plugins/` — assets directly generated from VTMB install and baked into Unreal Assets.

### Docs

- `docs/vision.md` — what Elysium is and is not, and how it is built.
- `docs/vtmb/` — the oracle: recovered retail facts and addresses.
- `docs/contracts/` — the seam data formats shared by the pipeline and the runtime.
- `docs/specs/layers/` — the build order: the layer map, every retail function the 108 maps need
  with its port status (`audit.tsv`), the upward hooks (`hooks.tsv`), one spec per layer.

## Editing files

- `ast-grep` (structural search/rewrite) and `sd` (regex replace) are installed if useful.

## Project rules

- Save game files are disposable, we have not released and don't try to migrate or keep compatibility
- The port is a VM host for VtMB's data. Schedules, dialogue and map scripts are the bytecode; the C++ substrate is the interpreter. Anything the bytecode can observe is reproduced verbatim: task semantics, condition order, interrupt timing, what a failure writes, and bugs, because shipped programs were tuned against them.
- Modernization is a peripheral swap. Two halves: visual-only (Unreal renders it better; adopt freely) and an algorithm Unreal already ships (adopt only with the retail contract and event sequencing kept). Nothing that changes event order or state is a modernization.
- Build in layer order, bottom-up: **L0 entity** (`CBaseEntity`, physics and movetypes, entity I/O,
  triggers, movers and doors, the save framework, the sound list, sound emission, effects, ConVars
  and game rules) → **L1 animation** → **L2 character** (`CBaseCombatCharacter` with stats,
  disciplines, feeding, weapons, inventory) → **L3 player** → **L4 NPC with L5 scripting** (the
  kernel, species, senses, navigation, squads, hints; scripted sequences, dialogue, Python; the
  player's verbs on NPCs). The order is retail's own: each layer calls the one below it far more
  than it is called back (`docs/specs/layers/README.md`).
- A layer is built for its **core** first — every retail function at least half the maps reach —
  and tested before the next layer starts. `sp_tutorial_1` and `sm_hub_1` together reach the whole
  core: they are every layer's witnesses. **Map-specific code** (reached by ten maps or fewer) is
  built per map after the core, in any order; it depends on the core, never on another map's code.

## When a problem is reported

A reported defect is a question about VtMB, never a request for a patch.

- Treat every visual or gameplay problem as a possible unimplemented VtMB behaviour,
  an unbuilt subsystem, or a missing wire into one. Do not fix the symptom.
- Before changing code, recover what retail does. For an NPC question start from the
  kernel ledger, `docs/vtmb/npc-kernel/` (`functions.md`, `fields.md`, `slots.md`,
  `entries.md`: what a function touches, who writes a field, who fills a slot, who calls in
  from outside), then the walked prose in `docs/vtmb/npc-ai/`, then the `vtmb-corpus`
  decompilation for what neither holds. Cite addresses.
- The kernel ledger (`docs/vtmb/npc-kernel/`) describes retail. It is regenerated when the corpus
  changes, not after a code commit, and no story gate runs `research kernel --check`. What the port
  cites is answered by `uv run elysium research where <address>`.
- Look an address, name, field or slot up before searching `docs/` for it: the `vtmb_where`
  tool, or `uv run elysium research where 0x1028a380 m_scriptState "slot 442"`,
  answers in under a second with its ledger rows, its `docs/vtmb` sections (file, heading,
  line) and the port lines that cite it. `research section <addr|name>` prints just the
  doc section about it, `research verdict <addr>` its porting verdict, `research cited
  --table unported.tsv` what the port never cites, `research rows <table.tsv> col=value`
  any `.tsv`. Do not grep `functions.md` or read a checklist whole.
- Compare the whole retail behaviour against the port. The deliverable is the port of
  that behaviour: every arm, every state it reads, its priority order, and what it
  writes — with the substrate sources it needs (senses, navigator, sounds, memory)
  wired in, not stubbed.
- A one-line fix is acceptable only when the retail chain was already reproduced and
  the defect is a single divergence from it, and the answer must say so with the
  retail evidence.
- A retail input whose producer sits in the same layer or a lower one is built with the work
  that reads it, never stubbed. If that lower layer is not finished, the work stops and the gap
  is reported as a planning fault, with the retail chain that needs it.
- A seam answering "nothing" is allowed in two places only: an **upward hook** — a lower
  layer's branch that only a higher layer runs (listed by address in
  `docs/specs/layers/hooks.tsv`, owned and tested by that higher layer) — and code **no map
  reaches**. Each names the retail field or function it stands for and, for a hook, the layer
  that completes it. Say explicitly what remains unrecovered.
- Record the recovery in the matching `docs/vtmb/` document.
- Divergences from retail are allowed only as named modernizations, stated in the answer.

## Gotchas

- `vtmb_asm`/`vtmb_code` say "no function matches" for bytes outside a Ghidra function → read the
  PE directly: `python E:/elysium-work/codex/sol-ghidra/pe.py const|asm <hexaddr> [count]`
  (system Python has capstone; the uv venv does not). Addresses past a section's raw size read as zero.
- A bare address in `vtmb_*` matches in every module → prefix the module or check which DLL answered.
- Headless Ghidra, even read-only dumps (`DumpAsm` logs `Save succeeded`) → run on a scratch copy
  of `$ELYSIUM_WORK_ROOT/research/ghidra/project/vtmb.{gpr,rep}`, never the shared project (lock
  races, zero-filled `.gbf`); see `research/tooling/ghidra/driver/README.md`. Judge a run by
  `<Script>: wrote <path>` in its log, not its exit code.
- Retail map entities without an import → lump 0 of `$ELYSIUM_VTMB_ROOT/Unofficial_Patch/maps/<map>.bsp`
  (fall back to `Vampire/maps/`); offset/size at byte 8. Script: `E:/elysium-work/codex/sol-ghidra/maps.py`.
- `git worktree remove` deletes gitignored files without asking and recurses through junctions, so a
  worktree linked to the main checkout's gitignored trees (`Content/ElysiumGenerated/`,
  `Content/ElysiumCorpus/`, `Plugins/ElysiumBaked/Content/`, `Plugins/External/`) takes the real
  baked content with it → never `git worktree remove --force`, never `git clean`, never delete a
  worktree folder by hand, in any checkout. Remove a worktree only with plain `git worktree remove`
  after `git status --porcelain --ignored` inside it prints nothing; otherwise leave it for the owner.
