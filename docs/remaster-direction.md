# Remaster direction — what "Elysium" is and is not

**Target: VtMB *remastered*.** Keep the game — its tone, its ambience, its feel, its logic —
and raise the craft with tools 2004 did not have. Not a pixel-perfect recreation: where the
original made a choice, we keep it; where the original was *constrained*, we are not.

This doc is the **direction charter**: what may be modernized, what must be reproduced, and
who decides. It sits beside `rebuild-strategy.md` (how the thing is built) and
`roadmap.md` (what is being built, in what order). Where they disagree with this doc on
*intent*, this doc wins and they get corrected.

## The one governing rule

> **We only change what we understand, and only on an explicit call.**

Reverse-engineering comes **first** — a system is decompiled, documented, and reproducible
*before* anyone proposes improving it. "This feels off, let's redo it" is not a plan; "here is
the decompiled behaviour, here is why 2004 shipped it that way, here is what I want instead"
is. Concretely:

1. **Default is reproduce.** Absent an explicit decision, every system is built to match
   retail behaviour. Uncertainty resolves toward the original, never toward invention.
2. **Divergence needs the understanding precondition.** A behavioural change may only be
   proposed once the faithful behaviour is *known* (RE'd or verified in-game) and recorded.
   Ignorance is not licence.
3. **Divergence needs an owner's call.** The project owner approves each behavioural
   divergence explicitly, and it is recorded **in the doc that owns the system** — the faithful
   behaviour stated next to the chosen one and marked as a divergence. No silent drift, no
   "while I was in there".
4. **The RE backlog does not shrink because of this direction.** Decompiling stays as
   valuable as ever — you cannot judge what to keep until you know what is there.

## Three change layers, three different rules

Every change lands in exactly one of these. Which layer a change belongs to is decided
*before* the work, not after. (These are **change layers** — unrelated to VtMB's own
engine/game-DLL/Python split in `game_runtime.md`, and to the debug architecture's three layers
in `debug-tooling.md`.)

| Layer | Contents | Rule |
|---|---|---|
| **Presentation** | UI, fonts, HUD, menus, typography, layout, iconography, textures/materials, post-processing, screen-space effects | **Modernize freely** within the art-direction test below. No approval gate. |
| **Feel** | Movement math, camera, input response, combat pacing, animation timing, audio mix | **Faithful first, polish by explicit call.** Build the RE'd original, keep it A/B-able, then propose deltas one system at a time. |
| **Logic & content** | Entity semantics, Source I/O, level scripts, dialogue trees, quests, stats, dice, save state | **Faithful.** Divergence only under the governing rule above. Bug-for-bug where the bug is load-bearing (e.g. error-to-false). |

The layer boundary is *game state*, not visibility: anything the player sees that carries no
game state is presentation; anything that changes what the game does is not. A modern font on
a sign is presentation. Making that sign readable at a distance the original did not allow is
a feel/logic question — ask.

## The two adjudication tests

**Presentation test** (extends `asset-enhancement.md`'s):

> Does it serve VtMB's art direction, or fix a technical deficit that fights it — or is it
> inventing/overriding an artist decision?

Serve or fix → in. Invent or override → out. The direction is specific and load-bearing:
**gothic-punk, grimy, wet, moody, deliberately un-glamorous.** "Higher fidelity" that
de-grimes, glamorizes or chromes the world fails even when it is technically better. Grime
*is* the style. A crisp, legible, well-set UI is not glamour — illegibility was never the
art direction, it was the hardware.

**Behaviour test** (for the feel and logic layers):

> Is this a 2004 **technical constraint or defect**, or an **authored design decision**?

Constraint or defect → may be fixed, with an owner's call. Design decision → reproduce, even
where it is unfashionable. An unclear case goes to the owner, not to taste.

## The four axes in scope

### 1. UI, HUD, fonts, menus — *re-skin: same structure, modern craft*

VtMB's UI **structure** is kept: the screen inventory and flow, panel anatomy, information
hierarchy, clan and faction iconography, the colour language, the diegetic framing (newsprint,
handwriting, blood). Its **craft** is replaced wholesale, because every weak part of it is a
2004 constraint rather than a decision.

Carried over — the design intent:

- Which screens exist and how the player moves between them (`.res` layouts and
  `trackerscheme.res` remain the **inventory of intent**: what a screen contains, what is
  emphasized, what is grouped).
- Panel anatomy and reading order, per screen.
- Palette, clan iconography, the paper/ink/blood material language of the popups and sheets.
- Every authored string, image, and sign/newspaper definition — read verbatim from the
  user's install, as always.

Replaced — the craft:

- **Bitmap `.fnt` glyph atlases → vector/SDF type.** The original fonts are unreadable at
  modern resolutions — the loudest defect in the game today. Typeface choices target VtMB's
  voice, not its bitmaps.
- **The 640×480 proportional VGUI space → resolution-independent layout.** Real widescreen and
  ultrawide, DPI-aware scaling, no letterbox canvas, no `//ws-fix` coordinate pairs. `.res`
  coordinates inform proportion and grouping; they are not the runtime layout engine.
- **Low-resolution UI bitmaps → upscaled or re-authored** at modern resolution, under the
  presentation test.
- **Static panels → restrained motion**: state transitions, focus, and feedback that a 2004
  VGUI could not express — this is a slow, moody game, not a kinetic one.
- **A modern component set**: focus/hover/disabled states, gamepad-navigable, text that
  reflows, tooltips where the original relied on the manual.

**There is no "classic UI" mode** — the pixel-faithful VGUI port is not built. The
VGUI/scheme/`.fnt`/`.res` decoders stay in `tools/` as extraction and reference machinery, not
a runtime layout stack. (`m0_menu_build.md` documents the original's structure and the
`GameUI.dll` findings; it is reference, not a port target.)

The **world**, by contrast, keeps its faithful path: geometry, placement, and lighting stay
anchored to VtMB's own data and the lightmap calibration, with enhancement as a toggle on top
(axis 2). Dropping the faithful UI does not license dropping the faithful world.

### 2. Assets — textures, materials, models

The `asset-enhancement.md` track is **in scope**: delight → super-resolve → style-anchored PBR
synthesis, tiered, curated per material family, budget-gated by the RTX 3060 / 12 GB floor. The
`elysium.EnhancedTextures` A/B toggle is the regression guard that keeps the enhanced set honest
against the faithful reference; the faithful set stays the reference forever.

Tier 0 (delight, super-resolve) is a technical-deficit fix for a dynamically-relit engine and
needs no special pleading. Tier 1 (normal/roughness/AO/envmask synthesis) is style-anchored
enhancement and is reviewed per family. Tier 2 (glossy/chrome, de-griming, "pop",
remodelling, invented hero detail) stays out of bounds.

### 3. Feel — movement, combat, camera

**Faithful first.** The Source `CGameMovement` math is ported line-by-line from the decompile
(`source_movement.md`) because it *is* the known-good baseline, and because you cannot tune
what you have not reproduced. Once it runs and is A/B-able, individual deltas — acceleration
curves, camera behaviour, combat readability, input latency — are proposed one at a time,
each under the behaviour test, each on an explicit call. Same for combat and camera when they
arrive.

Modern *plumbing* under identical behaviour is presentation-grade and needs no gate: frame-rate
independence, uncapped physics/render rates, high-polling-rate mouse input, gamepad support,
FOV control.

### 4. Quality of life & accessibility

Additive only — these change what the player can *configure and perceive*, never what the game
does. In scope:

- Full key/button remapping; gamepad support.
- Subtitles for all spoken content, with size and background controls.
- UI text scaling; colourblind-safe status colours; high-contrast option.
- Real graphics/audio options screens that back the settings the engine already has.
- Legible save/load and autosave affordances (the save *format* and its four blocks stay
  ours and deterministic — see `roadmap.md` 9.5).

Difficulty and balance are **not** QoL — they are the logic layer, governed by the one rule.

## What this direction does not change

- **Bring-your-own-game** (root `CLAUDE.md`) — modernizing the UI applies better craft to the
  user's own bytes, not shipped art.
- **The two clean halves** (root `CLAUDE.md`) — offline decode, runtime load; this direction
  does not change the split.
- **No `.uasset` baking of game content.**
- **The world calibration.** VtMB's look is indirect-bounce-dominated and the dynamic rig is
  anchored to its baked lightmaps (`rendering-perf.md`). That calibration defines the target
  look; richer surfaces respond to it, they do not redefine it.
- **`sp_tutorial_1` is the vertical slice** — the slice ladder in `roadmap.md` keeps the
  entity/IO/scripting spine landing before the UI work.

## Where the work lives

| Axis | Tracked as |
|---|---|
| UI foundation, menus, HUD, sign panels | `roadmap.md` P8 (8.6, 8.8, 8.9, 8.10) + P9 9.2 (dialogue UI) |
| Assets | `asset-enhancement.md`; scheduled from `roadmap.md` P10 |
| Feel | `roadmap.md` 4.7 (movement), 10.7 long tail (combat, camera) |
| QoL / accessibility | `roadmap.md` 8.10 |
| Every behavioural divergence | the doc that owns the system, marked as a divergence, owner-approved |
