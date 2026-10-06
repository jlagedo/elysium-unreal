# L2 — character

**Standing rule for every story:** follow retail behaviour. This spec owns its layer's rows in
`../audit.tsv`; a story names the rows it closes. What it needs from a lower layer is built there
first (`AGENTS.md`: missing inputs); what only a higher layer runs is left as a listed upward hook.
A story closes on its records green on the arena and its rows re-read against the port.

## Scope

- **Retail:** `CBaseCombatCharacter` with VtMB's stats, disciplines, blood and feeding wired in; weapons (`CBaseCombatWeapon` and its leaves); inventory, items, quests.
- **Work:** 396 open or partial core functions (104 KB of retail code) in 135 stories and 178 briefs; rpg 44, combat 41, weapons 30, inventory_ui_items 17, entity_core 2, parked 1.
- **Ready now:** 106 of 135 stories call nothing open below this layer (`schedule.md`).
- **Live paths first:** 6 stories hold stubs the baseline's arena fired (5,190 hits) — they lead the order.
- **Hooks:** 51 calls into higher layers stay as named hooks (`../hooks.tsv`).
- **Witnesses:** the arena, `sp_tutorial_1` and `sm_hub_1` (map records).

## The gate — the layer is done when

1. every story's records are green on one `uv run elysium arena` run, and a record that was
   intermittent is green in three boot orders;
2. every row of this layer in `../audit.tsv` is done, or a named divergence the owner accepted
   (`../decisions.md`);
3. the default and arm tiers have 0 failures, and every record that passed in `../baseline.md`
   still passes;
4. its upward hooks are listed, each with the layer that completes it;
5. its state-bearing engine-replaced rows ([`contract-checks.md`](contract-checks.md)) keep retail's
   inputs, outputs and event order, shown by a record, or are named divergences the owner accepted.

## Order

0. **The test instrument** — built once in L0 (`../harness.md`); this layer's stories add their
   own `retail_site` emit sites, `entity_field` names and `entity_call` entry points in the slice
   that ports them.
1. **The stories**, in [`stories-table.md`](stories-table.md): callees before callers, live paths
   first. Each has its file under `stories/` and its worker briefs under `briefs/`.

## Decisions that touch this layer

- Owner decision: choose whether CWeaponIGeneric::Kick preserves retail VPhysics force semantics or receives a named Chaos/Unreal physics modernization.
- Choose the Unreal asset-residency and socket mapping that replaces Source PrecacheSound/PrecacheModel and the retail particle attachment names. Keep retail parser order, payload names, defaults and event timing; record any chosen visual-only modernization explicitly.
- Decide whether `0x1024f840` may replace Source VPhysics and optional constraints with Unreal physics as a named modernization; the plan assumes retail branch selection, bounds, touch and fall-think timing are preserved.

## Stories added from the parked specs

- 0006-first-disciplines:87 — 10. RE53, the magnitude authoring.: [RE53 retail discipline magnitudes](stories/L2.parked.l2-rpg-discipline-magnitudes.md)
