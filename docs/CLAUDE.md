# Documentation (`docs/`)

The reverse-engineering reference this project builds on: **engine-neutral VtMB facts**, valid
regardless of target engine. The per-doc index is in the repo-root `CLAUDE.md`; this file is
the maintenance contract.

## Where a given fact belongs

- **Status, plans, task breakdowns, "what landed"** → **`roadmap.md`** (the playable-path ladder
  PP0–PP6, phases P0–P13, per-task status, the pipeline (PL*) and RE backlogs, the risk
  register). It is the *only* status file — there is no as-built archive and no decision log.
  Never mirror status into another doc; link to the task instead.
- **Strategy** (tracks, milestone vocabulary, sidecar contracts, per-system design targets) →
  `rebuild-strategy.md`.
- **What may be modernized vs must be reproduced** → `remaster-direction.md`. A divergence's
  record lives in the doc that owns the system (see House rules below).
- **A new VtMB format/behaviour finding** → the topic doc that owns it (`entity_io.md`,
  `python_bridge.md`, `audio_pipeline.md`, `mdl_v2531.md`, `phy_vphysics.md`, `bsp_format.md`,
  `vpk_format.md`, `texture_format.md`, `facial_animation.md`,
  `source_movement.md`, `controls.md`,
  `level_transitions.md`, `animation_and_movers.md`, `game_runtime.md`). This is **never**
  `tools/CLAUDE.md` — that file is pipeline orientation only, no matter how format-specific the
  finding reads. Cite where it came
  from — decompiled address, `tools/ghidra/run.ps1 -Script DumpGrep` output, or the data file.
  `python_bridge.md` ↔ `script_api.md` split the scripting layer the same way the other pairs
  split theirs: the first owns the binding **mechanism**, the second the per-name **inventory**
  (signature, owning datamap, handler address, call demand). A new engine-callable name goes in
  the second; a new fact about how binding works goes in the first. `vtmb-ui.md` ↔
  `ui-architecture.md` split the UI the same way — the first owns what VtMB's own screens are
  (which code owns them, the two schemes, the 1024×768 canvas law, the HUD class inventory), the
  second owns the Unreal re-skin's stack and design tokens.
- **Design intent for an Unreal system** → `runtime-architecture.md` (the spine: lifetimes, the
  frame, the player object, the session, the seams), `engine-core.md` (entity object model),
  `debug-tooling.md` (debug layers), `map-architecture.md` (map lifecycle),
  `rendering-perf.md` (render path + tuning), `asset-enhancement.md` (offline surface track),
  `input-architecture.md` (input path), `ui-architecture.md` (the CommonUI/Slate UI stack),
  `save-architecture.md` (persistence), `audio-architecture.md` (Audio Mixer integration). Each
  has a VtMB-facts counterpart it must not duplicate —
  `input-architecture.md` ↔ `controls.md`, `ui-architecture.md` ↔ `vtmb-ui.md`,
  `save-architecture.md` ↔ `savegame_format.md`, `audio-architecture.md` ↔
  `audio_pipeline.md`, `runtime-architecture.md` ↔ `game_runtime.md`.
  `runtime-architecture.md` is the one that *integrates* the others: it may state which system owns
  a seam, never how that system works internally.
- **What exists in code right now** → the directory `CLAUDE.md` next to that code
  (`Source/ElysiumUE/CLAUDE.md`, `tools/CLAUDE.md`, `Content/CLAUDE.md`), not a doc here.

## House rules

- **Facts, present tense.** These docs describe how VtMB works and what the design is — not how a
  doc changed or what a past decision replaced. Delete migration narrative rather than relocating
  it; git history holds it.
- **One fact, one home.** A fact is written out in exactly **one** doc — the one that owns the
  system. Everywhere else cites that doc by name instead of restating it.
- **A divergence is a fact, recorded where the system lives.** When we deliberately differ from
  VtMB, the owning doc states the faithful behaviour, states what we do instead, and marks it a
  divergence with the owner call. It does not go anywhere else.
- **Mark confidence.** An unverified reconstruction says so in the doc, and states what would
  verify it (the `recovered/` folder holds these; each carries a status line — e.g.
  `recovered/dice-system.md` records that it was verified by decompilation + the shipped
  `DiceRolls.txt`, so no running-game golden test was needed).
- **Correct in place.** When RE contradicts a doc, fix the doc in the same pass. Don't leave the
  old claim behind with a note explaining that it was wrong.

## `m0_menu_build.md`

Records what the original UI contains and why (the `GameUI.dll` decompile findings, scheme/
`.res` semantics, font roles) — the design intent the modern re-skin is checked against, not
a port target (Elysium-Unreal does not port VGUI; see `remaster-direction.md` axis 1). It is
**partly superseded**: it decompiles the menu VtMB links but does not present, so four of its
claims are corrected in `vtmb-ui.md` §6. Keep that banner when editing.

## Not in this repo

`tools/ghidra/README.md` documents the headless-Ghidra RE workspace. The whole
`tools/ghidra*/` and `tools/re/` trees are gitignored, local-only RE references — cite their
findings here, never commit their contents.
