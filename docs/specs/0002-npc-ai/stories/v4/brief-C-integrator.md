# Brief — V4c's integrator (and V4's close)

Runs after C1 and C2 report. Read `README.md` here, both briefs, `packets.md` § R2, the judge's
rulings (`stories/v1/triage.md` § "Judge's rulings, V4"), `Arena/README.md`.

1. **Apply the cross-lane lines** the coders reported, nothing more.
2. **Build once** (`uv run elysium build`). Fix only integration breaks; a third build means stop.
3. **Records first** (you own `Arena/` edits): if R2 item 3c found `melee_swing`'s `hit_event` a
   record error, correct it with the retail source (bug protocol step 1); its `known_red` stays N3
   (V11).
4. **Run, by name**: `uv run elysium arena damage_lethal_death verbs_stealth_kill ranged_open_fire
   cover range_bands melee_swing chase_melee control_sequence verbs_feed_trance`. Then the family
   filters once: `uv run elysium test Elysium.Arm.NpcKernelAnim. Elysium.Arm.NpcKernelAnimEvents.
   Elysium.Substrate.NpcCombat. Elysium.Arm.NpcKernelRunTask19. Elysium.Weapon` (use the weapon
   tests' actual prefix).
5. **Acceptance** (README §4, V4c):
   - `damage_lethal_death`, `verbs_stealth_kill`: as the judge ruled on Q3 — under (b), green on the
     transaction (`death`, `OnDeath`, `corpse ragdoll`, the seed `sequence`), `known_red` removed;
     under (a) or "file for later", red with `known_red` naming the story that owns the fall.
   - `cover`, `ranged_open_fire`: the shot still comes from the event (`animevent` inside
     `task_range_attack1`); `ranged_open_fire` still red only on N2 (V5).
   - `melee_swing`, `chase_melee`: red only on N3 (V11).
   - `verbs_feed_trance`, `control_sequence`: green (the feed's 4006/4007 now go through the NPC's
     dispatcher when the NPC is the attacker; a moved verdict is triaged).
6. **V4's close**: the arm tier once (`uv run elysium test arm`), the whole arena once, the default
   tier once; the ledger step (`kernel --check`, the override census, `unported.tsv`); `divergences.md`
   row 4 marked closed with the commit; `spec.md` V4 ticked with a short landed note in the V3 style
   (sub-stories, records turned green, what moved where); `stories/v1/triage.md` § "The fix order"
   V4 row struck through as V3's is. Moved verdicts in `stories/v4/report-c.md`.
7. **Commit once**: `fix(npc): V4c -- the NPC shot from its event, Weapon_FrameUpdate, the weighted
   pick, SetDisposition, the die-ragdoll seed; V4 closes`. Do not push.

Rules: wait for a build or run by its completion notification, never a sleep or polling loop. The
query budget (10 s warns, 60 s stops; never a file over ~200 KB whole). Text through Grep / Read /
Glob. Report ≤300 words: the build's wall time, verdicts before and after, totals, what is left red
and where it is placed.
