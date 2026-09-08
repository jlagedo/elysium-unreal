# Elysium-Unreal — Vision

Elysium-Unreal is *Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine)
rebuilt as a playable game — **modernized** — on Unreal Engine 5.8 + C++, consuming
engine-neutral intermediates produced by this repo's own decode/export pipeline. Tone,
ambience, feel and game logic are kept; craft is raised with tools 2004 did not have. This is
not a pixel-perfect recreation: where the original made a choice, we keep it; where the
original was *constrained*, we are not.

This document is the single north-star: what Elysium is and is not, the rule that governs
every change, and the shape of the build. Dated owner calls and divergences live in
`docs/decisions.md`; open threads live under `docs/specs/`; the retail oracle is `docs/vtmb/`;
seam data formats are `docs/contracts/`.

## What Elysium is, and is not

Elysium is VtMB's world, entities, dialogue, quests, stats and feel, ported and modernized —
not a new game inspired by it, and not a faithful-to-the-pixel VGUI reproduction. The **world**
keeps a faithful path: geometry, placement and lighting stay anchored to VtMB's own data. The
**UI** does not: VtMB's screens are structural reference for a modern re-skin, never a port
target. Where a system is a 2004 technical constraint, Unreal's native equivalent replaces it;
where a system is authored design, it is reproduced, however unfashionable.

Per root `CLAUDE.md`, the port is a **VM host** for VtMB's data: schedules, dialogue and map
scripts are the bytecode; the C++ substrate is the interpreter. Anything the bytecode can
observe is reproduced verbatim — task semantics, condition order, interrupt timing, what a
failure writes, and bugs, because shipped programs were tuned against them. This document does
not loosen that framing anywhere it touches feel or logic; where older direction language was
softer, `CLAUDE.md` is the one that governs.

**Bring-your-own-game** holds throughout: nothing game-sourced is ever committed to this repo.
The export corpus is read from the user's own install at runtime and export time; legal
posture and rationale live in root `CLAUDE.md`.

## The one governing rule

> We only change what we understand, and only on an explicit call.

Reverse-engineering comes first — a system is decompiled, documented and reproducible *before*
anyone proposes improving it. "This feels off, let's redo it" is not a plan; "here is the
decompiled behaviour, here is why 2004 shipped it that way, here is what I want instead" is.

1. **Default is reproduce.** Absent an explicit decision, every system is built to match
   retail behaviour. Uncertainty resolves toward the original, never toward invention.
2. **Divergence needs the understanding precondition.** A behavioural change may only be
   proposed once the faithful behaviour is known (reverse-engineered or verified in-game).
   Ignorance is not licence.
3. **Divergence needs an owner's call.** The project owner approves each behavioural
   divergence explicitly, and it is recorded in `docs/decisions.md` — the faithful behaviour
   stated next to the chosen one, marked as a divergence. No silent drift.
4. **The reverse-engineering backlog does not shrink because of this rule.** Decompiling stays
   as valuable as ever — you cannot judge what to keep until you know what is there.

`CLAUDE.md`'s framing of modernization sharpens this rule for the substrate specifically:
modernization is a **peripheral swap**. Two halves qualify — a visual-only change (Unreal
renders it better; adopt freely), and an algorithm Unreal already ships, adopted only with the
retail contract and event sequencing kept. Nothing that changes event order or state is a
modernization, no matter how it is framed.

## Three change layers

Every change lands in exactly one of these, decided *before* the work:

| Layer | Contents | Rule |
|---|---|---|
| **Presentation** | UI, fonts, HUD, menus, typography, layout, iconography, textures/materials, post-processing, screen-space effects | Modernize freely within the art-direction test below. No approval gate. |
| **Feel** | Movement math, camera, input response, combat pacing, animation timing, audio mix | Faithful first, polish by explicit call. Build the reverse-engineered original, then propose deltas one system at a time. No A/B mechanism, feature flag, or state-enabling cvar without an owner approval by name. |
| **Logic & content** | Entity semantics, Source I/O, level scripts, dialogue trees, quests, stats, dice, save state | Faithful. Divergence only under the governing rule above. Bug-for-bug where the bug is load-bearing (e.g. error-to-false). |

The layer boundary is *game state*, not visibility: anything the player sees that carries no
game state is presentation; anything that changes what the game does is not. A modern font on
a sign is presentation. Making that sign readable at a distance the original did not allow is a
feel/logic question — ask.

## The three adjudication tests

**Presentation test:**

> Does it serve VtMB's art direction, or fix a technical deficit that fights it — or is it
> inventing/overriding an artist decision?

