# Handover — spec 0002 (the character AI), coordinator's state on 2026-10-04 (evening)

You are the coordinator for spec 0002 in `E:\dev\elysium-unreal`. Load this file first, then read
what it points at. It replaces the conversation that produced it.

## Where things stand (2026-10-04, 23:20) — read this first

**Wave V4c is in integration and the tree is NOT clean.** About 71 modified and 8 new files are
uncommitted: the three coders' lanes (C1 attack producers, C2 pick / corpse / dead enemy, C3 the
team registry) plus whatever the integrator has applied. HEAD is `b430dc37` plus this file's
commit; nothing pushed since `2ac33a53` (`origin/main`).

Two Codex runs were alive when this was written. Their folders are under `E:\elysium-work\codex\`
(`brief.md`, `events.jsonl`, `last.md`, `stderr.log`); a run is over when `last.md` exists and
`events.jsonl` ends with `turn.completed` (or `turn.failed`: then read `stderr.log`).

| run | what | thread id (for `codex exec resume`) |
|---|---|---|
| `C-int` | V4c's integrator (`gpt-6.1-sol`, high): applies the owed lines, one `build --arm` (cap two), the records, both test tiers, the full arena, one commit. Started 23:07 | `01a109d1-1882-7332-a4e2-36ba50c4f7ac` |
| `V4d-plan` | verifies the V4d scout and makes `brief-D-ragdoll.md` final; writes `brief-D-integrator.md` and `packets-S14.md` (docs only). Started 23:08 | first line of its `events.jsonl` |

**First steps for whoever picks this up:**

1. `git log --oneline -3` and `git status --short`. If the integrator committed (a `feat(npc): V4c`
   commit), read its message: the verdict table, what the three `damage_*` records measured about
   the knockout, each red with its owner. If it did not commit, read `C-int/last.md`: it was told
   not to commit a wave that is not green and to say what state the tree is in. Resume its thread
   (model, effort and the bypass flag repeated) rather than starting a new integrator.
2. If `V4d-plan` finished, check its packet against the listing, commit its three files, and
   launch V4d's coders (`medium`) once V4c is committed.
3. A Codex run can die on an expired login ("refresh token has expired", HTTP 401; it happened to
   C1 and a scout). The fix is the owner's: `codex login`. Then resume the thread.

**The coders' reports** are in `E:\elysium-work\codex\C-int\report-C1.txt`, `-C2.txt`, `-C3.txt`;
the lines they owe other files are written as exact patches in `docs/vtmb/combat-and-damage.md`
(~:1228), `docs/vtmb/npc-ai/lifecycle.md` (~:3418), `senses.md` (~:256) and `teams.md`.

**Scout packets** (`gpt-6-luna` at `max`; leads, NOT verified, outside the repository):
`E:\elysium-work\codex\scout-V4d\packet.md`, `scout-V6`, `scout-V7`, `scout-V10V12`. Their main
claims, each to be checked before a brief uses it:

- V4d: an ordinary death may never reach `StartBodyRagdoll` today (capability false, force bone
  none, no `Bip01 Spine2` fallback); the `.phy` solid-to-bone frame mapping is unresolved; no
  cleanup of the map-owned ragdoll mesh; 34 distinct character models on the two witness maps.
- V6: the clock persists across map travel; there is no save / load action in the console or the
  arena harness (V6's records cannot be staged until one exists); a restored corpse can lose its
  removal think; the task cursor is `+0x5c40` (`+0x5c50` is the failure code).
- V7: none of the 19 inputs is fired by either witness map (records go in the Green Room); N5
  forwards the raw value; N6 starts the schedule at once where retail stores a forced-schedule
  state; N11 omits retail's stealth eligibility arms; the movement-multiplier input needs the
  shared playback / ground-speed scalar path.
- V10 + V12: N4's cause — retail's `CanHearSound 0x1030f7b0` has no expiry test (a separate think
  prunes at expiry + 4 s) while the port tests expiry inside hearing; retail's NPC footstep
  handler emits body audio and inserts no AI sound, so V12 may be small.

**Order left:** V4c (integrating) → V4d → V5b (planned, `stories/v5/`) → V6 → V7 → V10 → V12 → the
second full run → stop before V8 (the owner live) → V9 → gate 2.

## Read, in this order

1. `AGENTS.md` (repo root) and the owner's global `~/.claude/CLAUDE.md` (the query budget).
2. `docs/specs/0002-npc-ai/spec.md` — § Standing rules, § Step 2, § The bug protocol and, after it,
   the owner's standing rulings and his two rules of 2026-10-04 (**Testable first**, **Settle
   first**), § The sequence.
3. `docs/specs/TRACKER.md` — what is ticked. Next unticked: **V4c** (in integration), then V4d.
4. `docs/specs/0002-npc-ai/stories/v4/README.md` and the packets beside it: `packets-R1.md`,
   `-R2`, `-spike`, `-R1b-measurement`, `-S1` … `-S12`. Briefs: `stories/v4o/` (O1–O3, integrator),
   `stories/v4/brief-C1…`, `brief-C2…`, `brief-C3-team-registry.md`, `brief-C-integrator.md`,
   `brief-D-ragdoll.md`, `packets-S13.md` (settled before V4c),
   `stories/v5/` (the V5 plan), `stories/v5a/`, `stories/v11/` (landed; for reference).
5. `docs/specs/0002-npc-ai/stories/v1/triage.md` — the two sections "Judge's rulings, V4" and
   "Judge's rulings, V4 — second sitting" at its end.
6. `Arena/README.md`; `git log --oneline -20`.

## State (verified at V4o's commit `64895278`)

- **Branch `spec-0002/step-2`**, HEAD `64895278` plus this file's commit; nothing pushed.
- **Arena:** 124 records — 102 pass / 19 expected-fail / 1 fail (`rollcall_vzombie`, H11) / 2
  unexpected-pass (`hear_world_investigate`, `interest_mode_never`, intermittent on N4).
- **Tests:** default 170 / 0; arm 1,594 / 0; `kernel --check` 7/7.

| step | state | commit |
|---|---|---|
| V4r: readers R1, R2, the ragdoll spike, the judge (J1–J9) | closed | `65220814` |
| settling packets S1–S12, the judge's second sitting (J2b, J10–J14) | closed | `273a7dcc` … `08ff15c8` |
| the seam (A0): words, probes H18–H22, the V4 records | closed | `d7afee0d` |
| V5a + slot 363 (pulled forward) | closed | `a6bd4add` |
| V4a: the dispatcher in each entity's think, the clock's speed words, N19 | closed | `c7a2645c` |
| V4b + slot 562: retail's arrival script, the facing; N13 fixed | closed | `1442fdc2` |
| V11 (pulled forward): the coordinator, slot 331, the melee contact, the grapple words | closed | `88649932` |
| the V5 plan (V5b) | written | `ed91e53a` |
| V4o (pulled from 0015): layers, move-and-shoot, the event shot | closed | `64895278` |
| **V4c**: attack producers, the weighted pick, the death transaction, the corpse clocks | not started; briefs final | — |
| **V4d**: the ragdoll from the `.phy` | not started; spike done, brief final | — |

Order left: **V4c → V4d → V5b → V6 → V7 → V10 → V12 → V2 (full run) → V8 → V9 →
gate 2.**

## What moved green this session

`range_bands`, `cover_armed`, `sense_enemy_facing_me`, `ranged_sustained_fire` (V5a);
`script_walk_to_mark` (N19, V4a); `input_clearpatrolpath`, `patrol_sentry2_pingpong`,
`places_pedestrian_visit` (N13, V4b); `melee_swing`, `melee_ally_in_the_way`, `patrol_monk_loop`
(V11; the monk was a record error: the player's seat stood on the node). New and green:
`ranged_friend_in_line_of_fire`, `verbs_feed_victim_dispatch`, the player / prop anim-event
guards, `corpse_removed_unseen`, `corpse_kindred_burns`, `corpse_kept_seen`.

`cover_move_shoot` and `ranged_open_fire` went green in V4o.

Still red, with owner: `chase_melee` (the swing
clip's movement never moves the body), `corpse_pedestrian_stays`, `corpse_fades` → V4c;
`damage_lethal_death` → V4d; `rollcall_vzombie` → H11.

## The judge's rulings (all in the triage)

| # | item | ruling |
|---|---|---|
| J1 | N19: a missed sequence lookup plays sequence 0 | done, V4a |
| J2 → J2b | slot 247's bbox import | withdrawn and filed (only the player's acquire cone reads it; that story is unported); the slot body stays on a named seam, arm test only, V4c C1 |
| J3 | the player's anim-event dispatch | site and order done (V4a A4); the player's own sequence clock filed to 0015, a named pose-phase seam stands for it |
| J4 | props and the camera | prop poll deleted, no replacement (retail props never dispatch); camera dispatch and its handler done |
| J5 | the move-and-shoot overlay | first "stub"; superseded by "testable first": pulled forward as V4o |
| J6 | the shot-timer estimate | removed for every NPC wielder (S2 read all operator bodies); V4o / V4c C1 |
| J7 | record errors (`melee_swing`'s `hit_event`, N13's speeds per body) | corrected by the seam |
| J8 | three doc conflicts | fixed by their lanes |
| J9 | the arrival | only retail's whole velocity script counts: done, V4b; the arrival floor is retail's 0.0625 units (V11) |
| J10 | `0x102a11d0`, a non-hated NPC blocks the swing | done, V11 |
| J11 | four species with task-code fire (ChangBros, FrenzyShadow, Bach, ManBat) | filed; on neither witness map |
| J12 | the NPC clip is never lowered by a shot | done, V4o; the reload is V5b's |
| J13 / J14 | corpse removal by kind; the fade (25 makers on both maps) | V4c C2 and V4d, four records; the burn's look to 0014 |

## Open, for the next session or the owner

- **New from V4o, for V4c:** a gunman does not drop a dead enemy (it re-selects `START_COMBAT` on
  `ENEMY_DEAD` every think) → C2. The shot-timer estimate (`BeginRangedShot`, the NPC arm of
  `CommitArrivesFromAnimEvent`) is still in the code, unreached by any NPC task → C1 deletes it.
  NPC bullets now land: three ranged records hold the player unkillable through `events_player`.

- **The owner's rulings of 2026-10-04 (evening), do not re-ask:**
  - K4: NPC and non-NPC animation picks draw on **one shared stream**, as retail's single engine
    stream (V4c C2 routes them).
  - The real `TASK_RELOAD` (slots 322 / 323) is **ported in V5** with arm tests, plus a Green Room
    record with a flamethrower NPC so it gets a real run.
  - The melee contact's same-team test (`+0x10b0`): the team registry is **built in V4c** (read
    the field's writers first; a Green Room record of two same-team NPCs). One more V4c lane.
  - `main` was fast-forwarded to the work branch and pushed.
- Presence's rate-doubling is filed with 0006.
- **Seams left, named:** the same-team test of the melee contact (`+0x10b0`; no team registry);
  Ming Xiao's `GetBestMeleeWeapon`; slot 389's `muzzleflash` attachment; the turn script's arrival
  direction (`path+0x64`); `MoveNormal`'s restore arm (`0x102efc11`); the crowd corridor has no
  corners for the velocity script; the body turns to the facing point whole during a live move
  (inside K1, the motor on the body's tick).
- **Not read:** bytes `0x1026a233..0x1026a29f` (not indexed); which damage sources set the gib
  bit; whether the corpse maker passes spawnflag bit 9 (`corpse_fades` depends on it: C2 reads it
  first).
- **Noted, unowned:** an idle row in the feed victim's first think after release; the arena
  player dies to a few bat hits; four dangling citations fail `test_oracle_citations`; 18 other
  pytest failures that look like terminal colour codes (unconfirmed).
- `anim_prop_event` is vacuous by data (no placed prop authors an event; it proves the deleted
  poll only).

## The method — what this session added

- **Settle first:** every "unrecovered" in a brief or a coder's report gets a read-only reader
  (packets `S*`) before the lane that depends on it; readers run beside builds.
- **Testable first:** a record that cannot go green for want of a later story pulls that story (or
  its needed part) forward; the planner checks dependency cycles.
- **Integrators exceeded the two-build cap** four times (shadowed locals C4458/C4459, missing
  includes, fixes found during the run). Tell every coder shadowing is an error; tell every
  integrator to read all diffs for shadowing, includes and double definitions before build 1.
- Generated slot files (`…Slots.cpp`) are never hand-edited: a hand body goes in `…SlotBodies.cpp`
  with a `kernel_verdicts.tsv` row, then regenerate and `uv run elysium research kernel --check`.
- A coder's report always lists lines owed by other files; the integrator's prompt carries them.
- Everything else as before: ≤3 coders on disjoint files, then one integrator; coders never
  build; stage by explicit path; the verdict table in the commit message (a hook refuses
  `report*.md`); never push.

## Sub-agents run on Codex CLI (the owner's ruling, 2026-10-04)

**Load the `codex-cli` skill first** (`~/.claude/skills/codex-cli/SKILL.md`): it is the method —
the launch line, the sandbox, MCP approval, reading the result, resuming. It may not appear in a
session's skill listing; read the file. What follows is only what this project adds.

- **Model and effort (the owner):** `-m gpt-6.1-sol`; `high` for Opus-level work (integrators,
  readers over the listing, planners, judges), `medium` for Sonnet-level work (single-lane coders,
  doc amendments, small reads). Always pass both, on a resume too (neither carries over).
  The skill's benchmark found multi-function walks incomplete at `high` on a smaller model: a
  reader's packet is a lead, verified before it reaches code (as every packet has been).
- **No sandbox, ever (the owner): a sandboxed run errors.** Every `codex exec`, `resume` and
  `review` carries `--dangerously-bypass-approvals-and-sandbox`; no `-s`, no `sandbox_mode`.
- **Where a run lives:** `E:\elysium-work\codex\<slug>\` — `brief.md` (a copy of the brief; the
  tracked brief stays under `docs/specs/…/stories/`), `events.jsonl`, `last.md`, `stderr.log`.
- **Launch in the background, the brief on stdin** (`- < brief.md`). A positional prompt with
  stdin open hangs forever (it cost an hour here). Check `events.jsonl` within a minute of the
  launch; then wait for the completion notification, never poll.
- **After a run meant to be read-only, `git status`** (full access: the brief's sentence is the
  only guard). Every brief keeps the guardrails: the files named per lane, coders never build,
  stage by explicit path, never push.
- **Before an integrator's first build:** `codex exec review --uncommitted "<what to look for>"`
  for shadowed locals (C4458 / C4459), missing includes and double definitions — the causes of
  every over-cap build so far.
- Models on this machine today (`~/.codex/models_cache.json`): `gpt-6.1-sol`, `gpt-6-astra`,
  `gpt-6-sol`, `gpt-6-luna`, `gpt-5.6-*`, `gpt-5.5`; the skill's table predates 6.1.
- A turn starts at about 20,000 input tokens (the global config's plugins and MCP servers), mostly
  cached; drop unneeded servers per run with `-c 'mcp_servers.<name>.enabled=false'`, never
  `--ignore-user-config`.
