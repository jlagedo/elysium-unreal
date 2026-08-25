---
name: gameplay-change
description: Implement or review a gameplay change in the C++ runtime — map entities, entity I/O, the script bridge, interactive props, NPCs, combat, the character sheet, saves, disciplines, items. Use when a task touches `Source/ElysiumUE/Private/Substrate/`, an `FElysium*` entity class, `AcceptInput`/`FireOutput`, `GNativeBindings`, a datamap keyfield or input, a rulebook table, or any domain service under `Substrate/`.
---

# Gameplay integration contract

`docs/architecture/gameplay-systems-architecture.md` is the governing design guidance — read it
before implementing. It composes the entity rules **R1–R8** in `docs/architecture/engine-core.md`,
the runtime rules **S1–S12** in `docs/architecture/runtime-architecture.md`, and its own
compatibility rules **K1–K13**. Exact VtMB behavior lives in the owning `docs/vtmb/` document,
and the ownership register that decides what may be reproduced at all is the repo-root
`CLAUDE.md` rule "Unreal owns the engine; the substrate owns the game".

When working in existing gameplay code, assess the touched code against that architecture and
propose concrete changes for every divergence needed to bring it into conformance. Implement those
corrections when they are within the requested scope; otherwise report them explicitly rather than
expanding the task without authorization.

## The coding contract

- **Keep one object language.** Gameplay identity and mutable map state live on plain-C++
  `FElysiumEntity` classes and the declared session/save structures. Unreal actors and components
  are optional bodies for rendering, collision, movement and overlap; they do not become a second
  gameplay model. The substrate reaches them only through nullable `FElysiumWorldServices`.
- **Preserve the addressability boundary.** Non-addressable GAME_LUMP dressing may be placed in the
  baked level. Anything a map or script can name, mutate, hide, use, save, receive an input on, or
  fire an output from remains a live `.ents` entity; baking its mesh must not bake away its entity
  identity. Runtime readers consume exporter-produced Unreal-native coordinates verbatim.
- **Add no fourth legacy API tier.** Tier 1 is the case-folded class-chain input/field surface;
  Tier 2 is the single retail-evidenced `GNativeBindings` table; Tier 3 is the console bridge. Map
  outputs, `logic_pythoncheck`, dialogue, `ScheduleTask` and level scripts converge on the installed
  script host and the same domain implementations. Do not add a per-surface adapter, catch-all
  dispatcher or duplicate native table.
- **Bind a name at its retail kind.** A datamap input stays a registered class-chain input, a
  Character method stays a Tier-2 row, and a script helper stays in `__main__`; the receiver may
  distinguish identical spellings. An unimplemented recovered input uses
  `ELYSIUM_PENDING_INPUT`; every other gap reports through the existing stub and wire-accounting
  funnels and returns the retail-shaped failure/default. Never silently succeed, invent a receiver,
  or repair authored defects without an explicit divergence in the owning VtMB document.
- **Use one event transport and preserve its order.** Real producers call `FireOutput` or enqueue
  owned work; only queue service delivers. Do not call a receiver synchronously from a producer,
  prebind a target name, or add `FTimerManager`, a latent action, a private timer/event list, or a
  second scheduler. The recovered synchronous Python reflected-input call still passes through
  `AcceptInput`; any outputs it fires rejoin the ordinary queue. Follow the exact row, deadline,
  equal-time, late-binding and service order in the gameplay architecture and `docs/vtmb/entity_io.md`.
  The documented determinism divergences are a closed set; any other ordering difference is a bug.
- **Keep the two Python systems separate.** `pipeline/` and Unreal editor Python are offline
  export/generation tools and never run to produce game content at runtime. Embedded CPython is the
  runtime host for the user's loose VtMB scripts. Level scripts load into the shared `__main__`
  before the entity spawn pass; Python entity attributes resolve through the same class chain as
  I/O. Scripts remain user-install data: do not commit a hand-fixed fork or grow a native binding
  without retail evidence.
- **Give every value and rule one owner.** Persistent state is a Save-flagged registered field, a
  session-record member, or a declared save block. Recovered catalogs load patch-first through the
  rulebook into typed tables; do not retype their constants into C++. Keep combat relationship,
  emotional disposition and RPG reaction separate; keep ratings separate from rolls; route typed
  damage through the shared descriptor/commit path; transfer NPC body control through its owner
  arbiter.
- **Land domains behind the existing seams.** A gameplay addition consists of class-chain
  fields/inputs/outputs, one plain-C++ domain service, declared command verbs where player-facing,
  and save state in an existing home. It does not add a dispatcher, clock, input owner, presentation
  backchannel or save path. UI reads published view state and sends intent through the command bus.
- **Prove the real producer path.** Use `elysium.stubs`, `elysium.classes`, `elysium.wires` and the
  I/O history to distinguish never produced, missing target, missing input, refused receiver and
  invisible side effect. A quiet log is not acceptance, and debug injection does not prove an
  authored event producer. Coverage rules are the `elysium-testing` skill.

## Reviewing the diff

Ask two questions: does it put a real implementation behind these existing entity/API/event/save
seams, or does it create another route around them — and does it use Unreal's mechanism for
everything the ownership register does not reserve? Only a change that passes both belongs in the
runtime.
