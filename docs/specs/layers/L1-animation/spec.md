# L1 — animation

**Standing rule for every story:** follow retail behaviour. This spec owns its layer's rows in
`../audit.tsv`; a story names the rows it closes. What it needs from a lower layer is built there
first (`AGENTS.md`: missing inputs); what only a higher layer runs is left as a listed upward hook.
A story closes on its records green on the arena and its rows re-read against the port.

## Scope

- **Retail:** `CBaseAnimating`, `CBaseAnimatingOverlay`, `CBaseFlex`: the studio model, sequences and activities, anim events, pose parameters, bones and hitboxes, layers, flex.
- **Work:** 56 open or partial core functions (15 KB of retail code) in 20 stories and 29 briefs; animation 19, physics 1.
- **Ready now:** 15 of 20 stories call nothing open below this layer (`schedule.md`).
- **Live paths first:** 1 stories hold stubs the baseline's arena fired (90,430 hits) — they lead the order.
- **Hooks:** 6 calls into higher layers stay as named hooks (`../hooks.tsv`).
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

- Owner decision: whether Unreal skeletal evaluation may supply gameplay SetupBones and cache matrices after matching Source's mask, cache keys, and event timing, or whether the Source evaluator must remain separate. The current wield path reads the drawn frame and differs from the retail flags-filtered server merge.
- Owner decision: whether the player's visual clip phase remains a named modernization in place of the retail StudioFrameAdvance clock; that choice affects sequence and event timing.
- Owner decision: name the eye-rig blink envelope as a visual-only modernization, or require the retail m_blinktoggle state to drive the blink path.
