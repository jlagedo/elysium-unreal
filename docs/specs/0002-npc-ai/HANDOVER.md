# Handover — spec 0002 (the character AI), coordinator's state on 2026-10-04

You are the coordinator for spec 0002 in `E:\dev\elysium-unreal`. Load this file first, then read
what it points at. It replaces the conversation that produced it.

## Read, in this order

1. `CLAUDE.md` (repo root) and the owner's global `~/.claude/CLAUDE.md` (the query budget).
2. `docs/specs/0002-npc-ai/spec.md` — § Standing rules (1–8), § The known reds, § Step 2 (V1–V13,
   the V3 and V4 cuts and the owner's rulings under them), § The bug protocol and the owner's
   standing rulings at its end, § The sequence.
3. `docs/specs/TRACKER.md` — what is ticked. Next = the first unticked box: **V4r**.
4. `docs/specs/0002-npc-ai/stories/v4/README.md` — the V4 design (read § 7 and § 8 as amended:
   K3/K4 are fixed in V4, K5 is the ragdoll modernization, Q3 is settled) and the briefs beside it:
   `brief-R1-walk-reader.md`, `brief-R2-chain-reader.md` (extended), `brief-J-judge.md` (item 1
   withdrawn; N19 is item 4), `brief-A0-seam.md` (extended: the player / prop anim-event records,
   H21), `brief-A1..A3`, `brief-A-integrator.md`, `brief-B1`, `brief-B2`, `brief-B-integrator.md`,
   `brief-C1`, `brief-C2` (amended), `brief-C-integrator.md`, `brief-D-ragdoll.md` (V4d).
5. `docs/specs/0002-npc-ai/stories/v1/triage.md` — the reds (N1–N21), the questions, the judge's
   rulings; the newest sections are at the end.
6. `Arena/README.md` and `docs/specs/0002-npc-ai/stories/wave2/seam.md` — the record schema and the
   trace kinds.
7. `git log --oneline -25`.

## State

- **Branch `spec-0002/step-2`**, working tree clean, HEAD `8343da49` plus this file's commit.
  28+ commits ahead of `origin/main`; nothing pushed since `209b0567` (the owner pushes).
- **Ticked:** step 1 whole (T1–T6, T6b, gate 1); V1, V2, H, V3 whole (V3r, V3a–V3d), V13.
- **Suite** (`uv run elysium arena`, last full run under T6b): 106 records — 75 pass, 30
  expected-fail, 1 fail (`rollcall_vzombie`, harness gap H11). `interest_mode_never` and
  `hear_world_investigate` are intermittent on N4 (the sound-expiry race, V10's).
- **Tests:** default tier 171 / 0 failed; arm tier 1,541 / 0; `kernel --check` clean.
  `Elysium.Content.NavArea.*` green (hub and tutorial re-baked with the nav marks registered and
  every mark including the agent height).
- **Build:** a one-`.cpp` edit ~9 s; a kernel-header edit 72–100 s (p90 of the mix 81.7 s).

## What landed since the spec's last re-plan (one line each)

- V3: the body arbiter is deleted whole (zero uses of its names). The kernel's sequence plays on
  every body; places and patrols are programs (the ambient executor is gone); scenes run through
  `m_scriptState` and `SCHED_AISCRIPT`; dialogue is a program (`0x6d` / `0x6e` / `0x6a`,
  `TASK_RUN_DIALOG 0xb9`, one `OnDialogEnd`).
- V13: the nav marks register with the navigation system; pedestrians wait at a red crosswalk and
  cross on green; unlinked doors are walls on the two witness maps.
- Old bugs fixed: `OnLooked`'s SEE clear; the last-known position's field (`+0xc`); the use focus
  on an NPC's own capsule (N18); the place pick's port-only class test (N15); the pipeline's door
  test by the hull's own box (N20); the map host seeded at boot (H17).
- T6b: three include cuts; T6 ticked.

## Next: V4, the animation chain — planned, not started

Order: **V4r → (V4d spike) → V4a → V4b → V4c**, with **V4d** any time after V4a.

| step | what | notes |
|---|---|---|
| V4r | readers R1 (the walk, N13's cause; one lab session), R2 (the chain; the player / prop / camera dispatch; the visual picks), one short read (does a rig-less ordinary NPC ever reach `SCHED_DIE`: `SelectSchedule`'s state-7 fork against `0x1032c404`), then the judge (N19; slot 247's bbox and N13-in-the-bake only if raised) | read-only; no build |
| V4d spike | `regular_cop` with Unreal's default physics asset in a scratch state, only the movement capsule off on the dead body, `damage_lethal_death`: does the body fall and rest | one build; nothing committed but findings in `stories/v4/packets.md`; run it after R1's lab session |
| V4a | the seam (A0), then A1 dispatcher, A2 clock words, A3 view cone, the integrator | records: `sense_enemy_facing_me`, `anim_player_footsteps`, `anim_player_weapon_event`, `anim_prop_event` |
| V4b | B1 body speed, B2 move-yaw and facing, the integrator | the three patrols, `places_pedestrian_visit`, `face_enemy_turn` |
| V4c | C1 attack producers, C2 pick / disposition / the death transaction, the integrator | `script_walk_to_mark` if N19 lands; `ranged_open_fire` stays on V5, melee on V11 |
| V4d | the `.phy` → physics-asset builder, its bake step, the dead body's collision fix, the integrator | `damage_lethal_death`, `verbs_stealth_kill` with `corpse_on_floor` |

After V4: V5 → [V11 + V6] → V7 → V10 → V12 → the second full run → V8 (both maps played live,
with the owner) → V9 → gate 2. A V5 plan is the usual closing item of a V4 session.

## The owner's rulings to carry (all in the spec; do not re-ask)

- **Retail first.** The code follows retail's listing; the data is the installed corpus (Unofficial
  Patch included); nobody loads unpatched maps.
- **Bugs:** an old bug in landed work is fixed to retail without waiting. A bug that needs a later
  phase, the pipeline or a re-bake goes to an **adversarial judge agent** (argues against, then
  rules: implement now / stub / file for later; the ruling and its counter-argument go in the
  triage). A new divergence is recorded and not adopted; the work goes on around it.
- **Rule 2 is strict:** a divergence stands only where retail cannot be followed. "Out of this
  story's scope" is not a divergence — it is unported work with an owner.
- **V4 rulings:** the world-tick event poll and the hash-seeded pick are fixed in V4 for every
  animating entity, proven by arena records (the prop record stands in the Green Room from any
  corpus prop). The corpse's fall is a named modernization: Unreal (Chaos) solves it, the bodies,
  masses and joint limits come from the game's `.phy`, what game logic observes stays retail's.
  0014 keeps the death impulse, `prop_ragdoll`, joint friction, the full-corpus rollout.
- **K1 (V3):** a cine refuses a save while it possesses an NPC, until V6. The admission barrier is
  V6's; `UpdateIdealState` and its tests are V9's.
- **Git:** commit on `spec-0002/step-2`, once per green wave; never push.
- **When the owner asks "what is pending":** only what blocks the next task; no commits or
  pushes, no "what's next".

## How the work runs (the method that held)

- One checkout, no worktrees. A wave = ≤3 coders on disjoint files named in their briefs, then
  one integrator who builds, tests, updates records and commits once. Coders never build.
- **Launch coders only after the previous integrator's build has finished**, so half-written edits
  never enter a build. Read-only agents (readers, judges, planners) can run beside anything.
- **Integrators build with the arm tier in from the start** (`uv run elysium build --arm`):
  running arm prefixes on a plain build compiles the tier and spends a build. Cap: two builds; a
  third only for test-setup failures with a known, stated cause.
- Integrators **stage by explicit path** (never `git add -A`), put the verdict table in the commit
  message (a hook refuses report files such as `report-*.md`), tick the story only if its
  acceptance is met, and regenerate the kernel ledger before the build when it is stale.
- Briefs are files; a brief carries: retail first with addresses; the query budget (10 s warns,
  60 s stops; never a file over ~200 KB whole; no greps inside binary bakes); text through
  Grep / Read / Glob; a build, bake or run waited on by blocking or its completion notification,
  never a sleep or a polling loop; "do not commit" for coders.
- A record states what retail does. A red record is classified (record error / harness / game
  red), never loosened. A game red is placed on a story. Records that moved are explained in the
  commit message.
- Line numbers in briefs go stale after every wave: tell every coder to re-locate by Grep.
- Check a silent agent after ~30 minutes by its files' modification times; if it has written
  nothing, message it once, then stop and relaunch it with a tighter brief.
- Report to the owner in the concise style: lead with the result; tables for facts; say what is
  verified and what is inferred.

## Open items a new session will meet

- **N19** — a missed sequence lookup must play the model's sequence 0 (the bake carries
  `RawIndex`; no re-bake): on the judge's list, likely V4a lane A2.
- **Q-V3d1** (V4) — whether a line's gesture plays over the base sequence or replaces it.
- Seven production readers test two of `IsInDialog`'s four retail terms (suggested V7).
- Unrecovered seams listed in the triage: the dialogue's `+0x498` byte, `ShowPlayerChoices`, the
  silent close, `CDialog::Release` on the owner's death, dormancy's idle write; feed and explosion
  deaths; bone placement at `CineCleanup`.
- R2 filings: six other maps' road slabs and every other baked map's door cuts (at their next
  bake); N17 (`max_npcs` 0 on 9 shipped place rows); N12 (`TASK_WALK_RUN_PATH`'s `nav+0x14`).
- H11 (`rollcall_vzombie`: the schedule tap misses a spawn-time install); harness gaps H6–H15 on
  demand.
- `Npc.TravelSpeed`'s remaining patrol / gait parts: V4 lane B1 judges them.
