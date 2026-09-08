# Elysium-Unreal

Elysium-Unreal is *Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine) rebuilt
as a playable game — **modernized** — on **Unreal Engine 5.8 + C++**.

## Build & run (Windows)

- `.elysium.local.env` at the repository root holds this machine's roots;
- `ELYSIUM_UE_ROOT` — the Unreal Engine 5.8 install.
- `ELYSIUM_VTMB_ROOT` — the VtMB game install; the source corpus, read and never written.
- `ELYSIUM_WORK_ROOT` — the out-of-repo scratch tree for exports, bakes and logs.
- `uv run elysium` is the only public command surface;
- The running game receives `-ElysiumContentRoot` and reads the export corpus from disk.

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
- `docs/decisions.md` — dated ledger of owner calls: divergences, modernizations, traps.
- `docs/vtmb/` — the oracle: recovered retail facts and addresses. Never port narrative.
- `docs/contracts/` — the seam data formats shared by the pipeline and the runtime.
- `docs/specs/NNNN-<witness>/` — one open thread per folder; deleted when it lands.

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
- Before changing code, recover what retail does: the `vtmb-corpus` decompilation
  (the function, its virtual slot, who overrides it, the fields it reads, the callers),
  then `docs/vtmb/*.md` for what is already recovered. Cite addresses.
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
- Divergences from retail are allowed only as named modernizations, stated in the answer
  and recorded as a dated entry in `docs/decisions.md`.
