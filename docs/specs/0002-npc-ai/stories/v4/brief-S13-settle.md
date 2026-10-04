# Brief S13 — settling read before V4c (one Codex reader, high effort)

You are settling reader S13 for spec 0002 in this repository. Read `AGENTS.md` first and follow its
query budget (10 s warns, 60 s is a hard stop per query; never read a file over ~200 KB whole).
**Strictly read-only** on source code, `Arena/` and existing docs: you write ONLY the new file
`docs/specs/0002-npc-ai/stories/v4/packets-S13.md`. Do not build, test, bake, run the arena or the
game, and do not commit.

The owner's rule: retail behaviour that is unrecovered is settled by a reading before the work that
depends on it. Settle each item from the retail listing with the `vtmb-corpus` MCP tools
(`vtmb_where`, `vtmb_func`, `vtmb_code`, `vtmb_asm`, `vtmb_callers`, `vtmb_fields`, `vtmb_readers`,
`vtmb_slot`, `vtmb_vtable`; `uv run elysium research where|section <addr|name>` first), then compare
with the port by reading under `Source/ElysiumUE/Private/` (search by function name). For every
answer give addresses and mark it *verified* (read in the listing) or *inferred*.

1. **The corpse maker and spawnflag bit 9.** Context: `packets-S4.md` (J13/J14: fading corpses, 25
   makers on both witness maps, `SUB_FadeOut 0x10269960`) and `packets-S12.md` (where C2 should read
   it). Does the maker (the `npc_maker` family) pass spawnflag bit 9 (`0x200`) to the child, which
   code reads it at death, and what does the corpse do with and without it (fade vs `SUB_PVSRemove
   0x102696f0`)? State what the record `corpse_fades` must stage.
2. **The melee contact's same-team test, field `+0x10b0`** (retail `MeleeSwingStep 0x10343020`; the
   port's seam `ElysiumSwingSameTeam` answers false; `packets-S5.md` item 3, D1): what the field is
   (name, type), every writer (keyfield, input, code), every reader, and its default for the NPC
   classes on the two witness maps (`sm_hub_1`, `sp_tutorial_1`). Design input for a "team
   registry" lane: the minimal port state and the functions to port.
3. **A gunman does not drop a dead enemy.** In the port it re-selects `START_COMBAT` on
   `ENEMY_DEAD` every think (found in commit `64895278`; read `git show -s 64895278`). Recover
   retail's chain when the enemy dies: who raises `ENEMY_DEAD`, who clears the enemy
   (`GatherEnemyConditions 0x10270b20`, `ChooseEnemy`, `SetEnemy(NULL)`, the enemy memory's
   handling of a dead entry), which schedule `SelectSchedule` picks next and how the combat state
   ends (`SelectIdealState 0x1026f660`). Find the port's divergence and give the exact fix (file,
   function).
4. **`TASK_KNOCKOUT` and `ONE_HIT_KILL`.** A test victim died on its fifth hit carrying 18 damage
   against 100000 health; the integrator inferred `TASK_KNOCKOUT`'s `ONE_HIT_KILL` (not measured).
   Read retail's knockout path (the task's start arm, the condition or flag that makes a hit
   lethal, `OnTakeDamage_Alive`'s arms that kill regardless of health) and the port's (search
   `TASK_KNOCKOUT`, `ONE_HIT_KILL`, `Knockout` under `Source/`). Verdict: is a death at 5 hits × 18
   retail behaviour for that staging, or a port damage bug? Give the exact condition.
5. **The feed victim's first think after release shows an idle sequence row** (commit
   `88649932`'s message; `packets-S10.md`: `LeaveGrappleState 0x10329a70` writes no activity,
   sequence, state or schedule; while grappled `RunAI` is skipped). What does retail's victim do
   on its first think after the grapple ends (which activity / sequence is committed, by whom:
   `RunAnimation 0x1026c540`'s idle re-pick, `MaintainActivity`, the mesmerized schedule's first
   task)? Is an idle row there retail's, or a port defect; if a defect, the fix.

End the packet with **"Changes to the plan"**: for each item, which V4c lane must act (C1 attack
producers, C2 pick / disposition / corpse, a new team-registry lane C3) and the exact instruction,
the arena record that proves it (name, staging, expect / never), or "none".

Final message: one line per item (settled or not, the answer) and the plan changes, under 300 words.