Serve or fix → in. Invent or override → out. The direction is specific and load-bearing:
gothic-punk, grimy, wet, moody, deliberately un-glamorous. "Higher fidelity" that de-grimes,
glamorizes or chromes the world fails even when it is technically better. Grime *is* the style.
A crisp, legible, well-set UI is not glamour — illegibility was never the art direction, it was
the hardware.

**Behaviour test** (for the feel and logic layers):

> Is this a 2004 technical constraint or defect, or an authored design decision?

Constraint or defect → may be fixed, with an owner's call. Design decision → reproduce, even
where it is unfashionable. An unclear case goes to the owner, not to taste.

**Ownership test** (for every system — which half builds it, decided before the work):

> Does authored content or a game rule name this, or is it something the world merely needs in
> order to work?

Named by content → game logic → reproduced in the substrate, however Unreal would model it.
Needed by the world → engine service → Unreal owns it, and the substrate reaches it through a
query on the service seam. "Named by content" is decidable, not taste: a keyfield, an entity
input or output, a `.dlg` condition, a script call, a rulebook row, a save field, or a timing
the player can observe. Everything else — traces, visibility, pathfinding, physics solving,
skinning, audio mixing, culling, streaming — is a service Unreal already provides, and porting
Source's version of it is a defect. Troika's source is the reverse-engineering oracle for rules
and data, never an implementation to port.

The change layer decides the default; this test decides the mechanism. A feel-layer system
reproduces Source's **rules** — formulas, call order, thresholds — because those rules *are*
the feel; it never reproduces Source's **mechanisms**. Before any Source subsystem is
reproduced, name the observable that requires it — the authored value, the script-visible name,
the save field, the measurable feel. If Unreal's own equivalent produces that observable, with
any divergence enumerated where the two disagree, the port is refused.

## The build shape

Two clean halves, joined only by files on disk:

- **Offline, decode.** Python decoders under `pipeline/` decode VtMB's proprietary formats
  (BSP, MDL, TTH/TTZ, VPK, VMT, `.fnt`, `.res`, and more) into portable, engine-neutral
  intermediates — OBJ+MTL+PNG/DDS, skeletal containers, and plain-text sidecars — read from the
  user's own install and written to a gitignored, regenerable export tree.
- **Offline, bake.** A second offline stage, run by the Unreal editor as a commandlet, turns
  each exported map's *look* — geometry, materials, textures, props, decals, lights, fog, the
  level itself — into real Unreal assets on a gitignored, regenerable baked mount. No original
  game content is ever committed in any form, in either stage.
- **Runtime.** The C++ runtime module opens the baked level and adopts its actors by tag, then
  builds everything else in code from the intermediates on disk: collision, ropes, the sky,
  the entity substrate, NPCs, audio, scripting. Python never runs at runtime — the seam between
  the halves is purely file-based.

Within this shape, two tracks describe the work by what they draw on, not by schedule:

- **World/rendering parity** — reaching VtMB's own rendering and walking state (world, props,
  lights, water, decals, movement). Every system here has an exact data format exported by this
  repo's own pipeline to build from. The UI is not a parity target: VtMB's own screens are
  structural reference for the modern re-skin, not a thing to reproduce.
- **The game layer** — entities, Source I/O, triggers, movers, +use, travel, NPCs, scripting,
  dialogue, the main game loop. This layer exists only as reverse-engineering documentation and
  exported data; Unreal is the lead implementation here, designed Unreal-native from day one.

The Unreal editor sits in the offline development/content loop only, never at runtime — editor
Python authors what only the editor build can produce, while designers author original
project-owned packages that stay tracked in the repo. No shipped code path invokes editor
Python.

The exact sidecar file formats the runtime consumes (entities, geometry, lights, props, water,
decals, audio, and the rest), and the coordinate conventions they share, are a seam contract,
not vision — see `docs/contracts/`.

## The playable path

The direction for what a player can walk through end to end: from the main menu, through New
Game, into the opening theatre scene, through the tutorial, and out into the first open hub.
Each stage exercises the substrate in a different way — menu and New Game prove the modern UI
foundation and save/character setup; the theatre proves scripted sequences, camera direction
and dialogue; the tutorial proves the entity/I-O/scripting spine end to end in a dense,
self-contained space; the hub proves that spine at open-world scale with travel between maps.
This is a direction for sequencing effort, not a status report — what is currently playable is
tracked outside this document.

## What this vision does not change

- **Bring-your-own-game** and **the two clean halves** (root `CLAUDE.md`) — this document
  narrates them, it does not amend them.
- **The world's faithful calibration.** VtMB's look is indirect-bounce-dominated; richer
  surfaces respond to that calibration, they do not redefine it.
- **The governing rule applies uniformly.** Presentation is the one layer modernized without a
  gate; feel and logic are never modernized without the reverse-engineering being done first and
  an owner's call recorded.
