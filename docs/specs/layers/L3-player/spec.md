# L3 — player

**Standing rule for every story:** follow retail behaviour. This spec owns its layer's rows in
`../audit.tsv`; a story names the rows it closes. What it needs from a lower layer is built there
first (`AGENTS.md`: missing inputs); what only a higher layer runs is left as a listed upward hook.
A story closes on its records green on the arena and its rows re-read against the port.

## Scope

- **Retail:** `CBasePlayer`, `CHL2_Player`, `CGameMovement`: movement, input, the player's view, use, the player's weapons and disciplines.
- **Work:** 178 open or partial core functions (56 KB of retail code) in 49 stories and 77 briefs; player 46, entity_core 1, combat 1, physics 1.
- **Ready now:** 18 of 49 stories call nothing open below this layer (`schedule.md`).
- **Live paths first:** 0 stories hold stubs the baseline's arena fired (0 hits) — they lead the order.
- **Hooks:** 17 calls into higher layers stay as named hooks (`../hooks.tsv`).
- **Witnesses:** the arena, `sp_tutorial_1` and `sm_hub_1` (map records).

## The gate — the layer is done when

1. every story's records are green on one `uv run elysium arena` run, and a record that was
   intermittent is green in three boot orders;
2. every row of this layer in `../audit.tsv` is done, or a named divergence the owner accepted
   (`../decisions.md`);
3. the default and arm tiers have 0 failures, and every record that passed in `../baseline.md`
   still passes;
4. its upward hooks are listed, each with the layer that completes it.

## Order

0. **The test instrument** — built once in L0 (`../harness.md`); this layer's stories add their
   own `retail_site` emit sites, `entity_field` names and `entity_call` entry points in the slice
   that ports them.
1. **The stories**, in [`stories-table.md`](stories-table.md): callees before callers, live paths
   first. Each has its file under `stories/` and its worker briefs under `briefs/`.

## Decisions that touch this layer

- Owner decision: retain or remove the already named transform-flattening modernization at 0x100b7e40; if removed, player eye angles must compose with the move parent's transform.
- Owner decision: choose the Unreal-side replacement for VPhysicsShadowUpdate at 0x1017b8a0 and ShouldSavePhysics at 0x1017be30, with the retail transform, velocity, collision, and save ordering as its contract.
- Owner decision: confirm the scope cut for the six CHL2 suit-device and suit-power rows; the recovered player document finds no VtMB datamap or authored-content producer for them. The rows stay unplaced unless that scope changes.
