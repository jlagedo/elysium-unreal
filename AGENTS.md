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
- `docs/vtmb/` — the oracle: recovered retail facts and addresses. Never port narrative.
- `docs/contracts/` — the seam data formats shared by the pipeline and the runtime.

## Editing files

- `ast-grep` (structural search/rewrite) and `sd` (regex replace) are installed if useful.

## Project rules

- Save game files are disposable, we have not released and don't try to migrate or keep compatibility
- The port is a VM host for VtMB's data. Schedules, dialogue and map scripts are the bytecode; the C++ substrate is the interpreter. Anything the bytecode can observe is reproduced verbatim: task semantics, condition order, interrupt timing, what a failure writes, and bugs, because shipped programs were tuned against them.
- Modernization is a peripheral swap. Two halves: visual-only (Unreal renders it better; adopt freely) and an algorithm Unreal already ships (adopt only with the retail contract and event sequencing kept). Nothing that changes event order or state is a modernization.
- Build the host in dependency order. Clock before programs, kernel before consumers.
- A defect claim needs the retail script, schedule or map that reaches it, not a mask read.

## When a problem is reported

A reported defect is a question about VtMB, never a request for a patch.

- Treat every visual or gameplay problem as a possible unimplemented VtMB behaviour,
  an unbuilt subsystem, or a missing wire into one. Do not fix the symptom.
- Before changing code, recover what retail does. For an NPC question start from the
  kernel ledger, `docs/vtmb/npc-kernel/` (`functions.md`, `fields.md`, `slots.md`,
  `entries.md`: what a function touches, who writes a field, who fills a slot, who calls in
  from outside), then the walked prose in `docs/vtmb/npc-ai/`, then the `vtmb-corpus`
  decompilation for what neither holds. Cite addresses.
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
- Where a retail input has no source in the substrate yet, build the seam (the hook,
  the field, the accessor) and leave it answering "nothing" with a comment naming the
  retail field it stands for. Say explicitly what remains unrecovered.
- Record the recovery in the matching `docs/vtmb/` document.
- Divergences from retail are allowed only as named modernizations, stated in the answer.
